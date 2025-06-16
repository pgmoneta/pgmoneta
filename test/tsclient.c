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
 *
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <brt.h>
#include <configuration.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <value.h>
#include <walfile/wal_summary.h>
#include <tsclient.h>

/* system */
#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

char project_directory[BUFFER_SIZE];

static int check_output_outcome(int socket);
static int get_connection();
static char* get_configuration_path(char* format_string, ...);
static char* get_users_path(char* format_string, ...);
static char* get_restore_path(char* format_string, ...);
static char* get_pgbench_log_path(char* format_string, ...);
static char* get_backup_summary_path(char* format_string, ...);
static char* get_pg_wal_dir_path(char* format_string, ...);

static int do_checkpoint_and_get_lsn(int server, xlog_rec_ptr* checkpoint_lsn);

int
pgmoneta_tsclient_init(char* base_dir)
{
   struct main_configuration* config = NULL;
   int ret;
   size_t size;
   char* configuration_path = NULL;
   char* users_path = NULL;

   memset(project_directory, 0, sizeof(project_directory));
   memcpy(project_directory, base_dir, strlen(base_dir));

   configuration_path = get_configuration_path(PGMONETA_CONFIGURATION_TRAIL);
   users_path = get_users_path(PGMONETA_USERS_TRAIL);

   // Create the shared memory for the configuration
   size = sizeof(struct main_configuration);
   if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      goto error;
   }
   pgmoneta_init_main_configuration(shmem);
   /* Try reading configuration from the configuration path */

   if (configuration_path != NULL)
   {
      ret = pgmoneta_read_main_configuration(shmem, configuration_path);
      if (ret)
      {
         goto error;
      }

      config = (struct main_configuration*)shmem;
   }
   else
   {
      goto error;
   }

   /* Initiate logging */
   if (pgmoneta_init_logging())
   {
      goto error;
   }

   if (pgmoneta_start_logging())
   {
      goto error;
   }

   /* Try reading user configuration from the user path */
   if (users_path != NULL)
   {
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret)
      {
         goto error;
      }
   }
   else
   {
      goto error;
   }

   free(configuration_path);
   free(users_path);
   return 0;
error:
   pgmoneta_stop_logging();
   free(configuration_path);
   free(users_path);
   return 1;
}

int
pgmoneta_tsclient_destroy()
{
   size_t size;

   size = sizeof(struct main_configuration);
   pgmoneta_stop_logging();
   return pgmoneta_destroy_shared_memory(shmem, size);
}

