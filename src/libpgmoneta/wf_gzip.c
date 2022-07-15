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
#include <node.h>
#include <pgmoneta.h>
#include <gzip_compression.h>
#include <logging.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdlib.h>
#include <string.h>

static int gzip_setup(int, char*, struct node*, struct node**);
static int gzip_execute_compress(int, char*, struct node*, struct node**);
static int gzip_execute_uncompress(int, char*, struct node*, struct node**);
static int gzip_teardown(int, char*, struct node*, struct node**);

struct workflow*
pgmoneta_workflow_create_gzip(bool compress)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &gzip_setup;

   if (compress == true)
   {
      wf->execute = &gzip_execute_compress;
   }
   else
   {
      wf->execute = &gzip_execute_uncompress;
   }

   wf->teardown = &gzip_teardown;
   wf->next = NULL;

   return wf;
}

static int
gzip_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static int
gzip_execute_compress(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* d = NULL;
   char* to = NULL;
   char* prefix = NULL;
   char* directory = NULL;
   char* id = NULL;
   char* tarfile = NULL;
   time_t compression_time;
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
      else if (!strcmp(prefix, "Archive"))
      {
         directory = pgmoneta_get_node_string(i_nodes, "directory");
         id = pgmoneta_get_node_string(i_nodes, "id");

         d = pgmoneta_append(d, directory);
         d = pgmoneta_append(d, "/");
         d = pgmoneta_append(d, config->servers[server].name);
         d = pgmoneta_append(d, "-");
         d = pgmoneta_append(d, id);
         d = pgmoneta_append(d, ".tar.gz");

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

   compression_time = time(NULL);

   if (i_nodes != NULL)
   {
      if (!strcmp(prefix, "Archive"))
      {
         tarfile = pgmoneta_get_node_string(*o_nodes, "tarfile");

         pgmoneta_gzip_file(tarfile, d);
      }
      else if (!strcmp(prefix, "Restore"))
      {
         pgmoneta_gzip_data(d);
      }
   }
   else
   {
      pgmoneta_gzip_data(d);
   }

   total_seconds = (int)difftime(time(NULL), compression_time);
   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   seconds = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

   pgmoneta_log_debug("Compression: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);

   free(d);

   return 0;
}

static int
gzip_execute_uncompress(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* d = NULL;
   char* to = NULL;
   char* prefix = NULL;
   time_t decompress_time;
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

   decompress_time = time(NULL);

   pgmoneta_gunzip_data(d);

   total_seconds = (int)difftime(time(NULL), decompress_time);
   hours = total_seconds / 3600;
   minutes = (total_seconds % 3600) / 60;
   seconds = total_seconds % 60;

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%02i", hours, minutes, seconds);

   pgmoneta_log_debug("Decompress: %s/%s (Elapsed: %s)", config->servers[server].name, identifier, &elapsed[0]);

   free(d);

   return 0;
}

static int
gzip_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}
