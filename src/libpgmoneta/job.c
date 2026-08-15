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
#include <job.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <workflow.h>

/* system */
#include <unistd.h>

#define NAME "job"

static int
pgmoneta_job_include_job_in_payload(int server, struct json* payload);

static int
pgmoneta_job_init_job_category(struct json* job_category, int server);

static void
pgmoneta_job_set_timestamp(struct tm* timestamp);

char*
pgmoneta_job_get_id_from_path(const char* path)
{
   char* copy = NULL;
   char* basename = NULL;
   char* extension = NULL;
   char* id = NULL;
   size_t length;

   if (path == NULL)
   {
      return NULL;
   }

   copy = strdup(path);
   if (copy == NULL)
   {
      return NULL;
   }

   basename = strrchr(copy, '/');
   if (basename != NULL)
   {
      basename++;
   }
   else
   {
      basename = copy;
   }

   length = strlen(basename);
   if (length == 0)
   {
      free(copy);
      return NULL;
   }

   extension = strrchr(basename, '.');
   if (extension != NULL &&
       (strcmp(extension, JOB_FILE_EXTENSION) == 0 || strcmp(extension, JOB_FILE_TEMP_EXTENSION) == 0))
   {
      length = (size_t)(extension - basename);
   }

   if (pgmoneta_starts_with(basename, JOB_FILE_PREFIX))
   {
      basename += strlen(JOB_FILE_PREFIX);
      length -= strlen(JOB_FILE_PREFIX);
   }

   id = malloc(length + 1);
   if (id == NULL)
   {
      free(copy);
      return NULL;
   }

   memcpy(id, basename, length);
   id[length] = '\0';

   free(copy);

   return id;
}

static struct json*
pgmoneta_job_read_job(char* job_id);

static int
pgmoneta_job_remove_job(char* job_id);

static const char*
pgmoneta_job_command_name(int command);

static int
pgmoneta_job_command_type(const char* command);

static char*
pgmoneta_job_state_name(int status);

static int
pgmoneta_job_management_tp_wkflw_tp(int wrkflw_tp);

int
pgmoneta_job_init(int server_id, int workflow_type)
{
   if (server_id < 0 || server_id >= NUMBER_OF_SERVERS)
   {
      return -1;
   }

   struct main_configuration* config;
   struct job* job = NULL;

   config = (struct main_configuration*)shmem;

   job = &config->common.servers[server_id].job;

   job->server_id = server_id;
   job->owner_pid = getpid();
   job->workflow_type = workflow_type;

   atomic_store(&job->current_phase, PHASE_NONE);
   atomic_store(&job->state, JOB_STATE_STARTING);

   if (pgmoneta_job_gen_id(server_id, pgmoneta_job_management_tp_wkflw_tp(workflow_type), job->id))
   {
      return -1;
   }

   memset(&job->finished_at, 0, sizeof(job->finished_at));
   pgmoneta_job_set_timestamp(&job->started_at);
   pgmoneta_job_set_timestamp(&job->updated_at);

   return 0;
}

int
pgmoneta_job_gen_id(int server_id, int command, char* id)
{
   time_t t;
   char timestamp[15];
   struct tm* tm_info;

   if (server_id < 0 || server_id >= NUMBER_OF_SERVERS)
   {
      return -1;
   }

   time(&t);

   tm_info = localtime(&t);

   if (tm_info == NULL)
   {
      return -1;
   }

   strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", tm_info);

   if (snprintf(id, MISC_LENGTH, "s%d-%s-%s", server_id, pgmoneta_job_command_name(command), timestamp) < 0)
   {
      return -1;
   }

   return 0;
}

