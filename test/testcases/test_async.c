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
#include <json.h>
#include <management.h>
#include <mctf.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <utils.h>

/* system */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASYNC_JOB_POLL_ATTEMPTS  120
#define ASYNC_JOB_POLL_DELAY_S   1
#define ASYNC_JOB_SETTLE_DELAY_S 2

static struct json*
response_payload(struct json* payload)
{
   return payload != NULL ? (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_RESPONSE) : NULL;
}

static struct json*
job_payload(struct json* payload)
{
   struct json* response = response_payload(payload);

   return response != NULL ? (struct json*)pgmoneta_json_get(response, MANAGEMENT_CATEGORY_JOB) : NULL;
}

static bool
response_has_progress(struct json* payload)
{
   struct json* response = response_payload(payload);

   return response != NULL &&
          pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_PROGRESS_STATE) &&
          pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_DONE) &&
          pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_TOTAL) &&
          pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_ELAPSED) &&
          pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_PERCENTAGE) &&
          pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_REMAINING);
}

static char*
copy_job_id_from_status(char* command)
{
   struct json* response = NULL;
   struct json* job = NULL;
   char* response_job_id = NULL;
   char* job_id = NULL;

   if (pgmoneta_tsclient_job_status("primary", command, &response, 0))
   {
      goto done;
   }

   job = job_payload(response);
   response_job_id = job != NULL ? (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_ID) : NULL;
   if (response_job_id != NULL)
   {
      job_id = pgmoneta_append(NULL, response_job_id);
   }

done:
   pgmoneta_json_destroy(response);
   return job_id;
}

static bool
job_list_contains(struct json* response, char* job_id)
{
   struct json* payload = NULL;
   struct json* jobs = NULL;
   struct json_iterator* iter = NULL;
   bool found = false;

   payload = response_payload(response);
   jobs = payload != NULL ? (struct json*)pgmoneta_json_get(payload, MANAGEMENT_ARGUMENT_JOBS) : NULL;
   if (jobs == NULL || pgmoneta_json_iterator_create(jobs, &iter))
   {
      return false;
   }

   while (pgmoneta_json_iterator_next(iter))
   {
      struct json* entry = (struct json*)iter->value->data;
      char* entry_job_id = entry != NULL ? (char*)pgmoneta_json_get(entry, MANAGEMENT_ARGUMENT_JOB_ID) : NULL;

      if (entry_job_id != NULL && pgmoneta_compare_string(entry_job_id, job_id))
      {
         found = true;
         break;
      }
   }

   pgmoneta_json_iterator_destroy(iter);
   return found;
}

static bool
wait_for_job_completion(char* job_id)
{
   struct json* response = NULL;
   struct json* job = NULL;
   char* job_state = NULL;
   bool completed = false;

   for (int attempt = 0; attempt < ASYNC_JOB_POLL_ATTEMPTS; attempt++)
   {
      if (pgmoneta_tsclient_job(job_id, &response, 0) != 0)
      {
         pgmoneta_json_destroy(response);
         response = NULL;
         sleep(ASYNC_JOB_POLL_DELAY_S);
         continue;
      }

      job = job_payload(response);
      job_state = job != NULL ? (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_STATE) : NULL;

      if (job_state != NULL && pgmoneta_compare_string(job_state, "Completed"))
      {
         completed = true;
         pgmoneta_json_destroy(response);
         response = NULL;
         sleep(ASYNC_JOB_SETTLE_DELAY_S);
         break;
      }

      if (job_state != NULL && pgmoneta_compare_string(job_state, "Failed"))
      {
         pgmoneta_json_destroy(response);
         response = NULL;
         break;
      }

      pgmoneta_json_destroy(response);
      response = NULL;
      sleep(ASYNC_JOB_POLL_DELAY_S);
   }

   pgmoneta_json_destroy(response);

   return completed;
}

