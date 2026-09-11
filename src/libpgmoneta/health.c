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
#include <health.h>
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <utils.h>

/* system */
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static void health_check_loop(void);
static int server_probe(int server_idx, bool* up, int* auth_type);

void
pgmoneta_health_check(int argc, char** argv)
{
   pid_t pid;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < 100; i++)
   {
      pid = fork();
      if (pid != -1)
      {
         break;
      }
      SLEEP(10000000L)
   }

   if (pid == -1)
   {
      pgmoneta_log_error("Unable to fork health check process");
      return;
   }
   else if (pid == 0)
   {
      pgmoneta_start_logging();
      pgmoneta_memory_init();

      /* Restore default signal handlers so we can be terminated by the main process */
      signal(SIGTERM, SIG_DFL);
      signal(SIGINT, SIG_DFL);
      signal(SIGQUIT, SIG_DFL);

      pgmoneta_set_proc_title(argc, argv, "health check worker", NULL);
      health_check_loop();

      pgmoneta_memory_destroy();
      pgmoneta_stop_logging();
      exit(0);
   }
   else
   {
      config->health_check_pid = pid;
   }
}

void
pgmoneta_health_check_stop(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config->health_check_pid != 0)
   {
      kill(config->health_check_pid, SIGTERM);

      /* Wait up to 2s for exit - the worker checks its flags every 1s via sleep(1) */
      for (int i = 0; i < 10; i++)
      {
         if (kill(config->health_check_pid, 0))
         {
            break;
         }
         SLEEP(200000000L)
      }

      waitpid(config->health_check_pid, NULL, WNOHANG);
      config->health_check_pid = 0;
   }
}

static void
health_check_loop(void)
{
   struct main_configuration* config;
   bool up;
   int status;
   int previous_state[NUMBER_OF_SERVERS];
   int64_t period;

   config = (struct main_configuration*)shmem;

   pgmoneta_log_info("Health check started");

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      previous_state[i] = -2; /* Initial value representing 'never checked' */
   }

   while (config->running && config->health_check)
   {
      /* Sleep for the configured period, but check flags every second */
      period = pgmoneta_time_convert(config->health_check_period, FORMAT_TIME_S);
      if (period < HEALTH_CHECK_MIN_INTERVAL)
      {
         period = HEALTH_CHECK_MIN_INTERVAL;
      }
      for (int64_t i = 0; i < period && config->running && config->health_check; i++)
      {
         sleep(1);
      }

      if (!config->running || !config->health_check)
      {
         break;
      }

      pgmoneta_log_debug("Health check run");

      for (int i = 0; i < config->common.number_of_servers; i++)
      {
         int auth = HEALTH_CHECK_AUTH_UNKNOWN;
         up = false;
         status = server_probe(i, &up, &auth);

         /* status != 0 means connection or protocol error, treated as 'not up' for retries */
         if (status != 0)
         {
            up = false;
            atomic_store(&config->common.servers[i].auth_type, HEALTH_CHECK_AUTH_ERROR);
         }
         else
         {
            atomic_store(&config->common.servers[i].auth_type, (signed char)auth);
         }

         if (up)
         {
            config->common.servers[i].failures = 0;
            if (previous_state[i] != SERVER_HEALTH_UP)
            {
               pgmoneta_log_info("Health: Server %s is UP", config->common.servers[i].name);
               previous_state[i] = SERVER_HEALTH_UP;
            }
            atomic_store(&config->common.servers[i].health_state, SERVER_HEALTH_UP);
         }
         else
         {
            config->common.servers[i].failures++;
            if (config->common.servers[i].failures >= HEALTH_CHECK_MAX_RETRIES)
            {
               if (previous_state[i] != SERVER_HEALTH_DOWN)
               {
                  pgmoneta_log_warn("Health: Server %s is DOWN", config->common.servers[i].name);
                  previous_state[i] = SERVER_HEALTH_DOWN;
               }
               atomic_store(&config->common.servers[i].health_state, SERVER_HEALTH_DOWN);
            }
         }
      }
   }

   pgmoneta_log_info("Health check stopped");
}

