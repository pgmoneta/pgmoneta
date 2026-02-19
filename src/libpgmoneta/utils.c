/*
 * Copyright (C) 2026 The pgmoneta community
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <aes.h>
#include <compression.h>
#include <deque.h>
#include <gzip_compression.h>
#include <info.h>
#include <logging.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <arpa/inet.h>
#include <sched.h>
#include <sys/resource.h>

#ifndef EVBACKEND_LINUXAIO
#define EVBACKEND_LINUXAIO 0x00000040U
#endif

#ifndef EVBACKEND_IOURING
#define EVBACKEND_IOURING 0x00000080U
#endif

#ifdef HAVE_EXECINFO_H
#include <execinfo.h>
#endif

extern char** environ;
#ifdef HAVE_LINUX
static bool env_changed = false;
static int max_process_title_size = 0;
#endif

static int string_compare(const void* a, const void* b);

static char* get_server_basepath(int server);

static int get_permissions(char* from, int* permissions);

static void do_copy_file(struct worker_common* wc);
static void do_delete_file(struct worker_common* wc);
static bool is_valid_wal_file_suffix(char* f);
bool pgmoneta_is_number(char* str, int base);

int32_t
pgmoneta_get_request(struct message* msg)
{
   if (msg == NULL || msg->data == NULL || msg->length < 8)
   {
      return -1;
   }

   return pgmoneta_read_int32(msg->data + 4);
}

size_t
pgmoneta_get_aligned_size(size_t size)
{
   size_t allocate = 0;

   allocate = size / 512;

   if (size % 512 != 0)
   {
      allocate += 1;
   }

   allocate *= 512;

   return allocate;
}

int
pgmoneta_extract_username_database(struct message* msg, char** username, char** database, char** appname)
{
   int start, end;
   int counter = 0;
   signed char c;
   char** array = NULL;
   size_t size;
   char* un = NULL;
   char* db = NULL;
   char* an = NULL;

   *username = NULL;
   *database = NULL;
   *appname = NULL;

   /* We know where the parameters start, and we know that the message is zero terminated */
   for (int i = 8; i < msg->length - 1; i++)
   {
      c = pgmoneta_read_byte(msg->data + i);
      if (c == 0)
      {
         counter++;
      }
   }

   array = (char**)malloc(sizeof(char*) * counter);

   counter = 0;
   start = 8;
   end = 8;

   for (int i = 8; i < msg->length - 1; i++)
   {
      c = pgmoneta_read_byte(msg->data + i);
      end++;
      if (c == 0)
      {
         array[counter] = (char*)malloc(end - start);
         memset(array[counter], 0, end - start);
         memcpy(array[counter], msg->data + start, end - start);

         start = end;
         counter++;
      }
   }

   for (int i = 0; i < counter; i++)
   {
      if (!strcmp(array[i], "user"))
      {
         size = strlen(array[i + 1]) + 1;
         un = malloc(size);
         memset(un, 0, size);
         memcpy(un, array[i + 1], size);

         *username = un;
      }
      else if (!strcmp(array[i], "database"))
      {
         size = strlen(array[i + 1]) + 1;
         db = malloc(size);
         memset(db, 0, size);
         memcpy(db, array[i + 1], size);

         *database = db;
      }
      else if (!strcmp(array[i], "application_name"))
      {
         size = strlen(array[i + 1]) + 1;
         an = malloc(size);
         memset(an, 0, size);
         memcpy(an, array[i + 1], size);

         *appname = an;
      }
   }

   if (*database == NULL)
   {
      *database = *username;
   }

   pgmoneta_log_trace("Username: %s", *username);
   pgmoneta_log_trace("Database: %s", *database);

   for (int i = 0; i < counter; i++)
   {
      free(array[i]);
   }
   free(array);

   return 0;
}

int
pgmoneta_extract_message(char type, struct message* msg, struct message** extracted)
{
   int offset;
   int m_length;
   struct message* result = NULL;

   offset = 0;
   *extracted = NULL;

   while (result == NULL && offset < msg->length)
   {
      char t = (char)pgmoneta_read_byte(msg->data + offset);

      if (type == t)
      {
         m_length = pgmoneta_read_int32(msg->data + offset + 1);

         result = (struct message*)malloc(sizeof(struct message));
         result->data = aligned_alloc((size_t)ALIGNMENT_SIZE, pgmoneta_get_aligned_size(1 + m_length));

         memcpy(result->data, msg->data + offset, 1 + m_length);

         result->kind = pgmoneta_read_byte(result->data);
         result->length = 1 + m_length;

         *extracted = result;

         return 0;
      }
      else
      {
         // log warning messages
         if (type == 'N')
         {
            struct message* warning_msg = NULL;
            pgmoneta_extract_message_offset(offset, msg->data, &warning_msg);
            pgmoneta_log_notice_response_message(warning_msg);
            pgmoneta_free_message(warning_msg);
         }
         offset += 1;
         offset += pgmoneta_read_int32(msg->data + offset);
      }
   }

   pgmoneta_log_debug("No message with required type %c extracted", type);
   return 1;
}

int
pgmoneta_extract_error_fields(char type, struct message* msg, char** extracted)
{
   ssize_t offset = 1 + 4;
   char* result = NULL;
   *extracted = NULL;

   if (msg == NULL || msg->kind != 'E')
   {
      return 1;
   }

   while (result == NULL && offset < msg->length)
   {
      char t = (char)pgmoneta_read_byte(msg->data + offset);

      if (t == '\0')
      {
         return 1;
      }

      size_t field_len = strlen(msg->data + offset + 1) + 1;

      if (type == t)
      {
         result = (char*)malloc(field_len);
         memset(result, 0, field_len);
         strcpy(result, msg->data + offset + 1);

         *extracted = result;

         return 0;
      }
      else
      {
         offset += 1;
         offset += strlen(msg->data + offset) + 1;
      }
   }

   return 1;
}

size_t
pgmoneta_extract_message_offset(size_t offset, void* data, struct message** extracted)
{
   char type;
   int m_length;
   void* m_data;
   struct message* result = NULL;

   *extracted = NULL;

   type = (char)pgmoneta_read_byte(data + offset);
   m_length = pgmoneta_read_int32(data + offset + 1);

   result = (struct message*)malloc(sizeof(struct message));
   m_data = aligned_alloc((size_t)ALIGNMENT_SIZE, pgmoneta_get_aligned_size(1 + m_length));

   memcpy(m_data, data + offset, 1 + m_length);

   result->kind = type;
   result->length = 1 + m_length;
   result->data = m_data;

   *extracted = result;

   return offset + 1 + m_length;
}

int
pgmoneta_extract_message_from_data(char type, void* data, size_t data_size, struct message** extracted)
{
   size_t offset;
   void* m_data = NULL;
   int m_length;
   struct message* result = NULL;

   offset = 0;
   *extracted = NULL;

   while (result == NULL && offset < data_size)
   {
      char t = (char)pgmoneta_read_byte(data + offset);

      if (type == t)
      {
         m_length = pgmoneta_read_int32(data + offset + 1);

         result = (struct message*)malloc(sizeof(struct message));
         m_data = aligned_alloc((size_t)ALIGNMENT_SIZE, pgmoneta_get_aligned_size(1 + m_length));

         memcpy(m_data, data + offset, 1 + m_length);

         result->kind = pgmoneta_read_byte(m_data);
         result->length = 1 + m_length;
         result->data = m_data;

         *extracted = result;

         return 0;
      }
      else
      {
         // log warning messages
         if (type == 'N')
         {
            struct message* warning_msg = NULL;
            pgmoneta_extract_message_offset(offset, data, &warning_msg);
            pgmoneta_log_notice_response_message(warning_msg);
            pgmoneta_free_message(warning_msg);
         }
         offset += 1;
         offset += pgmoneta_read_int32(data + offset);
      }
   }

   pgmoneta_log_debug("No message with required type %c extracted", type);
   return 1;
}

signed char
pgmoneta_read_byte(void* data)
{
   return (signed char)*((char*)data);
}

uint8_t
pgmoneta_read_uint8(void* data)
{
   return (uint8_t)*((char*)data);
}

int16_t
pgmoneta_read_int16(void* data)
{
   int16_t val;
   memcpy(&val, data, sizeof(val));
   return ntohs(val);
}

uint16_t
pgmoneta_read_uint16(void* data)
{
   uint16_t val;
   memcpy(&val, data, sizeof(val));
   return ntohs(val);
}

int32_t
pgmoneta_read_int32(void* data)
{
   int32_t val;
   memcpy(&val, data, sizeof(val));
   return ntohl(val);
}

uint32_t
pgmoneta_read_uint32(void* data)
{
   uint32_t val;
   memcpy(&val, data, sizeof(val));
   return ntohl(val);
}

int64_t
pgmoneta_read_int64(void* data)
{
   if (pgmoneta_bigendian())
   {
      int64_t val;
      memcpy(&val, data, sizeof(val));
      return val;
   }
   else
   {
      unsigned char* bytes = (unsigned char*)data;
      uint64_t res = ((uint64_t)bytes[0] << 56) |
                     ((uint64_t)bytes[1] << 48) |
                     ((uint64_t)bytes[2] << 40) |
                     ((uint64_t)bytes[3] << 32) |
                     ((uint64_t)bytes[4] << 24) |
                     ((uint64_t)bytes[5] << 16) |
                     ((uint64_t)bytes[6] << 8) |
                     ((uint64_t)bytes[7]);
      return (int64_t)res;
   }
}

uint64_t
pgmoneta_read_uint64(void* data)
{
   if (pgmoneta_bigendian())
   {
      uint64_t val;
      memcpy(&val, data, sizeof(val));
      return val;
   }
   else
   {
      unsigned char* bytes = (unsigned char*)data;
      uint64_t res = ((uint64_t)bytes[0] << 56) |
                     ((uint64_t)bytes[1] << 48) |
                     ((uint64_t)bytes[2] << 40) |
                     ((uint64_t)bytes[3] << 32) |
                     ((uint64_t)bytes[4] << 24) |
                     ((uint64_t)bytes[5] << 16) |
                     ((uint64_t)bytes[6] << 8) |
                     ((uint64_t)bytes[7]);
      return res;
   }
}

bool
pgmoneta_read_bool(void* data)
{
   return (bool)*((bool*)data);
}

void
pgmoneta_write_byte(void* data, signed char b)
{
   *((char*)(data)) = b;
}

void
pgmoneta_write_uint8(void* data, uint8_t b)
{
   *((uint8_t*)(data)) = b;
}

void
pgmoneta_write_int16(void* data, int16_t i)
{
   int16_t n = htons(i);
   memcpy(data, &n, sizeof(n));
}

void
pgmoneta_write_uint16(void* data, uint16_t i)
{
   uint16_t n = htons(i);
   memcpy(data, &n, sizeof(n));
}

void
pgmoneta_write_int32(void* data, int32_t i)
{
   int32_t n = htonl(i);
   memcpy(data, &n, sizeof(n));
}

void
pgmoneta_write_uint32(void* data, uint32_t i)
{
   uint32_t n = htonl(i);
   memcpy(data, &n, sizeof(n));
}

void
pgmoneta_write_int64(void* data, int64_t i)
{
   if (pgmoneta_bigendian())
   {
      memcpy(data, &i, sizeof(i));
   }
   else
   {
      unsigned char* ptr = (unsigned char*)&i;
      unsigned char* out = (unsigned char*)data;
      out[7] = ptr[0];
      out[6] = ptr[1];
      out[5] = ptr[2];
      out[4] = ptr[3];
      out[3] = ptr[4];
      out[2] = ptr[5];
      out[1] = ptr[6];
      out[0] = ptr[7];
   }
}

void
pgmoneta_write_uint64(void* data, uint64_t i)
{
   if (pgmoneta_bigendian())
   {
      memcpy(data, &i, sizeof(i));
   }
   else
   {
      unsigned char* ptr = (unsigned char*)&i;
      unsigned char* out = (unsigned char*)data;
      out[7] = ptr[0];
      out[6] = ptr[1];
      out[5] = ptr[2];
      out[4] = ptr[3];
      out[3] = ptr[4];
      out[2] = ptr[5];
      out[1] = ptr[6];
      out[0] = ptr[7];
   }
}

void
pgmoneta_write_bool(void* data, bool b)
{
   *((bool*)(data)) = b;
}

char*
pgmoneta_read_string(void* data)
{
   return (char*)data;
}

void
pgmoneta_write_string(void* data, char* s)
{
   memcpy(data, s, strlen(s));
}

bool
pgmoneta_compare_string(const char* str1, const char* str2)
{
   if (str1 == NULL && str2 == NULL)
   {
      return true;
   }
   if ((str1 == NULL && str2 != NULL) || (str1 != NULL && str2 == NULL))
   {
      return false;
   }
   return strcmp(str1, str2) == 0;
}

bool
pgmoneta_bigendian(void)
{
   short int word = 0x0001;
   char* b = (char*)&word;
   return (b[0] ? false : true);
}

unsigned int
pgmoneta_swap(unsigned int i)
{
   return ((i << 24) & 0xff000000) |
          ((i << 8) & 0x00ff0000) |
          ((i >> 8) & 0x0000ff00) |
          ((i >> 24) & 0x000000ff);
}

void
pgmoneta_libev_engines(void)
{
   unsigned int engines = ev_supported_backends();

   if (engines & EVBACKEND_SELECT)
   {
      pgmoneta_log_debug("libev available: select");
   }
   if (engines & EVBACKEND_POLL)
   {
      pgmoneta_log_debug("libev available: poll");
   }
   if (engines & EVBACKEND_EPOLL)
   {
      pgmoneta_log_debug("libev available: epoll");
   }
   if (engines & EVBACKEND_LINUXAIO)
   {
      pgmoneta_log_debug("libev available: linuxaio");
   }
   if (engines & EVBACKEND_IOURING)
   {
      pgmoneta_log_debug("libev available: iouring");
   }
   if (engines & EVBACKEND_KQUEUE)
   {
      pgmoneta_log_debug("libev available: kqueue");
   }
   if (engines & EVBACKEND_DEVPOLL)
   {
      pgmoneta_log_debug("libev available: devpoll");
   }
   if (engines & EVBACKEND_PORT)
   {
      pgmoneta_log_debug("libev available: port");
   }
}

unsigned int
pgmoneta_libev(char* engine)
{
   unsigned int engines = ev_supported_backends();

   if (engine)
   {
      if (!strcmp("select", engine))
      {
         if (engines & EVBACKEND_SELECT)
         {
            return EVBACKEND_SELECT;
         }
         else
         {
            pgmoneta_log_warn("libev not available: select");
         }
      }
      else if (!strcmp("poll", engine))
      {
         if (engines & EVBACKEND_POLL)
         {
            return EVBACKEND_POLL;
         }
         else
         {
            pgmoneta_log_warn("libev not available: poll");
         }
      }
      else if (!strcmp("epoll", engine))
      {
         if (engines & EVBACKEND_EPOLL)
         {
            return EVBACKEND_EPOLL;
         }
         else
         {
            pgmoneta_log_warn("libev not available: epoll");
         }
      }
      else if (!strcmp("linuxaio", engine))
      {
         return EVFLAG_AUTO;
      }
      else if (!strcmp("iouring", engine))
      {
         if (engines & EVBACKEND_IOURING)
         {
            return EVBACKEND_IOURING;
         }
         else
         {
            pgmoneta_log_warn("libev not available: iouring");
         }
      }
      else if (!strcmp("devpoll", engine))
      {
         if (engines & EVBACKEND_DEVPOLL)
         {
            return EVBACKEND_DEVPOLL;
         }
         else
         {
            pgmoneta_log_warn("libev not available: devpoll");
         }
      }
      else if (!strcmp("port", engine))
      {
         if (engines & EVBACKEND_PORT)
         {
            return EVBACKEND_PORT;
         }
         else
         {
            pgmoneta_log_warn("libev not available: port");
         }
      }
      else if (!strcmp("auto", engine) || !strcmp("", engine))
      {
         return EVFLAG_AUTO;
      }
      else
      {
         pgmoneta_log_warn("libev unknown option: %s", engine);
      }
   }

   return EVFLAG_AUTO;
}