MCTF_TEST(test_async_backup_job_lookup)
{
   SSL* ssl = NULL;
   int socket = -1;
   struct query_response* qr = NULL;
   struct json* response = NULL;
   struct json* job = NULL;
   char* response_job_id = NULL;
   char* job_status = NULL;
   char* job_id = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT_INT_EQ(pgmoneta_test_connect_user(&ssl, &socket), 0, cleanup, "failed to connect to database as test user");

   MCTF_ASSERT_INT_EQ(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket, "DROP TABLE IF EXISTS async_lookup_data;", &qr), 0, cleanup, "failed to drop existing lookup data table");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT_INT_EQ(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket, "CREATE TABLE async_lookup_data (id integer, payload text);", &qr), 0, cleanup, "failed to create lookup data table");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT_INT_EQ(pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket, "INSERT INTO async_lookup_data SELECT g, repeat('x', 500) FROM generate_series(1, 1000) g;", &qr), 0, cleanup, "failed to insert lookup data");
   pgmoneta_test_cleanup_query_response(&qr);

   pgmoneta_test_cleanup_connection(&ssl, &socket);

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_mode("primary", "online", 0), 0, cleanup, "failed to set tsclient mode to online");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_backup("primary", NULL, true, &response, 0), 0, cleanup, "async backup request failed");

   response_job_id = (char*)pgmoneta_json_get(response_payload(response), MANAGEMENT_ARGUMENT_JOB_ID);
   MCTF_ASSERT_PTR_NONNULL(response_job_id, cleanup, "async backup response has no job id");
   job_id = pgmoneta_append(NULL, response_job_id);
   MCTF_ASSERT_PTR_NONNULL(job_id, cleanup, "failed to copy async backup job id");
   MCTF_ASSERT(pgmoneta_starts_with(job_id, "s0-backup-"), cleanup, "async backup returned an unexpected job id");

   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job(job_id, &response, 0), 0, cleanup, "job lookup by id failed");
   job = job_payload(response);
   MCTF_ASSERT_PTR_NONNULL(job, cleanup, "job lookup response has no job category");
   response_job_id = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_ID);
   MCTF_ASSERT_PTR_NONNULL(response_job_id, cleanup, "job lookup response has no job id");
   MCTF_ASSERT(pgmoneta_compare_string(response_job_id, job_id), cleanup, "job lookup response job id does not match original job id");

   job_status = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_STATE);
   MCTF_ASSERT_PTR_NONNULL(job_status, cleanup, "job lookup response has no state");
   MCTF_ASSERT(pgmoneta_compare_string(job_status, "Running") || pgmoneta_compare_string(job_status, "Completed"), cleanup, "job lookup response has unexpected state");
   if (pgmoneta_compare_string(job_status, "Running"))
   {
      MCTF_ASSERT(response_has_progress(response), cleanup, "running job lookup response has no progress data");
   }
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT(wait_for_job_completion(job_id), cleanup, "async backup job did not complete within the retry budget");

cleanup:
   pgmoneta_json_destroy(response);
   if (job_id != NULL)
   {
      pgmoneta_tsclient_job_remove_job(job_id, NULL, 0);
   }
   free(job_id);
   pgmoneta_test_cleanup_connection(&ssl, &socket);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

static int
prepare_async_data(void)
{
   SSL* ssl = NULL;
   int socket = -1;
   struct query_response* qr = NULL;
   int ret = 1;

   if (pgmoneta_test_connect_user(&ssl, &socket))
   {
      goto done;
   }
   if (pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket, "DROP TABLE IF EXISTS async_workflow_data;", &qr))
   {
      goto done;
   }
   pgmoneta_test_cleanup_query_response(&qr);
   if (pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket, "CREATE TABLE async_workflow_data (id integer, payload text);", &qr))
   {
      goto done;
   }
   pgmoneta_test_cleanup_query_response(&qr);
   if (pgmoneta_test_execute_query(PRIMARY_SERVER, ssl, socket, "INSERT INTO async_workflow_data SELECT g, repeat(md5(g::text), 16) FROM generate_series(1, 1000) g;", &qr))
   {
      goto done;
   }
   pgmoneta_test_cleanup_query_response(&qr);
   ret = 0;

