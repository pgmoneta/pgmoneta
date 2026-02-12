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
#include <achv.h>
#include <aes.h>
#include <backup.h>
#include <bzip2_compression.h>
#include <cmd.h>
#include <configuration.h>
#include <delete.h>
#include <gzip_compression.h>
#include <info.h>
#include <keep.h>
#include <logging.h>
#include <lz4_compression.h>
#include <management.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <remote.h>
#include <restore.h>
#include <retention.h>
#include <security.h>
#include <server.h>
#include <shmem.h>
#include <status.h>
#include <stddef.h>
#include <utils.h>
#include <verify.h>
#include <wal.h>
#include <zstandard_compression.h>

/* system */
#include <err.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <openssl/crypto.h>
#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#define NAME           "main"
#define MAX_FDS        64
#define SIGNALS_NUMBER 6

static void accept_mgt_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_metrics_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_management_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void shutdown_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void reload_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void coredump_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void retention_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static void verification_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static void valid_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static void wal_streaming_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static bool accept_fatal(int error);
static bool reload_configuration(void);
static void service_reload_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void reload_set_configuration(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload);
static bool reload_services_only(void);
static void init_receivewals(void);
static int init_receivewal(int server);
static int init_replication_slots(void);
static int init_replication_slot(int server);
static int verify_replication_slot(char* slot_name, int srv, SSL* ssl, int socket);
static int create_pidfile(void);
static void remove_pidfile(void);
static void shutdown_ports(void);

struct accept_io
{
   struct ev_io io;
   int socket;
   char** argv;
};

static volatile int keep_running = 1;
static volatile int stop = 0;
static char** argv_ptr;
static struct ev_loop* main_loop = NULL;
static struct accept_io io_mgt;
static int unix_management_socket = -1;
static struct accept_io io_metrics[MAX_FDS];
static int* metrics_fds = NULL;
static int metrics_fds_length = -1;
static struct accept_io io_management[MAX_FDS];
static int* management_fds = NULL;
static int management_fds_length = -1;

static void
start_mgt(void)
{
   memset(&io_mgt, 0, sizeof(struct accept_io));
   ev_io_init((struct ev_io*)&io_mgt, accept_mgt_cb, unix_management_socket, EV_READ);
   io_mgt.socket = unix_management_socket;
   io_mgt.argv = argv_ptr;
   ev_io_start(main_loop, (struct ev_io*)&io_mgt);
}

static void
shutdown_mgt(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   ev_io_stop(main_loop, (struct ev_io*)&io_mgt);
   pgmoneta_disconnect(unix_management_socket);
   errno = 0;
   pgmoneta_remove_unix_socket(config->common.unix_socket_dir, MAIN_UDS);
   errno = 0;
}

static void
start_metrics(void)
{
   for (int i = 0; i < metrics_fds_length; i++)
   {
      int sockfd = *(metrics_fds + i);

      memset(&io_metrics[i], 0, sizeof(struct accept_io));
      ev_io_init((struct ev_io*)&io_metrics[i], accept_metrics_cb, sockfd, EV_READ);
      io_metrics[i].socket = sockfd;
      io_metrics[i].argv = argv_ptr;
      ev_io_start(main_loop, (struct ev_io*)&io_metrics[i]);
   }
}

static void
shutdown_metrics(void)
{
   for (int i = 0; i < metrics_fds_length; i++)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_metrics[i]);
      pgmoneta_disconnect(io_metrics[i].socket);
      errno = 0;
   }
}

static void
start_management(void)
{
   for (int i = 0; i < management_fds_length; i++)
   {
      int sockfd = *(management_fds + i);

      memset(&io_management[i], 0, sizeof(struct accept_io));
      ev_io_init((struct ev_io*)&io_management[i], accept_management_cb, sockfd, EV_READ);
      io_management[i].socket = sockfd;
      io_management[i].argv = argv_ptr;
      ev_io_start(main_loop, (struct ev_io*)&io_management[i]);
   }
}

static void
shutdown_management(void)
{
   for (int i = 0; i < management_fds_length; i++)
   {
      ev_io_stop(main_loop, (struct ev_io*)&io_management[i]);
      pgmoneta_disconnect(io_management[i].socket);
      errno = 0;
   }
}

static void
version(void)
{
   printf("pgmoneta %s\n", VERSION);
   exit(1);
}

