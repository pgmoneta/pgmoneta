/*
 * Copyright (C) 2022 Red Hat
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <logging.h>
#include <stdio.h>
#include <utils.h>
#include <storage.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libssh/libssh.h>
#include <libssh/sftp.h>

static int ssh_storage_setup(int, char*, struct node*, struct node**);
static int ssh_storage_execute(int, char*, struct node*, struct node**);
static int ssh_storage_teardown(int, char*, struct node*, struct node**);

static int sftp_make_dir(char* local_dir, char* remote_dir);
static int sftp_copy_directory(char* from, char* to);
static int sftp_copy_file(char* from, char* to);

static ssh_session session = NULL;
static sftp_session sftp = NULL;
static bool is_error = false;

struct workflow*
pgmoneta_storage_create_ssh(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &ssh_storage_setup;
   wf->execute = &ssh_storage_execute;
   wf->teardown = &ssh_storage_teardown;
   wf->next = NULL;

   return wf;
}

static int
ssh_storage_setup(int server, char* identifier, struct node* i_nodes,
                  struct node** o_nodes)
{
   ssh_key srv_pubkey = NULL;
   ssh_key client_pubkey = NULL;
   ssh_key client_privkey = NULL;
   char* pubkey_path = NULL;
   char* privkey_path = NULL;
   char* pubkey_full_path = NULL;
   char* privkey_full_path = NULL;
   char* homedir = NULL;
   char* hexa = NULL;
   unsigned char* srv_pubkey_hash = NULL;
   size_t hash_length;
   int rc;
   enum ssh_known_hosts_e state;
   struct configuration* config;

   config = (struct configuration*)shmem;

   homedir = getenv("HOME");
   pubkey_path = "/.ssh/id_rsa.pub";
   privkey_path = "/.ssh/id_rsa";

   session = ssh_new();

   if (session == NULL)
   {
      goto error;
   }

   ssh_options_set(session, SSH_OPTIONS_USER, config->ssh_username);
   ssh_options_set(session, SSH_OPTIONS_HOST, config->ssh_hostname);

   rc = ssh_connect(session);
   if (rc != SSH_OK)
   {
      pgmoneta_log_error("Remote Backup: Error connecting to %s: %s\n",
                         config->ssh_hostname, ssh_get_error(session));
      goto error;
   }

   rc = ssh_get_server_publickey(session, &srv_pubkey);
   if (rc < 0)
   {
      goto error;
   }

   rc = ssh_get_publickey_hash(srv_pubkey, SSH_PUBLICKEY_HASH_SHA1,
                               &srv_pubkey_hash, &hash_length);
   if (rc < 0)
   {
      goto error;
   }

   state = ssh_session_is_known_server(session);
   switch (state)
   {
      case SSH_KNOWN_HOSTS_OK:
         break;
      case SSH_KNOWN_HOSTS_CHANGED:
         pgmoneta_log_error("the server key has changed: %s", strerror(errno));
         goto error;
      case SSH_KNOWN_HOSTS_OTHER:
         pgmoneta_log_error("the host key for this server was not found: %s", strerror(errno));
         goto error;
      case SSH_KNOWN_HOSTS_NOT_FOUND:
         pgmoneta_log_error("could not find known host file: %s", strerror(errno));
         goto error;
      case SSH_KNOWN_HOSTS_UNKNOWN:
         rc = ssh_session_update_known_hosts(session);
         if (rc < 0)
         {
            pgmoneta_log_error("could not update known_hosts file: %s", strerror(errno));
            goto error;
         }
         break;
      case SSH_KNOWN_HOSTS_ERROR:
         pgmoneta_log_error("error checking the host: %s", strerror(errno));
         goto error;
   }

   pubkey_full_path = pgmoneta_append(pubkey_full_path, homedir);
   pubkey_full_path = pgmoneta_append(pubkey_full_path, pubkey_path);

   rc = ssh_pki_import_pubkey_file(pubkey_full_path, &client_pubkey);
   if (rc != SSH_OK)
   {
      pgmoneta_log_error("could not import host's public key: %s", strerror(errno));
      goto error;
   }

   privkey_full_path = pgmoneta_append(privkey_full_path, homedir);
   privkey_full_path = pgmoneta_append(privkey_full_path, privkey_path);

   rc = ssh_pki_import_privkey_file(privkey_full_path, NULL, NULL, NULL,
                                    &client_privkey);
   if (rc != SSH_OK)
   {
      pgmoneta_log_error("could not import host's private key: %s", strerror(errno));
      goto error;
   }

   rc = ssh_userauth_publickey(session, NULL, client_privkey);
   if (rc != SSH_AUTH_SUCCESS)
   {
      pgmoneta_log_error("could not authenticate with public/private key: %s", strerror(errno));
      goto error;
   }

   sftp = sftp_new(session);

   if (sftp == NULL)
   {
      pgmoneta_log_error("Error: %s\n", ssh_get_error(session));
      goto error;
   }

   rc = sftp_init(sftp);
   if (rc != SSH_OK)
   {
      pgmoneta_log_error("Error: %s\n", sftp_get_error(sftp));
      goto error;
   }

   is_error = false;

   ssh_string_free_char(hexa);
   ssh_clean_pubkey_hash(&srv_pubkey_hash);
   ssh_key_free(srv_pubkey);
   ssh_key_free(client_pubkey);
   ssh_key_free(client_privkey);

   free(pubkey_full_path);
   free(privkey_full_path);

   return 0;

error:

   is_error = true;

   ssh_string_free_char(hexa);
   ssh_clean_pubkey_hash(&srv_pubkey_hash);
   ssh_key_free(srv_pubkey);
   ssh_key_free(client_pubkey);
   ssh_key_free(client_privkey);

   free(pubkey_full_path);
   free(privkey_full_path);

   sftp_free(sftp);

   ssh_disconnect(session);
   ssh_free(session);
   return 1;
}

static int
ssh_storage_execute(int server, char* identifier,
                    struct node* i_nodes, struct node** o_nodes)
{
   char* local_root = NULL;
   char* remote_root = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   remote_root = pgmoneta_append(remote_root, config->ssh_base_dir);
   if (!pgmoneta_ends_with(config->ssh_base_dir, "/"))
   {
      remote_root = pgmoneta_append(remote_root, "/");
   }

   remote_root = pgmoneta_append(remote_root, config->servers[server].name);
   remote_root = pgmoneta_append(remote_root, "/backup/");
   remote_root = pgmoneta_append(remote_root, identifier);

   local_root = pgmoneta_get_server_backup_identifier(server, identifier);

   if (sftp_make_dir(local_root, remote_root) == 1)
   {
      pgmoneta_log_error("could not create the backup directory: %s in the remote server: %s", remote_root, strerror(errno));
      goto error;
   }

   if (sftp_copy_directory(local_root, remote_root) != 0)
   {
      pgmoneta_log_error("failed to transfer the backup directory from the local host to the remote server: %s", strerror(errno));
      goto error;
   }

   is_error = false;

   free(remote_root);

   free(local_root);

   return 0;

error:

   is_error = true;

   free(remote_root);

   free(local_root);

   return 1;
}

static int
ssh_storage_teardown(int server, char* identifier,
                     struct node* i_nodes, struct node** o_nodes)
{
   char* root = NULL;

   if (!is_error)
   {
      root = pgmoneta_get_server_backup_identifier_data(server, identifier);
   }
   else
   {
      root = pgmoneta_get_server_backup_identifier(server, identifier);
   }

   pgmoneta_delete_directory(root);

   free(root);

   sftp_free(sftp);

   ssh_free(session);

   return 0;
}

static int
sftp_make_dir(char* local_dir, char* remote_dir)
{
   int rc;
   char* p;
   mode_t mode = 0;

   mode = pgmoneta_get_permission(local_dir);

   for (p = remote_dir + 1; *p; p++)
   {
      if (*p == '/')
      {
         *p = '\0';

         rc = sftp_mkdir(sftp, remote_dir, mode);
         if (rc != SSH_OK)
         {
            if (sftp_get_error(sftp) != SSH_FX_FILE_ALREADY_EXISTS)
            {
               pgmoneta_log_error("could not create the directory: %s in the remote server: %s", remote_dir, strerror(errno));
               goto error;
            }
         }

         *p = '/';
      }
   }

   rc = sftp_mkdir(sftp, remote_dir, mode);
   if (rc != SSH_OK)
   {
      if (sftp_get_error(sftp) != SSH_FX_FILE_ALREADY_EXISTS)
      {
         pgmoneta_log_error("could not create the directory: %s in the remote server: %s", remote_dir, strerror(errno));
         goto error;
      }
   }

   return 0;

error:
   return 1;
}

static int
sftp_copy_directory(char* from, char* to)
{
   char* from_file;
   char* to_file;
   int rc;
   DIR* dir;
   struct dirent* entry;
   mode_t mode = 0;

   if (!(dir = opendir(from)))
   {
      goto error;
   }

   mode = pgmoneta_get_permission(from);

   rc = sftp_mkdir(sftp, to, mode);
   if (rc != SSH_OK)
   {
      if (sftp_get_error(sftp) != SSH_FX_FILE_ALREADY_EXISTS)
      {
         goto error;
      }
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         char from_dir[1024];
         char to_dir[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(from_dir, sizeof(from_dir), "%s/%s", from, entry->d_name);
         snprintf(to_dir, sizeof(to_dir), "%s/%s", to, entry->d_name);

         sftp_copy_directory(from_dir, to_dir);
      }
      else
      {
         from_file = NULL;
         to_file = NULL;

         from_file = pgmoneta_append(from_file, from);
         from_file = pgmoneta_append(from_file, "/");
         from_file = pgmoneta_append(from_file, entry->d_name);

         to_file = pgmoneta_append(to_file, to);
         to_file = pgmoneta_append(to_file, "/");
         to_file = pgmoneta_append(to_file, entry->d_name);

         sftp_copy_file(from_file, to_file);

         free(from_file);
         free(to_file);
      }
   }

   closedir(dir);

   return 0;

error:

   closedir(dir);

   return 1;
}

static int
sftp_copy_file(char* s, char* d)
{
   char buffer[16384];
   FILE* sfile = NULL;
   sftp_file dfile = NULL;
   unsigned long read_bytes = 0;
   mode_t mode = 0;

   mode = pgmoneta_get_permission(s);

   sfile = fopen(s, "rb");

   if (sfile == NULL)
   {
      return 1;
   }

   dfile = sftp_open(sftp, d, O_WRONLY | O_CREAT | O_TRUNC, mode);

   if (dfile == NULL)
   {
      fclose(sfile);
      return 1;
   }

   memset(buffer, 0, sizeof(buffer));

   while ((read_bytes = fread(buffer, 1, sizeof(buffer), sfile)) > 0)
   {
      sftp_write(dfile, buffer, read_bytes);
   }

   fclose(sfile);
   sftp_close(dfile);

   return 0;
}
