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
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

cleanup:
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_configuration_get_returns_set_values)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_BLOCKING_TIMEOUT, "45s", 0) == 0, cleanup,
               "conf set blocking_timeout 45s failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_METRICS_CACHE_MAX_AGE, "2m", 0) == 0, cleanup,
               "conf set metrics_cache_max_age 2m failed");
   MCTF_ASSERT(pgmoneta_tsclient_conf_set(CONFIGURATION_ARGUMENT_LOG_ROTATION_AGE, "30s", 0) == 0, cleanup,
               "conf set log_rotation_age 30s failed");

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
