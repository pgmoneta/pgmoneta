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
#include <info.h>
#include <server.h>
#include <tsclient.h>
#include <tscommon.h>
#include <utils.h>
#include <mctf.h>

#include <stdio.h>
#include <stdlib.h>

MCTF_TEST(test_pgmoneta_delete_full)
{
   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   if (pgmoneta_tsclient_delete("primary", "oldest", 0))
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_retained_backup)
{
   char* d = NULL;
   int num_backups = 0;
   struct backup** backups = NULL;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, cleanup, "server backup not valid");

   // Retain the backup
   MCTF_ASSERT(!pgmoneta_tsclient_retain("primary", "oldest", false, 0), cleanup, "failed to retain backup");

   // Delete without force should fail
   MCTF_ASSERT(pgmoneta_tsclient_delete("primary", "oldest", 0) != 0, cleanup, "delete should fail for retained backup");

   // Verify the count is still 1
   MCTF_ASSERT(!pgmoneta_load_infos(d, &num_backups, &backups), cleanup, "failed to load backup infos");
   MCTF_ASSERT_INT_EQ(num_backups, 1, cleanup, "expected 1 backup after retain");

   // Free these backups
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

   // Expunge the backup (remove the retained flag)
   MCTF_ASSERT(!pgmoneta_tsclient_expunge("primary", "oldest", false, 0), cleanup, "failed to expunge backup");

   // Delete will work now without force
   MCTF_ASSERT(!pgmoneta_tsclient_delete("primary", "oldest", 0), cleanup, "failed to delete after expunge");

   // Verify the count is 0
   MCTF_ASSERT(!pgmoneta_load_infos(d, &num_backups, &backups), cleanup, "failed to load backup infos after delete");
   MCTF_ASSERT_INT_EQ(num_backups, 0, cleanup, "expected 0 backups after delete");

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

MCTF_TEST(test_pgmoneta_delete_force_retained_backup)
{
   char* d = NULL;
   int num_backups = 0;
   struct backup** backups = NULL;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, cleanup, "server backup not valid");

   // Retain the backup
   MCTF_ASSERT(!pgmoneta_tsclient_retain("primary", "oldest", false, 0), cleanup, "failed to retain backup");

   // Delete without force should fail
   MCTF_ASSERT(pgmoneta_tsclient_delete("primary", "oldest", 0) != 0, cleanup, "delete should fail for retained backup");

   // Verify the count is still 1
   MCTF_ASSERT(!pgmoneta_load_infos(d, &num_backups, &backups), cleanup, "failed to load backup infos");
   MCTF_ASSERT_INT_EQ(num_backups, 1, cleanup, "expected 1 backup after retain");

   // Free the backups
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

   // Delete will work now with the force flag
   MCTF_ASSERT(!pgmoneta_tsclient_force_delete("primary", "oldest", 0), cleanup, "failed to force delete retained backup");

   // Verify the count is 0
   MCTF_ASSERT(!pgmoneta_load_infos(d, &num_backups, &backups), cleanup, "failed to load backup infos after force delete");
   MCTF_ASSERT_INT_EQ(num_backups, 0, cleanup, "expected 0 backups after force delete");

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

MCTF_TEST(test_pgmoneta_delete_chain_last)
{
   char* d = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   struct backup** bcks_before = NULL;
   struct backup** bcks_after = NULL;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup_chain())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, cleanup, "server backup not valid");

   MCTF_ASSERT(!pgmoneta_load_infos(d, &num_bck_before, &bcks_before), cleanup, "failed to load backup infos");
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, cleanup, "expected 3 backups before deletion");

   if (pgmoneta_tsclient_delete("primary", "newest", 0))
   {
      // Cleanup resources before skipping

      free(d);
      d = NULL;
      if (bcks_before != NULL)
      {
         for (int i = 0; i < num_bck_before; i++)
         {
            free(bcks_before[i]);
            bcks_before[i] = NULL;
         }
         free(bcks_before);
         bcks_before = NULL;
      }
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   MCTF_ASSERT(!pgmoneta_load_infos(d, &num_bck_after, &bcks_after), cleanup, "failed to load backup infos after deletion");
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, cleanup, "expected 2 backups after deletion");

