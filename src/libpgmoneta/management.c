/*
 * Copyright (C) 2021 Red Hat
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

#define MANAGEMENT_HEADER_SIZE 5


static int read_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_socket(int socket, void* buf, size_t size);
static int write_ssl(SSL* ssl, void* buf, size_t size);
static int write_header(SSL* ssl, int fd, signed char type, int ns);

int
pgmoneta_management_read_header(int socket, signed char* id, int* ns)
{
   char header[MANAGEMENT_HEADER_SIZE];

   if (read_complete(NULL, socket, &header[0], sizeof(header)))
   {
      errno = 0;
      goto error;
   }

   *id = pgmoneta_read_byte(&(header));
   *ns = pgmoneta_read_int32(&(header[1]));
   
   return 0;

error:

   *id = -1;
   *ns = 0;

   return 1;
}

int
pgmoneta_management_read_payload(int socket, signed char id, int ns, char** payload_s1, char** payload_s2, char** payload_s3)
{
   char* s1 = NULL;
   char* s2 = NULL;
   char* s3 = NULL;
   char buf4[4];
   int size;

   *payload_s1 = NULL;
   *payload_s2 = NULL;
   *payload_s3 = NULL;

   switch (id)
   {
      case MANAGEMENT_BACKUP:
      case MANAGEMENT_LIST_BACKUP:
         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }
         size = pgmoneta_read_int32(&buf4);

         s1 = malloc(size + 1);
         memset(s1, 0, size + 1);
         if (read_complete(NULL, socket, s1, size))
         {
            goto error;
         }
         *payload_s1 = s1;
         break;
      case MANAGEMENT_RESTORE:
         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }

         size = pgmoneta_read_int32(&buf4);
         s1 = malloc(size + 1);
         memset(s1, 0, size + 1);
         if (read_complete(NULL, socket, s1, size))
         {
            goto error;
         }
         *payload_s1 = s1;

         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }

         size = pgmoneta_read_int32(&buf4);
         s2 = malloc(size + 1);
         memset(s2, 0, size + 1);
         if (read_complete(NULL, socket, s2, size))
         {
            goto error;
         }
         *payload_s2 = s2;

         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }

         size = pgmoneta_read_int32(&buf4);
         s3 = malloc(size + 1);
         memset(s3, 0, size + 1);
         if (read_complete(NULL, socket, s3, size))
         {
            goto error;
         }
         *payload_s3 = s3;

         break;
      case MANAGEMENT_DELETE:
         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }

         size = pgmoneta_read_int32(&buf4);
         s1 = malloc(size + 1);
         memset(s1, 0, size + 1);
         if (read_complete(NULL, socket, s1, size))
         {
            goto error;
         }
         *payload_s1 = s1;

         if (read_complete(NULL, socket, &buf4[0], sizeof(buf4)))
         {
            goto error;
         }

         size = pgmoneta_read_int32(&buf4);
         s2 = malloc(size + 1);
         memset(s2, 0, size + 1);
         if (read_complete(NULL, socket, s2, size))
         {
            goto error;
         }
         *payload_s2 = s2;

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
   char buf[4];

   if (write_header(ssl, socket, MANAGEMENT_BACKUP, 1))
   {
      pgmoneta_log_warn("pgmoneta_management_backup: write: %d", socket);
      errno = 0;
      goto error;
   }

   pgmoneta_write_int32(&buf, strlen(server));
   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_backup: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, socket, server, strlen(server)))
   {
      pgmoneta_log_warn("pgmoneta_management_backup: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_list_backup(SSL* ssl, int socket, char* server)
{
   char buf[4];

   if (write_header(ssl, socket, MANAGEMENT_LIST_BACKUP, 1))
   {
      pgmoneta_log_warn("pgmoneta_management_list_backup: write: %d", socket);
      errno = 0;
      goto error;
   }

   pgmoneta_write_int32(&buf, strlen(server));
   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_list_backup: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, socket, server, strlen(server)))
   {
      pgmoneta_log_warn("pgmoneta_management_list_backup: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_list_backup(SSL* ssl, int socket, char* server)
{
   char buf[4];
   char name[MISC_LENGTH];
   int srv;
   int number_of_directories;
   int length;

   memset(&buf, 0, sizeof(buf));

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_read_list_backup: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   srv = pgmoneta_read_int32(&buf);
   printf("Server           : %s\n", (srv == -1 ? "Unknown" : server));

   if (srv != -1)
   {
      if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
      {
         pgmoneta_log_warn("pgmoneta_management_read_list_backup: read: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      number_of_directories = pgmoneta_read_int32(&buf);
      printf("Number of backups: %d\n", number_of_directories);

      if (number_of_directories > 0)
      {
         printf("Backup           :\n");
         for (int i = 0; i < number_of_directories; i++)
         {
            if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
            {
               pgmoneta_log_warn("pgmoneta_management_read_list_backup: read: %d %s", socket, strerror(errno));
               errno = 0;
               goto error;
            }

            length = pgmoneta_read_int32(&buf);

            memset(&name[0], 0, sizeof(name));
            
            if (read_complete(ssl, socket, &name[0], length))
            {
               pgmoneta_log_warn("pgmoneta_management_read_list_backup: read: %d %s", socket, strerror(errno));
               errno = 0;
               goto error;
            }

            printf("                   %s\n", &name[0]);
         }
      }
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_write_list_backup(int socket, int server)
{
   char buf[4];
   char* d = NULL;
   int number_of_directories;
   DIR *dir = NULL;
   char** array = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_write_int32(&buf, server);
   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_write_list_backup: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (server != -1)
   {
      d = NULL;
      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[server].name);
      d = pgmoneta_append(d, "/backup/");

      if (pgmoneta_get_directories(d, &number_of_directories, &array))
      {
         pgmoneta_write_int32(&buf, 0);
         if (write_complete(NULL, socket, &buf, sizeof(buf)))
         {
            pgmoneta_log_warn("pgmoneta_management_write_list_backup: write: %d %s", socket, strerror(errno));
            errno = 0;
         }
         goto error;
      }

      pgmoneta_write_int32(&buf, number_of_directories);
      if (write_complete(NULL, socket, &buf, sizeof(buf)))
      {
         pgmoneta_log_warn("pgmoneta_management_write_list_backup: write: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      for (int i = 0; i < number_of_directories; i++)
      {
         pgmoneta_write_int32(&buf, strlen(array[i]));
         if (write_complete(NULL, socket, &buf, sizeof(buf)))
         {
            pgmoneta_log_warn("pgmoneta_management_write_list_backup: write: %d %s", socket, strerror(errno));
            errno = 0;
            goto error;
         }

         if (write_complete(NULL, socket, array[i], strlen(array[i])))
         {
            pgmoneta_log_warn("pgmoneta_management_list_backup: write: %d %s", socket, strerror(errno));
            errno = 0;
            goto error;
         }
      }

      for (int i = 0; i < number_of_directories; i++)
      {
         free(array[i]);
      }
      free(array);
   }

   free(d);

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   free(d);

   return 1;
}

int
pgmoneta_management_restore(SSL* ssl, int socket, char* server, char* backup_id, char* directory)
{
   char buf[4];

   if (write_header(ssl, socket, MANAGEMENT_RESTORE, 3))
   {
      pgmoneta_log_warn("pgmoneta_management_restore: write: %d", socket);
      errno = 0;
      goto error;
   }

   pgmoneta_write_int32(&buf, strlen(server));
   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_restore: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, socket, server, strlen(server)))
   {
      pgmoneta_log_warn("pgmoneta_management_restore: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   pgmoneta_write_int32(&buf, strlen(backup_id));
   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_restore: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, socket, backup_id, strlen(backup_id)))
   {
      pgmoneta_log_warn("pgmoneta_management_restore: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   pgmoneta_write_int32(&buf, strlen(directory));
   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_restore: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, socket, directory, strlen(directory)))
   {
      pgmoneta_log_warn("pgmoneta_management_restore: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_delete(SSL* ssl, int socket, char* server, char* backup_id)
{
   char buf[4];

   if (write_header(ssl, socket, MANAGEMENT_DELETE, 2))
   {
      pgmoneta_log_warn("pgmoneta_management_delete: write: %d", socket);
      errno = 0;
      goto error;
   }

   pgmoneta_write_int32(&buf, strlen(server));
   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_delete: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, socket, server, strlen(server)))
   {
      pgmoneta_log_warn("pgmoneta_management_delete: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   pgmoneta_write_int32(&buf, strlen(backup_id));
   if (write_complete(ssl, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_delete: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (write_complete(ssl, socket, backup_id, strlen(backup_id)))
   {
      pgmoneta_log_warn("pgmoneta_management_delete: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_delete(SSL* ssl, int socket, char* server, char* backup_id)
{
   char buf[4];
   char name[MISC_LENGTH];
   int srv;
   int number_of_directories;
   int length;

   memset(&buf, 0, sizeof(buf));

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_read_list_backup: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   srv = pgmoneta_read_int32(&buf);
   printf("Server           : %s\n", (srv == -1 ? "Unknown" : server));

   if (srv != -1)
   {
      if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
      {
         pgmoneta_log_warn("pgmoneta_management_read_list_backup: read: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      number_of_directories = pgmoneta_read_int32(&buf);
      printf("Number of backups: %d\n", number_of_directories);

      if (number_of_directories > 0)
      {
         printf("Backup           :\n");
         for (int i = 0; i < number_of_directories; i++)
         {
            if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
            {
               pgmoneta_log_warn("pgmoneta_management_read_list_backup: read: %d %s", socket, strerror(errno));
               errno = 0;
               goto error;
            }

            length = pgmoneta_read_int32(&buf);

            memset(&name[0], 0, sizeof(name));
            
            if (read_complete(ssl, socket, &name[0], length))
            {
               pgmoneta_log_warn("pgmoneta_management_read_list_backup: read: %d %s", socket, strerror(errno));
               errno = 0;
               goto error;
            }

            printf("                   %s\n", &name[0]);
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
   char buf[4];
   char* d = NULL;
   int number_of_directories;
   char** array = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_write_int32(&buf, server);
   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_write_delete: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (server != -1)
   {
      d = NULL;
      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[server].name);
      d = pgmoneta_append(d, "/backup/");

      if (pgmoneta_get_directories(d, &number_of_directories, &array))
      {
         pgmoneta_write_int32(&buf, 0);
         if (write_complete(NULL, socket, &buf, sizeof(buf)))
         {
            pgmoneta_log_warn("pgmoneta_management_write_list_backup: write: %d %s", socket, strerror(errno));
            errno = 0;
         }
         goto error;
      }

      pgmoneta_write_int32(&buf, number_of_directories);
      if (write_complete(NULL, socket, &buf, sizeof(buf)))
      {
         pgmoneta_log_warn("pgmoneta_management_write_list_backup: write: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      for (int i = 0; i < number_of_directories; i++)
      {
         pgmoneta_write_int32(&buf, strlen(array[i]));
         if (write_complete(NULL, socket, &buf, sizeof(buf)))
         {
            pgmoneta_log_warn("pgmoneta_management_write_list_backup: write: %d %s", socket, strerror(errno));
            errno = 0;
            goto error;
         }

         if (write_complete(NULL, socket, array[i], strlen(array[i])))
         {
            pgmoneta_log_warn("pgmoneta_management_list_backup: write: %d %s", socket, strerror(errno));
            errno = 0;
            goto error;
         }
      }
   }

   for (int i = 0; i < number_of_directories; i++)
   {
      free(array[i]);
   }
   free(array);

   free(d);

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
pgmoneta_management_stop(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_STOP, 0))
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
   if (write_header(ssl, socket, MANAGEMENT_STATUS, 0))
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
   char buf[4];
   char name[MISC_LENGTH];
   int servers;
   int number_of_directories;
   int length;

   memset(&buf, 0, sizeof(buf));

   if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_read_status: read: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   servers = pgmoneta_read_int32(&buf);
   printf("Number of servers: %d\n", servers);

   for (int i = 0; i < servers; i++)
   {
      if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
      {
         pgmoneta_log_warn("pgmoneta_management_read_status: read: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      number_of_directories = pgmoneta_read_int32(&buf);

      if (read_complete(ssl, socket, &buf[0], sizeof(buf)))
      {
         pgmoneta_log_warn("pgmoneta_management_read_status: read: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      length = pgmoneta_read_int32(&buf);

      memset(&name[0], 0, sizeof(name));
            
      if (read_complete(ssl, socket, &name[0], length))
      {
         pgmoneta_log_warn("pgmoneta_management_read_status: read: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      printf("Server           : %s\n", &name[0]);
      printf("  Backups        : %d\n", number_of_directories);
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_write_status(int socket)
{
   char buf[4];
   char* d = NULL;
   int number_of_directories = 0;
   char** array = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_write_int32(&buf, config->number_of_servers);
   if (write_complete(NULL, socket, &buf, sizeof(buf)))
   {
      pgmoneta_log_warn("pgmoneta_management_write_status: write: %d %s", socket, strerror(errno));
      errno = 0;
      goto error;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      d = NULL;
      d = pgmoneta_append(d, config->base_dir);
      d = pgmoneta_append(d, "/");
      d = pgmoneta_append(d, config->servers[i].name);
      d = pgmoneta_append(d, "/backup/");

      array = NULL;

      pgmoneta_get_directories(d, &number_of_directories, &array);

      pgmoneta_write_int32(&buf, number_of_directories);
      if (write_complete(NULL, socket, &buf, sizeof(buf)))
      {
         pgmoneta_log_warn("pgmoneta_management_write_status: write: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      pgmoneta_write_int32(&buf, strlen(config->servers[i].name));
      if (write_complete(NULL, socket, &buf, sizeof(buf)))
      {
         pgmoneta_log_warn("pgmoneta_management_write_status: write: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      if (write_complete(NULL, socket, config->servers[i].name, strlen(config->servers[i].name)))
      {
         pgmoneta_log_warn("pgmoneta_management_write_status: write: %d %s", socket, strerror(errno));
         errno = 0;
         goto error;
      }

      for (int i = 0; i < number_of_directories; i++)
      {
         free(array[i]);
      }
      free(array);

      free(d);
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
   if (write_header(ssl, socket, MANAGEMENT_DETAILS, 0))
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
   return pgmoneta_management_read_status(ssl, socket);
}

int
pgmoneta_management_write_details(int socket)
{
   return pgmoneta_management_write_status(socket);
}

int
pgmoneta_management_isalive(SSL* ssl, int socket)
{
   if (write_header(ssl, socket, MANAGEMENT_ISALIVE, 0))
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
   if (write_header(ssl, socket, MANAGEMENT_RESET, 0))
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
   if (write_header(ssl, socket, MANAGEMENT_RELOAD, 0))
   {
      pgmoneta_log_warn("pgmoneta_management_reload: write: %d", socket);
      errno = 0;
      goto error;
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

      pgmoneta_log_trace("Got: %ld, needs: %ld", r, needs);

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

         pgmoneta_log_debug("Write %d - %zd/%zd vs %zd", socket, numbytes, totalbytes, size);
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
   } while (keep_write);

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

         pgmoneta_log_debug("SSL/Write %d - %zd/%zd vs %zd", SSL_get_fd(ssl), numbytes, totalbytes, size);
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
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
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
   } while (keep_write);

   return 1;
}

static int
write_header(SSL* ssl, int socket, signed char type, int ns)
{
   char header[MANAGEMENT_HEADER_SIZE];

   pgmoneta_write_byte(&(header), type);
   pgmoneta_write_int32(&(header[1]), ns);

   return write_complete(ssl, socket, &(header), MANAGEMENT_HEADER_SIZE);
}