done:
   pgmoneta_test_cleanup_query_response(&qr);
   pgmoneta_test_cleanup_connection(&ssl, &socket);
   return ret;
}

static int
run_async_backup(char* incremental, char** job_id)
{
   struct json* response = NULL;
   char* response_job_id = NULL;
   int ret = 1;

   if (pgmoneta_tsclient_backup("primary", incremental, true, &response, 0))
   {
      goto done;
   }
   response_job_id = (char*)pgmoneta_json_get(response_payload(response), MANAGEMENT_ARGUMENT_JOB_ID);
   if (response_job_id == NULL)
   {
      goto done;
   }
   *job_id = pgmoneta_append(NULL, response_job_id);
   if (*job_id == NULL || !wait_for_job_completion(*job_id))
   {
      goto done;
   }
   ret = 0;

done:
   pgmoneta_json_destroy(response);
   return ret;
}

static int
start_async_backup(char* incremental, char** job_id)
{
   struct json* response = NULL;
   struct json* response_category = NULL;
   char* response_job_id = NULL;
   int ret = 1;

   if (job_id == NULL || pgmoneta_tsclient_backup("primary", incremental, true, &response, 0))
   {
      goto done;
   }

   response_category = response_payload(response);
   response_job_id = response_category != NULL ? (char*)pgmoneta_json_get(response_category, MANAGEMENT_ARGUMENT_JOB_ID) : NULL;
   if (response_job_id == NULL)
   {
      goto done;
   }

   *job_id = pgmoneta_append(NULL, response_job_id);
   if (*job_id == NULL)
   {
      goto done;
   }

   ret = 0;

done:
   pgmoneta_json_destroy(response);
   return ret;
}

static int
create_async_backup_chain(void)
{
   char* job_id = NULL;
   int ret = 1;

   if (prepare_async_data() || pgmoneta_tsclient_mode("primary", "online", 0) ||
       run_async_backup(NULL, &job_id))
   {
      goto done;
   }
   free(job_id);
   job_id = NULL;

   if (run_async_backup("newest", &job_id))
   {
      goto done;
   }
   free(job_id);
   job_id = NULL;

   ret = 0;

done:
   free(job_id);
   return ret;
}

static void
cleanup_async_test(void)
{
   pgmoneta_tsclient_job_remove_all(NULL, 0);
   pgmoneta_test_basedir_cleanup();
}

