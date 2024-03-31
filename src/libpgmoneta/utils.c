/*
 * Copyright (C) 2024 The pgmoneta community
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
#include <logging.h>
#include <restore.h>
#include <utils.h>
#include <workers.h>

/* system */
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <ev.h>
#include <execinfo.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <openssl/pem.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef EVBACKEND_LINUXAIO
#define EVBACKEND_LINUXAIO 0x00000040U
#endif

#ifndef EVBACKEND_IOURING
#define EVBACKEND_IOURING  0x00000080U
#endif

extern char** environ;
#ifdef HAVE_LINUX
static bool env_changed = false;
static int max_process_title_size = 0;
#endif

static int string_compare(const void* a, const void* b);

static bool is_wal_file(char* file);

static char* get_server_basepath(int server);

static int copy_tablespaces(char* from, char* to, char* base, char* server, char* id, struct backup* backup, struct workers* workers);

static int get_permissions(char* from, int* permissions);

static void copy_file(void* arg);

int32_t
pgmoneta_get_request(struct message* msg)
{
   if (msg == NULL || msg->data == NULL || msg->length < 8)
   {
      return -1;
   }

   return pgmoneta_read_int32(msg->data + 4);
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
   void* data = NULL;
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
         data = (void*)malloc(1 + m_length);

         memcpy(data, msg->data + offset, 1 + m_length);

         result->kind = pgmoneta_read_byte(data);
         result->length = 1 + m_length;
         result->max_length = 1 + m_length;
         result->data = data;

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
            pgmoneta_free_copy_message(warning_msg);
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
   size_t offset = 1 + 4;
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
         result = (char*) malloc(field_len);
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
   m_data = malloc(1 + m_length);

   memcpy(m_data, data + offset, 1 + m_length);

   result->kind = type;
   result->length = 1 + m_length;
   result->max_length = 1 + m_length;
   result->data = m_data;

   *extracted = result;

   return offset + 1 + m_length;
}

int
pgmoneta_extract_message_from_data(char type, void* data, size_t data_size, struct message** extracted)
{
   int offset;
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
         m_data = (void*)malloc(1 + m_length);

         memcpy(m_data, data + offset, 1 + m_length);

         result->kind = pgmoneta_read_byte(m_data);
         result->length = 1 + m_length;
         result->max_length = 1 + m_length;
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
            pgmoneta_free_copy_message(warning_msg);
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
   return (signed char) *((char*)data);
}

int16_t
pgmoneta_read_int16(void* data)
{
   unsigned char bytes[] = {*((unsigned char*)data),
                            *((unsigned char*)(data + 1))};

   int16_t res = (int16_t)((bytes[0] << 8)) |
                 ((bytes[1]));

   return res;
}

int32_t
pgmoneta_read_int32(void* data)
{
   unsigned char bytes[] = {*((unsigned char*)data),
                            *((unsigned char*)(data + 1)),
                            *((unsigned char*)(data + 2)),
                            *((unsigned char*)(data + 3))};

   int32_t res = (int32_t)(((uint32_t)bytes[0] << 24)) |
                 ((bytes[1] << 16)) |
                 ((bytes[2] << 8)) |
                 ((bytes[3]));

   return res;
}

int64_t
pgmoneta_read_int64(void* data)
{
   int64_t i0 = *((unsigned char*)data);
   int64_t i1 = *((unsigned char*)data + 1);
   int64_t i2 = *((unsigned char*)data + 2);
   int64_t i3 = *((unsigned char*)data + 3);
   int64_t i4 = *((unsigned char*)data + 4);
   int64_t i5 = *((unsigned char*)data + 5);
   int64_t i6 = *((unsigned char*)data + 6);
   int64_t i7 = *((unsigned char*)data + 7);

   i0 = i0 << 56;
   i1 = i1 << 48;
   i2 = i2 << 40;
   i3 = i3 << 32;
   i4 = i4 << 24;
   i5 = i5 << 16;
   i6 = i6 << 8;
   /* i7 = i7; */

   return i0 | i1 | i2 | i3 | i4 | i5 | i6 | i7;
}

