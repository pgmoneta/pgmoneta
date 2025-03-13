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
#include <art.h>
#include <info.h>
#include <logging.h>
#include <storage.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdlib.h>

static char* local_storage_name(void);
static int local_storage_execute(char* name, struct art*);

struct workflow*
pgmoneta_storage_create_local(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &local_storage_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &local_storage_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
local_storage_name(void)
{
   return "Local";
}

static int
local_storage_execute(char* name, struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   int hours;
   int minutes;
   double seconds;
   char elapsed[128];
   struct configuration* config;

   config = (struct configuration*)shmem;

#ifdef DEBUG
   if (pgmoneta_log_is_enabled(PGMONETA_LOGGING_LEVEL_DEBUG1))
   {
      char *a = NULL;
      a = pgmoneta_art_to_string(nodes, FORMAT_TEXT, NULL, 0);
      pgmoneta_log_debug("(Tree)\n%s", a);
      free(a);
   }
   assert(nodes != NULL);
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);

   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
   total_seconds = pgmoneta_compute_duration(start_t, end_t);
   hours = (int)total_seconds / 3600;
   minutes = ((int)total_seconds % 3600) / 60;
   seconds = (int)total_seconds % 60 + (total_seconds - ((long)total_seconds));

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

   pgmoneta_log_debug("Local storage engine (execute): %s/%s (Elapsed: %s)", config->servers[server].name, label, &elapsed[0]);

   return 0;
}