MCTF_TEST_NEGATIVE(test_async_job_protocol_validation)
{
   pgmoneta_test_setup();

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_raw(MANAGEMENT_JOB_ACTION_UNKNOWN, NULL, NULL, NULL, NULL, false, false,
                                                MANAGEMENT_ERROR_JOB_ACTION_INVALID),
                      0, cleanup, "job request without an action was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_raw(99, NULL, NULL, NULL, NULL, false, false,
                                                MANAGEMENT_ERROR_JOB_ACTION_INVALID),
                      0, cleanup, "job request with an invalid action was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_raw(MANAGEMENT_JOB_ACTION_REMOVE, NULL, NULL, NULL, NULL, false, false,
                                                MANAGEMENT_ERROR_JOB_REQUEST_INVALID),
                      0, cleanup, "remove request without an explicit target was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_raw(MANAGEMENT_JOB_ACTION_LIST, NULL, NULL, NULL, NULL, false, false,
                                                MANAGEMENT_ERROR_JOB_REQUEST_INVALID),
                      0, cleanup, "list request without an explicit selector was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_raw(MANAGEMENT_JOB_ACTION_LIST, NULL, "primary", NULL, "Running", false, false,
                                                MANAGEMENT_ERROR_JOB_REQUEST_INVALID),
                      0, cleanup, "list request with multiple selectors was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_raw(MANAGEMENT_JOB_ACTION_LIST, NULL, NULL, NULL, NULL, true, false,
                                                MANAGEMENT_ERROR_JOB_REQUEST_INVALID),
                      0, cleanup, "list request with All=false was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_raw(MANAGEMENT_JOB_ACTION_STATUS, NULL, "primary", "invalid", NULL, false, false,
                                                MANAGEMENT_ERROR_JOB_REQUEST_INVALID),
                      0, cleanup, "status request with an invalid command was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_raw(MANAGEMENT_JOB_ACTION_GET, "s999-backup-20260101000000", NULL, NULL, NULL, false, false,
                                                MANAGEMENT_ERROR_JOB_ERROR),
                      0, cleanup, "job id with an out-of-range server was accepted");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_raw(MANAGEMENT_JOB_ACTION_GET, "s0-backup-20260101000000-invalid", NULL, NULL, NULL, false, false,
                                                MANAGEMENT_ERROR_JOB_ERROR),
                      0, cleanup, "job id with trailing data was accepted");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_async_backup_chain)
{
   struct json* response = NULL;
   struct json* full = NULL;
   struct json* incremental_1 = NULL;

   pgmoneta_test_setup();
   MCTF_ASSERT_INT_EQ(create_async_backup_chain(), 0, cleanup, "failed to create async backup chain");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), 0, cleanup, "failed to list async backup chain");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 2, cleanup, "expected one full and one incremental backup");
   full = pgmoneta_tsclient_get_backup(response, 0);
   incremental_1 = pgmoneta_tsclient_get_backup(response, 1);
   MCTF_ASSERT_PTR_NONNULL(full, cleanup, "full backup is missing");
   MCTF_ASSERT_PTR_NONNULL(incremental_1, cleanup, "first incremental backup is missing");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(full), "FULL", cleanup, "first backup is not full");
   MCTF_ASSERT_STR_EQ(pgmoneta_tsclient_get_backup_type(incremental_1), "INCREMENTAL", cleanup, "second backup is not incremental");
   MCTF_ASSERT(pgmoneta_tsclient_verify_backup_chain(full, incremental_1), cleanup, "first incremental has the wrong parent");

cleanup:
   pgmoneta_json_destroy(response);
   cleanup_async_test();
   MCTF_FINISH();
}

MCTF_TEST(test_async_archive)
{
   char* job_id = NULL;
   char archive_path[MAX_PATH] = {0};

   pgmoneta_test_setup();
   MCTF_ASSERT_INT_EQ(create_async_backup_chain(), 0, cleanup, "failed to create backup chain for archive");
   pgmoneta_snprintf(archive_path, sizeof(archive_path), "%s/async_archive", TEST_BASE_DIR);
   pgmoneta_mkdir(archive_path);
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_archive("primary", "newest", NULL, archive_path, true, 0), 0, cleanup, "async archive request failed");
   job_id = copy_job_id_from_status("archive");
   MCTF_ASSERT_PTR_NONNULL(job_id, cleanup, "failed to obtain archive job id");
   MCTF_ASSERT(wait_for_job_completion(job_id), cleanup, "async archive did not complete");

cleanup:
   free(job_id);
   if (archive_path[0] != '\0')
   {
      pgmoneta_delete_directory(archive_path);
   }
   cleanup_async_test();
   MCTF_FINISH();
}

MCTF_TEST(test_async_restore)
{
   char* job_id = NULL;

   pgmoneta_test_setup();
   MCTF_ASSERT_INT_EQ(create_async_backup_chain(), 0, cleanup, "failed to create backup chain for restore");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_restore("primary", "newest", "current", true, 0), 0, cleanup, "async restore request failed");
   job_id = copy_job_id_from_status("restore");
   MCTF_ASSERT_PTR_NONNULL(job_id, cleanup, "failed to obtain restore job id");
   MCTF_ASSERT(wait_for_job_completion(job_id), cleanup, "async restore did not complete");