cleanup:
   free(d);
   d = NULL;
   if (bcks_before != NULL)
   {
      for (int i = 0; i < num_bck_before; i++)
      {
         free(bcks_before[i]);
         bcks_before[i] = NULL;
      }
      free(bcks_before);
      bcks_before = NULL;
   }
   if (bcks_after != NULL)
   {
      for (int i = 0; i < num_bck_after; i++)
      {
         free(bcks_after[i]);
         bcks_after[i] = NULL;
      }
      free(bcks_after);
      bcks_after = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_chain_middle)
{
   char* d = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   struct backup** bcks_before = NULL;
   struct backup** bcks_after = NULL;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup_chain())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, cleanup, "server backup not valid");

   MCTF_ASSERT(!pgmoneta_load_infos(d, &num_bck_before, &bcks_before), cleanup, "failed to load backup infos");
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, cleanup, "expected 3 backups before deletion");
   MCTF_ASSERT_PTR_NONNULL(bcks_before, cleanup, "backups array is null");
   MCTF_ASSERT_PTR_NONNULL(bcks_before[1], cleanup, "backup[1] is null");

   if (pgmoneta_tsclient_delete("primary", bcks_before[1]->label, 0))
   {
      // Cleanup resources before skipping

      free(d);
      d = NULL;
      if (bcks_before != NULL)
      {
         for (int i = 0; i < num_bck_before; i++)
         {
            free(bcks_before[i]);
            bcks_before[i] = NULL;
         }
         free(bcks_before);
         bcks_before = NULL;
      }
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   MCTF_ASSERT(!pgmoneta_load_infos(d, &num_bck_after, &bcks_after), cleanup, "failed to load backup infos after deletion");
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, cleanup, "expected 2 backups after deletion");
   MCTF_ASSERT_PTR_NONNULL(bcks_after, cleanup, "backups array after deletion is null");
   MCTF_ASSERT_PTR_NONNULL(bcks_after[0], cleanup, "backup[0] after deletion is null");
   MCTF_ASSERT_PTR_NONNULL(bcks_after[1], cleanup, "backup[1] after deletion is null");
   MCTF_ASSERT_INT_EQ(bcks_after[0]->type, TYPE_FULL, cleanup, "expected first backup to be full");
   MCTF_ASSERT_INT_EQ(bcks_after[1]->type, TYPE_INCREMENTAL, cleanup, "expected second backup to be incremental");
   MCTF_ASSERT_PTR_NONNULL(bcks_before[2], cleanup, "backup[2] before deletion is null");
   MCTF_ASSERT_STR_EQ(bcks_before[2]->label, bcks_after[1]->label, cleanup, "expected last backup label to match");

cleanup:
   free(d);
   d = NULL;
   if (bcks_before != NULL)
   {
      for (int i = 0; i < num_bck_before; i++)
      {
         free(bcks_before[i]);
         bcks_before[i] = NULL;
      }
      free(bcks_before);
      bcks_before = NULL;
   }
   if (bcks_after != NULL)
   {
      for (int i = 0; i < num_bck_after; i++)
      {
         free(bcks_after[i]);
         bcks_after[i] = NULL;
      }
      free(bcks_after);
      bcks_after = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_chain_root)
{
   char* d = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   struct backup** bcks_before = NULL;
   struct backup** bcks_after = NULL;

   pgmoneta_test_setup();

   if (pgmoneta_test_add_backup_chain())
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, cleanup, "server backup not valid");

   MCTF_ASSERT(!pgmoneta_load_infos(d, &num_bck_before, &bcks_before), cleanup, "failed to load backup infos");
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, cleanup, "expected 3 backups before deletion");
   MCTF_ASSERT_PTR_NONNULL(bcks_before, cleanup, "backups array is null");
   MCTF_ASSERT_PTR_NONNULL(bcks_before[1], cleanup, "backup[1] is null");

   if (pgmoneta_tsclient_delete("primary", "oldest", 0))
   {
      // Cleanup resources before skipping

      free(d);
      d = NULL;
      if (bcks_before != NULL)
      {
         for (int i = 0; i < num_bck_before; i++)
         {
            free(bcks_before[i]);
            bcks_before[i] = NULL;
         }
         free(bcks_before);
         bcks_before = NULL;
      }
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP("backup failed during setup");
   }

   MCTF_ASSERT(!pgmoneta_load_infos(d, &num_bck_after, &bcks_after), cleanup, "failed to load backup infos after deletion");
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, cleanup, "expected 2 backups after deletion");
   MCTF_ASSERT_PTR_NONNULL(bcks_after, cleanup, "backups array after deletion is null");
   MCTF_ASSERT_PTR_NONNULL(bcks_after[0], cleanup, "backup[0] after deletion is null");
   MCTF_ASSERT_INT_EQ(bcks_after[0]->type, TYPE_FULL, cleanup, "expected first backup to be full");
   MCTF_ASSERT_INT_EQ(bcks_after[1]->type, TYPE_INCREMENTAL, cleanup, "expected second backup to be incremental");
   MCTF_ASSERT_STR_EQ(bcks_before[1]->label, bcks_after[0]->label, cleanup, "expected first backup label to match");

cleanup:
   free(d);
   d = NULL;
   if (bcks_before != NULL)
   {
      for (int i = 0; i < num_bck_before; i++)
      {
         free(bcks_before[i]);
         bcks_before[i] = NULL;
      }
      free(bcks_before);
      bcks_before = NULL;
   }
   if (bcks_after != NULL)
   {
      for (int i = 0; i < num_bck_after; i++)
      {
         free(bcks_after[i]);
         bcks_after[i] = NULL;
      }
      free(bcks_after);
      bcks_after = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
