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

/* pgmoneta */
#include <pgmoneta.h>
#include <extension.h>
#include <logging.h>
#include <message.h>
#include <network.h>
#include <security.h>

/* system */
#include <stdlib.h>

static int query_execute(SSL* ssl, int socket, char* qs, struct query_response** qr);
static int detect_extensions(SSL* ssl, int socket, int server);

int
pgmoneta_ext_is_installed(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT * FROM pg_available_extensions WHERE name = 'pgmoneta_ext';", qr);
}

int
pgmoneta_ext_switch_wal(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT pgmoneta_ext_switch_wal();", qr);
}

int
pgmoneta_ext_checkpoint(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT pgmoneta_ext_checkpoint();", qr);
}

int
pgmoneta_ext_privilege(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT rolsuper FROM pg_roles WHERE rolname = current_user;", qr);
}

int
pgmoneta_ext_get_file(SSL* ssl, int socket, char* file_path, struct query_response** qr)
{
   char query[MAX_QUERY_LENGTH];
   snprintf(query, MAX_QUERY_LENGTH, "SELECT pgmoneta_ext_get_file('%s');", file_path);
   return query_execute(ssl, socket, query, qr);
}

int
pgmoneta_ext_get_files(SSL* ssl, int socket, char* file_path, struct query_response** qr)
{
   char query[MAX_QUERY_LENGTH];
   snprintf(query, MAX_QUERY_LENGTH, "SELECT * FROM pgmoneta_ext_get_files('%s');", file_path);
   return query_execute(ssl, socket, query, qr);
}

int
pgmoneta_ext_send_file_chunk(SSL* ssl, int socket, char* dest_path, char* base64_data, struct query_response** qr)
{
   char query[MAX_QUERY_LENGTH];
   snprintf(query, MAX_QUERY_LENGTH, "SELECT pgmoneta_ext_receive_file_chunk('%s', '%s');", base64_data, dest_path);
   return query_execute(ssl, socket, query, qr);
}

int
pgmoneta_ext_promote(SSL* ssl, int socket, struct query_response** qr)
{
   return query_execute(ssl, socket, "SELECT pgmoneta_ext_promote();", qr);
}

static int
query_execute(SSL* ssl, int socket, char* qs, struct query_response** qr)
{
   struct message* query_msg = NULL;

   if (pgmoneta_create_query_message(qs, &query_msg) != MESSAGE_STATUS_OK || query_msg == NULL)
   {
      pgmoneta_log_debug("Failed to create query message");
      goto error;
   }

   if (pgmoneta_query_execute(ssl, socket, query_msg, qr) != 0 || qr == NULL)
   {
      pgmoneta_log_debug("Failed to execute query: %s", qs);
      goto error;
   }

   pgmoneta_free_message(query_msg);

   return 0;

error:

   pgmoneta_free_message(query_msg);

   return 1;
}

