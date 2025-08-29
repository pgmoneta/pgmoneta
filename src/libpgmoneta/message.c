/*
 * Copyright (C) 2025 The pgmoneta community
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
#include "server.h"
#include <pgmoneta.h>
#include <achv.h>
#include <extension.h>
#include <logging.h>
#include <manifest.h>
#include <network.h>
#include <security.h>
#include <utils.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <sys/time.h>
#include <stdio.h>

static struct message* allocate_message(size_t size);

static int read_message(int socket, bool block, int timeout, struct message** msg);
static int write_message(int socket, struct message* msg);

static int ssl_read_message(SSL* ssl, int timeout, struct message** msg);
static int ssl_write_message(SSL* ssl, struct message* msg);

static int create_D_tuple(int number_of_columns, struct message* msg, struct tuple** tuple);
static int create_C_tuple(struct message* msg, struct tuple** tuple);
static int get_number_of_columns(struct message* msg);
static int get_column_name(struct message* msg, int index, char** name);

static unsigned char* decode_base64(char* base64_data, int* decoded_len);
static char** get_paths(char* data, int* count);
static void extract_file_name(char* path, char* file_name, char* file_path);

int
pgmoneta_read_block_message(SSL* ssl, int socket, struct message** msg)
{
   if (ssl == NULL)
   {
      return read_message(socket, true, 5, msg);
   }

   return ssl_read_message(ssl, 0, msg);
}

int
pgmoneta_read_timeout_message(SSL* ssl, int socket, int timeout, struct message** msg)
{
   if (ssl == NULL)
   {
      return read_message(socket, true, timeout, msg);
   }

   return ssl_read_message(ssl, timeout, msg);
}

int
pgmoneta_write_message(SSL* ssl, int socket, struct message* msg)
{
   if (ssl == NULL)
   {
      return write_message(socket, msg);
   }

   return ssl_write_message(ssl, msg);
}

void
pgmoneta_clear_message(void)
{
   pgmoneta_memory_free();
}

struct message*
pgmoneta_copy_message(struct message* msg)
{
   struct message* copy = NULL;

#ifdef DEBUG
   assert(msg != NULL);
   assert(msg->data != NULL);
   assert(msg->length > 0);
#endif

   copy = allocate_message(msg->length);

   copy->kind = msg->kind;

   memcpy(copy->data, msg->data, msg->length);

   return copy;
}

void
pgmoneta_free_message(struct message* msg)
{
   if (msg)
   {
      if (msg->data)
      {
         free(msg->data);
         msg->data = NULL;
      }

      free(msg);
      msg = NULL;
   }
}

void
pgmoneta_log_message(struct message* msg)
{
   if (msg == NULL)
   {
      pgmoneta_log_info("Message is NULL");
   }
   else if (msg->data == NULL)
   {
      pgmoneta_log_info("Message DATA is NULL");
   }
   else
   {
      pgmoneta_log_mem(msg->data, msg->length);
   }
}

void
pgmoneta_log_copyfail_message(struct message* msg)
{
   if (msg == NULL || msg->kind != 'f')
   {
      return;
   }

   pgmoneta_log_error("COPY-failure: %s", (char*) msg->data);
}

void
pgmoneta_log_error_response_message(struct message* msg)
{
   ssize_t offset = 1 + 4;
   signed char field_type = 0;
   char* error = NULL;
   char* error_code = NULL;

   if (msg == NULL || msg->kind != 'E')
   {
      return;
   }

   pgmoneta_extract_error_fields('M', msg, &error);
   pgmoneta_extract_error_fields('C', msg, &error_code);

   pgmoneta_log_error("error response message: %s (SQLSTATE code: %s)", error, error_code);

   while (offset < msg->length)
   {
      field_type = pgmoneta_read_byte(msg->data + offset);

      if (field_type == '\0')
      {
         break;
      }

      offset += 1;

      if (field_type != 'M' && field_type != 'C')
      {
         pgmoneta_log_debug("error response field type: %c, message: %s", field_type, msg->data + offset);
      }

      offset += (strlen(msg->data + offset) + 1);
   }

   free(error_code);
   free(error);
}

void
pgmoneta_log_notice_response_message(struct message* msg)
{
   ssize_t offset = 1 + 4;
   signed char field_type = 0;
   char* error = NULL;
   char* error_code = NULL;

   if (msg == NULL || msg->kind != 'N')
   {
      return;
   }

   pgmoneta_extract_error_fields('M', msg, &error);
   pgmoneta_extract_error_fields('C', msg, &error_code);

   pgmoneta_log_warn("notice response message: %s (SQLSTATE code: %s)", error, error_code);

   while (offset < msg->length)
   {
      field_type = pgmoneta_read_byte(msg->data + offset);

      if (field_type == '\0')
      {
         break;
      }

      offset += 1;

      if (field_type != 'M' && field_type != 'C')
      {
         pgmoneta_log_debug("notice response field type: %c, message: %s", field_type, msg->data + offset);
      }

      offset += (strlen(msg->data + offset) + 1);
   }

   free(error_code);
   free(error);
}

int
pgmoneta_write_empty(SSL* ssl, int socket)
{
   char zero[1];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&zero, 0, sizeof(zero));

   msg.kind = 0;
   msg.length = 1;
   msg.data = &zero;

   if (ssl == NULL)
   {
      return write_message(socket, &msg);
   }

   return ssl_write_message(ssl, &msg);
}

int
pgmoneta_write_notice(SSL* ssl, int socket)
{
   char notice[1];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&notice, 0, sizeof(notice));

   notice[0] = 'N';

   msg.kind = 'N';
   msg.length = 1;
   msg.data = &notice;

   if (ssl == NULL)
   {
      return write_message(socket, &msg);
   }

   return ssl_write_message(ssl, &msg);
}

int
pgmoneta_write_tls(SSL* ssl, int socket)
{
   char tls[1];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&tls, 0, sizeof(tls));

   tls[0] = 'S';

   msg.kind = 'S';
   msg.length = 1;
   msg.data = &tls;

   if (ssl == NULL)
   {
      return write_message(socket, &msg);
   }

   return ssl_write_message(ssl, &msg);
}

int
pgmoneta_write_terminate(SSL* ssl, int socket)
{
   char terminate[5];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&terminate, 0, sizeof(terminate));

   pgmoneta_write_byte(&terminate, 'X');
   pgmoneta_write_int32(&(terminate[1]), 4);

   msg.kind = 'X';
   msg.length = 5;
   msg.data = &terminate;

   if (ssl == NULL)
   {
      return write_message(socket, &msg);
   }

   return ssl_write_message(ssl, &msg);
}

int
pgmoneta_write_connection_refused(SSL* ssl, int socket)
{
   int size = 46;
   char connection_refused[size];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&connection_refused, 0, sizeof(connection_refused));

   pgmoneta_write_byte(&connection_refused, 'E');
   pgmoneta_write_int32(&(connection_refused[1]), size - 1);
   pgmoneta_write_string(&(connection_refused[5]), "SFATAL");
   pgmoneta_write_string(&(connection_refused[12]), "VFATAL");
   pgmoneta_write_string(&(connection_refused[19]), "C53300");
   pgmoneta_write_string(&(connection_refused[26]), "Mconnection refused");

   msg.kind = 'E';
   msg.length = size;
   msg.data = &connection_refused;

   if (ssl == NULL)
   {
      return write_message(socket, &msg);
   }

   return ssl_write_message(ssl, &msg);
}

int
pgmoneta_write_connection_refused_old(SSL* ssl, int socket)
{
   int size = 20;
   char connection_refused[size];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&connection_refused, 0, sizeof(connection_refused));

   pgmoneta_write_byte(&connection_refused, 'E');
   pgmoneta_write_string(&(connection_refused[1]), "connection refused");

   msg.kind = 'E';
   msg.length = size;
   msg.data = &connection_refused;

   if (ssl == NULL)
   {
      return write_message(socket, &msg);
   }

   return ssl_write_message(ssl, &msg);
}

int
pgmoneta_create_auth_password_response(char* password, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 6 + strlen(password);

   m = allocate_message(size);

   m->kind = 'p';

   pgmoneta_write_byte(m->data, 'p');
   pgmoneta_write_int32(m->data + 1, size - 1);
   pgmoneta_write_string(m->data + 5, password);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_auth_md5_response(char* md5, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + strlen(md5) + 1;

   m = allocate_message(size);

   m->kind = 'p';

   pgmoneta_write_byte(m->data, 'p');
   pgmoneta_write_int32(m->data + 1, size - 1);
   pgmoneta_write_string(m->data + 5, md5);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_write_auth_scram256(SSL* ssl, int socket)
{
   char scram[24];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&scram, 0, sizeof(scram));

   scram[0] = 'R';
   pgmoneta_write_int32(&(scram[1]), 23);
   pgmoneta_write_int32(&(scram[5]), 10);
   pgmoneta_write_string(&(scram[9]), "SCRAM-SHA-256");

   msg.kind = 'R';
   msg.length = 24;
   msg.data = &scram;

   if (ssl == NULL)
   {
      return write_message(socket, &msg);
   }

   return ssl_write_message(ssl, &msg);
}

int
pgmoneta_create_auth_scram256_response(char* nounce, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + 13 + 4 + 9 + strlen(nounce);

   m = allocate_message(size);

   m->kind = 'p';

   pgmoneta_write_byte(m->data, 'p');
   pgmoneta_write_int32(m->data + 1, size - 1);
   pgmoneta_write_string(m->data + 5, "SCRAM-SHA-256");
   pgmoneta_write_string(m->data + 22, " n,,n=,r=");
   pgmoneta_write_string(m->data + 31, nounce);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_auth_scram256_continue(char* cn, char* sn, char* salt, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + 4 + 2 + strlen(cn) + strlen(sn) + 3 + strlen(salt) + 7;

   m = allocate_message(size);

   m->kind = 'R';

   pgmoneta_write_byte(m->data, 'R');
   pgmoneta_write_int32(m->data + 1, size - 1);
   pgmoneta_write_int32(m->data + 5, 11);
   pgmoneta_write_string(m->data + 9, "r=");
   pgmoneta_write_string(m->data + 11, cn);
   pgmoneta_write_string(m->data + 11 + strlen(cn), sn);
   pgmoneta_write_string(m->data + 11 + strlen(cn) + strlen(sn), ",s=");
   pgmoneta_write_string(m->data + 11 + strlen(cn) + strlen(sn) + 3, salt);
   pgmoneta_write_string(m->data + 11 + strlen(cn) + strlen(sn) + 3 + strlen(salt), ",i=4096");

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_auth_scram256_continue_response(char* wp, char* p, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + strlen(wp) + 3 + strlen(p);

   m = allocate_message(size);

   m->kind = 'p';

   pgmoneta_write_byte(m->data, 'p');
   pgmoneta_write_int32(m->data + 1, size - 1);
   pgmoneta_write_string(m->data + 5, wp);
   pgmoneta_write_string(m->data + 5 + strlen(wp), ",p=");
   pgmoneta_write_string(m->data + 5 + strlen(wp) + 3, p);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_auth_scram256_final(char* ss, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + 4 + 2 + strlen(ss);

   m = allocate_message(size);

   m->kind = 'R';

   pgmoneta_write_byte(m->data, 'R');
   pgmoneta_write_int32(m->data + 1, size - 1);
   pgmoneta_write_int32(m->data + 5, 12);
   pgmoneta_write_string(m->data + 9, "v=");
   pgmoneta_write_string(m->data + 11, ss);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_write_auth_success(SSL* ssl, int socket)
{
   char success[9];
   struct message msg;

   memset(&msg, 0, sizeof(struct message));
   memset(&success, 0, sizeof(success));

   success[0] = 'R';
   pgmoneta_write_int32(&(success[1]), 8);
   pgmoneta_write_int32(&(success[5]), 0);

   msg.kind = 'R';
   msg.length = 9;
   msg.data = &success;

   if (ssl == NULL)
   {
      return write_message(socket, &msg);
   }

   return ssl_write_message(ssl, &msg);
}

int
pgmoneta_create_ssl_message(struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 8;

   m = allocate_message(size);

   m->kind = 0;

   pgmoneta_write_int32(m->data, size);
   pgmoneta_write_int32(m->data + 4, 80877103);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_startup_message(char* username, char* database, bool replication, struct message** msg)
{
   struct message* m = NULL;
   size_t size;
   size_t us;
   size_t ds;

   us = strlen(username);
   ds = strlen(database);
   size = 4 + 4 + 4 + 1 + us + 1 + 8 + 1 + ds + 1 + 17 + 9 + 1;

   if (replication)
   {
      size += 14;
   }

   m = allocate_message(size);

   m->kind = 0;

   pgmoneta_write_int32(m->data, size);
   pgmoneta_write_int32(m->data + 4, 196608);
   pgmoneta_write_string(m->data + 8, "user");
   pgmoneta_write_string(m->data + 13, username);
   pgmoneta_write_string(m->data + 13 + us + 1, "database");
   pgmoneta_write_string(m->data + 13 + us + 1 + 9, database);
   pgmoneta_write_string(m->data + 13 + us + 1 + 9 + ds + 1, "application_name");
   pgmoneta_write_string(m->data + 13 + us + 1 + 9 + ds + 1 + 17, "pgmoneta");

   if (replication)
   {
      pgmoneta_write_string(m->data + 13 + us + 1 + 9 + ds + 1 + 17 + 9, "replication");
      pgmoneta_write_string(m->data + 13 + us + 1 + 9 + ds + 1 + 17 + 9 + 12, "1");
   }

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_identify_system_message(struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + 17;

   m = allocate_message(size);

   m->kind = 'Q';

   pgmoneta_write_byte(m->data, 'Q');
   pgmoneta_write_int32(m->data + 1, size - 1);
   pgmoneta_write_string(m->data + 5, "IDENTIFY_SYSTEM;");

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_timeline_history_message(int timeline, struct message** msg)
{
   char tl[8];
   struct message* m = NULL;
   size_t size;

   memset(&tl[0], 0, sizeof(tl));
   snprintf(&tl[0], sizeof(tl), "%d", timeline);

   size = 1 + 4 + 17 + strlen(tl) + 1 + 1;

   m = allocate_message(size);

   m->kind = 'Q';

   pgmoneta_write_byte(m->data, 'Q');
   pgmoneta_write_int32(m->data + 1, size - 1);
   pgmoneta_write_string(m->data + 5, "TIMELINE_HISTORY ");
   memcpy(m->data + 5 + 17, tl, strlen(tl));
   pgmoneta_write_string(m->data + 5 + 17 + strlen(tl), ";");

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_read_replication_slot_message(char* slot, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + 22 + strlen(slot) + 1 + 1;

   m = allocate_message(size);

   m->kind = 'Q';

   pgmoneta_write_byte(m->data, 'Q');
   pgmoneta_write_int32(m->data + 1, size - 1);
   pgmoneta_write_string(m->data + 5, "READ_REPLICATION_SLOT ");
   pgmoneta_write_string(m->data + 5 + 22, slot);
   pgmoneta_write_string(m->data + 5 + 22 + strlen(slot), ";");

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_start_replication_message(char* xlogpos, int timeline, char* slot, struct message** msg)
{
   char cmd[1024];
   struct message* m = NULL;
   size_t size;

   memset(&cmd[0], 0, sizeof(cmd));

   if (slot != NULL && strlen(slot) > 0)
   {
      if (xlogpos != NULL && strlen(xlogpos) > 0)
      {
         snprintf(&cmd[0], sizeof(cmd), "START_REPLICATION SLOT %s PHYSICAL %s TIMELINE %d;", slot, xlogpos, timeline);
      }
      else
      {
         snprintf(&cmd[0], sizeof(cmd), "START_REPLICATION SLOT %s PHYSICAL 0/0 TIMELINE %d;", slot, timeline);
      }
   }
   else
   {
      if (xlogpos != NULL && strlen(xlogpos) > 0)
      {
         snprintf(&cmd[0], sizeof(cmd), "START_REPLICATION PHYSICAL %s TIMELINE %d;", xlogpos, timeline);
      }
      else
      {
         snprintf(&cmd[0], sizeof(cmd), "START_REPLICATION PHYSICAL 0/0 TIMELINE %d;", timeline);
      }
   }

   size = 1 + 4 + strlen(cmd) + 1;

   m = allocate_message(size);

   m->kind = 'Q';

   pgmoneta_write_byte(m->data, 'Q');
   pgmoneta_write_int32(m->data + 1, size - 1);
   memcpy(m->data + 5, &cmd[0], strlen(cmd));

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_standby_status_update_message(int64_t received, int64_t flushed, int64_t applied, struct message** msg)
{
   struct message* m = NULL;
   size_t size;

   size = 1 + 4 + 1 + 8 + 8 + 8 + 8 + 1;

   m = allocate_message(size);

   m->kind = 'd';

   pgmoneta_write_byte(m->data, 'd');
   pgmoneta_write_int32(m->data + 1, size - 1);
   pgmoneta_write_byte(m->data + 1 + 4, 'r');
   pgmoneta_write_int64(m->data + 1 + 4 + 1, received);
   pgmoneta_write_int64(m->data + 1 + 4 + 1 + 8, flushed);
   pgmoneta_write_int64(m->data + 1 + 4 + 1 + 8 + 8, applied);
   pgmoneta_write_int64(m->data + 1 + 4 + 1 + 8 + 8 + 8, pgmoneta_get_current_timestamp() - pgmoneta_get_y2000_timestamp());
   pgmoneta_write_byte(m->data + 1 + 4 + 1 + 8 + 8 + 8 + 8, 0);

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_base_backup_message(int server_version, bool incremental, char* label, bool include_wal,
                                    int compression, int compression_level,
                                    struct message** msg)
{
   bool use_new_format = server_version >= 15;
   char cmd[1024];
   char* options = NULL;
   struct message* m = NULL;
   size_t size;

   memset(&cmd[0], 0, sizeof(cmd));
   // other options are
   if (use_new_format)
   {
      if (incremental)
      {
         options = pgmoneta_append(options, "INCREMENTAL, ");
      }
      options = pgmoneta_append(options, "LABEL '");
      options = pgmoneta_append(options, label);
      options = pgmoneta_append(options, "', ");

      if (include_wal)
      {
         options = pgmoneta_append(options, "WAL true, ");

         options = pgmoneta_append(options, "WAIT false, ");
      }
      else
      {
         options = pgmoneta_append(options, "WAL false, ");
      }

      if (compression == COMPRESSION_SERVER_GZIP)
      {
         options = pgmoneta_append(options, "COMPRESSION 'gzip', ");
         options = pgmoneta_append(options, "COMPRESSION_DETAIL 'level=");
         options = pgmoneta_append_int(options, compression_level);
         options = pgmoneta_append(options, "', ");
      }
      else if (compression == COMPRESSION_SERVER_ZSTD)
      {
         options = pgmoneta_append(options, "COMPRESSION 'zstd', ");
         options = pgmoneta_append(options, "COMPRESSION_DETAIL 'level=");
         options = pgmoneta_append_int(options, compression_level);
         options = pgmoneta_append(options, ",workers=4', ");
      }
      else if (compression == COMPRESSION_SERVER_LZ4)
      {
         options = pgmoneta_append(options, "COMPRESSION 'lz4', ");
         options = pgmoneta_append(options, "COMPRESSION_DETAIL 'level=");
         options = pgmoneta_append_int(options, compression_level);
         options = pgmoneta_append(options, "', ");
      }

      options = pgmoneta_append(options, "CHECKPOINT 'fast', ");

      options = pgmoneta_append(options, "MANIFEST 'yes', ");

      options = pgmoneta_append(options, "MANIFEST_CHECKSUMS 'SHA512'");

      snprintf(cmd, sizeof(cmd), "BASE_BACKUP (%s)", options);
   }
   else
   {
      options = pgmoneta_append(options, "LABEL '");
      options = pgmoneta_append(options, label);
      options = pgmoneta_append(options, "' ");

      options = pgmoneta_append(options, "FAST ");

      if (include_wal)
      {
         options = pgmoneta_append(options, "WAL ");

         options = pgmoneta_append(options, "NOWAIT ");
      }

      options = pgmoneta_append(options, "MANIFEST 'yes' ");

      options = pgmoneta_append(options, "MANIFEST_CHECKSUMS 'SHA512'");

      snprintf(cmd, sizeof(cmd), "BASE_BACKUP %s;", options);
   }

   if (options != NULL)
   {
      free(options);
      options = NULL;
   }

   size = 1 + 4 + strlen(cmd) + 1;

   m = allocate_message(size);

   m->kind = 'Q';

   pgmoneta_write_byte(m->data, 'Q');
   pgmoneta_write_int32(m->data + 1, size - 1);
   memcpy(m->data + 5, &cmd[0], strlen(cmd));

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_replication_slot_message(char* create_slot_name, struct message** msg, int version)
{
   char cmd[1024];
   struct message* m = NULL;
   size_t size;

   memset(&cmd[0], 0, sizeof(cmd));

   if (version >= 15)
   {
      snprintf(cmd, sizeof(cmd), "CREATE_REPLICATION_SLOT %s PHYSICAL (RESERVE_WAL true);", create_slot_name);
   }
   else
   {
      snprintf(cmd, sizeof(cmd), "CREATE_REPLICATION_SLOT %s PHYSICAL RESERVE_WAL;", create_slot_name);
   }

   size = 1 + 4 + strlen(cmd) + 1;

   m = allocate_message(size);

   m->kind = 'Q';

   pgmoneta_write_byte(m->data, 'Q');
   pgmoneta_write_int32(m->data + 1, size - 1);
   memcpy(m->data + 5, &cmd[0], strlen(cmd));

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_create_search_replication_slot_message(char* slot_name, struct message** msg)
{
   char cmd[1024];
   struct message* m = NULL;
   size_t size;

   memset(&cmd[0], 0, sizeof(cmd));

   snprintf(cmd, sizeof(cmd), "SELECT slot_name, slot_type FROM pg_replication_slots WHERE slot_name = '%s';", slot_name);

   size = 1 + 4 + strlen(cmd) + 1;

   m = allocate_message(size);

   m->kind = 'Q';

   pgmoneta_write_byte(m->data, 'Q');
   pgmoneta_write_int32(m->data + 1, size - 1);
   memcpy(m->data + 5, &cmd[0], strlen(cmd));

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_send_copy_done_message(SSL* ssl, int socket)
{
   struct message* msg = NULL;
   int size = 1 + 4;

   msg = allocate_message(size);

   msg->kind = 'c';

   pgmoneta_write_byte(msg->data, 'c');
   pgmoneta_write_int32(msg->data + 1, size - 1);

   if (pgmoneta_write_message(ssl, socket, msg) != MESSAGE_STATUS_OK)
   {
      pgmoneta_log_error("Could not send CopyDone message");
      goto error;
   }

   pgmoneta_free_message(msg);
   return 0;

error:
   pgmoneta_free_message(msg);
   return 1;
}

int
pgmoneta_create_query_message(char* query, struct message** msg)
{
   struct message* m = NULL;
   size_t size;
   char cmd[1024];

   memset(&cmd[0], 0, sizeof(cmd));
   strcpy(cmd, query);
   size = 1 + 4 + strlen(cmd) + 1;

   m = allocate_message(size);

   m->kind = 'Q';

   pgmoneta_write_byte(m->data, 'Q');
   pgmoneta_write_int32(m->data + 1, size - 1);
   memcpy(m->data + 5, &cmd[0], strlen(cmd));

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_send_copy_data(SSL* ssl, int socket, char* buffer, size_t nbytes)
{
   struct message* msg = NULL;
   size_t size = 1 + 4 + nbytes;
   msg = allocate_message(size);
   msg->kind = 'd';

   pgmoneta_write_byte(msg->data, 'd');
   pgmoneta_write_int32(msg->data + 1, size - 1);
   memcpy(msg->data + 5, &buffer[0], nbytes);

   if (pgmoneta_write_message(ssl, socket, msg) != MESSAGE_STATUS_OK)
   {
      pgmoneta_log_error("Could not send CopyData message");
      goto error;
   }

   pgmoneta_free_message(msg);
   return 0;

error:
   pgmoneta_free_message(msg);
   return 1;
}

int
pgmoneta_query_execute(SSL* ssl, int socket, struct message* msg, struct query_response** response)
{
   int status;
   int fd = -1;
   bool cont;
   int cols;
   char* name = NULL;
   struct message* rmsg = NULL;
   char* content = NULL;
   struct message* reply = NULL;
   struct query_response* r = NULL;
   struct tuple* current = NULL;
   struct tuple* ctuple = NULL;
   size_t data_size;
   void* data = pgmoneta_memory_dynamic_create(&data_size);
   size_t offset = 0;

   *response = NULL;

   status = pgmoneta_write_message(ssl, socket, msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG5))
   {
      pgmoneta_log_trace("Query request -- BEGIN");
      pgmoneta_log_message(msg);
      pgmoneta_log_trace("Query request -- END");
   }

   cont = true;
   while (cont)
   {
      status = pgmoneta_read_block_message(ssl, socket, &reply);

      if (status == MESSAGE_STATUS_OK)
      {
         data = pgmoneta_memory_dynamic_append(data, data_size, reply->data, reply->length, &data_size);

         if (pgmoneta_has_message('Z', data, data_size))
         {
            cont = false;
         }
      }
      else if (status == MESSAGE_STATUS_ZERO)
      {
         SLEEP(1000000L);
      }
      else
      {
         goto error;
      }

      pgmoneta_clear_message();
      reply = NULL;
   }

   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG5))
   {
      if (data == NULL)
      {
         pgmoneta_log_debug("Data is NULL");
      }
      else
      {
         pgmoneta_log_trace("Query response -- BEGIN");
         pgmoneta_log_mem(data, data_size);
         pgmoneta_log_trace("Query response -- END");
      }
   }

   r = (struct query_response*)malloc(sizeof(struct query_response));
   memset(r, 0, sizeof(struct query_response));

   if (pgmoneta_has_message('E', data, data_size)) /* if the response is ErrorResponse */
   {
      goto error;
   }
   if (pgmoneta_has_message('T', data, data_size)) /* if the response is RowDescription */
   {
      if (pgmoneta_extract_message_from_data('T', data, data_size, &rmsg))
      {
         goto error;
      }

      cols = get_number_of_columns(rmsg);

      r->number_of_columns = cols;

      for (int i = 0; i < cols; i++)
      {
         if (get_column_name(rmsg, i, &name))
         {
            goto error;
         }

         memcpy(&r->names[i][0], name, strlen(name));

         free(name);
         name = NULL;
      }

      while (offset < data_size)
      {
         offset = pgmoneta_extract_message_offset(offset, data, &msg);

         if (msg != NULL && msg->kind == 'D')
         {
            struct tuple* dtuple = NULL;

            create_D_tuple(cols, msg, &dtuple);

            if (r->tuples == NULL)
            {
               r->tuples = dtuple;
            }
            else
            {
               current->next = dtuple;
            }

            current = dtuple;
         }

         pgmoneta_free_message(msg);
         msg = NULL;
      }

      r->is_command_complete = false;
   }
   else if (pgmoneta_has_message('C', data, data_size)) /* if the response is CommandComplete */
   {
      if (pgmoneta_extract_message_from_data('C', data, data_size, &rmsg))
      {
         goto error;
      }
      r->number_of_columns = 1;

      create_C_tuple(rmsg, &ctuple);

      r->tuples = ctuple;
      r->is_command_complete = true;
   }
   else /* if the response is an anything else */
   {
      goto error;
   }

   *response = r;

   pgmoneta_free_message(rmsg);
   pgmoneta_memory_dynamic_destroy(data);

   free(content);

   return 0;