cleanup:
   free(job_id);
   cleanup_async_test();
   MCTF_FINISH();
}

MCTF_TEST(test_async_delete)
{
   struct json* response = NULL;
   char* job_id = NULL;

   pgmoneta_test_setup();
   MCTF_ASSERT_INT_EQ(create_async_backup_chain(), 0, cleanup, "failed to create backup chain for delete");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_delete("primary", "newest", true, 0), 0, cleanup, "async delete request failed");
   job_id = copy_job_id_from_status("delete");
   MCTF_ASSERT_PTR_NONNULL(job_id, cleanup, "failed to obtain delete job id");
   MCTF_ASSERT(wait_for_job_completion(job_id), cleanup, "async delete did not complete");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_list_backup("primary", NULL, &response, 0), 0, cleanup, "failed to list backups after delete");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_get_backup_count(response), 1, cleanup, "newest incremental backup was not deleted");

cleanup:
   pgmoneta_json_destroy(response);
   free(job_id);
   cleanup_async_test();
   MCTF_FINISH();
}

MCTF_TEST(test_async_remove_job_from_lists)
{
   struct json* response = NULL;
   char* job_id = NULL;

   pgmoneta_test_setup();
   MCTF_ASSERT_INT_EQ(prepare_async_data(), 0, cleanup, "failed to prepare data for job removal");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_mode("primary", "online", 0), 0, cleanup, "failed to set primary online");
   MCTF_ASSERT_INT_EQ(run_async_backup(NULL, &job_id), 0, cleanup, "async backup did not complete");

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_all(&response, 0), 0, cleanup, "list all failed before removal");
   MCTF_ASSERT(job_list_contains(response, job_id), cleanup, "job missing from all jobs before removal");
   pgmoneta_json_destroy(response);
   response = NULL;
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_server("primary", &response, 0), 0, cleanup, "list server failed before removal");
   MCTF_ASSERT(job_list_contains(response, job_id), cleanup, "job missing from server jobs before removal");
   pgmoneta_json_destroy(response);
   response = NULL;
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_status("Completed", &response, 0), 0, cleanup, "list completed failed before removal");
   MCTF_ASSERT(job_list_contains(response, job_id), cleanup, "job missing from completed jobs before removal");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_remove_job(job_id, NULL, 0), 0, cleanup, "failed to remove persisted job");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_all(&response, 0), 0, cleanup, "list all failed after removal");
   MCTF_ASSERT(!job_list_contains(response, job_id), cleanup, "removed job remains in all jobs");
   pgmoneta_json_destroy(response);
   response = NULL;
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_server("primary", &response, 0), 0, cleanup, "list server failed after removal");
   MCTF_ASSERT(!job_list_contains(response, job_id), cleanup, "removed job remains in server jobs");
   pgmoneta_json_destroy(response);
   response = NULL;
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_status("Completed", &response, 0), 0, cleanup, "list completed failed after removal");
   MCTF_ASSERT(!job_list_contains(response, job_id), cleanup, "removed job remains in completed jobs");

cleanup:
   pgmoneta_json_destroy(response);
   free(job_id);
   cleanup_async_test();
   MCTF_FINISH();
}

MCTF_TEST(test_async_job_lists_running_and_completed)
{
   struct json* response = NULL;
   char* job_id = NULL;

   pgmoneta_test_setup();
   MCTF_ASSERT_INT_EQ(prepare_async_data(), 0, cleanup, "failed to prepare data for job list test");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_mode("primary", "online", 0), 0, cleanup, "failed to set primary online");
   MCTF_ASSERT_INT_EQ(start_async_backup(NULL, &job_id), 0, cleanup, "failed to start async backup");

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_all(&response, 0), 0, cleanup, "failed to list all jobs while backup was running");
   MCTF_ASSERT(job_list_contains(response, job_id), cleanup, "running backup is missing from all jobs");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_status("Running", &response, 0), 0, cleanup, "failed to list running jobs");
   MCTF_ASSERT(job_list_contains(response, job_id), cleanup, "backup is missing from running jobs");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_server("primary", &response, 0), 0, cleanup, "failed to list primary server jobs");
   MCTF_ASSERT(job_list_contains(response, job_id), cleanup, "backup is missing from primary server jobs");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT(wait_for_job_completion(job_id), cleanup, "async backup did not complete");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_list_status("Completed", &response, 0), 0, cleanup, "failed to list completed jobs");
   MCTF_ASSERT(job_list_contains(response, job_id), cleanup, "backup is missing from completed jobs");

