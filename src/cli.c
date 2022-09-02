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
#include <configuration.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>

/* system */
#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <openssl/ssl.h>

#define ACTION_UNKNOWN      0
#define ACTION_BACKUP       1
#define ACTION_LIST_BACKUP  2
#define ACTION_RESTORE      3
#define ACTION_ARCHIVE      4
#define ACTION_DELETE       5
#define ACTION_STOP         6
#define ACTION_STATUS       7
#define ACTION_DETAILS      8
#define ACTION_ISALIVE      9
#define ACTION_RESET       10
#define ACTION_RELOAD      11
#define ACTION_RETAIN      12
#define ACTION_EXPUNGE     13
#define ACTION_HELP        99

static int find_action(int argc, char** argv, int* place);

static void help_backup(void);
static void help_list_backup(void);
static void help_restore(void);
static void help_archive(void);
static void help_delete(void);
static void help_retain(void);
static void help_expunge(void);

static int backup(SSL* ssl, int socket, char* server);
static int list_backup(SSL* ssl, int socket, char* server);
static int restore(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory);
static int archive(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory);
static int delete(SSL* ssl, int socket, char* server, char* backup_id);
static int stop(SSL* ssl, int socket);
static int status(SSL* ssl, int socket);
static int details(SSL* ssl, int socket);
static int isalive(SSL* ssl, int socket);
static int reset(SSL* ssl, int socket);
static int reload(SSL* ssl, int socket);
static int retain(SSL* ssl, int socket, char* server, char* backup_id);
static int expunge(SSL* ssl, int socket, char* server, char* backup_id);

