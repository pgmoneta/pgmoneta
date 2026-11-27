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
#include <configuration.h>
#include <json.h>
#include <management.h>
#include <mctf.h>
#include <shmem.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

MCTF_TEST(test_configuration_accept_time)
{
   pgmoneta_test_setup();

   // Zero / disabled
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "0", 0) == 0, cleanup,
               "conf set 0 failed");

   // Seconds
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10s", 0) == 0, cleanup,
               "conf set 10s failed");

   // Minutes
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "2m", 0) == 0, cleanup,
               "conf set 2m failed");

   // Hours
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h", 0) == 0, cleanup,
               "conf set 1h failed");

   // Days
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1d", 0) == 0, cleanup,
               "conf set 1d failed");

   // Weeks
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1w", 0) == 0, cleanup,
               "conf set 1w failed");

   // Uppercase suffix
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, "1S", 0) == 0, cleanup,
               "conf set 1S failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, "2M", 0) == 0, cleanup,
               "conf set 2M failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1H", 0) == 0, cleanup,
               "conf set 1H failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1D", 0) == 0, cleanup,
               "conf set 1D failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_configuration_reject_invalid_time)
{
   pgmoneta_test_setup();

   // Invalid suffix
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10x", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set 10x should fail");

   // Negative value
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "-1s", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set -1s should fail");

   // Mixed units
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h5s", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set 1h5s should fail");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "1h 5s", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set 1h 5s should fail");

   // Space between number and unit
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "10 s", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set 10 s should fail");

   // Non-numeric
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "abc", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set abc should fail");

   // authentication_timeout must be at least 1 second
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_AUTHENTICATION_TIMEOUT, "0", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set authentication_timeout 0 should fail");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_get_returns_set_values)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_AUTHENTICATION_TIMEOUT, "17s", 0) == 0, cleanup,
               "conf set authentication_timeout 17s failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "45s", 0) == 0, cleanup,
               "conf set blocking_timeout 45s failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "2m", 0) == 0, cleanup,
               "conf set metrics_cache_max_age 2m failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, "30s", 0) == 0, cleanup,
               "conf set log_rotation_age 30s failed");

   MCTF_ASSERT(pgmoneta_tsclient_conf_get(CONFIGURATION_ARGUMENT_AUTHENTICATION_TIMEOUT, 0) == 0, cleanup,
               "conf get authentication_timeout failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_get(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, 0) == 0, cleanup,
               "conf get blocking_timeout failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_get(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, 0) == 0, cleanup,
               "conf get metrics_cache_max_age failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_get(CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, 0) == 0, cleanup,
               "conf get log_rotation_age failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_retention_parse)
{
   struct main_configuration* config = NULL;
   bool shmem_allocated = false;
   struct
   {
      char* name;
      int retention[4]; /* days, weeks, months, years; -1 is unset */
   } expected[] = {
      {"valid", {7, 4, 3, 1}},
      {"blank_months", {7, 4, -1, 1}},
      {"blank_weeks", {7, -1, 3, 1}},
      {"blank_years", {7, 4, 3, -1}},
      {"placeholders", {7, -1, -1, 1}},
      {"empty_field", {7, 4, -1, 1}},
      {"zero", {7, -1, 3, 1}},
      {"negative", {-1, -1, -1, -1}},
      {"non_numeric", {-1, -1, -1, -1}},
      {"extra_field", {-1, -1, -1, -1}},
      {"extra_field_whitespace", {-1, -1, -1, -1}},
      {"trailing_comma", {-1, -1, -1, -1}},
   };
   int number_of_expected = sizeof(expected) / sizeof(expected[0]);

   if (shmem == NULL)
   {
      MCTF_ASSERT_INT_EQ(pgmoneta_create_shared_memory(sizeof(struct main_configuration), HUGEPAGE_OFF, &shmem), 0,
                         cleanup, "failed to create shared memory");
      shmem_allocated = true;
   }

   pgmoneta_test_config_save();

   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/retention/01.conf"), 0,
                      cleanup, "failed to read retention/01.conf");

   config = (struct main_configuration*)shmem;

   // The documented default
   MCTF_ASSERT_INT_EQ(config->retention_days, 7, cleanup, "global: days should be 7");
   MCTF_ASSERT_INT_EQ(config->retention_weeks, -1, cleanup, "global: weeks should be unset");
   MCTF_ASSERT_INT_EQ(config->retention_months, -1, cleanup, "global: months should be unset");
   MCTF_ASSERT_INT_EQ(config->retention_years, -1, cleanup, "global: years should be unset");

   MCTF_ASSERT_INT_EQ(config->common.number_of_servers, number_of_expected,
                      cleanup, "expected %d servers, got %d", number_of_expected, config->common.number_of_servers);

   for (int i = 0; i < number_of_expected; i++)
   {
      struct server* srv = &config->common.servers[i];

      MCTF_ASSERT_STR_EQ(srv->name, expected[i].name, cleanup, "server %d should be %s", i, expected[i].name);
      MCTF_ASSERT_INT_EQ(srv->retention_days, expected[i].retention[0],
                         cleanup, "%s: days %d, expected %d", srv->name, srv->retention_days, expected[i].retention[0]);
      MCTF_ASSERT_INT_EQ(srv->retention_weeks, expected[i].retention[1],
                         cleanup, "%s: weeks %d, expected %d", srv->name, srv->retention_weeks, expected[i].retention[1]);
      MCTF_ASSERT_INT_EQ(srv->retention_months, expected[i].retention[2],
                         cleanup, "%s: months %d, expected %d", srv->name, srv->retention_months, expected[i].retention[2]);
      MCTF_ASSERT_INT_EQ(srv->retention_years, expected[i].retention[3],
                         cleanup, "%s: years %d, expected %d", srv->name, srv->retention_years, expected[i].retention[3]);
   }

cleanup:
   pgmoneta_test_config_restore();
   if (shmem_allocated)
   {
      pgmoneta_destroy_shared_memory(shmem, sizeof(struct main_configuration));
      shmem = NULL;
   }
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_retention_accept)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_RETENTION, "7, 4, 3, 1", 0) == 0, cleanup,
               "conf set retention 7, 4, 3, 1 failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_RETENTION, "7, 4, , 1", 0) == 0, cleanup,
               "conf set retention 7, 4, , 1 failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_RETENTION, "7,4,,1", 0) == 0, cleanup,
               "conf set retention 7,4,,1 failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_RETENTION, "7, 4, X, 1", 0) == 0, cleanup,
               "conf set retention 7, 4, X, 1 failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_RETENTION, "7, 0, 3, 1", 0) == 0, cleanup,
               "conf set retention 7, 0, 3, 1 failed");

   // The documented default, last so the server ends up with the same retention as the test configuration
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_RETENTION, "7, - , - , -", 0) == 0, cleanup,
               "conf set retention 7, - , - , - failed");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST_NEGATIVE(test_configuration_retention_reject)
{
   pgmoneta_test_setup();

   // Negative value
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_RETENTION, "7, 4, -3, 1", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set retention 7, 4, -3, 1 should fail");

   // Non-numeric
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_RETENTION, "7, abc, 3, 1", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set retention 7, abc, 3, 1 should fail");

   // More than four fields
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_RETENTION, "7, 4, 3, 1, 9", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set retention 7, 4, 3, 1, 9 should fail");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_RETENTION, "7 , 4 , 3 , 1 , 9 ", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set retention 7 , 4 , 3 , 1 , 9 should fail");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_RETENTION, "7, 4, 3, 1,", MANAGEMENT_ERROR_CONF_SET_ERROR) == 0, cleanup,
               "conf set retention 7, 4, 3, 1, should fail");

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_time_format_output)
{
   pgmoneta_time_t t;
   char* str = NULL;
   int ret;

   // Seconds
   t = PGMONETA_TIME_SEC(10);
   ret = pgmoneta_time_format(t, FORMAT_TIME_S, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format seconds failed");
   MCTF_ASSERT_STR_EQ(str, "10s", cleanup, "format seconds mismatch");
   free(str);
   str = NULL;

   // Minutes
   t = PGMONETA_TIME_MIN(5);
   ret = pgmoneta_time_format(t, FORMAT_TIME_MIN, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format minutes failed");
   MCTF_ASSERT_STR_EQ(str, "5m", cleanup, "format minutes mismatch");
   free(str);
   str = NULL;

   // Hours
   t = PGMONETA_TIME_HOUR(2);
   ret = pgmoneta_time_format(t, FORMAT_TIME_HOUR, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format hours failed");
   MCTF_ASSERT_STR_EQ(str, "2h", cleanup, "format hours mismatch");
   free(str);
   str = NULL;

   // Days
   t = PGMONETA_TIME_DAY(1);
   ret = pgmoneta_time_format(t, FORMAT_TIME_DAY, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format days failed");
   MCTF_ASSERT_STR_EQ(str, "1d", cleanup, "format days mismatch");
   free(str);
   str = NULL;

   // Timestamp (epoch 0)
   t = PGMONETA_TIME_SEC(0);
   ret = pgmoneta_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format timestamp failed");
   MCTF_ASSERT_STR_EQ(str, "1970-01-01T00:00:00Z", cleanup, "format timestamp epoch mismatch");
   free(str);
   str = NULL;

   // Timestamp (1 second)
   t = PGMONETA_TIME_SEC(1);
   ret = pgmoneta_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format timestamp 1s failed");
   MCTF_ASSERT_STR_EQ(str, "1970-01-01T00:00:01Z", cleanup, "format timestamp 1s mismatch");
   free(str);
   str = NULL;

   // Timestamp (90 seconds)
   t = PGMONETA_TIME_SEC(90);
   ret = pgmoneta_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format timestamp 90s failed");
   MCTF_ASSERT_STR_EQ(str, "1970-01-01T00:01:30Z", cleanup, "format timestamp 90s mismatch");
   free(str);
   str = NULL;

   // Timestamp for year 2000
   t = PGMONETA_TIME_SEC(946684800LL);
   ret = pgmoneta_time_format(t, FORMAT_TIME_TIMESTAMP, &str);
   MCTF_ASSERT_INT_EQ(ret, 0, cleanup, "time_format timestamp y2k failed");
   MCTF_ASSERT_STR_EQ(str, "2000-01-01T00:00:00Z", cleanup, "format timestamp y2k mismatch");
   free(str);
   str = NULL;

   // NULL output should return error
   ret = pgmoneta_time_format(t, FORMAT_TIME_S, NULL);
   MCTF_ASSERT_INT_EQ(ret, 1, cleanup, "time_format NULL output should return 1");

cleanup:
   if (str != NULL)
   {
      free(str);
      str = NULL;
   }
   MCTF_FINISH();
}

static int
test_enum_to_str(char* where, int value)
{
   if (!where)
   {
      return 1;
   }

   switch (value)
   {
      case 0:
         snprintf(where, MISC_LENGTH, "%s", "off");
         break;
      case 1:
         snprintf(where, MISC_LENGTH, "%s", "foreground");
         break;
      case 2:
         snprintf(where, MISC_LENGTH, "%s", "background");
         break;
      default:
         return 1;
   }

   return 0;
}

MCTF_TEST(test_configuration_json_put_enum_value)
{
   struct json* res = NULL;
   struct json* nested = NULL;

   pgmoneta_json_create(&res);

   pgmoneta_json_put_enum_value(res, "validation", 1, test_enum_to_str);

   nested = (struct json*)pgmoneta_json_get(res, "validation");
   MCTF_ASSERT_PTR_NONNULL(nested, cleanup, "nested object should exist");
   MCTF_ASSERT(pgmoneta_json_contains_key(nested, "value"), cleanup, "should contain 'value' key");
   MCTF_ASSERT(pgmoneta_json_contains_key(nested, "string_value"), cleanup, "should contain 'string_value' key");
   MCTF_ASSERT_INT_EQ((int)pgmoneta_json_get(nested, "value"), 1, cleanup, "value should be 1");
   MCTF_ASSERT_STR_EQ((char*)pgmoneta_json_get(nested, "string_value"), "foreground", cleanup, "string_value should be 'foreground'");

cleanup:
   pgmoneta_json_destroy(res);
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_json_put_time_value)
{
   struct json* res = NULL;
   struct json* nested = NULL;

   pgmoneta_json_create(&res);

   // Seconds
   pgmoneta_json_put_time_value(res, "idle_timeout", PGMONETA_TIME_SEC(30), FORMAT_TIME_S);

   nested = (struct json*)pgmoneta_json_get(res, "idle_timeout");
   MCTF_ASSERT_PTR_NONNULL(nested, cleanup, "seconds: nested object should exist");

   MCTF_ASSERT(pgmoneta_json_contains_key(nested, "value"), cleanup, "seconds: should contain 'value' key");
   MCTF_ASSERT(pgmoneta_json_contains_key(nested, "string_value"), cleanup, "seconds: should contain 'string_value' key");

   MCTF_ASSERT_INT_EQ((int)pgmoneta_json_get(nested, "value"), 30, cleanup, "seconds: value should be 30");
   MCTF_ASSERT_STR_EQ((char*)pgmoneta_json_get(nested, "string_value"), "30s", cleanup, "seconds: string_value should be '30s'");

   pgmoneta_json_put_time_value(res, "authentication_timeout", PGMONETA_TIME_SEC(17), FORMAT_TIME_S);

   nested = (struct json*)pgmoneta_json_get(res, "authentication_timeout");
   MCTF_ASSERT_PTR_NONNULL(nested, cleanup, "authentication: nested object should exist");

   MCTF_ASSERT(pgmoneta_json_contains_key(nested, "value"), cleanup, "authentication: should contain 'value' key");
   MCTF_ASSERT(pgmoneta_json_contains_key(nested, "string_value"), cleanup, "authentication: should contain 'string_value' key");

   MCTF_ASSERT_INT_EQ((int)pgmoneta_json_get(nested, "value"), 17, cleanup, "authentication: value should be 17");
   MCTF_ASSERT_STR_EQ((char*)pgmoneta_json_get(nested, "string_value"), "17s", cleanup, "authentication: string_value should be '17s'");

   // Minutes
   pgmoneta_json_put_time_value(res, "blocking_timeout", PGMONETA_TIME_MIN(5), FORMAT_TIME_MIN);

   nested = (struct json*)pgmoneta_json_get(res, "blocking_timeout");
   MCTF_ASSERT_PTR_NONNULL(nested, cleanup, "minutes: nested object should exist");

   MCTF_ASSERT(pgmoneta_json_contains_key(nested, "value"), cleanup, "minutes: should contain 'value' key");
   MCTF_ASSERT(pgmoneta_json_contains_key(nested, "string_value"), cleanup, "minutes: should contain 'string_value' key");

   MCTF_ASSERT_INT_EQ((int)pgmoneta_json_get(nested, "value"), 5, cleanup, "minutes: value should be 5");
   MCTF_ASSERT_STR_EQ((char*)pgmoneta_json_get(nested, "string_value"), "5m", cleanup, "minutes: string_value should be '5m'");

cleanup:
   pgmoneta_json_destroy(res);
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_json_put_size_value)
{
   struct json* res = NULL;
   struct json* nested = NULL;

   pgmoneta_json_create(&res);

   pgmoneta_json_put_size_value(res, "buffer_size", 4096);

   nested = (struct json*)pgmoneta_json_get(res, "buffer_size");
   MCTF_ASSERT_PTR_NONNULL(nested, cleanup, "nested object should exist");
   MCTF_ASSERT(pgmoneta_json_contains_key(nested, "value"), cleanup, "should contain 'value' key");
   MCTF_ASSERT(pgmoneta_json_contains_key(nested, "string_value"), cleanup, "should contain 'string_value' key");
   MCTF_ASSERT_INT_EQ((int)pgmoneta_json_get(nested, "value"), 4096, cleanup, "value should be 4096");
   MCTF_ASSERT_STR_EQ((char*)pgmoneta_json_get(nested, "string_value"), "4096B", cleanup, "string_value should be '4096B'");

cleanup:
   pgmoneta_json_destroy(res);
   MCTF_FINISH();
}

/**
 * Read a configuration that sets log_rotation_size and metrics_cache_max_size
 * to the same value, and count the "Unknown:" warnings the reader prints for
 * a rejected value.
 */
static int
read_byte_size(char* value, int* log_rotation_size, int* metrics_cache_max_size, int* warnings)
{
   char conf_path[] = "/tmp/pgmoneta_test_byte_size_XXXXXX";
   char err_path[] = "/tmp/pgmoneta_test_byte_size_err_XXXXXX";
   char line[MISC_LENGTH * 4];
   struct main_configuration* config = NULL;
   FILE* f = NULL;
   int conf_fd = -1;
   int err_fd = -1;
   int saved_stderr = -1;
   int ret = 1;

   *warnings = 0;

   conf_fd = mkstemp(conf_path);
   err_fd = mkstemp(err_path);
   if (conf_fd == -1 || err_fd == -1)
   {
      goto done;
   }

   f = fdopen(conf_fd, "w");
   if (f == NULL)
   {
      goto done;
   }
   conf_fd = -1;
   fprintf(f, "[pgmoneta]\nhost = localhost\nbase_dir = /tmp\n"
              "log_rotation_size = %s\nmetrics_cache_max_size = %s\n\n"
              "[primary]\nhost = localhost\nport = 5432\nuser = repl\n",
           value, value);
   fclose(f);
   f = NULL;

   fflush(stderr);
   saved_stderr = dup(STDERR_FILENO);
   if (saved_stderr == -1 || dup2(err_fd, STDERR_FILENO) == -1)
   {
      goto done;
   }
   ret = pgmoneta_test_load_conf(conf_path);
   fflush(stderr);
   dup2(saved_stderr, STDERR_FILENO);

   config = (struct main_configuration*)shmem;
   *log_rotation_size = config->common.log_rotation_size;
   *metrics_cache_max_size = config->metrics_cache_max_size;

   f = fopen(err_path, "r");
   while (f != NULL && fgets(line, sizeof(line), f) != NULL)
   {
      if (strstr(line, "Unknown:") != NULL)
      {
         (*warnings)++;
      }
   }

done:
   if (f != NULL)
   {
      fclose(f);
   }
   if (saved_stderr != -1)
   {
      close(saved_stderr);
   }
   if (conf_fd != -1)
   {
      close(conf_fd);
   }
   if (err_fd != -1)
   {
      close(err_fd);
   }
   unlink(conf_path);
   unlink(err_path);

   return ret;
}

MCTF_TEST(test_configuration_byte_size_accept)
{
   struct
   {
      char* value;
      int bytes;
   } cases[] = {
      {"0", 0},
      {"0B", 0},
      {"1", 1},
      {"1B", 1},
      {"1b", 1},
      {"1K", 1024},
      {"1k", 1024},
      {"1KB", 1024},
      {"1kb", 1024},
      {"256k", 262144},
      {"1M", 1048576},
      {"1MB", 1048576},
      {"1mb", 1048576},
      {"1G", 1073741824},
      {"1GB", 1073741824},
      {"1gb", 1073741824},
      {"010K", 10240},
      {"2047M", 2146435072},
      {"2097151K", 2147482624},
      {"2147483647", 2147483647},
      {"2147483647B", 2147483647},
   };
   int log_rotation_size = -1;
   int metrics_cache_max_size = -1;
   int warnings = -1;

   pgmoneta_test_config_save();

   for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
   {
      MCTF_ASSERT_INT_EQ(read_byte_size(cases[i].value, &log_rotation_size, &metrics_cache_max_size, &warnings), 0,
                         cleanup, "'%s': failed to read the configuration", cases[i].value);
      MCTF_ASSERT_INT_EQ(log_rotation_size, cases[i].bytes, cleanup,
                         "'%s': log_rotation_size is %d, expected %d", cases[i].value, log_rotation_size, cases[i].bytes);
      MCTF_ASSERT_INT_EQ(metrics_cache_max_size, cases[i].bytes, cleanup,
                         "'%s': metrics_cache_max_size is %d, expected %d", cases[i].value, metrics_cache_max_size, cases[i].bytes);
      MCTF_ASSERT_INT_EQ(warnings, 0, cleanup, "'%s': accepted value should not be reported", cases[i].value);
   }

cleanup:
   pgmoneta_test_config_restore();
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_byte_size_reject)
{
   char* cases[] = {
      // unknown or malformed suffixes
      "1T",
      "2T",
      "5X",
      "1Z",
      "1Q",
      "1XYZ",
      "1TG",
      "1K2",
      "1BB",
      "1KBB",
      "1KBX",
      "1MBX",
      "1GBX",
      "10GBX",
      "1.5G",
      "1 K",
      "0x10",
      // no digits, or a sign
      "K",
      "B",
      "KB",
      "-1",
      "-1K",
      "+1K",
      // larger than an int
      "2048M",
      "2G",
      "3G",
      "4G",
      "5G",
      "2097152K",
      "2147483648",
      "2147483648B",
      "4294967295B",
      "4294967296B",
      "4294967297B",
      "9223372036854775807",
      "99999999999999999999999999999999",
   };
   int log_rotation_size = -1;
   int metrics_cache_max_size = -1;
   int warnings = -1;

   pgmoneta_test_config_save();

   for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
   {
      MCTF_ASSERT_INT_EQ(read_byte_size(cases[i], &log_rotation_size, &metrics_cache_max_size, &warnings), 0,
                         cleanup, "'%s': failed to read the configuration", cases[i]);
      MCTF_ASSERT_INT_EQ(warnings, 2, cleanup, "'%s': both keys should be reported, got %d warnings", cases[i], warnings);
      MCTF_ASSERT_INT_EQ(log_rotation_size, 0, cleanup,
                         "'%s': log_rotation_size should keep its default, got %d", cases[i], log_rotation_size);
      MCTF_ASSERT_INT_EQ(metrics_cache_max_size, 0, cleanup,
                         "'%s': metrics_cache_max_size should keep its default, got %d", cases[i], metrics_cache_max_size);
   }

cleanup:
   pgmoneta_test_config_restore();
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_read_bounds_long_key_and_value)
{
   /* A key or value longer than MISC_LENGTH used to overrun left[]/right[] */
   FILE* file = NULL;
   char path[] = "/tmp/pgmoneta_test_long_value_XXXXXX";
   int fd = -1;
   void* shmem = NULL;
   struct main_configuration* config = NULL;
   char long_value[MISC_LENGTH + 80];
   char long_key[MISC_LENGTH + 80];

   memset(long_value, 'a', sizeof(long_value) - 1);
   long_value[sizeof(long_value) - 1] = '\0';

   memset(long_key, 'k', sizeof(long_key) - 1);
   long_key[sizeof(long_key) - 1] = '\0';

   fd = mkstemp(path);
   MCTF_ASSERT(fd != -1, cleanup, "mkstemp failed for the scratch config file");

   file = fdopen(fd, "w");
   MCTF_ASSERT_PTR_NONNULL(file, cleanup, "fdopen failed for the scratch config file");

   fprintf(file, "[pgmoneta]\n");
   fprintf(file, "host = %s\n", long_value);
   fprintf(file, "%s = 1\n", long_key);
   fprintf(file, "metrics = 5001\n");
   fprintf(file, "base_dir = /tmp/pgmoneta\n");
   fprintf(file, "s3_base_dir = /%s\n", long_value);
   fprintf(file, "\n");
   fprintf(file, "[primary]\n");
   fprintf(file, "host = localhost\n");
   fprintf(file, "port = 5432\n");
   fclose(file);
   file = NULL;

   MCTF_ASSERT(pgmoneta_create_shared_memory(sizeof(struct main_configuration), HUGEPAGE_OFF, &shmem) == 0,
               cleanup, "failed to create shared memory for the test configuration");
   MCTF_ASSERT_PTR_NONNULL(shmem, cleanup, "shared memory pointer is NULL");

   pgmoneta_init_main_configuration(shmem);
   config = (struct main_configuration*)shmem;

   MCTF_ASSERT_INT_EQ(pgmoneta_read_main_configuration(shmem, path), 0,
                      cleanup, "pgmoneta_read_main_configuration should parse a config with an over-long key/value without failing");

   MCTF_ASSERT(strlen(config->host) < MISC_LENGTH, cleanup,
               "host must be truncated to fit within MISC_LENGTH, not left unset or overrun");
   MCTF_ASSERT(strlen(config->host) > 0, cleanup,
               "host should still hold the truncated value, not be empty");

   MCTF_ASSERT(strlen(config->s3.base_dir) > MISC_LENGTH, cleanup,
               "s3_base_dir must not be truncated at MISC_LENGTH");

cleanup:
   if (file != NULL)
   {
      fclose(file);
   }
   if (shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, sizeof(struct main_configuration));
   }
   unlink(path);
   MCTF_FINISH();
}

static int
read_compression(char* value, int* compression_type, int* warnings)
{
   char conf_path[] = "/tmp/pgmoneta_test_compression_XXXXXX";
   char err_path[] = "/tmp/pgmoneta_test_compression_err_XXXXXX";
   char line[MISC_LENGTH * 4];
   struct main_configuration* config = NULL;
   FILE* f = NULL;
   int conf_fd = -1;
   int err_fd = -1;
   int saved_stderr = -1;
   int ret = 1;

   *warnings = 0;

   conf_fd = mkstemp(conf_path);
   err_fd = mkstemp(err_path);
   if (conf_fd == -1 || err_fd == -1)
   {
      goto done;
   }

   f = fdopen(conf_fd, "w");
   if (f == NULL)
   {
      goto done;
   }
   conf_fd = -1;
   fprintf(f, "[pgmoneta]\nhost = localhost\nbase_dir = /tmp\n"
              "compression = %s\n\n"
              "[primary]\nhost = localhost\nport = 5432\nuser = repl\n",
           value);
   fclose(f);
   f = NULL;

   fflush(stderr);
   saved_stderr = dup(STDERR_FILENO);
   if (saved_stderr == -1 || dup2(err_fd, STDERR_FILENO) == -1)
   {
      goto done;
   }
   ret = pgmoneta_test_load_conf(conf_path);
   fflush(stderr);
   dup2(saved_stderr, STDERR_FILENO);

   config = (struct main_configuration*)shmem;
   *compression_type = config->compression_type;

   f = fopen(err_path, "r");
   while (f != NULL && fgets(line, sizeof(line), f) != NULL)
   {
      if (strstr(line, "Unknown:") != NULL)
      {
         (*warnings)++;
      }
   }

done:
   if (f != NULL)
   {
      fclose(f);
   }
   if (saved_stderr != -1)
   {
      close(saved_stderr);
   }
   if (conf_fd != -1)
   {
      close(conf_fd);
   }
   if (err_fd != -1)
   {
      close(err_fd);
   }
   unlink(conf_path);
   unlink(err_path);

   return ret;
}

MCTF_TEST(test_configuration_compression)
{
   struct
   {
      char* value;
      int expected;
   } valid_cases[] = {
      {"none", COMPRESSION_NONE},
      {"gzip", COMPRESSION_CLIENT_GZIP},
      {"client-gzip", COMPRESSION_CLIENT_GZIP},
      {"server-gzip", COMPRESSION_SERVER_GZIP},
      {"zstd", COMPRESSION_CLIENT_ZSTD},
      {"client-zstd", COMPRESSION_CLIENT_ZSTD},
      {"server-zstd", COMPRESSION_SERVER_ZSTD},
      {"lz4", COMPRESSION_CLIENT_LZ4},
      {"client-lz4", COMPRESSION_CLIENT_LZ4},
      {"server-lz4", COMPRESSION_SERVER_LZ4},
      {"bz2", COMPRESSION_CLIENT_BZIP2},
      {"client-bz2", COMPRESSION_CLIENT_BZIP2},
      {"bzip2", COMPRESSION_CLIENT_BZIP2},
      {"client-bzip2", COMPRESSION_CLIENT_BZIP2},
      {"BZIP2", COMPRESSION_CLIENT_BZIP2},
      {"CLIENT-BZIP2", COMPRESSION_CLIENT_BZIP2},
   };
   char* invalid_cases[] = {
      "unknown",
      "invalid",
      "server-bz2",
      "server-bzip2",
      "snappy",
   };
   int compression_type = -1;
   int warnings = 0;

   for (size_t i = 0; i < sizeof(valid_cases) / sizeof(valid_cases[0]); i++)
   {
      MCTF_ASSERT_INT_EQ(read_compression(valid_cases[i].value, &compression_type, &warnings), 0,
                         cleanup, "'%s': failed to read the configuration", valid_cases[i].value);
      MCTF_ASSERT_INT_EQ(compression_type, valid_cases[i].expected, cleanup,
                         "'%s': unexpected compression_type", valid_cases[i].value);
      MCTF_ASSERT_INT_EQ(warnings, 0, cleanup,
                         "'%s': accepted value should not emit Unknown warning", valid_cases[i].value);
   }

   for (size_t i = 0; i < sizeof(invalid_cases) / sizeof(invalid_cases[0]); i++)
   {
      MCTF_ASSERT_INT_EQ(read_compression(invalid_cases[i], &compression_type, &warnings), 0,
                         cleanup, "'%s': failed to read the configuration", invalid_cases[i]);
      MCTF_ASSERT_INT_EQ(warnings, 1, cleanup,
                         "'%s': invalid compression value should report Unknown warning", invalid_cases[i]);
      MCTF_ASSERT_INT_EQ(compression_type, COMPRESSION_CLIENT_ZSTD, cleanup,
                         "'%s': compression_type should remain default COMPRESSION_CLIENT_ZSTD", invalid_cases[i]);
   }

cleanup:
   pgmoneta_test_config_restore();
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_config_large_files)
{
   char config_bin[MAX_PATH];
   char path[MAX_PATH];
   char cmd[MAX_PATH * 3];
   char* output = NULL;
   int exit_code = 0;
   FILE* f = NULL;
   char line_buf[1024];
   size_t line_count = 0;
   bool found_modified_key = false;
   bool found_new_key = false;
   bool found_server_section = false;
   bool found_server_host = false;
   bool found_server_port = false;
   bool found_first_comment = false;
   bool found_last_comment = false;

   /* Resolve pgmoneta-config binary path */
   MCTF_ASSERT_INT_EQ(pgmoneta_test_resolve_binary_path("pgmoneta-config", config_bin), 0, cleanup,
                      "pgmoneta-config binary not found or not executable");

   pgmoneta_snprintf(path, sizeof(path), "test_config_large_files.conf");

   /* 1. Create a configuration file with >8192 lines (8305 lines) */
   f = fopen(path, "w");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "could not create test configuration file");

   fprintf(f, "[general]\n");
   fprintf(f, "key = original_value\n");
   for (int i = 0; i < 8300; i++)
   {
      fprintf(f, "# comment line %d\n", i);
   }
   fprintf(f, "[server]\n");
   fprintf(f, "host = 127.0.0.1\n");
   fprintf(f, "port = 5432\n");
   fclose(f);
   f = NULL;

   /* 2. Update an existing key in [general] exceeding the 8192 line threshold */
   pgmoneta_snprintf(cmd, sizeof(cmd), "\"%s\" set \"%s\" general key modified_value", config_bin, path);
   MCTF_ASSERT_INT_EQ(pgmoneta_test_exec_command(cmd, &output, &exit_code), 0, cleanup,
                      "failed to execute pgmoneta-config set command");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "pgmoneta-config set returned error: %s",
                      output != NULL ? output : "<null>");
   free(output);
   output = NULL;

   /* Verify [general] key is updated and all lines beyond 8192 remain intact */
   f = fopen(path, "r");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "could not read modified configuration file");
   line_count = 0;
   found_modified_key = false;
   found_server_section = false;
   found_server_host = false;
   found_server_port = false;
   found_first_comment = false;
   found_last_comment = false;

   while (fgets(line_buf, sizeof(line_buf), f) != NULL)
   {
      line_count++;
      if (strstr(line_buf, "key = modified_value") != NULL)
      {
         found_modified_key = true;
      }
      if (strstr(line_buf, "# comment line 0\n") != NULL || strcmp(line_buf, "# comment line 0\n") == 0)
      {
         found_first_comment = true;
      }
      if (strstr(line_buf, "# comment line 8299") != NULL)
      {
         found_last_comment = true;
      }
      if (strstr(line_buf, "[server]") != NULL)
      {
         found_server_section = true;
      }
      if (strstr(line_buf, "host = 127.0.0.1") != NULL)
      {
         found_server_host = true;
      }
      if (strstr(line_buf, "port = 5432") != NULL)
      {
         found_server_port = true;
      }
   }
   fclose(f);
   f = NULL;

   MCTF_ASSERT(found_modified_key, cleanup, "modified key not found in configuration file");
   MCTF_ASSERT(found_first_comment, cleanup, "first comment line missing");
   MCTF_ASSERT(found_last_comment, cleanup, "last comment line (8299) missing - file was truncated");
   MCTF_ASSERT(found_server_section, cleanup, "[server] section missing - file was truncated");
   MCTF_ASSERT(found_server_host, cleanup, "server host missing - file was truncated");
   MCTF_ASSERT(found_server_port, cleanup, "server port missing - file was truncated");
   MCTF_ASSERT_INT_EQ(line_count, 8305, cleanup, "unexpected line count after config set (expected 8305, got %zu)", line_count);

   /* 3. Add a new key to [general] */
   pgmoneta_snprintf(cmd, sizeof(cmd), "\"%s\" set \"%s\" general new_key new_value", config_bin, path);
   MCTF_ASSERT_INT_EQ(pgmoneta_test_exec_command(cmd, &output, &exit_code), 0, cleanup,
                      "failed to execute pgmoneta-config set for new key");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "pgmoneta-config set new_key failed: %s",
                      output != NULL ? output : "<null>");
   free(output);
   output = NULL;

   /* 4. Delete the added key from [general] */
   pgmoneta_snprintf(cmd, sizeof(cmd), "\"%s\" del \"%s\" general new_key", config_bin, path);
   MCTF_ASSERT_INT_EQ(pgmoneta_test_exec_command(cmd, &output, &exit_code), 0, cleanup,
                      "failed to execute pgmoneta-config del for new key");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "pgmoneta-config del new_key failed: %s",
                      output != NULL ? output : "<null>");
   free(output);
   output = NULL;

   /* Verify new_key was deleted while modified key and subsequent lines remain intact */
   f = fopen(path, "r");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "could not read configuration file after del");
   line_count = 0;
   found_modified_key = false;
   found_new_key = false;
   found_last_comment = false;
   found_server_section = false;

   while (fgets(line_buf, sizeof(line_buf), f) != NULL)
   {
      line_count++;
      if (strstr(line_buf, "key = modified_value") != NULL)
      {
         found_modified_key = true;
      }
      if (strstr(line_buf, "new_key") != NULL)
      {
         found_new_key = true;
      }
      if (strstr(line_buf, "# comment line 8299") != NULL)
      {
         found_last_comment = true;
      }
      if (strstr(line_buf, "[server]") != NULL)
      {
         found_server_section = true;
      }
   }
   fclose(f);
   f = NULL;

   MCTF_ASSERT(found_modified_key, cleanup, "modified key missing after del");
   MCTF_ASSERT(!found_new_key, cleanup, "deleted new_key should not be present");
   MCTF_ASSERT(found_last_comment, cleanup, "comment line 8299 missing after key del");
   MCTF_ASSERT(found_server_section, cleanup, "[server] section missing after key del");

   /* 5. Delete the [server] section */
   pgmoneta_snprintf(cmd, sizeof(cmd), "\"%s\" del \"%s\" server", config_bin, path);
   MCTF_ASSERT_INT_EQ(pgmoneta_test_exec_command(cmd, &output, &exit_code), 0, cleanup,
                      "failed to execute pgmoneta-config del section");
   MCTF_ASSERT_INT_EQ(exit_code, 0, cleanup, "pgmoneta-config del section failed: %s",
                      output != NULL ? output : "<null>");
   free(output);
   output = NULL;

   /* Verify [server] section was deleted while [general] and comments remain */
   f = fopen(path, "r");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "could not read configuration file after section del");
   found_modified_key = false;
   found_server_section = false;
   found_server_host = false;
   found_last_comment = false;

   while (fgets(line_buf, sizeof(line_buf), f) != NULL)
   {
      if (strstr(line_buf, "key = modified_value") != NULL)
      {
         found_modified_key = true;
      }
      if (strstr(line_buf, "[server]") != NULL)
      {
         found_server_section = true;
      }
      if (strstr(line_buf, "host = 127.0.0.1") != NULL)
      {
         found_server_host = true;
      }
      if (strstr(line_buf, "# comment line 8299") != NULL)
      {
         found_last_comment = true;
      }
   }
   fclose(f);
   f = NULL;

   MCTF_ASSERT(found_modified_key, cleanup, "[general] key missing after section del");
   MCTF_ASSERT(found_last_comment, cleanup, "comments missing after section del");
   MCTF_ASSERT(!found_server_section, cleanup, "[server] section should be deleted");
   MCTF_ASSERT(!found_server_host, cleanup, "server host should be deleted");

cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   free(output);
   unlink(path);
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_config_failure_paths)
{
   char config_bin[MAX_PATH];
   char path[MAX_PATH];
   char tmp_path[MAX_PATH];
   char cmd[MAX_PATH * 3];
   char* output = NULL;
   int exit_code = 0;
   FILE* f = NULL;
   char line_buf[1024];
   bool found_key = false;

   MCTF_ASSERT_INT_EQ(pgmoneta_test_resolve_binary_path("pgmoneta-config", config_bin), 0, cleanup,
                      "pgmoneta-config binary not found or not executable");

   pgmoneta_snprintf(path, sizeof(path), "test_config_failure_paths.conf");
   pgmoneta_snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

   /* 1. Setup initial configuration file */
   f = fopen(path, "w");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "could not create test configuration file");
   fprintf(f, "[general]\n");
   fprintf(f, "key = original_value\n");
   fclose(f);
   f = NULL;

   /* 2. Failure: Temp file cannot be created (directory blocking tmp_path) */
   unlink(tmp_path);
#ifndef _WIN32
   mkdir(tmp_path, 0700);
#else
   mkdir(tmp_path);
#endif

   /* Run set with unopenable temp file */
   pgmoneta_snprintf(cmd, sizeof(cmd), "\"%s\" set \"%s\" general key modified_value", config_bin, path);
   pgmoneta_test_exec_command(cmd, &output, &exit_code);
   MCTF_ASSERT(exit_code != 0, cleanup, "config set should fail when temp file cannot be opened");
   free(output);
   output = NULL;

   /* Verify original file remains intact */
   f = fopen(path, "r");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "original file missing after failed config set");
   found_key = false;
   while (fgets(line_buf, sizeof(line_buf), f) != NULL)
   {
      if (strstr(line_buf, "key = original_value") != NULL)
      {
         found_key = true;
      }
   }
   fclose(f);
   f = NULL;
   MCTF_ASSERT(found_key, cleanup, "original configuration was modified after failed fopen");

   /* Run del with unopenable temp file */
   pgmoneta_snprintf(cmd, sizeof(cmd), "\"%s\" del \"%s\" general key", config_bin, path);
   pgmoneta_test_exec_command(cmd, &output, &exit_code);
   MCTF_ASSERT(exit_code != 0, cleanup, "config del should fail when temp file cannot be opened");
   free(output);
   output = NULL;

   /* Verify original file remains intact */
   f = fopen(path, "r");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "original file missing after failed config del");
   found_key = false;
   while (fgets(line_buf, sizeof(line_buf), f) != NULL)
   {
      if (strstr(line_buf, "key = original_value") != NULL)
      {
         found_key = true;
      }
   }
   fclose(f);
   f = NULL;
   MCTF_ASSERT(found_key, cleanup, "original configuration was modified after failed fopen in del");

   rmdir(tmp_path);

   /* 3. Failure on non-Windows platforms via /dev/full write failure */