error:

   pgmoneta_disconnect(fd);

   pgmoneta_clear_message();
   pgmoneta_free_message(rmsg);
   pgmoneta_memory_dynamic_destroy(data);

   free(content);

   return 1;
}

bool
pgmoneta_has_message(char type, void* data, size_t data_size)
{
   size_t offset;

   offset = 0;

   while (offset < data_size)
   {
      char t = (char)pgmoneta_read_byte(data + offset);

      if (type == t)
      {
         // log error response message when we find it
         if (type == 'E')
         {
            struct message* msg = NULL;
            pgmoneta_extract_message_offset(offset, data, &msg);
            pgmoneta_log_error_response_message(msg);
            pgmoneta_free_message(msg);
         }
         return true;
      }
      else
      {
         offset += 1;
         offset += pgmoneta_read_int32(data + offset);
      }
   }

   return false;
}

char*
pgmoneta_query_response_get_data(struct query_response* response, int column)
{
   if (response == NULL || column > response->number_of_columns)
   {
      return NULL;
   }

   return response->tuples->data[column];
}

int
pgmoneta_free_query_response(struct query_response* response)
{
   struct tuple* current = NULL;
   struct tuple* next = NULL;

   if (response != NULL)
   {
      current = response->tuples;

      while (current != NULL)
      {
         next = current->next;

         for (int i = 0; i < response->number_of_columns; i++)
         {
            free(current->data[i]);
         }
         free(current->data);
         free(current);

         current = next;
      }

      free(response);
   }

   return 0;
}

