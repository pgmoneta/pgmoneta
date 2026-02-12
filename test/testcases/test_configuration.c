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
#include <management.h>
#include <mctf.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>

#include <stdlib.h>

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

MCTF_TEST(test_configuration_reject_invalid_time)
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