#ifndef _WIN32
   if (access("/dev/full", W_OK) == 0)
   {
      unlink(tmp_path);
      if (symlink("/dev/full", tmp_path) == 0)
      {
         pgmoneta_snprintf(cmd, sizeof(cmd), "\"%s\" set \"%s\" general key modified_value", config_bin, path);
         pgmoneta_test_exec_command(cmd, &output, &exit_code);
         MCTF_ASSERT(exit_code != 0, cleanup, "config set should fail on /dev/full write failure");
         free(output);
         output = NULL;

         /* Verify original file is still intact */
         f = fopen(path, "r");
         MCTF_ASSERT_PTR_NONNULL(f, cleanup, "original file missing after failed set on /dev/full");
         found_key = false;
         while (fgets(line_buf, sizeof(line_buf), f) != NULL)
         {
            if (strstr(line_buf, "key = original_value") != NULL)
            {
               found_key = true;
            }
         }
         fclose(f);
         f = NULL;
         MCTF_ASSERT(found_key, cleanup, "original configuration was modified after failed write on /dev/full");

         unlink(tmp_path);
      }
   }
#endif

   /* 4. Failure: Non-existent section deletion */
   pgmoneta_snprintf(cmd, sizeof(cmd), "\"%s\" del \"%s\" nonexistent_section", config_bin, path);
   pgmoneta_test_exec_command(cmd, &output, &exit_code);
   MCTF_ASSERT(exit_code != 0, cleanup, "config del should fail when section is not found");
   free(output);
   output = NULL;

   /* 5. Failure: Non-existent key deletion */
   pgmoneta_snprintf(cmd, sizeof(cmd), "\"%s\" del \"%s\" general nonexistent_key", config_bin, path);
   pgmoneta_test_exec_command(cmd, &output, &exit_code);
   MCTF_ASSERT(exit_code != 0, cleanup, "config del should fail when key is not found");
   free(output);
   output = NULL;

   /* Verify original file remains intact */
   f = fopen(path, "r");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "original file missing after non-existent key del");
   found_key = false;
   while (fgets(line_buf, sizeof(line_buf), f) != NULL)
   {
      if (strstr(line_buf, "key = original_value") != NULL)
      {
         found_key = true;
      }
   }
   fclose(f);
   f = NULL;
   MCTF_ASSERT(found_key, cleanup, "original configuration was modified after non-existent key del");

cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   free(output);
   unlink(tmp_path);
   rmdir(tmp_path);
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_as_seconds_rejects_unrepresentable)
{
   char path[] = "/tmp/pgmoneta_as_seconds_XXXXXX";
   void* shmem = NULL;
   struct main_configuration* config = NULL;
   FILE* file = NULL;
   int fd = -1;
   char digits[400];

   memset(digits, '0', sizeof(digits) - 1);
   digits[sizeof(digits) - 1] = '\0';
   digits[sizeof(digits) - 2] = '5';

   fd = mkstemp(path);
   MCTF_ASSERT(fd != -1, cleanup, "could not create a temporary configuration");
   file = fdopen(fd, "w");
   MCTF_ASSERT(file != NULL, cleanup, "could not open the temporary configuration");

   fprintf(file, "[pgmoneta]\n");
   fprintf(file, "host = localhost\n");
   fprintf(file, "unix_socket_dir = /tmp/\n");
   fprintf(file, "base_dir = /tmp/pgmoneta\n");
   fprintf(file, "authentication_timeout = %ss\n", digits);
   fprintf(file, "blocking_timeout = 5000000000\n");
   fprintf(file, "metrics_cache_max_age = 1000000w\n");
   fprintf(file, "log_rotation_age = 2147483647m\n");
   fprintf(file, "verification = 2h\n");
   fclose(file);
   file = NULL;

   MCTF_ASSERT(pgmoneta_create_shared_memory(sizeof(struct main_configuration), HUGEPAGE_OFF, &shmem) == 0,
               cleanup, "failed to create shared memory for the test configuration");
   MCTF_ASSERT_PTR_NONNULL(shmem, cleanup, "shared memory pointer is NULL");

   pgmoneta_init_main_configuration(shmem);
   config = (struct main_configuration*)shmem;

   MCTF_ASSERT_INT_EQ(pgmoneta_read_main_configuration(shmem, path), 0,
                      cleanup, "an over-long or unrepresentable age must not fail the whole parse");

   MCTF_ASSERT(config->authentication_timeout.s >= 0, cleanup,
               "an over-long digit run must not produce a negative timeout");
   MCTF_ASSERT(config->blocking_timeout.s >= 0, cleanup,
               "a value above INT_MAX must not be truncated into a negative timeout");
   MCTF_ASSERT(config->common.log_rotation_age.s >= 0, cleanup,
               "an overflowing product must not produce a negative age");
   MCTF_ASSERT(config->metrics_cache_max_age.s >= 0, cleanup,
               "an overflowing product must not produce a negative age");
   MCTF_ASSERT(config->verification.s == 7200, cleanup,
               "a valid suffixed value must still parse");

cleanup:
   if (file != NULL)
   {
      fclose(file);
   }
   if (shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, sizeof(struct main_configuration));
   }
   unlink(path);
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_as_int_rejects_out_of_range)
{
   char path[] = "/tmp/pgmoneta_as_int_XXXXXX";
   void* shmem = NULL;
   struct main_configuration* config = NULL;
   FILE* file = NULL;
   int fd = -1;

   fd = mkstemp(path);
   MCTF_ASSERT(fd != -1, cleanup, "could not create a temporary configuration");
   file = fdopen(fd, "w");
   MCTF_ASSERT(file != NULL, cleanup, "could not open the temporary configuration");

   fprintf(file, "[pgmoneta]\n");
   fprintf(file, "host = localhost\n");
   fprintf(file, "unix_socket_dir = /tmp/\n");
   fprintf(file, "base_dir = /tmp/pgmoneta\n");
   fprintf(file, "compression_level = -2147483649\n");
   fprintf(file, "workers = 2147483648\n");
   fprintf(file, "retention = -2147483649\n");
   fclose(file);
   file = NULL;

   MCTF_ASSERT(pgmoneta_create_shared_memory(sizeof(struct main_configuration), HUGEPAGE_OFF, &shmem) == 0,
               cleanup, "failed to create shared memory for the test configuration");
   MCTF_ASSERT_PTR_NONNULL(shmem, cleanup, "shared memory pointer is NULL");

   pgmoneta_init_main_configuration(shmem);
   config = (struct main_configuration*)shmem;
   int default_compression_level = config->compression_level;
   int default_workers = config->workers;

   MCTF_ASSERT_INT_EQ(pgmoneta_read_main_configuration(shmem, path), 0,
                      cleanup, "out-of-range int values must not fail the whole parse");

   MCTF_ASSERT_INT_EQ(config->compression_level, default_compression_level, cleanup,
                      "below-INT_MIN compression_level must not wrap to INT_MAX");
   MCTF_ASSERT_INT_EQ(config->workers, default_workers, cleanup,
                      "above-INT_MAX workers must not wrap to INT_MIN");
   MCTF_ASSERT_INT_EQ(config->retention_days, -1, cleanup,
                      "below-INT_MIN retention must not wrap to positive days");

cleanup:
   if (file != NULL)
   {
      fclose(file);
   }
   if (shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, sizeof(struct main_configuration));
   }
   unlink(path);
   MCTF_FINISH();
}