char*
pgmoneta_libev_engine(unsigned int val)
{
   switch (val)
   {
      case EVBACKEND_SELECT:
         return "select";
      case EVBACKEND_POLL:
         return "poll";
      case EVBACKEND_EPOLL:
         return "epoll";
      case EVBACKEND_LINUXAIO:
         return "linuxaio";
      case EVBACKEND_IOURING:
         return "iouring";
      case EVBACKEND_KQUEUE:
         return "kqueue";
      case EVBACKEND_DEVPOLL:
         return "devpoll";
      case EVBACKEND_PORT:
         return "port";
   }

   return "Unknown";
}

char*
pgmoneta_get_home_directory(void)
{
   char* dir = NULL;

#if defined(HAVE_DARWIN) || defined(HAVE_OSX)
#define GET_ENV(name) getenv(name)
#else
#define GET_ENV(name) secure_getenv(name)
#endif

   dir = pgmoneta_append(dir, GET_ENV("HOME"));

   return dir;
}

char*
pgmoneta_get_user_name(void)
{
   struct passwd* pw = getpwuid(getuid());

   if (pw == NULL)
   {
      return NULL;
   }

   return pw->pw_name;
}

char*
pgmoneta_get_password(void)
{
   char p[MAX_PASSWORD_LENGTH];
   struct termios oldt, newt;
   int i = 0;
   int c;
   char* result = NULL;

   memset(&p, 0, sizeof(p));

   tcgetattr(STDIN_FILENO, &oldt);
   newt = oldt;

   newt.c_lflag &= ~(ECHO);

   tcsetattr(STDIN_FILENO, TCSANOW, &newt);

   while ((c = getchar()) != '\n' && c != EOF && i < MAX_PASSWORD_LENGTH)
   {
      p[i++] = c;
   }
   p[i] = '\0';

   tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

   result = malloc(strlen(p) + 1);
   memset(result, 0, strlen(p) + 1);

   memcpy(result, &p, strlen(p));

   return result;
}

int
pgmoneta_base64_encode(void* raw, size_t raw_length, char** encoded, size_t* encoded_length)
{
   BIO* b64_bio;
   BIO* mem_bio;
   BUF_MEM* mem_bio_mem_ptr;
   char* r = NULL;

   *encoded = NULL;
   *encoded_length = 0;

   if (raw == NULL)
   {
      goto error;
   }

   b64_bio = BIO_new(BIO_f_base64());
   mem_bio = BIO_new(BIO_s_mem());

   BIO_push(b64_bio, mem_bio);
   BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);
   BIO_write(b64_bio, raw, raw_length);
   BIO_flush(b64_bio);

   BIO_get_mem_ptr(mem_bio, &mem_bio_mem_ptr);

   BIO_set_close(mem_bio, BIO_NOCLOSE);
   BIO_free_all(b64_bio);

   BUF_MEM_grow(mem_bio_mem_ptr, (*mem_bio_mem_ptr).length + 1);
   (*mem_bio_mem_ptr).data[(*mem_bio_mem_ptr).length] = '\0';

   r = malloc(strlen((*mem_bio_mem_ptr).data) + 1);
   memset(r, 0, strlen((*mem_bio_mem_ptr).data) + 1);
   memcpy(r, (*mem_bio_mem_ptr).data, strlen((*mem_bio_mem_ptr).data));

   BUF_MEM_free(mem_bio_mem_ptr);

   *encoded = r;
   *encoded_length = strlen(r);

   return 0;

error:

   *encoded = NULL;

   return 1;
}

int
pgmoneta_base64_decode(char* encoded, size_t encoded_length, void** raw, size_t* raw_length)
{
   BIO* b64_bio;
   BIO* mem_bio;
   size_t size;
   char* decoded;
   int index;

   *raw = NULL;
   *raw_length = 0;

   if (encoded == NULL)
   {
      goto error;
   }

   size = (encoded_length * 3) / 4 + 1;
   decoded = malloc(size);
   memset(decoded, 0, size);

   b64_bio = BIO_new(BIO_f_base64());
   mem_bio = BIO_new(BIO_s_mem());

   BIO_write(mem_bio, encoded, encoded_length);
   BIO_push(b64_bio, mem_bio);
   BIO_set_flags(b64_bio, BIO_FLAGS_BASE64_NO_NL);

   index = 0;
   while (0 < BIO_read(b64_bio, decoded + index, 1))
   {
      index++;
   }

   BIO_free_all(b64_bio);

   *raw = (void*)decoded;
   *raw_length = index;

   return 0;

error:

   *raw = NULL;
   *raw_length = 0;

   return 1;
}

void
pgmoneta_set_proc_title(int argc, char** argv, char* s1, char* s2)
{
   char title[MAX_PROCESS_TITLE_LENGTH];
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;
   memset(&title, 0, sizeof(title));

   // sanity check: if the user does not want to
   // update the process title, do nothing
   if (config->update_process_title == UPDATE_PROCESS_TITLE_NEVER)
   {
      return;
   }
#ifdef HAVE_LINUX
   size_t size;
   char** env = environ;
   int es = 0;

   // compute how long was the command line
   // when the application was started
   if (max_process_title_size == 0)
   {
      char* end_of_area = NULL;

      /* Walk argv */
      for (int i = 0; i < argc; i++)
      {
         if (i == 0 || end_of_area + 1 == argv[i])
         {
            end_of_area = argv[i] + strlen(argv[i]);
         }
      }

      /* Walk original environ */
      if (end_of_area != NULL)
      {
         for (int i = 0; env[i] != NULL; i++)
         {
            if (end_of_area + 1 == env[i])
            {
               end_of_area = env[i] + strlen(env[i]);
            }
         }

         max_process_title_size = end_of_area - argv[0];
      }
   }

   if (!env_changed)
   {
      for (int i = 0; env[i] != NULL; i++)
      {
         es++;
      }

      environ = (char**)malloc(sizeof(char*) * (es + 1));
      if (environ == NULL)
      {
         return;
      }

      for (int i = 0; env[i] != NULL; i++)
      {
         size = strlen(env[i]);
         environ[i] = (char*)malloc(size + 1);

         if (environ[i] == NULL)
         {
            return;
         }

         memset(environ[i], 0, size + 1);
         memcpy(environ[i], env[i], size);
      }
      environ[es] = NULL;
      env_changed = true;
   }

   // compose the new title
   snprintf(title, sizeof(title) - 1, "pgmoneta: %s%s%s",
            s1 != NULL ? s1 : "",
            s1 != NULL && s2 != NULL ? "/" : "",
            s2 != NULL ? s2 : "");

   // nuke the command line info
   memset(*argv, 0, max_process_title_size);

   // copy the new title over argv checking
   // the update_process_title policy
   if (config->update_process_title == UPDATE_PROCESS_TITLE_STRICT)
   {
      size = max_process_title_size;
   }
   else
   {
      // here we can set the title to a full description
      size = strlen(title) + 1;
   }

   memcpy(*argv, title, size);
   memset(*argv + size, 0, 1);

   // keep track of how long the title is now
   max_process_title_size = size;
#elif defined(HAVE_OSX)
   // compose the new title
   snprintf(title, sizeof(title), "pgmoneta: %s%s%s",
            s1 != NULL ? s1 : "",
            s1 != NULL && s2 != NULL ? "/" : "",
            s2 != NULL ? s2 : "");

   // set the program name using setprogname (macOS API)
   setprogname(title);
#else
   setproctitle("-pgmoneta: %s%s%s",
                s1 != NULL ? s1 : "",
                s1 != NULL && s2 != NULL ? "/" : "",
                s2 != NULL ? s2 : "");

#endif
}

unsigned int
pgmoneta_version_as_number(unsigned int major, unsigned int minor, unsigned int patch)
{
   return (patch % 100) + (minor % 100) * 100 + (major % 100) * 10000;
}

unsigned int
pgmoneta_version_number(void)
{
   return pgmoneta_version_as_number(PGMONETA_MAJOR_VERSION,
                                     PGMONETA_MINOR_VERSION,
                                     PGMONETA_PATCH_VERSION);
}

bool
pgmoneta_version_ge(unsigned int major, unsigned int minor, unsigned int patch)
{
   if (pgmoneta_version_number() >= pgmoneta_version_as_number(major, minor, patch))
   {
      return true;
   }
   else
   {
      return false;
   }
}

int
pgmoneta_mkdir(char* dir)
{
   char* p;

   for (p = dir + 1; *p; p++)
   {
      if (*p == '/')
      {
         *p = '\0';

         if (mkdir(dir, S_IRWXU) != 0)
         {
            if (errno != EEXIST)
            {
               return 1;
            }

            errno = 0;
         }

         *p = '/';
      }
   }

   if (mkdir(dir, S_IRWXU) != 0)
   {
      if (errno != EEXIST)
      {
         return 1;
      }

      errno = 0;
   }

   return 0;
}

static void
append_bounded(char** out, const char* s, size_t current_len, size_t cap)
{
   if (s == NULL || cap <= current_len)
   {
      return;
   }

   size_t remain = cap - current_len;
   size_t slen = strlen(s);
   size_t to_copy = (slen > remain) ? remain : slen;

   if (to_copy == 0)
   {
      return;
   }

   char* chunk = (char*)malloc(to_copy + 1);
   if (chunk == NULL)
   {
      return;
   }

   memcpy(chunk, s, to_copy);
   chunk[to_copy] = '\0';
   *out = pgmoneta_append(*out, chunk);
   free(chunk);
}

static void
append_char_bounded(char** out, char c, size_t current_len, size_t cap)
{
   if (current_len < cap)
   {
      *out = pgmoneta_append_char(*out, c);
   }
}

static int
hvsnprintf(char* buf, size_t n, const char* fmt, va_list ap)
{
   size_t cap = 8192;
   if (n > 0 && (n - 1) < cap)
   {
      cap = n - 1;
   }

   char* out = NULL;
   const char* p = (fmt != NULL) ? fmt : "";
   char scratch[128];

   while (*p != '\0')
   {
      if (*p != '%')
      {
         size_t cur = (out != NULL) ? strlen(out) : 0;
         append_char_bounded(&out, *p, cur, cap);
         p++;
         continue;
      }

      p++;
      if (*p == '%')
      {
         size_t cur = (out != NULL) ? strlen(out) : 0;
         append_char_bounded(&out, '%', cur, cap);
         p++;
         continue;
      }

      /* Parse flags (support '0' for zero-padding) */
      bool flag_zero = false;
      while (*p == '0')
      {
         flag_zero = true;
         p++;
      }

      /* Parse width */
      int width = -1;
      if (isdigit((unsigned char)*p))
      {
         width = 0;
         while (isdigit((unsigned char)*p))
         {
            width = width * 10 + (*p - '0');
            p++;
         }
      }

      /* Parse precision */
      int precision = -1;
      if (*p == '.')
      {
         p++;
         if (*p == '*')
         {
            precision = va_arg(ap, int);
            p++;
         }
         else
         {
            precision = 0;
            while (isdigit((unsigned char)*p))
            {
               precision = precision * 10 + (*p - '0');
               p++;
            }
         }
         if (precision < 0)
         {
            precision = -1;
         }
      }

      /* Length modifier */
      enum { LM_NONE,
             LM_L,
             LM_LL,
             LM_Z } lm = LM_NONE;
      if (*p == 'l')
      {
         p++;
         if (*p == 'l')
         {
            lm = LM_LL;
            p++;
         }
         else
         {
            lm = LM_L;
         }
      }
      else if (*p == 'z')
      {
         lm = LM_Z;
         p++;
      }

      char conv = *p;
      if (conv == '\0')
      {
         break;
      }
      p++;

      scratch[0] = '\0';

      switch (conv)
      {
         case 's':
         {
            char* s = va_arg(ap, char*);
            if (s == NULL)
            {
               s = "(null)";
            }
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, s, cur, cap);
            break;
         }
         case 'c':
         {
            int ch = va_arg(ap, int);
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_char_bounded(&out, (char)ch, cur, cap);
            break;
         }
         case 'd':
         case 'i':
         {
            long long v;
            if (lm == LM_LL)
            {
               v = va_arg(ap, long long);
            }
            else if (lm == LM_L)
            {
               v = va_arg(ap, long);
            }
            else if (lm == LM_Z)
            {
               v = (ssize_t)va_arg(ap, ssize_t);
            }
            else
            {
               v = va_arg(ap, int);
            }

            if (width >= 0)
            {
               (void)snprintf(scratch, sizeof(scratch),
                              flag_zero ? "%0*lld" : "%*lld", width, v);
            }
            else
            {
               (void)snprintf(scratch, sizeof(scratch), "%lld", v);
            }
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'u':
         {
            unsigned long long v;
            if (lm == LM_LL)
            {
               v = va_arg(ap, unsigned long long);
            }
            else if (lm == LM_L)
            {
               v = va_arg(ap, unsigned long);
            }
            else if (lm == LM_Z)
            {
               v = (size_t)va_arg(ap, size_t);
            }
            else
            {
               v = va_arg(ap, unsigned int);
            }

            if (width >= 0)
            {
               (void)snprintf(scratch, sizeof(scratch),
                              flag_zero ? "%0*llu" : "%*llu", width, v);
            }
            else
            {
               (void)snprintf(scratch, sizeof(scratch), "%llu", v);
            }
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'x':
         case 'X':
         {
            unsigned long long v;
            if (lm == LM_LL)
            {
               v = va_arg(ap, unsigned long long);
            }
            else if (lm == LM_L)
            {
               v = va_arg(ap, unsigned long);
            }
            else if (lm == LM_Z)
            {
               v = (size_t)va_arg(ap, size_t);
            }
            else
            {
               v = va_arg(ap, unsigned int);
            }

            if (width >= 0)
            {
               if (conv == 'x')
               {
                  (void)snprintf(scratch, sizeof(scratch),
                                 flag_zero ? "%0*llx" : "%*llx", width, v);
               }
               else
               {
                  (void)snprintf(scratch, sizeof(scratch),
                                 flag_zero ? "%0*llX" : "%*llX", width, v);
               }
            }
            else
            {
               (void)snprintf(scratch, sizeof(scratch),
                              (conv == 'x') ? "%llx" : "%llX", v);
            }

            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'p':
         {
            void* ptr = va_arg(ap, void*);
            (void)snprintf(scratch, sizeof(scratch), "%p", ptr);
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         case 'f':
         case 'F':
         case 'g':
         case 'G':
         case 'e':
         case 'E':
         {
            double dv = va_arg(ap, double);
            (void)snprintf(scratch, sizeof(scratch), "%g", dv);
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_bounded(&out, scratch, cur, cap);
            break;
         }
         default:
         {
            size_t cur = (out != NULL) ? strlen(out) : 0;
            append_char_bounded(&out, '%', cur, cap);
            cur = (out != NULL) ? strlen(out) : 0;
            append_char_bounded(&out, conv, cur, cap);
            break;
         }
      }
   }

   size_t produced_len = (out != NULL) ? strlen(out) : 0;

   if (buf != NULL && n > 0)
   {
      size_t to_copy = (produced_len < (n - 1)) ? produced_len : (n - 1);
      if (to_copy > 0 && out != NULL)
      {
         memcpy(buf, out, to_copy);
      }
      if (n > 0)
      {
         buf[to_copy] = '\0';
      }
   }

   if (out != NULL)
   {
      free(out);
   }

   return (int)produced_len;
}

int
pgmoneta_snprintf(char* buf, size_t n, const char* fmt, ...)
{
   va_list ap;
   va_list ap_copy;
   int ret;

   va_start(ap, fmt);
   va_copy(ap_copy, ap);
   ret = vsnprintf(buf, n, fmt, ap_copy);
   va_end(ap_copy);
   if (ret < 0)
   {
      ret = hvsnprintf(buf, n, fmt, ap);
   }
   va_end(ap);

   return ret;
}

char*
pgmoneta_append(char* orig, char* s)
{
   size_t orig_length;
   size_t s_length;
   char* n = NULL;

   if (s == NULL)
   {
      return orig;
   }

   s_length = strlen(s);

   if (s_length == 0)
   {
      return orig;
   }

   if (orig != NULL)
   {
      orig_length = strlen(orig);
   }
   else
   {
      orig_length = 0;
   }

   n = (char*)realloc(orig, orig_length + s_length + 1);

   if (n == NULL)
   {
      return orig;
   }

   memcpy(n + orig_length, s, s_length);

   n[orig_length + s_length] = '\0';

   return n;
}

char*
pgmoneta_append_char(char* orig, char c)
{
   char str[2];

   memset(&str[0], 0, sizeof(str));
   snprintf(&str[0], 2, "%c", c);
   orig = pgmoneta_append(orig, str);

   return orig;
}

char*
pgmoneta_append_int(char* orig, int i)
{
   char number[12];

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 11, "%d", i);
   orig = pgmoneta_append(orig, number);

   return orig;
}

char*
pgmoneta_append_ulong(char* orig, unsigned long l)
{
   char number[21];

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 20, "%lu", l);
   orig = pgmoneta_append(orig, number);

   return orig;
}

char*
pgmoneta_append_double(char* orig, double d)
{
   char number[21];

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 20, "%lf", d);
   orig = pgmoneta_append(orig, number);

   return orig;
}

