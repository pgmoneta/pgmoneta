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
   int ret = 0;

   pgmoneta_test_setup();

   ret = pgmoneta_test_backup("primary", NULL);
   if (ret != 0)
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   ret = !pgmoneta_tsclient_delete("primary", "oldest");
   if (!ret)
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_chain_last)
{
   int ret = 0;
   char* d = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   struct backup** bcks_before = NULL;
   struct backup** bcks_after = NULL;

   pgmoneta_test_setup();

   ret = pgmoneta_test_backup("primary", NULL);
   if (ret != 0)
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   ret = pgmoneta_test_backup("primary", "newest");
   if (ret != 0)
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   ret = pgmoneta_test_backup("primary", "newest");
   if (ret != 0)
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, "server backup not valid", cleanup);

   ret = !pgmoneta_load_infos(d, &num_bck_before, &bcks_before);
   MCTF_ASSERT(ret, "failed to load backup infos", cleanup);
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, "expected 3 backups before deletion", cleanup);

   ret = !pgmoneta_tsclient_delete("primary", "newest");
   if (!ret)
   {
      /* Cleanup resources before skipping */
      if (d != NULL)
      {
         free(d);
         d = NULL;
      }
      if (bcks_before != NULL)
      {
         for (int i = 0; i < num_bck_before; i++)
         {
            if (bcks_before[i] != NULL)
            {
               free(bcks_before[i]);
            }
         }
         free(bcks_before);
         bcks_before = NULL;
      }
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   ret = !pgmoneta_load_infos(d, &num_bck_after, &bcks_after);
   MCTF_ASSERT(ret, "failed to load backup infos after deletion", cleanup);
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, "expected 2 backups after deletion", cleanup);

cleanup:
   if (d != NULL)
   {
      free(d);
      d = NULL;
   }
   if (bcks_before != NULL)
   {
      for (int i = 0; i < num_bck_before; i++)
      {
         if (bcks_before[i] != NULL)
         {
            free(bcks_before[i]);
            bcks_before[i] = NULL;
         }
      }
      free(bcks_before);
      bcks_before = NULL;
   }
   if (bcks_after != NULL)
   {
      for (int i = 0; i < num_bck_after; i++)
      {
         if (bcks_after[i] != NULL)
         {
            free(bcks_after[i]);
            bcks_after[i] = NULL;
         }
      }
      free(bcks_after);
      bcks_after = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_chain_middle)
{
   int ret = 0;
   char* d = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   struct backup** bcks_before = NULL;
   struct backup** bcks_after = NULL;

   pgmoneta_test_setup();

   ret = pgmoneta_test_backup("primary", NULL);
   if (ret != 0)
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   ret = pgmoneta_test_backup("primary", "newest");
   if (ret != 0)
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   ret = pgmoneta_test_backup("primary", "newest");
   if (ret != 0)
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, "server backup not valid", cleanup);

   ret = !pgmoneta_load_infos(d, &num_bck_before, &bcks_before);
   MCTF_ASSERT(ret, "failed to load backup infos", cleanup);
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, "expected 3 backups before deletion", cleanup);
   MCTF_ASSERT_PTR_NONNULL(bcks_before, "backups array is null", cleanup);
   MCTF_ASSERT_PTR_NONNULL(bcks_before[1], "backup[1] is null", cleanup);

   ret = !pgmoneta_tsclient_delete("primary", bcks_before[1]->label);
   if (!ret)
   {
      /* Cleanup resources before skipping */
      if (d != NULL)
      {
         free(d);
         d = NULL;
      }
      if (bcks_before != NULL)
      {
         for (int i = 0; i < num_bck_before; i++)
         {
            if (bcks_before[i] != NULL)
            {
               free(bcks_before[i]);
            }
         }
         free(bcks_before);
         bcks_before = NULL;
      }
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   ret = !pgmoneta_load_infos(d, &num_bck_after, &bcks_after);
   MCTF_ASSERT(ret, "failed to load backup infos after deletion", cleanup);
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, "expected 2 backups after deletion", cleanup);
   MCTF_ASSERT_PTR_NONNULL(bcks_after, "backups array after deletion is null", cleanup);
   MCTF_ASSERT_PTR_NONNULL(bcks_after[0], "backup[0] after deletion is null", cleanup);
   MCTF_ASSERT_PTR_NONNULL(bcks_after[1], "backup[1] after deletion is null", cleanup);
   MCTF_ASSERT_INT_EQ(bcks_after[0]->type, TYPE_FULL, "expected first backup to be full", cleanup);
   MCTF_ASSERT_INT_EQ(bcks_after[1]->type, TYPE_INCREMENTAL, "expected second backup to be incremental", cleanup);
   MCTF_ASSERT_PTR_NONNULL(bcks_before[2], "backup[2] before deletion is null", cleanup);
   MCTF_ASSERT_STR_EQ(bcks_before[2]->label, bcks_after[1]->label, "expected last backup label to match", cleanup);

