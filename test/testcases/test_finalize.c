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
#include <management.h>
#include <mctf.h>
#include <tsclient.h>
#include <tscommon.h>

#include <stdio.h>
#include <stdlib.h>

MCTF_TEST_NEGATIVE(test_pgmoneta_finalize_backup_lock_released)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0, cleanup,
               "first backup failed");

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0, cleanup,
               "second backup failed — repository lock may not have been released");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_finalize_restore_lock_released)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup,
               "backup failed during setup");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0, cleanup,
               "first restore failed");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0, cleanup,
               "second restore failed — repository lock may not have been released");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_finalize_verify_lock_released)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup,
               "backup failed during setup");

   MCTF_ASSERT(pgmoneta_tsclient_verify("primary", "newest", TEST_RESTORE_DIR, NULL, NULL, 0) == 0, cleanup,
               "first verify failed");

   MCTF_ASSERT(pgmoneta_tsclient_verify("primary", "newest", TEST_RESTORE_DIR, NULL, NULL, 0) == 0, cleanup,
               "second verify failed — repository lock may not have been released");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_finalize_backup_restore_verify_sequence)
{
   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_tsclient_backup("primary", NULL, 0) == 0, cleanup,
               "backup failed");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0, cleanup,
               "restore failed after backup");

   MCTF_ASSERT(pgmoneta_tsclient_verify("primary", "newest", TEST_RESTORE_DIR, NULL, NULL, 0) == 0, cleanup,
               "verify failed after restore");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