int
pgmoneta_job_parse_id(char* id, int* server_id, int* command, char** timestamp)
{
   char command_str[MISC_LENGTH] = {0};
   char timestamp_l[MISC_LENGTH] = {0};
   char normalized_id[MAX_PATH] = {0};
   char* id_value = id;
   char* parsed_timestamp = NULL;
   int server_id_l = -1;
   int command_l = -1;
   int consumed = 0;
   size_t id_length;

   if (server_id != NULL)
   {
      *server_id = -1;
   }
   if (command != NULL)
   {
      *command = -1;
   }
   if (timestamp != NULL)
   {
      *timestamp = NULL;
   }

   if (id_value == NULL)
   {
      return 1;
   }

   if (pgmoneta_starts_with(id_value, JOB_FILE_PREFIX))
   {
      id_value += strlen(JOB_FILE_PREFIX);
   }

   id_length = strlen(id_value);
   if (id_length == 0 || id_length >= sizeof(normalized_id))
   {
      return 1;
   }

   memcpy(normalized_id, id_value, id_length + 1);

   if (id_length >= strlen(JOB_FILE_EXTENSION) &&
       strcmp(normalized_id + id_length - strlen(JOB_FILE_EXTENSION), JOB_FILE_EXTENSION) == 0)
   {
      normalized_id[id_length - strlen(JOB_FILE_EXTENSION)] = '\0';
   }

   if (sscanf(normalized_id, "s%d-%127[^-]-%127[0-9]%n", &server_id_l, command_str, timestamp_l, &consumed) != 3 ||
       normalized_id[consumed] != '\0' ||
       server_id_l < 0 || server_id_l >= NUMBER_OF_SERVERS)
   {
      return 1;
   }

   command_l = pgmoneta_job_command_type(command_str);
   if (command_l == -1)
   {
      return 1;
   }

   if (timestamp != NULL)
   {
      parsed_timestamp = strdup(timestamp_l);
      if (parsed_timestamp == NULL)
      {
         return 1;
      }
      *timestamp = parsed_timestamp;
   }

   if (server_id != NULL)
   {
      *server_id = server_id_l;
   }
   if (command != NULL)
   {
      *command = command_l;
   }

   return 0;
}

char*
pgmoneta_job_get_id(int server)
{
   if (server < 0 || server >= NUMBER_OF_SERVERS)
   {
      return NULL;
   }

   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   return config->common.servers[server].job.id;
}

char*
pgmoneta_job_get_state(int server)
{
   char* state_name = NULL;
   if (server < 0 || server >= NUMBER_OF_SERVERS)
   {
      return NULL;
   }

   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   state_name = pgmoneta_job_state_name(atomic_load(&config->common.servers[server].job.state));

   return state_name;
}

bool
pgmoneta_job_is_active(int server)
{
   if (server < 0 || server >= NUMBER_OF_SERVERS)
   {
      return false;
   }

   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   struct job* job = &config->common.servers[server].job;
   int state = atomic_load(&job->state);

   if (state != JOB_STATE_NONE && job->server_id == server)
   {
      return true;
   }

   return false;
}

void
pgmoneta_job_update_phase(int server, int phase)
{
   if (server < 0 || server >= NUMBER_OF_SERVERS)
   {
      return;
   }

   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   struct job* job = &config->common.servers[server].job;
   atomic_store(&job->current_phase, phase);
   pgmoneta_job_set_timestamp(&job->updated_at);
}

void
pgmoneta_job_update_state(int server, int state)
{
   if (server < 0 || server >= NUMBER_OF_SERVERS)
   {
      return;
   }

   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   atomic_store(&config->common.servers[server].job.state, state);
   pgmoneta_job_set_timestamp(&config->common.servers[server].job.updated_at);

   if (state == JOB_STATE_COMPLETED || state == JOB_STATE_FAILED)
   {
      pgmoneta_job_set_timestamp(&config->common.servers[server].job.finished_at);
   }

   if (state == JOB_STATE_STARTING)
   {
      pgmoneta_job_set_timestamp(&config->common.servers[server].job.started_at);
   }
}

int
pgmoneta_job_flush(int server, struct json* payload)
{
   char tmp_path[MAX_EXTRA_PATH];
   char path[MAX_EXTRA_PATH];

   if (server < 0 || server >= NUMBER_OF_SERVERS)
   {
      return -1;
   }

   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (payload == NULL)
   {
      return -1;
   }

   /* Fill the payload with the job details */
   pgmoneta_job_include_job_in_payload(server, payload);

   snprintf(tmp_path, sizeof(tmp_path), "%s/%s/%s%s%s", config->base_dir, JOBS_DIR, JOB_FILE_PREFIX, config->common.servers[server].job.id, JOB_FILE_TEMP_EXTENSION);
   snprintf(path, sizeof(path), "%s/%s/%s%s%s", config->base_dir, JOBS_DIR, JOB_FILE_PREFIX, config->common.servers[server].job.id, JOB_FILE_EXTENSION);

   /* Write the payload to a temporary file first */
   if (pgmoneta_json_write_file(tmp_path, payload))
   {
      return -1;
   }

   /* Rename the temporary file to the final job file */
   if (rename(tmp_path, path) != 0)
   {
      return -1;
   }

   return 0;
}

void
pgmoneta_job_cleanup(int server)
{
   if (server < 0 || server >= NUMBER_OF_SERVERS)
   {
      return;
   }

   struct main_configuration* config;
   struct job* job;

   config = (struct main_configuration*)shmem;

   /* Reset the job structure */
   job = &config->common.servers[server].job;

   job->id[0] = '\0';
   job->server_id = -1;
   job->owner_pid = 0;
   job->workflow_type = -1;
   atomic_store(&job->current_phase, 0);
   atomic_store(&job->state, JOB_STATE_NONE);
   memset(&job->started_at, 0, sizeof(job->started_at));
   memset(&job->updated_at, 0, sizeof(job->updated_at));
   memset(&job->finished_at, 0, sizeof(job->finished_at));
}

