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
#include <json.h>
#include <management.h>
#include <mctf.h>
#include <shmem.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>
#include <workflow.h>

/* system */
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
   MCTF_ASSERT_INT_EQ(pgmoneta_job_add_async_data(PRIMARY_SERVER, response), 0, cleanup, "failed to fill job response");
   MCTF_ASSERT(pgmoneta_job_is_active(PRIMARY_SERVER), cleanup, "running job is not active");

   id = (char*)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_JOB_ID);
   state = (char*)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_JOB_STATE);
   MCTF_ASSERT_PTR_NONNULL(id, cleanup, "job response has no id");
   MCTF_ASSERT_STR_EQ(state, "Running", cleanup, "job response has an unexpected state");

   pgmoneta_job_update_state(PRIMARY_SERVER, JOB_STATE_COMPLETED);
   MCTF_ASSERT_INT_EQ(pgmoneta_job_finish(PRIMARY_SERVER, response), 0, cleanup, "job finish failed");
   MCTF_ASSERT(!pgmoneta_job_is_active(PRIMARY_SERVER), cleanup, "finished job is still active");

   job = (struct json*)pgmoneta_json_get(response, MANAGEMENT_CATEGORY_JOB);
   MCTF_ASSERT_PTR_NONNULL(job, cleanup, "persisted job payload has no job metadata");
   started_at = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_START_TIME);
   updated_at = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_UPDATED_TIME);
   finished_at = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_END_TIME);
   MCTF_ASSERT_PTR_NONNULL(started_at, cleanup, "job start timestamp is absent");
   MCTF_ASSERT_PTR_NONNULL(updated_at, cleanup, "job update timestamp is absent");
   MCTF_ASSERT_PTR_NONNULL(finished_at, cleanup, "job end timestamp is absent");

cleanup:
   pgmoneta_json_destroy(response);
   pgmoneta_job_cleanup(PRIMARY_SERVER);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