static int
detect_extensions(SSL* ssl, int socket, int server)
{
   int ret;
   struct query_response* qr = NULL;
   struct tuple* current = NULL;
   struct main_configuration* config;
   int extension_idx;
   bool pgmoneta_ext_found = false;

   config = (struct main_configuration*)shmem;

   config->common.servers[server].number_of_extensions = 0;
   config->common.servers[server].has_extension = false;
   memset(config->common.servers[server].ext_version, 0, sizeof(config->common.servers[server].ext_version));

   ret = query_execute(ssl, socket,
                       "SELECT name, installed_version, comment "
                       "FROM pg_available_extensions "
                       "WHERE installed_version IS NOT NULL "
                       "ORDER BY name;",
                       &qr);
   if (ret != 0)
   {
      pgmoneta_log_warn("Failed to detect extensions for server %s", config->common.servers[server].name);
      goto error;
   }

   if (qr == NULL || qr->number_of_columns < 3)
   {
      pgmoneta_log_warn("Invalid query response for extensions on server %s", config->common.servers[server].name);
      goto error;
   }

   current = qr->tuples;
   while (current != NULL)
   {
      if (config->common.servers[server].number_of_extensions >= NUMBER_OF_EXTENSIONS)
      {
         pgmoneta_log_warn("Maximum number of extensions reached for server %s (%d)",
                           config->common.servers[server].name, NUMBER_OF_EXTENSIONS);
         break;
      }

      extension_idx = config->common.servers[server].number_of_extensions;

      if (current->data[0] != NULL)
      {
         strncpy(config->common.servers[server].extensions[extension_idx].name,
                 current->data[0], MISC_LENGTH - 1);
         config->common.servers[server].extensions[extension_idx].name[MISC_LENGTH - 1] = '\0';

         if (!strcmp(current->data[0], "pgmoneta_ext"))
         {
            pgmoneta_ext_found = true;
            if (current->data[1] != NULL)
            {
               strncpy(config->common.servers[server].ext_version, current->data[1], MISC_LENGTH - 1);
               config->common.servers[server].ext_version[MISC_LENGTH - 1] = '\0';
               config->common.servers[server].has_extension = true;
            }
         }
      }
      else
      {
         config->common.servers[server].extensions[extension_idx].name[0] = '\0';
      }

      if (current->data[1] != NULL &&
          pgmoneta_extension_parse_version(current->data[1],
                                           &config->common.servers[server].extensions[extension_idx].installed_version) == 0)
      {
         config->common.servers[server].extensions[extension_idx].enabled = true;
      }
      else
      {
         pgmoneta_log_warn("Failed to parse extension version '%s' for %s on server %s",
                           current->data[1] ? current->data[1] : "NULL",
                           config->common.servers[server].extensions[extension_idx].name,
                           config->common.servers[server].name);
         config->common.servers[server].extensions[extension_idx].enabled = false;
         config->common.servers[server].extensions[extension_idx].installed_version.major = -1;
         config->common.servers[server].extensions[extension_idx].installed_version.minor = -1;
         config->common.servers[server].extensions[extension_idx].installed_version.patch = -1;
      }

      if (current->data[2] != NULL)
      {
         strncpy(config->common.servers[server].extensions[extension_idx].comment,
                 current->data[2], MISC_LENGTH - 1);
         config->common.servers[server].extensions[extension_idx].comment[MISC_LENGTH - 1] = '\0';
      }
      else
      {
         config->common.servers[server].extensions[extension_idx].comment[0] = '\0';
      }

      config->common.servers[server].extensions[extension_idx].server = server;

      config->common.servers[server].number_of_extensions++;
      current = current->next;
   }

   pgmoneta_log_debug("Server %s: Detected %d extensions:",
                      config->common.servers[server].name,
                      config->common.servers[server].number_of_extensions);

   for (int i = 0; i < config->common.servers[server].number_of_extensions; i++)
   {
      struct extension_info* ext = &config->common.servers[server].extensions[i];
      if (ext->enabled)
      {
         pgmoneta_log_debug("  - %s (version %d.%d.%d) - %s",
                            ext->name,
                            ext->installed_version.major,
                            ext->installed_version.minor == -1 ? 0 : ext->installed_version.minor,
                            ext->installed_version.patch == -1 ? 0 : ext->installed_version.patch,
                            ext->comment);
      }
      else
      {
         pgmoneta_log_debug("  - %s (version parse failed) - %s",
                            ext->name, ext->comment);
      }
   }

   if (!pgmoneta_ext_found)
   {
      pgmoneta_log_debug("pgmoneta_ext extension not found on server %s", config->common.servers[server].name);
   }

   pgmoneta_free_query_response(qr);
   return 0;

error:
   pgmoneta_free_query_response(qr);
   return 1;
}

