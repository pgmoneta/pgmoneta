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
#include <achv.h>
#include <logging.h>
#include <manifest.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <utils.h>
#include <io.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/time.h>
#include <stdio.h>

static int read_message(int socket, bool block, int timeout, struct message** msg);
static int write_message(int socket, struct message* msg);

static int ssl_read_message(SSL* ssl, int timeout, struct message** msg);
static int ssl_write_message(SSL* ssl, struct message* msg);

static int create_D_tuple(int number_of_columns, struct message* msg, struct tuple** tuple);
static int get_number_of_columns(struct message* msg);
static int get_column_name(struct message* msg, int index, char** name);

static bool is_server_side_compression(void);

int
pgmoneta_read_block_message(SSL* ssl, int socket, struct message** msg)
{
   if (ssl == NULL)
   {
      return read_message(socket, true, 0, msg);
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
pgmoneta_free_message(struct message* msg)
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

   copy = (struct message*)malloc(sizeof(struct message));
   copy->data = malloc(msg->length);

   copy->kind = msg->kind;
   copy->length = msg->length;
   memcpy(copy->data, msg->data, msg->length);

   return copy;
}

void
pgmoneta_free_copy_message(struct message* msg)
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
   size_t offset = 1 + 4;
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
   size_t offset = 1 + 4;
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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'p';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'p';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'p';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'R';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'p';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'R';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 0;
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 0;
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'Q';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'Q';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'Q';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'Q';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'd';
   m->length = size;

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
pgmoneta_create_base_backup_message(int server_version, char* label, bool include_wal, char* checksum_algorithm,
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

      options = pgmoneta_append(options, "MANIFEST_CHECKSUMS '");
      options = pgmoneta_append(options, checksum_algorithm);
      options = pgmoneta_append(options, "'");

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

      options = pgmoneta_append(options, "MANIFEST_CHECKSUMS '");
      options = pgmoneta_append(options, checksum_algorithm);
      options = pgmoneta_append(options, "' ");

      snprintf(cmd, sizeof(cmd), "BASE_BACKUP %s;", options);
   }

   if (options != NULL)
   {
      free(options);
      options = NULL;
   }

   size = 1 + 4 + strlen(cmd) + 1;

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'Q';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'Q';
   m->length = size;

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

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'Q';
   m->length = size;

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

   msg = (struct message*)malloc(sizeof(struct message));
   msg->data = malloc(size);

   memset(msg->data, 0, size);

   msg->kind = 'c';
   msg->length = size;

   pgmoneta_write_byte(msg->data, 'c');
   pgmoneta_write_int32(msg->data + 1, size - 1);

   if (pgmoneta_write_message(ssl, socket, msg) != MESSAGE_STATUS_OK)
   {
      pgmoneta_log_error("Could not send CopyDone message");
      goto error;
   }

   pgmoneta_free_copy_message(msg);
   return 0;

error:
   pgmoneta_free_copy_message(msg);
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
   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'Q';
   m->length = size;

   pgmoneta_write_byte(m->data, 'Q');
   pgmoneta_write_int32(m->data + 1, size - 1);
   memcpy(m->data + 5, &cmd[0], strlen(cmd));

   *msg = m;

   return MESSAGE_STATUS_OK;
}