int
pgmoneta_tsclient_execute_backup(char* server, char* incremental)
{
   int socket = -1;

   socket = get_connection();
   // Security Checks
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }
   // Create a backup request to the main server
   if (pgmoneta_management_request_backup(NULL, socket, server, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, incremental, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   // Check the outcome field of the output, if true success, else failure
   if (check_output_outcome(socket))
   {
      goto error;
   }

   pgmoneta_disconnect(socket);
   return 0;
error:
   pgmoneta_disconnect(socket);
   return 1;
}

int
pgmoneta_tsclient_execute_restore(char* server, char* backup_id, char* position)
{
   char* restore_path = NULL;
   int socket = -1;

   socket = get_connection();
   // Security Checks
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      goto error;
   }

   // Fallbacks
   if (backup_id == NULL)
   {
      backup_id = "newest";
   }

   restore_path = get_restore_path(PGMONETA_RESTORE_TRAIL);
   // Create a restore request to the main server
   if (pgmoneta_management_request_restore(NULL, socket, server, backup_id, position, restore_path, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   // Check the outcome field of the output, if true success, else failure
   if (check_output_outcome(socket))
   {
      goto error;
   }

   free(restore_path);
   pgmoneta_disconnect(socket);
   return 0;
error:
   free(restore_path);
   pgmoneta_disconnect(socket);
   return 1;
}

int
pgmoneta_tsclient_execute_delete(char* server, char* backup_id)
{
   int socket = -1;

   socket = get_connection();
   // Security Checks
   if (!pgmoneta_socket_isvalid(socket) || server == NULL)
   {
      return 1;
   }

   // Fallbacks
   if (!backup_id)
   {
      backup_id = "oldest";
   }

   // Create a delete request to the main server
   if (pgmoneta_management_request_delete(NULL, socket, server, backup_id, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
   {
      goto error;
   }

   // Check the outcome field of the output, if true success, else failure
   if (check_output_outcome(socket))
   {
      goto error;
   }

   pgmoneta_disconnect(socket);
   return 0;
error:
   pgmoneta_disconnect(socket);
   return 1;
}

int
pgmoneta_tsclient_execute_query(int srv, char* query, bool is_dummy, struct query_response** qr)
{
   int socket = -1;
   struct main_configuration* config = NULL;
   struct message* msg = NULL;
   struct query_response* response = NULL;
   int usr = -1;
   SSL* ssl = NULL;

   config = (struct main_configuration*)shmem;

   pgmoneta_memory_init();

   /* find the corresponding user's index of the given server */
   for (int i = 0; i < config->common.number_of_users; i++)
   {
      if (!strcmp(config->common.servers[srv].username, config->common.users[i].username))
      {
         usr = i;
      }
   }
   if (usr == -1)
   {
      goto error;
   }

   if (is_dummy)
   {
      /* establish a connection, with dummy user pass and replication flag not set */
      if (pgmoneta_server_authenticate(srv, "mydb", "myuser", "password", false, &ssl, &socket) != AUTH_SUCCESS)
      {
         goto error;
      }
   }
   else
   {
      /* establish a connection, with replication flag not set */
      if (pgmoneta_server_authenticate(srv, "postgres", config->common.users[usr].username, config->common.users[usr].password, false, &ssl, &socket) != AUTH_SUCCESS)
      {
         goto error;
      }
   }

   /* create and execute the query */
   pgmoneta_create_query_message(query, &msg);
   if (pgmoneta_query_execute(NULL, socket, msg, &response) || response == NULL)
   {
      goto error;
   }

   *qr = response;

   pgmoneta_memory_destroy();
   pgmoneta_free_message(msg);
   pgmoneta_disconnect(socket);
   return 0;
error:
   pgmoneta_memory_destroy();
   pgmoneta_free_message(msg);
   pgmoneta_free_query_response(response);
   pgmoneta_disconnect(socket);
   return 1;
}

int
pgmoneta_tsclient_brt_init(block_ref_table** brt)
{
   block_ref_table* b = NULL;

   if ((b = (block_ref_table*)malloc(sizeof(block_ref_table))) == NULL)
   {
      goto error;
   }

   *brt = b;
   return 0;
error:
   pgmoneta_brt_destroy(b);
   return 1;
}

void
pgmoneta_tsclient_brt_destroy(block_ref_table* brt)
{
   pgmoneta_brt_destroy(brt);
}

void
pgmoneta_tsclient_relation_fork_init(int spcoid, int dboid, int relnum, enum fork_number forknum, struct rel_file_locator* r, enum fork_number* frk)
{
   struct rel_file_locator rlocator;
   rlocator.spcOid = spcoid; rlocator.dbOid = dboid; rlocator.relNumber = relnum;

   *r = rlocator;
   *frk = forknum;
}

int
pgmoneta_tsclient_execute_consecutive_mark_block_modified(block_ref_table* brt, struct rel_file_locator* rlocator, enum fork_number frk, block_number blkno, int n)
{
   for (int i = 0; i < n; i++)
   {
      if (pgmoneta_brt_mark_block_modified(brt, rlocator, frk, blkno + i))
      {
         return 1;
      }
   }
   return 0;
}

int
pgmoneta_tsclient_write(block_ref_table* brt)
{
   char* r = NULL;
   r = get_backup_summary_path(PGMONETA_BACKUP_SUMMARY_TRAIL);

   r = pgmoneta_append(r, "tmp.summary");
   if (pgmoneta_brt_write(brt, r))
   {
      goto error;
   }

   free(r);
   return 0;
error:
   if (r != NULL)
   {
      free(r);
   }
   return 1;
}

int
pgmoneta_tsclient_read(block_ref_table** brt)
{
   char* r = NULL;

   r = get_backup_summary_path(PGMONETA_BACKUP_SUMMARY_TRAIL);
   r = pgmoneta_append(r, "tmp.summary");

   if (pgmoneta_brt_read(r, brt))
   {
      goto error;
   }
   free(r);
   return 0;
error:
   if (r != NULL)
   {
      free(r);
   }
   return 1;
}

int
pgmoneta_tsclient_summarize_wal(char* server)
{
   char* wal_dir = NULL;
   char* summary_dir = NULL;
   struct main_configuration* config = NULL;
   int srv = -1;
   xlog_rec_ptr s_lsn;
   xlog_rec_ptr e_lsn;
   struct query_response* qr = NULL;

   config = (struct main_configuration*)shmem;
   /* get the server index */
   for (int i = 0; i < config->common.number_of_servers; i++)
   {
      if (!strcmp(config->common.servers[i].name, server))
      {
         srv = i;
         break;
      }
   }

   /* get the starting lsn for summary */
   if (do_checkpoint_and_get_lsn(srv, &s_lsn))
   {
      goto error;
   }

   /* Create a table  */
   if (pgmoneta_tsclient_execute_query(srv, "CREATE TABLE t1 (id int);", 1, &qr))
   {
      goto error;
   }
   pgmoneta_free_query_response(qr);
   qr = NULL;

   /* Insert some tuples */
   if (pgmoneta_tsclient_execute_query(srv, "INSERT INTO t1 SELECT  GENERATE_SERIES(1, 800);", 1, &qr))
   {
      goto error;
   }
   pgmoneta_free_query_response(qr);
   qr = NULL;

   /* get the ending lsn for summary */
   if (do_checkpoint_and_get_lsn(srv, &e_lsn))
   {
      goto error;
   }

   /* Switch the wal segment so that records won't appear in partial segments */
   if (pgmoneta_tsclient_execute_query(srv, "SELECT pg_switch_wal();", 0, &qr))
   {
      goto error;
   }
   pgmoneta_free_query_response(qr);
   qr = NULL;

   /* Append some more tuples just to ensure that a new wal segment is streamed */
   if (do_checkpoint_and_get_lsn(srv, NULL))
   {
      goto error;
   }
   pgmoneta_free_query_response(qr);
   qr = NULL;

   sleep(5);

   wal_dir = get_pg_wal_dir_path(PGMONETA_PG_WAL_DIR_TRAIL, server);
   /* Create summary directory in the base_dir of a server if not already present */
   summary_dir = pgmoneta_get_server_summary(srv);
   if (pgmoneta_mkdir(summary_dir))
   {
      goto error;
   }

   if (pgmoneta_summarize_wal(srv, wal_dir, s_lsn, e_lsn))
   {
      goto error;
   }

   free(summary_dir);
   free(wal_dir);
   return 0;

error:
   pgmoneta_free_query_response(qr);
   free(summary_dir);
   free(wal_dir);
   return 1;
}

static int
check_output_outcome(int socket)
{
   struct json* read = NULL;
   struct json* outcome = NULL;

   if (pgmoneta_management_read_json(NULL, socket, NULL, NULL, &read))
   {
      goto error;
   }

   if (!pgmoneta_json_contains_key(read, MANAGEMENT_CATEGORY_OUTCOME))
   {
      goto error;
   }

   outcome = (struct json*)pgmoneta_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
   if (!pgmoneta_json_contains_key(outcome, MANAGEMENT_ARGUMENT_STATUS) && !(bool)pgmoneta_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
   {
      goto error;
   }

   pgmoneta_json_destroy(read);
   return 0;
error:
   pgmoneta_json_destroy(read);
   return 1;
}

static int
get_connection()
{
   int socket = -1;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;
   if (!strlen(config->common.configuration_path))
   {
      if (pgmoneta_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
      {
         return -1;
      }
   }
   return socket;
}

static char*
get_restore_path(char* format_string, ...)
{
   va_list args;
   char* restore_path = NULL;
   char buffer[BUFFER_SIZE];

   va_start(args, format_string);
   memset(buffer, 0, sizeof(buffer));
   snprintf(buffer, sizeof(buffer), format_string, args);
   va_end(args);

   restore_path = pgmoneta_append(restore_path, project_directory);
   restore_path = pgmoneta_append(restore_path, buffer);

   return restore_path;
}

static char*
get_backup_summary_path(char* format_string, ...)
{
   va_list args;
   char* backup_summary_path = NULL;
   char buffer[BUFFER_SIZE];

   va_start(args, format_string);
   memset(buffer, 0, sizeof(buffer));
   snprintf(buffer, sizeof(buffer), format_string, args);
   va_end(args);

   backup_summary_path = pgmoneta_append(backup_summary_path, project_directory);
   backup_summary_path = pgmoneta_append(backup_summary_path, buffer);

   return backup_summary_path;
}

static char*
get_configuration_path(char* format_string, ...)
{
   va_list args;
   char* configuration_path = NULL;
   char buffer[BUFFER_SIZE];

   va_start(args, format_string);
   memset(buffer, 0, sizeof(buffer));
   snprintf(buffer, sizeof(buffer), format_string, args);
   va_end(args);

   configuration_path = pgmoneta_append(configuration_path, project_directory);
   configuration_path = pgmoneta_append(configuration_path, buffer);

   return configuration_path;
}

static char*
get_users_path(char* format_string, ...)
{
   va_list args;
   char* users_path = NULL;
   char buffer[BUFFER_SIZE];

   va_start(args, format_string);
   memset(buffer, 0, sizeof(buffer));
   snprintf(buffer, sizeof(buffer), format_string, args);
   va_end(args);

   users_path = pgmoneta_append(users_path, project_directory);
   users_path = pgmoneta_append(users_path, buffer);

   return users_path;
}

static char*
get_pgbench_log_path(char* format_string, ...)
{
   va_list args;
   char* pgbench_log_path = NULL;
   char buffer[BUFFER_SIZE];

   va_start(args, format_string);
   memset(buffer, 0, sizeof(buffer));
   snprintf(buffer, sizeof(buffer), format_string, args);
   va_end(args);

   pgbench_log_path = pgmoneta_append(pgbench_log_path, project_directory);
   pgbench_log_path = pgmoneta_append(pgbench_log_path, buffer);

   return pgbench_log_path;
}

static char*
get_pg_wal_dir_path(char* format_string, ...)
{
   va_list args;
   char* pg_waldir_path = NULL;
   char buffer[BUFFER_SIZE];

   va_start(args, format_string);
   memset(buffer, 0, sizeof(buffer));
   vsnprintf(buffer, sizeof(buffer), format_string, args);
   va_end(args);

   pg_waldir_path = pgmoneta_append(pg_waldir_path, project_directory);
   pg_waldir_path = pgmoneta_append(pg_waldir_path, buffer);

   return pg_waldir_path;
}

static int
do_checkpoint_and_get_lsn(int srv, xlog_rec_ptr* checkpoint_lsn)
{
   // xlog_rec_ptr c_lsn = 0;
   char* chkpt_lsn = NULL;
   uint32_t chkpt_hi = 0;
   uint32_t chkpt_lo = 0;
   struct query_response* qr = NULL;

   /* Assuming the user associated with this server has 'pg_checkpoint' role */
   if (pgmoneta_tsclient_execute_query(srv, "CHECKPOINT;", 0, &qr))
   {
      goto error;
   }
   // Ignore this result
   pgmoneta_free_query_response(qr);
   qr = NULL;

   if (pgmoneta_tsclient_execute_query(srv, "SELECT checkpoint_lsn FROM pg_control_checkpoint();", 0, &qr))
   {
      goto error;
   }

   /* Extract the checkpoint lsn */
   if (qr == NULL || qr->number_of_columns < 1)
   {
      goto error;
   }

   chkpt_lsn = pgmoneta_query_response_get_data(qr, 0);

   if (chkpt_lsn != NULL)
   {
      sscanf(chkpt_lsn, "%X/%X", &chkpt_hi, &chkpt_lo);
   }

   if (checkpoint_lsn)
   {
      *checkpoint_lsn = ((uint64_t)chkpt_hi << 32) + (uint64_t)chkpt_lo;
   }

   pgmoneta_free_query_response(qr);
   return 0;
error:
   pgmoneta_free_query_response(qr);
   return 1;
}