int
pgmoneta_detect_server_extensions(int server)
{
   int usr = -1;
   SSL* ssl = NULL;
   int socket = -1;
   struct main_configuration* config;
   int result;

   config = (struct main_configuration*)shmem;

   if (server < 0 || server >= config->common.number_of_servers)
   {
      pgmoneta_log_error("Invalid server index: %d", server);
      return 1;
   }

   if (!config->common.servers[server].online)
   {
      pgmoneta_log_warn("Server %s is not online", config->common.servers[server].name);
      return 1;
   }

   /* Find and authenticate user for this server */
   for (int i = 0; usr == -1 && i < config->common.number_of_users; i++)
   {
      if (!strcmp(config->common.servers[server].username, config->common.users[i].username))
      {
         usr = i;
      }
   }

   if (usr == -1)
   {
      pgmoneta_log_error("User not found for server: %d", server);
      goto error;
   }

   if (pgmoneta_server_authenticate(server, "postgres", config->common.users[usr].username, config->common.users[usr].password, false, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_error("Authentication failed for user %s on %s", config->common.users[usr].username, config->common.servers[server].name);
      goto error;
   }

   result = detect_extensions(ssl, socket, server);

   pgmoneta_close_ssl(ssl);
   pgmoneta_disconnect(socket);

   return result;

error:
   if (ssl != NULL)
   {
      pgmoneta_close_ssl(ssl);
   }
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   pgmoneta_memory_destroy();

   return 1;
}

int
pgmoneta_extension_parse_version(char* version_str, struct version* version)
{
   char* str_copy = NULL;
   char* token = NULL;
   char* saveptr = NULL;
   int part = 0;

   if (!version_str || !version)
   {
      pgmoneta_log_error("Invalid parameters for version parsing");
      goto error;
   }

   version->major = -1;
   version->minor = -1;
   version->patch = -1;

   str_copy = strdup(version_str);
   if (!str_copy)
   {
      pgmoneta_log_error("Failed to allocate memory for version string copy");
      goto error;
   }

   token = strtok_r(str_copy, ".", &saveptr);
   while (token && part < 3)
   {
      while (*token == ' ' || *token == '\t')
      {
         token++;
      }

      char* endptr;
      long value = strtol(token, &endptr, 10);

      if (endptr == token || value < 0 || value > INT_MAX)
      {
         pgmoneta_log_warn("Invalid version component '%s' in version string '%s'", token, version_str);
         goto error;
      }

      switch (part)
      {
         case 0:
            version->major = (int)value;
            break;
         case 1:
            version->minor = (int)value;
            break;
         case 2:
            version->patch = (int)value;
            break;
      }

      part++;
      token = strtok_r(NULL, ".", &saveptr);
   }

   free(str_copy);

   if (version->major == -1)
   {
      pgmoneta_log_error("No major version found in version string '%s'", version_str);
      goto error;
   }

   return 0;

error:
   if (str_copy)
   {
      free(str_copy);
   }
   return 1;
}

int
pgmoneta_version_to_string(struct version* version, char* buffer, size_t buffer_size)
{
   if (!version || !buffer || buffer_size == 0)
   {
      pgmoneta_log_error("Invalid parameters for version to string conversion");
      goto error;
   }

   int major = (version->major == -1) ? 0 : version->major;
   int minor = (version->minor == -1) ? 0 : version->minor;
   int patch = (version->patch == -1) ? 0 : version->patch;

   int result;

   if (version->patch != -1)
   {
      result = snprintf(buffer, buffer_size, "%d.%d.%d", major, minor, patch);
   }
   else if (version->minor != -1)
   {
      result = snprintf(buffer, buffer_size, "%d.%d", major, minor);
   }
   else
   {
      result = snprintf(buffer, buffer_size, "%d", major);
   }

   if (result < 0)
   {
      pgmoneta_log_error("snprintf failed for version string");
      goto error;
   }

   if ((size_t)result >= buffer_size)
   {
      pgmoneta_log_error("Buffer too small for version string (need %d, have %zu)", result + 1, buffer_size);
      goto error;
   }

   return 0;

error:
   return 1;
}

int
pgmoneta_compare_versions(struct version* v1, struct version* v2)
{
   if (!v1 || !v2)
   {
      return 0;
   }

   int major1 = (v1->major == -1) ? 0 : v1->major;
   int major2 = (v2->major == -1) ? 0 : v2->major;

   if (major1 != major2)
   {
      return (major1 > major2) ? 1 : -1;
   }

   int minor1 = (v1->minor == -1) ? 0 : v1->minor;
   int minor2 = (v2->minor == -1) ? 0 : v2->minor;

   if (minor1 != minor2)
   {
      return (minor1 > minor2) ? 1 : -1;
   }

   int patch1 = (v1->patch == -1) ? 0 : v1->patch;
   int patch2 = (v2->patch == -1) ? 0 : v2->patch;

   if (patch1 != patch2)
   {
      return (patch1 > patch2) ? 1 : -1;
   }

   return 0;
}

int
pgmoneta_extension_is_installed(int server, const char* extension_name)
{
   struct main_configuration* config;

   if (!extension_name)
   {
      return 0;
   }

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < config->common.servers[server].number_of_extensions; i++)
   {
      if (strcmp(config->common.servers[server].extensions[i].name, extension_name) == 0 &&
          config->common.servers[server].extensions[i].enabled)
      {
         return 1;
      }
   }

   return 0;
}

struct extension_info*
pgmoneta_get_extension_info(int server, const char* extension_name)
{
   struct main_configuration* config;

   if (!extension_name)
   {
      return NULL;
   }

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < config->common.servers[server].number_of_extensions; i++)
   {
      if (strcmp(config->common.servers[server].extensions[i].name, extension_name) == 0)
      {
         return &config->common.servers[server].extensions[i];
      }
   }

   return NULL;
}
