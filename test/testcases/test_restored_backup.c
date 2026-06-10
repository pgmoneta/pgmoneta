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
#include <json.h>
#include <logging.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <tsclient.h>
#include <tsclient_helpers.h>
#include <tscommon.h>
#include <mctf.h>

#include <openssl/ssl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MCTF_TEST(test_pgmoneta_restored_backup_start)
{
   SSL* custom_user_ssl = NULL;
   int custom_user_socket = -1;
   struct query_response* qr = NULL;
   struct json* list_response = NULL;
   struct json* backup = NULL;
   char* label = NULL;
   char restore_path[MAX_PATH];
   int restored_port = -1;
   char command[2048];
   char* output = NULL;
   int exit_code = 0;
   const char* version = NULL;
   const char* container_engine = NULL;

   pgmoneta_test_setup();

   /* --- seed data on primary --- */
   MCTF_ASSERT(pgmoneta_server_authenticate(PRIMARY_SERVER, "mydb", "myuser", "mypass", false,
                                            &custom_user_ssl, &custom_user_socket) == 0,
               cleanup, "failed to authenticate with custom user - check user configuration");

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, custom_user_ssl, custom_user_socket,
                                           "DROP TABLE IF EXISTS restore_check;", &qr) == 0,
               cleanup, "failed to drop existing table");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, custom_user_ssl, custom_user_socket,
                                           "CREATE TABLE restore_check (id int);", &qr) == 0,
               cleanup, "failed to create table");
   pgmoneta_test_cleanup_query_response(&qr);

   MCTF_ASSERT(pgmoneta_test_execute_query(PRIMARY_SERVER, custom_user_ssl, custom_user_socket,
                                           "INSERT INTO restore_check VALUES (1), (2), (3);", &qr) == 0,
               cleanup, "failed to insert data");
   pgmoneta_test_cleanup_query_response(&qr);

   /* --- backup and restore --- */
   MCTF_ASSERT(pgmoneta_test_add_backup() == 0, cleanup,
               "backup failed - check server is online and backup configuration");

   MCTF_ASSERT(pgmoneta_tsclient_restore("primary", "newest", "current", 0) == 0, cleanup,
               "restore operation failed");

   /* --- get backup label to construct correct restore path --- */
   MCTF_ASSERT(pgmoneta_tsclient_list_backup("primary", NULL, &list_response, 0) == 0, cleanup,
               "failed to list backups after restore");

   backup = pgmoneta_tsclient_get_backup(list_response, 0);
   MCTF_ASSERT(backup != NULL, cleanup, "failed to get backup entry");

   label = pgmoneta_tsclient_get_backup_label(backup);
   MCTF_ASSERT(label != NULL, cleanup, "failed to get backup label");
   label = strdup(label);
   MCTF_ASSERT(label != NULL, cleanup, "failed to duplicate backup label");

   pgmoneta_snprintf(restore_path, sizeof(restore_path), "%s/primary-%s", TEST_RESTORE_DIR, label);

   /* --- start restored server in container --- */
   restored_port = start_restored_backup(restore_path, RESTORED_BACKUP_DEFAULT_PORT);
   MCTF_ASSERT(restored_port > 0, cleanup, "failed to start restored backup container");

   /* --- verify row count via psql inside the container --- */
   version = getenv("TEST_PG_VERSION");
   if (version == NULL || version[0] == '\0')
   {
      version = "17";
   }

   container_engine = getenv("CONTAINER_ENGINE");
   if (container_engine == NULL || container_engine[0] == '\0')
   {
      container_engine = "podman";
   }

   pgmoneta_snprintf(command, sizeof(command),
                     "%s exec %s /usr/pgsql-%s/bin/psql "
                     "-h localhost -p 5432 -U myuser -d mydb -tAc "
                     "\"SELECT count(*) FROM restore_check;\"",
                     container_engine, "pgmoneta-test-restored-backup", version);

   MCTF_ASSERT(pgmoneta_test_exec_command(command, &output, &exit_code) == 0 && exit_code == 0,
               cleanup, "failed to run psql query inside restored container (exit=%d): %s",
               exit_code, output != NULL ? output : "(null)");

   MCTF_ASSERT(output != NULL && atoi(output) == 3,
               cleanup, "restored backup does not contain expected 3 rows");

cleanup:
   free(output);
   free(label);
   if (list_response != NULL)
   {
      pgmoneta_json_destroy(list_response);
   }
   pgmoneta_test_cleanup_query_response(&qr);
   pgmoneta_test_cleanup_connection(&custom_user_ssl, &custom_user_socket);
   stop_restored_backup();
   pgmoneta_test_basedir_cleanup();
   pgmoneta_test_teardown();
   MCTF_FINISH();
}