int
pgmoneta_job_finish(int server, struct json* payload)
{
   if (server < 0 || server >= NUMBER_OF_SERVERS)
   {
      return -1;
   }

   /* Flush the job information to disk */
   if (pgmoneta_job_flush(server, payload))
   {
      return -1;
   }

   /* Cleanup the job structure */
   pgmoneta_job_cleanup(server);

   return 0;
}

int
pgmoneta_job_fill_response(int server, struct json* response)
{
   if (server < 0 || server >= NUMBER_OF_SERVERS)
   {
      return -1;
   }

   struct main_configuration* config;
   struct job* job;

   config = (struct main_configuration*)shmem;
   job = &config->common.servers[server].job;

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_JOB_ID, (uintptr_t)job->id, ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_JOB_STATE, (uintptr_t)pgmoneta_job_get_state(server), ValueString);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_JOB_PID, (uintptr_t)job->owner_pid, ValueInt32);
   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_WORKFLOW, (uintptr_t)pgmoneta_job_command_name(pgmoneta_job_management_tp_wkflw_tp(job->workflow_type)), ValueString);

   return 0;
}

void
pgmoneta_job_rq(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* en = NULL;
   int ec = -1;
   struct json* req = NULL;
   struct json* res = NULL;
   struct json* job_payload_file = NULL;
   struct json* job_category = NULL;
   struct json* outcome_category = NULL;
   struct json* response_category = NULL;
   struct json* job_category_copy = NULL;
   struct json* outcome_category_copy = NULL;
   struct json* response_category_copy = NULL;
   char* job_id = NULL;
   char* elapsed = NULL;
   int srv = -1;
   int command = -1;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   pgmoneta_start_logging();

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   job_id = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_JOB_ID);

   /* First: Check if currently active */
   if (pgmoneta_job_parse_id(job_id, &srv, &command, NULL))
   {
      ec = MANAGEMENT_ERROR_JOB_ERROR;
      goto error;
   }

   if (pgmoneta_management_create_response(payload, srv, &res))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      goto error;
   }

   if (pgmoneta_job_is_active(srv) && pgmoneta_compare_string(job_id, config->common.servers[srv].job.id))
   {
      /* Read the current active job data */
      if (pgmoneta_job_include_job_in_payload(srv, res))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }
   }
   else
   {
      /* Read a previously finished job data */
      job_payload_file = pgmoneta_job_read_job(job_id);

      if (job_payload_file == NULL)
      {
         ec = MANAGEMENT_ERROR_JOB_NOT_FOUND;
         goto error;
      }

      job_category = (struct json*)pgmoneta_json_get(job_payload_file, MANAGEMENT_CATEGORY_JOB);
      outcome_category = (struct json*)pgmoneta_json_get(job_payload_file, MANAGEMENT_CATEGORY_OUTCOME);
      response_category = (struct json*)pgmoneta_json_get(job_payload_file, MANAGEMENT_CATEGORY_RESPONSE);

      if (job_category == NULL || outcome_category == NULL || response_category == NULL)
      {
         ec = MANAGEMENT_ERROR_JOB_ERROR;
         goto error;
      }

      if (pgmoneta_json_clone(job_category, &job_category_copy) ||
          pgmoneta_json_clone(outcome_category, &outcome_category_copy) ||
          pgmoneta_json_clone(response_category, &response_category_copy))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }

      if (pgmoneta_json_put(res, MANAGEMENT_CATEGORY_JOB, (uintptr_t)job_category_copy, ValueJSON))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }
      job_category_copy = NULL;

      if (pgmoneta_json_put(res, MANAGEMENT_CATEGORY_OUTCOME, (uintptr_t)outcome_category_copy, ValueJSON))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }
      outcome_category_copy = NULL;

      if (pgmoneta_json_put(res, MANAGEMENT_CATEGORY_RESPONSE, (uintptr_t)response_category_copy, ValueJSON))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }
      response_category_copy = NULL;
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(ssl, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_JOB_NETWORK;
      pgmoneta_log_error("Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Job: %s (Elapsed: %s)", job_id, elapsed);

   pgmoneta_json_destroy(payload);
   pgmoneta_json_destroy(job_payload_file);

   pgmoneta_json_destroy(job_category_copy);
   pgmoneta_json_destroy(outcome_category_copy);
   pgmoneta_json_destroy(response_category_copy);

   free(elapsed);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);