static void
version(void)
{
   printf("pgmoneta-cli %s\n", VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgmoneta-cli %s\n", VERSION);
   printf("  Command line utility for pgmoneta\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgmoneta-cli [ -c CONFIG_FILE ] [ COMMAND ] \n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE Set the path to the pgmoneta.conf file\n");
   printf("  -h, --host HOST          Set the host name\n");
   printf("  -p, --port PORT          Set the port number\n");
   printf("  -U, --user USERNAME      Set the user name\n");
   printf("  -P, --password PASSWORD  Set the password\n");
   printf("  -L, --logfile FILE       Set the log file\n");
   printf("  -v, --verbose            Output text string of result\n");
   printf("  -V, --version            Display version information\n");
   printf("  -?, --help               Display help\n");
   printf("\n");
   printf("Commands:\n");
   printf("  backup                   Backup a server\n");
   printf("  list-backup              List the backups for a server\n");
   printf("  restore                  Restore a backup from a server\n");
   printf("  archive                  Archive a backup from a server\n");
   printf("  delete                   Delete a backup from a server\n");
   printf("  retain                   Retain a backup from a server\n");
   printf("  expunge                  Expunge a backup from a server\n");
   printf("  is-alive                 Is pgmoneta alive\n");
   printf("  stop                     Stop pgmoneta\n");
   printf("  status                   Status of pgmoneta\n");
   printf("  details                  Detailed status of pgmoneta\n");
   printf("  reload                   Reload the configuration\n");
   printf("  reset                    Reset the Prometheus statistics\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

int
main(int argc, char** argv)
{
   int socket = -1;
   SSL* s_ssl = NULL;
   int ret;
   int exit_code = 0;
   char* configuration_path = NULL;
   char* host = NULL;
   char* port = NULL;
   char* username = NULL;
   char* password = NULL;
   bool verbose = false;
   char* logfile = NULL;
   char* server = NULL;
   char* id = NULL;
   char* pos = NULL;
   char* dir = NULL;
   bool do_free = true;
   int c;
   int option_index = 0;
   size_t size;
   int position;
   int32_t action = ACTION_UNKNOWN;
   char un[MAX_USERNAME_LENGTH];
   struct configuration* config = NULL;

   while (1)
   {
      static struct option long_options[] =
      {
         {"config", required_argument, 0, 'c'},
         {"host", required_argument, 0, 'h'},
         {"port", required_argument, 0, 'p'},
         {"user", required_argument, 0, 'U'},
         {"password", required_argument, 0, 'P'},
         {"logfile", required_argument, 0, 'L'},
         {"verbose", no_argument, 0, 'v'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };

      c = getopt_long(argc, argv, "vV?c:h:p:U:P:L:",
                      long_options, &option_index);

      if (c == -1)
      {
         break;
      }

      switch (c)
      {
         case 'c':
            configuration_path = optarg;
            break;
         case 'h':
            host = optarg;
            break;
         case 'p':
            port = optarg;
            break;
         case 'U':
            username = optarg;
            break;
         case 'P':
            password = optarg;
            break;
         case 'L':
            logfile = optarg;
            break;
         case 'v':
            verbose = true;
            break;
         case 'V':
            version();
            break;
         case '?':
            usage();
            exit(1);
            break;
         default:
            break;
      }
   }

   if (getuid() == 0)
   {
      warnx("pgmoneta-cli: Using the root account is not allowed");
      exit(1);
   }

   if (configuration_path != NULL && (host != NULL || port != NULL))
   {
      warnx("pgmoneta-cli: Use either -c or -h/-p to define endpoint");
      exit(1);
   }

   if (argc <= 1)
   {
      usage();
      exit(1);
   }

   size = sizeof(struct configuration);
   if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      warnx("pgmoneta-cli: Error creating shared memory");
      exit(1);
   }
   pgmoneta_init_configuration(shmem);

   if (configuration_path != NULL)
   {
      ret = pgmoneta_read_configuration(shmem, configuration_path);
      if (ret)
      {
         warnx("pgmoneta-cli: Configuration not found: %s", configuration_path);
         exit(1);
      }

      if (logfile)
      {
         config = (struct configuration*)shmem;

         config->log_type = PGMONETA_LOGGING_TYPE_FILE;
         memset(&config->log_path[0], 0, MISC_LENGTH);
         memcpy(&config->log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
      }

      if (pgmoneta_start_logging())
      {
         exit(1);
      }

      config = (struct configuration*)shmem;
   }
   else
   {
      ret = pgmoneta_read_configuration(shmem, "/etc/pgmoneta/pgmoneta.conf");
      if (ret)
      {
         if (host == NULL || port == NULL)
         {
            warnx("pgmoneta-cli: Host and port must be specified");
            exit(1);
         }
      }
      else
      {
         configuration_path = "/etc/pgmoneta/pgmoneta.conf";

         if (logfile)
         {
            config = (struct configuration*)shmem;

            config->log_type = PGMONETA_LOGGING_TYPE_FILE;
            memset(&config->log_path[0], 0, MISC_LENGTH);
            memcpy(&config->log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
         }

         if (pgmoneta_start_logging())
         {
            exit(1);
         }

         config = (struct configuration*)shmem;
      }
   }

   if (argc > 0)
   {
      action = find_action(argc, argv, &position);

      if (action == ACTION_BACKUP)
      {
         if (argc == position + 2)
         {
            server = argv[argc - 1];
         }
         else
         {
            help_backup();
            action = ACTION_HELP;
         }
      }
      else if (action == ACTION_LIST_BACKUP)
      {
         if (argc == position + 2)
         {
            server = argv[argc - 1];
         }
         else
         {
            help_list_backup();
            action = ACTION_HELP;
         }
      }
      else if (action == ACTION_RESTORE)
      {
         if (argc == position + 5)
         {
            server = argv[argc - 4];
            id = argv[argc - 3];
            pos = argv[argc - 2];
            dir = argv[argc - 1];
         }
         else if (argc == position + 4)
         {
            server = argv[argc - 3];
            id = argv[argc - 2];
            dir = argv[argc - 1];
         }
         else
         {
            help_restore();
            action = ACTION_HELP;
         }
      }
      else if (action == ACTION_ARCHIVE)
      {
         if (argc == position + 5)
         {
            server = argv[argc - 4];
            id = argv[argc - 3];
            pos = argv[argc - 2];
            dir = argv[argc - 1];
         }
         else if (argc == position + 4)
         {
            server = argv[argc - 3];
            id = argv[argc - 2];
            dir = argv[argc - 1];
         }
         else
         {
            help_archive();
            action = ACTION_HELP;
         }
      }
      else if (action == ACTION_DELETE)
      {
         if (argc == position + 3)
         {
            server = argv[argc - 2];
            id = argv[argc - 1];
         }
         else
         {
            help_delete();
            action = ACTION_HELP;
         }
      }
      else if (action == ACTION_STOP)
      {
         /* Ok */
      }
      else if (action == ACTION_STATUS)
      {
         /* Ok */
      }
      else if (action == ACTION_DETAILS)
      {
         /* Ok */
      }
      else if (action == ACTION_ISALIVE)
      {
         /* Ok */
      }
      else if (action == ACTION_RESET)
      {
         /* Ok */
      }
      else if (action == ACTION_RELOAD)
      {
         /* Local connection only */
         if (configuration_path == NULL)
         {
            action = ACTION_UNKNOWN;
         }
      }
      else if (action == ACTION_RETAIN)
      {
         if (argc == position + 3)
         {
            server = argv[argc - 2];
            id = argv[argc - 1];
         }
         else
         {
            help_retain();
            action = ACTION_HELP;
         }
      }
      else if (action == ACTION_EXPUNGE)
      {
         if (argc == position + 3)
         {
            server = argv[argc - 2];
            id = argv[argc - 1];
         }
         else
         {
            help_expunge();
            action = ACTION_HELP;
         }
      }

      if (action != ACTION_UNKNOWN)
      {
         if (configuration_path != NULL)
         {
            /* Local connection */
            if (pgmoneta_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
            {
               exit_code = 1;
               goto done;
            }
         }
         else
         {
            /* Remote connection */
            if (pgmoneta_connect(host, atoi(port), &socket))
            {
               warnx("pgmoneta-cli: No route to host: %s:%s", host, port);
               goto done;
            }

            /* User name */
            if (username == NULL)
            {
username:
               printf("User name: ");

               memset(&un, 0, sizeof(un));
               if (fgets(&un[0], sizeof(un), stdin) == NULL)
               {
                  exit_code = 1;
                  goto done;
               }
               un[strlen(un) - 1] = 0;
               username = &un[0];
            }

            if (username == NULL || strlen(username) == 0)
            {
               goto username;
            }

            /* Password */
            if (password == NULL)
            {
password:
               if (password != NULL)
               {
                  free(password);
                  password = NULL;
               }

               printf("Password : ");
               password = pgmoneta_get_password();
               printf("\n");
            }
            else
            {
               do_free = false;
            }

            for (int i = 0; i < strlen(password); i++)
            {
               if ((unsigned char)(*(password + i)) & 0x80)
               {
                  goto password;
               }
            }

            /* Authenticate */
            if (pgmoneta_remote_management_scram_sha256(username, password, socket, &s_ssl) != AUTH_SUCCESS)
            {
               warnx("pgmoneta-cli: Bad credentials for %s", username);
               goto done;
            }
         }
      }

      if (action == ACTION_BACKUP)
      {
         exit_code = backup(s_ssl, socket, server);
      }
      else if (action == ACTION_LIST_BACKUP)
      {
         exit_code = list_backup(s_ssl, socket, server);
      }
      else if (action == ACTION_RESTORE)
      {
         exit_code = restore(s_ssl, socket, server, id, pos, dir);
      }
      else if (action == ACTION_ARCHIVE)
      {
         exit_code = archive(s_ssl, socket, server, id, pos, dir);
      }
      else if (action == ACTION_DELETE)
      {
         exit_code = delete(s_ssl, socket, server, id);
      }
      else if (action == ACTION_STOP)
      {
         exit_code = stop(s_ssl, socket);
      }
      else if (action == ACTION_STATUS)
      {
         exit_code = status(s_ssl, socket);
      }
      else if (action == ACTION_DETAILS)
      {
         exit_code = details(s_ssl, socket);
      }
      else if (action == ACTION_ISALIVE)
      {
         exit_code = isalive(s_ssl, socket);
      }
      else if (action == ACTION_RESET)
      {
         exit_code = reset(s_ssl, socket);
      }
      else if (action == ACTION_RELOAD)
      {
         exit_code = reload(s_ssl, socket);
      }
      else if (action == ACTION_RETAIN)
      {
         exit_code = retain(s_ssl, socket, server, id);
      }
      else if (action == ACTION_EXPUNGE)
      {
         exit_code = expunge(s_ssl, socket, server, id);
      }
   }

done:

   if (s_ssl != NULL)
   {
      int res;
      SSL_CTX* ctx = SSL_get_SSL_CTX(s_ssl);
      res = SSL_shutdown(s_ssl);
      if (res == 0)
      {
         SSL_shutdown(s_ssl);
      }
      SSL_free(s_ssl);
      SSL_CTX_free(ctx);
   }

   pgmoneta_disconnect(socket);

   if (action == ACTION_UNKNOWN)
   {
      usage();
      exit_code = 1;
   }

   if (configuration_path != NULL)
   {
      if (action == ACTION_HELP)
      {
         /* Ok */
      }
      else if (action != ACTION_UNKNOWN && exit_code != 0)
      {
         warnx("No connection to pgmoneta on %s", config->unix_socket_dir);
      }
   }

   pgmoneta_stop_logging();
   pgmoneta_destroy_shared_memory(shmem, size);

   if (do_free)
   {
      free(password);
   }

   if (verbose)
   {
      if (exit_code == 0)
      {
         printf("Success (0)\n");
      }
      else
      {
         printf("Error (%d)\n", exit_code);
      }
   }

   return exit_code;
}

static int
find_action(int argc, char** argv, int* place)
{
   *place = -1;

   for (int i = 1; i < argc; i++)
   {
      if (!strcmp("backup", argv[i]))
      {
         *place = i;
         return ACTION_BACKUP;
      }
      else if (!strcmp("list-backup", argv[i]))
      {
         *place = i;
         return ACTION_LIST_BACKUP;
      }
      else if (!strcmp("restore", argv[i]))
      {
         *place = i;
         return ACTION_RESTORE;
      }
      else if (!strcmp("archive", argv[i]))
      {
         *place = i;
         return ACTION_ARCHIVE;
      }
      else if (!strcmp("delete", argv[i]))
      {
         *place = i;
         return ACTION_DELETE;
      }
      else if (!strcmp("stop", argv[i]))
      {
         *place = i;
         return ACTION_STOP;
      }
      else if (!strcmp("status", argv[i]))
      {
         *place = i;
         return ACTION_STATUS;
      }
      else if (!strcmp("details", argv[i]))
      {
         *place = i;
         return ACTION_DETAILS;
      }
      else if (!strcmp("is-alive", argv[i]))
      {
         *place = i;
         return ACTION_ISALIVE;
      }
      else if (!strcmp("reset", argv[i]))
      {
         *place = i;
         return ACTION_RESET;
      }
      else if (!strcmp("reload", argv[i]))
      {
         *place = i;
         return ACTION_RELOAD;
      }
      else if (!strcmp("retain", argv[i]))
      {
         *place = i;
         return ACTION_RETAIN;
      }
      else if (!strcmp("expunge", argv[i]))
      {
         *place = i;
         return ACTION_EXPUNGE;
      }
   }

   return ACTION_UNKNOWN;
}

static void
help_backup(void)
{
   printf("Backup a server\n");
   printf("  pgmoneta-cli backup [<server>|all]\n");
}

static void
help_list_backup(void)
{
   printf("List backups for a server\n");
   printf("  pgmoneta-cli list-backup <server>\n");
}

static void
help_restore(void)
{
   printf("Restore a backup for a server\n");
   printf("  pgmoneta-cli restore <server> [<timestamp>|oldest|newest] [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>\n");
}

static void
help_archive(void)
{
   printf("Archive a backup for a server\n");
   printf("  pgmoneta-cli archive [<server>|all] [<timestamp>|oldest|newest] [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>\n");
}

static void
help_delete(void)
{
   printf("Delete a backup for a server\n");
   printf("  pgmoneta-cli delete <server> [<timestamp>|oldest|newest]\n");
}

static void
help_retain(void)
{
   printf("Retain a backup for a server\n");
   printf("  pgmoneta-cli retain <server> [<timestamp>|oldest|newest]\n");
}

static void
help_expunge(void)
{
   printf("Expunge a backup for a server\n");
   printf("  pgmoneta-cli expunge <server> [<timestamp>|oldest|newest]\n");
}

static int
backup(SSL* ssl, int socket, char* server)
{
   int ret;
   int number_of_returns = 0;
   int code = 0;

   ret = pgmoneta_management_backup(ssl, socket, server);
   pgmoneta_management_read_int32(ssl, socket, &number_of_returns);

   for (int i = 0; i < number_of_returns; i++)
   {
      pgmoneta_management_read_int32(ssl, socket, &code);
   }

   return ret;
}

static int
list_backup(SSL* ssl, int socket, char* server)
{
   if (pgmoneta_management_list_backup(ssl, socket, server) == 0)
   {
      pgmoneta_management_read_list_backup(ssl, socket, server);
   }
   else
   {
      return 1;
   }

   return 0;
}

static int
restore(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory)
{
   int ret;
   int number_of_returns = 0;
   int code = 0;

   ret = pgmoneta_management_restore(ssl, socket, server, backup_id, position, directory);
   pgmoneta_management_read_int32(ssl, socket, &number_of_returns);

   for (int i = 0; i < number_of_returns; i++)
   {
      pgmoneta_management_read_int32(ssl, socket, &code);
   }

   return ret;
}

static int
archive(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory)
{
   int ret;
   int number_of_returns = 0;
   int code = 0;

   ret = pgmoneta_management_archive(ssl, socket, server, backup_id, position, directory);
   pgmoneta_management_read_int32(ssl, socket, &number_of_returns);

   for (int i = 0; i < number_of_returns; i++)
   {
      pgmoneta_management_read_int32(ssl, socket, &code);
   }

   return ret;
}

static int
delete(SSL* ssl, int socket, char* server, char* backup_id)
{
   if (pgmoneta_management_delete(ssl, socket, server, backup_id) == 0)
   {
      pgmoneta_management_read_delete(ssl, socket, server, backup_id);
   }
   else
   {
      return 1;
   }

   return 0;
}

static int
stop(SSL* ssl, int socket)
{
   if (pgmoneta_management_stop(ssl, socket))
   {
      return 1;
   }

   return 0;
}

static int
status(SSL* ssl, int socket)
{
   if (pgmoneta_management_status(ssl, socket) == 0)
   {
      pgmoneta_management_read_status(ssl, socket);
   }
   else
   {
      return 1;
   }

   return 0;
}

static int
details(SSL* ssl, int socket)
{
   if (pgmoneta_management_details(ssl, socket) == 0)
   {
      pgmoneta_management_read_details(ssl, socket);
   }
   else
   {
      return 1;
   }

   return 0;
}

static int
isalive(SSL* ssl, int socket)
{
   int status = -1;

   if (pgmoneta_management_isalive(ssl, socket) == 0)
   {
      if (pgmoneta_management_read_isalive(ssl, socket, &status))
      {
         return 1;
      }

      if (status != 1 && status != 2)
      {
         return 1;
      }
   }
   else
   {
      return 1;
   }

   return 0;
}

static int
reset(SSL* ssl, int socket)
{
   if (pgmoneta_management_reset(ssl, socket))
   {
      return 1;
   }

   return 0;
}

static int
reload(SSL* ssl, int socket)
{
   if (pgmoneta_management_reload(ssl, socket))
   {
      return 1;
   }

   return 0;
}

static int
retain(SSL* ssl, int socket, char* server, char* backup_id)
{
   if (pgmoneta_management_retain(ssl, socket, server, backup_id))
   {
      return 1;
   }

   return 0;
}

static int
expunge(SSL* ssl, int socket, char* server, char* backup_id)
{
   if (pgmoneta_management_expunge(ssl, socket, server, backup_id))
   {
      return 1;
   }

   return 0;
}