static void
usage(void)
{
   printf("pgmoneta %s\n", VERSION);
   printf("  Backup / restore solution for PostgreSQL\n");
   printf("\n");

   printf("Usage:\n");
   printf("  pgmoneta [ -c CONFIG_FILE ] [ -u USERS_FILE ] [ -d ]\n");
   printf("\n");
   printf("Options:\n");
   printf("  -c, --config CONFIG_FILE  Set the path to the pgmoneta.conf file\n");
   printf("  -u, --users USERS_FILE    Set the path to the pgmoneta_users.conf file\n");
   printf("  -A, --admins ADMINS_FILE  Set the path to the pgmoneta_admins.conf file\n");
   printf("  -D, --directory DIRECTORY Set the directory containing all configuration files\n");
   printf("                            Can also be set via PGMONETA_CONFIG_DIR environment variable\n");
   printf("  -d, --daemon              Run as a daemon\n");
   printf("  -V, --version             Display version information\n");
   printf("  -?, --help                Display help\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

int
main(int argc, char** argv)
{
   char* configuration_path = NULL;
   char* users_path = NULL;
   char* admins_path = NULL;
   char* directory_path = NULL;
   bool daemon = false;
   bool pid_file_created = false;
   bool management_started = false;
   bool mgt_started = false;
   bool metrics_started = false;
   pid_t pid, sid;
   struct signal_info signal_watcher[SIGNALS_NUMBER];
   struct ev_periodic retention;
   struct ev_periodic valid;
   struct ev_periodic wal_streaming;
   struct ev_periodic verification;
   size_t shmem_size;
   size_t prometheus_cache_shmem_size = 0;
   struct main_configuration* config = NULL;
   int ret;
   char* os = NULL;
   int kernel_major, kernel_minor, kernel_patch;
   argv_ptr = argv;
   char* filepath = NULL;
   int optind = 0;
   int num_options = 0;
   int num_results = 0;
   char config_path_buffer[MAX_PATH];
   char users_path_buffer[MAX_PATH];
   char admins_path_buffer[MAX_PATH];
   struct stat path_stat = {0};
   char* adjusted_dir_path = NULL;

   cli_option options[] = {
      {"c", "config", true},
      {"u", "users", true},
      {"A", "admins", true},
      {"d", "daemon", false},
      {"D", "directory", true},
      {"V", "version", false},
      {"?", "help", false},
   };

   num_options = sizeof(options) / sizeof(options[0]);

   cli_result results[num_options];

   num_results = cmd_parse(argc, argv, options, num_options, results, num_options, false, &filepath, &optind);

   if (num_results < 0)
   {
      errx(1, "Error parsing command line\n");
      return 1;
   }

   for (int i = 0; i < num_results; i++)
   {
      char* optname = results[i].option_name;
      char* optarg = results[i].argument;

      if (optname == NULL)
      {
         break;
      }
      else if (!strcmp(optname, "c") || !strcmp(optname, "config"))
      {
         configuration_path = optarg;
      }
      else if (!strcmp(optname, "u") || !strcmp(optname, "users"))
      {
         users_path = optarg;
      }
      else if (!strcmp(optname, "A") || !strcmp(optname, "admins"))
      {
         admins_path = optarg;
      }
      else if (!strcmp(optname, "d") || !strcmp(optname, "daemon"))
      {
         daemon = true;
      }
      else if (!strcmp(optname, "D") || !strcmp(optname, "directory"))
      {
         directory_path = optarg;
      }
      else if (!strcmp(optname, "V") || !strcmp(optname, "version"))
      {
         version();
      }
      else if (!strcmp(optname, "?") || !strcmp(optname, "help"))
      {
         usage();
         exit(0);
      }
   }

   if (getuid() == 0)
   {
      warnx("pgmoneta: Using the root account is not allowed");
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Using the root account is not allowed");
#endif
      exit(1);
   }

   shmem_size = sizeof(struct main_configuration);
   if (pgmoneta_create_shared_memory(shmem_size, HUGEPAGE_OFF, &shmem))
   {
      warnx("pgmoneta: Error in creating shared memory");
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Error in creating shared memory");
#endif
      goto error;
   }

   pgmoneta_init_main_configuration(shmem);
   config = (struct main_configuration*)shmem;

   if (directory_path == NULL)
   {
      // Check for environment variable if no -D flag provided
      directory_path = getenv("PGMONETA_CONFIG_DIR");
      if (directory_path != NULL)
      {
         pgmoneta_log_info("Configuration directory set via PGMONETA_CONFIG_DIR environment variable: %s", directory_path);
      }
   }

   if (directory_path != NULL)
   {
      if (!strcmp(directory_path, "/etc/pgmoneta"))
      {
         pgmoneta_log_warn("Using the default configuration directory %s, -D can be omitted.", directory_path);
      }

      if (access(directory_path, F_OK) != 0)
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Configuration directory not found %s", directory_path);
#endif
         pgmoneta_log_error("Configuration directory not found: %s", directory_path);
         exit(1);
      }

      if (stat(directory_path, &path_stat) == 0)
      {
         if (!S_ISDIR(path_stat.st_mode))
         {
#ifdef HAVE_SYSTEMD
            sd_notifyf(0, "STATUS=Path is not a directory %s", directory_path);
#endif
            pgmoneta_log_error("Path is not a directory: %s", directory_path);
            exit(1);
         }
      }

      if (access(directory_path, R_OK | X_OK) != 0)
      {
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Insufficient permissions for directory %s", directory_path);
#endif
         pgmoneta_log_error("Insufficient permissions for directory: %s", directory_path);
         exit(1);
      }

      if (directory_path[strlen(directory_path) - 1] != '/')
      {
         adjusted_dir_path = pgmoneta_append(strdup(directory_path), "/");
      }
      else
      {
         adjusted_dir_path = strdup(directory_path);
      }

      if (adjusted_dir_path == NULL)
      {
         pgmoneta_log_error("Memory allocation failed while copying directory path.");
         exit(1);
      }

      if (!configuration_path && pgmoneta_normalize_path(adjusted_dir_path, "pgmoneta.conf", PGMONETA_DEFAULT_CONFIG_FILE_PATH, config_path_buffer, sizeof(config_path_buffer)) == 0 && strlen(config_path_buffer) > 0)
      {
         configuration_path = config_path_buffer;
      }

      if (!users_path && pgmoneta_normalize_path(adjusted_dir_path, "pgmoneta_users.conf", PGMONETA_DEFAULT_USERS_FILE_PATH, users_path_buffer, sizeof(users_path_buffer)) == 0 && strlen(users_path_buffer) > 0)
      {
         users_path = users_path_buffer;
      }

      if (!admins_path && pgmoneta_normalize_path(adjusted_dir_path, "pgmoneta_admins.conf", CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH, admins_path_buffer, sizeof(admins_path_buffer)) == 0 && strlen(admins_path_buffer) > 0)
      {
         admins_path = admins_path_buffer;
      }

      free(adjusted_dir_path);
   }

   if (configuration_path != NULL)
   {
      ret = pgmoneta_validate_config_file(configuration_path);

      if (ret)
      {
         switch (ret)
         {
            case ENOENT:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Configuration file not found or not a regular file: %s", configuration_path);
#endif
               errx(1, "Configuration file not found or not a regular file: %s", configuration_path);
               break;

            case EACCES:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Can't read configuration file: %s", configuration_path);
#endif
               errx(1, "Can't read configuration file: %s", configuration_path);
               break;

            case EINVAL:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Configuration file contains binary data or invalid path: %s", configuration_path);
#endif
               errx(1, "Configuration file contains binary data or invalid path: %s", configuration_path);
               break;

            default:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Configuration file validation failed: %s", configuration_path);
#endif
               errx(1, "Configuration file validation failed: %s", configuration_path);
         }
      }

      if (pgmoneta_read_main_configuration(shmem, configuration_path))
      {
         warnx("Failed to read configuration file: %s", configuration_path);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Failed to read configuration file: %s", configuration_path);
#endif
      }
   }
   else
   {
      configuration_path = "/etc/pgmoneta/pgmoneta.conf";

      ret = pgmoneta_validate_config_file(configuration_path);

      if (ret)
      {
         switch (ret)
         {
            case ENOENT:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Configuration file not found or not a regular file: %s", configuration_path);
#endif
               errx(1, "Configuration file not found or not a regular file: %s", configuration_path);
               break;

            case EACCES:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Can't read configuration file: %s", configuration_path);
#endif
               errx(1, "Can't read configuration file: %s", configuration_path);
               break;

            case EINVAL:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Configuration file contains binary data or invalid path: %s", configuration_path);
#endif
               errx(1, "Configuration file contains binary data or invalid path: %s", configuration_path);
               break;

            default:
#ifdef HAVE_SYSTEMD
               sd_notifyf(0, "STATUS=Configuration file validation failed: %s", configuration_path);
#endif
               errx(1, "Configuration file validation failed: %s", configuration_path);
         }
      }

      if (pgmoneta_read_main_configuration(shmem, configuration_path))
      {
         warnx("Failed to read configuration file: %s", configuration_path);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Failed to read configuration file: %s", configuration_path);
#endif
      }
   }

   memcpy(&config->common.configuration_path[0], configuration_path, MIN(strlen(configuration_path), (size_t)MAX_PATH - 1));

   if (users_path != NULL)
   {
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret == 1)
      {
         warnx("pgmoneta: USERS configuration not found: %s", users_path);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=USERS configuration not found: %s", users_path);
#endif
         goto error;
      }
      else if (ret == 2)
      {
         warnx("pgmoneta: Invalid master key file");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         goto error;
      }
      else if (ret == 3)
      {
         warnx("pgmoneta: USERS: Too many users defined %d (max %d)", config->common.number_of_users, NUMBER_OF_USERS);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=USERS: Too many users defined %d (max %d)", config->common.number_of_users, NUMBER_OF_USERS);
#endif
         goto error;
      }
   }
   else
   {
      users_path = "/etc/pgmoneta/pgmoneta_users.conf";
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret == 1)
      {
         warnx("pgmoneta: USERS configuration not found: %s", users_path);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=USERS configuration not found: %s", users_path);
#endif
         goto error;
      }
      else if (ret == 2)
      {
         warnx("pgmoneta: Invalid master key file");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         goto error;
      }
      else if (ret == 3)
      {
         warnx("pgmoneta: USERS: Too many users defined %d (max %d)", config->common.number_of_users, NUMBER_OF_USERS);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=USERS: Too many users defined %d (max %d)", config->common.number_of_users, NUMBER_OF_USERS);
#endif
         goto error;
      }
   }

   memcpy(&config->common.users_path[0], users_path, MIN(strlen(users_path), (size_t)MAX_PATH - 1));

   if (admins_path != NULL)
   {
      ret = pgmoneta_read_admins_configuration(shmem, admins_path);
      if (ret == 1)
      {
         warnx("pgmoneta: ADMINS configuration not found: %s", admins_path);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=ADMINS configuration not found: %s", admins_path);
#endif
         goto error;
      }
      else if (ret == 2)
      {
         warnx("pgmoneta: Invalid master key file");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         goto error;
      }
      else if (ret == 3)
      {
         warnx("pgmoneta: ADMINS: Too many admins defined %d (max %d)", config->common.number_of_admins, NUMBER_OF_ADMINS);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=ADMINS: Too many admins defined %d (max %d)", config->common.number_of_admins, NUMBER_OF_ADMINS);
#endif
         goto error;
      }
      memcpy(&config->common.admins_path[0], admins_path, MIN(strlen(admins_path), (size_t)MAX_PATH - 1));
   }
   else
   {
      admins_path = "/etc/pgmoneta/pgmoneta_admins.conf";
      ret = pgmoneta_read_admins_configuration(shmem, admins_path);
      if (ret == 0)
      {
         memcpy(&config->common.admins_path[0], admins_path, MIN(strlen(admins_path), (size_t)MAX_PATH - 1));
      }
   }

   if (pgmoneta_start_logging())
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Failed to start logging");
#endif
      goto error;
   }

   if (pgmoneta_validate_main_configuration(shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid configuration");
#endif
      goto error;
   }
   if (pgmoneta_validate_users_configuration(shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid USERS configuration");
#endif
      goto error;
   }
   if (pgmoneta_validate_admins_configuration(shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid ADMINS configuration");
#endif
      goto error;
   }

   config = (struct main_configuration*)shmem;

   if (daemon)
   {
      if (config->common.log_type == PGMONETA_LOGGING_TYPE_CONSOLE)
      {
         warnx("pgmoneta: Daemon mode can't be used with console logging");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Daemon mode can't be used with console logging");
#endif
         goto error;
      }

      pid = fork();

      if (pid < 0)
      {
         warnx("pgmoneta: Daemon mode failed");
#ifdef HAVE_SYSTEMD
         sd_notify(0, "STATUS=Daemon mode failed");
#endif
         goto error;
      }

      if (pid > 0)
      {
         exit(0);
      }

      /* We are a daemon now */
      umask(0);
      sid = setsid();

      if (sid < 0)
      {
         exit(1);
      }
   }
   else
   {
      daemon = false;
   }

   if (create_pidfile())
   {
      goto error;
   }
   pid_file_created = true;

   pgmoneta_set_proc_title(argc, argv, "main", NULL);

   if (pgmoneta_init_prometheus_cache(&prometheus_cache_shmem_size, &prometheus_cache_shmem))
   {
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Error in creating and initializing prometheus cache shared memory");
#endif
      errx(1, "Error in creating and initializing prometheus cache shared memory");
   }

   /* Bind Unix Domain Socket */
   if (pgmoneta_bind_unix_socket(config->common.unix_socket_dir, MAIN_UDS, &unix_management_socket))
   {
      pgmoneta_log_fatal("Could not bind to %s/%s", config->common.unix_socket_dir, MAIN_UDS);
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->common.unix_socket_dir, MAIN_UDS);
#endif
      goto error;
   }

   /* libev */
   main_loop = ev_default_loop(pgmoneta_libev(config->libev));
   if (!main_loop)
   {
      pgmoneta_log_fatal("No loop implementation (%x) (%x)",
                         pgmoneta_libev(config->libev), ev_supported_backends());
#ifdef HAVE_SYSTEMD
      sd_notifyf(0, "STATUS=No loop implementation (%x) (%x)", pgmoneta_libev(config->libev), ev_supported_backends());
#endif
      goto error;
   }

   ev_signal_init((struct ev_signal*)&signal_watcher[0], shutdown_cb, SIGTERM);
   ev_signal_init((struct ev_signal*)&signal_watcher[1], reload_cb, SIGHUP);
   ev_signal_init((struct ev_signal*)&signal_watcher[2], shutdown_cb, SIGINT);
   ev_signal_init((struct ev_signal*)&signal_watcher[3], coredump_cb, SIGABRT);
   ev_signal_init((struct ev_signal*)&signal_watcher[4], shutdown_cb, SIGALRM);
   ev_signal_init((struct ev_signal*)&signal_watcher[5], service_reload_cb, SIGUSR1);

   for (int i = 0; i < 6; i++)
   {
      signal_watcher[i].slot = -1;
      ev_signal_start(main_loop, (struct ev_signal*)&signal_watcher[i]);
   }

   if (pgmoneta_tls_valid())
   {
      pgmoneta_log_fatal("Invalid TLS configuration");
#ifdef HAVE_SYSTEMD
      sd_notify(0, "STATUS=Invalid TLS configuration");
#endif
      goto error;
   }

   start_mgt();
   mgt_started = true;

   if (config->metrics > 0)
   {
      /* Bind metrics socket */
      if (pgmoneta_bind(config->host, config->metrics, &metrics_fds, &metrics_fds_length))
      {
         pgmoneta_log_fatal("Could not bind to %s:%d", config->host, config->metrics);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->metrics);
#endif
         goto error;
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgmoneta_log_fatal("Too many descriptors %d", metrics_fds_length);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Too many descriptors %d", metrics_fds_length);
#endif
         goto error;
      }

      start_metrics();
      metrics_started = true;
   }

   if (config->management > 0)
   {
      /* Bind management socket */
      if (pgmoneta_bind(config->host, config->management, &management_fds, &management_fds_length))
      {
         pgmoneta_log_fatal("Could not bind to %s:%d", config->host, config->management);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->management);
#endif
         goto error;
      }

      if (management_fds_length > MAX_FDS)
      {
         pgmoneta_log_fatal("Too many descriptors %d", management_fds_length);
#ifdef HAVE_SYSTEMD
         sd_notifyf(0, "STATUS=Too many descriptors %d", management_fds_length);
#endif
         goto error;
      }

      start_management();
      management_started = true;
   }

   /* Create and/or validate replication slots */
   if (init_replication_slots())
   {
      goto error;
   }

   /* Start to retrieve WAL */
   init_receivewals();

   /* Start to validate server configuration */
   ev_periodic_init(&valid, valid_cb, 0., 600, 0);
   ev_periodic_start(main_loop, &valid);

   /* Start to verify WAL streaming */
   ev_periodic_init(&wal_streaming, wal_streaming_cb, 0., 60, 0);
   ev_periodic_start(main_loop, &wal_streaming);

   /* Start backup retention policy */
   ev_periodic_init(&retention, retention_cb, 0., config->retention_interval, 0);
   ev_periodic_start(main_loop, &retention);

   /* Start SHA512 verification job */
   ev_periodic_init(&verification, verification_cb, 0., pgmoneta_time_convert(config->verification, FORMAT_TIME_S), 0);
   ev_periodic_start(main_loop, &verification);

   pgmoneta_log_info("Started on %s", config->host);
   pgmoneta_log_debug("Management: %d", unix_management_socket);
   for (int i = 0; i < metrics_fds_length; i++)
   {
      pgmoneta_log_debug("Metrics: %d", *(metrics_fds + i));
   }
   for (int i = 0; i < management_fds_length; i++)
   {
      pgmoneta_log_debug("Remote management: %d", *(management_fds + i));
   }
   pgmoneta_libev_engines();
   pgmoneta_log_debug("libev engine: %s", pgmoneta_libev_engine(ev_backend(main_loop)));
   pgmoneta_log_debug("%s", OpenSSL_version(OPENSSL_VERSION));
   pgmoneta_log_debug("Configuration size: %lu", shmem_size);
   pgmoneta_log_debug("Known users: %d", config->common.number_of_users);
   pgmoneta_log_debug("Known admins: %d", config->common.number_of_admins);

   pgmoneta_os_kernel_version(&os, &kernel_major, &kernel_minor, &kernel_patch);

   for (int i = 0; i < config->common.number_of_servers; i++)
   {
      if (config->common.servers[i].online)
      {
         pgmoneta_log_info("Server %s is online", config->common.servers[i].name);
      }
      else
      {
         pgmoneta_log_info("Server %s is offline", config->common.servers[i].name);
      }
   }

   free(os);