cleanup:
   if (d != NULL)
   {
      free(d);
      d = NULL;
   }
   if (bcks_before != NULL)
   {
      for (int i = 0; i < num_bck_before; i++)
      {
         if (bcks_before[i] != NULL)
         {
            free(bcks_before[i]);
            bcks_before[i] = NULL;
         }
      }
      free(bcks_before);
      bcks_before = NULL;
   }
   if (bcks_after != NULL)
   {
      for (int i = 0; i < num_bck_after; i++)
      {
         if (bcks_after[i] != NULL)
         {
            free(bcks_after[i]);
            bcks_after[i] = NULL;
         }
      }
      free(bcks_after);
      bcks_after = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_delete_chain_root)
{
   int ret = 0;
   char* d = NULL;
   int num_bck_before = 0;
   int num_bck_after = 0;
   struct backup** bcks_before = NULL;
   struct backup** bcks_after = NULL;

   pgmoneta_test_setup();

   ret = pgmoneta_test_backup("primary", NULL);
   if (ret != 0)
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   ret = pgmoneta_test_backup("primary", "newest");
   if (ret != 0)
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   ret = pgmoneta_test_backup("primary", "newest");
   if (ret != 0)
   {
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   d = pgmoneta_get_server_backup(PRIMARY_SERVER);
   MCTF_ASSERT_PTR_NONNULL(d, "server backup not valid", cleanup);

   ret = !pgmoneta_load_infos(d, &num_bck_before, &bcks_before);
   MCTF_ASSERT(ret, "failed to load backup infos", cleanup);
   MCTF_ASSERT_INT_EQ(num_bck_before, 3, "expected 3 backups before deletion", cleanup);
   MCTF_ASSERT_PTR_NONNULL(bcks_before, "backups array is null", cleanup);
   MCTF_ASSERT_PTR_NONNULL(bcks_before[1], "backup[1] is null", cleanup);

   ret = !pgmoneta_tsclient_delete("primary", "oldest");
   if (!ret)
   {
      /* Cleanup resources before skipping */
      if (d != NULL)
      {
         free(d);
         d = NULL;
      }
      if (bcks_before != NULL)
      {
         for (int i = 0; i < num_bck_before; i++)
         {
            if (bcks_before[i] != NULL)
            {
               free(bcks_before[i]);
            }
         }
         free(bcks_before);
         bcks_before = NULL;
      }
      pgmoneta_test_basedir_cleanup();
      MCTF_SKIP();
   }

   ret = !pgmoneta_load_infos(d, &num_bck_after, &bcks_after);
   MCTF_ASSERT(ret, "failed to load backup infos after deletion", cleanup);
   MCTF_ASSERT_INT_EQ(num_bck_after, 2, "expected 2 backups after deletion", cleanup);
   MCTF_ASSERT_PTR_NONNULL(bcks_after, "backups array after deletion is null", cleanup);
   MCTF_ASSERT_PTR_NONNULL(bcks_after[0], "backup[0] after deletion is null", cleanup);
   MCTF_ASSERT_INT_EQ(bcks_after[0]->type, TYPE_FULL, "expected first backup to be full", cleanup);
   MCTF_ASSERT_INT_EQ(bcks_after[1]->type, TYPE_INCREMENTAL, "expected second backup to be incremental", cleanup);
   MCTF_ASSERT_STR_EQ(bcks_before[1]->label, bcks_after[0]->label, "expected first backup label to match", cleanup);

cleanup:
   if (d != NULL)
   {
      free(d);
      d = NULL;
   }
   if (bcks_before != NULL)
   {
      for (int i = 0; i < num_bck_before; i++)
      {
         if (bcks_before[i] != NULL)
         {
            free(bcks_before[i]);
            bcks_before[i] = NULL;
         }
      }
      free(bcks_before);
      bcks_before = NULL;
   }
   if (bcks_after != NULL)
   {
      for (int i = 0; i < num_bck_after; i++)
      {
         if (bcks_after[i] != NULL)
         {
            free(bcks_after[i]);
            bcks_after[i] = NULL;
         }
      }
      free(bcks_after);
      bcks_after = NULL;
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
