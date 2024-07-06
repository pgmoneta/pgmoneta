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
#include <node.h>
#include <info.h>
#include <management.h>
#include <network.h>
#include <logging.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/types.h>

static int archive_setup(int, char*, struct node*, struct node**);
static int archive_execute(int, char*, struct node*, struct node**);
static int archive_teardown(int, char*, struct node*, struct node**);

struct workflow*
pgmoneta_workflow_create_archive(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &archive_setup;
   wf->execute = &archive_execute;
   wf->teardown = &archive_teardown;
   wf->next = NULL;

   return wf;
}

static int
archive_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* tarfile = NULL;
   char* destination = NULL;
   char* id = NULL;
   struct node* o_tarfile = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Archive (setup): %s/%s", config->servers[server].name, identifier);
   pgmoneta_list_nodes(i_nodes);
   pgmoneta_list_nodes(*o_nodes);

   destination = pgmoneta_get_node_string(i_nodes, "destination");
   id = pgmoneta_get_node_string(i_nodes, "id");

   tarfile = pgmoneta_append(tarfile, destination);
   tarfile = pgmoneta_append(tarfile, "/archive-");
   tarfile = pgmoneta_append(tarfile, config->servers[server].name);
   tarfile = pgmoneta_append(tarfile, "-");
   tarfile = pgmoneta_append(tarfile, id);
   tarfile = pgmoneta_append(tarfile, ".tar");

   if (pgmoneta_create_node_string(tarfile, "tarfile", &o_tarfile))
   {
      goto error;
   }

   pgmoneta_append_node(o_nodes, o_tarfile);

   free(tarfile);

   return 0;

error:

   free(tarfile);

   return 1;
}

static int
archive_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* output = NULL;
   char* tarfile = NULL;
   char* save_path = NULL;
   char* id = NULL;
   char* destination = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Archive (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_list_nodes(i_nodes);
   pgmoneta_list_nodes(*o_nodes);

   output = pgmoneta_get_node_string(i_nodes, "output");

   if (output == NULL)
   {
      goto error;
   }

   id = pgmoneta_get_node_string(i_nodes, "id");
   destination = pgmoneta_get_node_string(i_nodes, "destination");

   tarfile = pgmoneta_append(tarfile, destination);
   tarfile = pgmoneta_append(tarfile, "/archive-");
   tarfile = pgmoneta_append(tarfile, config->servers[server].name);
   tarfile = pgmoneta_append(tarfile, "-");
   tarfile = pgmoneta_append(tarfile, id);
   tarfile = pgmoneta_append(tarfile, ".tar");

   save_path = pgmoneta_append(save_path, "./archive-");
   save_path = pgmoneta_append(save_path, config->servers[server].name);
   save_path = pgmoneta_append(save_path, "-");
   save_path = pgmoneta_append(save_path, id);

   if (pgmoneta_tar_directory(output, tarfile, save_path))
   {
      goto error;
   }
   free(tarfile);

   return 0;

error:

   free(tarfile);
   return 1;
}

static int
archive_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* output = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Archive (teardown): %s/%s", config->servers[server].name, identifier);
   pgmoneta_list_nodes(i_nodes);
   pgmoneta_list_nodes(*o_nodes);

   output = pgmoneta_get_node_string(i_nodes, "output");

   if (output == NULL)
   {
      goto error;
   }

   pgmoneta_delete_directory(output);

   return 0;

error:

   return 1;
}