void
pgmoneta_write_byte(void* data, signed char b)
{
   *((char*)(data)) = b;
}

void
pgmoneta_write_int32(void* data, int32_t i)
{
   char* ptr = (char*)&i;

   *((char*)(data + 3)) = *ptr;
   ptr++;
   *((char*)(data + 2)) = *ptr;
   ptr++;
   *((char*)(data + 1)) = *ptr;
   ptr++;
   *((char*)(data)) = *ptr;
}

void
pgmoneta_write_int64(void* data, int64_t i)
{
   char* ptr = (char*)&i;

   *((char*)(data + 7)) = *ptr;
   ptr++;
   *((char*)(data + 6)) = *ptr;
   ptr++;
   *((char*)(data + 5)) = *ptr;
   ptr++;
   *((char*)(data + 4)) = *ptr;
   ptr++;
   *((char*)(data + 3)) = *ptr;
   ptr++;
   *((char*)(data + 2)) = *ptr;
   ptr++;
   *((char*)(data + 1)) = *ptr;
   ptr++;
   *((char*)(data)) = *ptr;
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
   struct passwd* pw = getpwuid(getuid());

   if (pw == NULL)
   {
      return NULL;
   }

   return pw->pw_dir;
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
pgmoneta_base64_encode(char* raw, int raw_length, char** encoded)
{
   BIO* b64_bio;
   BIO* mem_bio;
   BUF_MEM* mem_bio_mem_ptr;
   char* r = NULL;

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

   return 0;

error:

   *encoded = NULL;

   return 1;
}

int
pgmoneta_base64_decode(char* encoded, size_t encoded_length, char** raw, int* raw_length)
{
   BIO* b64_bio;
   BIO* mem_bio;
   size_t size;
   char* decoded;
   int index;

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

   *raw = decoded;
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
   struct configuration* config;

   config = (struct configuration*)shmem;

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

   // compute how long was the command line
   // when the application was started
   if (max_process_title_size == 0)
   {
      for (int i = 0; i < argc; i++)
      {
         max_process_title_size += strlen(argv[i]) + 1;
      }
   }

   // compose the new title
   memset(&title, 0, sizeof(title));
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
   return (patch % 100)
          + (minor % 100) * 100
          + (major % 100) * 10000;
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

   if (orig != NULL)
   {
      orig_length = strlen(orig);
   }
   else
   {
      orig_length = 0;
   }

   s_length = strlen(s);

   n = (char*)realloc(orig, orig_length + s_length + 1);

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
pgmoneta_append_bool(char* orig, bool b)
{
   if (b)
   {
      orig = pgmoneta_append(orig, "1");
   }
   else
   {
      orig = pgmoneta_append(orig, "0");
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

   for (int i = 0; i < length; i++)
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

unsigned long
pgmoneta_directory_size(char* directory)
{
   unsigned long total_size = 0;
   DIR* dir;
   struct dirent* entry;
   char* p;
   struct stat st;
   unsigned long l;

   if (!(dir = opendir(directory)))
   {
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

int
pgmoneta_get_directories(char* base, int* number_of_directories, char*** dirs)
{
   char* d = NULL;
   char** array = NULL;
   int nod = 0;
   int n;
   DIR* dir;
   struct dirent* entry;

   *number_of_directories = 0;
   *dirs = NULL;

   nod = 0;

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
pgmoneta_get_files(char* base, int* number_of_files, char*** files)
{
   char* d = NULL;
   char** array = NULL;
   int nof = 0;
   int n;
   DIR* dir;
   struct dirent* entry;

   *number_of_files = 0;
   *files = NULL;

   nof = 0;

   if (!(dir = opendir(base)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_REG)
      {
         nof++;
      }
   }

   closedir(dir);
   dir = NULL;

   dir = opendir(base);

   array = (char**)malloc(sizeof(char*) * nof);
   n = 0;

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_REG)
      {
         array[n] = (char*)malloc(strlen(entry->d_name) + 1);
         memset(array[n], 0, strlen(entry->d_name) + 1);
         memcpy(array[n], entry->d_name, strlen(entry->d_name));
         n++;
      }
   }

   closedir(dir);
   dir = NULL;

   pgmoneta_sort(nof, array);

   free(d);
   d = NULL;

   *number_of_files = nof;
   *files = array;

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   for (int i = 0; i < nof; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   *number_of_files = 0;
   *files = NULL;

   return 1;
}

int
pgmoneta_get_wal_files(char* base, int* number_of_files, char*** files)
{
   char** array = NULL;
   int nof = 0;
   int n;
   DIR* dir;
   struct dirent* entry;

   *number_of_files = 0;
   *files = NULL;

   nof = 0;

   if (!(dir = opendir(base)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (pgmoneta_ends_with(entry->d_name, ".partial"))
      {
         continue;
      }

      if (strstr(entry->d_name, ".history") != NULL)
      {
         continue;
      }

      if (entry->d_type == DT_REG)
      {
         nof++;
      }
   }

   closedir(dir);
   dir = NULL;

   if (nof > 0)
   {
      dir = opendir(base);

      array = (char**)malloc(sizeof(char*) * nof);
      n = 0;

      while ((entry = readdir(dir)) != NULL)
      {
         if (pgmoneta_ends_with(entry->d_name, ".partial"))
         {
            continue;
         }

         if (strstr(entry->d_name, ".history") != NULL)
         {
            continue;
         }

         if (entry->d_type == DT_REG)
         {
            array[n] = (char*)malloc(strlen(entry->d_name) + 1);
            memset(array[n], 0, strlen(entry->d_name) + 1);
            memcpy(array[n], entry->d_name, strlen(entry->d_name));
            n++;
         }
      }

      closedir(dir);
      dir = NULL;

      pgmoneta_sort(nof, array);
   }

   *number_of_files = nof;
   *files = array;

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   for (int i = 0; i < nof; i++)
   {
      free(array[i]);
   }
   free(array);

   *number_of_files = 0;
   *files = NULL;

   return 1;
}

int
pgmoneta_delete_file(char* file)
{
   int ret;

   ret = unlink(file);

   if (ret != 0)
   {
      pgmoneta_log_warn("pgmoneta_delete_file: %s (%s)", file, strerror(errno));
      errno = 0;
      ret = 1;
   }

   return ret;
}

int
pgmoneta_copy_postgresql(char* from, char* to, char* base, char* server, char* id, struct backup* backup, struct workers* workers)
{
   DIR* d = opendir(from);
   char* from_buffer = NULL;
   char* to_buffer = NULL;
   struct dirent* entry;
   struct stat statbuf;
   char** restore_last_files_names = NULL;

   if (pgmoneta_get_restore_last_files_names(&restore_last_files_names))
   {
      goto error;
   }

   for (int i = 0; restore_last_files_names[i] != NULL; i++)
   {
      char* temp = NULL;
      temp = (char*)malloc((strlen(restore_last_files_names[i]) + strlen(from)) * sizeof(char) + 1);

      if (temp == NULL)
      {
         goto error;
      }
      snprintf(temp, strlen(from) + strlen(restore_last_files_names[i]) + 1, "%s%s", from, restore_last_files_names[i]);
      free(restore_last_files_names[i]);

      restore_last_files_names[i] = temp;
   }

   pgmoneta_mkdir(to);

   if (d)
   {
      while ((entry = readdir(d)))
      {
         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

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
               if (!strcmp(entry->d_name, "pg_tblspc"))
               {
                  copy_tablespaces(from, to, base, server, id, backup, workers);
               }
               else
               {
                  pgmoneta_copy_directory(from_buffer, to_buffer, restore_last_files_names, workers);
               }
            }
            else
            {
               bool file_is_excluded = false;
               for (int i = 0; restore_last_files_names[i] != NULL; i++)
               {
                  file_is_excluded = !strcmp(from_buffer, restore_last_files_names[i]);
               }
               if (!file_is_excluded)
               {
                  pgmoneta_copy_file(from_buffer, to_buffer, workers);
               }
            }
         }

         free(from_buffer);
         free(to_buffer);

         from_buffer = NULL;
         to_buffer = NULL;
      }
      closedir(d);
   }
   else
   {
      goto error;
   }

   for (int i = 0; restore_last_files_names[i] != NULL; i++)
   {
      free(restore_last_files_names[i]);
   }
   free(restore_last_files_names);

   return 0;

error:
   for (int i = 0; restore_last_files_names[i] != NULL; i++)
   {
      free(restore_last_files_names[i]);
   }
   free(restore_last_files_names);

   return 1;
}

static int
copy_tablespaces(char* from, char* to, char* base, char* server, char* id, struct backup* backup, struct workers* workers)
{
   char* from_tblspc = NULL;
   char* to_tblspc = NULL;
   int idx = -1;
   DIR* d = NULL;
   ssize_t size;
   struct dirent* entry;

   from_tblspc = pgmoneta_append(from_tblspc, from);
   if (!pgmoneta_ends_with(from_tblspc, "/"))
   {
      from_tblspc = pgmoneta_append(from_tblspc, "/");
   }
   from_tblspc = pgmoneta_append(from_tblspc, "pg_tblspc/");

   to_tblspc = pgmoneta_append(to_tblspc, to);
   if (!pgmoneta_ends_with(to_tblspc, "/"))
   {
      to_tblspc = pgmoneta_append(to_tblspc, "/");
   }
   to_tblspc = pgmoneta_append(to_tblspc, "pg_tblspc/");

   pgmoneta_mkdir(to_tblspc);

   if (backup->number_of_tablespaces > 0)
   {
      d = opendir(from_tblspc);

      if (d == NULL)
      {
         pgmoneta_log_error("Could not open the %s directory", from_tblspc);
         goto error;
      }

      while ((entry = readdir(d)))
      {
         char tmp_tblspc_name[MISC_LENGTH];
         char* link = NULL;
         char path[MAX_PATH];
         char* tblspc_name = NULL;

         if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
         {
            continue;
         }

         link = pgmoneta_append(link, from_tblspc);
         link = pgmoneta_append(link, entry->d_name);

         memset(&path[0], 0, sizeof(path));
         size = readlink(link, &path[0], sizeof(path));
         if (size == -1)
         {
            goto error;
         }

         if (pgmoneta_ends_with(&path[0], "/"))
         {
            memset(&tmp_tblspc_name[0], 0, sizeof(tmp_tblspc_name));
            memcpy(&tmp_tblspc_name[0], &path[0], strlen(&path[0]) - 1);

            tblspc_name = strrchr(&tmp_tblspc_name[0], '/') + 1;
         }
         else
         {
            tblspc_name = strrchr(&path[0], '/') + 1;
         }

         for (int i = 0; idx == -1 && i < backup->number_of_tablespaces; i++)
         {
            if (!strcmp(tblspc_name, backup->tablespaces[i]))
            {
               idx = i;
            }
         }

         if (idx >= 0)
         {
            char* to_oid = NULL;
            char* to_directory = NULL;
            char* relative_directory = NULL;

            pgmoneta_log_trace("Tablespace %s -> %s was found in the backup", entry->d_name, &path[0]);

            to_oid = pgmoneta_append(to_oid, to_tblspc);
            to_oid = pgmoneta_append(to_oid, entry->d_name);

            to_directory = pgmoneta_append(to_directory, base);
            to_directory = pgmoneta_append(to_directory, "/");
            to_directory = pgmoneta_append(to_directory, server);
            to_directory = pgmoneta_append(to_directory, "-");
            to_directory = pgmoneta_append(to_directory, id);
            to_directory = pgmoneta_append(to_directory, "-");
            to_directory = pgmoneta_append(to_directory, tblspc_name);
            to_directory = pgmoneta_append(to_directory, "/");

            relative_directory = pgmoneta_append(relative_directory, "../../");
            relative_directory = pgmoneta_append(relative_directory, server);
            relative_directory = pgmoneta_append(relative_directory, "-");
            relative_directory = pgmoneta_append(relative_directory, id);
            relative_directory = pgmoneta_append(relative_directory, "-");
            relative_directory = pgmoneta_append(relative_directory, tblspc_name);
            relative_directory = pgmoneta_append(relative_directory, "/");

            pgmoneta_delete_directory(to_directory);
            pgmoneta_mkdir(to_directory);
            pgmoneta_symlink_at_file(to_oid, relative_directory);

            pgmoneta_copy_directory(&path[0], to_directory, NULL, workers);

            free(to_oid);
            free(to_directory);
            free(relative_directory);

            to_oid = NULL;
            to_directory = NULL;
         }
         else
         {
            pgmoneta_log_trace("Tablespace %s -> %s was not found in the backup", entry->d_name, &path[0]);
         }

         free(link);
         link = NULL;
      }

      closedir(d);
   }

   free(from_tblspc);
   free(to_tblspc);

   return 0;

error:

   free(from_tblspc);
   free(to_tblspc);

   return 1;
}

int
pgmoneta_copy_directory(char* from, char* to, char** restore_last_files_names, struct workers* workers)
{
   DIR* d = opendir(from);
   char* from_buffer = NULL;
   char* to_buffer = NULL;
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
               for (int i = 0; restore_last_files_names[i] != NULL; i++)
               {
                  file_is_excluded = !strcmp(from_buffer, restore_last_files_names[i]);
               }
               if (!file_is_excluded)
               {
                  pgmoneta_copy_file(from_buffer, to_buffer, workers);
               }
            }
         }

         free(from_buffer);
         free(to_buffer);

         from_buffer = NULL;
         to_buffer = NULL;
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
      return 1;
   }

   if (workers != NULL)
   {
      pgmoneta_workers_add(workers, copy_file, (void*)fi);
   }
   else
   {
      copy_file(fi);
   }

   return 0;
}

static void
copy_file(void* arg)
{
   int fd_from = -1;
   int fd_to = -1;
   char buffer[8192];
   ssize_t nread = -1;
   int saved_errno = -1;
   int permissions = -1;
   struct worker_input* fi = NULL;

   fi = (struct worker_input*)arg;

   fd_from = open(fi->from, O_RDONLY);
   if (fd_from < 0)
   {
      goto error;
   }
   if (get_permissions(fi->from, &permissions))
   {
      goto error;
   }
   fd_to = open(fi->to, O_WRONLY | O_CREAT | O_EXCL, permissions);
   if (fd_to < 0)
   {
      goto error;
   }

   while ((nread = read(fd_from, buffer, sizeof(buffer))) > 0)
   {
      char* out = &buffer[0];
      ssize_t nwritten;

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
            goto error;
         }
      }
      while (nread > 0);
   }

   if (nread == 0)
   {
      if (close(fd_to) < 0)
      {
         fd_to = -1;
         goto error;
      }
      close(fd_from);
   }

   free(fi);

   return;

error:
   saved_errno = errno;

   close(fd_from);
   if (fd_to >= 0)
   {
      close(fd_to);
   }

   errno = saved_errno;

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
pgmoneta_basename_file(char* s, char** basename)
{
   size_t size;
   char* ext = NULL;
   char* r = NULL;

   *basename = NULL;

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
      size = strlen(s) + 1;

      r = (char*)malloc(size);
      if (r == NULL)
      {
         goto error;
      }

      memset(r, 0, size);
      memcpy(r, s, strlen(s));
   }

   *basename = r;

   return 0;

error:

   return 1;
}