char*
pgmoneta_append_double_precision(char* orig, double d, int precision)
{
   char number[21];

   char* format = NULL;
   format = pgmoneta_append_char(format, '%');
   format = pgmoneta_append_char(format, '.');
   format = pgmoneta_append_int(format, precision);
   format = pgmoneta_append_char(format, 'f');

   memset(&number[0], 0, sizeof(number));
   snprintf(&number[0], 20, format, d);
   orig = pgmoneta_append(orig, number);

   free(format);

   return orig;
}

char*
pgmoneta_append_bool(char* orig, bool b)
{
   if (b)
   {
      orig = pgmoneta_append(orig, "true");
   }
   else
   {
      orig = pgmoneta_append(orig, "false");
   }

   return orig;
}

char*
pgmoneta_remove_whitespace(char* orig)
{
   size_t length;
   char c = 0;
   char* result = NULL;

   if (orig == NULL || strlen(orig) == 0)
   {
      return orig;
   }

   length = strlen(orig);

   for (size_t i = 0; i < length; i++)
   {
      c = *(orig + i);
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      {
         /* Skip */
      }
      else
      {
         result = pgmoneta_append_char(result, c);
      }
   }

   return result;
}

char*
pgmoneta_remove_prefix(char* orig, char* prefix)
{
   char* res = NULL;
   int idx = 0;
   int len1 = strlen(orig);
   int len2 = strlen(prefix);
   int len = 0;
   if (orig == NULL)
   {
      return NULL;
   }
   // make a copy of the original one
   if (prefix == NULL)
   {
      res = pgmoneta_append(res, orig);
      return res;
   }
   while (idx < len1 && idx < len2)
   {
      if (orig[idx] == prefix[idx])
      {
         idx++;
      }
   }
   len = len1 - idx + 1;
   res = malloc(len);
   res[len - 1] = 0;
   if (len > 1)
   {
      strcpy(res, orig + idx);
   }
   return res;
}

char*
pgmoneta_remove_suffix(char* orig, char* suffix)
{
   char* new_str = NULL;
   if (orig == NULL)
   {
      return new_str;
   }

   if (pgmoneta_ends_with(orig, suffix))
   {
      new_str = (char*)malloc(strlen(orig) - strlen(suffix) + 1);

      if (new_str != NULL)
      {
         memset(new_str, 0, strlen(orig) - strlen(suffix) + 1);
         memcpy(new_str, orig, strlen(orig) - strlen(suffix));
      }
   }
   else
   {
      new_str = pgmoneta_append(new_str, orig);
   }

   return new_str;
}

unsigned long
pgmoneta_directory_size(char* directory)
{
   unsigned long total_size = 0;
   DIR* dir;
   struct dirent* entry;
   char* p;
   struct stat st;
   unsigned long l;

   if (directory == NULL || strlen(directory) == 0)
   {
      pgmoneta_log_error("Empty directory");
      return 0;
   }

   if (!(dir = opendir(directory)))
   {
      errno = 0;
      return total_size;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

         total_size += pgmoneta_directory_size(path);
      }
      else if (entry->d_type == DT_REG)
      {
         p = NULL;

         p = pgmoneta_append(p, directory);
         p = pgmoneta_append(p, "/");
         p = pgmoneta_append(p, entry->d_name);

         memset(&st, 0, sizeof(struct stat));

         stat(p, &st);

         l = st.st_size / st.st_blksize;

         if (st.st_size % st.st_blksize != 0)
         {
            l += 1;
         }

         total_size += (l * st.st_blksize);

         free(p);
      }
      else if (entry->d_type == DT_LNK)
      {
         p = NULL;

         p = pgmoneta_append(p, directory);
         p = pgmoneta_append(p, "/");
         p = pgmoneta_append(p, entry->d_name);

         memset(&st, 0, sizeof(struct stat));

         stat(p, &st);

         total_size += st.st_blksize;

         free(p);
      }
   }

   closedir(dir);

   return total_size;
}

unsigned long
pgmoneta_calculate_wal_size(char* directory, char* start)
{
   unsigned long total_size = 0;
   struct deque* wal_files = NULL;
   struct deque_iterator* iter = NULL;
   char* basename = NULL;
   char* path = NULL;
   char* filename = NULL;

   if (pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, directory, false, &wal_files))
   {
      return 0;
   }

   pgmoneta_deque_iterator_create(wal_files, &iter);
   while (pgmoneta_deque_iterator_next(iter))
   {
      filename = (char*)pgmoneta_value_data(iter->value);

      if (pgmoneta_is_encrypted(filename))
      {
         pgmoneta_strip_extension(filename, &basename);
      }
      else
      {
         basename = strdup(filename);
      }

      if (pgmoneta_is_compressed(basename))
      {
         char* bn = basename;
         basename = NULL;
         pgmoneta_strip_extension(bn, &basename);
         free(bn);
      }

      if (strcmp(basename, start) >= 0)
      {
         path = pgmoneta_append(path, directory);
         if (!pgmoneta_ends_with(path, "/"))
         {
            path = pgmoneta_append(path, "/");
         }
         path = pgmoneta_append(path, filename);

         total_size += pgmoneta_get_file_size(path);
         free(path);
         path = NULL;
      }
      free(basename);
      basename = NULL;
   }

   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(wal_files);

   return total_size;
}

int
pgmoneta_get_directories(char* base, int* number_of_directories, char*** dirs)
{
   char* d = NULL;
   char** array = NULL;
   int nod = 0;
   int n = 0;
   DIR* dir = NULL;
   struct dirent* entry = NULL;

   *number_of_directories = 0;
   *dirs = NULL;

   nod = 0;

   if (base == NULL || !strcmp(base, ""))
   {
      goto error;
   }

   if (!(dir = opendir(base)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         nod++;
      }
   }

   closedir(dir);
   dir = NULL;

   if (nod > 0)
   {
      dir = opendir(base);
      array = (char**)malloc(sizeof(char*) * nod);
      n = 0;

      while ((entry = readdir(dir)) != NULL)
      {
         if (entry->d_type == DT_DIR)
         {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
               continue;
            }

            array[n] = (char*)malloc(strlen(entry->d_name) + 1);
            memset(array[n], 0, strlen(entry->d_name) + 1);
            memcpy(array[n], entry->d_name, strlen(entry->d_name));
            n++;
         }
      }

      closedir(dir);
      dir = NULL;

      pgmoneta_sort(nod, array);
   }

   free(d);
   d = NULL;

   *number_of_directories = nod;
   *dirs = array;

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   for (int i = 0; i < nod; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   *number_of_directories = 0;
   *dirs = NULL;

   return 1;
}

int
pgmoneta_delete_directory(char* path)
{
   DIR* d = opendir(path);
   size_t path_len = strlen(path);
   int r = -1;
   int r2 = -1;
   char* buf;
   size_t len;
   struct dirent* entry;

   if (d)
   {
      r = 0;
      while (!r && (entry = readdir(d)))
      {
         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

         len = path_len + strlen(entry->d_name) + 2;
         buf = malloc(len);

         if (buf)
         {
            struct stat statbuf;

            snprintf(buf, len, "%s/%s", path, entry->d_name);
            if (!lstat(buf, &statbuf))
            {
               if (S_ISDIR(statbuf.st_mode))
               {
                  r2 = pgmoneta_delete_directory(buf);
               }
               else
               {
                  r2 = unlink(buf);
               }
            }
            free(buf);
         }
         r = r2;
      }
      closedir(d);
   }

   if (!r)
   {
      r = rmdir(path);
   }

   return r;
}