static int
server_probe(int server_idx, bool* up, int* auth_type)
{
   struct main_configuration* config;
   SSL* ssl = NULL;
   int fd = -1;
   int usr = -1;
   int timeout;
   int status;
   struct message* query_msg = NULL;
   struct message* msg = NULL;
   bool query_success = false;
   bool query_ready = false;
   int offset_q;
   char type_q;
   int len_q;

   config = (struct main_configuration*)shmem;

   *up = false;
   *auth_type = HEALTH_CHECK_AUTH_UNKNOWN;

   /* Resolve the health check user from the user vault */
   for (int i = 0; usr == -1 && i < config->common.number_of_users; i++)
   {
      if (pgmoneta_compare_string(config->health_check_user, config->common.users[i].username))
      {
         usr = i;
      }
   }

   if (usr == -1)
   {
      pgmoneta_log_debug("Health: Health check user '%s' not found for server %s",
                         config->health_check_user, config->common.servers[server_idx].name);
      return 1;
   }

   pgmoneta_log_debug("Health: Probing server %s (%s:%d) as user %s",
                      config->common.servers[server_idx].name,
                      config->common.servers[server_idx].host,
                      config->common.servers[server_idx].port,
                      config->health_check_user);

   timeout = (int)pgmoneta_time_convert(config->health_check_timeout, FORMAT_TIME_S);
   if (timeout < 1)
   {
      timeout = 1;
   }

   if (pgmoneta_server_authenticate(server_idx, "postgres",
                                    config->common.users[usr].username,
                                    config->common.users[usr].password,
                                    false, &ssl, &fd) != AUTH_SUCCESS)
   {
      pgmoneta_log_debug("Health: Authentication failed for server %s",
                         config->common.servers[server_idx].name);
      *auth_type = HEALTH_CHECK_AUTH_ERROR;
      goto error;
   }

   *auth_type = pgmoneta_get_health_auth_type();

   if (pgmoneta_create_query_message("SELECT 1;", &query_msg) != MESSAGE_STATUS_OK || query_msg == NULL)
   {
      pgmoneta_log_debug("Health: Failed to create query for server %s",
                         config->common.servers[server_idx].name);
      goto error;
   }

   if (pgmoneta_write_message(ssl, fd, query_msg) != MESSAGE_STATUS_OK)
   {
      pgmoneta_log_debug("Health: Failed to send query to server %s",
                         config->common.servers[server_idx].name);
      goto error;
   }

   /* Wait for the query response, honouring health_check_timeout */
   while (true)
   {
      status = pgmoneta_read_timeout_message(ssl, fd, timeout, &msg);
      if (status != MESSAGE_STATUS_OK || msg == NULL)
      {
         pgmoneta_log_debug("Health: Failed to read query response from server %s (status %d)",
                            config->common.servers[server_idx].name, status);
         goto error;
      }

      offset_q = 0;
      while (offset_q < msg->length)
      {
         type_q = pgmoneta_read_byte(msg->data + offset_q);
         len_q = pgmoneta_read_int32(msg->data + offset_q + 1);

         if (type_q == 'T' || type_q == 'C' || type_q == 'D')
         {
            query_success = true;
         }
         else if (type_q == 'E')
         {
            query_success = false;
         }
         else if (type_q == 'Z')
         {
            query_ready = true;
            break;
         }

         offset_q += 1 + len_q;
         if (offset_q >= msg->length)
         {
            break;
         }
      }

      pgmoneta_clear_message();
      msg = NULL;

      if (query_ready)
      {
         break;
      }
   }

   pgmoneta_write_terminate(ssl, fd);

   *up = query_success;

   pgmoneta_free_message(query_msg);
   pgmoneta_close_ssl(ssl);
   pgmoneta_disconnect(fd);

   return 0;

error:
   if (msg != NULL)
   {
      pgmoneta_clear_message();
   }
   if (query_msg != NULL)
   {
      pgmoneta_free_message(query_msg);
   }
   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (fd != -1)
   {
      pgmoneta_disconnect(fd);
   }

   return 1;
}
