/*
 * Copyright (C) 2022 Red Hat
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
#include <info.h>
#include <logging.h>
#include <network.h>
#include <management.h>
#include <utils.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#define MANAGEMENT_HEADER_SIZE 1

static int read_int32(char* prefix, int socket, int* i);
static int read_int64(char* prefix, int socket, unsigned long* l);
static int read_string(char* prefix, int socket, char** str);
static int write_int32(char* prefix, int socket, int i);
static int write_int64(char* prefix, int socket, unsigned long l);
static int write_string(char* prefix, int socket, char* str);
static int read_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_socket(int socket, void* buf, size_t size);
static int write_ssl(SSL* ssl, void* buf, size_t size);
static int write_header(SSL* ssl, int fd, signed char type);

int
pgmoneta_management_read_header(int socket, signed char* id)
{
   char header[MANAGEMENT_HEADER_SIZE];

   if (read_complete(NULL, socket, &header[0], sizeof(header)))
   {
      errno = 0;
      goto error;
   }

   *id = pgmoneta_read_byte(&(header));

   return 0;

error:

   *id = -1;

   return 1;
}

int
pgmoneta_management_read_payload(int socket, signed char id, char** payload_s1, char** payload_s2, char** payload_s3, char** payload_s4)
{
   *payload_s1 = NULL;
   *payload_s2 = NULL;
   *payload_s3 = NULL;
   *payload_s4 = NULL;

   switch (id)
   {
      case MANAGEMENT_BACKUP:
      case MANAGEMENT_LIST_BACKUP:
         read_string("pgmoneta_management_read_payload", socket, payload_s1);
         break;
      case MANAGEMENT_RESTORE:
      case MANAGEMENT_ARCHIVE:
         read_string("pgmoneta_management_read_payload", socket, payload_s1);
         read_string("pgmoneta_management_read_payload", socket, payload_s2);
         read_string("pgmoneta_management_read_payload", socket, payload_s3);
         read_string("pgmoneta_management_read_payload", socket, payload_s4);
         break;
      case MANAGEMENT_DELETE:
      case MANAGEMENT_RETAIN:
      case MANAGEMENT_EXPUNGE:
         read_string("pgmoneta_management_read_payload", socket, payload_s1);
         read_string("pgmoneta_management_read_payload", socket, payload_s2);
         break;
      case MANAGEMENT_STOP:
      case MANAGEMENT_STATUS:
      case MANAGEMENT_DETAILS:
      case MANAGEMENT_RESET:
      case MANAGEMENT_RELOAD:
         break;
      default:
         goto error;
         break;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_backup(SSL* ssl, int socket, char* server)
{
   if (write_header(ssl, socket, MANAGEMENT_BACKUP))
   {
      pgmoneta_log_warn("pgmoneta_management_backup: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_backup", socket, server))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_list_backup(SSL* ssl, int socket, char* server)
{
   if (write_header(ssl, socket, MANAGEMENT_LIST_BACKUP))
   {
      pgmoneta_log_warn("pgmoneta_management_list_backup: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_list_backup", socket, server))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_list_backup(SSL* ssl, int socket, char* server)
{
   char* name = NULL;
   int srv;
   int number_of_backups;
   int keep;
   int valid;
   unsigned long backup_size;
   unsigned long restore_size;
   unsigned long wal_size;
   unsigned long delta_size;
   char* bck = NULL;
   char* res = NULL;
   char* ws = NULL;
   char* ds = NULL;

   if (read_int32("pgmoneta_management_read_list_backup", socket, &srv))
   {
      goto error;
   }

   printf("Server           : %s\n", (srv == -1 ? "Unknown" : server));

   if (srv != -1)
   {
      if (read_int32("pgmoneta_management_read_list_backup", socket, &number_of_backups))
      {
         goto error;
      }

      printf("Number of backups: %d\n", number_of_backups);

      if (number_of_backups > 0)
      {
         printf("Backup           :\n");
         for (int i = 0; i < number_of_backups; i++)
         {
            if (read_string("pgmoneta_management_read_list_backup", socket, &name))
            {
               goto error;
            }

            if (read_int32("pgmoneta_management_read_list_backup", socket, &keep))
            {
               goto error;
            }

            if (read_int32("pgmoneta_management_read_list_backup", socket, &valid))
            {
               goto error;
            }

            if (read_int64("pgmoneta_management_read_list_backup", socket, &backup_size))
            {
               goto error;
            }

            bck = pgmoneta_bytes_to_string(backup_size);

            if (read_int64("pgmoneta_management_read_list_backup", socket, &restore_size))
            {
               goto error;
            }

            res = pgmoneta_bytes_to_string(restore_size);

            if (read_int64("pgmoneta_management_read_list_backup", socket, &wal_size))
            {
               goto error;
            }

            ws = pgmoneta_bytes_to_string(wal_size);

            if (read_int64("pgmoneta_management_read_list_backup", socket, &delta_size))
            {
               goto error;
            }

            ds = pgmoneta_bytes_to_string(delta_size);

            if (valid == VALID_UNKNOWN)
            {
               printf("                   %s (Unknown)\n", &name[0]);
            }
            else
            {
               printf("                   %s (Backup: %s Restore: %s WAL: %s Delta: %s Retain: %s Valid: %s)\n",
                      &name[0], bck, res, ws, ds, keep ? "Yes" : "No", valid == VALID_TRUE ? "Yes" : "No");
            }

            free(bck);
            bck = NULL;

            free(res);
            res = NULL;

            free(ws);
            ws = NULL;

            free(ds);
            ds = NULL;

            free(name);
            name = NULL;
         }
      }
   }

   return 0;

error:

   free(bck);
   free(res);
   free(ws);
   free(ds);
   free(name);

   return 1;
}

int
pgmoneta_management_write_list_backup(int socket, int server)
{
   char* d = NULL;
   char* wal_dir = NULL;
   int number_of_backups;
   struct backup** backups = NULL;
   int nob;
   unsigned long wal;
   unsigned long delta;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (write_int32("pgmoneta_management_write_list_backup", socket, server))
   {
      goto error;
   }

   if (server != -1)
   {
      d = pgmoneta_get_server_backup(server);
      wal_dir = pgmoneta_get_server_wal(server);

      if (pgmoneta_get_backups(d, &number_of_backups, &backups))
      {
         write_int32("pgmoneta_management_write_list_backup", socket, 0);
         goto error;
      }

      nob = 0;
      for (int i = 0; i < number_of_backups; i++)
      {
         if (backups[i] != NULL)
         {
            nob++;
         }
      }

      if (write_int32("pgmoneta_management_write_list_backup", socket, nob))
      {
         goto error;
      }

      for (int i = 0; i < number_of_backups; i++)
      {
         if (backups[i] != NULL)
         {
            if (write_string("pgmoneta_management_write_list_backup", socket, backups[i]->label))
            {
               goto error;
            }

            if (write_int32("pgmoneta_management_write_list_backup", socket, backups[i]->keep ? 1 : 0))
            {
               goto error;
            }

            if (write_int32("pgmoneta_management_write_list_backup", socket, backups[i]->valid))
            {
               goto error;
            }

            if (write_int64("pgmoneta_management_write_list_backup", socket, backups[i]->backup_size))
            {
               goto error;
            }

            if (write_int64("pgmoneta_management_write_list_backup", socket, backups[i]->restore_size))
            {
               goto error;
            }

            wal = pgmoneta_number_of_wal_files(wal_dir, &backups[i]->wal[0], NULL);
            wal *= config->servers[server].wal_size;

            if (write_int64("pgmoneta_management_write_list_backup", socket, wal))
            {
               goto error;
            }

            delta = 0;
            if (i > 0)
            {
               delta = pgmoneta_number_of_wal_files(wal_dir, &backups[i - 1]->wal[0], &backups[i]->wal[0]);
               delta *= config->servers[server].wal_size;
            }

            if (write_int64("pgmoneta_management_write_list_backup", socket, delta))
            {
               goto error;
            }
         }
      }

      for (int i = 0; i < number_of_backups; i++)
      {
         free(backups[i]);
      }
      free(backups);
   }

   free(d);
   free(wal_dir);

   return 0;

error:

   free(d);
   free(wal_dir);

   return 1;
}

int
pgmoneta_management_restore(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory)
{
   if (write_header(ssl, socket, MANAGEMENT_RESTORE))
   {
      pgmoneta_log_warn("pgmoneta_management_restore: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_restore", socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_restore", socket, backup_id))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_restore", socket, position))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_restore", socket, directory))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_archive(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory)
{
   if (write_header(ssl, socket, MANAGEMENT_ARCHIVE))
   {
      pgmoneta_log_warn("pgmoneta_management_archive: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_archive", socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_archive", socket, backup_id))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_archive", socket, position))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_archive", socket, directory))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_delete(SSL* ssl, int socket, char* server, char* backup_id)
{
   if (write_header(ssl, socket, MANAGEMENT_DELETE))
   {
      pgmoneta_log_warn("pgmoneta_management_delete: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_delete", socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_delete", socket, backup_id))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_delete(SSL* ssl, int socket, char* server, char* backup_id)
{
   char* name = NULL;
   int srv;
   int number_of_backups;

   if (read_int32("pgmoneta_management_read_delete", socket, &srv))
   {
      goto error;
   }

   printf("Server           : %s\n", (srv == -1 ? "Unknown" : server));

   if (srv != -1)
   {
      if (read_int32("pgmoneta_management_read_delete", socket, &number_of_backups))
      {
         goto error;
      }

      printf("Number of backups: %d\n", number_of_backups);

      if (number_of_backups > 0)
      {
         printf("Backup           :\n");
         for (int i = 0; i < number_of_backups; i++)
         {
            if (read_string("pgmoneta_management_read_delete", socket, &name))
            {
               goto error;
            }

            printf("                   %s\n", name);

            free(name);
            name = NULL;
         }
      }
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_write_delete(int socket, int server, int result)
{
   char* d = NULL;
   int number_of_backups = 0;
   char** array = NULL;

   if (write_int32("pgmoneta_management_write_delete", socket, server))
   {
      goto error;
   }

   if (server != -1)
   {
      d = pgmoneta_get_server_backup(server);

      if (pgmoneta_get_directories(d, &number_of_backups, &array))
      {
         write_int32("pgmoneta_management_write_delete", socket, 0);
         goto error;
      }

      if (write_int32("pgmoneta_management_write_delete", socket, number_of_backups))
      {
         goto error;
      }

      for (int i = 0; i < number_of_backups; i++)
      {
         if (write_string("pgmoneta_management_write_delete", socket, array[i]))
         {
            goto error;
         }
      }
   }

   for (int i = 0; i < number_of_backups; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   return 1;
}

int
pgmoneta_management_stop(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_STOP))
   {
      pgmoneta_log_warn("pgmoneta_management_stop: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_status(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_STATUS))
   {
      pgmoneta_log_warn("pgmoneta_management_status: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_status(SSL* ssl, int socket)
{
   char* name = NULL;
   unsigned long used_size;
   unsigned long free_size;
   unsigned long total_size;
   unsigned long server_size;
   char* size_string;
   int retention;
   int link;
   int servers;
   int number_of_directories;

   if (read_int64("pgmoneta_management_read_status", socket, &used_size))
   {
      goto error;
   }

   size_string = pgmoneta_bytes_to_string(used_size);
   printf("Used space       : %s\n", size_string);
   free(size_string);

   if (read_int64("pgmoneta_management_read_status", socket, &free_size))
   {
      goto error;
   }

   size_string = pgmoneta_bytes_to_string(free_size);
   printf("Free space       : %s\n", size_string);
   free(size_string);

   if (read_int64("pgmoneta_management_read_status", socket, &total_size))
   {
      goto error;
   }

   size_string = pgmoneta_bytes_to_string(total_size);
   printf("Total space      : %s\n", size_string);
   free(size_string);

   if (read_int32("pgmoneta_management_read_status", socket, &link))
   {
      goto error;
   }

   printf("Link             : %s\n", link == 1 ? "Yes" : "No");

   if (read_int32("pgmoneta_management_read_status", socket, &servers))
   {
      goto error;
   }

   printf("Number of servers: %d\n", servers);

   for (int i = 0; i < servers; i++)
   {
      if (read_int32("pgmoneta_management_read_status", socket, &retention))
      {
         goto error;
      }

      if (read_int64("pgmoneta_management_read_status", socket, &server_size))
      {
         goto error;
      }

      size_string = pgmoneta_bytes_to_string(server_size);

      if (read_int32("pgmoneta_management_read_status", socket, &number_of_directories))
      {
         goto error;
      }

      if (read_string("pgmoneta_management_read_status", socket, &name))
      {
         goto error;
      }

      printf("Server           : %s\n", &name[0]);
      printf("  Retention      : %d days\n", retention);
      printf("  Backups        : %d\n", number_of_directories);
      printf("  Space          : %s\n", size_string);

      free(size_string);
      size_string = NULL;

      free(name);
      name = NULL;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_write_status(int socket)
{
   char* d = NULL;
   int retention;
   unsigned long used_size;
   unsigned long free_size;
   unsigned long total_size;
   int number_of_directories = 0;
   char** array = NULL;
   unsigned long server_size;
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = NULL;
   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");

   used_size = pgmoneta_directory_size(d);

   free(d);
   d = NULL;

   free_size = pgmoneta_free_space(config->base_dir);
   total_size = pgmoneta_total_space(config->base_dir);

   if (write_int64("pgmoneta_management_write_status", socket, used_size))
   {
      goto error;
   }

   if (write_int64("pgmoneta_management_write_status", socket, free_size))
   {
      goto error;
   }

   if (write_int64("pgmoneta_management_write_status", socket, total_size))
   {
      goto error;
   }

   if (write_int32("pgmoneta_management_write_status", socket, config->link ? 1 : 0))
   {
      goto error;
   }

   if (write_int32("pgmoneta_management_write_status", socket, config->number_of_servers))
   {
      goto error;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      retention = config->servers[i].retention;
      if (retention <= 0)
      {
         retention = config->retention;
      }

      if (write_int32("pgmoneta_management_write_status", socket, retention))
      {
         goto error;
      }

      d = pgmoneta_get_server(i);

      server_size = pgmoneta_directory_size(d);

      if (write_int64("pgmoneta_management_write_status", socket, server_size))
      {
         goto error;
      }

      free(d);
      d = NULL;

      d = pgmoneta_get_server_backup(i);

      pgmoneta_get_directories(d, &number_of_directories, &array);

      if (write_int32("pgmoneta_management_write_status", socket, number_of_directories))
      {
         goto error;
      }

      if (write_string("pgmoneta_management_write_status", socket, config->servers[i].name))
      {
         goto error;
      }

      for (int j = 0; j < number_of_directories; j++)
      {
         free(array[j]);
      }
      free(array);
      array = NULL;

      free(d);
      d = NULL;
   }

   return 0;

error:

   for (int i = 0; i < number_of_directories; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

   return 1;
}

int
pgmoneta_management_details(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_DETAILS))
   {
      pgmoneta_log_warn("pgmoneta_management_details: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_details(SSL* ssl, int socket)
{
   char* name = NULL;
   unsigned long used_size;
   unsigned long free_size;
   unsigned long total_size;
   unsigned long server_size;
   char* size_string;
   int retention;
   int link;
   int servers;
   int number_of_backups;
   unsigned long backup_size;
   unsigned long restore_size;
   unsigned long wal_size;
   unsigned long delta_size;
   int keep;
   int valid;
   char* bck = NULL;
   char* res = NULL;
   char* ws = NULL;
   char* ds = NULL;

   if (read_int64("pgmoneta_management_read_details", socket, &used_size))
   {
      goto error;
   }

   size_string = pgmoneta_bytes_to_string(used_size);
   printf("Used space       : %s\n", size_string);
   free(size_string);

   if (read_int64("pgmoneta_management_read_details", socket, &free_size))
   {
      goto error;
   }

   size_string = pgmoneta_bytes_to_string(free_size);
   printf("Free space       : %s\n", size_string);
   free(size_string);

   if (read_int64("pgmoneta_management_read_details", socket, &total_size))
   {
      goto error;
   }

   size_string = pgmoneta_bytes_to_string(total_size);
   printf("Total space      : %s\n", size_string);
   free(size_string);

   if (read_int32("pgmoneta_management_read_details", socket, &link))
   {
      goto error;
   }

   printf("Link             : %s\n", link == 1 ? "Yes" : "No");

   if (read_int32("pgmoneta_management_read_details", socket, &servers))
   {
      goto error;
   }

   printf("Number of servers: %d\n", servers);

   for (int i = 0; i < servers; i++)
   {
      if (read_string("pgmoneta_management_read_details", socket, &name))
      {
         goto error;
      }

      if (read_int32("pgmoneta_management_read_details", socket, &retention))
      {
         goto error;
      }

      if (read_int64("pgmoneta_management_read_details", socket, &server_size))
      {
         goto error;
      }

      size_string = pgmoneta_bytes_to_string(server_size);

      if (read_int32("pgmoneta_management_read_details", socket, &number_of_backups))
      {
         goto error;
      }

      printf("Server           : %s\n", name);
      printf("  Retention      : %d days\n", retention);
      printf("  Backups        : %d\n", number_of_backups);

      free(name);
      name = NULL;

      for (int j = 0; j < number_of_backups; j++)
      {
         if (read_string("pgmoneta_management_read_details", socket, &name))
         {
            goto error;
         }

         if (read_int32("pgmoneta_management_read_details", socket, &keep))
         {
            goto error;
         }

         if (read_int32("pgmoneta_management_read_details", socket, &valid))
         {
            goto error;
         }

         if (read_int64("pgmoneta_management_read_details", socket, &backup_size))
         {
            goto error;
         }

         bck = pgmoneta_bytes_to_string(backup_size);

         if (read_int64("pgmoneta_management_read_details", socket, &restore_size))
         {
            goto error;
         }

         res = pgmoneta_bytes_to_string(restore_size);

         if (read_int64("pgmoneta_management_read_details", socket, &wal_size))
         {
            goto error;
         }

         ws = pgmoneta_bytes_to_string(wal_size);

         if (read_int64("pgmoneta_management_read_details", socket, &delta_size))
         {
            goto error;
         }

         ds = pgmoneta_bytes_to_string(delta_size);

         if (valid == VALID_UNKNOWN)
         {
            printf("                   %s (Unknown)\n", name);
         }
         else
         {
            printf("                   %s (Backup: %s Restore: %s WAL: %s Delta: %s Retain: %s Valid: %s)\n",
                   name, bck, res, ws, ds, keep ? "Yes" : "No", valid == VALID_TRUE ? "Yes" : "No");
         }

         free(bck);
         bck = NULL;

         free(res);
         res = NULL;

         free(ws);
         ws = NULL;

         free(ds);
         ds = NULL;

         free(name);
         name = NULL;
      }

      printf("  Space          : %s\n", size_string);

      free(size_string);
      size_string = NULL;

      free(name);
      name = NULL;
   }

   return 0;

error:

   free(bck);
   free(res);
   free(ws);
   free(ds);
   free(name);

   return 1;
}

int
pgmoneta_management_write_details(int socket)
{
   char* d = NULL;
   char* wal_dir = NULL;
   int retention;
   unsigned long used_size;
   unsigned long free_size;
   unsigned long total_size;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   unsigned long server_size;
   unsigned long wal;
   unsigned long delta;
   struct configuration* config;

   config = (struct configuration*)shmem;

   d = NULL;
   d = pgmoneta_append(d, config->base_dir);
   d = pgmoneta_append(d, "/");

   used_size = pgmoneta_directory_size(d);

   free(d);
   d = NULL;

   free_size = pgmoneta_free_space(config->base_dir);
   total_size = pgmoneta_total_space(config->base_dir);

   if (write_int64("pgmoneta_management_write_details", socket, used_size))
   {
      goto error;
   }

   if (write_int64("pgmoneta_management_write_details", socket, free_size))
   {
      goto error;
   }

   if (write_int64("pgmoneta_management_write_details", socket, total_size))
   {
      goto error;
   }

   if (write_int32("pgmoneta_management_write_details", socket, config->link ? 1 : 0))
   {
      goto error;
   }

   if (write_int32("pgmoneta_management_write_details", socket, config->number_of_servers))
   {
      goto error;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      wal_dir = pgmoneta_get_server_wal(i);

      if (write_string("pgmoneta_management_write_details", socket, config->servers[i].name))
      {
         goto error;
      }

      retention = config->servers[i].retention;
      if (retention <= 0)
      {
         retention = config->retention;
      }

      if (write_int32("pgmoneta_management_write_details", socket, retention))
      {
         goto error;
      }

      d = pgmoneta_get_server(i);

      server_size = pgmoneta_directory_size(d);

      if (write_int64("pgmoneta_management_write_details", socket, server_size))
      {
         goto error;
      }

      free(d);
      d = NULL;

      d = pgmoneta_get_server_backup(i);

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (write_int32("pgmoneta_management_write_details", socket, number_of_backups))
      {
         goto error;
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         if (backups[j] != NULL)
         {
            if (write_string("pgmoneta_management_write_details", socket, backups[j]->label))
            {
               goto error;
            }

            if (write_int32("pgmoneta_management_write_details", socket, backups[j]->keep ? 1 : 0))
            {
               goto error;
            }

            if (write_int32("pgmoneta_management_write_details", socket, backups[j]->valid))
            {
               goto error;
            }

            if (write_int64("pgmoneta_management_write_details", socket, backups[j]->backup_size))
            {
               goto error;
            }

            if (write_int64("pgmoneta_management_write_details", socket, backups[j]->restore_size))
            {
               goto error;
            }

            wal = pgmoneta_number_of_wal_files(wal_dir, &backups[j]->wal[0], NULL);
            wal *= config->servers[i].wal_size;

            if (write_int64("pgmoneta_management_write_details", socket, wal))
            {
               goto error;
            }

            delta = 0;
            if (j > 0)
            {
               delta = pgmoneta_number_of_wal_files(wal_dir, &backups[j - 1]->wal[0], &backups[j]->wal[0]);
               delta *= config->servers[i].wal_size;
            }

            if (write_int64("pgmoneta_management_write_details", socket, delta))
            {
               goto error;
            }
         }
      }

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);
      backups = NULL;

      free(d);
      d = NULL;

      free(wal_dir);
      wal_dir = NULL;
   }

   return 0;

error:

   for (int i = 0; i < number_of_backups; i++)
   {
      free(backups[i]);
   }
   free(backups);

   free(d);

   return 1;
}

int
pgmoneta_management_isalive(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_ISALIVE))
   {
      pgmoneta_log_warn("pgmoneta_management_isalive: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_isalive(SSL* ssl, int socket, int* status)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_read_isalive: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *status = pgmoneta_read_int32(&buf);

   return 0;

error:

   return 1;
}

int
pgmoneta_management_write_isalive(int socket)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   pgmoneta_write_int32(buf, 1);

   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_write_isalive: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_reset(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_RESET))
   {
      pgmoneta_log_warn("pgmoneta_management_reset: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_reload(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_RELOAD))
   {
      pgmoneta_log_warn("pgmoneta_management_reload: write: %d", socket);
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_retain(SSL* ssl, int socket, char* server, char* backup_id)
{
   if (write_header(ssl, socket, MANAGEMENT_RETAIN))
   {
      pgmoneta_log_warn("pgmoneta_management_retain: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_retain", socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_retain", socket, backup_id))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_expunge(SSL* ssl, int socket, char* server, char* backup_id)
{
   if (write_header(ssl, socket, MANAGEMENT_EXPUNGE))
   {
      pgmoneta_log_warn("pgmoneta_management_expunge: write: %d", socket);
      errno = 0;
      goto error;
   }

   if (write_string("pgmoneta_management_expunge", socket, server))
   {
      goto error;
   }

   if (write_string("pgmoneta_management_expunge", socket, backup_id))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_int32(SSL* ssl, int socket, int* status)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_read_int32: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *status = pgmoneta_read_int32(&buf);

   return 0;

error:

   return 1;
}

int
pgmoneta_management_write_int32(int socket, int code)
{
   char buf[4];

   memset(&buf, 0, sizeof(buf));

   pgmoneta_write_int32(buf, code);

   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_write_int32: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
read_int32(char* prefix, int socket, int* i)
{
   char buf4[4] = {0};

   *i = 0;

   if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *i = pgmoneta_read_int32(&buf4);

   return 0;

error:

   return 1;
}

static int
read_int64(char* prefix, int socket, unsigned long* l)
{
   char buf8[8] = {0};

   *l = 0;

   if (read_complete(NULL, socket, &buf8[0], sizeof(buf8)))
   {
      pgmoneta_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *l = pgmoneta_read_int64(&buf8);

   return 0;

error:

   return 1;
}

static int
read_string(char* prefix, int socket, char** str)
{
   char* s = NULL;
   char buf4[4] = {0};
   int size;

   *str = NULL;

   if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   size = pgmoneta_read_int32(&buf4);
   if (size > 0)
   {
      s = malloc(size + 1);
      memset(s, 0, size + 1);

      if (read_complete(NULL, socket, s, size))
      {
         pgmoneta_log_warn("%s: read: %d %s", prefix, socket, strerror(errno));
         errno = 0;
         goto error;
      }

      *str = s;
   }

   return 0;

error:

   return 1;
}

static int
write_int32(char* prefix, int socket, int i)
{
   char buf4[4] = {0};

   pgmoneta_write_int32(&buf4, i);
   if (write_complete(NULL, socket, &buf4, sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
write_int64(char* prefix, int socket, unsigned long l)
{
   char buf8[8] = {0};

   pgmoneta_write_int64(&buf8, l);
   if (write_complete(NULL, socket, &buf8, sizeof(buf8)))
   {
      pgmoneta_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
write_string(char* prefix, int socket, char* str)
{
   char buf4[4] = {0};

   pgmoneta_write_int32(&buf4, str != NULL ? strlen(str) : 0);
   if (write_complete(NULL, socket, &buf4, sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (str != NULL)
   {
      if (write_complete(NULL, socket, str, strlen(str)))
      {
         pgmoneta_log_warn("%s: write: %d %s", prefix, socket, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
read_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   ssize_t r;
   size_t offset;
   size_t needs;
   int retries;

   offset = 0;
   needs = size;
   retries = 0;

read:
   if (ssl == NULL)
   {
      r = read(socket, buf + offset, needs);
   }
   else
   {
      r = SSL_read(ssl, buf + offset, needs);
   }

   if (r == -1)
   {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
         errno = 0;
         goto read;
      }

      goto error;
   }
   else if (r < needs)
   {
      /* Sleep for 10ms */
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 10000000L;
      nanosleep(&ts, NULL);

      if (retries < 100)
      {
         offset += r;
         needs -= r;
         retries++;
         goto read;
      }
      else
      {
         errno = EINVAL;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
write_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   if (ssl == NULL)
   {
      return write_socket(socket, buf, size);
   }

   return write_ssl(ssl, buf, size);
}