void
pgmoneta_query_response_debug(struct query_response* response)
{
   int number_of_tuples = 0;
   struct tuple* t = NULL;

   if (response == NULL)
   {
      pgmoneta_log_debug("Query is NULL");
      return;
   }

   pgmoneta_log_trace("Query Response");
   pgmoneta_log_trace("Columns: %d", response->number_of_columns);

   for (int i = 0; i < response->number_of_columns; i++)
   {
      pgmoneta_log_trace("Column: %s", response->names[i]);
   }

   t = response->tuples;
   while (t != NULL)
   {
      number_of_tuples++;
      t = t->next;
   }

   pgmoneta_log_trace("Tuples: %d", number_of_tuples);
}

static struct message*
allocate_message(size_t size)
{
   struct message* m = NULL;

   m = (struct message*)malloc(sizeof(struct message));

   if (m == NULL)
   {
      goto error;
   }

   m->data = aligned_alloc((size_t)ALIGNMENT_SIZE, pgmoneta_get_aligned_size(size));

   if (m->data == NULL)
   {
      free(m);
      goto error;
   }

   m->kind = 0;
   m->length = size;
   memset(m->data, 0, size);

   return m;

error:

   return NULL;
}

static int
create_D_tuple(int number_of_columns, struct message* msg, struct tuple** tuple)
{
   int offset;
   int length;
   struct tuple* result = NULL;

   result = (struct tuple*)malloc(sizeof(struct tuple));
   memset(result, 0, sizeof(struct tuple));

   result->data = (char**)malloc(number_of_columns * sizeof(char*));
   result->next = NULL;

   offset = 7;

   for (int i = 0; i < number_of_columns; i++)
   {
      length = pgmoneta_read_int32(msg->data + offset);
      offset += 4;

      if (length > 0)
      {
         result->data[i] = (char*)malloc(length + 1);
         memset(result->data[i], 0, length + 1);
         memcpy(result->data[i], msg->data + offset, length);
         offset += length;
      }
      else
      {
         result->data[i] = NULL;
      }
   }

   *tuple = result;

   return 0;
}

