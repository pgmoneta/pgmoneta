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
 *
 */

#include <pgmoneta.h>
#include <info.h>
#include <logging.h>
#include <tsclient.h>
#include <tscommon.h>
#include <mctf.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MCTF_TEST(test_pgmoneta_backup_full)
{
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_backup_incremental_basic)
{
   char* d = NULL;
   int num_backups = 0;
   struct backup** backups = NULL;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup_chain())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup chain failed during setup");
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, cleanup, "server backup directory is null");

   MCTF_ASSERT(!pgmoneta_load_infos(d, &num_backups, &backups), cleanup, "failed to load backup infos");
   MCTF_ASSERT_INT_EQ(num_backups, 3, cleanup, "backup count mismatch");
   MCTF_ASSERT_PTR_NONNULL(backups, cleanup, "backups should not be NULL");

   // sort the backups in ascending order

   pgmoneta_sort_backups(backups, num_backups, false);

   MCTF_ASSERT_INT_EQ(backups[0]->type, TYPE_FULL, cleanup, "backup 0 type mismatch");
   MCTF_ASSERT_INT_EQ(backups[1]->type, TYPE_INCREMENTAL, cleanup, "backup 1 type mismatch");
   MCTF_ASSERT_INT_EQ(backups[2]->type, TYPE_INCREMENTAL, cleanup, "backup 2 type mismatch");

   MCTF_ASSERT_STR_EQ(backups[1]->parent_label, backups[0]->label, cleanup, "backup 1 parent mismatch");
   MCTF_ASSERT_STR_EQ(backups[2]->parent_label, backups[1]->label, cleanup, "backup 2 parent mismatch");

cleanup:
   free(d);
   d = NULL;
   if (backups != NULL)
   {
      for (int i = 0; i < num_backups; i++)
      {
         free(backups[i]);
         backups[i] = NULL;
      }
      free(backups);
      backups = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
