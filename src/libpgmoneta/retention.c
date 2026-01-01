/*
 * Copyright (C) 2026 The pgmoneta community
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
#include <management.h>
#include <utils.h>
#include <workflow.h>

#define NAME "retention"

void
pgmoneta_retention(char** argv)
{
   int server = 0;
   int ec = -1;
   char* en = NULL;
   struct workflow* workflow = NULL;
   struct art* nodes = NULL;
   struct main_configuration* config;

   pgmoneta_start_logging();

   config = (struct main_configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "retention", NULL);

   for (server = 0; server < config->common.number_of_servers; server++)
   {
      bool active = false;

      if (!config->common.servers[server].online)
      {
         pgmoneta_log_debug("Retention: Server %s is offline", config->common.servers[server].name);
         continue;
      }

      if (!atomic_compare_exchange_strong(&config->common.servers[server].repository, &active, true))
      {
         pgmoneta_log_debug("Retention: Server %s is active", config->common.servers[server].name);
         continue;
      }

#ifdef DEBUG
      pgmoneta_log_info("Retention: Acquired repository lock");
#endif

      config->common.servers[server].active_retention = true;

      workflow = pgmoneta_workflow_create(WORKFLOW_TYPE_RETENTION, NULL);

      if (pgmoneta_art_create(&nodes))
      {
         goto error;
      }

      if (pgmoneta_workflow_execute(workflow, nodes, &en, &ec))
      {
         goto error;
      }

      pgmoneta_art_destroy(nodes);
      pgmoneta_workflow_destroy(workflow);

      nodes = NULL;
      workflow = NULL;

      config->common.servers[server].active_retention = false;
      atomic_store(&config->common.servers[server].repository, false);
   }

   pgmoneta_stop_logging();

   exit(0);

error:

   pgmoneta_log_error("Retention: %s (%d)", en != NULL ? en : NAME, ec != -1 ? ec : MANAGEMENT_ERROR_RETENTION_ERROR);

   pgmoneta_art_destroy(nodes);
   pgmoneta_workflow_destroy(workflow);

   config->common.servers[server].active_retention = false;
   atomic_store(&config->common.servers[server].repository, false);

   pgmoneta_stop_logging();

   exit(1);
}