static int
create_C_tuple(struct message* msg, struct tuple** tuple)
{
   int length;
   struct tuple* result = NULL;

   result = (struct tuple*)malloc(sizeof(struct tuple));
   memset(result, 0, sizeof(struct tuple));

   result->data = (char**)malloc(1 * sizeof(char*));
   result->next = NULL;

   length = pgmoneta_read_int32(msg->data + 1);
   length -= 5; // Exclude the message identifier byte and the length of message bytes (4)

   if (length > 0)
   {
      result->data[0] = (char*)malloc(length + 1);
      memset(result->data[0], 0, length + 1);
      memcpy(result->data[0], msg->data + 5, length);
   }
   else
   {
      result->data[0] = NULL;
   }

   *tuple = result;
   return 0;
}

static int
get_number_of_columns(struct message* msg)
{
   if (msg->kind == 'T')
   {
      return pgmoneta_read_int16(msg->data + 5);
   }

   return 0;
}

static int
get_column_name(struct message* msg, int index, char** name)
{
   int current = 0;
   int offset;
   int16_t cols;
   char* tmp = NULL;

   *name = NULL;

   if (msg->kind == 'T')
   {
      cols = pgmoneta_read_int16(msg->data + 5);

      if (index < cols)
      {
         offset = 7;

         while (current < index)
         {
            tmp = pgmoneta_read_string(msg->data + offset);

            offset += strlen(tmp) + 1;
            offset += 4;
            offset += 2;
            offset += 4;
            offset += 2;
            offset += 4;
            offset += 2;

            current++;
         }

         tmp = pgmoneta_read_string(msg->data + offset);

         *name = pgmoneta_append(*name, tmp);

         return 0;
      }
   }

   return 1;
}