error:

   pgmoneta_management_response_error(ssl, client_fd, NULL,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_JOB_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   pgmoneta_json_destroy(payload);
   pgmoneta_json_destroy(job_payload_file);

   pgmoneta_json_destroy(job_category_copy);
   pgmoneta_json_destroy(outcome_category_copy);
   pgmoneta_json_destroy(response_category_copy);

   free(elapsed);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}

void
pgmoneta_job_status_rq(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* en = NULL;
   int ec = -1;
   struct json* req = NULL;
   struct json* res = NULL;
   struct json* job_payload_file = NULL;
   struct json* job_category = NULL;
   struct json* outcome_category = NULL;
   struct json* response_category = NULL;
   struct json* job_category_copy = NULL;
   struct json* outcome_category_copy = NULL;
   struct json* response_category_copy = NULL;
   struct deque* jobs_dq = NULL;
   struct deque_iterator* diter = NULL;
   bool found = false;
   char* server = NULL;
   char* cmd = NULL;
   char* job_id = NULL;
   char dir_path[MAX_EXTRA_PATH];
   char* elapsed = NULL;
   int srv = -1;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   pgmoneta_start_logging();

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   server = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_SERVER);
   cmd = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_COMMAND);

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      if (pgmoneta_compare_string(server, config->common.servers[i].name))
      {
         srv = i;
         break;
      }
   }

   if (srv == -1)
   {
      ec = MANAGEMENT_ERROR_JOB_NOSERVER;
      goto error;
   }

   if (pgmoneta_management_create_response(payload, srv, &res))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      goto error;
   }

   if (pgmoneta_job_is_active(srv) && pgmoneta_job_command_type(cmd) == pgmoneta_job_management_tp_wkflw_tp(config->common.servers[srv].job.workflow_type) && pgmoneta_job_command_type(cmd) != -1)
   {
      /* Read the current active job data */
      if (pgmoneta_job_include_job_in_payload(srv, res))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }
   }
   else
   {
      /* Read a previously finished job data if exists */
      if (pgmoneta_deque_create(false, &jobs_dq))
      {
         ec = MANAGEMENT_ERROR_JOB_DEQUE_CREATE;
         goto error;
      }

      snprintf(dir_path, sizeof(dir_path), "%s/%s", config->base_dir, JOBS_DIR);

      if (pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, dir_path, false, &jobs_dq))
      {
         ec = MANAGEMENT_ERROR_JOB_ERROR;
         goto error;
      }

      if (pgmoneta_deque_iterator_create(jobs_dq, &diter))
      {
         ec = MANAGEMENT_ERROR_JOB_ERROR;
         goto error;
      }

      while (pgmoneta_deque_iterator_next(diter))
      {
         int job_srv = -1;
         int job_command_type = -1;
         char* candidate_job_id = NULL;

         candidate_job_id = pgmoneta_job_get_id_from_path((char*)diter->tag);
         if (candidate_job_id == NULL)
         {
            continue;
         }

         if (pgmoneta_job_parse_id(candidate_job_id, &job_srv, &job_command_type, NULL))
         {
            free(candidate_job_id);
            continue;
         }

         if (job_command_type == -1)
         {
            free(candidate_job_id);
            continue;
         }

         if (srv == job_srv && job_command_type == pgmoneta_job_command_type(cmd))
         {
            job_id = candidate_job_id;
            found = true;
            break;
         }

         free(candidate_job_id);
      }

      if (!found)
      {
         ec = MANAGEMENT_ERROR_JOB_NOT_FOUND;
         goto error;
      }

      job_payload_file = pgmoneta_job_read_job(job_id);

      if (job_payload_file == NULL)
      {
         ec = MANAGEMENT_ERROR_JOB_NOT_FOUND;
         goto error;
      }

      job_category = (struct json*)pgmoneta_json_get(job_payload_file, MANAGEMENT_CATEGORY_JOB);
      outcome_category = (struct json*)pgmoneta_json_get(job_payload_file, MANAGEMENT_CATEGORY_OUTCOME);
      response_category = (struct json*)pgmoneta_json_get(job_payload_file, MANAGEMENT_CATEGORY_RESPONSE);

      if (job_category == NULL || outcome_category == NULL || response_category == NULL)
      {
         ec = MANAGEMENT_ERROR_JOB_ERROR;
         goto error;
      }

      if (pgmoneta_json_clone(job_category, &job_category_copy) ||
          pgmoneta_json_clone(outcome_category, &outcome_category_copy) ||
          pgmoneta_json_clone(response_category, &response_category_copy))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }

      if (pgmoneta_json_put(res, MANAGEMENT_CATEGORY_JOB, (uintptr_t)job_category_copy, ValueJSON))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }
      job_category_copy = NULL;

      if (pgmoneta_json_put(res, MANAGEMENT_CATEGORY_OUTCOME, (uintptr_t)outcome_category_copy, ValueJSON))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }
      outcome_category_copy = NULL;

      if (pgmoneta_json_put(res, MANAGEMENT_CATEGORY_RESPONSE, (uintptr_t)response_category_copy, ValueJSON))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }
      response_category_copy = NULL;
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(ssl, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_JOB_NETWORK;
      pgmoneta_log_error("Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Job: %s (Elapsed: %s)", job_id, elapsed);

   pgmoneta_json_destroy(payload);
   pgmoneta_json_destroy(job_payload_file);
   pgmoneta_deque_iterator_destroy(diter);
   pgmoneta_deque_destroy(jobs_dq);

   pgmoneta_json_destroy(job_category_copy);
   pgmoneta_json_destroy(outcome_category_copy);
   pgmoneta_json_destroy(response_category_copy);

   free(elapsed);
   free(job_id);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);