#ifdef HAVE_SYSTEMD
   sd_notifyf(0,
              "READY=1\n"
              "STATUS=Running\n"
              "MAINPID=%lu",
              (unsigned long)getpid());
#endif

   while (keep_running)
   {
      ev_loop(main_loop, 0);
   }

   pgmoneta_log_info("Shutdown");
#ifdef HAVE_SYSTEMD
   sd_notify(0, "STOPPING=1");
#endif

   shutdown_management();
   shutdown_metrics();
   shutdown_mgt();

   for (int i = 0; i < 5; i++)
   {
      ev_signal_stop(main_loop, (struct ev_signal*)&signal_watcher[i]);
   }

   ev_loop_destroy(main_loop);

   free(metrics_fds);
   free(management_fds);

   remove_pidfile();

   pgmoneta_stop_logging();
   pgmoneta_destroy_shared_memory(shmem, shmem_size);
   pgmoneta_destroy_shared_memory(prometheus_cache_shmem, prometheus_cache_shmem_size);

   if (daemon || stop)
   {
      kill(0, SIGTERM);
   }

   return 0;

error:

   if (pid_file_created)
   {
      remove_pidfile();
      pid_file_created = false;
   }

   if (mgt_started)
   {
      shutdown_mgt();
   }

   if (metrics_started)
   {
      shutdown_metrics();
   }

   if (management_started)
   {
      shutdown_management();
   }

   free(metrics_fds);
   free(management_fds);

   config->running = false;

   pgmoneta_stop_logging();
   pgmoneta_destroy_shared_memory(shmem, shmem_size);
   pgmoneta_destroy_shared_memory(prometheus_cache_shmem, prometheus_cache_shmem_size);

   if (daemon || stop)
   {
      kill(0, SIGTERM);
   }

   exit(1);

   return 1;
}