static int
read_message(int socket, bool block, int timeout, struct message** msg)
{
   bool keep_read = false;
   ssize_t numbytes;
   struct timeval tv;
   struct message* m = NULL;

   if (unlikely(timeout > 0))
   {
      tv.tv_sec = timeout;
      tv.tv_usec = 0;
      setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
   }

   do
   {
      m = pgmoneta_memory_message();

      numbytes = read(socket, m->data, DEFAULT_BUFFER_SIZE);

      if (likely(numbytes > 0))
      {
         m->kind = (signed char)(*((char*)m->data));
         m->length = numbytes;
         *msg = m;

         if (unlikely(timeout > 0))
         {
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
         }

         return MESSAGE_STATUS_OK;
      }
      else if (numbytes == 0)
      {
         pgmoneta_memory_free();

         if ((errno == EAGAIN || errno == EWOULDBLOCK) && block)
         {
            keep_read = true;
            errno = 0;
         }
         else
         {
            if (unlikely(timeout > 0))
            {
               tv.tv_sec = 0;
               tv.tv_usec = 0;
               setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
            }

            return MESSAGE_STATUS_ZERO;
         }
      }
      else
      {
         pgmoneta_memory_free();

         if ((errno == EAGAIN || errno == EWOULDBLOCK) && block)
         {
            keep_read = true;
            errno = 0;
         }
         else
         {
            keep_read = false;
         }
      }
   }
   while (keep_read);

   if (unlikely(timeout > 0))
   {
      tv.tv_sec = 0;
      tv.tv_usec = 0;
      setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));

      pgmoneta_memory_free();
   }

   return MESSAGE_STATUS_ERROR;
}

static int
write_message(int socket, struct message* msg)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;
   ssize_t write_size;

#ifdef DEBUG
   assert(msg != NULL);
#endif

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = msg->length;

   do
   {
      keep_write = false;

      write_size = MIN(remaining, DEFAULT_BUFFER_SIZE);

      numbytes = write(socket, msg->data + offset, write_size);

      if (numbytes >= 0)
      {
         totalbytes += numbytes;
      }

      if (likely(totalbytes == msg->length))
      {
         return MESSAGE_STATUS_OK;
      }
      else if (numbytes != -1)
      {
         offset += numbytes;
         remaining -= numbytes;

         if (totalbytes == msg->length)
         {
            return MESSAGE_STATUS_OK;
         }

         keep_write = true;
         errno = 0;
      }
      else
      {
         pgmoneta_log_debug("Error %d - %zd/%zd (%zd) - %d/%s",
                            socket,
                            numbytes, totalbytes, msg->length,
                            errno, strerror(errno));

         switch (errno)
         {
            case EAGAIN:
               keep_write = true;
               break;
            default:
               keep_write = false;
               break;
         }
         errno = 0;
      }
   }
   while (keep_write);

   return MESSAGE_STATUS_ERROR;
}

static int
ssl_read_message(SSL* ssl, int timeout, struct message** msg)
{
   bool keep_read = false;
   ssize_t numbytes;
   time_t start_time;
   struct message* m = NULL;

   if (unlikely(timeout > 0))
   {
      start_time = time(NULL);
   }

   do
   {
      m = pgmoneta_memory_message();

      numbytes = SSL_read(ssl, m->data, DEFAULT_BUFFER_SIZE);

      if (likely(numbytes > 0))
      {
         m->kind = (signed char)(*((char*)m->data));
         m->length = numbytes;
         *msg = m;

         return MESSAGE_STATUS_OK;
      }
      else
      {
         int err;

         pgmoneta_memory_free();

         err = SSL_get_error(ssl, numbytes);
         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
               if (timeout > 0)
               {
                  if (difftime(time(NULL), start_time) >= timeout)
                  {
                     return MESSAGE_STATUS_ZERO;
                  }

                  /* Sleep for 100ms */
                  SLEEP(100000000L);
               }
               keep_read = true;
               break;
            case SSL_ERROR_WANT_READ:
               keep_read = true;
               break;
            case SSL_ERROR_WANT_WRITE:
               keep_read = true;
               break;
            case SSL_ERROR_WANT_CONNECT:
               keep_read = true;
               break;
            case SSL_ERROR_WANT_ACCEPT:
               keep_read = true;
               break;
            case SSL_ERROR_WANT_X509_LOOKUP:
               keep_read = true;
               break;
#ifndef HAVE_OPENBSD
            case SSL_ERROR_WANT_ASYNC:
               keep_read = true;
               break;
            case SSL_ERROR_WANT_ASYNC_JOB:
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
               keep_read = true;
               break;
#endif
            case SSL_ERROR_SYSCALL:
               pgmoneta_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_read = false;
               break;
            case SSL_ERROR_SSL:
               pgmoneta_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               keep_read = false;
               break;
         }
         ERR_clear_error();
      }
   }
   while (keep_read);

   return MESSAGE_STATUS_ERROR;
}

static int
ssl_write_message(SSL* ssl, struct message* msg)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

#ifdef DEBUG
   assert(msg != NULL);
#endif

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = msg->length;

   do
   {
      numbytes = SSL_write(ssl, msg->data + offset, remaining);

      if (likely(numbytes == msg->length))
      {
         return MESSAGE_STATUS_OK;
      }
      else if (numbytes > 0)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == msg->length)
         {
            return MESSAGE_STATUS_OK;
         }

         pgmoneta_log_debug("SSL/Write %d - %zd/%zd vs %zd", SSL_get_fd(ssl), numbytes, totalbytes, msg->length);
         keep_write = true;
         errno = 0;
      }
      else
      {
         unsigned long err = SSL_get_error(ssl, numbytes);

         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
               errno = 0;
               keep_write = true;
               break;
            case SSL_ERROR_SYSCALL:
               err = ERR_get_error();
               pgmoneta_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               pgmoneta_log_error("Reason: %s", ERR_reason_error_string(err));
               errno = 0;
               keep_write = false;
               break;
            case SSL_ERROR_SSL:
               err = ERR_get_error();
               pgmoneta_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               pgmoneta_log_error("Reason: %s", ERR_reason_error_string(err));
               errno = 0;
               keep_write = false;
               break;
         }
         ERR_clear_error();

         if (!keep_write)
         {
            return MESSAGE_STATUS_ERROR;
         }
      }
   }
   while (keep_write);

   return MESSAGE_STATUS_ERROR;
}

