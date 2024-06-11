/*
 * Copyright (C) 2024 The pgmoneta community
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
#include <hashmap.h>
#include <info.h>
#include <logging.h>
#include <string.h>
#include <utils.h>
#include <security.h>
#include <storage.h>
#include <io.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libssh/libssh.h>
#include <libssh/sftp.h>

static int ssh_storage_setup(int, char*, struct node*, struct node**);
static int ssh_storage_backup_execute(int, char*, struct node*, struct node**);
static int ssh_storage_wal_shipping_execute(int, char*, struct node*, struct node**);
static int ssh_storage_backup_teardown(int, char*, struct node*, struct node**);
static int ssh_storage_wal_shipping_teardown(int, char*, struct node*, struct node**);

static char* get_remote_server_basepath(int server);
static char* get_remote_server_backup(int server);
static char* get_remote_server_backup_identifier(int server, char* identifier);
static char* get_remote_server_wal(int server);

static int read_latest_backup_sha256(char* path);

static int sftp_make_directory(char* local_dir, char* remote_dir);
static int sftp_copy_directory(char* local_root, char* remote_root, char* relative_path);
static int sftp_copy_file(char* local_root, char* remote_root, char* relative_path);
static int sftp_wal_prepare(sftp_file* file, int segsize);
static bool sftp_exists(char* path);
static int sftp_get_file_size(char* file_path, size_t* file_size);
static int sftp_permission(char* path, int user, int group, int all);

static ssh_session session = NULL;
static sftp_session sftp = NULL;

static struct hashmap* hash_map = NULL;

static bool is_error = false;

static char** file_paths = NULL;
static char** hashes = NULL;
static char* latest_remote_root = NULL;

struct workflow*
pgmoneta_storage_create_ssh(int workflow_type)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->setup = &ssh_storage_setup;

   switch (workflow_type)
   {
      case WORKFLOW_TYPE_BACKUP:
         wf->execute = &ssh_storage_backup_execute;
         wf->teardown = &ssh_storage_backup_teardown;
         break;
      case WORKFLOW_TYPE_WAL_SHIPPING:
         wf->execute = &ssh_storage_wal_shipping_execute;
         wf->teardown = &ssh_storage_wal_shipping_teardown;
         break;
      default:
         break;
   }
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

   if (strlen(config->ssh_ciphers) == 0)
   {
      ssh_options_set(session, SSH_OPTIONS_CIPHERS_C_S, "aes256-ctr,aes192-ctr,aes128-ctr");
   }
   else
   {
      ssh_options_set(session, SSH_OPTIONS_CIPHERS_C_S, config->ssh_ciphers);
   }

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
ssh_storage_backup_execute(int server, char* identifier,
                           struct node* i_nodes, struct node** o_nodes)
{
   char* server_path = NULL;
   char* local_root = NULL;
   char* remote_root = NULL;
   char* latest_backup_sha256 = NULL;
   int next_newest = -1;
   int number_of_backups = 0;
   struct backup** backups = NULL;

   remote_root = get_remote_server_backup_identifier(server, identifier);

   local_root = pgmoneta_get_server_backup_identifier(server, identifier);

   if (sftp_make_directory(local_root, remote_root) == 1)
   {
      pgmoneta_log_error("could not create the backup directory: %s in the remote server: %s", remote_root, strerror(errno));
      goto error;
   }

   server_path = pgmoneta_get_server_backup(server);

   pgmoneta_get_backups(server_path, &number_of_backups, &backups);

   if (number_of_backups >= 2)
   {
      for (int j = number_of_backups - 2; j >= 0 && next_newest == -1; j--)
      {
         if (backups[j]->valid == VALID_TRUE)
         {
            if (next_newest == -1)
            {
               next_newest = j;
            }
         }
      }
   }

   if (pgmoneta_hashmap_create(16384, &hash_map))
   {
      goto error;
   }

   if (next_newest != -1)
   {
      latest_remote_root = get_remote_server_backup_identifier(server, backups[next_newest]->label);

      latest_backup_sha256 = pgmoneta_get_server_backup_identifier(server, backups[next_newest]->label);
      latest_backup_sha256 = pgmoneta_append(latest_backup_sha256, "backup.sha256");

      if (read_latest_backup_sha256(latest_backup_sha256))
      {
         goto error;
      }
   }

   sftp_copy_file(local_root, remote_root, "/backup.info");
   sftp_copy_file(local_root, remote_root, "/backup.sha256");

   local_root = pgmoneta_append(local_root, "/data");
   remote_root = pgmoneta_append(remote_root, "/data");

   if (sftp_copy_directory(local_root, remote_root, "") != 0)
   {
      pgmoneta_log_error("failed to transfer the backup directory from the local host to the remote server: %s", strerror(errno));
      goto error;
   }

   is_error = false;

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   if (latest_backup_sha256 != NULL)
   {
      free(latest_backup_sha256);
   }

   free(server_path);
   free(remote_root);
   free(local_root);

   return 0;

error:

   is_error = true;

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   if (latest_backup_sha256 != NULL)
   {
      free(latest_backup_sha256);
   }

   free(server_path);
   free(remote_root);
   free(local_root);

   return 1;
}

static int
ssh_storage_wal_shipping_execute(int server, char* identifier,
                                 struct node* i_nodes, struct node** o_nodes)
{
   char* local_root = NULL;
   char* remote_root = NULL;

   remote_root = get_remote_server_wal(server);
   local_root = pgmoneta_get_server_wal(server);

   if (sftp_make_directory(local_root, remote_root) == 1)
   {
      pgmoneta_log_error("could not create the wal-shipping directory: %s in the remote server: %s", remote_root, ssh_get_error(session));
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
ssh_storage_backup_teardown(int server, char* identifier,
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

   pgmoneta_hashmap_destroy(hash_map);
   free(hash_map);

   free(root);

   free(latest_remote_root);

   sftp_free(sftp);

   ssh_free(session);

   return 0;
}

static int
ssh_storage_wal_shipping_teardown(int server, char* identifier,
                                  struct node* i_nodes, struct node** o_nodes)
{
   sftp_free(sftp);

   ssh_free(session);

   return 0;
}
static int
sftp_make_directory(char* local_dir, char* remote_dir)
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
sftp_copy_directory(char* local_root, char* remote_root, char* relative_path)
{
   char* from = NULL;
   char* to = NULL;
   char* relative_file;
   int rc;
   DIR* dir;
   struct dirent* entry;
   mode_t mode = 0;

   from = pgmoneta_append(from, local_root);
   from = pgmoneta_append(from, relative_path);

   to = pgmoneta_append(to, remote_root);
   to = pgmoneta_append(to, relative_path);

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
         char relative_dir[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(relative_dir, sizeof(relative_dir), "%s/%s", relative_path, entry->d_name);

         sftp_copy_directory(local_root, remote_root, relative_dir);
      }
      else
      {
         relative_file = NULL;

         relative_file = pgmoneta_append(relative_file, relative_path);
         relative_file = pgmoneta_append(relative_file, "/");
         relative_file = pgmoneta_append(relative_file, entry->d_name);

         if (sftp_copy_file(local_root, remote_root, relative_file))
         {
            free(relative_file);
            goto error;
         }

         free(relative_file);
      }
   }

   closedir(dir);

   free(from);
   free(to);

   return 0;

error:

   closedir(dir);

   free(from);
   free(to);

   return 1;
}

static int
sftp_copy_file(char* local_root, char* remote_root, char* relative_path)
{
   char* s = NULL;
   char* d = NULL;
   char* sha256 = NULL;
   char* latest_sha256 = NULL;
   char* latest_backup_path = NULL;
   char buffer[16384];
   FILE* sfile = NULL;
   sftp_file dfile = NULL;
   unsigned long read_bytes = 0;
   mode_t mode = 0;
   bool is_link = false;

   s = pgmoneta_append(s, local_root);
   s = pgmoneta_append(s, relative_path);

   d = pgmoneta_append(d, remote_root);
   d = pgmoneta_append(d, relative_path);

   pgmoneta_create_sha256_file(s, &sha256);

   if (latest_remote_root != NULL)
   {
      latest_backup_path = pgmoneta_append(latest_backup_path, latest_remote_root);
      latest_backup_path = pgmoneta_append(latest_backup_path, relative_path);

      if (pgmoneta_hashmap_contains_key(hash_map, relative_path))
      {
         latest_sha256 = pgmoneta_hashmap_get(hash_map, relative_path);

         if (!strcmp(latest_sha256, sha256))
         {
            is_link = true;
         }
      }
   }

   if (is_link)
   {
      if (sftp_symlink(sftp, latest_backup_path, d) < 0)
      {
         pgmoneta_log_error("Failed to link remotely: %s", ssh_get_error(session));
         goto error;
      }
   }
   else
   {
      mode = pgmoneta_get_permission(s);

      sfile = pgmoneta_open_file(s, "rb");

      if (sfile == NULL)
      {
         goto error;
      }

      dfile = sftp_open(sftp, d, O_WRONLY | O_CREAT | O_TRUNC, mode);

      if (dfile == NULL)
      {
         goto error;
      }

      memset(buffer, 0, sizeof(buffer));

      while ((read_bytes = fread(buffer, 1, sizeof(buffer), sfile)) > 0)
      {
         sftp_write(dfile, buffer, read_bytes);
      }
   }

   if (sfile != NULL)
   {
      fclose(sfile);
   }

   if (dfile != NULL)
   {
      sftp_close(dfile);
   }

   free(s);
   free(d);
   free(sha256);

   if (latest_backup_path != NULL)
   {
      free(latest_backup_path);
   }

   return 0;

error:

   if (sfile != NULL)
   {
      fclose(sfile);
   }

   if (dfile != NULL)
   {
      sftp_close(dfile);
   }

   free(s);
   free(d);
   free(sha256);

   if (latest_backup_path != NULL)
   {
      free(latest_backup_path);
   }

   return 1;
}

static int
sftp_wal_prepare(sftp_file* file, int segsize)
{
   char buffer[8192] = {0};
   size_t written = 0;

   if (file == NULL || *file == NULL)
   {
      return 1;
   }

   while (written < segsize)
   {
      written += sftp_write(*file, buffer, sizeof(buffer));
   }

   if (sftp_seek(*file, 0) < 0)
   {
      pgmoneta_log_error("WAL error: %s", ssh_get_error(session));
      return 1;
   }
   return 0;
}

static int
read_latest_backup_sha256(char* path)
{
   char buffer[4096];
   int n = 0;
   int lines = 0;
   FILE* file = NULL;

   file = pgmoneta_open_file(path, "r");
   if (file == NULL)
   {
      goto error;
   }

   while ((fgets(&buffer[0], sizeof(buffer), file)) != NULL)
   {
      lines++;
   }

   fclose(file);

   file = pgmoneta_open_file(path, "r");

   file_paths = (char**)malloc(sizeof(char*) * lines);

   if (file_paths == NULL)
   {
      goto error;
   }

   hashes = (char**)malloc(sizeof(char*) * lines);

   if (hashes == NULL)
   {
      goto error;
   }

   memset(&buffer[0], 0, sizeof(buffer));

   while ((fgets(&buffer[0], sizeof(buffer), file)) != NULL)
   {
      char* ptr = NULL;
      ptr = strtok(&buffer[0], ":");

      if (ptr == NULL)
      {
         goto error;
      }

      file_paths[n] = (char*)malloc(strlen(ptr) + 1);

      if (file_paths[n] == NULL)
      {
         goto error;
      }

      memset(file_paths[n], 0, strlen(ptr) + 1);
      memcpy(file_paths[n], ptr, strlen(ptr));

      ptr = strtok(NULL, ":");

      hashes[n] = (char*)malloc(strlen(ptr));

      if (hashes[n] == NULL)
      {
         goto error;
      }

      memset(hashes[n], 0, strlen(ptr));
      memcpy(hashes[n], ptr, strlen(ptr) - 1);

      if (pgmoneta_hashmap_put(hash_map, file_paths[n], hashes[n]))
      {
         goto error;
      }

      n++;
   }

   fclose(file);

   return 0;

error:

   if (file != NULL)
   {
      fclose(file);
   }

   return 1;
}

static char*
get_remote_server_basepath(int server)
{
   char* d = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = pgmoneta_append(d, config->ssh_base_dir);
   if (!pgmoneta_ends_with(config->ssh_base_dir, "/"))
   {
      d = pgmoneta_append(d, "/");
   }
   d = pgmoneta_append(d, config->servers[server].name);
   d = pgmoneta_append(d, "/");

   return d;
}

static char*
get_remote_server_backup(int server)
{
   char* d = NULL;

   d = get_remote_server_basepath(server);
   d = pgmoneta_append(d, "backup/");

   return d;
}

static char*
get_remote_server_backup_identifier(int server, char* identifier)
{
   char* d = NULL;

   d = get_remote_server_backup(server);
   d = pgmoneta_append(d, identifier);

   return d;
}

static char*
get_remote_server_wal(int server)
{
   char* d = NULL;

   d = get_remote_server_basepath(server);
   d = pgmoneta_append(d, "wal/");

   return d;
}

int
pgmoneta_sftp_wal_open(int server, char* filename, int segsize, sftp_file* file)
{

   char* root = NULL;
   char* path = NULL;

   root = get_remote_server_wal(server);

   if (root == NULL || strlen(root) == 0 || !sftp_exists(root))
   {
      goto error;
   }

   path = pgmoneta_append(path, root);
   if (!pgmoneta_ends_with(path, "/"))
   {
      path = pgmoneta_append(path, "/");
   }

   path = pgmoneta_append(path, filename);
   path = pgmoneta_append(path, ".partial");

   if (sftp_exists(path))
   {
      // file alreay exists, check if it's padded already
      size_t size = 0;
      sftp_get_file_size(path, &size);
      if (size == segsize)
      {
         *file = sftp_open(sftp, path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
         if (*file == NULL)
         {
            pgmoneta_log_error("WAL error: %s", ssh_get_error(session));
            goto error;
         }
         sftp_permission(path, 6, 0, 0);

         free(path);
         return 0;
      }
      if (size != 0)
      {
         // corrupted file
         pgmoneta_log_error("WAL file corrupted: %s", path);
         goto error;
      }
   }

   *file = sftp_open(sftp, path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);

   if (*file == NULL)
   {
      pgmoneta_log_error("WAL error: %s", ssh_get_error(session));
      goto error;
   }

   if (sftp_wal_prepare(file, segsize))
   {
      goto error;
   }

   free(path);
   return 0;

error:
   if (*file != NULL)
   {
      sftp_close(*file);
   }
   free(path);
   return 1;
}

int
pgmoneta_sftp_wal_close(int server, char* filename, bool partial, sftp_file* file)
{

   char* root = NULL;
   char tmp_file_path[MAX_PATH] = {0};
   char file_path[MAX_PATH] = {0};

   root = get_remote_server_wal(server);

   if (file == NULL || *file == NULL || root == NULL || filename == NULL || strlen(root) == 0 || strlen(filename) == 0)
   {
      return 1;
   }

   if (partial)
   {
      pgmoneta_log_warn("Not renaming %s.partial, this segment is incomplete", filename);
      sftp_close(*file);
      return 0;
   }

   if (pgmoneta_ends_with(root, "/"))
   {
      snprintf(tmp_file_path, sizeof(tmp_file_path), "%s%s.partial", root, filename);
      snprintf(file_path, sizeof(file_path), "%s%s", root, filename);
   }
   else
   {
      snprintf(tmp_file_path, sizeof(tmp_file_path), "%s/%s.partial", root, filename);
      snprintf(file_path, sizeof(file_path), "%s/%s", root, filename);
   }
   if (sftp_rename(sftp, tmp_file_path, file_path) != 0)
   {
      pgmoneta_log_error("could not rename file %s to %s", tmp_file_path, file_path);
      goto error;
   }

   sftp_close(*file);

   return 0;

error:
   sftp_close(*file);
   return 1;
}
static bool
sftp_exists(char* path)
{
   if (sftp_stat(sftp, path) != NULL)
   {
      return true;
   }
   return false;
}

static int
sftp_get_file_size(char* file_path, size_t* file_size)
{
   sftp_file file = NULL;
   sftp_attributes attributes = NULL;

   if ((file = sftp_open(sftp, file_path, O_RDONLY, 0)) == NULL)
   {
      pgmoneta_log_error("Failed to open file: %s : %s", file_path, ssh_get_error(session));
      goto error;
   }

   if ((attributes = sftp_fstat(file)) == NULL)
   {
      pgmoneta_log_error("Error retrieving file attributes: %s : %s", file_path, ssh_get_error(session));
      goto error;
   }

   *file_size = attributes->size;

   sftp_attributes_free(attributes);
   sftp_close(file);

   return 0;

error:
   if (file != NULL)
   {
      sftp_close(file);
   }
   if (attributes != NULL)
   {
      sftp_attributes_free(attributes);
   }
   return 1;
}

static int
sftp_permission(char* path, int user, int group, int all)
{
   int ret;
   mode_t mode;
   pgmoneta_get_permission_mode(user, group, all, &mode);
   ret = sftp_chmod(sftp, path, mode);
   if (ret != 0)
   {
      return 1;
   }

   return 0;
}
