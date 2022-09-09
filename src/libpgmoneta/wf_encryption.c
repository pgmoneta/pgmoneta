/*
 * Copyright (C) 2022 Red Hat
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
#include <aes.h>
#include <link.h>
#include <logging.h>
#include <node.h>
#include <pgmoneta.h>
#include <utils.h>
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
   return 0;
}

static int
encryption_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* d = NULL;
   char* enc_file = NULL;
   char* to = NULL;
   char* prefix = NULL;
   char* directory = NULL;
   char* id = NULL;
   char* compress_suffix = NULL;
   char* tarfile = NULL;
   time_t encrypt_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char elapsed[128];
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (i_nodes != NULL)
   {
      prefix = pgmoneta_get_node_string(i_nodes, "prefix");

      if (!strcmp(prefix, "Restore"))
      {
         to = pgmoneta_get_node_string(*o_nodes, "to");
         d = pgmoneta_append(d, to);
      }
      else if (!strcmp(prefix, "Archive"))
      {
         directory = pgmoneta_get_node_string(i_nodes, "directory");
         id = pgmoneta_get_node_string(i_nodes, "id");

         d = pgmoneta_append(d, directory);
         d = pgmoneta_append(d, "/");
         d = pgmoneta_append(d, config->servers[server].name);
         d = pgmoneta_append(d, "-");
         d = pgmoneta_append(d, id);
         switch (config->compression_type)
         {
            case COMPRESSION_GZIP:
               compress_suffix = ".gz";
               break;
            case COMPRESSION_ZSTD:
               compress_suffix = ".zstd";
               break;
            case COMPRESSION_LZ4:
               compress_suffix = ".lz4";
               break;
            case COMPRESSION_NONE:
               compress_suffix = "";
               break;
            default:
               pgmoneta_log_error("encryption_execute: Unknown compression type");
               break;
         }
         d = pgmoneta_append(d, ".tar");
         d = pgmoneta_append(d, compress_suffix);
         d = pgmoneta_append(d, ".aes");
         if (pgmoneta_exists(d))
         {
            pgmoneta_delete_file(d);
         }
      }
      else
      {
         d = pgmoneta_get_server_backup_identifier_data(server, identifier);
      }
   }
   else
   {
      d = pgmoneta_get_server_backup_identifier_data(server, identifier);
   }

   encrypt_time = time(NULL);

   if (i_nodes != NULL)
   {
      if (!strcmp(prefix, "Archive"))
      {
         tarfile = pgmoneta_get_node_string(*o_nodes, "tarfile");
         enc_file = pgmoneta_append(enc_file, tarfile);
         enc_file = pgmoneta_append(enc_file, compress_suffix);
         pgmoneta_encrypt_file(enc_file, d);
      }
      else
      {
         pgmoneta_encrypt_data(d);
      }
   }
   else
   {
      pgmoneta_encrypt_data(d);
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
   char* prefix = NULL;
   time_t decrypt_time;
   int total_seconds;
   int hours;
   int minutes;
   int seconds;
   char elapsed[128];
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (i_nodes != NULL)
   {
      prefix = pgmoneta_get_node_string(i_nodes, "prefix");

      if (!strcmp(prefix, "Restore"))
      {
         to = pgmoneta_get_node_string(*o_nodes, "to");
         d = malloc(strlen(to) + 1);
         memset(d, 0, strlen(to) + 1);
         memcpy(d, to, strlen(to));
      }
      else
      {
         d = pgmoneta_get_server_backup_identifier_data(server, identifier);
      }
   }
   else
   {
      d = pgmoneta_get_server_backup_identifier_data(server, identifier);
   }

   decrypt_time = time(NULL);

   pgmoneta_decrypt_data(d);

   total_seconds = (int)difftime(time(NULL), decrypt_time);
   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   seconds = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

   pgmoneta_log_debug("Decryption: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);

   free(d);

   return 0;
}

static int
encryption_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}