int
pgmoneta_read_copy_stream(int srv, SSL* ssl, int socket, struct stream_buffer* buffer)
{
   int numbytes = 0;
   bool keep_read = false;
   int err;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   /*
    * if buffer is still too full,
    * try enlarging it to be at least big enough for one TCP packet (I'm using 1500B here)
    * we don't expect it to absolutely work
    */
   if (buffer->size - buffer->end < 1500)
   {
      if (pgmoneta_memory_stream_buffer_enlarge(buffer, 1500))
      {
         pgmoneta_log_error("Fail to enlarge stream buffer");
      }
   }
   if (buffer->end >= buffer->size)
   {
      pgmoneta_log_error("Not enough space to read new copy-out data");
      goto error;
   }
   do
   {
      if (ssl != NULL)
      {
         numbytes = SSL_read(ssl, buffer->buffer + buffer->end, buffer->size - buffer->end);
      }
      else
      {
         numbytes = read(socket, buffer->buffer + buffer->end, buffer->size - buffer->end);
      }

      if (likely(numbytes > 0))
      {
         buffer->end += numbytes;
         return MESSAGE_STATUS_OK;
      }
      else if (numbytes == 0)
      {
         if (ssl != NULL)
         {
            goto ssl_error;
         }

         if (errno == EAGAIN || errno == EWOULDBLOCK)
         {
            keep_read = true;
            errno = 0;
            SLEEP(1000000L);
         }
         else
         {
            return MESSAGE_STATUS_ZERO;
         }
      }
      else
      {
         if (ssl != NULL)
         {
ssl_error:
            err = SSL_get_error(ssl, numbytes);
            switch (err)
            {
               case SSL_ERROR_ZERO_RETURN:
                  /* Sleep for 100ms */
                  SLEEP(100000000L);
                  keep_read = true;
                  break;
               case SSL_ERROR_WANT_READ:
                  keep_read = true;
                  break;
               case SSL_ERROR_WANT_WRITE:
                  keep_read = true;
                  break;
               case SSL_ERROR_WANT_CONNECT:
                  keep_read = true;
                  break;
               case SSL_ERROR_WANT_ACCEPT:
                  keep_read = true;
                  break;
               case SSL_ERROR_WANT_X509_LOOKUP:
                  keep_read = true;
                  break;
#ifndef HAVE_OPENBSD
               case SSL_ERROR_WANT_ASYNC:
                  keep_read = true;
                  break;
               case SSL_ERROR_WANT_ASYNC_JOB:
               case SSL_ERROR_WANT_CLIENT_HELLO_CB:
                  keep_read = true;
                  break;
#endif
               case SSL_ERROR_SYSCALL:
                  pgmoneta_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
                  errno = 0;
                  keep_read = false;
                  break;
               case SSL_ERROR_SSL:
                  pgmoneta_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
                  keep_read = false;
                  break;
            }
            ERR_clear_error();
         }
         else
         {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
               keep_read = true;
               errno = 0;
               SLEEP(1000000L);
            }
            else
            {
               keep_read = false;
            }
         }
      }
   }
   while (keep_read && config->running && pgmoneta_server_is_online(srv));

error:
   return MESSAGE_STATUS_ERROR;
}

int
pgmoneta_consume_copy_stream(int srv, SSL* ssl, int socket, struct stream_buffer* buffer, struct message** message)
{
   struct message* m = NULL;
   bool keep_read = false;
   int status;
   int length;

   pgmoneta_free_message(*message);
   do
   {
      while (buffer->cursor >= buffer->end)
      {
         status = pgmoneta_read_copy_stream(srv, ssl, socket, buffer);
         if (status == MESSAGE_STATUS_ZERO)
         {
            SLEEP(1000000L);
         }
         else if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
      }
      m = (struct message*)malloc(sizeof(struct message));
      m->kind = buffer->buffer[buffer->cursor++];
      // try to get message length
      while (buffer->cursor + 4 >= buffer->end)
      {
         status = pgmoneta_read_copy_stream(srv, ssl, socket, buffer);
         if (status == MESSAGE_STATUS_ZERO)
         {
            SLEEP(1000000L);
         }
         else if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
      }
      length = pgmoneta_read_int32(buffer->buffer + buffer->cursor);
      // receive the whole message even if we are going to skip it
      while (buffer->cursor + length >= buffer->end)
      {
         status = pgmoneta_read_copy_stream(srv, ssl, socket, buffer);
         if (status == MESSAGE_STATUS_ZERO)
         {
            SLEEP(1000000L);
         }
         else if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
      }
      if (m->kind != 'D' && m->kind != 'H' && m->kind != 'W' && m->kind != 'T' &&
          m->kind != 'c' && m->kind != 'f' && m->kind != 'E' && m->kind != 'd' && m->kind != 'C')
      {
         // skip this message
         keep_read = true;
         buffer->cursor += length;
         buffer->start = buffer->cursor;
         continue;
      }

      if (m->kind != 'D' && m->kind != 'T' && m->kind != 'E')
      {
         m->data = (void*) malloc(length - 4 + 1);
         m->length = length - 4;
         memset(m->data, 0, m->length + 1);
         memcpy(m->data, buffer->buffer + (buffer->cursor + 4), m->length);
      }
      else
      {
         /** include all the data in message's data buffer, i.e. include type and length info,
          * if it's a DataRow, RowDescription or ErrorResponse message
          * This is to accommodate our existing message parsing APIs for these types of messages
          */
         m->data = (void*) malloc(length + 1);
         m->length = length + 1;
         memcpy(m->data, buffer->buffer + buffer->cursor - 1, m->length);
      }

      *message = m;
      buffer->cursor += length;
      buffer->start = buffer->cursor;

      keep_read = false;

   }
   while (keep_read);

   return MESSAGE_STATUS_OK;

error:
   pgmoneta_free_message(m);
   *message = NULL;
   return status;
}

int
pgmoneta_consume_copy_stream_start(int srv, SSL* ssl, int socket, struct stream_buffer* buffer, struct message* message, struct token_bucket* network_bucket)
{
   bool keep_read = false;
   int status;
   int length;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;
   do
   {
      while (config->running && pgmoneta_server_is_online(srv) && buffer->cursor >= buffer->end)
      {
         status = pgmoneta_read_copy_stream(srv, ssl, socket, buffer);
         if (status == MESSAGE_STATUS_ZERO)
         {
            SLEEP(1000000L);
         }
         else if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
      }
      message->kind = buffer->buffer[buffer->cursor];
      // try to get message length
      while (buffer->cursor + 1 + 4 >= buffer->end)
      {
         status = pgmoneta_read_copy_stream(srv, ssl, socket, buffer);
         if (status == MESSAGE_STATUS_ZERO)
         {
            SLEEP(1000000L);
         }
         else if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
      }
      length = pgmoneta_read_int32(buffer->buffer + buffer->cursor + 1);

      if (network_bucket)
      {
         while (1)
         {
            if (!pgmoneta_token_bucket_consume(network_bucket, length))
            {
               break;
            }
            else
            {
               SLEEP(500000000L)
            }
         }
      }
      // receive the whole message even if we are going to skip it
      while (buffer->cursor + 1 + length >= buffer->end)
      {
         status = pgmoneta_read_copy_stream(srv, ssl, socket, buffer);
         if (status == MESSAGE_STATUS_ZERO)
         {
            SLEEP(1000000L);
         }
         else if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
      }
      if (message->kind != 'D' && message->kind != 'H' && message->kind != 'W' && message->kind != 'T' &&
          message->kind != 'c' && message->kind != 'f' && message->kind != 'E' && message->kind != 'd' && message->kind != 'C')
      {
         // skip this message
         keep_read = true;
         buffer->cursor += (length + 1);
         buffer->start = buffer->cursor;
         continue;
      }

      if (message->kind != 'D' && message->kind != 'T')
      {
         message->data = buffer->buffer + (buffer->cursor + 1 + 4);
         message->length = length - 4;
      }
      else
      {
         /** include all the data in message's data buffer, i.e. include type and length info,
          * if it's a DataRow or RowDescription message
          * This is to accommodate our existing message parsing APIs for these two types of messages
          */
         message->data = buffer->buffer + buffer->cursor;
         message->length = length + 1;
      }

      keep_read = false;

   }
   while (keep_read && config->running && pgmoneta_server_is_online(srv));

   return MESSAGE_STATUS_OK;

error:
   memset(message, 0, sizeof(struct message));
   return status;
}