int
pgmoneta_get_files(uint32_t file_type_mask, char* base, bool recursive, struct deque** files)
{
   DIR* dir = NULL;
   struct dirent* entry = NULL;
   struct deque* array = NULL;
   char full_path[MAX_PATH];

   if (*files == NULL)
   {
      pgmoneta_deque_create(false, &array);
   }
   else
   {
      array = *files;
   }

   if (base == NULL)
   {
      goto error;
   }

   if (!(dir = opendir(base)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      {
         continue;
      }

      pgmoneta_snprintf(full_path, sizeof(full_path), "%s/%s", base, entry->d_name);

      if (entry->d_type == DT_DIR && recursive)
      {
         if (pgmoneta_get_files(file_type_mask, full_path, recursive, &array))
         {
            goto error;
         }
      }
      else if (entry->d_type == DT_REG)
      {
         uint32_t file_type = pgmoneta_get_file_type(full_path);

         if (file_type_mask == PGMONETA_FILE_TYPE_ALL || (file_type & file_type_mask))
         {
            if (recursive)
            {
               /* Recursive: return full path with pgmoneta_append (caller must free) */
               char* path_copy = NULL;
               path_copy = pgmoneta_append(path_copy, full_path);
               if (path_copy == NULL)
               {
                  goto error;
               }
               if (pgmoneta_deque_add(array, path_copy, (uintptr_t)path_copy, ValueString))
               {
                  free(path_copy);
                  goto error;
               }
               free(path_copy);
            }
            else
            {
               /* Non-recursive: return basename directly (backward compatible) */
               if (pgmoneta_deque_add(array, entry->d_name, (uintptr_t)entry->d_name, ValueString))
               {
                  goto error;
               }
            }
         }
      }
   }

   pgmoneta_deque_sort(array);

   *files = array;

   closedir(dir);
   dir = NULL;

   return 0;

error:
   if (dir != NULL)
   {
      closedir(dir);
   }

   if (*files == NULL)
   {
      pgmoneta_deque_destroy(array);
   }

   return 1;
}

int
pgmoneta_extract_file(char* file_path, char* destination)
{
   uint32_t file_type = 0;
   char* archive_path = NULL;
   char* extracted_path = NULL;
   char* previous_archive = NULL;
   struct archive* a = NULL;
   struct archive_entry* entry = NULL;
   bool is_generated_archive = false;
   la_int64_t entry_size = 0;
   uint64_t extracted_size = 0;
   unsigned long free_space = 0;

   if (file_path == NULL || destination == NULL)
   {
      goto error;
   }

   file_type = pgmoneta_get_file_type(file_path);
   archive_path = pgmoneta_append(archive_path, file_path);

   if (archive_path == NULL)
   {
      goto error;
   }

   /* Layer 1: Handle encryption (.aes) */
   if (file_type & PGMONETA_FILE_TYPE_ENCRYPTED)
   {
      if (pgmoneta_strip_extension(archive_path, &extracted_path))
      {
         goto error;
      }

      if (pgmoneta_decrypt_file(archive_path, extracted_path))
      {
         goto error;
      }

      previous_archive = archive_path;
      archive_path = extracted_path;
      extracted_path = NULL;

      if (is_generated_archive)
      {
         remove(previous_archive);
      }
      free(previous_archive);
      previous_archive = NULL;

      is_generated_archive = true;
   }

   /* Layer 2: Handle compression (.gz, .zstd, .lz4, .bz2, .tgz) */
   if (file_type & PGMONETA_FILE_TYPE_COMPRESSED)
   {
      if (pgmoneta_ends_with(archive_path, ".tgz"))
      {
         if (pgmoneta_strip_extension(archive_path, &extracted_path))
         {
            goto error;
         }

         extracted_path = pgmoneta_append(extracted_path, ".tar");
      }
      else if (pgmoneta_strip_extension(archive_path, &extracted_path))
      {
         goto error;
      }

      if (extracted_path == NULL)
      {
         goto error;
      }

      if (pgmoneta_ends_with(archive_path, ".tgz"))
      {
         if (pgmoneta_gunzip_file(archive_path, extracted_path))
         {
            goto error;
         }
      }
      else if (pgmoneta_decompress(archive_path, extracted_path))
      {
         goto error;
      }

      previous_archive = archive_path;
      archive_path = extracted_path;
      extracted_path = NULL;

      if (is_generated_archive)
      {
         remove(previous_archive);
      }
      free(previous_archive);
      previous_archive = NULL;

      is_generated_archive = true;
   }

   /* Verify it's a TAR archive after processing layers */
   if (!(pgmoneta_get_file_type(archive_path) & PGMONETA_FILE_TYPE_TAR))
   {
      pgmoneta_log_error("pgmoneta_extract_file: file is not a TAR archive: %s", file_path);
      goto error;
   }

   /* Layer 3a: Estimate extraction size from TAR headers */
   a = archive_read_new();
   archive_read_support_format_tar(a);

   if (archive_read_open_filename(a, archive_path, 10240) != ARCHIVE_OK)
   {
      pgmoneta_log_error("Failed to open the tar file for reading");
      goto error;
   }

   while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
   {
      entry_size = archive_entry_size(entry);
      if (entry_size > 0)
      {
         if (extracted_size > UINT64_MAX - (uint64_t)entry_size)
         {
            pgmoneta_log_error("Extracted TAR size overflow for file: %s", file_path);
            goto error;
         }
         extracted_size += (uint64_t)entry_size;
      }
   }

   archive_read_close(a);
   archive_read_free(a);
   a = NULL;

   free_space = pgmoneta_free_space(destination);
   if (extracted_size > 0 && (free_space == 0 || extracted_size > (uint64_t)free_space))
   {
      pgmoneta_log_error("Not enough space to extract TAR archive: %s", file_path);
      goto error;
   }

   /* Layer 3b: Extract TAR archive */
   a = archive_read_new();
   archive_read_support_format_tar(a);

   if (archive_read_open_filename(a, archive_path, 10240) != ARCHIVE_OK)
   {
      pgmoneta_log_error("Failed to open the tar file for reading");
      goto error;
   }

   while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
   {
      char dst_file_path[MAX_PATH];
      memset(dst_file_path, 0, sizeof(dst_file_path));
      const char* entry_path = archive_entry_pathname(entry);
      if (pgmoneta_ends_with(destination, "/"))
      {
         pgmoneta_snprintf(dst_file_path, sizeof(dst_file_path), "%s%s", destination, entry_path);
      }
      else
      {
         pgmoneta_snprintf(dst_file_path, sizeof(dst_file_path), "%s/%s", destination, entry_path);
      }

      archive_entry_set_pathname(entry, dst_file_path);
      if (archive_read_extract(a, entry, 0) != ARCHIVE_OK)
      {
         pgmoneta_log_error("Failed to extract entry: %s", archive_error_string(a));
         goto error;
      }
   }

   if (is_generated_archive)
   {
      remove(archive_path);
   }
   free(archive_path);

   archive_read_close(a);
   archive_read_free(a);
   return 0;

error:
   if (is_generated_archive && archive_path)
   {
      remove(archive_path);
   }
   free(archive_path);
   free(extracted_path);

   if (a != NULL)
   {
      archive_read_close(a);
      archive_read_free(a);
   }
   return 1;
}

/**
 * Check if a file has a valid WAL file suffix pattern
 * 
 * Valid patterns are:
 * - 24-char hex (uncompressed)
 * - 24-char hex + .partial
 * - 24-char hex + compression suffix (.gz, .lz4, .zst, .zstd, .bz2)
 * - 24-char hex + compression suffix + .aes
 * 
 * @param f The filename to check
 * @return true if the file has a valid WAL suffix pattern, false otherwise
 */
static bool
is_valid_wal_file_suffix(char* f)
{
   char* valid_suffixes[] = {
      "",
      ".partial",
      ".gz",
      ".lz4",
      ".zstd",
      ".bz2",
      ".gz.aes",
      ".lz4.aes",
      ".zstd.aes",
      ".bz2.aes",
      NULL};

   char path[MAX_PATH];

   memset(path, 0, sizeof(path));
   size_t len = strlen(f);
   if (len >= sizeof(path))
   {
      len = sizeof(path) - 1;
   }
   memcpy(path, f, len);

   char* name = basename(path);

   int name_len = strlen(name);

   for (int i = 0; valid_suffixes[i] != NULL; i++)
   {
      int suffix_len = strlen(valid_suffixes[i]);

      if (suffix_len == 0)
      {
         if (name_len == 24 && pgmoneta_is_wal_file(name))
         {
            return true;
         }
      }
      else
      {
         if (pgmoneta_ends_with(name, valid_suffixes[i]))
         {
            int prefix_len = name_len - suffix_len;
            if (prefix_len == 24)
            {
               char prefix[25];

               memset(prefix, 0, sizeof(prefix));
               memcpy(prefix, name, 24);

               if (pgmoneta_is_wal_file(prefix))
               {
                  return true;
               }
            }
         }
      }
   }

   return false;
}

int
pgmoneta_get_wal_files(char* base, struct deque** files)
{
   DIR* dir;
   struct dirent* entry;
   struct deque* array = NULL;

   if (*files == NULL)
   {
      pgmoneta_deque_create(false, &array);
   }
   else
   {
      array = *files;
   }

   if (!(dir = opendir(base)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type != DT_REG)
      {
         continue;
      }

      if (pgmoneta_ends_with(entry->d_name, ".history"))
      {
         continue;
      }

      if (is_valid_wal_file_suffix(entry->d_name))
      {
         if (pgmoneta_deque_add(array, entry->d_name, (uintptr_t)entry->d_name, ValueString))
         {
            pgmoneta_log_error("WAL: Error adding %s", entry->d_name);
            goto error;
         }
      }
   }

   pgmoneta_deque_sort(array);

   *files = array;

   closedir(dir);

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   pgmoneta_deque_destroy(*files);
   *files = NULL;

   return 1;
}

int
pgmoneta_delete_file(char* file, struct workers* workers)
{
   struct worker_input* fi = NULL;

   if (pgmoneta_create_worker_input(NULL, file, NULL, 0, workers, &fi))
   {
      goto error;
   }

   if (workers != NULL)
   {
      if (workers->outcome)
      {
         pgmoneta_workers_add(workers, do_delete_file, (struct worker_common*)fi);
      }
   }
   else
   {
      do_delete_file((struct worker_common*)fi);
   }

   return 0;

error:

   return 1;
}

static void
do_delete_file(struct worker_common* wc)
{
   struct worker_input* fi = (struct worker_input*)wc;
   int ret = unlink(fi->from);

   if (ret != 0)
   {
      pgmoneta_log_warn("pgmoneta_delete_file: %s (%s)", fi->from, strerror(errno));
      errno = 0;
   }

   free(fi);
}

int
pgmoneta_copy_directory(char* from, char* to, char** restore_last_files_names, struct workers* workers)
{
   DIR* d = opendir(from);
   char* from_buffer;
   char* to_buffer;
   struct dirent* entry;
   struct stat statbuf;

   pgmoneta_mkdir(to);

   if (d)
   {
      while ((entry = readdir(d)))
      {
         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

         from_buffer = NULL;
         to_buffer = NULL;

         from_buffer = pgmoneta_append(from_buffer, from);
         from_buffer = pgmoneta_append(from_buffer, "/");
         from_buffer = pgmoneta_append(from_buffer, entry->d_name);

         to_buffer = pgmoneta_append(to_buffer, to);
         to_buffer = pgmoneta_append(to_buffer, "/");
         to_buffer = pgmoneta_append(to_buffer, entry->d_name);

         if (!stat(from_buffer, &statbuf))
         {
            if (S_ISDIR(statbuf.st_mode))
            {
               pgmoneta_copy_directory(from_buffer, to_buffer, restore_last_files_names, workers);
            }
            else
            {
               bool file_is_excluded = false;
               if (restore_last_files_names != NULL)
               {
                  for (int i = 0; restore_last_files_names[i] != NULL; i++)
                  {
                     file_is_excluded = !strcmp(from_buffer, restore_last_files_names[i]);
                  }
                  if (!file_is_excluded)
                  {
                     pgmoneta_copy_file(from_buffer, to_buffer, workers);
                  }
               }
               else
               {
                  pgmoneta_copy_file(from_buffer, to_buffer, workers);
               }
            }
         }

         free(from_buffer);
         free(to_buffer);
      }
      closedir(d);
   }
   else
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

void
pgmoneta_list_directory(char* directory)
{
   DIR* d = opendir(directory);
   char* current = NULL;
   struct dirent* entry;
   struct stat statbuf;

   if (d)
   {
      while ((entry = readdir(d)))
      {
         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

         current = pgmoneta_append(current, directory);
         if (!pgmoneta_ends_with(current, "/"))
         {
            current = pgmoneta_append_char(current, '/');
         }
         current = pgmoneta_append(current, entry->d_name);

         if (!stat(current, &statbuf))
         {
            if (S_ISDIR(statbuf.st_mode))
            {
               pgmoneta_list_directory(current);
            }
            else
            {
               pgmoneta_log_debug(current);
            }
         }

         free(current);
         current = NULL;
      }
      closedir(d);
   }
   else
   {
      pgmoneta_log_error("%s doesn't exists", directory);
   }
}

static int
get_permissions(char* from, int* permissions)
{
   struct stat from_stat;

   if (stat(from, &from_stat) == -1)
   {
      return 1;
   }

   *permissions = from_stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);

   return 0;
}

int
pgmoneta_copy_file(char* from, char* to, struct workers* workers)
{
   struct worker_input* fi = NULL;

   if (pgmoneta_create_worker_input(NULL, from, to, 0, workers, &fi))
   {
      goto error;
   }

   if (workers != NULL)
   {
      if (workers->outcome)
      {
         pgmoneta_workers_add(workers, do_copy_file, (struct worker_common*)fi);
      }
   }
   else
   {
      do_copy_file((struct worker_common*)fi);
   }

   return 0;

error:

   return 1;
}

static void
do_copy_file(struct worker_common* wc)
{
   struct worker_input* fi = (struct worker_input*)wc;
   struct main_configuration* config = (struct main_configuration*)shmem;
   char* from = NULL;
   int fd_from = -1;
   int fd_to = -1;
   void* buffer = NULL;
   size_t buffer_size = 8192;
   ssize_t nread = -1;
   int permissions = -1;
   char* dn = NULL;
   char* to = NULL;
   bool use_direct_io = false;
   bool aligned_buffer = false;
   int flags_from = O_RDONLY;
   int flags_to = O_WRONLY | O_CREAT | O_TRUNC;
   size_t alignment = 4096;

   /* if the file is partial try for complete file */
   if (!pgmoneta_is_file(fi->from) && pgmoneta_ends_with(fi->from, ".partial"))
   {
      pgmoneta_strip_extension(fi->from, &from);
   }
   else
   {
      from = pgmoneta_append(from, fi->from);
   }

   if (config != NULL && config->direct_io != DIRECT_IO_OFF && config->storage_engine == STORAGE_ENGINE_LOCAL)
   {
#if defined(__linux__)
      /* In auto mode, pre-check O_DIRECT support to avoid unnecessary attempts */
      if (config->direct_io == DIRECT_IO_AUTO)
      {
         char* from_copy = strdup(from);
         if (from_copy != NULL)
         {
            char* test_dir = dirname(from_copy);
            if (!pgmoneta_direct_io_supported(test_dir))
            {
               pgmoneta_log_debug("O_DIRECT not supported on %s, using buffered I/O", test_dir);
               use_direct_io = false;
            }
            else
            {
               use_direct_io = true;
            }
            free(from_copy);
         }
         else
         {
            use_direct_io = false;
         }
      }
      else
      {
         /* DIRECT_IO_ON: attempt O_DIRECT, will fail if unsupported */
         use_direct_io = true;
      }

      if (use_direct_io)
      {
         alignment = pgmoneta_get_block_size(from);
         if (alignment < 512)
         {
            alignment = 512;
         }
         buffer_size = alignment > 8192 ? alignment : 8192;
         /* ensure buffer_size is multiple of alignment */
         if (buffer_size % alignment != 0)
         {
            buffer_size = ((buffer_size / alignment) + 1) * alignment;
         }

         flags_from |= O_DIRECT;
         flags_to |= O_DIRECT;
      }

#else
      /* Non-Linux platforms: O_DIRECT not supported */
      use_direct_io = false;
#endif
   }

   if (use_direct_io)
   {
      buffer = pgmoneta_allocate_aligned(buffer_size, alignment);
      aligned_buffer = true;
   }
   else
   {
      buffer = malloc(buffer_size);
   }

   if (buffer == NULL)
   {
      goto error;
   }

   fd_from = open(from, flags_from);

#if defined(__linux__)
   if (fd_from < 0)
   {
      if (errno == EINVAL && use_direct_io && config != NULL && config->direct_io == DIRECT_IO_AUTO)
      {
         pgmoneta_log_debug("O_DIRECT open failed for %s (EINVAL), falling back to buffered I/O", from);
         use_direct_io = false;
         flags_from &= ~O_DIRECT;
         flags_to &= ~O_DIRECT;
         fd_from = open(from, flags_from);
      }
   }
#endif

   if (fd_from < 0)
   {
      pgmoneta_log_error("File doesn't exists: %s", from);
      goto error;
   }

   if (get_permissions(from, &permissions))
   {
      pgmoneta_log_error("Unable to get file permissions: %s", from);
      goto error;
   }

   to = strdup(fi->to);
   dn = strdup(dirname(fi->to));

   if (pgmoneta_mkdir(dn))
   {
      pgmoneta_log_error("Could not create directory: %s", dn);
      goto error;
   }

   fd_to = open(to, flags_to, permissions);

#if defined(__linux__)
   if (fd_to < 0 && errno == EINVAL && use_direct_io && config != NULL && config->direct_io == DIRECT_IO_AUTO)
   {
      /* Fallback for fd_to */
      pgmoneta_log_debug("O_DIRECT open failed for %s (EINVAL), falling back to buffered I/O", to);
      use_direct_io = false;
      flags_to &= ~O_DIRECT;
      fd_to = open(to, flags_to, permissions);
   }
#endif

   if (fd_to < 0)
   {
      pgmoneta_log_error("Unable to create file: %s", to);
      goto error;
   }

   while ((nread = read(fd_from, buffer, buffer_size)) > 0)
   {
      char* out = (char*)buffer;
      ssize_t nwritten;

#if defined(__linux__)
      if (use_direct_io && (nread % alignment != 0))
      {
         /* Partial block at EOF - O_DIRECT requires aligned I/O.
          * Close and reopen destination without O_DIRECT for tail write. */
         pgmoneta_log_debug("Partial block detected (%zu bytes, alignment %zu), switching to buffered I/O for tail", nread, alignment);
         close(fd_to);
         flags_to &= ~O_DIRECT;
         fd_to = open(to, flags_to | O_APPEND, permissions);
         if (fd_to < 0)
         {
            pgmoneta_log_error("Unable to reopen file for partial block write: %s", to);
            goto error;
         }
         use_direct_io = false;
      }
#endif

      do
      {
         nwritten = write(fd_to, out, nread);

         if (nwritten >= 0)
         {
            nread -= nwritten;
            out += nwritten;
         }
         else if (errno != EINTR)
         {
#if defined(__linux__)
            /* If write fails with EINVAL and we're using O_DIRECT in auto mode,
             * it might be due to alignment. Try fallback. */
            if (errno == EINVAL && use_direct_io && config != NULL && config->direct_io == DIRECT_IO_AUTO)
            {
               pgmoneta_log_debug("O_DIRECT write failed for %s (EINVAL), falling back to buffered I/O", to);
               close(fd_to);
               flags_to &= ~O_DIRECT;
               fd_to = open(to, flags_to | O_APPEND, permissions);
               if (fd_to < 0)
               {
                  goto error;
               }
               use_direct_io = false;
               /* Retry the write */
               continue;
            }
#endif
            goto error;
         }
      }
      while (nread > 0);
   }

   if (nread == 0)
   {
      fsync(fd_to);

      if (close(fd_to) < 0)
      {
         fd_to = -1;
         goto error;
      }
      close(fd_from);
   }

#ifdef DEBUG
   pgmoneta_log_trace("FILETRACKER | Copy | %s | %s |", fi->from, fi->to);
#endif

   if (fi->common.workers != NULL)
   {
      fi->common.workers->outcome = true;
   }

   free(dn);
   free(from);
   free(to);
   if (aligned_buffer)
   {
      pgmoneta_free_aligned(buffer);
   }
   else
   {
      free(buffer);
   }
   free(fi);

   return;

error:

#ifdef DEBUG
   pgmoneta_log_trace("FILETRACKER | Fail | %s | %s | %s |", fi->from, fi->to, strerror(errno));
#endif

   if (fd_from >= 0)
   {
      close(fd_from);
   }
   if (fd_to >= 0)
   {
      close(fd_to);
   }

   errno = 0;

   if (fi->common.workers != NULL)
   {
      fi->common.workers->outcome = false;
   }

   free(dn);
   free(from);
   free(to);
   if (aligned_buffer)
   {
      pgmoneta_free_aligned(buffer);
   }
   else
   {
      free(buffer);
   }
   free(fi);
}

int
pgmoneta_move_file(char* from, char* to)
{
   int ret;

   ret = rename(from, to);
   if (ret != 0)
   {
      pgmoneta_log_warn("pgmoneta_move_file: %s -> %s (%s)", from, to, strerror(errno));
      errno = 0;
      ret = 1;
   }

   return ret;
}

int
pgmoneta_strip_extension(char* s, char** name)
{
   size_t size;
   char* ext = NULL;
   char* r = NULL;

   *name = NULL;

   ext = strrchr(s, '.');
   if (ext != NULL)
   {
      size = ext - s + 1;
      r = (char*)malloc(size);
      if (r == NULL)
      {
         goto error;
      }

      memset(r, 0, size);
      memcpy(r, s, size - 1);
   }
   else
   {
      r = pgmoneta_append(r, s);
      if (r == NULL)
      {
         goto error;
      }
   }

   *name = r;

   return 0;

error:

   return 1;
}

char*
pgmoneta_translate_file_size(uint64_t size)
{
   char* translated_size = NULL;
   double sz = (double)size;
   char* units[] = {"B", "kB", "MB", "GB", "TB", "PB"};
   int i = 0;

   while (sz >= 1024 && i < 6)
   {
      sz /= 1024.0;
      i++;
   }

   translated_size = pgmoneta_append_double_precision(translated_size, sz, 2);
   translated_size = pgmoneta_append(translated_size, units[i]);

   return translated_size;
}

bool
pgmoneta_exists(char* f)
{
   if (f != NULL)
   {
      if (access(f, F_OK) == 0)
      {
         return true;
      }
   }

   return false;
}

bool
pgmoneta_is_directory(char* directory)
{
   struct stat statbuf;

   memset(&statbuf, 0, sizeof(struct stat));

   if (!lstat(directory, &statbuf))
   {
      if (S_ISDIR(statbuf.st_mode))
      {
         return true;
      }
   }

   return false;
}

bool
pgmoneta_is_file(char* file)
{
   struct stat statbuf;

   memset(&statbuf, 0, sizeof(struct stat));

   if (!lstat(file, &statbuf))
   {
      if (S_ISREG(statbuf.st_mode))
      {
         return true;
      }
   }

   return false;
}

bool
pgmoneta_compare_files(char* f1, char* f2)
{
   FILE* fp1 = NULL;
   FILE* fp2 = NULL;
   struct stat statbuf1 = {0};
   struct stat statbuf2 = {0};
   size_t fs1;
   size_t fs2;
   char buf1[8192];
   char buf2[8192];
   size_t cs;
   size_t bs;

   fp1 = fopen(f1, "r");

   if (fp1 == NULL)
   {
      goto error;
   }

   fp2 = fopen(f2, "r");

   if (fp2 == NULL)
   {
      goto error;
   }

   memset(&statbuf1, 0, sizeof(struct stat));
   memset(&statbuf2, 0, sizeof(struct stat));

   if (stat(f1, &statbuf1) != 0)
   {
      errno = 0;
      goto error;
   }

   if (stat(f2, &statbuf2) != 0)
   {
      errno = 0;
      goto error;
   }

   if (statbuf1.st_size != statbuf2.st_size)
   {
      goto error;
   }

   cs = sizeof(char);
   bs = sizeof(buf1);

   while (!feof(fp1))
   {
      fs1 = fread(&buf1[0], cs, bs, fp1);
      fs2 = fread(&buf2[0], cs, bs, fp2);

      if (fs1 != fs2)
      {
         goto error;
      }

      if (memcmp(&buf1[0], &buf2[0], fs1) != 0)
      {
         goto error;
      }
   }

   fclose(fp1);
   fclose(fp2);

   return true;

error:

   if (fp1 != NULL)
   {
      fclose(fp1);
   }

   if (fp2 != NULL)
   {
      fclose(fp2);
   }

   return false;
}

int
pgmoneta_symlink_file(char* from, char* to)
{
   int ret;

   ret = symlink(to, from);

#ifdef DEBUG
   pgmoneta_log_trace("FILETRACKER | Link | %s | %s |", from, to);
#endif

   if (ret != 0)
   {
      pgmoneta_log_debug("pgmoneta_symlink_file: %s -> %s (%s)", from, to, strerror(errno));
      errno = 0;
      ret = 1;
   }

   return ret;
}

int
pgmoneta_symlink_at_file(char* from, char* to)
{
   int dirfd;
   int ret;
   char* dir_path;
   char* ret_path;
   char absolute_path[MAX_PATH];

   dir_path = dirname(strdup(from));
#ifndef HAVE_OSX
   dirfd = open(dir_path, O_DIRECTORY | O_NOFOLLOW);
#elif defined(HAVE_OPENBSD)
   dirfd = open(dir_path, O_PATH | O_DIRECTORY | O_NOFOLLOW);
#else
   dirfd = open(dir_path, O_DIRECTORY | O_NOFOLLOW);
#endif
   if (dirfd == -1)
   {
      pgmoneta_log_debug("Could not open parent directory: %s (%s)", dir_path, strerror(errno));
      errno = 0;
      ret = 1;
   }

   if (!pgmoneta_starts_with(from, "/"))
   {
      memset(absolute_path, 0, sizeof(absolute_path));
      ret_path = realpath(from, absolute_path);
      if (ret_path == NULL)
      {
         return 1;
      }

      ret = symlinkat(to, dirfd, absolute_path);
   }
   else
   {
      ret = symlinkat(to, dirfd, from);
   }

   if (ret != 0)
   {
      pgmoneta_log_debug("pgmoneta_symlink_at_file: %s -> %s (%s)", from, to, strerror(errno));
      errno = 0;
      ret = 1;
   }
   close(dirfd);
   free(dir_path);

   return ret;
}

bool
pgmoneta_is_symlink(char* file)
{
   struct stat statbuf;

   memset(&statbuf, 0, sizeof(struct stat));

   if (!lstat(file, &statbuf))
   {
      if (S_ISLNK(statbuf.st_mode))
      {
         return true;
      }
   }

   return false;
}

char*
pgmoneta_get_symlink(char* symlink)
{
   ssize_t size;
   char link[1024];
   char* result = NULL;

   memset(&link[0], 0, sizeof(link));
   size = readlink(symlink, &link[0], sizeof(link));
   if (size == -1)
   {
      goto error;
   }
   link[size + 1] = '\0';

   if (strlen(&link[0]) == 0)
   {
      goto error;
   }

   result = pgmoneta_append(result, &link[0]);

#ifdef DEBUG
   pgmoneta_log_trace("FILETRACKER | Get | %s | %s |", symlink, result);
#endif

   return result;

error:

#ifdef DEBUG
   pgmoneta_log_trace("FILETRACKER | Get | %s | NULL |", symlink);
#endif

   return NULL;
}

bool
pgmoneta_is_symlink_valid(char* path)
{
   char* link = NULL;
   struct stat buf;
   ssize_t size;
   bool ret = false;

   if (lstat(path, &buf) == 0)
   {
      link = malloc(buf.st_size + 1);
      memset(link, 0, buf.st_size + 1);

      size = readlink(path, link, buf.st_size + 1);
      if (size == -1)
      {
         goto error;
      }
      link[buf.st_size] = '\0';

      if (stat(link, &buf) == 0)
      {
         ret = true;
      }
   }

   free(link);

   return ret;

error:

   free(link);

   return false;
}

int
pgmoneta_copy_wal_files(char* from, char* to, char* start, struct workers* workers)
{
   struct deque* wal_files = NULL;
   struct deque_iterator* it = NULL;
   char* basename = NULL;
   char* ff = NULL;
   char* tf = NULL;

   pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, from, false, &wal_files);

   pgmoneta_deque_iterator_create(wal_files, &it);
   while (pgmoneta_deque_iterator_next(it))
   {
      char* wal_file = (char*)it->value->data;

      if (pgmoneta_is_encrypted(wal_file))
      {
         if (pgmoneta_strip_extension(wal_file, &basename))
         {
            goto error;
         }
      }
      else
      {
         basename = pgmoneta_append(basename, wal_file);
      }

      if (pgmoneta_is_compressed(basename))
      {
         char* bn = basename;
         basename = NULL;
         if (pgmoneta_strip_extension(bn, &basename))
         {
            goto error;
         }
         free(bn);
      }

      if (strcmp(basename, start) >= 0)
      {
         if (pgmoneta_ends_with(basename, ".partial"))
         {
            ff = pgmoneta_append(ff, from);
            if (!pgmoneta_ends_with(ff, "/"))
            {
               ff = pgmoneta_append(ff, "/");
            }
            ff = pgmoneta_append(ff, wal_file);

            tf = pgmoneta_append(tf, to);
            if (!pgmoneta_ends_with(tf, "/"))
            {
               tf = pgmoneta_append(tf, "/");
            }
            tf = pgmoneta_append(tf, basename);
         }
         else
         {
            ff = pgmoneta_append(ff, from);
            if (!pgmoneta_ends_with(ff, "/"))
            {
               ff = pgmoneta_append(ff, "/");
            }
            ff = pgmoneta_append(ff, wal_file);

            tf = pgmoneta_append(tf, to);
            if (!pgmoneta_ends_with(tf, "/"))
            {
               tf = pgmoneta_append(tf, "/");
            }
            tf = pgmoneta_append(tf, wal_file);
         }

         pgmoneta_copy_file(ff, tf, workers);
      }

      free(basename);
      free(ff);
      free(tf);

      basename = NULL;
      ff = NULL;
      tf = NULL;
   }
   pgmoneta_deque_iterator_destroy(it);
   it = NULL;

   pgmoneta_deque_destroy(wal_files);

   return 0;

error:

   pgmoneta_deque_iterator_destroy(it);
   pgmoneta_deque_destroy(wal_files);

   return 1;
}

