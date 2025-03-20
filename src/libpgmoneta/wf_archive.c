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
#include <art.h>
#include <deque.h>
#include <info.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
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

static char* archive_name(void);
static int archive_execute(char*, struct art*);
static int archive_teardown(char*, struct art*);

struct workflow*
pgmoneta_create_archive(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &archive_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &archive_execute;
   wf->teardown = &archive_teardown;
   wf->next = NULL;

   return wf;
}

static char*
archive_name(void)
{
   return "Archive";
}

static int
archive_execute(char* name, struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* root = NULL;
   char* base = NULL;
   char* src = NULL;
   char* dst = NULL;
   char* d_name = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char* a = NULL;
      a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
      pgmoneta_log_debug("(Tree)\n%s", a);
      free(a);
   }
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
   assert(pgmoneta_art_contains_key(nodes, NODE_TARGET_ROOT));
   assert(pgmoneta_art_contains_key(nodes, NODE_TARGET_BASE));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   root = (char*)pgmoneta_art_search(nodes, NODE_TARGET_ROOT);
   base = (char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE);

   pgmoneta_log_debug("Archive (execute): %s/%s", config->servers[server].name, label);

   src = pgmoneta_append(src, base);

   dst = pgmoneta_append(dst, root);
   if (!pgmoneta_ends_with(dst, "/"))
   {
      dst = pgmoneta_append(dst, "/");
   }
   dst = pgmoneta_append(dst, "archive-");
   dst = pgmoneta_append(dst, config->servers[server].name);
   dst = pgmoneta_append(dst, "-");
   dst = pgmoneta_append(dst, label);
   dst = pgmoneta_append(dst, ".tar");

   d_name = pgmoneta_append(d_name, config->servers[server].name);
   d_name = pgmoneta_append(d_name, "-");
   d_name = pgmoneta_append(d_name, label);

   if (pgmoneta_exists(dst))
   {
      pgmoneta_delete_file(dst, NULL);
   }

   if (pgmoneta_tar_directory(src, dst, d_name))
   {
      goto error;
   }

   if (pgmoneta_art_insert(nodes, NODE_TARGET_FILE, (uintptr_t)dst, ValueString))
   {
      goto error;
   }

   free(src);
   free(dst);
   free(d_name);

   return 0;

error:

   free(src);
   free(dst);
   free(d_name);

   return 1;
}

static int
archive_teardown(char* name, struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   char* destination = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char* a = NULL;
      a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
      pgmoneta_log_debug("(Tree)\n%s", a);
      free(a);
   }
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
   assert(pgmoneta_art_contains_key(nodes, NODE_TARGET_BASE));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   pgmoneta_log_debug("Archive (teardown): %s/%s", config->servers[server].name, label);

   destination = (char*)pgmoneta_art_search(nodes, NODE_TARGET_BASE);

   if (pgmoneta_exists(destination))
   {
      pgmoneta_delete_directory(destination);
   }

   return 0;
}