error:

   pgmoneta_management_response_error(ssl, client_fd, NULL,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_JOB_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   pgmoneta_json_destroy(payload);
   pgmoneta_json_destroy(job_payload_file);
   pgmoneta_deque_iterator_destroy(diter);
   pgmoneta_deque_destroy(jobs_dq);

   pgmoneta_json_destroy(job_category_copy);
   pgmoneta_json_destroy(outcome_category_copy);
   pgmoneta_json_destroy(response_category_copy);

   free(elapsed);
   free(job_id);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}

void
pgmoneta_job_remove_rq(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* en = NULL;
   int ec = -1;
   struct json* hdr = NULL;
   struct json* req = NULL;
   struct json* res = NULL;
   int rmv_tp = -1;
   struct deque* jobs_dq = NULL;
   struct deque_iterator* diter = NULL;
   char* job_id = NULL;
   char* requested_job_id = NULL;
   char dir_path[MAX_EXTRA_PATH];
   char* elapsed = NULL;
   int srv = -1;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   pgmoneta_start_logging();

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   hdr = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_HEADER);
   rmv_tp = (int)(uintptr_t)pgmoneta_json_get(hdr, MANAGEMENT_ARGUMENT_COMMAND);
   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);

   if (pgmoneta_management_create_response(payload, srv, &res))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      goto error;
   }

   if (rmv_tp == MANAGEMENT_JOB_REMOVE_JOB)
   {
      /* Remove a specific job */

      requested_job_id = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_JOB_ID);

      if (requested_job_id == NULL)
      {
         ec = MANAGEMENT_ERROR_JOB_ERROR;
         goto error;
      }

      if (pgmoneta_job_parse_id(requested_job_id, &srv, NULL, NULL))
      {
         ec = MANAGEMENT_ERROR_JOB_ERROR;
         goto error;
      }

      if (pgmoneta_compare_string(requested_job_id, config->common.servers[srv].job.id))
      {
         ec = MANAGEMENT_ERROR_JOB_ACTIVE;
         goto error;
      }

      if (pgmoneta_job_remove_job(requested_job_id))
      {
         ec = MANAGEMENT_ERROR_JOB_ERROR;
         goto error;
      }

      pgmoneta_json_put(res, MANAGEMENT_ARGUMENT_JOB_ID, (uintptr_t)requested_job_id, ValueString);
   }
   else if (rmv_tp == MANAGEMENT_JOB_REMOVE_ALL)
   {
      /* Remove all jobs */
      if (pgmoneta_deque_create(false, &jobs_dq))
      {
         ec = MANAGEMENT_ERROR_JOB_DEQUE_CREATE;
         goto error;
      }

      snprintf(dir_path, sizeof(dir_path), "%s/%s", config->base_dir, JOBS_DIR);

      if (pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, dir_path, false, &jobs_dq))
      {
         ec = MANAGEMENT_ERROR_JOB_ERROR;
         goto error;
      }

      if (pgmoneta_deque_iterator_create(jobs_dq, &diter))
      {
         ec = MANAGEMENT_ERROR_JOB_ERROR;
         goto error;
      }

      while (pgmoneta_deque_iterator_next(diter))
      {
         job_id = pgmoneta_job_get_id_from_path((char*)diter->tag);

         if (job_id == NULL)
         {
            continue;
         }

         if (pgmoneta_job_remove_job(job_id))
         {
            ec = MANAGEMENT_ERROR_JOB_ERROR;
            goto error;
         }

         free(job_id);
         job_id = NULL;
      }
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(ssl, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_JOB_NETWORK;
      pgmoneta_log_error("Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Job Remove: (Elapsed: %s)", elapsed);

   pgmoneta_json_destroy(payload);
   pgmoneta_deque_iterator_destroy(diter);
   pgmoneta_deque_destroy(jobs_dq);

   free(elapsed);
   free(job_id);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);