int
pgmoneta_number_of_wal_files(char* directory, char* from, char* to)
{
   int result;
   struct deque* wal_files = NULL;
   struct deque_iterator* it = NULL;
   char* basename = NULL;

   result = 0;

   pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, directory, false, &wal_files);

   pgmoneta_deque_iterator_create(wal_files, &it);
   while (pgmoneta_deque_iterator_next(it))
   {
      char* wal_file = (char*)it->value->data;

      if (pgmoneta_is_encrypted(wal_file))
      {
         if (pgmoneta_strip_extension(wal_file, &basename))
         {
            goto error;
         }
      }
      else
      {
         basename = pgmoneta_append(basename, wal_file);
      }

      if (pgmoneta_is_compressed(basename))
      {
         char* bn = basename;
         basename = NULL;
         if (pgmoneta_strip_extension(bn, &basename))
         {
            goto error;
         }
         free(bn);
      }

      if (strcmp(basename, from) >= 0)
      {
         if (to == NULL || strcmp(basename, to) < 0)
         {
            result++;
         }
      }

      free(basename);
      basename = NULL;
   }
   pgmoneta_deque_iterator_destroy(it);
   it = NULL;

   pgmoneta_deque_destroy(wal_files);

   return result;

error:

   pgmoneta_deque_iterator_destroy(it);

   pgmoneta_deque_destroy(wal_files);

   return 0;
}

unsigned long
pgmoneta_free_space(char* path)
{
   struct statvfs buf;

   if (statvfs(path, &buf))
   {
      errno = 0;
      return 0;
   }

   return buf.f_bsize * buf.f_bavail;
}

unsigned long
pgmoneta_total_space(char* path)
{
   struct statvfs buf;

   if (statvfs(path, &buf))
   {
      errno = 0;
      return 0;
   }

   return buf.f_frsize * buf.f_blocks;
}

unsigned long
pgmoneta_biggest_file(char* directory)
{
   unsigned long biggest_size = 0;
   DIR* dir;
   char* p;
   unsigned long l;
   unsigned long size = 0;
   struct dirent* entry;
   struct stat st;

   if (!(dir = opendir(directory)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         char path[MAX_PATH];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         memset(&path[0], 0, sizeof(path));
         snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

         size = pgmoneta_biggest_file(path);

         if (size > biggest_size)
         {
            biggest_size = size;
         }
      }
      else if (entry->d_type == DT_REG)
      {
         p = NULL;

         p = pgmoneta_append(p, directory);
         p = pgmoneta_append(p, "/");
         p = pgmoneta_append(p, entry->d_name);

         memset(&st, 0, sizeof(struct stat));

         stat(p, &st);

         l = st.st_size / st.st_blksize;

         if (st.st_size % st.st_blksize != 0)
         {
            l += 1;
         }

         size = (l * st.st_blksize);

         if (size > biggest_size)
         {
            biggest_size = size;
         }

         free(p);
      }
      else if (entry->d_type == DT_LNK)
      {
         p = NULL;

         p = pgmoneta_append(p, directory);
         p = pgmoneta_append(p, "/");
         p = pgmoneta_append(p, entry->d_name);

         memset(&st, 0, sizeof(struct stat));

         stat(p, &st);

         size = st.st_blksize;

         if (size > biggest_size)
         {
            biggest_size = size;
         }

         free(p);
      }
   }

   closedir(dir);

   return biggest_size;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   return 1024 * 1024 * 1024;
}

bool
pgmoneta_starts_with(char* str, char* prefix)
{
   if (str == NULL || prefix == NULL)
   {
      return false;
   }
   return strncmp(prefix, str, strlen(prefix)) == 0;
}

bool
pgmoneta_ends_with(char* str, char* suffix)
{
   int str_len;
   int suffix_len;

   if (str != NULL && suffix != NULL)
   {
      str_len = strlen(str);
      suffix_len = strlen(suffix);

      return (str_len >= suffix_len) && (strcmp(str + (str_len - suffix_len), suffix) == 0);
   }

   return false;
}

bool
pgmoneta_contains(char* str, char* s)
{
   if (str == NULL || s == NULL)
   {
      return false;
   }
   return strstr(str, s) != NULL;
}

char*
pgmoneta_remove_first(char* str)
{
   char* new_str = NULL;
   size_t len;

   if (str == NULL)
   {
      return NULL;
   }

   len = strlen(str);
   if (len == 0)
   {
      return str;
   }

   new_str = (char*)malloc(len);

   if (new_str == NULL)
   {
      goto error;
   }

   memset(new_str, 0, len);
   memcpy(new_str, str + 1, len - 1);

   free(str);

   return new_str;

error:

   return NULL;
}

char*
pgmoneta_remove_last(char* str)
{
   char* new_str = NULL;
   size_t len;

   if (str == NULL)
   {
      return NULL;
   }

   len = strlen(str);
   if (len == 0)
   {
      return str;
   }

   new_str = (char*)malloc(len);

   if (new_str == NULL)
   {
      goto error;
   }

   memset(new_str, 0, len);
   memcpy(new_str, str, len - 1);

   free(str);

   return new_str;

error:

   return NULL;
}

void
pgmoneta_sort(size_t size, char** array)
{
   if (array != NULL)
   {
      qsort(array, size, sizeof(const char*), string_compare);
   }
}

char*
pgmoneta_bytes_to_string(uint64_t bytes)
{
   char* sizes[] = {"EB", "PB", "TB", "GB", "MB", "KB", "B"};
   uint64_t exbibytes = 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
   uint64_t multiplier = exbibytes;
   char* result;

   result = (char*)malloc(sizeof(char) * 20);

   if (result == NULL)
   {
      goto error;
   }

   for (size_t i = 0; i < sizeof(sizes) / sizeof(*(sizes)); i++, multiplier /= 1024)
   {
      if (bytes < multiplier)
      {
         continue;
      }

      if (bytes % multiplier == 0)
      {
         sprintf(result, "%" PRIu64 " %s", bytes / multiplier, sizes[i]);
      }
      else
      {
         sprintf(result, "%.1f %s", (float)bytes / multiplier, sizes[i]);
      }

      return result;
   }

   strcpy(result, "0");
   return result;

error:

   return NULL;
}

int
pgmoneta_read_version(char* directory, char** version)
{
   char* result = NULL;
   char* filename = NULL;
   FILE* file = NULL;
   char buf[3];

   *version = NULL;

   filename = pgmoneta_append(filename, directory);
   filename = pgmoneta_append(filename, "/PG_VERSION");

   file = fopen(filename, "r");
   if (file == NULL)
   {
      goto error;
   }

   memset(&buf[0], 0, sizeof(buf));
   if (fgets(&buf[0], sizeof(buf), file) == NULL)
   {
      goto error;
   }

   result = malloc(strlen(&buf[0]) + 1);

   if (result == NULL)
   {
      goto error;
   }

   memset(result, 0, strlen(&buf[0]) + 1);
   memcpy(result, &buf[0], strlen(&buf[0]));

   *version = result;

   fclose(file);

   free(filename);

   return 0;

error:

   if (file != NULL)
   {
      fclose(file);
   }

   free(filename);

   return 1;
}

bool
pgmoneta_is_wal_file(char* file)
{
   if (strlen(file) != 24)
   {
      return false;
   }

   for (int i = 0; i < 24; i++)
   {
      char c = file[i];
      if (!isxdigit(c))
      {
         return false;
      }
   }

   return true;
}

char*
pgmoneta_wal_file_name(uint32_t tli, size_t segno, int segsize)
{
   char hex[128];
   char* f = NULL;

   memset(&hex[0], 0, sizeof(hex));
   int segments_per_id = 0x100000000ULL / segsize;
   int seg_id = segno / segments_per_id;
   int seg_offset = segno % segments_per_id;

   snprintf(&hex[0], sizeof(hex), "%08X%08X%08X", tli, seg_id, seg_offset);
   f = pgmoneta_append(f, hex);
   return f;
}

int
pgmoneta_read_wal(char* directory, char** wal)
{
   bool found = false;
   char* result = NULL;
   char* pgwal = NULL;

   struct deque* wal_files = NULL;
   struct deque_iterator* it = NULL;

   *wal = NULL;

   pgwal = pgmoneta_append(pgwal, directory);
   pgwal = pgmoneta_append(pgwal, "/pg_wal/");

   pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, pgwal, false, &wal_files);

   if (pgmoneta_deque_size(wal_files) == 0)
   {
      goto error;
   }

   pgmoneta_deque_iterator_create(wal_files, &it);
   while (pgmoneta_deque_iterator_next(it) && !found)
   {
      char* wal_file = (char*)it->value->data;

      if (pgmoneta_is_wal_file(wal_file))
      {
         result = malloc(strlen(wal_file) + 1);
         memset(result, 0, strlen(wal_file) + 1);
         memcpy(result, wal_file, strlen(wal_file));

         *wal = result;

         found = true;
      }
   }
   pgmoneta_deque_iterator_destroy(it);
   it = NULL;

   free(pgwal);
   pgmoneta_deque_destroy(wal_files);

   return 0;