int
pgmoneta_query_execute(SSL* ssl, int socket, struct message* msg, struct query_response** response)
{
   int status;
   int fd = -1;
   bool cont;
   int cols;
   char* name = NULL;
   struct message* tmsg = NULL;
   char* content = NULL;
   struct message* reply = NULL;
   struct query_response* r = NULL;
   struct tuple* current = NULL;
   size_t data_size;
   void* data = pgmoneta_memory_dynamic_create(&data_size);
   size_t offset = 0;

   *response = NULL;

   status = pgmoneta_write_message(ssl, socket, msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
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

      pgmoneta_free_message(reply);
      reply = NULL;
   }

   if (data == NULL)
   {
      pgmoneta_log_debug("Data is NULL");
   }
   else
   {
      pgmoneta_log_mem(data, data_size);
   }

   if (pgmoneta_has_message('E', data, data_size))
   {
      goto error;
   }

   if (pgmoneta_extract_message_from_data('T', data, data_size, &tmsg))
   {
      goto error;
   }

   cols = get_number_of_columns(tmsg);

   r = (struct query_response*)malloc(sizeof(struct query_response));
   memset(r, 0, sizeof(struct query_response));

   r->number_of_columns = cols;

   for (int i = 0; i < cols; i++)
   {
      if (get_column_name(tmsg, i, &name))
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

      pgmoneta_free_copy_message(msg);
      msg = NULL;
   }

   *response = r;

   pgmoneta_free_copy_message(tmsg);
   pgmoneta_memory_dynamic_destroy(data);

   free(content);

   return 0;

error:

   pgmoneta_disconnect(fd);

   pgmoneta_free_message(msg);
   pgmoneta_free_copy_message(tmsg);
   pgmoneta_memory_dynamic_destroy(data);

   free(content);

   return 1;
}

