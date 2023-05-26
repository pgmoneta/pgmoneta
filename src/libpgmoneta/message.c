/*
 * Copyright (C) 2023 Red Hat
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
#include <memory.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <utils.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/time.h>

static int read_message(int socket, bool block, int timeout, struct message** msg);
static int write_message(int socket, struct message* msg);

static int ssl_read_message(SSL* ssl, int timeout, struct message** msg);
static int ssl_write_message(SSL* ssl, struct message* msg);

static int create_D_tuple(int number_of_columns, struct message* msg, struct tuple** tuple);
static int get_number_of_columns(struct message* msg);
static int get_column_name(struct message* msg, int index, char** name);

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
         snprintf(&cmd[0], sizeof(cmd), "START_REPLICATION SLOT %s PHYSICAL %s;", slot, xlogpos);
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
         snprintf(&cmd[0], sizeof(cmd), "START_REPLICATION PHYSICAL %s;", xlogpos);
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

   size = 1 + 8 + 8 + 8 + 8 + 1;

   m = (struct message*)malloc(sizeof(struct message));
   m->data = malloc(size);

   memset(m->data, 0, size);

   m->kind = 'r';
   m->length = size;

   pgmoneta_write_byte(m->data, 'r');
   pgmoneta_write_int64(m->data + 1, received);
   pgmoneta_write_int64(m->data + 9, flushed);
   pgmoneta_write_int64(m->data + 17, applied);
   pgmoneta_write_int64(m->data + 25, pgmoneta_get_current_timestamp() - pgmoneta_get_y2000_timestamp());
   pgmoneta_write_byte(m->data + 33, 0);

   *msg = m;

   return MESSAGE_STATUS_OK;
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
pgmoneta_query_execute(int socket, struct message* msg, struct query_response** response)
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

   status = pgmoneta_write_message(NULL, socket, msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   cont = true;
   while (cont)
   {
      status = pgmoneta_read_block_message(NULL, socket, &reply);

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
      pgmoneta_log_info("Query is NULL");
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
         int err = SSL_get_error(ssl, numbytes);

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
               pgmoneta_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
            case SSL_ERROR_SSL:
               pgmoneta_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
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
