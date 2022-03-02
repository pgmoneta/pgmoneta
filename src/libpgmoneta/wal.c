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
#include <pgmoneta.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <security.h>
#include <server.h>
#include <wal.h>
#include <utils.h>

/* system */
#include <ev.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <openssl/ssl.h>

void
pgmoneta_wal(int srv, char** argv)
{
   int usr;
   char* d = NULL;
   char* cmd = NULL;
   int status;
   struct configuration* config;

   pgmoneta_start_logging();
   pgmoneta_memory_init();

   config = (struct configuration*)shmem;

   pgmoneta_set_proc_title(1, argv, "wal", config->servers[srv].name);

   usr = -1;
   for (int i = 0; usr == -1 && i < config->number_of_users; i++)
   {
      if (!strcmp(config->servers[srv].username, config->users[i].username))
      {
         usr = i;
      }  
   }

   pgmoneta_server_info(srv);

   d = pgmoneta_get_server_wal(srv);

   pgmoneta_mkdir(d);

   if (config->servers[srv].valid)
   {
      cmd = pgmoneta_append(cmd, "PGPASSWORD=\"");
      cmd = pgmoneta_append(cmd, config->users[usr].password);
      cmd = pgmoneta_append(cmd, "\" ");

      cmd = pgmoneta_append(cmd, config->pgsql_dir);
      cmd = pgmoneta_append(cmd, "/pg_receivewal ");

      cmd = pgmoneta_append(cmd, "-h ");
      cmd = pgmoneta_append(cmd, config->servers[srv].host);
      cmd = pgmoneta_append(cmd, " ");

      cmd = pgmoneta_append(cmd, "-p ");
      cmd = pgmoneta_append_int(cmd, config->servers[srv].port);
      cmd = pgmoneta_append(cmd, " ");

      cmd = pgmoneta_append(cmd, "-U ");
      cmd = pgmoneta_append(cmd, config->servers[srv].username);
      cmd = pgmoneta_append(cmd, " ");

      if (strlen(config->servers[srv].wal_slot) > 0)
      {
         cmd = pgmoneta_append(cmd, "-S ");
         cmd = pgmoneta_append(cmd, config->servers[srv].wal_slot);
         cmd = pgmoneta_append(cmd, " ");
      }

      if (config->servers[srv].synchronous)
      {
         cmd = pgmoneta_append(cmd, "--synchronous ");
      }

      cmd = pgmoneta_append(cmd, "--no-loop ");
      cmd = pgmoneta_append(cmd, "--no-password ");

      cmd = pgmoneta_append(cmd, "-D ");
      cmd = pgmoneta_append(cmd, d);
   
      pgmoneta_log_info("WAL: %s", config->servers[srv].name);

      config->servers[srv].wal_streaming = true;
      status = system(cmd);
      config->servers[srv].wal_streaming = false;

      if (status != 0)
      {
         config->servers[srv].valid = false;
         pgmoneta_log_error("WAL: Could not start receiver for %s", config->servers[srv].name);
      }
   }
   else
   {
      pgmoneta_log_error("WAL: Server %s is not in a valid configuration", config->servers[srv].name);
   }

   pgmoneta_memory_destroy();
   pgmoneta_stop_logging();

   free(d);
   free(cmd);

   exit(0);
}
