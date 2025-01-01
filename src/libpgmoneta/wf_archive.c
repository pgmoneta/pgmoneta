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
#include <pgmoneta.h>
#include <achv.h>
#include <deque.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
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

static int archive_setup(int, char*, struct deque*);
static int archive_execute(int, char*, struct deque*);
static int archive_teardown(int, char*, struct deque*);

struct workflow*
pgmoneta_create_archive(void)
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
archive_setup(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Archive (setup): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}

static int
archive_execute(int server, char* identifier, struct deque* nodes)
{
   char* tarfile = NULL;
   char* save_path = NULL;
   char* label = NULL;
   char* directory = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Archive (execute): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   label = (char*)pgmoneta_deque_get(nodes, NODE_LABEL);
   directory = (char*)pgmoneta_deque_get(nodes, NODE_DIRECTORY);

   tarfile = pgmoneta_append(tarfile, directory);
   tarfile = pgmoneta_append(tarfile, "/archive-");
   tarfile = pgmoneta_append(tarfile, config->servers[server].name);
   tarfile = pgmoneta_append(tarfile, "-");
   tarfile = pgmoneta_append(tarfile, label);
   tarfile = pgmoneta_append(tarfile, ".tar");

   save_path = pgmoneta_append(save_path, "./archive-");
   save_path = pgmoneta_append(save_path, config->servers[server].name);
   save_path = pgmoneta_append(save_path, "-");
   save_path = pgmoneta_append(save_path, label);

   if (pgmoneta_tar_directory(directory, tarfile, save_path))
   {
      goto error;
   }

   if (pgmoneta_deque_add(nodes, NODE_TARFILE, (uintptr_t)tarfile, ValueString))
   {
      goto error;
   }

   free(tarfile);
   free(save_path);

   return 0;

error:

   free(tarfile);
   free(save_path);

   return 1;
}

static int
archive_teardown(int server, char* identifier, struct deque* nodes)
{
   char* output = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Archive (teardown): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   output = (char*)pgmoneta_deque_get(nodes, NODE_OUTPUT);

   if (output == NULL)
   {
      goto error;
   }

   pgmoneta_delete_directory(output);

   return 0;

error:

   return 1;
}
