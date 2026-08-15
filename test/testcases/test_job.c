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

#include <pgmoneta.h>
#include <job.h>
#include <json.h>
#include <management.h>
#include <mctf.h>
#include <shmem.h>
#include <tscommon.h>
#include <utils.h>
#include <workflow.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct json*
job_metadata(struct json* payload)
{
   struct json* response = NULL;

   response = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_RESPONSE);
   if (response == NULL)
   {
      return NULL;
   }

   return (struct json*)pgmoneta_json_get(response, MANAGEMENT_CATEGORY_JOB);
}

MCTF_TEST(test_job_generate_and_parse_id)
{
   char id[MISC_LENGTH] = {0};
   char* timestamp = NULL;
   int server = -1;
   int command = -1;

   MCTF_ASSERT_INT_EQ(pgmoneta_job_gen_id(0, MANAGEMENT_ARCHIVE, id), 0, cleanup, "job id generation failed");
   MCTF_ASSERT(pgmoneta_starts_with(id, "s0-archive-"), cleanup, "generated job id has an unexpected prefix");
   MCTF_ASSERT_INT_EQ(pgmoneta_job_parse_id(id, &server, &command, &timestamp), 0, cleanup, "job id parsing failed");
   MCTF_ASSERT_INT_EQ(server, 0, cleanup, "parsed server differs from generated job id");
   MCTF_ASSERT_INT_EQ(command, MANAGEMENT_ARCHIVE, cleanup, "parsed command differs from generated job id");
   MCTF_ASSERT_PTR_NONNULL(timestamp, cleanup, "parsed timestamp is null");
   MCTF_ASSERT_INT_EQ(strlen(timestamp), 14, cleanup, "parsed timestamp has an unexpected length");

cleanup:
   free(timestamp);
   MCTF_FINISH();
}

MCTF_TEST(test_job_parse_id_rejects_invalid_values)
{
   int server = 0;
   int command = 0;

   MCTF_ASSERT_INT_EQ(pgmoneta_job_parse_id(NULL, &server, &command, NULL), 1, cleanup, "null job id was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_job_parse_id("s0-unknown-20260817001442", &server, &command, NULL), 1, cleanup, "unknown command was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_job_parse_id("s9999-archive-20260817001442", &server, &command, NULL), 1, cleanup, "invalid server was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_job_parse_id("s0-archive-invalid", &server, &command, NULL), 1, cleanup, "invalid timestamp was accepted");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_job_get_id_from_path)
{
   char* id = NULL;

   id = pgmoneta_job_get_id_from_path("/tmp/pgmoneta/jobs/job-s0-archive-20260817001442.json");
   MCTF_ASSERT_PTR_NONNULL(id, cleanup, "job id extraction returned null");
   MCTF_ASSERT_STR_EQ(id, "s0-archive-20260817001442", cleanup, "job id extraction from JSON path failed");
   free(id);
   id = NULL;

   id = pgmoneta_job_get_id_from_path("job-s0-backup-20260817001442.tmp");
   MCTF_ASSERT_PTR_NONNULL(id, cleanup, "temporary job id extraction returned null");
   MCTF_ASSERT_STR_EQ(id, "s0-backup-20260817001442", cleanup, "job id extraction from temporary path failed");

cleanup:
   free(id);
   MCTF_FINISH();
}

MCTF_TEST(test_job_lifecycle_metadata)
{
   struct main_configuration* config = NULL;
   struct json* response = NULL;
   struct json* job = NULL;
   char path[MAX_EXTRA_PATH] = {0};
   char* id = NULL;
   char* state = NULL;
   char* started_at = NULL;
   char* updated_at = NULL;
   char* finished_at = NULL;

   pgmoneta_test_setup();

   config = (struct main_configuration*)shmem;
   MCTF_ASSERT_INT_EQ(pgmoneta_mkdir(config->base_dir), 0, cleanup, "failed to create test base directory");
   snprintf(path, sizeof(path), "%s/%s", config->base_dir, JOBS_DIR);
   MCTF_ASSERT_INT_EQ(pgmoneta_mkdir(path), 0, cleanup, "failed to create jobs directory");

   MCTF_ASSERT_INT_EQ(pgmoneta_job_init(PRIMARY_SERVER, WORKFLOW_TYPE_ARCHIVE), 0, cleanup, "job initialization failed");
   pgmoneta_job_update_state(PRIMARY_SERVER, JOB_STATE_RUNNING);
   pgmoneta_job_update_phase(PRIMARY_SERVER, PHASE_NONE);

   MCTF_ASSERT_INT_EQ(pgmoneta_json_create(&response), 0, cleanup, "failed to create response JSON");
   MCTF_ASSERT_INT_EQ(pgmoneta_job_fill_response(PRIMARY_SERVER, response), 0, cleanup, "failed to fill job response");
   MCTF_ASSERT(pgmoneta_job_is_active(PRIMARY_SERVER), cleanup, "running job is not active");

   id = (char*)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_JOB_ID);
   state = (char*)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_JOB_STATE);
   MCTF_ASSERT_PTR_NONNULL(id, cleanup, "job response has no id");
   MCTF_ASSERT_STR_EQ(state, "Running", cleanup, "job response has an unexpected state");

   pgmoneta_job_update_state(PRIMARY_SERVER, JOB_STATE_COMPLETED);
   MCTF_ASSERT_INT_EQ(pgmoneta_job_flush(PRIMARY_SERVER, response), 0, cleanup, "job flush failed");

   job = (struct json*)pgmoneta_json_get(response, MANAGEMENT_CATEGORY_JOB);
   MCTF_ASSERT_PTR_NONNULL(job, cleanup, "persisted job payload has no job metadata");
   started_at = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_START_TIME);
   updated_at = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_UPDATED_TIME);
   finished_at = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_END_TIME);
   MCTF_ASSERT_PTR_NONNULL(started_at, cleanup, "job start timestamp is absent");
   MCTF_ASSERT_PTR_NONNULL(updated_at, cleanup, "job update timestamp is absent");
   MCTF_ASSERT_PTR_NONNULL(finished_at, cleanup, "job end timestamp is absent");
   MCTF_ASSERT_INT_EQ(strlen(started_at), 20, cleanup, "job start timestamp is not ISO 8601 UTC");
   MCTF_ASSERT_INT_EQ(strlen(updated_at), 20, cleanup, "job update timestamp is not ISO 8601 UTC");
   MCTF_ASSERT_INT_EQ(strlen(finished_at), 20, cleanup, "job end timestamp is not ISO 8601 UTC");

