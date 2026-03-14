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
#include <aes.h>
#include <art.h>
#include <compression.h>
#include <hot_standby.h>
#include <info.h>
#include <logging.h>
#include <mctf.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <utils.h>
#include <workflow.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
check_files_recursive(const char* dir_path, int* found_files)
{
   DIR* dir;
   struct dirent* entry;
   char path[MAX_PATH];

   if (!(dir = opendir(dir_path)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }
         pgmoneta_snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
         if (check_files_recursive(path, found_files))
         {
            goto error;
         }
      }
      else
      {
         (*found_files)++;
         if (pgmoneta_is_encrypted(entry->d_name))
         {
            pgmoneta_log_error("File %s/%s is encrypted (.aes)", dir_path, entry->d_name);
            goto error;
         }
         if (pgmoneta_compression_is_compressed(entry->d_name))
         {
            pgmoneta_log_error("File %s/%s is compressed", dir_path, entry->d_name);
            goto error;
         }
      }
   }

   closedir(dir);
   return 0;

error:
   if (dir != NULL)
   {
      closedir(dir);
   }
   return 1;
}

MCTF_TEST(test_pgmoneta_hot_standby_basic)
{
   char standby_dir[MAX_PATH];
   char pg_version_file[MAX_PATH];
   int found_files = 0;

   pgmoneta_test_setup();

   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   pgmoneta_snprintf(standby_dir, sizeof(standby_dir), "%s/primary", TEST_HOT_STANDBY_DIR);
   MCTF_ASSERT(pgmoneta_exists(standby_dir), cleanup, "hot standby directory does not exist: %s", standby_dir);

   pgmoneta_snprintf(pg_version_file, sizeof(pg_version_file), "%s/PG_VERSION", standby_dir);
   MCTF_ASSERT(pgmoneta_exists(pg_version_file), cleanup, "PG_VERSION file missing in hot standby");

   MCTF_ASSERT(check_files_recursive(standby_dir, &found_files) == 0, cleanup, "Found encrypted or compressed files in hot standby");
   MCTF_ASSERT(found_files > 0, cleanup, "No files found in hot standby directory");

cleanup:
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_hot_standby_overrides)
{
   char overrides_dir[MAX_PATH];
   char override_src[MAX_PATH];
   char override_dst[MAX_PATH];
   char standby_dir[MAX_PATH];
   int found_files = 0;
   FILE* f = NULL;

   pgmoneta_test_setup();

   /* Prepare overrides source directory and file */
   pgmoneta_snprintf(overrides_dir, sizeof(overrides_dir), "%s/overrides", TEST_HOT_STANDBY_DIR);
   pgmoneta_mkdir(overrides_dir);

   pgmoneta_snprintf(override_src, sizeof(override_src), "%s/override_marker.txt", overrides_dir);
   f = fopen(override_src, "w");
   MCTF_ASSERT(f != NULL, cleanup, "Failed to create override source file: %s", override_src);
   fprintf(f, "hot-standby-override");
   fclose(f);
   f = NULL;

   /* Create a backup which should trigger hot_standby and overrides handling */
   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "backup failed during setup - check server is online and backup configuration");

   /* Standby destination for the primary server */
   pgmoneta_snprintf(standby_dir, sizeof(standby_dir), "%s/primary", TEST_HOT_STANDBY_DIR);
   MCTF_ASSERT(pgmoneta_exists(standby_dir), cleanup, "hot standby directory does not exist: %s", standby_dir);

   /* The override file should have been copied into the standby destination */
   pgmoneta_snprintf(override_dst, sizeof(override_dst), "%s/override_marker.txt", standby_dir);
   MCTF_ASSERT(pgmoneta_exists(override_dst), cleanup, "override file missing in hot standby: %s", override_dst);

   /* Ensure there are no encrypted or compressed files in the final standby layout */
   MCTF_ASSERT(check_files_recursive(standby_dir, &found_files) == 0, cleanup, "Found encrypted or compressed files in hot standby (overrides)");
   MCTF_ASSERT(found_files > 0, cleanup, "No files found in hot standby directory (overrides)");

cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}