error:

   pgmoneta_management_response_error(ssl, client_fd, NULL,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_JOB_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   pgmoneta_json_destroy(payload);
   pgmoneta_deque_iterator_destroy(diter);
   pgmoneta_deque_destroy(jobs_dq);

   free(elapsed);
   free(job_id);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}

void
pgmoneta_job_list_rq(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* en = NULL;
   int ec = -1;
   struct json* hdr = NULL;
   struct json* req = NULL;
   struct json* res = NULL;
   struct json* jobs = NULL;
   struct json* job_payload_file = NULL;
   struct json* job_category = NULL;
   struct json* job_category_copy = NULL;
   struct json* active_job = NULL;
   struct deque* jobs_dq = NULL;
   struct deque_iterator* diter = NULL;
   int lst_tp = -1;
   char* server = NULL;
   char* status = NULL;
   char* job_id = NULL;
   int srv = -1;
   int nm_of_jobs = 0;
   char* elapsed = NULL;
   char dir_path[MAX_EXTRA_PATH];
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   pgmoneta_start_logging();

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   hdr = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_HEADER);
   lst_tp = (int)(uintptr_t)pgmoneta_json_get(hdr, MANAGEMENT_ARGUMENT_COMMAND);

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);

   if (lst_tp == MANAGEMENT_JOB_LIST_SERVER)
   {
      server = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_SERVER);

      if (server == NULL)
      {
         ec = MANAGEMENT_ERROR_JOB_ERROR;
         goto error;
      }

      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (pgmoneta_compare_string(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }
   }
   else if (lst_tp == MANAGEMENT_JOB_LIST_STATUS)
   {
      status = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_JOB_STATE);

      if (status == NULL)
      {
         ec = MANAGEMENT_ERROR_JOB_ERROR;
         goto error;
      }
   }

   if (pgmoneta_management_create_response(payload, srv, &res))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      goto error;
   }

   /* Read all jobs (Running - Failed - Completed) then filter them */
   if (pgmoneta_json_create(&jobs))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      goto error;
   }

   for (int i = 0; i < NUMBER_OF_SERVERS; i++)
   {
      if (pgmoneta_job_is_active(i))
      {
         if (pgmoneta_json_create(&active_job))
         {
            ec = MANAGEMENT_ERROR_ALLOCATION;
            goto error;
         }

         if (pgmoneta_job_init_job_category(active_job, i))
         {
            ec = MANAGEMENT_ERROR_ALLOCATION;
            goto error;
         }

         if (srv != -1 && i == srv)
         {
            pgmoneta_json_put(active_job, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)i, ValueInt32);
            if (pgmoneta_json_append(jobs, (uintptr_t)active_job, ValueJSON))
            {
               ec = MANAGEMENT_ERROR_ALLOCATION;
               goto error;
            }
            active_job = NULL;
            nm_of_jobs++;
            goto next_active;
         }

         if (status != NULL)
         {
            char* job_state = (char*)pgmoneta_json_get(active_job, MANAGEMENT_ARGUMENT_JOB_STATE);

            if (job_state != NULL && pgmoneta_compare_string(job_state, status))
            {
               pgmoneta_json_put(active_job, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)i, ValueInt32);
               if (pgmoneta_json_append(jobs, (uintptr_t)active_job, ValueJSON))
               {
                  ec = MANAGEMENT_ERROR_ALLOCATION;
                  goto error;
               }
               active_job = NULL;
               nm_of_jobs++;
            }

            goto next_active;
         }

         pgmoneta_json_put(active_job, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)i, ValueInt32);
         if (pgmoneta_json_append(jobs, (uintptr_t)active_job, ValueJSON))
         {
            ec = MANAGEMENT_ERROR_ALLOCATION;
            goto error;
         }
         active_job = NULL;
         nm_of_jobs++;
      }