bool
pgmoneta_has_message(char type, void* data, size_t data_size)
{
   int offset;

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
            pgmoneta_free_copy_message(msg);
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

static bool
is_server_side_compression(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;
   return config->compression_type == COMPRESSION_SERVER_GZIP || config->compression_type == COMPRESSION_SERVER_LZ4 || config->compression_type == COMPRESSION_SERVER_ZSTD;
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
      setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
   }

   do
   {
      m = pgmoneta_memory_message();

      numbytes = read(socket, m->data, m->max_length);

      if (likely(numbytes > 0))
      {
         m->kind = (signed char)(*((char*)m->data));
         m->length = numbytes;
         *msg = m;

         if (unlikely(timeout > 0))
         {
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
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
               setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
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
      setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

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

#ifdef DEBUG
   assert(msg != NULL);
#endif

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = msg->length;

   do
   {
      numbytes = write(socket, msg->data + offset, remaining);

      if (likely(numbytes == msg->length))
      {
         return MESSAGE_STATUS_OK;
      }
      else if (numbytes != -1)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == msg->length)
         {
            return MESSAGE_STATUS_OK;
         }

         pgmoneta_log_debug("Write %d - %zd/%zd vs %zd", socket, numbytes, totalbytes, msg->length);
         keep_write = true;
         errno = 0;
      }
      else
      {
         switch (errno)
         {
            case EAGAIN:
               keep_write = true;
               errno = 0;
               break;
            default:
               keep_write = false;
               break;
         }
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

      numbytes = SSL_read(ssl, m->data, m->max_length);

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
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
#endif
#endif
               keep_read = true;
               break;
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
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
#endif
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
pgmoneta_read_copy_stream(SSL* ssl, int socket, struct stream_buffer* buffer)
{
   int numbytes = 0;
   bool keep_read = false;
   int err;
   struct configuration* config;

   config = (struct configuration*)shmem;

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
               case SSL_ERROR_WANT_READ:
               case SSL_ERROR_WANT_WRITE:
               case SSL_ERROR_WANT_CONNECT:
               case SSL_ERROR_WANT_ACCEPT:
               case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
               case SSL_ERROR_WANT_ASYNC:
               case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
               case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
#endif
#endif
                  keep_read = true;
                  break;
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
   while (keep_read && config->running);

error:
   return MESSAGE_STATUS_ERROR;
}

int
pgmoneta_consume_copy_stream(SSL* ssl, int socket, struct stream_buffer* buffer, struct message** message)
{
   struct message* m = NULL;
   bool keep_read = false;
   int status;
   int length;

   pgmoneta_free_copy_message(*message);
   do
   {
      while (buffer->cursor >= buffer->end)
      {
         status = pgmoneta_read_copy_stream(ssl, socket, buffer);
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
         status = pgmoneta_read_copy_stream(ssl, socket, buffer);
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
         status = pgmoneta_read_copy_stream(ssl, socket, buffer);
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
   pgmoneta_free_copy_message(m);
   *message = NULL;
   return status;
}

int
pgmoneta_consume_copy_stream_start(SSL* ssl, int socket, struct stream_buffer* buffer, struct message* message, struct token_bucket* network_bucket)
{
   bool keep_read = false;
   int status;
   int length;
   struct configuration* config;

   config = (struct configuration*)shmem;
   do
   {
      while (config->running && buffer->cursor >= buffer->end)
      {
         status = pgmoneta_read_copy_stream(ssl, socket, buffer);
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
         status = pgmoneta_read_copy_stream(ssl, socket, buffer);
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
         status = pgmoneta_read_copy_stream(ssl, socket, buffer);
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
   while (keep_read && config->running);

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
pgmoneta_consume_data_row_messages(SSL* ssl, int socket, struct stream_buffer* buffer, struct query_response** response)
{
   int cols;
   int status;
   char* name = NULL;
   struct message* msg = (struct message*)malloc(sizeof (struct message));
   struct tuple* current = NULL;
   struct query_response* r = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (msg == NULL)
   {
      goto error;
   }

   memset(msg, 0, sizeof (struct message));

   // consume DataRow messages from stream buffer until CommandComplete
   while (config->running && (msg == NULL || msg->kind != 'C'))
   {
      status = pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, NULL);

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
   pgmoneta_free_copy_message(msg);
   msg = NULL;

   return 0;
error:
   pgmoneta_close_ssl(ssl);
   pgmoneta_free_copy_message(msg);
   pgmoneta_disconnect(socket);
   pgmoneta_free_query_response(r);
   return 1;
}

int
pgmoneta_receive_archive_files(SSL* ssl, int socket, struct stream_buffer* buffer, char* basedir, struct tablespace* tablespaces, int version, struct token_bucket* bucket, struct token_bucket* network_bucket)
{
   char directory[MAX_PATH];
   char link_path[MAX_PATH];
   char null_buffer[2 * 512]; // 2 tar block size of terminator null bytes
   FILE* file = NULL;
   struct query_response* response = NULL;
   struct message* msg = (struct message*)malloc(sizeof (struct message));
   struct tuple* tup = NULL;

   memset(msg, 0, sizeof (struct message));

   // Receive the second result set
   if (pgmoneta_consume_data_row_messages(ssl, socket, buffer, &response))
   {
      goto error;
   }
   tup = response->tuples;
   while (tup != NULL)
   {
      char file_path[MAX_PATH];
      char directory[MAX_PATH];
      memset(file_path, 0, sizeof(file_path));
      memset(directory, 0, sizeof(directory));
      if (tup->data[1] == NULL)
      {
         // main data directory
         if (pgmoneta_ends_with(basedir, "/"))
         {
            snprintf(file_path, sizeof(file_path), "%sdata/%s", basedir, "base.tar");
            snprintf(directory, sizeof(directory), "%sdata/", basedir);
         }
         else
         {
            snprintf(file_path, sizeof(file_path), "%s/data/%s", basedir, "base.tar");
            snprintf(directory, sizeof(directory), "%s/data/", basedir);
         }
      }
      else
      {
         // user level tablespace
         struct tablespace* tblspc = tablespaces;
         while (tblspc != NULL)
         {
            if (pgmoneta_compare_string(tup->data[1], tblspc->path))
            {
               tblspc->oid = atoi(tup->data[0]);
               break;
            }
            tblspc = tblspc->next;
         }
         if (pgmoneta_ends_with(basedir, "/"))
         {
            snprintf(file_path, sizeof(file_path), "%s%s/%s.tar", basedir, tblspc->name, tblspc->name);
            snprintf(directory, sizeof(directory), "%s%s/", basedir, tblspc->name);
         }
         else
         {
            snprintf(file_path, sizeof(file_path), "%s/%s/%s.tar", basedir, tblspc->name, tblspc->name);
            snprintf(directory, sizeof(directory), "%s/%s/", basedir, tblspc->name);
         }
      }
      pgmoneta_mkdir(directory);
      file = pgmoneta_open_file(file_path, "wb");
      if (file == NULL)
      {
         pgmoneta_log_error("Could not create archive tar file");
         goto error;
      }
      // get the copy out response
      while (msg == NULL || msg->kind != 'H')
      {
         pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, NULL);
         if (msg->kind == 'E' || msg->kind == 'f')
         {
            pgmoneta_log_copyfail_message(msg);
            pgmoneta_log_error_response_message(msg);
            fflush(file);
            fclose(file);
            goto error;
         }
         pgmoneta_consume_copy_stream_end(buffer, msg);
      }
      while (msg->kind != 'c')
      {
         pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, network_bucket);
         if (msg->kind == 'E' || msg->kind == 'f')
         {
            pgmoneta_log_copyfail_message(msg);
            pgmoneta_log_error_response_message(msg);
            fflush(file);
            fclose(file);
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
            if (pgmoneta_write_file(msg->data, msg->length, 1, file) != 1)
            {
               pgmoneta_log_error("could not write to file %s", file_path);
               fflush(file);
               fclose(file);
               goto error;
            }
         }
         pgmoneta_consume_copy_stream_end(buffer, msg);
      }
      //append two blocks of null bytes to the end of the tar file
      memset(null_buffer, 0, 2 * 512);
      if (pgmoneta_write_file(null_buffer, 2 * 512, 1, file) != 1)
      {
         pgmoneta_log_error("could not write to file %s", file_path);
         fflush(file);
         fclose(file);
         goto error;
      }
      fflush(file);
      fclose(file);

      // extract the file
      pgmoneta_extract_tar_file(file_path, directory);
      remove(file_path);
      pgmoneta_free_copy_message(msg);

      msg = NULL;
      tup = tup->next;
   }

   if (pgmoneta_receive_manifest_file(ssl, socket, buffer, basedir, bucket, network_bucket))
   {
      goto error;
   }

   // update symbolic link
   struct tablespace* tblspc = tablespaces;
   while (tblspc != NULL)
   {
      memset(link_path, 0, sizeof(link_path));
      memset(directory, 0, sizeof(directory));

      if (pgmoneta_ends_with(basedir, "/"))
      {
         snprintf(link_path, sizeof(link_path), "%sdata/pg_tblspc/%d", basedir, tblspc->oid);
         snprintf(directory, sizeof(directory), "%s%s/", basedir, tblspc->name);
      }
      else
      {
         snprintf(link_path, sizeof(link_path), "%s/data/pg_tblspc/%d", basedir, tblspc->oid);
         snprintf(directory, sizeof(directory), "%s/%s/", basedir, tblspc->name);
      }

      unlink(link_path);
      pgmoneta_symlink_file(link_path, directory);
      tblspc = tblspc->next;
   }

   memset(directory, 0, sizeof(directory));

   if (pgmoneta_ends_with(basedir, "/"))
   {
      snprintf(directory, sizeof(directory), "%sdata", basedir);
   }
   else
   {
      snprintf(directory, sizeof(directory), "%s/data", basedir);
   }

   if (pgmoneta_manifest_checksum_verify(directory))
   {
      pgmoneta_log_error("Manifest verification failed");
      goto error;
   }

   pgmoneta_free_query_response(response);
   pgmoneta_free_copy_message(msg);
   return 0;

error:
   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   pgmoneta_free_query_response(response);
   pgmoneta_free_copy_message(msg);
   return 1;
}

int
pgmoneta_receive_archive_stream(SSL* ssl, int socket, struct stream_buffer* buffer, char* basedir, struct tablespace* tablespaces, struct token_bucket* bucket, struct token_bucket* network_bucket)
{
   struct query_response* response = NULL;
   struct message* msg = (struct message*)malloc(sizeof (struct message));
   struct tuple* tup = NULL;
   struct tablespace* tblspc = NULL;
   char null_buffer[2 * 512];
   char file_path[MAX_PATH];
   char directory[MAX_PATH];
   char link_path[MAX_PATH];
   char tmp_manifest_file_path[MAX_PATH];
   char manifest_file_path[MAX_PATH];
   memset(file_path, 0, sizeof(file_path));
   memset(directory, 0, sizeof(directory));
   memset(link_path, 0, sizeof(link_path));
   memset(manifest_file_path, 0, sizeof(manifest_file_path));
   memset(tmp_manifest_file_path, 0, sizeof(tmp_manifest_file_path));
   memset(null_buffer, 0, 2 * 512);
   char type;
   FILE* file = NULL;

   if (msg == NULL)
   {
      goto error;
   }

   memset(msg, 0, sizeof(struct message));

   // Receive the second result set
   if (pgmoneta_consume_data_row_messages(ssl, socket, buffer, &response))
   {
      goto error;
   }
   while (msg == NULL || msg->kind != 'H')
   {
      pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, NULL);
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
      pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, network_bucket);
      if (msg->kind == 'E' || msg->kind == 'f')
      {
         pgmoneta_log_copyfail_message(msg);
         pgmoneta_log_error_response_message(msg);
         goto error;
      }
      if (msg->kind == 'd')
      {
         type = *((char*)msg->data);
         switch (type)
         {
            case 'n':
            {
               // append two blocks of null buffer and extract the tar file
               if (file != NULL)
               {
                  if ((!is_server_side_compression()) && pgmoneta_write_file(null_buffer, 2 * 512, 1, file) != 1)
                  {
                     pgmoneta_log_error("could not write to file %s", file_path);
                     fflush(file);
                     fclose(file);
                     file = NULL;
                     goto error;
                  }
                  fflush(file);
                  fclose(file);
                  file = NULL;
                  pgmoneta_extract_tar_file(file_path, directory);
                  remove(file_path);
               }
               // new tablespace or main directory tar file
               char* archive_name = pgmoneta_read_string(msg->data + 1);
               char* archive_path = pgmoneta_read_string(msg->data + 1 + strlen(archive_name) + 1);

               memset(file_path, 0, sizeof(file_path));
               memset(directory, 0, sizeof(directory));
               // The tablespace order in the second result set is presumably the same as the order in which the server sends tablespaces
               tblspc = tablespaces;
               if (tup == NULL)
               {
                  tup = response->tuples;
               }
               else
               {
                  tup = tup->next;
               }
               if (tup->data[1] == NULL)
               {
                  // main data directory
                  if (pgmoneta_ends_with(basedir, "/"))
                  {
                     snprintf(file_path, sizeof(file_path), "%sdata/%s", basedir, "base.tar");
                     snprintf(directory, sizeof(directory), "%sdata/", basedir);
                  }
                  else
                  {
                     snprintf(file_path, sizeof(file_path), "%s/data/%s", basedir, "base.tar");
                     snprintf(directory, sizeof(directory), "%s/data/", basedir);
                  }
               }
               else
               {
                  // user level tablespace
                  tblspc = tablespaces;
                  while (tblspc != NULL)
                  {
                     if (pgmoneta_compare_string(tblspc->path, archive_path))
                     {
                        tblspc->oid = atoi(tup->data[0]);
                        break;
                     }
                     tblspc = tblspc->next;
                  }
                  if (pgmoneta_ends_with(basedir, "/"))
                  {
                     snprintf(file_path, sizeof(file_path), "%s%s/%s.tar", basedir, tblspc->name, tblspc->name);
                     snprintf(directory, sizeof(directory), "%s%s/", basedir, tblspc->name);
                  }
                  else
                  {
                     snprintf(file_path, sizeof(file_path), "%s/%s/%s.tar", basedir, tblspc->name, tblspc->name);
                     snprintf(directory, sizeof(directory), "%s/%s/", basedir, tblspc->name);
                  }
               }
               pgmoneta_mkdir(directory);
               file = pgmoneta_open_file(file_path, "wb");
               if (file == NULL)
               {
                  pgmoneta_log_error("Could not create archive tar file");
                  goto error;
               }
               break;
            }
            case 'm':
            {
               // start of manifest, finish off previous data archive receiving
               if (file != NULL)
               {
                  if ((!is_server_side_compression()) && pgmoneta_write_file(null_buffer, 2 * 512, 1, file) != 1)
                  {
                     pgmoneta_log_error("could not write to file %s", file_path);
                     fflush(file);
                     fclose(file);
                     file = NULL;
                     goto error;
                  }
                  fflush(file);
                  fclose(file);
                  file = NULL;
                  pgmoneta_extract_tar_file(file_path, directory);
                  remove(file_path);
               }
               if (pgmoneta_ends_with(basedir, "/"))
               {
                  snprintf(tmp_manifest_file_path, sizeof(tmp_manifest_file_path), "%sdata/%s", basedir, "backup_manifest.tmp");
                  snprintf(manifest_file_path, sizeof(manifest_file_path), "%sdata/%s", basedir, "backup_manifest");
               }
               else
               {
                  snprintf(tmp_manifest_file_path, sizeof(tmp_manifest_file_path), "%s/data/%s", basedir, "backup_manifest.tmp");
                  snprintf(manifest_file_path, sizeof(manifest_file_path), "%s/data/%s", basedir, "backup_manifest");
               }
               file = pgmoneta_open_file(tmp_manifest_file_path, "wb");
               break;
            }
            case 'd':
            {
               // real data
               if (msg->length <= 1)
               {
                  break;
               }

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

               if (pgmoneta_write_file(msg->data + 1, msg->length - 1, 1, file) != 1)
               {
                  pgmoneta_log_error("could not write to file %s", file_path);
                  goto error;
               }
               break;
            }
            case 'p':
            {
               // progress report, ignore this for now
               break;
            }
            default:
            {
               // should not happen, error
               pgmoneta_log_error("Invalid copy out data type");
               goto error;
            }
         }
      }
      pgmoneta_consume_copy_stream_end(buffer, msg);
   }

   if (file != NULL)
   {
      if (rename(tmp_manifest_file_path, manifest_file_path) != 0)
      {
         pgmoneta_log_error("could not rename file %s to %s", tmp_manifest_file_path, manifest_file_path);
         goto error;
      }
      fflush(file);
      fclose(file);
      file = NULL;
   }

   // update symlink
   tblspc = tablespaces;
   while (tblspc != NULL)
   {
      // update symlink
      memset(link_path, 0, sizeof(link_path));
      memset(directory, 0, sizeof(directory));
      if (pgmoneta_ends_with(basedir, "/"))
      {
         snprintf(link_path, sizeof(link_path), "%sdata/pg_tblspc/%d", basedir, tblspc->oid);
         snprintf(directory, sizeof(directory), "%s%s/", basedir, tblspc->name);
      }
      else
      {
         snprintf(link_path, sizeof(link_path), "%s/data/pg_tblspc/%d", basedir, tblspc->oid);
         snprintf(directory, sizeof(directory), "%s/%s/", basedir, tblspc->name);
      }
      unlink(link_path);
      pgmoneta_symlink_file(link_path, directory);
      tblspc = tblspc->next;
   }

   // verify checksum if available
   char dir[MAX_PATH];
   memset(dir, 0, sizeof(dir));
   if (pgmoneta_ends_with(basedir, "/"))
   {
      snprintf(dir, sizeof(dir), "%sdata", basedir);
   }
   else
   {
      snprintf(dir, sizeof(dir), "%s/data", basedir);
   }
   if (pgmoneta_manifest_checksum_verify(dir))
   {
      pgmoneta_log_error("Manifest verification failed");
      goto error;
   }

   pgmoneta_free_query_response(response);
   pgmoneta_free_copy_message(msg);
   return 0;

error:
   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   if (file != NULL)
   {
      fflush(file);
      fclose(file);
   }
   pgmoneta_free_query_response(response);
   pgmoneta_free_copy_message(msg);
   return 1;
}

int
pgmoneta_receive_manifest_file(SSL* ssl, int socket, struct stream_buffer* buffer, char* basedir, struct token_bucket* bucket, struct token_bucket* network_bucket)
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
   file = pgmoneta_open_file(tmp_file_path, "wb");

   if (file == NULL)
   {
      goto error;
   }

   // get the copy out response
   while (msg == NULL || msg->kind != 'H')
   {
      pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, NULL);
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
      pgmoneta_consume_copy_stream_start(ssl, socket, buffer, msg, network_bucket);
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
         if (pgmoneta_write_file(msg->data, msg->length, 1, file) != 1)
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
   pgmoneta_free_copy_message(msg);
   return 0;

error:
   fflush(file);
   fclose(file);
   pgmoneta_free_copy_message(msg);
   return 1;
}