static int
write_socket(int socket, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = write(socket, buf + offset, remaining);

      if (likely(numbytes == size))
      {
         return 0;
      }
      else if (numbytes != -1)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == size)
         {
            return 0;
         }

         pgmoneta_log_trace("Write %d - %zd/%zd vs %zd", socket, numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         switch (errno)
         {
            case EAGAIN:
               keep_write = true;
               errno = 0;
               break;
            default:
               keep_write = false;
               break;
         }
      }
   }
   while (keep_write);

   return 1;
}

static int
write_ssl(SSL* ssl, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = SSL_write(ssl, buf + offset, remaining);

      if (likely(numbytes == size))
      {
         return 0;
      }
      else if (numbytes > 0)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == size)
         {
            return 0;
         }

         pgmoneta_log_trace("SSL/Write %d - %zd/%zd vs %zd", SSL_get_fd(ssl), numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         int err = SSL_get_error(ssl, numbytes);

         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
#endif
#endif
               errno = 0;
               keep_write = true;
               break;
            case SSL_ERROR_SYSCALL:
               pgmoneta_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
            case SSL_ERROR_SSL:
               pgmoneta_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
         }
         ERR_clear_error();

         if (!keep_write)
         {
            return 1;
         }
      }
   }
   while (keep_write);

   return 1;
}

static int
write_header(SSL* ssl, int socket, signed char type)
{
   char header[MANAGEMENT_HEADER_SIZE];

   pgmoneta_write_byte(&(header), type);

   return write_complete(ssl, socket, &(header), MANAGEMENT_HEADER_SIZE);
}