bool
pgmoneta_exists(char* f)
{
   if (access(f, F_OK) == 0)
   {
      return true;
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
   size_t alloc;
   char* result = NULL;

   memset(&link[0], 0, sizeof(link));
   size = readlink(symlink, &link[0], sizeof(link));
   link[size + 1] = '\0';

   alloc = strlen(&link[0]) + 1;
   result = malloc(alloc);
   memset(result, 0, alloc);
   memcpy(result, &link[0], strlen(&link[0]));

   return result;
}

int
pgmoneta_copy_wal_files(char* from, char* to, char* start, struct workers* workers)
{
   int number_of_wal_files = 0;
   char** wal_files = NULL;
   char* basename = NULL;
   char* ff = NULL;
   char* tf = NULL;

   pgmoneta_get_files(from, &number_of_wal_files, &wal_files);

   for (int i = 0; i < number_of_wal_files; i++)
   {
      pgmoneta_basename_file(wal_files[i], &basename);

      if (strcmp(wal_files[i], start) >= 0)
      {
         if (pgmoneta_ends_with(wal_files[i], ".partial"))
         {
            ff = pgmoneta_append(ff, from);
            if (!pgmoneta_ends_with(ff, "/"))
            {
               ff = pgmoneta_append(ff, "/");
            }
            ff = pgmoneta_append(ff, wal_files[i]);

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
            ff = pgmoneta_append(ff, wal_files[i]);

            tf = pgmoneta_append(tf, to);
            if (!pgmoneta_ends_with(tf, "/"))
            {
               tf = pgmoneta_append(tf, "/");
            }
            tf = pgmoneta_append(tf, wal_files[i]);
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

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   return 0;
}

int
pgmoneta_number_of_wal_files(char* directory, char* from, char* to)
{
   int result;
   int number_of_wal_files = 0;
   char** wal_files = NULL;
   char* basename = NULL;

   result = 0;

   pgmoneta_get_files(directory, &number_of_wal_files, &wal_files);

   for (int i = 0; i < number_of_wal_files; i++)
   {
      pgmoneta_basename_file(wal_files[i], &basename);

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

   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   return result;
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

bool
pgmoneta_starts_with(char* str, char* prefix)
{
   return strncmp(prefix, str, strlen(prefix)) == 0;
}

bool
pgmoneta_ends_with(char* str, char* suffix)
{
   int str_len = strlen(str);
   int suffix_len = strlen(suffix);

   return (str_len >= suffix_len) && (strcmp(str + (str_len - suffix_len), suffix) == 0);
}

bool
pgmoneta_contains(char* str, char* s)
{
   return strstr(str, s) != NULL;
}

void
pgmoneta_sort(size_t size, char** array)
{
   qsort(array, size, sizeof(const char*), string_compare);
}

char*
pgmoneta_bytes_to_string(uint64_t bytes)
{
   char* sizes[] = {"EB", "PB", "TB", "GB", "MB", "KB", "B"};
   uint64_t exbibytes = 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
   uint64_t multiplier = exbibytes;
   char* result;

   result = (char*)malloc(sizeof(char) * 20);

   for (int i = 0; i < sizeof(sizes) / sizeof(*(sizes)); i++, multiplier /= 1024)
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

int
pgmoneta_read_wal(char* directory, char** wal)
{
   bool found = false;
   char* result = NULL;
   char* pgwal = NULL;
   int number_of_wal_files = 0;
   char** wal_files = NULL;

   *wal = NULL;

   pgwal = pgmoneta_append(pgwal, directory);
   pgwal = pgmoneta_append(pgwal, "/pg_wal/");

   number_of_wal_files = 0;
   wal_files = NULL;

   pgmoneta_get_files(pgwal, &number_of_wal_files, &wal_files);

   if (number_of_wal_files == 0)
   {
      goto error;
   }

   for (int i = 0; !found && i < number_of_wal_files; i++)
   {
      if (is_wal_file(wal_files[i]))
      {
         result = malloc(strlen(wal_files[i]) + 1);
         memset(result, 0, strlen(wal_files[i]) + 1);
         memcpy(result, wal_files[i], strlen(wal_files[i]));

         *wal = result;

         found = true;
      }
   }

   free(pgwal);
   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   return 0;

error:

   free(pgwal);
   for (int i = 0; i < number_of_wal_files; i++)
   {
      free(wal_files[i]);
   }
   free(wal_files);

   return 1;
}

int
pgmoneta_read_wal_info(char* directory, char** startpos, char** chkptpos, uint32_t* start_timeline)
{
   char label[MAX_PATH];
   char buffer[MAX_PATH];
   char* start = NULL;
   char* chkpt = NULL;
   FILE* file = NULL;
   uint32_t tli = 0;
   int numfields = 0;

   start = (char*)malloc(MISC_LENGTH);
   chkpt = (char*)malloc(MISC_LENGTH);

   memset(start, 0, MISC_LENGTH);
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
      if (pgmoneta_starts_with(buffer, "START WAL LOCATION"))
      {
         numfields = sscanf(buffer, "START WAL LOCATION: %s (file ", start);
         if (numfields != 1)
         {
            pgmoneta_log_error("Error parsing start wal location");
            goto error;
         }
         *startpos = start;
      }
      else if (pgmoneta_starts_with(buffer, "CHECKPOINT LOCATION"))
      {
         numfields = sscanf(buffer, "CHECKPOINT LOCATION: %s\n", chkpt);
         if (numfields != 1)
         {
            pgmoneta_log_error("Error parsing checkpoint wal location");
            goto error;
         }
         *chkptpos = chkpt;
      }
      else if (pgmoneta_starts_with(buffer, "START TIMELINE"))
      {
         numfields = sscanf(buffer, "START TIMELINE: %u\n", &tli);
         if (numfields != 1)
         {
            pgmoneta_log_error("Error parsing backup start timeline");
            goto error;
         }
         *start_timeline = tli;
      }

      memset(buffer, 0, sizeof(buffer));
   }

   fclose(file);
   return 0;

error:
   if (file != NULL)
   {
      fclose(file);
   }
   free(start);
   free(chkpt);
   return 1;
}

static int
string_compare(const void* a, const void* b)
{
   return strcmp(*(const char**)a, *(const char**)b);
}

static bool
is_wal_file(char* file)
{
   if (pgmoneta_ends_with(file, ".history"))
   {
      return false;
   }

   if (strlen(file) != 24)
   {
      return false;
   }

   return true;
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
   d = pgmoneta_append(d, "backup/");

   return d;
}

char*
pgmoneta_get_server_wal(int server)
{
   char* d = NULL;

   d = get_server_basepath(server);
   d = pgmoneta_append(d, "wal/");

   return d;
}

char*
pgmoneta_get_server_wal_shipping(int server)
{
   struct configuration* config;
   char* ws = NULL;
   config = (struct configuration*) shmem;
   if (strlen(config->servers[server].wal_shipping) > 0)
   {
      ws = pgmoneta_append(ws, config->servers[server].wal_shipping);
      if (!pgmoneta_ends_with(ws, "/"))
      {
         ws = pgmoneta_append(ws, "/");
      }
      ws = pgmoneta_append(ws, config->servers[server].name);
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
      ws = pgmoneta_append(ws, "/wal/");
   }
   return ws;
}

char*
pgmoneta_get_server_backup_identifier(int server, char* identifier)
{
   char* d = NULL;

   d = pgmoneta_get_server_backup(server);
   d = pgmoneta_append(d, identifier);
   d = pgmoneta_append(d, "/");

   return d;
}

char*
pgmoneta_get_server_backup_identifier_data(int server, char* identifier)
{
   char* d = NULL;

   d = pgmoneta_get_server_backup_identifier(server, identifier);
   d = pgmoneta_append(d, "data/");

   return d;
}

char*
pgmoneta_get_server_backup_identifier_tablespace(int server, char* identifier, char* name)
{
   char* d = NULL;

   d = pgmoneta_get_server_backup_identifier(server, identifier);
   d = pgmoneta_append(d, name);
   d = pgmoneta_append(d, "/");

   return d;
}

char*
pgmoneta_get_server_backup_identifier_data_wal(int server, char* identifier)
{
   char* d = NULL;

   d = pgmoneta_get_server_backup_identifier_data(server, identifier);
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
}

int
pgmoneta_permission(char* e, int user, int group, int all)
{
   int ret;
   mode_t mode = 0;

   switch (user)
   {
      case 7:
      {
         mode = S_IRUSR | S_IWUSR | S_IXUSR;
         break;
      }
      case 6:
      {
         mode = S_IRUSR | S_IWUSR;
         break;
      }
      case 4:
      {
         mode = S_IRUSR;
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
         mode = S_IRGRP | S_IWGRP | S_IXGRP;
         break;
      }
      case 6:
      {
         mode += S_IRGRP | S_IWGRP;
         break;
      }
      case 4:
      {
         mode += S_IRGRP;
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
         mode = S_IROTH | S_IWOTH | S_IXOTH;
         break;
      }
      case 6:
      {
         mode += S_IROTH | S_IWOTH;
         break;
      }
      case 4:
      {
         mode += S_IROTH;
         break;
      }
      default:
      {
         break;
      }
   }

   ret = chmod(e, mode);
   if (ret == -1)
   {
      errno = 0;
      ret = 1;
   }

   return ret;
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
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = pgmoneta_append(d, config->base_dir);
   if (!pgmoneta_ends_with(config->base_dir, "/"))
   {
      d = pgmoneta_append(d, "/");
   }
   d = pgmoneta_append(d, config->servers[server].name);
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
      strftime(short_date, SHORT_TIME_LENGHT, "%Y%m%d", ptm);
   }

   if (long_date != NULL)
   {
      strftime(long_date, LONG_TIME_LENGHT, "%Y%m%dT%H%M%SZ", ptm);
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

int
pgmoneta_convert_base32_to_hex(unsigned char* base32, int base32_length,
                               unsigned char** hex)
{
   int i = 0;
   char* hex_buf;

   *hex = NULL;

   hex_buf = malloc(base32_length * 2 + 1);
   memset(hex_buf, 0, base32_length * 2 + 1);

   for (i = 0; i < base32_length; i++)
   {
      sprintf(&hex_buf[i * 2], "%02x", base32[i]);
   }
   hex_buf[base32_length * 2] = 0;

   *hex = (unsigned char*)hex_buf;

   return 0;
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

bool
pgmoneta_is_file_archive(char* file_path)
{
   if (pgmoneta_ends_with(file_path, ".aes") ||
       pgmoneta_ends_with(file_path, ".zstd") ||
       pgmoneta_ends_with(file_path, ".lz4") ||
       pgmoneta_ends_with(file_path, ".bz2") ||
       pgmoneta_ends_with(file_path, ".gz"))
   {
      return true;
   }

   return false;
}

/* Parser for pgmoneta-cli amd pgmoneta-admin commands */
bool
parse_command(int argc,
              char** argv,
              int offset,
              struct pgmoneta_parsed_command* parsed,
              const struct pgmoneta_command command_table[],
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

   for (int i = 0; i < command_count; i++)
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
   else if (command_index == -1)  /* Command was matched, but subcommand was not */
   {
      if (subcommand)
      {
         warnx("Unknown subcommand '%s' for command '%s'\n", subcommand, command);
      }
      else  /* User did not type a subcommand */
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
   parsed->args[0] = parsed->args[0] ? parsed->args[0] : (char*) parsed->cmd->default_argument;

   /* Warn the user if there is enough information about deprecation */
   if (parsed->cmd->deprecated
       && pgmoneta_version_ge(parsed->cmd->deprecated_since_major,
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

#ifdef DEBUG

int
pgmoneta_backtrace(void)
{
#ifdef HAVE_LINUX
   void* array[100];
   size_t size;
   char** strings;

   size = backtrace(array, 100);
   strings = backtrace_symbols(array, size);

   for (size_t i = 0; i < size; i++)
   {
      printf("%s\n", strings[i]);
   }

   free(strings);
#endif

   return 0;
}

#endif