static void
accept_mgt_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   int32_t id;
   char* server = NULL;
   int srv;
   pid_t pid;
   char* str = NULL;
   struct timespec start_t;
   struct timespec end_t;
   struct accept_io* ai;
   struct json* payload = NULL;
   struct json* header = NULL;
   struct json* request = NULL;
   struct main_configuration* config;
   uint8_t compression = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("accept_mgt_cb: got invalid event: %s", strerror(errno));
      return;
   }

   config = (struct main_configuration*)shmem;
   ai = (struct accept_io*)watcher;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgmoneta_log_warn("Restarting management due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_mgt();

         if (pgmoneta_bind_unix_socket(config->common.unix_socket_dir, MAIN_UDS, &unix_management_socket))
         {
            pgmoneta_log_fatal("Could not bind to %s", config->common.unix_socket_dir);
            exit(1);
         }

         start_mgt();

         pgmoneta_log_debug("Management: %d", unix_management_socket);
      }
      else
      {
         pgmoneta_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   /* Process internal management request */
   if (pgmoneta_management_read_json(NULL, client_fd, &compression, &encryption, &payload))
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_BAD_PAYLOAD, NAME, compression, encryption, NULL);
      pgmoneta_log_error("Management: Bad payload (%d)", MANAGEMENT_ERROR_BAD_PAYLOAD);
      goto error;
   }

   if (encryption == MANAGEMENT_ENCRYPTION_NONE)
   {
      char* c = pgmoneta_get_host(client_addr);
      pgmoneta_log_debug("Unencrypted request from %s", c);
      free(c);
      c = NULL;
   }

   header = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_HEADER);
   id = (int32_t)pgmoneta_json_get(header, MANAGEMENT_ARGUMENT_COMMAND);

   str = pgmoneta_json_to_string(payload, FORMAT_JSON, NULL, 0);
   pgmoneta_log_debug("Management %d: %s", id, str);

   request = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);

   if (id == MANAGEMENT_BACKUP)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (!strcmp(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }

      if (srv != -1)
      {
         if (pgmoneta_server_is_online(srv))
         {
            pid = fork();
            if (pid == -1)
            {
               pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_BACKUP_NOFORK, NAME,
                                                  compression, encryption, payload);
               pgmoneta_log_error("Backup: No fork (%d)", MANAGEMENT_ERROR_BACKUP_NOFORK);
               goto error;
            }
            else if (pid == 0)
            {
               struct json* pyl = NULL;

               shutdown_ports();

               pgmoneta_json_clone(payload, &pyl);

               pgmoneta_set_proc_title(1, ai->argv, "backup", config->common.servers[srv].name);
               pgmoneta_backup(client_fd, srv, compression, encryption, pyl);
            }
         }
         else
         {
            pgmoneta_management_response_error(NULL, client_fd, (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER),
                                               MANAGEMENT_ERROR_BACKUP_OFFLINE, NAME, compression, encryption, payload);
            pgmoneta_log_info("Backup: Server %s is offline", server);
         }
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_BACKUP_NOSERVER, NAME,
                                            compression, encryption, payload);
         pgmoneta_log_error("Backup: No server %s (%d)", server, MANAGEMENT_ERROR_BACKUP_NOSERVER);
         goto error;
      }
   }
   else if (id == MANAGEMENT_LIST_BACKUP)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (!strcmp(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }

      if (srv != -1)
      {
         pid = fork();
         if (pid == -1)
         {
            pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_LIST_BACKUP_NOFORK, NAME, compression, encryption, payload);
            pgmoneta_log_error("List backup: No fork %s (%d)", server, MANAGEMENT_ERROR_LIST_BACKUP_NOFORK);
            goto error;
         }
         else if (pid == 0)
         {
            struct json* pyl = NULL;

            shutdown_ports();

            pgmoneta_json_clone(payload, &pyl);

            pgmoneta_set_proc_title(1, ai->argv, "list-backup", config->common.servers[srv].name);
            pgmoneta_list_backup(client_fd, srv, compression, encryption, pyl);
         }
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_LIST_BACKUP_NOSERVER, NAME, compression, encryption, payload);
         pgmoneta_log_error("List backup: No server %s (%d)", server, MANAGEMENT_ERROR_LIST_BACKUP_NOSERVER);
         goto error;
      }
   }
   else if (id == MANAGEMENT_DELETE)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (!strcmp(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }

      if (srv != -1)
      {
         pid = fork();
         if (pid == -1)
         {
            pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_DELETE_NOFORK, NAME, compression, encryption, payload);
            pgmoneta_log_error("Delete: No fork %s (%d)", server, MANAGEMENT_ERROR_DELETE_NOFORK);
            goto error;
         }
         else if (pid == 0)
         {
            struct json* pyl = NULL;

            shutdown_ports();

            pgmoneta_json_clone(payload, &pyl);

            pgmoneta_set_proc_title(1, ai->argv, "delete", config->common.servers[srv].name);
            pgmoneta_delete_backup(client_fd, srv, compression, encryption, pyl);
            pgmoneta_delete_wal(srv);
         }
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_DELETE_NOSERVER, NAME, compression, encryption, payload);
         pgmoneta_log_error("Delete: No server %s (%d)", server, MANAGEMENT_ERROR_DELETE_NOSERVER);
         goto error;
      }
   }
   else if (id == MANAGEMENT_RESTORE)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (!strcmp(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }

      if (srv != -1)
      {
         pid = fork();
         if (pid == -1)
         {
            pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_RESTORE_NOFORK, NAME, compression, encryption, payload);
            pgmoneta_log_error("Restore: No fork %s (%d)", server, MANAGEMENT_ERROR_RESTORE_NOFORK);
            goto error;
         }
         else if (pid == 0)
         {
            struct json* pyl = NULL;

            shutdown_ports();

            pgmoneta_json_clone(payload, &pyl);

            pgmoneta_set_proc_title(1, ai->argv, "restore", config->common.servers[srv].name);
            pgmoneta_restore(NULL, client_fd, srv, compression, encryption, pyl);
         }
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_RESTORE_NOSERVER, NAME, compression, encryption, payload);
         pgmoneta_log_error("Restore: No server %s (%d)", server, MANAGEMENT_ERROR_RESTORE_NOSERVER);
         goto error;
      }
   }
   else if (id == MANAGEMENT_VERIFY)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (!strcmp(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }

      if (srv != -1)
      {
         pid = fork();
         if (pid == -1)
         {
            pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_VERIFY_NOFORK, NAME, compression, encryption, payload);
            pgmoneta_log_error("Verify: No fork %s (%d)", server, MANAGEMENT_ERROR_VERIFY_NOFORK);
            goto error;
         }
         else if (pid == 0)
         {
            struct json* pyl = NULL;

            shutdown_ports();

            pgmoneta_json_clone(payload, &pyl);

            pgmoneta_set_proc_title(1, ai->argv, "verify", config->common.servers[srv].name);
            pgmoneta_verify(NULL, client_fd, srv, compression, encryption, pyl);
         }
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_VERIFY_NOSERVER, NAME, compression, encryption, payload);
         pgmoneta_log_error("Restore: No server %s (%d)", server, MANAGEMENT_ERROR_VERIFY_NOSERVER);
         goto error;
      }
   }
   else if (id == MANAGEMENT_ARCHIVE)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (!strcmp(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }

      if (srv != -1)
      {
         pid = fork();
         if (pid == -1)
         {
            pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_ARCHIVE_NOFORK, NAME, compression, encryption, payload);
            pgmoneta_log_error("Archive: No fork %s (%d)", server, MANAGEMENT_ERROR_ARCHIVE_NOFORK);
            goto error;
         }
         else if (pid == 0)
         {
            struct json* pyl = NULL;

            shutdown_ports();

            pgmoneta_json_clone(payload, &pyl);

            pgmoneta_set_proc_title(1, ai->argv, "archive", config->common.servers[srv].name);
            pgmoneta_archive(NULL, client_fd, srv, compression, encryption, pyl);
         }
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_ARCHIVE_NOSERVER, NAME, compression, encryption, payload);
         pgmoneta_log_error("Archive: No server %s (%d)", server, MANAGEMENT_ERROR_ARCHIVE_NOSERVER);
         goto error;
      }
   }
   else if (id == MANAGEMENT_SHUTDOWN)
   {
#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

      pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload);

      ev_break(loop, EVBREAK_ALL);
      keep_running = 0;
      stop = 1;
      config->running = false;
   }
   else if (id == MANAGEMENT_PING)
   {
      struct json* response = NULL;

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

      pgmoneta_management_create_response(payload, -1, &response);

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

      pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_RESET)
   {
#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

      pgmoneta_prometheus_reset();

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

      pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_RELOAD)
   {
      bool restart = false;
      struct json* response = NULL;

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

      restart = reload_configuration();

      pgmoneta_management_create_response(payload, -1, &response);

      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_RESTART, (uintptr_t)restart, ValueBool);

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

      pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_CONF_LS)
   {
      struct json* response = NULL;

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

      pgmoneta_management_create_response(payload, -1, &response);

      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_MAIN_CONF_PATH, (uintptr_t)config->common.configuration_path, ValueString);
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_USER_CONF_PATH, (uintptr_t)config->common.users_path, ValueString);
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH, (uintptr_t)config->common.admins_path, ValueString);

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

      pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_CONF_GET)
   {
      pid = fork();
      if (pid == -1)
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_CONF_GET_NOFORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Conf Get: No fork %s (%d)", server, MANAGEMENT_ERROR_CONF_GET_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgmoneta_json_clone(payload, &pyl);

         pgmoneta_set_proc_title(1, ai->argv, "conf get", NULL);
         pgmoneta_conf_get(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_CONF_SET)
   {
      pid = fork();
      if (pid == -1)
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_CONF_SET_NOFORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Conf Set: No fork %s (%d)", server, MANAGEMENT_ERROR_CONF_SET_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgmoneta_json_clone(payload, &pyl);

         pgmoneta_set_proc_title(1, ai->argv, "conf set", NULL);
         reload_set_configuration(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_STATUS)
   {
      pid = fork();
      if (pid == -1)
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_STATUS_NOFORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Status: No fork %s (%d)", server, MANAGEMENT_ERROR_STATUS_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgmoneta_json_clone(payload, &pyl);

         pgmoneta_set_proc_title(1, ai->argv, "status", NULL);
         pgmoneta_status(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_STATUS_DETAILS)
   {
      pid = fork();
      if (pid == -1)
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_STATUS_DETAILS_NOFORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Details: No fork %s (%d)", server, MANAGEMENT_ERROR_STATUS_DETAILS_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgmoneta_json_clone(payload, &pyl);

         pgmoneta_set_proc_title(1, ai->argv, "details", NULL);
         pgmoneta_status_details(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_RETAIN)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (!strcmp(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }

      if (srv != -1)
      {
         pid = fork();
         if (pid == -1)
         {
            pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_RETAIN_NOFORK, NAME, compression, encryption, payload);
            pgmoneta_log_error("Retain: No fork %s (%d)", server, MANAGEMENT_ERROR_RETAIN_NOFORK);
            goto error;
         }
         else if (pid == 0)
         {
            struct json* pyl = NULL;

            shutdown_ports();

            pgmoneta_json_clone(payload, &pyl);

            pgmoneta_set_proc_title(1, ai->argv, "retain", config->common.servers[srv].name);
            pgmoneta_retain_backup(NULL, client_fd, srv, compression, encryption, pyl);
         }
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_RETAIN_NOSERVER, NAME, compression, encryption, payload);
         pgmoneta_log_error("Retain: No server %s (%d)", server, MANAGEMENT_ERROR_RETAIN_NOSERVER);
         goto error;
      }
   }
   else if (id == MANAGEMENT_EXPUNGE)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (!strcmp(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }

      if (srv != -1)
      {
         pid = fork();
         if (pid == -1)
         {
            pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_EXPUNGE_NOFORK, NAME, compression, encryption, payload);
            pgmoneta_log_error("Expunge: No fork %s (%d)", server, MANAGEMENT_ERROR_EXPUNGE_NOFORK);
            goto error;
         }
         else if (pid == 0)
         {
            struct json* pyl = NULL;

            shutdown_ports();

            pgmoneta_json_clone(payload, &pyl);

            pgmoneta_set_proc_title(1, ai->argv, "expunge", config->common.servers[srv].name);
            pgmoneta_expunge_backup(NULL, client_fd, srv, compression, encryption, pyl);
         }
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_EXPUNGE_NOSERVER, NAME, compression, encryption, payload);
         pgmoneta_log_error("Expunge: No server %s (%d)", server, MANAGEMENT_ERROR_EXPUNGE_NOSERVER);
         goto error;
      }
   }
   else if (id == MANAGEMENT_DECRYPT)
   {
      pid = fork();
      if (pid == -1)
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_DECRYPT_NOFORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Decrypt: No fork %s (%d)", server, MANAGEMENT_ERROR_DECRYPT_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgmoneta_json_clone(payload, &pyl);

         pgmoneta_set_proc_title(1, ai->argv, "decrypt", NULL);
         pgmoneta_decrypt_request(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_ENCRYPT)
   {
      pid = fork();
      if (pid == -1)
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_ENCRYPT_NOFORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Encrypt: No fork %s (%d)", server, MANAGEMENT_ERROR_ENCRYPT_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgmoneta_json_clone(payload, &pyl);

         pgmoneta_set_proc_title(1, ai->argv, "encrypt", NULL);
         pgmoneta_encrypt_request(NULL, client_fd, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_DECOMPRESS)
   {
      pid = fork();
      if (pid == -1)
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_DECOMPRESS_NOFORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Decompress: No fork %s (%d)", server, MANAGEMENT_ERROR_DECOMPRESS_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgmoneta_json_clone(payload, &pyl);

         if (config->compression_type == COMPRESSION_CLIENT_GZIP || config->compression_type == COMPRESSION_SERVER_GZIP)
         {
            pgmoneta_set_proc_title(1, ai->argv, "decompress/gzip", NULL);
            pgmoneta_gunzip_request(NULL, client_fd, compression, encryption, pyl);
         }
         else if (config->compression_type == COMPRESSION_CLIENT_ZSTD || config->compression_type == COMPRESSION_SERVER_ZSTD)
         {
            pgmoneta_set_proc_title(1, ai->argv, "decompress/zstd", NULL);
            pgmoneta_zstandardd_request(NULL, client_fd, compression, encryption, pyl);
         }
         else if (config->compression_type == COMPRESSION_CLIENT_LZ4 || config->compression_type == COMPRESSION_SERVER_LZ4)
         {
            pgmoneta_set_proc_title(1, ai->argv, "decompress/lz4", NULL);
            pgmoneta_lz4d_request(NULL, client_fd, compression, encryption, pyl);
         }
         else if (config->compression_type == COMPRESSION_CLIENT_BZIP2)
         {
            pgmoneta_set_proc_title(1, ai->argv, "decompress/bz2", NULL);
            pgmoneta_bunzip2_request(NULL, client_fd, compression, encryption, pyl);
         }
         else
         {
            pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_DECOMPRESS_UNKNOWN, NAME, compression, encryption, payload);
            pgmoneta_log_error("Decompress: Unknown compression (%d)", MANAGEMENT_ERROR_DECOMPRESS_NOFORK);
         }
      }
   }
   else if (id == MANAGEMENT_COMPRESS)
   {
      pid = fork();
      if (pid == -1)
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_COMPRESS_NOFORK, NAME, compression, encryption, payload);
         pgmoneta_log_error("Compress: No fork %s (%d)", server, MANAGEMENT_ERROR_COMPRESS_NOFORK);
         goto error;
      }
      else if (pid == 0)
      {
         struct json* pyl = NULL;

         shutdown_ports();

         pgmoneta_json_clone(payload, &pyl);

         if (config->compression_type == COMPRESSION_CLIENT_GZIP || config->compression_type == COMPRESSION_SERVER_GZIP)
         {
            pgmoneta_set_proc_title(1, ai->argv, "compress/gzip", NULL);
            pgmoneta_gzip_request(NULL, client_fd, compression, encryption, pyl);
         }
         else if (config->compression_type == COMPRESSION_CLIENT_ZSTD || config->compression_type == COMPRESSION_SERVER_ZSTD)
         {
            pgmoneta_set_proc_title(1, ai->argv, "compress/zstd", NULL);
            pgmoneta_zstandardc_request(NULL, client_fd, compression, encryption, pyl);
         }
         else if (config->compression_type == COMPRESSION_CLIENT_LZ4 || config->compression_type == COMPRESSION_SERVER_LZ4)
         {
            pgmoneta_set_proc_title(1, ai->argv, "compress/lz4", NULL);
            pgmoneta_lz4c_request(NULL, client_fd, compression, encryption, pyl);
         }
         else if (config->compression_type == COMPRESSION_CLIENT_BZIP2)
         {
            pgmoneta_set_proc_title(1, ai->argv, "compress/bz2", NULL);
            pgmoneta_bzip2_request(NULL, client_fd, compression, encryption, pyl);
         }
         else
         {
            pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_COMPRESS_UNKNOWN, NAME, compression, encryption, payload);
            pgmoneta_log_error("Compress: Unknown compression (%d)", MANAGEMENT_ERROR_DECOMPRESS_NOFORK);
         }
      }
   }
   else if (id == MANAGEMENT_INFO)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (!strcmp(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }

      if (srv != -1)
      {
         pid = fork();
         if (pid == -1)
         {
            pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_INFO_NOFORK, NAME, compression, encryption, payload);
            pgmoneta_log_error("Info: No fork %s (%d)", server, MANAGEMENT_ERROR_INFO_NOFORK);
            goto error;
         }
         else if (pid == 0)
         {
            struct json* pyl = NULL;

            shutdown_ports();

            pgmoneta_json_clone(payload, &pyl);

            pgmoneta_set_proc_title(1, ai->argv, "info", config->common.servers[srv].name);
            pgmoneta_info_request(NULL, client_fd, srv, compression, encryption, pyl);
         }
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_INFO_NOSERVER, NAME, compression, encryption, payload);
         pgmoneta_log_error("Info: No server %s (%d)", server, MANAGEMENT_ERROR_INFO_NOSERVER);
         goto error;
      }
   }
   else if (id == MANAGEMENT_ANNOTATE)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (!strcmp(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }

      if (srv != -1)
      {
         pid = fork();
         if (pid == -1)
         {
            pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_ANNOTATE_NOFORK, NAME, compression, encryption, payload);
            pgmoneta_log_error("Annotate: No fork %s (%d)", server, MANAGEMENT_ERROR_ANNOTATE_NOFORK);
            goto error;
         }
         else if (pid == 0)
         {
            struct json* pyl = NULL;

            shutdown_ports();

            pgmoneta_json_clone(payload, &pyl);

            pgmoneta_set_proc_title(1, ai->argv, "annotate", config->common.servers[srv].name);
            pgmoneta_annotate_request(NULL, client_fd, srv, compression, encryption, pyl);
         }
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_ANNOTATE_NOSERVER, NAME, compression, encryption, payload);
         pgmoneta_log_error("Annotate: No server %s (%d)", server, MANAGEMENT_ERROR_ANNOTATE_NOSERVER);
         goto error;
      }
   }
   else if (id == MANAGEMENT_MODE)
   {
      char* action = NULL;
      struct timespec start_t;
      struct timespec end_t;
      struct json* response = NULL;

#ifdef HAVE_FREEBSD
      clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);
      action = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_ACTION);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->common.number_of_servers; i++)
      {
         if (!strcmp(config->common.servers[i].name, server))
         {
            srv = i;
         }
      }

      if (srv != -1)
      {
         if (!strcmp(action, "offline"))
         {
            pgmoneta_server_set_online(srv, false);

#ifdef HAVE_FREEBSD
            clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
            clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

            pgmoneta_log_info("Mode: Server %s is offline", config->common.servers[srv].name);

            if (pgmoneta_management_create_response(payload, srv, &response))
            {
               pgmoneta_log_error("Mode: Error sending response for %s", server);
               goto error;
            }

            pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ONLINE, (uintptr_t)pgmoneta_server_is_online(srv), ValueBool);

            if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
            {
               pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_MODE_NETWORK, NAME, compression,
                                                  encryption, payload);
               pgmoneta_log_error("Mode: Error sending response for %s", server);
               goto error;
            }
         }
         else if (!strcmp(action, "online"))
         {
            pgmoneta_server_set_online(srv, true);

            if (pgmoneta_server_verify_connection(srv))
            {
               if (init_replication_slot(srv))
               {
                  pgmoneta_server_set_online(srv, false);
                  pgmoneta_log_warn("Replication: Server %s is offline", config->common.servers[srv].name);
               }
               /* Only start WAL streaming if not already running */
               if (config->common.servers[srv].wal_streaming <= 0)
               {
                  if (init_receivewal(srv))
                  {
                     pgmoneta_server_set_online(srv, false);
                     pgmoneta_log_warn("WAL: Server %s is offline", config->common.servers[srv].name);
                  }
               }
            }
            else
            {
               pgmoneta_server_set_online(srv, false);
               pgmoneta_log_warn("Verify: Server %s is offline", config->common.servers[srv].name);
            }

#ifdef HAVE_FREEBSD
            clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
            clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif
            pgmoneta_log_info("Mode: Server %s is %s", config->common.servers[srv].name,
                              pgmoneta_server_is_online(srv) ? "online" : "offline");

            if (pgmoneta_management_create_response(payload, srv, &response))
            {
               pgmoneta_log_error("Mode: Error sending response for %s", server);
               goto error;
            }

            pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ONLINE, (uintptr_t)pgmoneta_server_is_online(srv), ValueBool);

            if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t,
                                                compression, encryption,
                                                payload))
            {
               pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_MODE_NETWORK, NAME, compression,
                                                  encryption, payload);
               pgmoneta_log_error("Mode: Error sending response for %s", server);
               goto error;
            }
         }
         else
         {
            pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_MODE_UNKNOWN_ACTION, NAME,
                                               compression, encryption, payload);
            pgmoneta_log_error("Mode: Unknown action %s for server %s (%d)", action, server,
                               MANAGEMENT_ERROR_MODE_UNKNOWN_ACTION);
            goto error;
         }
      }
      else
      {
         pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_MODE_NOSERVER, NAME, compression, encryption, payload);
         pgmoneta_log_error("Mode: No server %s (%d)", server, MANAGEMENT_ERROR_MODE_NOSERVER);
         goto error;
      }
   }
   else
   {
      pgmoneta_management_response_error(NULL, client_fd, NULL, MANAGEMENT_ERROR_UNKNOWN_COMMAND, NAME, compression, encryption, payload);
      pgmoneta_log_error("Unknown: %s (%d)", pgmoneta_json_to_string(payload, FORMAT_JSON, NULL, 0), MANAGEMENT_ERROR_UNKNOWN_COMMAND);
      goto error;
   }

   free(str);
   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);

   return;