error:

   free(pgwal);
   pgmoneta_deque_iterator_destroy(it);
   pgmoneta_deque_destroy(wal_files);

   return 1;
}

int
pgmoneta_read_checkpoint_info(char* directory, char** chkptpos)
{
   char label[MAX_PATH];
   char buffer[MAX_PATH];
   char* chkpt = NULL;
   FILE* file = NULL;
   int numfields = 0;

   chkpt = (char*)malloc(MISC_LENGTH);

   if (chkpt == NULL)
   {
      goto error;
   }

   memset(chkpt, 0, MISC_LENGTH);
   memset(buffer, 0, sizeof(buffer));
   memset(label, 0, MAX_PATH);
   snprintf(label, MAX_PATH, "%s/backup_label", directory);

   file = fopen(label, "r");
   if (file == NULL)
   {
      pgmoneta_log_error("Unable to open backup_label file: %s", strerror(errno));
      goto error;
   }
   while (fgets(buffer, sizeof(buffer), file) != NULL)
   {
      if (pgmoneta_starts_with(buffer, "CHECKPOINT LOCATION"))
      {
         numfields = sscanf(buffer, "CHECKPOINT LOCATION: %s\n", chkpt);
         if (numfields != 1)
         {
            pgmoneta_log_error("Error parsing checkpoint wal location");
            goto error;
         }

         break;
      }
      memset(buffer, 0, sizeof(buffer));
   }

   *chkptpos = chkpt;

   fclose(file);
   return 0;

error:
   if (file != NULL)
   {
      fclose(file);
   }
   free(chkpt);
   return 1;
}

static int
string_compare(const void* a, const void* b)
{
   return strcmp(*(char**)a, *(char**)b);
}

char*
pgmoneta_get_server(int server)
{
   return get_server_basepath(server);
}

char*
pgmoneta_get_server_backup(int server)
{
   char* d = NULL;

   d = get_server_basepath(server);
   if (d == NULL)
   {
      return NULL;
   }

   d = pgmoneta_append(d, "backup/");

   return d;
}

char*
pgmoneta_get_server_wal(int server)
{
   char* d = NULL;

   d = get_server_basepath(server);
   if (d == NULL)
   {
      return NULL;
   }

   d = pgmoneta_append(d, "wal/");

   return d;
}

char*
pgmoneta_get_server_summary(int server)
{
   char* d = NULL;

   d = get_server_basepath(server);
   if (d == NULL)
   {
      return NULL;
   }

   d = pgmoneta_append(d, "summary/");

   return d;
}

char*
pgmoneta_get_server_wal_shipping(int server)
{
   struct main_configuration* config;
   char* ws = NULL;
   config = (struct main_configuration*)shmem;
   if (server < 0 || server >= config->common.number_of_servers)
   {
      return NULL;
   }
   if (strlen(config->common.servers[server].wal_shipping) > 0)
   {
      ws = pgmoneta_append(ws, config->common.servers[server].wal_shipping);
      if (!pgmoneta_ends_with(ws, "/"))
      {
         ws = pgmoneta_append(ws, "/");
      }
      ws = pgmoneta_append(ws, config->common.servers[server].name);
      return ws;
   }
   return NULL;
}

char*
pgmoneta_get_server_wal_shipping_wal(int server)
{
   char* ws = NULL;
   ws = pgmoneta_get_server_wal_shipping(server);

   if (ws != NULL)
   {
      if (!pgmoneta_ends_with(ws, "/"))
      {
         ws = pgmoneta_append(ws, "/");
      }
      ws = pgmoneta_append(ws, "wal/");
   }
   return ws;
}

char*
pgmoneta_get_server_workspace(int server)
{
   struct main_configuration* config;
   char* ws = NULL;

   config = (struct main_configuration*)shmem;

   if (server < 0 || server >= config->common.number_of_servers)
   {
      goto error;
   }

   if (strlen(config->common.servers[server].workspace) > 0)
   {
      ws = pgmoneta_append(ws, config->common.servers[server].workspace);

      if (!pgmoneta_ends_with(ws, "/"))
      {
         ws = pgmoneta_append_char(ws, '/');
      }
   }
   else if (strlen(config->workspace) > 0)
   {
      ws = pgmoneta_append(ws, config->workspace);

      if (!pgmoneta_ends_with(ws, "/"))
      {
         ws = pgmoneta_append_char(ws, '/');
      }
   }
   else
   {
      ws = pgmoneta_append(ws, "/tmp/pgmoneta-workspace/");
   }

   if (!pgmoneta_exists(ws))
   {
      if (pgmoneta_mkdir(ws))
      {
         pgmoneta_log_error("Could not create directory: %s", ws);
         goto error;
      }
   }

   return ws;

error:

   return NULL;
}

int
pgmoneta_delete_server_workspace(int server, char* label)
{
   char* ws = NULL;

   ws = pgmoneta_get_server_workspace(server);
   if (ws == NULL)
   {
      goto error;
   }

   if (label != NULL && strlen(label) > 0)
   {
      ws = pgmoneta_append(ws, label);
   }

   if (pgmoneta_delete_directory(ws))
   {
      goto error;
   }

   free(ws);

   return 0;

error:

   free(ws);

   return 1;
}

char*
pgmoneta_get_server_backup_identifier(int server, char* identifier)
{
   char* d = NULL;

   d = pgmoneta_get_server_backup(server);
   if (d == NULL)
   {
      return NULL;
   }
   d = pgmoneta_append(d, identifier);
   d = pgmoneta_append(d, "/");

   return d;
}

char*
pgmoneta_get_server_extra_identifier(int server, char* identifier)
{
   char* d = NULL;

   d = pgmoneta_get_server_backup(server);
   if (d == NULL)
   {
      return NULL;
   }
   d = pgmoneta_append(d, identifier);
   d = pgmoneta_append(d, "/extra/");

   return d;
}

char*
pgmoneta_get_server_backup_identifier_data(int server, char* identifier)
{
   char* d = NULL;

   d = pgmoneta_get_server_backup_identifier(server, identifier);
   if (d == NULL)
   {
      return NULL;
   }
   d = pgmoneta_append(d, "data/");

   return d;
}

char*
pgmoneta_get_server_backup_identifier_tablespace(int server, char* identifier, char* name)
{
   char* d = NULL;

   d = pgmoneta_get_server_backup_identifier(server, identifier);
   if (d == NULL)
   {
      return NULL;
   }
   d = pgmoneta_append(d, name);
   d = pgmoneta_append(d, "/");

   return d;
}

char*
pgmoneta_get_server_backup_identifier_data_wal(int server, char* identifier)
{
   char* d = NULL;

   d = pgmoneta_get_server_backup_identifier_data(server, identifier);
   if (d == NULL)
   {
      return NULL;
   }
   d = pgmoneta_append(d, "pg_wal/");

   return d;
}

int
pgmoneta_permission_recursive(char* d)
{
   DIR* dir = opendir(d);
   char* f = NULL;
   struct dirent* entry;
   struct stat statbuf;

   if (dir)
   {
      while ((entry = readdir(dir)))
      {
         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

         f = pgmoneta_append(f, d);
         if (!pgmoneta_ends_with(f, "/"))
         {
            f = pgmoneta_append(f, "/");
         }
         f = pgmoneta_append(f, entry->d_name);

         if (f == NULL)
         {
            goto error;
         }

         if (!stat(f, &statbuf))
         {
            if (S_ISDIR(statbuf.st_mode))
            {
               pgmoneta_permission(f, 7, 0, 0);
               pgmoneta_permission_recursive(f);
            }
            else
            {
               pgmoneta_permission(f, 6, 0, 0);
            }
         }

         free(f);
         f = NULL;
      }

      closedir(dir);
   }

   return 0;

error:

   free(f);

   if (dir != NULL)
   {
      closedir(dir);
   }

   return 1;
}

int
pgmoneta_permission(char* e, int user, int group, int all)
{
   int ret;
   mode_t mode;
   pgmoneta_get_permission_mode(user, group, all, &mode);

   ret = chmod(e, mode);
   if (ret == -1)
   {
      errno = 0;
      ret = 1;
   }

   return ret;
}

int
pgmoneta_get_permission_mode(int user, int group, int all, mode_t* mode)
{
   *mode = 0;

   switch (user)
   {
      case 7:
      {
         *mode = S_IRUSR | S_IWUSR | S_IXUSR;
         break;
      }
      case 6:
      {
         *mode = S_IRUSR | S_IWUSR;
         break;
      }
      case 4:
      {
         *mode = S_IRUSR;
         break;
      }
      default:
      {
         break;
      }
   }

   switch (group)
   {
      case 7:
      {
         *mode = S_IRGRP | S_IWGRP | S_IXGRP;
         break;
      }
      case 6:
      {
         *mode += S_IRGRP | S_IWGRP;
         break;
      }
      case 4:
      {
         *mode += S_IRGRP;
         break;
      }
      default:
      {
         break;
      }
   }

   switch (all)
   {
      case 7:
      {
         *mode = S_IROTH | S_IWOTH | S_IXOTH;
         break;
      }
      case 6:
      {
         *mode += S_IROTH | S_IWOTH;
         break;
      }
      case 4:
      {
         *mode += S_IROTH;
         break;
      }
      default:
      {
         break;
      }
   }
   return 0;
}

mode_t
pgmoneta_get_permission(char* path)
{
   struct stat statbuf;

   stat(path, &statbuf);

   return statbuf.st_mode;
}

static char*
get_server_basepath(int server)
{
   char* d = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (server < 0 || server >= config->common.number_of_servers)
   {
      return NULL;
   }

   d = pgmoneta_append(d, config->base_dir);
   if (!pgmoneta_ends_with(config->base_dir, "/"))
   {
      d = pgmoneta_append(d, "/");
   }
   d = pgmoneta_append(d, config->common.servers[server].name);
   d = pgmoneta_append(d, "/");

   return d;
}

int
pgmoneta_get_timestamp_ISO8601_format(char* short_date, char* long_date)
{
   time_t now = time(&now);
   if (now == -1)
   {
      return 1;
   }

   struct tm* ptm = gmtime(&now);
   if (ptm == NULL)
   {
      return 1;
   }

   if (short_date != NULL)
   {
      strftime(short_date, SHORT_TIME_LENGTH, "%Y%m%d", ptm);
   }

   if (long_date != NULL)
   {
      strftime(long_date, LONG_TIME_LENGTH, "%Y%m%dT%H%M%SZ", ptm);
   }

   return 0;
}

int
pgmoneta_get_timestamp_UTC_format(char* utc_date)
{
   time_t now = time(&now);
   if (now == -1)
   {
      return 1;
   }

   struct tm* ptm = gmtime(&now);
   if (ptm == NULL)
   {
      return 1;
   }

   if (utc_date != NULL)
   {
      strftime(utc_date, UTC_TIME_LENGTH, "%a, %d %b %Y %T GMT", ptm);
   }

   return 0;
}

int64_t
pgmoneta_get_current_timestamp(void)
{
   struct timeval tv;

   gettimeofday(&tv, NULL);
   return tv.tv_sec * (int64_t)1000000 + tv.tv_usec;
}

int64_t
pgmoneta_get_y2000_timestamp(void)
{
   struct tm tm_y2000 = {0};
   time_t y2000;

   tm_y2000.tm_year = 2000 - 1900;
   tm_y2000.tm_mon = 0;
   tm_y2000.tm_mday = 1;

   y2000 = mktime(&tm_y2000);
   y2000 = timegm(localtime(&y2000));

   return y2000 * (int64_t)1000000;
}

double
pgmoneta_compute_duration(struct timespec start_time, struct timespec end_time)
{
   double nano = (double)(end_time.tv_nsec - start_time.tv_nsec);
   double sec = (double)((end_time.tv_sec - start_time.tv_sec) * 1E9);
   return (sec + nano) / (1E9);
}

char*
pgmoneta_get_timestamp_string(struct timespec start_time, struct timespec end_time, double* seconds)
{
   double total_seconds;
   int hours;
   int minutes;
   double sec;
   char elapsed[128];
   char* result = NULL;

   *seconds = 0;

   total_seconds = pgmoneta_compute_duration(start_time, end_time);

   *seconds = total_seconds;

   hours = (int)total_seconds / 3600;
   minutes = ((int)total_seconds % 3600) / 60;
   sec = (int)total_seconds % 60 + (total_seconds - ((long)total_seconds));

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, sec);

   result = pgmoneta_append(result, &elapsed[0]);

   return result;
}

int
pgmoneta_convert_base32_to_hex(unsigned char* base32, int base32_length,
                               unsigned char** hex)
{
   int i = 0;
   char* hex_buf;

   *hex = NULL;

   hex_buf = malloc(base32_length * 2 + 1);

   if (hex_buf == NULL)
   {
      goto error;
   }

   memset(hex_buf, 0, base32_length * 2 + 1);

   for (i = 0; i < base32_length; i++)
   {
      sprintf(&hex_buf[i * 2], "%02x", base32[i]);
   }
   hex_buf[base32_length * 2] = 0;

   *hex = (unsigned char*)hex_buf;

   return 0;

error:

   free(hex_buf);

   return 1;
}

size_t
pgmoneta_get_file_size(char* file_path)
{
   struct stat file_stat;

   if (stat(file_path, &file_stat) != 0)
   {
      pgmoneta_log_warn("pgmoneta_get_file_size: %s (%s)", file_path, strerror(errno));
      errno = 0;
      return 0;
   }

   return file_stat.st_size;
}

int
pgmoneta_copy_and_extract_file(char* from, char** to)
{
   char* new_to = NULL;
   char* old_to = NULL;

   old_to = *to;

   if (pgmoneta_copy_file(from, old_to, NULL))
   {
      goto error;
   }

   if (pgmoneta_is_encrypted(old_to))
   {
      if (pgmoneta_strip_extension(old_to, &new_to))
      {
         goto error;
      }

      if (pgmoneta_decrypt_file(old_to, new_to))
      {
         free(new_to);
         goto error;
      }

      free(old_to);
      old_to = new_to;
      new_to = NULL;
   }

   if (pgmoneta_is_compressed(old_to))
   {
      if (pgmoneta_strip_extension(old_to, &new_to))
      {
         goto error;
      }

      if (pgmoneta_decompress(old_to, new_to))
      {
         free(new_to);
         goto error;
      }

      free(old_to);
      old_to = new_to;
   }

   *to = old_to;

   return 0;
error:
   return 1;
}

bool
pgmoneta_is_encrypted(char* file_path)
{
   if (pgmoneta_ends_with(file_path, ".aes"))
   {
      return true;
   }

   return false;
}

bool
pgmoneta_is_compressed(char* file_path)
{
   if (pgmoneta_ends_with(file_path, ".zstd") ||
       pgmoneta_ends_with(file_path, ".lz4") ||
       pgmoneta_ends_with(file_path, ".bz2") ||
       pgmoneta_ends_with(file_path, ".gz"))
   {
      return true;
   }

   return false;
}

