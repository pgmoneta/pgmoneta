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
#include <aes.h>
#include <info.h>
#include <link.h>
#include <logging.h>
#include <node.h>
#include <pgmoneta.h>
#include <utils.h>
#include <workers.h>
#include <workflow.h>

/* system */
#include <stdlib.h>

static int encryption_setup(int, char*, struct node*, struct node**);
static int encryption_execute(int, char*, struct node*, struct node**);
static int decryption_execute(int, char*, struct node*, struct node**);
static int encryption_teardown(int, char*, struct node*, struct node**);

struct workflow*
pgmoneta_workflow_encryption(bool encrypt)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &encryption_setup;

   if (encrypt)
   {
      wf->execute = &encryption_execute;
   }
   else
   {
      wf->execute = &decryption_execute;
   }

   wf->teardown = &encryption_teardown;
   wf->next = NULL;

   return wf;
}

static int
encryption_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Encryption (setup): %s/%s", config->servers[server].name, identifier);
   pgmoneta_list_nodes(i_nodes);
   pgmoneta_list_nodes(*o_nodes);

   return 0;
}

static int
encryption_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* d = NULL;
   char* enc_file = NULL;
   char* root = NULL;
   char* to = NULL;
   char* compress_suffix = NULL;
   char* tarfile = NULL;
   time_t encrypt_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char elapsed[128];
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Encryption (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_list_nodes(i_nodes);
   pgmoneta_list_nodes(*o_nodes);

   tarfile = pgmoneta_get_node_string(*o_nodes, "tarfile");

   encrypt_time = time(NULL);

   if (tarfile == NULL)
   {
      number_of_workers = pgmoneta_get_number_of_workers(server);
      if (number_of_workers > 0)
      {
         pgmoneta_workers_initialize(number_of_workers, &workers);
      }

      root = pgmoneta_get_node_string(*o_nodes, "root");
      to = pgmoneta_get_node_string(*o_nodes, "to");
      d = pgmoneta_append(d, to);

      pgmoneta_encrypt_data(d, workers);
      pgmoneta_encrypt_tablespaces(root, workers);

      if (number_of_workers > 0)
      {
         pgmoneta_workers_wait(workers);
         pgmoneta_workers_destroy(workers);
      }
   }
   else
   {
      switch (config->compression_type)
      {
         case COMPRESSION_CLIENT_GZIP:
         case COMPRESSION_SERVER_GZIP:
            compress_suffix = ".gz";
            break;
         case COMPRESSION_CLIENT_ZSTD:
         case COMPRESSION_SERVER_ZSTD:
            compress_suffix = ".zstd";
            break;
         case COMPRESSION_CLIENT_LZ4:
         case COMPRESSION_SERVER_LZ4:
            compress_suffix = ".lz4";
            break;
         case COMPRESSION_CLIENT_BZIP2:
            compress_suffix = ".bz2";
            break;
         case COMPRESSION_NONE:
            compress_suffix = "";
            break;
         default:
            pgmoneta_log_error("encryption_execute: Unknown compression type");
            break;
      }

      d = pgmoneta_append(d, tarfile);
      d = pgmoneta_append(d, compress_suffix);
      d = pgmoneta_append(d, ".aes");
      if (pgmoneta_exists(d))
      {
         pgmoneta_delete_file(d, NULL);
      }

      enc_file = pgmoneta_append(enc_file, tarfile);
      enc_file = pgmoneta_append(enc_file, compress_suffix);
      pgmoneta_encrypt_file(enc_file, d);
   }

   total_seconds = (int)difftime(time(NULL), encrypt_time);
   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   seconds = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

   pgmoneta_log_debug("Encryption: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);

   free(d);
   free(enc_file);

   return 0;
}

static int
decryption_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* d = NULL;
   char* to = NULL;
   char* id = NULL;
   time_t decrypt_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char elapsed[128];
   int number_of_backups = 0;
   int number_of_workers = 0;
   struct workers* workers = NULL;
   struct backup** backups = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Decryption (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_list_nodes(i_nodes);
   pgmoneta_list_nodes(*o_nodes);

   if (!strcmp(identifier, "oldest"))
   {
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

      for (int i = 0; id == NULL && i < number_of_backups; i++)
      {
         if (backups[i]->valid == VALID_TRUE)
         {
            id = backups[i]->label;
         }
      }

      free(d);
      d = NULL;
   }
   else if (!strcmp(identifier, "latest") || !strcmp(identifier, "newest"))
   {
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         goto error;
      }

      for (int i = number_of_backups - 1; id == NULL && i >= 0; i--)
      {
         if (backups[i]->valid == VALID_TRUE)
         {
            id = backups[i]->label;
         }
      }

      free(d);
      d = NULL;
   }
   else
   {
      id = identifier;
   }

   to = pgmoneta_get_node_string(*o_nodes, "to");

   if (to != NULL)
   {
      d = pgmoneta_append(d, to);
   }
   else
   {
      d = pgmoneta_get_server_backup_identifier_data(server, id);
   }

   decrypt_time = time(NULL);

   number_of_workers = pgmoneta_get_number_of_workers(server);
   if (number_of_workers > 0)
   {
      pgmoneta_workers_initialize(number_of_workers, &workers);
   }

   pgmoneta_decrypt_directory(d, workers);

   if (number_of_workers > 0)
   {
      pgmoneta_workers_wait(workers);
      pgmoneta_workers_destroy(workers);
   }

   total_seconds = (int)difftime(time(NULL), decrypt_time);
   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   seconds = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

   pgmoneta_log_debug("Decryption: %s/%s (Elapsed: %s)", config->servers[server].name, id, &elapsed[0]);

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);

   return 0;

error:
   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);

   return 1;
}

static int
encryption_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Encryption (teardown): %s/%s", config->servers[server].name, identifier);
   pgmoneta_list_nodes(i_nodes);
   pgmoneta_list_nodes(*o_nodes);

   return 0;
}