error:

   free(str);
   pgmoneta_json_destroy(payload);

   pgmoneta_disconnect(client_fd);
}

static void
accept_metrics_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   struct main_configuration* config;
   SSL_CTX* ctx = NULL;
   SSL* client_ssl = NULL;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_debug("accept_metrics_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   config = (struct main_configuration*)shmem;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgmoneta_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_metrics();

         free(metrics_fds);
         metrics_fds = NULL;
         metrics_fds_length = 0;

         if (pgmoneta_bind(config->host, config->metrics, &metrics_fds, &metrics_fds_length))
         {
            pgmoneta_log_fatal("Could not bind to %s:%d", config->host, config->metrics);
            exit(1);
         }

         if (metrics_fds_length > MAX_FDS)
         {
            pgmoneta_log_fatal("Too many descriptors %d", metrics_fds_length);
            exit(1);
         }

         start_metrics();

         for (int i = 0; i < metrics_fds_length; i++)
         {
            pgmoneta_log_debug("Metrics: %d", *(metrics_fds + i));
         }
      }
      else
      {
         pgmoneta_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   if (!fork())
   {
      ev_loop_fork(loop);
      shutdown_ports();
      if (strlen(config->metrics_cert_file) > 0 && strlen(config->metrics_key_file) > 0)
      {
         if (pgmoneta_create_ssl_ctx(false, &ctx))
         {
            pgmoneta_log_error("Could not create metrics SSL context");
            goto child_error;
         }

         if (pgmoneta_create_ssl_server(ctx, config->metrics_key_file, config->metrics_cert_file, config->metrics_ca_file, client_fd, &client_ssl))
         {
            pgmoneta_log_error("Could not create metrics SSL server");
            goto child_error;
         }
      }
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgmoneta_prometheus(client_ssl, client_fd);
      exit(0);
child_error:
      if (client_ssl == NULL && ctx != NULL)
      {
         SSL_CTX_free(ctx);
      }
      pgmoneta_close_ssl(client_ssl);
      pgmoneta_disconnect(client_fd);
      exit(1);
   }

   pgmoneta_close_ssl(client_ssl);
   pgmoneta_disconnect(client_fd);
}