void
pgmoneta_consume_copy_stream_end(struct stream_buffer* buffer, struct message* message)
{
   int length = pgmoneta_read_int32(buffer->buffer + buffer->cursor + 1);
   buffer->cursor += (1 + length);
   buffer->start = buffer->cursor;
   // left shift unconsumed data to reuse space
   if (buffer->start < buffer->end)
   {
      if (buffer->start > 0)
      {
         memmove(buffer->buffer, buffer->buffer + buffer->start, buffer->end - buffer->start);
         buffer->end -= buffer->start;
         buffer->cursor -= buffer->start;
         buffer->start = 0;
      }
   }
   else
   {
      buffer->start = buffer->end = buffer->cursor = 0;
   }
   message->data = NULL;
   message->length = 0;
}

int
pgmoneta_consume_data_row_messages(int srv, SSL* ssl, int socket, struct stream_buffer* buffer, struct query_response** response)
{
   int cols;
   int status;
   char* name = NULL;
   struct message* msg = (struct message*)malloc(sizeof (struct message));
   struct tuple* current = NULL;
   struct query_response* r = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (msg == NULL)
   {
      goto error;
   }

   memset(msg, 0, sizeof (struct message));

   // consume DataRow messages from stream buffer until CommandComplete
   while (config->running && pgmoneta_server_is_online(srv) && (msg == NULL || msg->kind != 'C'))
   {
      status = pgmoneta_consume_copy_stream_start(srv, ssl, socket, buffer, msg, NULL);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      if (msg->kind == 'E' || msg->kind == 'f')
      {
         pgmoneta_log_copyfail_message(msg);
         pgmoneta_log_error_response_message(msg);
         goto error;
      }

      if (msg->kind == 'T')
      {
         cols = get_number_of_columns(msg);
         r = (struct query_response*)malloc(sizeof(struct query_response));

         if (r == NULL)
         {
            goto error;
         }

         memset(r, 0, sizeof(struct query_response));

         r->number_of_columns = cols;
         for (int i = 0; i < cols; i++)
         {
            if (get_column_name(msg, i, &name))
            {
               goto error;
            }

            memcpy(&r->names[i][0], name, strlen(name));

            free(name);
            name = NULL;
         }
      }
      else if (msg->kind == 'D')
      {
         if (r == NULL)
         {
            // we should have received the RowDescription message
            goto error;
         }
         struct tuple* dtuple = NULL;

         create_D_tuple(cols, msg, &dtuple);

         if (r->tuples == NULL)
         {
            r->tuples = dtuple;
         }
         else
         {
            current->next = dtuple;
         }

         current = dtuple;
      }

      pgmoneta_consume_copy_stream_end(buffer, msg);
   }
   *response = r;
   // msg is reusable, it doesn't actually hold data,
   // but merely points to somewhere in the stream buffer
   // So no need for pgmoneta_free_message :)
   free(msg);
   msg = NULL;

   return 0;
error:
   pgmoneta_close_ssl(ssl);
   free(msg);
   pgmoneta_disconnect(socket);
   pgmoneta_free_query_response(r);
   return 1;
}

int
pgmoneta_receive_manifest_file(int srv, SSL* ssl, int socket, struct stream_buffer* buffer, char* basedir, struct token_bucket* bucket, struct token_bucket* network_bucket)
{
   char tmp_file_path[MAX_PATH];
   char file_path[MAX_PATH];
   FILE* file = NULL;
   struct message* msg = (struct message*)malloc(sizeof (struct message));

   if (msg == NULL)
   {
      goto error;
   }

   memset(msg, 0, sizeof (struct message));

   memset(tmp_file_path, 0, sizeof(tmp_file_path));
   memset(file_path, 0, sizeof(file_path));

   // Name the manifest with .tmp suffix so that we know backup is invalid if replication is interrupted
   if (pgmoneta_ends_with(basedir, "/"))
   {
      snprintf(tmp_file_path, sizeof(tmp_file_path), "%sdata/%s", basedir, "backup_manifest.tmp");
      snprintf(file_path, sizeof(file_path), "%sdata/%s", basedir, "backup_manifest");
   }
   else
   {
      snprintf(tmp_file_path, sizeof(tmp_file_path), "%s/data/%s", basedir, "backup_manifest.tmp");
      snprintf(file_path, sizeof(file_path), "%s/data/%s", basedir, "backup_manifest");
   }
   file = fopen(tmp_file_path, "wb");

   if (file == NULL)
   {
      goto error;
   }

   // get the copy out response
   while (msg == NULL || msg->kind != 'H')
   {
      pgmoneta_consume_copy_stream_start(srv, ssl, socket, buffer, msg, NULL);
      if (msg->kind == 'E' || msg->kind == 'f')
      {
         pgmoneta_log_copyfail_message(msg);
         pgmoneta_log_error_response_message(msg);
         goto error;
      }
      pgmoneta_consume_copy_stream_end(buffer, msg);
   }

   while (msg->kind != 'c')
   {
      pgmoneta_consume_copy_stream_start(srv, ssl, socket, buffer, msg, network_bucket);
      if (msg->kind == 'E' || msg->kind == 'f')
      {
         pgmoneta_log_copyfail_message(msg);
         pgmoneta_log_error_response_message(msg);
         goto error;
      }
      if (msg->kind == 'd' && msg->length > 0)
      {

         if (bucket)
         {
            while (1)
            {
               if (!pgmoneta_token_bucket_consume(bucket, msg->length))
               {
                  break;
               }
               else
               {
                  SLEEP(500000000L)
               }
            }
         }

         // copy data
         if (fwrite(msg->data, msg->length, 1, file) != 1)
         {
            pgmoneta_log_error("could not write to file %s", file_path);
            goto error;
         }
      }
      pgmoneta_consume_copy_stream_end(buffer, msg);
   }
   // finish, remove the .tmp suffix
   if (rename(tmp_file_path, file_path) != 0)
   {
      pgmoneta_log_error("could not rename file %s to %s", tmp_file_path, file_path);
      goto error;
   }
   fflush(file);
   fclose(file);
   pgmoneta_free_message(msg);
   return 0;

error:
   fflush(file);
   fclose(file);
   pgmoneta_free_message(msg);
   return 1;
}

