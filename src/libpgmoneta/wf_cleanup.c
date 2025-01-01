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
#include <info.h>
#include <logging.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdlib.h>

static int cleanup_setup(int, char*, struct deque*);
static int cleanup_execute_restore(int, char*, struct deque*);
static int cleanup_teardown(int, char*, struct deque*);

struct workflow*
pgmoneta_create_cleanup(int type)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &cleanup_setup;
   switch (type)
   {
      case CLEANUP_TYPE_RESTORE:
         wf->execute = &cleanup_execute_restore;
         break;
      default:
         pgmoneta_log_error("Invalid cleanup type");
   }
   wf->teardown = &cleanup_teardown;
   wf->next = NULL;

   return wf;
}

static int
cleanup_setup(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Cleanup (setup): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}

static int
cleanup_execute_restore(int server, char* identifier, struct deque* nodes)
{
   char* label = NULL;
   char* path = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_trace("Cleanup (execute): %s/%s", config->servers[server].name, identifier);

   label = (char*)pgmoneta_deque_get(nodes, NODE_LABEL);

   pgmoneta_log_debug("Cleanup (execute): %s/%s", config->servers[server].name, label);
   pgmoneta_deque_list(nodes);

   path = pgmoneta_append(path, (char*)pgmoneta_deque_get(nodes, NODE_DIRECTORY));
   if (!pgmoneta_ends_with(path, "/"))
   {
      path = pgmoneta_append(path, "/");
   }
   path = pgmoneta_append(path, config->servers[server].name);
   path = pgmoneta_append(path, "-");
   path = pgmoneta_append(path, label);
   path = pgmoneta_append(path, "/backup_label.old");

   if (pgmoneta_exists(path))
   {
      pgmoneta_delete_file(path, true, NULL);
   }

   free(path);

   return 0;
}

static int
cleanup_teardown(int server, char* identifier, struct deque* nodes)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("Cleanup (teardown): %s/%s", config->servers[server].name, identifier);
   pgmoneta_deque_list(nodes);

   return 0;
}