static void
accept_management_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   struct main_configuration* config;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_debug("accept_management_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   memset(&address, 0, sizeof(address));

   config = (struct main_configuration*)shmem;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgmoneta_log_warn("Restarting listening port due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_management();

         free(management_fds);
         management_fds = NULL;
         management_fds_length = 0;

         if (pgmoneta_bind(config->host, config->management, &management_fds, &management_fds_length))
         {
            pgmoneta_log_fatal("Could not bind to %s:%d", config->host, config->management);
            exit(1);
         }

         if (management_fds_length > MAX_FDS)
         {
            pgmoneta_log_fatal("Too many descriptors %d", management_fds_length);
            exit(1);
         }

         start_management();

         for (int i = 0; i < management_fds_length; i++)
         {
            pgmoneta_log_debug("Remote management: %d", *(management_fds + i));
         }
      }
      else
      {
         pgmoneta_log_debug("accept: %s (%d)", strerror(errno), watcher->fd);
      }
      errno = 0;
      return;
   }

   pgmoneta_get_address((struct sockaddr*)&client_addr, (char*)&address, sizeof(address));

   if (!fork())
   {
      char* addr = NULL;

      addr = pgmoneta_append(addr, address);

      ev_loop_fork(loop);
      shutdown_ports();
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgmoneta_remote_management(client_fd, addr);
   }

   pgmoneta_disconnect(client_fd);
}

static void
shutdown_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   pgmoneta_log_debug("shutdown requested (%p, %p, %d)", loop, w, revents);
   ev_break(loop, EVBREAK_ALL);
   keep_running = 0;
   config->running = false;
}

static void
reload_set_configuration(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   bool restart_required = false;

   // Apply configuration changes to shared memory
   if (pgmoneta_conf_set(ssl, client_fd, compression, encryption, payload, &restart_required))
   {
      goto error;
      pgmoneta_log_debug("pgmoneta: configuration changes applied successfully");
   }

   // Only restart services if config change succeeded AND no restart required
   if (restart_required)
   {
      pgmoneta_log_info("Configuration requires restart - continuing with old configuration");
   }
   else
   {
      pgmoneta_log_info("Configuration applied successfully, reloading services");
      kill(getppid(), SIGUSR1);
   }

   exit(0);

error:
   pgmoneta_log_error("Error applying configuration changes");
   exit(1);
}

static bool
reload_services_only(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   shutdown_metrics();

   free(metrics_fds);
   metrics_fds = NULL;
   metrics_fds_length = 0;

   if (config->metrics > 0)
   {
      /* Bind metrics socket */
      if (pgmoneta_bind(config->host, config->metrics, &metrics_fds, &metrics_fds_length))
      {
         pgmoneta_log_fatal("Could not bind to %s:%d", config->host, config->metrics);
         goto error;
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgmoneta_log_fatal("Too many descriptors %d", metrics_fds_length);
         goto error;
      }

      start_metrics();

      for (int i = 0; i < metrics_fds_length; i++)
      {
         pgmoneta_log_debug("Metrics: %d", *(metrics_fds + i));
      }
   }

   shutdown_management();

   free(management_fds);
   management_fds = NULL;
   management_fds_length = 0;

   if (config->management > 0)
   {
      /* Bind management socket */
      if (pgmoneta_bind(config->host, config->management, &management_fds, &management_fds_length))
      {
         pgmoneta_log_fatal("Could not bind to %s:%d", config->host, config->management);
         goto error;
      }

      if (management_fds_length > MAX_FDS)
      {
         pgmoneta_log_fatal("Too many descriptors %d", management_fds_length);
         goto error;
      }

      start_management();

      for (int i = 0; i < management_fds_length; i++)
      {
         pgmoneta_log_debug("Remote management: %d", *(management_fds + i));
      }
   }

   pgmoneta_log_info("conf set: Services restarted successfully");
   return true;

error:
   return false;
}