next_active:
      pgmoneta_json_destroy(active_job);
      active_job = NULL;
      continue;
   }

   if (pgmoneta_deque_create(false, &jobs_dq))
   {
      ec = MANAGEMENT_ERROR_JOB_DEQUE_CREATE;
      goto error;
   }

   snprintf(dir_path, sizeof(dir_path), "%s/%s", config->base_dir, JOBS_DIR);
   if (pgmoneta_get_files(PGMONETA_FILE_TYPE_ALL, dir_path, false, &jobs_dq))
   {
      ec = MANAGEMENT_ERROR_JOB_ERROR;
      goto error;
   }

   if (pgmoneta_deque_iterator_create(jobs_dq, &diter))
   {
      ec = MANAGEMENT_ERROR_JOB_ERROR;
      goto error;
   }

   while (pgmoneta_deque_iterator_next(diter))
   {
      int job_srv = -1;

      job_id = pgmoneta_job_get_id_from_path((char*)diter->tag);
      if (job_id == NULL)
      {
         continue;
      }

      if (pgmoneta_job_parse_id(job_id, &job_srv, NULL, NULL))
      {
         goto next;
      }

      job_payload_file = pgmoneta_job_read_job(job_id);

      if (job_payload_file == NULL)
      {
         goto next;
      }

      job_category = (struct json*)pgmoneta_json_get(job_payload_file, MANAGEMENT_CATEGORY_JOB);

      if (srv != -1 && job_srv != srv)
      {
         goto next;
      }

      if (status != NULL)
      {
         char* job_state = (char*)pgmoneta_json_get(job_category, MANAGEMENT_ARGUMENT_JOB_STATE);

         if (job_state == NULL || !pgmoneta_compare_string(job_state, status))
         {
            goto next;
         }
      }

      if (pgmoneta_json_clone(job_category, &job_category_copy))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }

      pgmoneta_json_put(job_category_copy, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)job_srv, ValueInt32);
      if (pgmoneta_json_append(jobs, (uintptr_t)job_category_copy, ValueJSON))
      {
         ec = MANAGEMENT_ERROR_ALLOCATION;
         goto error;
      }
      job_category_copy = NULL;
      nm_of_jobs++;