uint32_t
pgmoneta_get_file_type(char* file_path)
{
   uint32_t type = PGMONETA_FILE_TYPE_UNKNOWN;
   char* file_path_copy = NULL;
   char* basename_copy = NULL;
   char* current = NULL;
   char* dot = NULL;

   if (file_path == NULL)
   {
      return type;
   }

   file_path_copy = pgmoneta_append(file_path_copy, file_path);
   if (file_path_copy == NULL)
   {
      return type;
   }

   basename_copy = pgmoneta_append(basename_copy, basename(file_path_copy));
   free(file_path_copy);
   file_path_copy = NULL;
   if (basename_copy == NULL)
   {
      return type;
   }

   current = basename_copy;

   /* Check for encryption suffix first (.aes) */
   if (pgmoneta_ends_with(current, ".aes"))
   {
      type |= PGMONETA_FILE_TYPE_ENCRYPTED;
      current[strlen(current) - 4] = '\0';
   }

   /* Check for compression suffixes - set both generic and specific flags */
   if (pgmoneta_ends_with(current, ".gz"))
   {
      type |= PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_GZIP;
      dot = strrchr(current, '.');
      if (dot != NULL)
      {
         *dot = '\0';
      }
   }
   else if (pgmoneta_ends_with(current, ".lz4"))
   {
      type |= PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_LZ4;
      dot = strrchr(current, '.');
      if (dot != NULL)
      {
         *dot = '\0';
      }
   }
   else if (pgmoneta_ends_with(current, ".zstd"))
   {
      type |= PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_ZSTD;
      dot = strrchr(current, '.');
      if (dot != NULL)
      {
         *dot = '\0';
      }
   }
   else if (pgmoneta_ends_with(current, ".bz2"))
   {
      type |= PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_BZ2;
      dot = strrchr(current, '.');
      if (dot != NULL)
      {
         *dot = '\0';
      }
   }

   /* Check for TAR archive after stripping compression */
   if (pgmoneta_ends_with(current, ".tar"))
   {
      type |= PGMONETA_FILE_TYPE_TAR;
      current[strlen(current) - 4] = '\0';
   }

   /* Check for .tgz (tar.gz shorthand) */
   if (pgmoneta_ends_with(current, ".tgz"))
   {
      type |= PGMONETA_FILE_TYPE_TAR;
      type |= PGMONETA_FILE_TYPE_COMPRESSED | PGMONETA_FILE_TYPE_GZIP;
      current[strlen(current) - 4] = '\0';
   }

   /* Check for partial suffix */
   if (pgmoneta_ends_with(current, ".partial"))
   {
      type |= PGMONETA_FILE_TYPE_PARTIAL;
      current[strlen(current) - 8] = '\0';
   }

   /* Check for WAL file pattern (24-char hex) */
   if (strlen(current) == 24 && pgmoneta_is_wal_file(current))
   {
      type |= PGMONETA_FILE_TYPE_WAL;
   }

   free(basename_copy);

   return type;
}

/* Parser for pgmoneta-cli amd pgmoneta-admin commands */
bool
parse_command(int argc,
              char** argv,
              int offset,
              struct pgmoneta_parsed_command* parsed,
              struct pgmoneta_command command_table[],
              size_t command_count)
{
   char* command = NULL;
   char* subcommand = NULL;
   bool command_match = false;
   int default_command_match = -1;
   int arg_count = -1;
   int command_index = -1;
   int j;

   /* Parse command, and exit if there is no match */
   if (offset < argc)
   {
      command = argv[offset++];
   }
   else
   {
      warnx("A command is required\n");
      return false;
   }

   if (offset < argc)
   {
      subcommand = argv[offset];
   }

   for (size_t i = 0; i < command_count; i++)
   {
      if (strncmp(command, command_table[i].command, MISC_LENGTH) == 0)
      {
         command_match = true;
         if (subcommand && strncmp(subcommand, command_table[i].subcommand, MISC_LENGTH) == 0)
         {
            offset++;
            command_index = i;
            break;
         }
         else if (EMPTY_STR(command_table[i].subcommand))
         {
            /* Default command does not require a subcommand, might be followed by an argument */
            default_command_match = i;
         }
      }
   }

   if (command_match == false)
   {
      warnx("Unknown command '%s'\n", command);
      return false;
   }

   if (command_index == -1 && default_command_match >= 0)
   {
      command_index = default_command_match;
      subcommand = "";
   }
   else if (command_index == -1) /* Command was matched, but subcommand was not */
   {
      if (subcommand)
      {
         warnx("Unknown subcommand '%s' for command '%s'\n", subcommand, command);
      }
      else /* User did not type a subcommand */
      {
         warnx("Command '%s' requires a subcommand\n", command);
      }
      return false;
   }

   parsed->cmd = &command_table[command_index];

   /* Iterate until find an accepted_arg_count that is equal or greater than the typed command arg_count */
   arg_count = argc - offset;
   for (j = 0; j < MISC_LENGTH; j++)
   {
      if (parsed->cmd->accepted_argument_count[j] >= arg_count)
      {
         break;
      }
   }
   if (arg_count < parsed->cmd->accepted_argument_count[0])
   {
      warnx("Too few arguments provided for command '%s%s%s'\n", command,
            (command && !EMPTY_STR(subcommand)) ? " " : "", subcommand);
      return false;
   }
   if (j == MISC_LENGTH || arg_count > parsed->cmd->accepted_argument_count[j])
   {
      warnx("Too many arguments provided for command '%s%s%s'\n", command,
            (command && !EMPTY_STR(subcommand)) ? " " : "", subcommand);
      return false;
   }

   /* Copy argv + offset pointers into parsed->args */
   for (int i = 0; i < arg_count; i++)
   {
      parsed->args[i] = argv[i + offset];
   }
   parsed->args[0] = parsed->args[0] ? parsed->args[0] : (char*)parsed->cmd->default_argument;

   /* Warn the user if there is enough information about deprecation */
   if (parsed->cmd->deprecated && pgmoneta_version_ge(parsed->cmd->deprecated_since_major,
                                                      parsed->cmd->deprecated_since_minor, 0))
   {
      warnx("command <%s> has been deprecated by <%s> since version %d.%d",
            parsed->cmd->command,
            parsed->cmd->deprecated_by,
            parsed->cmd->deprecated_since_major,
            parsed->cmd->deprecated_since_minor);
   }

   return true;
}

int
pgmoneta_token_bucket_init(struct token_bucket* tb, long max_rate)
{
   if (tb != NULL && max_rate > 0)
   {
      if (max_rate > DEFAULT_BURST)
      {
         tb->burst = max_rate;
      }
      else
      {
         tb->burst = DEFAULT_BURST;
      }
      atomic_init(&tb->cur_tokens, tb->burst);
      tb->max_rate = max_rate;
      tb->every = DEFAULT_EVERY;
      atomic_init(&tb->last_time, (unsigned long)time(NULL));
      return 0;
   }

   return 1;
}

void
pgmoneta_token_bucket_destroy(struct token_bucket* tb)
{
   if (tb)
   {
      free(tb);
   }
}

int
pgmoneta_token_bucket_add(struct token_bucket* tb)
{
   unsigned long diff;
   unsigned long expected_tokens;
   unsigned long new_tokens;
   unsigned long expected_time;
   unsigned long cur_time;

   expected_time = atomic_load(&tb->last_time);
   cur_time = (unsigned long)time(NULL);
   diff = cur_time - expected_time;

   if (diff < (unsigned long)tb->every)
   {
      return 0;
   }

   // update time
   while (!atomic_compare_exchange_weak(&tb->last_time, &expected_time, cur_time))
   {
      expected_time = atomic_load(&tb->last_time);
      cur_time = (unsigned long)time(NULL);
      diff = cur_time - expected_time;
      if (diff < (unsigned long)tb->every)
      {
         return 0;
      }
   }

   // update token
   expected_tokens = atomic_load(&tb->cur_tokens);
   new_tokens = expected_tokens + tb->max_rate * (diff / tb->every);

   if (new_tokens > tb->burst)
   {
      new_tokens = tb->burst;
   }

   while (!atomic_compare_exchange_weak(&tb->cur_tokens, &expected_tokens, new_tokens))
   {
      expected_tokens = atomic_load(&tb->cur_tokens);
      new_tokens = expected_tokens + tb->max_rate * (diff / tb->every);

      if (new_tokens > tb->burst)
      {
         new_tokens = tb->burst;
      }
   }

   return 0;
}

int
pgmoneta_token_bucket_consume(struct token_bucket* tb, unsigned long tokens)
{
   if (tokens < tb->burst)
   {
      return pgmoneta_token_bucket_once(tb, tokens);
   }
   else
   {
      unsigned long accum = 0;
      unsigned long cur_tokens;
      while (accum < tokens)
      {
         cur_tokens = atomic_load(&tb->cur_tokens);
         if (!pgmoneta_token_bucket_once(tb, cur_tokens))
         {
            accum += cur_tokens;
         }
         else
         {
            SLEEP(500000000L);
         }
      }
      return 0;
   }
}

int
pgmoneta_token_bucket_once(struct token_bucket* tb, unsigned long tokens)
{
   unsigned long expected;

   if (!pgmoneta_token_bucket_add(tb))
   {
      expected = atomic_load(&tb->cur_tokens);

      while (expected >= tokens)
      {
         if (atomic_compare_exchange_weak(&tb->cur_tokens, &expected, expected - tokens))
         {
            return 0;
         }
      }
   }

   return 1;
}

char*
pgmoneta_format_and_append(char* buf, char* format, ...)
{
   va_list args;
   va_start(args, format);

   // Determine the required buffer size
   int size_needed = vsnprintf(NULL, 0, format, args) + 1;
   va_end(args);

   // Allocate buffer to hold the formatted string
   char* formatted_str = malloc(size_needed);

   va_start(args, format);
   vsnprintf(formatted_str, size_needed, format, args);
   va_end(args);

   buf = pgmoneta_append(buf, formatted_str);

   free(formatted_str);

   return buf;
}

int
pgmoneta_atoi(char* input)
{
   char* endptr = NULL;
   long val = 0;

   if (input == NULL)
   {
      return 0;
   }

   errno = 0;
   val = strtol(input, &endptr, 10);
   if (errno != 0 || endptr == input || val > INT_MAX || val < INT_MIN)
   {
      return 0;
   }

   return (int)val;
}

char*
pgmoneta_indent(char* str, char* tag, int indent)
{
   for (int i = 0; i < indent; i++)
   {
      str = pgmoneta_append(str, " ");
   }
   if (tag != NULL)
   {
      str = pgmoneta_append(str, tag);
   }
   return str;
}

char*
pgmoneta_escape_string(char* str)
{
   if (str == NULL)
   {
      return NULL;
   }

   char* translated_ec_string = NULL;
   int len = 0;
   int idx = 0;
   size_t translated_len = 0;

   len = strlen(str);
   for (int i = 0; i < len; i++)
   {
      if (str[i] == '\"' || str[i] == '\'' || str[i] == '\\' || str[i] == '\n' || str[i] == '\t' || str[i] == '\r')
      {
         translated_len++;
      }
      translated_len++;
   }
   translated_ec_string = (char*)malloc(translated_len + 1);

   for (int i = 0; i < len; i++, idx++)
   {
      switch (str[i])
      {
         case '\\':
         case '\'':
         case '\"':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = str[i];
            break;
         case '\n':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = 'n';
            break;
         case '\t':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = 't';
            break;
         case '\r':
            translated_ec_string[idx] = '\\';
            idx++;
            translated_ec_string[idx] = 'r';
            break;
         default:
            translated_ec_string[idx] = str[i];
            break;
      }
   }
   translated_ec_string[idx] = '\0'; // terminator

   return translated_ec_string;
}

char*
pgmoneta_lsn_to_string(uint64_t lsn)
{
   char* result = NULL;
   result = (char*)malloc(64 * sizeof(char));

   if (result == NULL)
   {
      pgmoneta_log_fatal("pgmoneta_lsn_to_string: malloc failed");
      return NULL;
   }
   memset(result, 0, 64);
   snprintf(result, 64, "%X/%X", (uint32_t)(lsn >> 32), (uint32_t)lsn);
   return result;
}

uint64_t
pgmoneta_string_to_lsn(char* lsn)
{
   uint32_t hi = 0;
   uint32_t lo = 0;

   if (lsn == NULL)
   {
      return 0;
   }

   sscanf(lsn, "%X/%X", &hi, &lo);
   return ((uint64_t)hi << 32) + (uint64_t)lo;
}

bool
pgmoneta_is_incremental_path(char* path)
{
   char* name;
   int len = 0;
   int seglen = 0;
   if (path == NULL)
   {
      return false;
   }
   len = strlen(path);
   // extract the last segment
   for (int i = len - 1; i >= 0; i--)
   {
      if (path[i] == '/')
      {
         break;
      }
      seglen++;
   }
   name = path + (len - seglen);
   return pgmoneta_starts_with(name, INCREMENTAL_PREFIX);
}

bool
pgmoneta_is_number(char* str, int base)
{
   if (str == NULL || strlen(str) == 0)
   {
      return false;
   }

   if (base != 10 && base != 16)
   {
      return false;
   }

   for (int i = 0; str[i] != '\0'; i++)
   {
      if (i == 0 && str[i] == '-' && strlen(str) > 1)
      {
         continue;
      }
      if (str[i] >= 48 && str[i] <= 57)
      {
         /* Standard numbers */
      }
      else if (str[i] == '\r' || str[i] == '\n')
      {
         /* Feeds */
      }
      else if (base == 16)
      {
         if ((str[i] >= 65 && str[i] <= 70) || (str[i] >= 97 && str[i] <= 102))
         {
            /* Hex */
         }
         else
         {
            return false;
         }
      }
      else
      {
         return false;
      }
   }

   return true;
}

int
pgmoneta_split(const char* string, char*** results, int* count, char delimiter)
{
   char delim_str[2] = {delimiter, '\0'};
   char* temp = strdup(string);
   char** temp_results = NULL;
   int num_objects = 0;
   char* token = NULL;

   *results = NULL;
   *count = 0;

   if (!string || !results || !count || !temp)
   {
      goto error;
   }

   if (strlen(string) == 0)
   {
      temp_results = calloc(1, sizeof(char*));
      if (!temp_results)
      {
         goto error;
      }
      temp_results[0] = NULL;
      *results = temp_results;
      temp_results = NULL;
      return 0;
   }

   token = strtok(temp, delim_str);
   while (token)
   {
      num_objects++;
      token = strtok(NULL, delim_str);
   }

   temp_results = calloc(num_objects + 1, sizeof(char*));
   if (!temp_results)
   {
      goto error;
   }

   free(temp);
   temp = strdup(string);
   if (!temp)
   {
      free(temp_results);
      goto error;
   }

   token = strtok(temp, delim_str);
   for (int i = 0; i < num_objects; i++)
   {
      temp_results[i] = strdup(token);
      if (!temp_results[i])
      {
         goto error;
      }
      token = strtok(NULL, delim_str);
   }

   temp_results[num_objects] = NULL;
   *count = num_objects;

   *results = temp_results;
   temp_results = NULL;

   free(temp);
   return 0;

error:
   free(temp);

   if (temp_results)
   {
      for (int i = 0; i < num_objects; i++)
      {
         if (temp_results[i])
         {
            free(temp_results[i]);
         }
      }
      free(temp_results);
   }

   return -1;
}

int
pgmoneta_merge_string_arrays(char** lists[], char*** out_list)
{
   if (!lists || !out_list)
   {
      return -1;
   }

   int total = 0;
   char*** current;
   char** merged = NULL;
   int index = 0;

   for (current = lists; *current; current++)
   {
      for (char** str = *current; *str; str++)
      {
         total++;
      }
   }

   merged = calloc(total + 1, sizeof(char*));
   if (!merged)
   {
      return -1;
   }

   for (current = lists; *current; current++)
   {
      for (char** str = *current; *str; str++)
      {
         merged[index] = strdup(*str);
         if (!merged[index])
         {
            for (int i = 0; i < index; i++)
            {
               free(merged[i]);
            }
            free(merged);
            return -1;
         }
         index++;
      }
   }

   *out_list = merged;
   return 0;
}

int
pgmoneta_is_substring(char* a, char* b)
{
   if (!a || !b || *a == '\0')
   {
      return 0;
   }
   return strstr(b, a) != NULL;
}