cleanup:
   pgmoneta_json_destroy(response);
   pgmoneta_job_cleanup(PRIMARY_SERVER);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_job_rejects_invalid_server)
{
   char id[MISC_LENGTH] = {0};

   MCTF_ASSERT_INT_EQ(pgmoneta_job_init(-1, WORKFLOW_TYPE_ARCHIVE), -1, cleanup, "negative server id was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_job_init(NUMBER_OF_SERVERS, WORKFLOW_TYPE_ARCHIVE), -1, cleanup, "out-of-range server id was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_job_gen_id(-1, MANAGEMENT_ARCHIVE, id), -1, cleanup, "negative server id generated a job id");
   MCTF_ASSERT_INT_EQ(pgmoneta_job_gen_id(NUMBER_OF_SERVERS, MANAGEMENT_ARCHIVE, id), -1, cleanup, "out-of-range server id generated a job id");
   MCTF_ASSERT(!pgmoneta_job_is_active(-1), cleanup, "negative server id was considered active");
   MCTF_ASSERT(!pgmoneta_job_is_active(NUMBER_OF_SERVERS), cleanup, "out-of-range server id was considered active");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_job_async_backup_commands)
{
   struct json* response = NULL;
   struct json* job = NULL;
   char* response_job_id = NULL;
   char* job_id = NULL;
   char* job_state = NULL;
   bool completed = false;
   int number_of_jobs = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_async_backup("primary", &response, 0), 0, cleanup, "async backup request failed");
   response_job_id = (char*)pgmoneta_json_get((struct json*)pgmoneta_json_get(response, MANAGEMENT_CATEGORY_RESPONSE), MANAGEMENT_ARGUMENT_JOB_ID);
   MCTF_ASSERT_PTR_NONNULL(response_job_id, cleanup, "async backup response has no job id");
   job_id = strdup(response_job_id);
   MCTF_ASSERT_PTR_NONNULL(job_id, cleanup, "failed to copy async backup job id");
   MCTF_ASSERT(pgmoneta_starts_with(job_id, "s0-backup-"), cleanup, "async backup returned an unexpected job id");
   pgmoneta_json_destroy(response);
   response = NULL;

   for (int attempt = 0; attempt < 60; attempt++)
   {
      MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job(job_id, &response, 0), 0, cleanup, "job lookup failed");
      job = job_metadata(response);
      MCTF_ASSERT_PTR_NONNULL(job, cleanup, "job lookup has no job metadata");
      job_state = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_STATE);
      MCTF_ASSERT_PTR_NONNULL(job_state, cleanup, "job lookup has no state");

      if (pgmoneta_compare_string(job_state, "Completed"))
      {
         completed = true;
         break;
      }

      MCTF_ASSERT(!pgmoneta_compare_string(job_state, "Failed"), cleanup, "async backup job failed");
      pgmoneta_json_destroy(response);
      response = NULL;
      sleep(1);
   }

   MCTF_ASSERT(completed, cleanup, "async backup job did not complete within 60 seconds");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_status("primary", "backup", &response, 0), 0, cleanup, "job status request failed");
   MCTF_ASSERT_PTR_NONNULL(job_metadata(response), cleanup, "job status response has no job metadata");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_all(&response, 0), 0, cleanup, "job list all request failed");
   number_of_jobs = (int)pgmoneta_json_get((struct json*)pgmoneta_json_get(response, MANAGEMENT_CATEGORY_RESPONSE), MANAGEMENT_ARGUMENT_NUMBER_OF_JOBS);
   MCTF_ASSERT(number_of_jobs > 0, cleanup, "job list all returned no jobs");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_server("primary", &response, 0), 0, cleanup, "job list server request failed");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_status("Completed", &response, 0), 0, cleanup, "job list status request failed");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_remove(job_id, 0), 0, cleanup, "job remove request failed");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job(job_id, NULL, MANAGEMENT_ERROR_JOB_NOT_FOUND), 0, cleanup, "removed job was still available");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_remove(NULL, 0), 0, cleanup, "job remove all request failed");

cleanup:
   pgmoneta_json_destroy(response);
   free(job_id);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