MCTF_TEST(test_pgmoneta_hot_standby_tablespaces)
{
   char standby_dir[MAX_PATH];
   char tblspc_target[MAX_PATH];
   char backup_dir[MAX_PATH];
   char data_dir[MAX_PATH];
   char tblspc_dir[MAX_PATH];
   char marker_src[MAX_PATH];
   char marker_dst[MAX_PATH];
   char info_path[MAX_PATH];
   int found_files = 0;
   struct main_configuration* config = (struct main_configuration*)shmem;
   struct workflow* wf = pgmoneta_create_hot_standby();
   struct art* nodes = NULL;
   struct backup** backups = NULL;
   int number_of_backups = 0;
   FILE* f = NULL;

   pgmoneta_test_setup();

   /* Create a real backup first to have a valid structure */
   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup, "Initial backup failed");

   /* Get server backup base directory */
   char* base = pgmoneta_get_server_backup(0);
   pgmoneta_load_infos(base, &number_of_backups, &backups);
   MCTF_ASSERT(number_of_backups > 0, cleanup, "No backups found");

   char* label = backups[number_of_backups - 1]->label;

   /* 1. Fake the tablespace directory in the backup */
   pgmoneta_snprintf(backup_dir, sizeof(backup_dir), "%s/%s", base, label);
   pgmoneta_snprintf(data_dir, sizeof(data_dir), "%s/data", backup_dir);
   pgmoneta_snprintf(tblspc_dir, sizeof(tblspc_dir), "%s/pg_tblspc/16384", data_dir);
   pgmoneta_mkdir(data_dir);
   pgmoneta_mkdir(tblspc_dir);

   pgmoneta_snprintf(marker_src, sizeof(marker_src), "%s/mapped_file.txt", tblspc_dir);
   f = fopen(marker_src, "w");
   MCTF_ASSERT(f != NULL, cleanup, "Failed to create marker file");
   fprintf(f, "tablespace-mapping-test");
   fclose(f);
   f = NULL;

   /* 2. Poison backup.info with tablespace metadata */
   pgmoneta_snprintf(info_path, sizeof(info_path), "%s/backup.info", backup_dir);
   f = fopen(info_path, "a");
   MCTF_ASSERT(f != NULL, cleanup, "Failed to open backup.info for appending");
   fprintf(f, "TABLESPACES=1\n");
   fprintf(f, "TABLESPACE_OID_0=16384\n");
   fprintf(f, "TABLESPACE_PATH_0=/tmp/fake_backup_path\n");
   fprintf(f, "TABLESPACE_0=fake_tblspc\n");
   fclose(f);
   f = NULL;

   /* 3. Configure the mapping in shared memory */
   pgmoneta_snprintf(standby_dir, sizeof(standby_dir), "%s/primary", TEST_HOT_STANDBY_DIR);
   pgmoneta_snprintf(tblspc_target, sizeof(tblspc_target), "%s/tblspc_mapped", TEST_HOT_STANDBY_DIR);
   pgmoneta_mkdir(tblspc_target);

   memset(config->common.servers[0].hot_standby_tablespaces[0], 0, MAX_PATH);
   pgmoneta_snprintf(config->common.servers[0].hot_standby_tablespaces[0], MAX_PATH, "16384 -> %s", tblspc_target);

   /* 4. Execute the hot standby workflow manually on our poisoned backup */
   pgmoneta_art_create(&nodes);
   pgmoneta_art_insert(nodes, NODE_SERVER_ID, (uintptr_t)0, ValueInt32);
   pgmoneta_art_insert(nodes, NODE_LABEL, (uintptr_t)label, ValueString);

   MCTF_ASSERT(wf->execute(label, nodes) == 0, cleanup, "Hot standby execution failed");

   /* 5. Assertions */
   pgmoneta_snprintf(marker_dst, sizeof(marker_dst), "%s/mapped_file.txt", tblspc_target);
   MCTF_ASSERT(pgmoneta_exists(marker_dst), cleanup, "Mapped tablespace file missing at destination");

   /* Check the symlink in pg_tblspc */
   char link_path[MAX_PATH];
   pgmoneta_snprintf(link_path, sizeof(link_path), "%s/pg_tblspc/16384", standby_dir);
   MCTF_ASSERT(pgmoneta_exists(link_path), cleanup, "Symlink in pg_tblspc missing");

cleanup:
   if (f != NULL)
   {
      fclose(f);
   }
   if (backups != NULL)
   {
      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);
   }
   free(base);
   pgmoneta_art_destroy(nodes);
   free(wf);
   pgmoneta_test_basedir_cleanup();
   MCTF_FINISH();
}