int
pgmoneta_receive_extra_files(SSL* ssl, int socket, char* username, char* source_dir, char* target_dir, char** info_extra)
{
   int count = 0;
   char** paths = NULL;
   struct query_response* qr = NULL;

   pgmoneta_ext_privilege(ssl, socket, &qr);
   if (qr != NULL && qr->tuples != NULL && qr->tuples->data != NULL && qr->tuples->data[0] != NULL && qr->tuples->data[0][0] == 't')
   {
      pgmoneta_free_query_response(qr);
      qr = NULL;

      pgmoneta_ext_get_files(ssl, socket, source_dir, &qr);
      if (qr != NULL)
      {
         paths = get_paths(qr->tuples->data[0], &count);
         pgmoneta_free_query_response(qr);
         qr = NULL;
      }
      else
      {
         pgmoneta_log_warn("Retrieving extra files: Query failed");
         goto error;
      }

      if (paths != NULL)
      {
         if (*info_extra == NULL)
         {
            *info_extra = malloc(MAX_EXTRA_PATH);
            (*info_extra)[0] = '\0';
         }

         for (int j = 0; j < count; j++)
         {
            char* dest_dir;
            char* dest_path;
            char file_name[MAX_PATH];
            char file_path[MAX_PATH];

            extract_file_name(paths[j], file_name, file_path);

            if (file_path[0] != '\0')
            {
               dest_dir = (char*)malloc((strlen(target_dir) + strlen(file_path) + 1) * sizeof(char));
               snprintf(dest_dir, strlen(target_dir) + strlen(file_path) + 1, "%s%s", target_dir, file_path);
            }
            else
            {
               dest_dir = (char*)malloc((strlen(target_dir) + 1) * sizeof(char));
               memset(dest_dir, 0, strlen(target_dir) + 1);
               memcpy(dest_dir, target_dir, strlen(target_dir));
            }

            pgmoneta_mkdir(dest_dir);

            dest_path = (char*)malloc((strlen(dest_dir) + strlen(file_name) + 1) * sizeof(char));
            snprintf(dest_path, strlen(dest_dir) + strlen(file_name) + 1, "%s%s", dest_dir, file_name);

            pgmoneta_ext_get_file(ssl, socket, paths[j], &qr);
            if (qr != NULL && qr->tuples != NULL)
            {
               struct tuple* current_tuple = qr->tuples;
               while (current_tuple != NULL)
               {
                  if (current_tuple->data != NULL && current_tuple->data[0] != NULL)
                  {
                     int decoded_len;
                     unsigned char* decoded_data;

                     decoded_data = decode_base64(current_tuple->data[0], &decoded_len);
                     if (decoded_data != NULL)
                     {
                        FILE* file = fopen(dest_path, "wb");
                        if (file != NULL)
                        {
                           fwrite(decoded_data, 1, decoded_len, file);
                           fclose(file);
                        }
                        else
                        {
                           pgmoneta_log_error("Retrieving extra files: Could not open file \"%s\" for writing", dest_path);
                           free(dest_dir);
                           free(dest_path);
                           goto error;
                        }
                        free(decoded_data);
                     }
                  }
                  current_tuple = current_tuple->next;
               }
               if (strlen(*info_extra) == 0)
               {
                  *info_extra = pgmoneta_append(*info_extra, paths[j]);
               }
               else
               {
                  *info_extra = pgmoneta_append(*info_extra, ", ");
                  *info_extra = pgmoneta_append(*info_extra, paths[j]);
               }
            }
            else
            {
               pgmoneta_log_warn("Retrieving extra files: Query failed");
               goto error;
            }

            pgmoneta_free_query_response(qr);
            free(dest_dir);
            free(dest_path);
            free(paths[j]);
            qr = NULL;
         }
         free(paths);
      }
      else
      {
         pgmoneta_log_warn("Retrieving extra files: Incorrect path \"%s\"", source_dir);
      }

      pgmoneta_free_query_response(qr);
      return 0;
   }
   else if (qr != NULL && qr->tuples != NULL && qr->tuples->data != NULL && qr->tuples->data[0] != NULL && qr->tuples->data[0][0] == 'f')
   {
      pgmoneta_log_warn("Retrieving extra files: User %s is not SUPERUSER", username);
      goto error;
   }
   else
   {
      pgmoneta_log_warn("Retrieving extra files: Query failed");
      goto error;
   }

error:
   if (paths != NULL)
   {
      for (int i = 0; i < count; i++)
      {
         free(paths[i]);
      }
      free(paths);
   }
   pgmoneta_free_query_response(qr);

   return 1;
}

static char**
get_paths(char* data, int* count)
{
   if (data == NULL || count == NULL)
   {
      return NULL;
   }

   int index;
   size_t data_length;
   char* cleaned_data = NULL;
   char* token = NULL;
   char** paths;

   // Remove the first '{' and the last '}'
   data_length = strlen(data);
   if (data_length < 2 || data[0] != '{' || data[data_length - 1] != '}')
   {
      return NULL;
   }
   cleaned_data = (char*)malloc(data_length - 1);
   strncpy(cleaned_data, data + 1, data_length - 2);
   cleaned_data[data_length - 2] = '\0';

   if (strlen(cleaned_data) == 0)
   {
      *count = 0;
      goto error;
   }

   *count = 1;
   for (char* tmp = cleaned_data; *tmp; tmp++)
   {
      if (*tmp == ',')
      {
         (*count)++;
      }
   }

   paths = (char**)malloc(*count * sizeof(char*));
   if (paths == NULL)
   {
      goto error;
   }

   index = 0;
   token = strtok(cleaned_data, ",");
   while (token != NULL)
   {
      paths[index] = strdup(token);
      if (paths[index] == NULL)
      {
         for (int i = 0; i < index; i++)
         {
            free(paths[i]);
         }
         free(paths);
         goto error;
      }
      index++;
      token = strtok(NULL, ",");
   }

   free(cleaned_data);

   return paths;

error:
   if (cleaned_data != NULL)
   {
      free(cleaned_data);
   }

   return NULL;
}

static void
extract_file_name(char* path, char* file_name, char* file_path)
{
   char* last_slash;

   last_slash = strrchr(path, '/');

   if (last_slash != NULL)
   {
      size_t path_length;

      // Copy the file name (the part after the last '/')
      strcpy(file_name, last_slash + 1);

      // Copy the path up to and including the last '/'
      path_length = last_slash - path + 1;
      strncpy(file_path, path, path_length);
      file_path[path_length] = '\0';   // Null-terminate the file_path string
   }
   else
   {
      // If no '/' is found, the entire path is the file name and file_path is empty
      strcpy(file_name, path);
      file_path[0] = '\0';
   }
}

static unsigned char*
decode_base64(char* base64_data, int* decoded_len)
{
   size_t base64_len;
   size_t max_decoded_len;
   unsigned char* decoded_data;
   int actual_decoded_len;

   base64_len = strlen(base64_data);
   max_decoded_len = (base64_len / 4) * 3;
   decoded_data = (unsigned char*)malloc(max_decoded_len);

   // Perform base64 decoding using OpenSSL
   actual_decoded_len = EVP_DecodeBlock(decoded_data, (unsigned char*)base64_data, base64_len);

   // Handle padding
   if (base64_data[base64_len - 1] == '=')
   {
      actual_decoded_len--;
   }
   if (base64_data[base64_len - 2] == '=')
   {
      actual_decoded_len--;
   }

   if (actual_decoded_len < 0)
   {
      pgmoneta_log_error("error decode: Base64 decoding failed");
      free(decoded_data);
      return NULL;
   }

   *decoded_len = actual_decoded_len;
   return decoded_data;
}

int
pgmoneta_send_file(SSL* ssl, int socket, char* username, char* source_path, char* target_path)
{
   FILE* file = NULL;
   struct query_response* qr = NULL;
   unsigned char buffer[PGMONETA_CHUNK_SIZE];
   char* encode_chunk = NULL;
   size_t bytes_read;
   size_t encoded_size;

   // Check if the user has sufficient privileges
   pgmoneta_ext_privilege(ssl, socket, &qr);
   if (qr != NULL && qr->tuples != NULL && qr->tuples->data != NULL && qr->tuples->data[0] != NULL && qr->tuples->data[0][0] == 't')
   {
      pgmoneta_free_query_response(qr);
      qr = NULL;

      // Open the file for reading
      file = fopen(source_path, "rb");
      if (file == NULL)
      {
         pgmoneta_log_warn("Sending file: Failed to open file: %s", source_path);
         goto error;
      }

      // Send the file content in chunks
      while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
      {
         pgmoneta_base64_encode(buffer, bytes_read, &encode_chunk, &encoded_size);

         pgmoneta_ext_send_file_chunk(ssl, socket, target_path, encode_chunk, &qr);
         if (!qr)
         {
            pgmoneta_log_error("Sending file: Send file chunk failed");
            goto error;
         }
         pgmoneta_free_query_response(qr);
         free(encode_chunk);
         encode_chunk = NULL;
         qr = NULL;
      }
   }
   else if (qr != NULL && qr->tuples != NULL && qr->tuples->data != NULL && qr->tuples->data[0] != NULL && qr->tuples->data[0][0] == 'f')
   {
      pgmoneta_log_error("Sending file: User %s is not SUPERUSER", username);
      goto error;
   }
   else
   {
      pgmoneta_log_error("Sending file: Query failed");
      goto error;
   }

   fclose(file);
   if (encode_chunk)
   {
      free(encode_chunk);
   }
   pgmoneta_free_query_response(qr);

   return 0;

error:
   if (file)
   {
      fclose(file);
   }
   if (encode_chunk)
   {
      free(encode_chunk);
   }
   pgmoneta_free_query_response(qr);

   return 1;
}