cleanup:
   pgmoneta_json_destroy(response);
   if (job_id != NULL)
   {
      wait_for_job_completion(job_id);
   }
   free(job_id);
   cleanup_async_test();
   MCTF_FINISH();
}

MCTF_TEST(test_async_job_status_running_then_latest)
{
   struct json* response = NULL;
   struct json* job = NULL;
   char* status_job_id = NULL;
   char* job_state = NULL;
   char* previous_job_id = NULL;
   char* job_id = NULL;

   pgmoneta_test_setup();
   MCTF_ASSERT_INT_EQ(prepare_async_data(), 0, cleanup, "failed to prepare data for job status test");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_mode("primary", "online", 0), 0, cleanup, "failed to set primary online");
   MCTF_ASSERT_INT_EQ(run_async_backup(NULL, &previous_job_id), 0, cleanup, "failed to create previous completed backup job");
   MCTF_ASSERT_INT_EQ(start_async_backup("newest", &job_id), 0, cleanup, "failed to start async incremental backup");
   MCTF_ASSERT(!pgmoneta_compare_string(previous_job_id, job_id), cleanup, "current backup reused the previous job id");

   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_status("primary", "backup", &response, 0), 0, cleanup, "failed to query current backup job");
   job = job_payload(response);
   MCTF_ASSERT_PTR_NONNULL(job, cleanup, "current backup status has no job metadata");
   status_job_id = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_ID);
   job_state = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_STATE);
   MCTF_ASSERT_PTR_NONNULL(status_job_id, cleanup, "current backup status has no job id");
   MCTF_ASSERT_PTR_NONNULL(job_state, cleanup, "current backup status has no state");
   MCTF_ASSERT_STR_EQ(status_job_id, job_id, cleanup, "current backup status returned a different job");
   MCTF_ASSERT_STR_EQ(job_state, "Running", cleanup, "current backup status did not return a running job");
   MCTF_ASSERT(response_has_progress(response), cleanup, "running job status response has no progress data");
   pgmoneta_json_destroy(response);
   response = NULL;

   MCTF_ASSERT(wait_for_job_completion(job_id), cleanup, "async backup did not complete");
   MCTF_ASSERT_INT_EQ(pgmoneta_tsclient_job_status("primary", "backup", &response, 0), 0, cleanup, "failed to query latest completed backup job");
   job = job_payload(response);
   MCTF_ASSERT_PTR_NONNULL(job, cleanup, "latest backup status has no job metadata");
   status_job_id = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_ID);
   job_state = (char*)pgmoneta_json_get(job, MANAGEMENT_ARGUMENT_JOB_STATE);
   MCTF_ASSERT_PTR_NONNULL(status_job_id, cleanup, "latest backup status has no job id");
   MCTF_ASSERT_PTR_NONNULL(job_state, cleanup, "latest backup status has no state");
   MCTF_ASSERT_STR_EQ(status_job_id, job_id, cleanup, "latest backup status did not return the completed job");
   MCTF_ASSERT_STR_EQ(job_state, "Completed", cleanup, "latest backup status did not return a completed job");

cleanup:
   pgmoneta_json_destroy(response);
   if (job_id != NULL)
   {
      wait_for_job_completion(job_id);
   }
   free(previous_job_id);
   free(job_id);
   cleanup_async_test();
   MCTF_FINISH();
}