static void
service_reload_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   pgmoneta_log_debug("pgmoneta: service restart requested (%p, %p, %d)", loop, w, revents);
   reload_services_only();
}

static void
reload_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   pgmoneta_log_debug("reload requested (%p, %p, %d)", loop, w, revents);
   reload_configuration();
}

static void
coredump_cb(struct ev_loop* loop, ev_signal* w, int revents)
{
   pgmoneta_log_info("core dump requested (%p, %p, %d)", loop, w, revents);
   remove_pidfile();
   abort();
}

static void
retention_cb(struct ev_loop* loop __attribute__((unused)), ev_periodic* w __attribute__((unused)), int revents)
{
   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("retention_cb: got invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   if (!fork())
   {
      shutdown_ports();
      pgmoneta_retention(argv_ptr);
   }
}

static void
verification_cb(struct ev_loop* loop __attribute__((unused)), ev_periodic* w __attribute__((unused)), int revents)
{
   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("verification_cb: got invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   if (!fork())
   {
      shutdown_ports();
      pgmoneta_sha512_verification(argv_ptr);
   }
}

static void
valid_cb(struct ev_loop* loop __attribute__((unused)), ev_periodic* w __attribute__((unused)), int revents)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("valid_cb: got invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   if (!fork())
   {
      pgmoneta_start_logging();
      pgmoneta_memory_init();

      for (int i = 0; i < config->common.number_of_servers; i++)
      {
         pgmoneta_log_trace("Valid - Server %s Online %d Primary %d Valid %d WAL %d",
                            config->common.servers[i].name,
                            config->common.servers[i].online,
                            config->common.servers[i].primary,
                            config->common.servers[i].valid,
                            config->common.servers[i].wal_streaming > 0);

         if (keep_running && config->common.servers[i].online && !config->common.servers[i].valid)
         {
            int usr = -1;
            SSL* ssl = NULL;
            int socket = -1;
            int auth = AUTH_ERROR;

            for (int j = 0; usr == -1 && j < config->common.number_of_users; j++)
            {
               if (!strcmp(config->common.servers[i].username, config->common.users[j].username))
               {
                  usr = i;
               }
            }

            auth = pgmoneta_server_authenticate(i, "postgres",
                                                config->common.users[usr].username,
                                                config->common.users[usr].password,
                                                true, &ssl, &socket);

            if (auth != AUTH_SUCCESS)
            {
               pgmoneta_log_error("Authentication failed for user %s on %s",
                                  config->common.users[usr].username,
                                  config->common.servers[i].name);
            }
            else
            {
               pgmoneta_server_info(i, ssl, socket);
            }

            pgmoneta_close_ssl(ssl);
            pgmoneta_disconnect(socket);
         }
      }

      pgmoneta_memory_destroy();
      pgmoneta_stop_logging();

      exit(0);
   }
}

static void
wal_streaming_cb(struct ev_loop* loop __attribute__((unused)), ev_periodic* w __attribute__((unused)), int revents)
{
   bool start = false;
   int follow;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("wal_streaming_cb: got invalid event: %s", strerror(errno));
      return;
   }

   for (int i = 0; i < config->common.number_of_servers; i++)
   {
      if (keep_running && config->common.servers[i].wal_streaming <= 0)
      {
         start = false;

         if (config->common.servers[i].online)
         {
            pgmoneta_log_trace("WAL streaming: Server %s Online %d Valid %d WAL %d CHECKSUMS %d SUMMARIZE_WAL %d",
                               config->common.servers[i].name, config->common.servers[i].online,
                               config->common.servers[i].valid, config->common.servers[i].wal_streaming > 0,
                               config->common.servers[i].checksums, config->common.servers[i].summarize_wal);

            if (strlen(config->common.servers[i].follow) == 0)
            {
               follow = -1;

               for (int j = 0;
                    follow == -1 && j < config->common.number_of_servers; j++)
               {
                  if (!strcmp(config->common.servers[j].follow, config->common.servers[i].name))
                  {
                     follow = j;
                  }
               }

               if (follow == -1)
               {
                  start = true;
               }
               else if (config->common.servers[follow].wal_streaming <= 0)
               {
                  start = true;
               }
            }
            else
            {
               for (int j = 0; !start && j < config->common.number_of_servers;
                    j++)
               {
                  if (!strcmp(config->common.servers[i].follow, config->common.servers[j].name) &&
                      config->common.servers[j].wal_streaming <= 0)
                  {
                     start = true;
                  }
               }
            }
         }
         else
         {
            pgmoneta_log_debug("WAL streaming: Server %s is offline", config->common.servers[i].name);
         }

         if (start)
         {
            pid_t pid;

            pid = fork();
            if (pid == -1)
            {
               /* No process */
               pgmoneta_log_error("pgmoenta: WAL - Cannot create process");
            }
            else if (pid == 0)
            {
               shutdown_ports();
               pgmoneta_wal(i, argv_ptr);
            }
         }
      }
   }
}

static bool
accept_fatal(int error)
{
   switch (error)
   {
      case EAGAIN:
      case ENETDOWN:
      case EPROTO:
      case ENOPROTOOPT:
      case EHOSTDOWN:
#ifdef HAVE_LINUX
      case ENONET:
#endif
      case EHOSTUNREACH:
      case EOPNOTSUPP:
      case ENETUNREACH:
         return false;
         break;
   }

   return true;
}

static bool
reload_configuration(void)
{
   bool restart = false;
   int old_metrics;
   int old_management;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   old_metrics = config->metrics;
   old_management = config->management;

   pgmoneta_reload_configuration(&restart);

   if (old_metrics != config->metrics)
   {
      shutdown_metrics();

      free(metrics_fds);
      metrics_fds = NULL;
      metrics_fds_length = 0;

      if (config->metrics > 0)
      {
         /* Bind metrics socket */
         if (pgmoneta_bind(config->host, config->metrics, &metrics_fds, &metrics_fds_length))
         {
            pgmoneta_log_fatal("Could not bind to %s:%d", config->host, config->metrics);
            exit(1);
         }

         if (metrics_fds_length > MAX_FDS)
         {
            pgmoneta_log_fatal("Too many descriptors %d", metrics_fds_length);
            exit(1);
         }

         start_metrics();

         for (int i = 0; i < metrics_fds_length; i++)
         {
            pgmoneta_log_debug("Metrics: %d", *(metrics_fds + i));
         }
      }
   }

   if (old_management != config->management)
   {
      shutdown_management();

      free(management_fds);
      management_fds = NULL;
      management_fds_length = 0;

      if (config->management > 0)
      {
         /* Bind management socket */
         if (pgmoneta_bind(config->host, config->management, &management_fds, &management_fds_length))
         {
            pgmoneta_log_fatal("Could not bind to %s:%d", config->host, config->management);
            exit(1);
         }

         if (management_fds_length > MAX_FDS)
         {
            pgmoneta_log_fatal("Too many descriptors %d", management_fds_length);
            exit(1);
         }

         start_management();

         for (int i = 0; i < management_fds_length; i++)
         {
            pgmoneta_log_debug("Remote management: %d", *(management_fds + i));
         }
      }
   }

   return restart;
}

static void
init_receivewals(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   for (int i = 0; i < config->common.number_of_servers; i++)
   {
      if (init_receivewal(i))
      {
         pgmoneta_server_set_online(i, false);
         pgmoneta_log_debug("WAL: Server %s is offline", config->common.servers[i].name);
      }
   }
}

static int
init_receivewal(int server)
{
   int ret = 0;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (strlen(config->common.servers[server].follow) == 0)
   {
      if (config->common.servers[server].online)
      {
         pid_t pid;

         pid = fork();
         if (pid == -1)
         {
            /* No process */
            pgmoneta_log_error("WAL - Cannot create process");
            ret = 1;
         }
         else if (pid == 0)
         {
            shutdown_ports();
            pgmoneta_wal(server, argv_ptr);
         }
      }
      else
      {
         ret = 1;
      }
   }

   return ret;
}

static int
init_replication_slots(void)
{
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;

   for (int srv = 0; srv < config->common.number_of_servers; srv++)
   {
      if (init_replication_slot(srv))
      {
         pgmoneta_server_set_online(srv, false);
         pgmoneta_log_debug("Replication: Server %s is offline", config->common.servers[srv].name);
      }
   }

   return 0;
}

static int
init_replication_slot(int server)
{
   int usr = -1;
   int auth = AUTH_ERROR;
   int slot_status = INCORRECT_SLOT_TYPE;
   SSL* ssl = NULL;
   int socket = 0;
   int ret = 0;
   struct message* slot_request_msg = NULL;
   struct message* slot_response_msg = NULL;
   struct main_configuration* config = NULL;
   bool create_slot = false;

   config = (struct main_configuration*)shmem;

   pgmoneta_memory_init();

   usr = -1;
   for (int i = 0; usr == -1 && i < config->common.number_of_users; i++)
   {
      if (!strcmp(config->common.servers[server].username, config->common.users[i].username))
      {
         usr = i;
      }
   }

   if (usr != -1)
   {
      create_slot = config->common.servers[server].create_slot == CREATE_SLOT_YES ||
                    (config->create_slot == CREATE_SLOT_YES && config->common.servers[server].create_slot != CREATE_SLOT_NO);
      socket = 0;
      auth = pgmoneta_server_authenticate(server, "postgres",
                                          config->common.users[usr].username, config->common.users[usr].password,
                                          false, &ssl, &socket);

      if (auth == AUTH_SUCCESS)
      {
         pgmoneta_server_info(server, ssl, socket);

         if (!pgmoneta_server_valid(server))
         {
            pgmoneta_log_error("Could not get version for server %s", config->common.servers[server].name);
            ret = 1;
            goto server_done;
         }

         if (config->common.servers[server].version < POSTGRESQL_MIN_VERSION)
         {
            pgmoneta_log_error("PostgreSQL %d or higher is required for server %s", POSTGRESQL_MIN_VERSION,
                               config->common.servers[server].name);
            ret = 1;
            goto server_done;
         }

         if (config->common.servers[server].version < 15 && (config->compression_type == COMPRESSION_SERVER_GZIP ||
                                                             config->compression_type == COMPRESSION_SERVER_ZSTD ||
                                                             config->compression_type == COMPRESSION_SERVER_LZ4))
         {
            pgmoneta_log_error("PostgreSQL 15 or higher is required for server %s for server side compression",
                               config->common.servers[server].name);
            ret = 1;
            goto server_done;
         }

         if (config->common.servers[server].version >= 17 && !config->common.servers[server].summarize_wal)
         {
            pgmoneta_log_error("PostgreSQL %d or higher requires summarize_wal for server %s",
                               config->common.servers[server].version, config->common.servers[server].name);
            ret = 1;
            goto server_done;
         }

         /* Verify replication slot */
         slot_status = verify_replication_slot(config->common.servers[server].wal_slot, server, ssl, socket);
         if (slot_status == VALID_SLOT)
         {
            /* Ok */
         }
         else if (!create_slot)
         {
            if (slot_status == SLOT_NOT_FOUND)
            {
               pgmoneta_log_error("Replication slot '%s' is not found for server %s",
                                  config->common.servers[server].wal_slot, config->common.servers[server].name);
               ret = 1;
            }
            else if (slot_status == INCORRECT_SLOT_TYPE)
            {
               pgmoneta_log_error("Replication slot '%s' should be physical", config->common.servers[server].wal_slot);
               ret = 1;
            }
         }
      }
      else if (auth == AUTH_BAD_PASSWORD)
      {
         pgmoneta_log_error("Authentication failed for user %s on %s",
                            config->common.users[usr].username, config->common.servers[server].name);
      }

server_done:
      pgmoneta_close_ssl(ssl);
      pgmoneta_disconnect(socket);

      ssl = NULL;
      socket = 0;

      if (ret == 0 && create_slot && slot_status == SLOT_NOT_FOUND)
      {
         auth = pgmoneta_server_authenticate(server, "postgres", config->common.users[usr].username,
                                             config->common.users[usr].password, true, &ssl, &socket);

         if (auth == AUTH_SUCCESS)
         {
            pgmoneta_log_trace("CREATE_SLOT: %s/%s", config->common.servers[server].name, config->common.servers[server].wal_slot);
            pgmoneta_create_replication_slot_message(config->common.servers[server].wal_slot, &slot_request_msg,
                                                     config->common.servers[server].version);
            if (pgmoneta_write_message(ssl, socket, slot_request_msg) == MESSAGE_STATUS_OK)
            {
               if (pgmoneta_read_block_message(ssl, socket, &slot_response_msg) == MESSAGE_STATUS_OK)
               {
                  if (slot_response_msg->kind == 'E')
                  {
                     pgmoneta_log_error_response_message(slot_response_msg);
                     ret = 1;
                  }
                  else
                  {
                     pgmoneta_log_info("Created replication slot %s on %s",
                                       config->common.servers[server].wal_slot, config->common.servers[server].name);
                  }
               }
               else
               {
                  pgmoneta_log_error("Could not read CREATE_REPLICATION_SLOT response for %s", config->common.servers[server].name);
                  ret = 1;
               }
            }
            else
            {
               pgmoneta_log_error("Could not write CREATE_REPLICATION_SLOT request for %s", config->common.servers[server].name);
               ret = 1;
            }
         }
         else if (auth == AUTH_BAD_PASSWORD)
         {
            pgmoneta_log_error("Authentication failed for user %s on %s", config->common.users[usr].username, config->common.servers[server].name);
         }
         else
         {
            pgmoneta_log_debug("Server %s is offline", config->common.servers[server].name);
         }

         pgmoneta_free_message(slot_request_msg);
         slot_request_msg = NULL;

         pgmoneta_clear_message();
         slot_response_msg = NULL;

         pgmoneta_close_ssl(ssl);
         pgmoneta_disconnect(socket);

         ssl = NULL;
         socket = 0;
      }
   }
   else
   {
      pgmoneta_log_error("Invalid user for %s", config->common.servers[server].name);
   }

   pgmoneta_memory_destroy();

   return ret;
}

static int
verify_replication_slot(char* slot_name, int srv, SSL* ssl, int socket)
{
   int ret = VALID_SLOT;
   struct message* query;
   struct query_response* response;
   struct main_configuration* config = NULL;
   struct tuple* current = NULL;

   config = (struct main_configuration*)shmem;

   pgmoneta_create_search_replication_slot_message(slot_name, &query);
   if (pgmoneta_query_execute(ssl, socket, query, &response) || response == NULL)
   {
      pgmoneta_log_error("Could not execute verify replication slot query for %s", config->common.servers[srv].name);
   }
   else
   {
      current = response->tuples;
      if (current == NULL)
      {
         ret = SLOT_NOT_FOUND;
      }
      else if (strcmp(current->data[1], "physical"))
      {
         ret = INCORRECT_SLOT_TYPE;
      }
   }

   pgmoneta_free_message(query);
   pgmoneta_free_query_response(response);

   return ret;
}

static int
create_pidfile(void)
{
   char buffer[64];
   pid_t pid;
   int r;
   int fd;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (strlen(config->pidfile) == 0)
   {
      // no pidfile set, use a default one
      if (!pgmoneta_ends_with(config->common.unix_socket_dir, "/"))
      {
         snprintf(config->pidfile, sizeof(config->pidfile), "%s/pgmoneta.%s.pid",
                  config->common.unix_socket_dir,
                  !strncmp(config->host, "*", sizeof(config->host)) ? "all" : config->host);
      }
      else
      {
         snprintf(config->pidfile, sizeof(config->pidfile), "%spgmoneta.%s.pid",
                  config->common.unix_socket_dir,
                  !strncmp(config->host, "*", sizeof(config->host)) ? "all" : config->host);
      }
      pgmoneta_log_debug("PID file automatically set to: [%s]", config->pidfile);
   }

   if (strlen(config->pidfile) > 0)
   {
      // check pidfile is not there
      if (access(config->pidfile, F_OK) == 0)
      {
         pgmoneta_log_fatal("PID file [%s] exists, is there another instance running ?", config->pidfile);
         goto error;
      }

      pid = getpid();

      fd = open(config->pidfile, O_WRONLY | O_CREAT | O_EXCL, 0644);
      if (fd < 0)
      {
         warn("Could not create PID file '%s'", config->pidfile);
         goto error;
      }

      snprintf(&buffer[0], sizeof(buffer), "%u\n", (unsigned)pid);

      pgmoneta_permission(config->pidfile, 6, 4, 0);

      r = write(fd, &buffer[0], strlen(buffer));
      if (r < 0)
      {
         warn("Could not write pidfile '%s'", config->pidfile);
         goto error;
      }

      close(fd);
   }

   return 0;

error:

   return 1;
}

static void
remove_pidfile(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (strlen(config->pidfile) > 0 && access(config->pidfile, F_OK) == 0)
   {
      unlink(config->pidfile);
   }
}

static void
shutdown_ports(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config->metrics > 0)
   {
      shutdown_metrics();
   }

   if (config->management > 0)
   {
      shutdown_management();
   }
}