next:
      pgmoneta_json_destroy(job_payload_file);
      job_payload_file = NULL;
      free(job_id);
      job_id = NULL;
   }

   if (pgmoneta_json_put(res, MANAGEMENT_ARGUMENT_JOBS, (uintptr_t)jobs, ValueJSON))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      goto error;
   }
   jobs = NULL;
   pgmoneta_json_put(res, MANAGEMENT_ARGUMENT_NUMBER_OF_JOBS, (uintptr_t)nm_of_jobs, ValueInt32);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(ssl, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_JOB_NETWORK;
      pgmoneta_log_error("Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Job List: (Elapsed: %s)", elapsed);

   pgmoneta_json_destroy(payload);
   pgmoneta_deque_iterator_destroy(diter);
   pgmoneta_deque_destroy(jobs_dq);

   free(elapsed);
   free(job_id);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(0);
error:

   pgmoneta_management_response_error(ssl, client_fd, NULL,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_JOB_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   pgmoneta_json_destroy(payload);
   pgmoneta_json_destroy(active_job);
   pgmoneta_json_destroy(job_category_copy);
   pgmoneta_json_destroy(job_payload_file);
   pgmoneta_json_destroy(jobs);
   pgmoneta_deque_iterator_destroy(diter);
   pgmoneta_deque_destroy(jobs_dq);

   free(elapsed);
   free(job_id);

   pgmoneta_disconnect(client_fd);

   pgmoneta_stop_logging();

   exit(1);
}

static int
pgmoneta_job_include_job_in_payload(int server, struct json* payload)
{
   struct json* j = NULL;

   if (pgmoneta_json_create(&j))
   {
      goto error;
   }

   if (pgmoneta_job_init_job_category(j, server))
   {
      goto error;
   }

   if (pgmoneta_json_put(payload, MANAGEMENT_CATEGORY_JOB, (uintptr_t)j, ValueJSON))
   {
      goto error;
   }

   return 0;

error:
   pgmoneta_json_destroy(j);

   return 1;
}

static int
pgmoneta_job_init_job_category(struct json* job_category, int server)
{
   char started_at[32] = {0};
   char updated_at[32] = {0};
   char finished_at[32] = {0};

   if (server < 0 || server >= NUMBER_OF_SERVERS)
   {
      return 1;
   }

   struct main_configuration* config;
   struct job* job;
   config = (struct main_configuration*)shmem;

   job = &config->common.servers[server].job;

   if (job->started_at.tm_year != 0)
   {
      strftime(started_at, sizeof(started_at), "%Y-%m-%d - %H:%M:%S", &job->started_at);
   }

   if (job->updated_at.tm_year != 0)
   {
      strftime(updated_at, sizeof(updated_at), "%Y-%m-%d - %H:%M:%S", &job->updated_at);
   }

   if (job->finished_at.tm_year != 0)
   {
      strftime(finished_at, sizeof(finished_at), "%Y-%m-%d - %H:%M:%S", &job->finished_at);
   }

   pgmoneta_json_put(job_category, MANAGEMENT_ARGUMENT_JOB_ID, (uintptr_t)job->id, ValueString);
   pgmoneta_json_put(job_category, MANAGEMENT_ARGUMENT_JOB_STATE, (uintptr_t)pgmoneta_job_get_state(server), ValueString);
   pgmoneta_json_put(job_category, MANAGEMENT_ARGUMENT_JOB_PID, (uintptr_t)job->owner_pid, ValueInt32);
   pgmoneta_json_put(job_category, MANAGEMENT_ARGUMENT_WORKFLOW, (uintptr_t)pgmoneta_job_command_name(pgmoneta_job_management_tp_wkflw_tp(job->workflow_type)), ValueString);
   pgmoneta_json_put(job_category, MANAGEMENT_ARGUMENT_JOB_START_TIME, (uintptr_t)started_at, ValueString);
   pgmoneta_json_put(job_category, MANAGEMENT_ARGUMENT_JOB_UPDATED_TIME, (uintptr_t)updated_at, ValueString);
   pgmoneta_json_put(job_category, MANAGEMENT_ARGUMENT_JOB_END_TIME, (uintptr_t)finished_at, ValueString);

   return 0;
}

static void
pgmoneta_job_set_timestamp(struct tm* timestamp)
{
   time_t now = time(NULL);

   if (gmtime_r(&now, timestamp) == NULL)
   {
      memset(timestamp, 0, sizeof(*timestamp));
   }
}

static struct json*
pgmoneta_job_read_job(char* job_id)
{
   struct json* payload = NULL;
   char path[MAX_EXTRA_PATH];
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   snprintf(path, sizeof(path), "%s/%s/%s%s%s", config->base_dir, JOBS_DIR, JOB_FILE_PREFIX, job_id, JOB_FILE_EXTENSION);

   if (!pgmoneta_is_file(path))
   {
      pgmoneta_json_destroy(payload);
      return NULL;
   }

   if (pgmoneta_json_read_file(path, &payload))
   {
      pgmoneta_json_destroy(payload);
      return NULL;
   }

   return payload;
}

static int
pgmoneta_job_remove_job(char* job_id)
{
   char path[MAX_EXTRA_PATH];
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   snprintf(path, sizeof(path), "%s/%s/%s%s%s", config->base_dir, JOBS_DIR, JOB_FILE_PREFIX, job_id, JOB_FILE_EXTENSION);

   if (!pgmoneta_is_file(path))
   {
      return -1;
   }

   if (remove(path) != 0)
   {
      return -1;
   }

   return 0;
}

static const char*
pgmoneta_job_command_name(int command)
{
   switch (command)
   {
      case MANAGEMENT_BACKUP:
         return "backup";
      case MANAGEMENT_RESTORE:
         return "restore";
      case MANAGEMENT_ARCHIVE:
         return "archive";
      case MANAGEMENT_DELETE:
         return "delete";
      default:
         return "unknown";
   }
}

static int
pgmoneta_job_command_type(const char* command)
{
   if (pgmoneta_compare_string(command, "backup"))
   {
      return MANAGEMENT_BACKUP;
   }
   else if (pgmoneta_compare_string(command, "restore"))
   {
      return MANAGEMENT_RESTORE;
   }
   else if (pgmoneta_compare_string(command, "archive"))
   {
      return MANAGEMENT_ARCHIVE;
   }
   else if (pgmoneta_compare_string(command, "delete"))
   {
      return MANAGEMENT_DELETE;
   }

   return -1;
}

static char*
pgmoneta_job_state_name(int status)
{
   switch (status)
   {
      case JOB_STATE_NONE:
         return "None";
      case JOB_STATE_STARTING:
         return "Starting";
      case JOB_STATE_RUNNING:
         return "Running";
      case JOB_STATE_COMPLETED:
         return "Completed";
      case JOB_STATE_FAILED:
         return "Failed";
      default:
         return "Unknown";
   }
}

static int
pgmoneta_job_management_tp_wkflw_tp(int wrkflw_tp)
{
   switch (wrkflw_tp)
   {
      case WORKFLOW_TYPE_BACKUP:
      case WORKFLOW_TYPE_INCREMENTAL_BACKUP:
         return MANAGEMENT_BACKUP;
      case WORKFLOW_TYPE_RESTORE:
         return MANAGEMENT_RESTORE;
      case WORKFLOW_TYPE_ARCHIVE:
         return MANAGEMENT_ARCHIVE;
      case WORKFLOW_TYPE_DELETE_BACKUP:
         return MANAGEMENT_DELETE;
      default:
         return -1;
   }
}