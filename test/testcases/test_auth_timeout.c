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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <pgmoneta.h>
#include <configuration.h>
#include <mctf.h>
#include <shmem.h>
#include <tscommon.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool shmem_allocated = false;

MCTF_MODULE_SETUP(auth_timeout)
{
   if (shmem == NULL)
   {
      pgmoneta_create_shared_memory(sizeof(struct main_configuration), HUGEPAGE_OFF, &shmem);
      memset(shmem, 0, sizeof(struct main_configuration));
      shmem_allocated = true;
   }
}

MCTF_MODULE_TEARDOWN(auth_timeout)
{
   if (shmem_allocated && shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, sizeof(struct main_configuration));
      shmem = NULL;
      shmem_allocated = false;
   }
}

MCTF_TEST_SETUP(auth_timeout)
{
   pgmoneta_test_config_save();
}

MCTF_TEST_TEARDOWN(auth_timeout)
{
   pgmoneta_test_config_restore();
}

MCTF_TEST(test_auth_timeout_parser)
{
   struct main_configuration* config;

   /* conf/01: authentication_timeout = 17s is read by the file parser */
   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/auth_timeout/01.conf"), 0,
                      cleanup, "failed to read 01.conf");
   config = (struct main_configuration*)shmem;
   MCTF_ASSERT_INT_EQ((int)pgmoneta_time_convert(config->authentication_timeout, FORMAT_TIME_S), 17,
                      cleanup, "01.conf: authentication_timeout should be 17 seconds");

   /* conf/02: without the key the default stays 5 seconds */
   MCTF_ASSERT_INT_EQ(pgmoneta_test_load_conf(TEST_CONF_DIR "/auth_timeout/02.conf"), 0,
                      cleanup, "failed to read 02.conf");
   config = (struct main_configuration*)shmem;
   MCTF_ASSERT_INT_EQ((int)pgmoneta_time_convert(config->authentication_timeout, FORMAT_TIME_S), 5,
                      cleanup, "02.conf: authentication_timeout should default to 5 seconds");

cleanup:
   MCTF_FINISH();
}