int
pgmoneta_resolve_path(char* orig_path, char** new_path)
{
#if defined(HAVE_DARWIN) || defined(HAVE_OSX)
#define GET_ENV(name) getenv(name)
#else
#define GET_ENV(name) secure_getenv(name)
#endif

   char* res = NULL;
   char* env_res = NULL;
   int len = strlen(orig_path);
   int res_len = 0;
   bool double_quote = false;
   bool single_quote = false;
   bool in_env = false;

   *new_path = NULL;

   if (orig_path == NULL)
   {
      goto error;
   }

   for (int idx = 0; idx < len; idx++)
   {
      char* ch = NULL;

      bool valid_env_char = orig_path[idx] == '_' || (orig_path[idx] >= 'A' && orig_path[idx] <= 'Z') || (orig_path[idx] >= 'a' && orig_path[idx] <= 'z') || (orig_path[idx] >= '0' && orig_path[idx] <= '9');
      if (in_env && !valid_env_char)
      {
         in_env = false;
         if (env_res == NULL)
         {
            goto error;
         }
         char* env_value = GET_ENV(env_res);
         free(env_res);
         env_res = NULL;
         if (env_value == NULL)
         {
            goto error;
         }
         res = pgmoneta_append(res, env_value);
         res_len += strlen(env_value);
      }

      if (orig_path[idx] == '\"' && !single_quote)
      {
         double_quote = !double_quote;
         continue;
      }
      else if (orig_path[idx] == '\'' && !double_quote)
      {
         single_quote = !single_quote;
         continue;
      }

      if (orig_path[idx] == '\\')
      {
         if (idx + 1 < len)
         {
            ch = pgmoneta_append_char(ch, orig_path[idx + 1]);
            idx++;
         }
         else
         {
            goto error;
         }
      }
      else if (orig_path[idx] == '$')
      {
         if (single_quote)
         {
            ch = pgmoneta_append_char(ch, '$');
         }
         else
         {
            in_env = true;
         }
      }
      else
      {
         ch = pgmoneta_append_char(ch, orig_path[idx]);
      }

      if (in_env)
      {
         env_res = pgmoneta_append(env_res, ch);
      }
      else
      {
         res = pgmoneta_append(res, ch);
         ++res_len;
      }

      free(ch);
   }

   if (in_env)
   {
      if (env_res == NULL)
      {
         goto error;
      }
      char* env_value = GET_ENV(env_res);
      free(env_res);
      env_res = NULL;
      if (env_value == NULL)
      {
         goto error;
      }
      res = pgmoneta_append(res, env_value);
      res_len += strlen(env_value);
   }

   if (res_len > MAX_PATH)
   {
      goto error;
   }
   *new_path = res;
   return 0;

error:
   free(res);
   free(env_res);
   return 1;
}

__attribute__((unused)) static bool
calculate_offset(uint64_t addr, uint64_t* offset, char** filepath)
{
#if defined(HAVE_LINUX) && defined(HAVE_EXECINFO_H)
   char line[256];
   char *start, *end, *base_offset, *filepath_ptr;
   uint64_t start_addr, end_addr, base_offset_value;
   FILE* fp;
   bool success = false;

   fp = fopen("/proc/self/maps", "r");
   if (fp == NULL)
   {
      goto error;
   }

   while (fgets(line, sizeof(line), fp) != NULL)
   {
      // exmaple line:
      // 7fb60d1ea000-7fb60d20c000 r--p 00000000 103:02 120327460 /usr/lib/libc.so.6
      start = strtok(line, "-");
      end = strtok(NULL, " ");
      strtok(NULL, " "); // skip the next token
      base_offset = strtok(NULL, " ");
      strtok(NULL, " "); // skip the next token
      strtok(NULL, " "); // skip the next token
      filepath_ptr = strtok(NULL, " \n");
      if (start != NULL && end != NULL && base_offset != NULL && filepath_ptr != NULL)
      {
         start_addr = strtoul(start, NULL, 16);
         end_addr = strtoul(end, NULL, 16);
         if (addr >= start_addr && addr < end_addr)
         {
            success = true;
            break;
         }
      }
   }
   if (!success)
   {
      goto error;
   }

   base_offset_value = strtoul(base_offset, NULL, 16);
   *offset = addr - start_addr + base_offset_value;
   *filepath = pgmoneta_append(*filepath, filepath_ptr);
   if (fp != NULL)
   {
      fclose(fp);
   }
   return 0;

error:
   if (fp != NULL)
   {
      fclose(fp);
   }
   return 1;

#else
   return 1;

#endif
}

int
pgmoneta_backtrace(void)
{
   char* s = NULL;
   int ret = 0;

   ret = pgmoneta_backtrace_string(&s);

   if (s != NULL)
   {
      pgmoneta_log_debug(s);
   }

   free(s);

   return ret;
}

int
pgmoneta_backtrace_string(char** s)
{
#ifdef HAVE_EXECINFO_H
   void* bt[1024];
   char* log_str = NULL;
   size_t bt_size;

   *s = NULL;

   bt_size = backtrace(bt, 1024);
   if (bt_size == 0)
   {
      goto error;
   }

   log_str = pgmoneta_append(log_str, "Backtrace:\n");

   // the first element is ___interceptor_backtrace, so we skip it
   for (size_t i = 1; i < bt_size; i++)
   {
      uint64_t addr = (uint64_t)bt[i];
      uint64_t offset;
      char* filepath = NULL;
      char cmd[256], buffer[256], log_buffer[64];
      bool found_main = false;
      FILE* pipe;

      if (calculate_offset(addr, &offset, &filepath))
      {
         continue;
      }

      snprintf(cmd, sizeof(cmd), "addr2line -e %s -fC 0x%lx", filepath, offset);
      free(filepath);
      filepath = NULL;

      pipe = popen(cmd, "r");
      if (pipe == NULL)
      {
         pgmoneta_log_debug("Failed to run command: %s, reason: %s", cmd, strerror(errno));
         continue;
      }

      if (fgets(buffer, sizeof(buffer), pipe) == NULL)
      {
         pgmoneta_log_debug("Failed to read from command output: %s", strerror(errno));
         pclose(pipe);
         continue;
      }
      buffer[strlen(buffer) - 1] = '\0'; // Remove trailing newline
      if (strcmp(buffer, "main") == 0)
      {
         found_main = true;
      }
      snprintf(log_buffer, sizeof(log_buffer), "#%zu  0x%lx in ", i - 1, addr);
      log_str = pgmoneta_append(log_str, log_buffer);
      log_str = pgmoneta_append(log_str, buffer);
      log_str = pgmoneta_append(log_str, "\n");

      if (fgets(buffer, sizeof(buffer), pipe) == NULL)
      {
         log_str = pgmoneta_append(log_str, "\tat ???:??\n");
      }
      else
      {
         buffer[strlen(buffer) - 1] = '\0'; // Remove trailing newline
         log_str = pgmoneta_append(log_str, "\tat ");
         log_str = pgmoneta_append(log_str, buffer);
         log_str = pgmoneta_append(log_str, "\n");
      }

      pclose(pipe);
      if (found_main)
      {
         break;
      }
   }

   *s = log_str;

   return 0;

error:
   if (log_str != NULL)
   {
      free(log_str);
   }
   return 1;
#else
   return 1;
#endif
}

void
pgmoneta_dump_art(struct art* a)
{
#ifdef DEBUG
   assert(a != NULL);
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char* s = NULL;
      s = pgmoneta_art_to_string(a, FORMAT_TEXT, NULL, 0);
      if (s != NULL && strlen(s) > 0)
      {
         pgmoneta_log_debug("(Tree)\n%s", s);
      }
      else
      {
         pgmoneta_log_debug("(Tree)");
      }
      free(s);
   }
#endif
}

int
pgmoneta_os_kernel_version(char** os, int* kernel_major, int* kernel_minor, int* kernel_patch)
{
   bool bsd = false;
   *os = NULL;
   *kernel_major = 0;
   *kernel_minor = 0;
   *kernel_patch = 0;

#if defined(HAVE_LINUX) || defined(HAVE_FREEBSD) || defined(HAVE_OPENBSD) || defined(HAVE_OSX)
   struct utsname buffer;

   if (uname(&buffer) != 0)
   {
      pgmoneta_log_debug("Failed to retrieve system information.");
      goto error;
   }

   // Copy system name using pgmoneta_append (dynamically allocated)
   *os = pgmoneta_append(NULL, buffer.sysname);
   if (*os == NULL)
   {
      pgmoneta_log_debug("Failed to allocate memory for OS name.");
      goto error;
   }

   // Parse kernel version based on OS
#if defined(HAVE_LINUX)
   if (sscanf(buffer.release, "%d.%d.%d", kernel_major, kernel_minor, kernel_patch) < 2)
   {
      pgmoneta_log_debug("Failed to parse Linux kernel version.");
      goto error;
   }
#elif defined(HAVE_FREEBSD) || defined(HAVE_OPENBSD)
   if (sscanf(buffer.release, "%d.%d", kernel_major, kernel_minor) < 2)
   {
      pgmoneta_log_debug("Failed to parse BSD OS kernel version.");
      goto error;
   }
   *kernel_patch = 0; // BSD doesn't use patch version
   bsd = true;
#elif defined(HAVE_OSX)
   if (sscanf(buffer.release, "%d.%d.%d", kernel_major, kernel_minor, kernel_patch) < 2)
   {
      pgmoneta_log_debug("Failed to parse macOS kernel version.");
      goto error;
   }
#endif

   if (!bsd)

   {
      pgmoneta_log_debug("OS: %s | Kernel Version: %d.%d.%d", *os, *kernel_major, *kernel_minor, *kernel_patch);
   }

   else

   {
      pgmoneta_log_debug("OS: %s | Version: %d.%d", *os, *kernel_major, *kernel_minor);
   }

   return 0;

error:
   //Free memory if already allocated
   if (*os != NULL)
   {
      free(*os);
      *os = NULL;
   }

   *os = pgmoneta_append(NULL, "Unknown");
   if (*os == NULL)
   {
      pgmoneta_log_debug("Failed to allocate memory for unknown OS name.");
   }

   pgmoneta_log_debug("Unable to retrieve OS and kernel version.");

   *kernel_major = 0;
   *kernel_minor = 0;
   *kernel_patch = 0;
   return 1;

#else
   *os = pgmoneta_append(NULL, "Unknown");
   if (*os == NULL)
   {
      pgmoneta_log_debug("Failed to allocate memory for unknown OS name.");
   }

   pgmoneta_log_debug("Kernel version not available.");
   return 1;
#endif
}

char*
pgmoneta_get_parent_dir(const char* path)
{
   if (path == NULL)
   {
      return NULL;
   }

   char* parent_dir = strdup(path);
   if (parent_dir == NULL)
   {
      return NULL;
   }

   char* last_slash = strrchr(parent_dir, '/');
   if (last_slash != NULL && last_slash != parent_dir)
   {
      *last_slash = '\0';
   }
   else if (last_slash == parent_dir)
   {
      // The path is like "/foo", so keep "/"
      parent_dir[1] = '\0';
   }
   else if (last_slash == NULL)
   {
      // No slash found, return "."
      free(parent_dir);
      parent_dir = strdup(".");
   }

   return parent_dir;
}

int
pgmoneta_normalize_path(char* directory_path, char* filename, char* default_path, char* path_buffer, size_t buffer_size)
{
   char* temp_path = NULL;

   if (path_buffer == NULL || buffer_size == 0 || filename == NULL)
   {
      goto error;
   }

   memset(path_buffer, 0, buffer_size);

   if (directory_path != NULL)
   {
      temp_path = pgmoneta_append(NULL, directory_path);
      if (temp_path == NULL)
      {
         goto error;
      }

      if (directory_path[strlen(directory_path) - 1] != '/')
      {
         temp_path = pgmoneta_append(temp_path, "/");
         if (temp_path == NULL)
         {
            goto error;
         }
      }

      temp_path = pgmoneta_append(temp_path, filename);
      if (temp_path == NULL)
      {
         goto error;
      }

      if (strlen(temp_path) >= buffer_size)
      {
         pgmoneta_log_error("Configuration directory path is too long: %s (maximum %zu characters)",
                            temp_path, buffer_size - 1);
         goto error;
      }

      if (access(temp_path, F_OK) == 0)
      {
         pgmoneta_log_debug("Using config file: %s", temp_path);
         strcpy(path_buffer, temp_path);
         free(temp_path);
         return 0;
      }
      else
      {
         pgmoneta_log_info("Config file %s not found in directory %s", filename, directory_path);
         free(temp_path);
         temp_path = NULL;
      }
   }

   if (default_path != NULL)
   {
      if (access(default_path, F_OK) == 0)
      {
         if (strlen(default_path) >= buffer_size)
         {
            pgmoneta_log_error("Default configuration path is too long: %s (maximum %zu characters)",
                               default_path, buffer_size - 1);
            goto error;
         }
         pgmoneta_log_info("Using default config file: %s", default_path);
         strcpy(path_buffer, default_path);
         return 0;
      }
      else
      {
         pgmoneta_log_info("Default config file %s not found, continuing without %s", default_path, filename);
         return 0;
      }
   }

   pgmoneta_log_warn("No path specified for config file %s", filename);

error:

   free(temp_path);
   return 1;
}

void*
pgmoneta_allocate_aligned(size_t size, size_t alignment)
{
   void* ptr = NULL;
#if defined(__linux__)
   if (posix_memalign(&ptr, alignment, size) != 0)
   {
      pgmoneta_log_error("Failed to allocate aligned memory: size=%zu alignment=%zu", size, alignment);
      return NULL;
   }
#else
   /* On non-Linux platforms, use regular malloc */
   ptr = malloc(size);
   if (ptr == NULL)
   {
      pgmoneta_log_error("Failed to allocate memory: size=%zu", size);
   }
#endif
   return ptr;
}

void
pgmoneta_free_aligned(void* ptr)
{
   if (ptr != NULL)
   {
      free(ptr);
   }
}

size_t
pgmoneta_get_block_size(const char* path)
{
   struct statvfs st;

   if (path == NULL)
   {
      return 4096;
   }

   if (statvfs(path, &st) == 0 && st.f_bsize > 0)
   {
      return st.f_bsize;
   }

   /* Safe default: 4096 bytes */
   return 4096;
}

bool
pgmoneta_direct_io_supported(const char* path)
{
#if defined(__linux__)
   char template[MAX_PATH];
   int fd = -1;

   /* path must be a writable directory */
   if (path == NULL)
   {
      return false;
   }

   /* Create a temporary file to test O_DIRECT support */
   pgmoneta_snprintf(template, sizeof(template), "%s/.direct_io_test_XXXXXX", path);
   fd = mkstemp(template);
   if (fd < 0)
   {
      pgmoneta_log_debug("Cannot create temp file for O_DIRECT test in %s: %s", path, strerror(errno));
      return false;
   }
   close(fd);

   /* Try to open with O_DIRECT */
   fd = open(template, O_RDWR | O_DIRECT);
   unlink(template);

   if (fd >= 0)
   {
      close(fd);
      return true;
   }

   if (errno == EINVAL || errno == EOPNOTSUPP)
   {
      pgmoneta_log_debug("O_DIRECT unsupported on %s", path);
   }
   else
   {
      pgmoneta_log_debug("O_DIRECT open failed on %s: %s", path, strerror(errno));
   }
   return false;
#else
   (void)path;
   return false;
#endif
}

void
pgmoneta_cpu_yield(void)
{
#ifdef HAVE_LINUX
   sched_yield();
#else
   /* On non-Linux systems, use a minimal sleep as fallback */
   struct timespec ts = {0, 1000}; /* 1 microsecond */
   nanosleep(&ts, NULL);
#endif
}

int
pgmoneta_set_priority(int priority)
{
#ifdef HAVE_LINUX
   return setpriority(PRIO_PROCESS, 0, priority);
#else
   (void)priority;
   return 0; /* No-op on non-Linux systems */
#endif
}

int
pgmoneta_get_priority(void)
{
#ifdef HAVE_LINUX
   int prio;

   errno = 0;
   prio = getpriority(PRIO_PROCESS, 0);
   if (prio == -1 && errno != 0)
   {
      return -1;
   }
   return prio;
#else
   return 0; /* Default priority on non-Linux systems */
#endif
}
