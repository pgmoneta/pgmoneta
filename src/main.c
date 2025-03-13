/*
 * Copyright (C) 2025 The pgmoneta community
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
#include <utils.h>
#include <verify.h>
#include <wal.h>
#include <zstandard_compression.h>

/* system */
#include <err.h>
#include <errno.h>
#include <ev.h>
#include <fcntl.h>
#include <getopt.h>
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
#ifdef HAVE_LINUX
#include <systemd/sd-daemon.h>
#endif

#define NAME "main"
#define MAX_FDS 64
#define OFFLINE 1000

static void accept_mgt_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_metrics_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void accept_management_cb(struct ev_loop* loop, struct ev_io* watcher, int revents);
static void shutdown_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void reload_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void coredump_cb(struct ev_loop* loop, ev_signal* w, int revents);
static void wal_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static void retention_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static void valid_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static void wal_streaming_cb(struct ev_loop* loop, ev_periodic* w, int revents);
static bool accept_fatal(int error);
static bool reload_configuration(void);
static void init_receivewals(void);
static int init_replication_slots(void);
static int verify_replication_slot(char* slot_name, int srv, SSL* ssl, int socket);
static int  create_pidfile(void);
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
static bool offline = false;

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
   struct configuration* config;

   config = (struct configuration*)shmem;

   ev_io_stop(main_loop, (struct ev_io*)&io_mgt);
   pgmoneta_disconnect(unix_management_socket);
   errno = 0;
   pgmoneta_remove_unix_socket(config->unix_socket_dir, MAIN_UDS);
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
   printf("  -c, --config CONFIG_FILE Set the path to the pgmoneta.conf file\n");
   printf("  -u, --users USERS_FILE   Set the path to the pgmoneta_users.conf file\n");
   printf("  -A, --admins ADMINS_FILE Set the path to the pgmoneta_admins.conf file\n");
   printf("  -d, --daemon             Run as a daemon\n");
   printf("      --offline            Run in offline mode\n");
   printf("  -V, --version            Display version information\n");
   printf("  -?, --help               Display help\n");
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
   bool daemon = false;
   bool pid_file_created = false;
   bool management_started = false;
   bool mgt_started = false;
   bool metrics_started = false;
   pid_t pid, sid;
   struct signal_info signal_watcher[5];
   struct ev_periodic wal;
   struct ev_periodic retention;
   struct ev_periodic valid;
   struct ev_periodic wal_streaming;
   size_t shmem_size;
   size_t prometheus_cache_shmem_size = 0;
   struct configuration* config = NULL;
   int ret;
   int c;
   char* os = NULL;

   int kernel_major, kernel_minor, kernel_patch;

   argv_ptr = argv;

   while (1)
   {
      static struct option long_options[] =
      {
         {"config", required_argument, 0, 'c'},
         {"users", required_argument, 0, 'u'},
         {"admins", required_argument, 0, 'A'},
         {"daemon", no_argument, 0, 'd'},
         {"offline", no_argument, 0, OFFLINE},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };
      int option_index = 0;

      c = getopt_long (argc, argv, "dV?c:u:A:",
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
         case 'u':
            users_path = optarg;
            break;
         case 'A':
            admins_path = optarg;
            break;
         case 'd':
            daemon = true;
            break;
         case OFFLINE:
            offline = true;
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
      warnx("pgmoneta: Using the root account is not allowed");
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Using the root account is not allowed");
#endif
      exit(1);
   }

   shmem_size = sizeof(struct configuration);
   if (pgmoneta_create_shared_memory(shmem_size, HUGEPAGE_OFF, &shmem))
   {
      warnx("pgmoneta: Error in creating shared memory");
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating shared memory");
#endif
      goto error;
   }

   pgmoneta_init_configuration(shmem);
   config = (struct configuration*)shmem;

   if (configuration_path != NULL)
   {
      if (pgmoneta_read_configuration(shmem, configuration_path))
      {
         warnx("pgmoneta: Configuration not found: %s", configuration_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Configuration not found: %s", configuration_path);
#endif
         goto error;
      }
   }
   else
   {
      if (pgmoneta_read_configuration(shmem, "/etc/pgmoneta/pgmoneta.conf"))
      {
         warnx("pgmoneta: Configuration not found: /etc/pgmoneta/pgmoneta.conf");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Configuration not found: /etc/pgmoneta/pgmoneta.conf");
#endif
         goto error;
      }
      configuration_path = "/etc/pgmoneta/pgmoneta.conf";
   }
   memcpy(&config->configuration_path[0], configuration_path, MIN(strlen(configuration_path), MAX_PATH - 1));

   if (users_path != NULL)
   {
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret == 1)
      {
         warnx("pgmoneta: USERS configuration not found: %s", users_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=USERS configuration not found: %s", users_path);
#endif
         goto error;
      }
      else if (ret == 2)
      {
         warnx("pgmoneta: Invalid master key file");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         goto error;
      }
      else if (ret == 3)
      {
         warnx("pgmoneta: USERS: Too many users defined %d (max %d)", config->number_of_users, NUMBER_OF_USERS);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=USERS: Too many users defined %d (max %d)", config->number_of_users, NUMBER_OF_USERS);
#endif
         goto error;
      }
      memcpy(&config->users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
   }
   else
   {
      users_path = "/etc/pgmoneta/pgmoneta_users.conf";
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret == 0)
      {
         memcpy(&config->users_path[0], users_path, MIN(strlen(users_path), MAX_PATH - 1));
      }
   }

   if (admins_path != NULL)
   {
      ret = pgmoneta_read_admins_configuration(shmem, admins_path);
      if (ret == 1)
      {
         warnx("pgmoneta: ADMINS configuration not found: %s", admins_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=ADMINS configuration not found: %s", admins_path);
#endif
         goto error;
      }
      else if (ret == 2)
      {
         warnx("pgmoneta: Invalid master key file");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         goto error;
      }
      else if (ret == 3)
      {
         warnx("pgmoneta: ADMINS: Too many admins defined %d (max %d)", config->number_of_admins, NUMBER_OF_ADMINS);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=ADMINS: Too many admins defined %d (max %d)", config->number_of_admins, NUMBER_OF_ADMINS);
#endif
         goto error;
      }
      memcpy(&config->admins_path[0], admins_path, MIN(strlen(admins_path), MAX_PATH - 1));
   }
   else
   {
      admins_path = "/etc/pgmoneta/pgmoneta_admins.conf";
      ret = pgmoneta_read_admins_configuration(shmem, admins_path);
      if (ret == 0)
      {
         memcpy(&config->admins_path[0], admins_path, MIN(strlen(admins_path), MAX_PATH - 1));
      }
   }

   if (pgmoneta_init_logging())
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Failed to init logging");
#endif
      goto error;
   }

   if (pgmoneta_start_logging())
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Failed to start logging");
#endif
      goto error;
   }

   if (pgmoneta_validate_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid configuration");
#endif
      goto error;
   }
   if (pgmoneta_validate_users_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid USERS configuration");
#endif
      goto error;
   }
   if (pgmoneta_validate_admins_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid ADMINS configuration");
#endif
      goto error;
   }

   config = (struct configuration*)shmem;

   if (!offline && daemon)
   {
      if (config->log_type == PGMONETA_LOGGING_TYPE_CONSOLE)
      {
         warnx("pgmoneta: Daemon mode can't be used with console logging");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Daemon mode can't be used with console logging");
#endif
         goto error;
      }

      pid = fork();

      if (pid < 0)
      {
         warnx("pgmoneta: Daemon mode failed");
#ifdef HAVE_LINUX
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
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating and initializing prometheus cache shared memory");
#endif
      errx(1, "Error in creating and initializing prometheus cache shared memory");
   }

   /* Bind Unix Domain Socket */
   if (pgmoneta_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
   {
      pgmoneta_log_fatal("Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#endif
      goto error;
   }

   /* libev */
   main_loop = ev_default_loop(pgmoneta_libev(config->libev));
   if (!main_loop)
   {
      pgmoneta_log_fatal("No loop implementation (%x) (%x)",
                         pgmoneta_libev(config->libev), ev_supported_backends());
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=No loop implementation (%x) (%x)", pgmoneta_libev(config->libev), ev_supported_backends());
#endif
      goto error;
   }

   ev_signal_init((struct ev_signal*)&signal_watcher[0], shutdown_cb, SIGTERM);
   ev_signal_init((struct ev_signal*)&signal_watcher[1], reload_cb, SIGHUP);
   ev_signal_init((struct ev_signal*)&signal_watcher[2], shutdown_cb, SIGINT);
   ev_signal_init((struct ev_signal*)&signal_watcher[3], coredump_cb, SIGABRT);
   ev_signal_init((struct ev_signal*)&signal_watcher[4], shutdown_cb, SIGALRM);

   for (int i = 0; i < 5; i++)
   {
      signal_watcher[i].slot = -1;
      ev_signal_start(main_loop, (struct ev_signal*)&signal_watcher[i]);
   }

   if (pgmoneta_tls_valid())
   {
      pgmoneta_log_fatal("Invalid TLS configuration");
#ifdef HAVE_LINUX
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
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->metrics);
#endif
         goto error;
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgmoneta_log_fatal("Too many descriptors %d", metrics_fds_length);
#ifdef HAVE_LINUX
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
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->management);
#endif
         goto error;
      }

      if (management_fds_length > MAX_FDS)
      {
         pgmoneta_log_fatal("Too many descriptors %d", management_fds_length);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Too many descriptors %d", management_fds_length);
#endif
         goto error;
      }

      start_management();
      management_started = true;
   }

   /* Create and/or validate replication slots */
   if (!offline && init_replication_slots())
   {
      goto error;
   }

   if (!offline)
   {
      /* Start to retrieve WAL */
      init_receivewals();

      /* Start to validate server configuration */
      ev_periodic_init (&valid, valid_cb, 0., 600, 0);
      ev_periodic_start (main_loop, &valid);

      /* Start to verify WAL streaming */
      ev_periodic_init (&wal_streaming, wal_streaming_cb, 0., 60, 0);
      ev_periodic_start (main_loop, &wal_streaming);
   }

   if (!offline)
   {
      /* Start WAL compression */
      if (config->compression_type != COMPRESSION_NONE ||
          config->encryption != ENCRYPTION_NONE)
      {
         ev_periodic_init(&wal, wal_cb, 0., 60, 0);
         ev_periodic_start(main_loop, &wal);
      }
   }

   if (!offline)
   {
      /* Start backup retention policy */
      ev_periodic_init(&retention, retention_cb, 0., config->retention_interval, 0);
      ev_periodic_start(main_loop, &retention);
   }

   if (!offline)
   {
      pgmoneta_log_info("Started on %s", config->host);
   }
   else
   {
      pgmoneta_log_info("Started on %s (offline)", config->host);
   }
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
   pgmoneta_log_debug("Known users: %d", config->number_of_users);
   pgmoneta_log_debug("Known admins: %d", config->number_of_admins);

   pgmoneta_os_kernel_version(&os, &kernel_major, &kernel_minor, &kernel_patch);

   free(os);

#ifdef HAVE_LINUX
   sd_notifyf(0,
              "READY=1\n"
              "STATUS=Running\n"
              "MAINPID=%lu", (unsigned long)getpid());
#endif

   while (keep_running)
   {
      ev_loop(main_loop, 0);
   }

   pgmoneta_log_info("Shutdown");
#ifdef HAVE_LINUX
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
   struct configuration* config;
   uint8_t compression = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("accept_mgt_cb: got invalid event: %s", strerror(errno));
      return;
   }

   config = (struct configuration*)shmem;
   ai = (struct accept_io*)watcher;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr*)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgmoneta_log_warn("Restarting management due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_mgt();

         if (pgmoneta_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
         {
            pgmoneta_log_fatal("Could not bind to %s", config->unix_socket_dir);
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

   header = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_HEADER);
   id = (int32_t)pgmoneta_json_get(header, MANAGEMENT_ARGUMENT_COMMAND);

   str = pgmoneta_json_to_string(payload, FORMAT_JSON, NULL, 0);
   pgmoneta_log_debug("Management %d: %s", id, str);

   request = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);

   if (id == MANAGEMENT_BACKUP)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      if (!offline)
      {
         srv = -1;
         for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
         {
            if (!strcmp(config->servers[i].name, server))
            {
               srv = i;
            }
         }

         if (srv != -1)
         {
            pid = fork();
            if (pid == -1)
            {
               pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_BACKUP_NOFORK, NAME, compression, encryption, payload);
               pgmoneta_log_error("Backup: No fork (%d)", MANAGEMENT_ERROR_BACKUP_NOFORK);
               goto error;
            }
            else if (pid == 0)
            {
               struct json* pyl = NULL;

               shutdown_ports();

               pgmoneta_json_clone(payload, &pyl);

               pgmoneta_set_proc_title(1, ai->argv, "backup", config->servers[srv].name);
               pgmoneta_backup(client_fd, srv, compression, encryption, pyl);
            }
         }
         else
         {
            pgmoneta_management_response_error(NULL, client_fd, server, MANAGEMENT_ERROR_BACKUP_NOSERVER, NAME, compression, encryption, payload);
            pgmoneta_log_error("Backup: No server %s (%d)", server, MANAGEMENT_ERROR_BACKUP_NOSERVER);
            goto error;
         }
      }
      else
      {
         pgmoneta_log_warn("Can not create backups in offline mode");

         pgmoneta_management_response_error(NULL, client_fd, (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER), MANAGEMENT_ERROR_BACKUP_OFFLINE, NAME, compression, encryption, payload);
         pgmoneta_log_error("Offline: Server %s (%d)", server, MANAGEMENT_ERROR_BACKUP_OFFLINE);
      }
   }
   else if (id == MANAGEMENT_LIST_BACKUP)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
      {
         if (!strcmp(config->servers[i].name, server))
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

            pgmoneta_set_proc_title(1, ai->argv, "list-backup", config->servers[srv].name);
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
      for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
      {
         if (!strcmp(config->servers[i].name, server))
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

            pgmoneta_set_proc_title(1, ai->argv, "delete", config->servers[srv].name);
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
      for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
      {
         if (!strcmp(config->servers[i].name, server))
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

            pgmoneta_set_proc_title(1, ai->argv, "restore", config->servers[srv].name);
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
      for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
      {
         if (!strcmp(config->servers[i].name, server))
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

            pgmoneta_set_proc_title(1, ai->argv, "verify", config->servers[srv].name);
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
      for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
      {
         if (!strcmp(config->servers[i].name, server))
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

            pgmoneta_set_proc_title(1, ai->argv, "archive", config->servers[srv].name);
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
      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

      pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload);

      ev_break(loop, EVBREAK_ALL);
      keep_running = 0;
      stop = 1;
      config->running = false;
   }
   else if (id == MANAGEMENT_PING)
   {
      struct json* response = NULL;

      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

      pgmoneta_management_create_response(payload, -1, &response);

      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

      pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_RESET)
   {
      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

      pgmoneta_prometheus_reset();

      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

      pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_RELOAD)
   {
      bool restart = false;
      struct json* response = NULL;

      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

      restart = reload_configuration();

      pgmoneta_management_create_response(payload, -1, &response);

      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_RESTART, (uintptr_t)restart, ValueBool);

      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

      pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload);
   }
   else if (id == MANAGEMENT_CONF_LS)
   {
      struct json* response = NULL;

      clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);

      pgmoneta_management_create_response(payload, -1, &response);

      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_MAIN_CONF_PATH, (uintptr_t)config->configuration_path, ValueString);
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_USER_CONF_PATH, (uintptr_t)config->users_path, ValueString);
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH, (uintptr_t)config->admins_path, ValueString);

      clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);

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
         pgmoneta_conf_set(NULL, client_fd, compression, encryption, pyl);
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
         pgmoneta_status(NULL, client_fd, offline, compression, encryption, pyl);
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
         pgmoneta_status_details(NULL, client_fd, offline, compression, encryption, pyl);
      }
   }
   else if (id == MANAGEMENT_RETAIN)
   {
      server = (char*)pgmoneta_json_get(request, MANAGEMENT_ARGUMENT_SERVER);

      srv = -1;
      for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
      {
         if (!strcmp(config->servers[i].name, server))
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

            pgmoneta_set_proc_title(1, ai->argv, "retain", config->servers[srv].name);
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
      for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
      {
         if (!strcmp(config->servers[i].name, server))
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

            pgmoneta_set_proc_title(1, ai->argv, "expunge", config->servers[srv].name);
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
      for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
      {
         if (!strcmp(config->servers[i].name, server))
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

            pgmoneta_set_proc_title(1, ai->argv, "info", config->servers[srv].name);
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
      for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
      {
         if (!strcmp(config->servers[i].name, server))
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
            pgmoneta_log_error("Annotate: No fork %s (%d)", server, MANAGEMENT_ERROR_INFO_NOFORK);
            goto error;
         }
         else if (pid == 0)
         {
            struct json* pyl = NULL;

            shutdown_ports();

            pgmoneta_json_clone(payload, &pyl);

            pgmoneta_set_proc_title(1, ai->argv, "annotate", config->servers[srv].name);
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
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_debug("accept_metrics_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   config = (struct configuration*)shmem;

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
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgmoneta_prometheus(client_fd);
   }

   pgmoneta_disconnect(client_fd);
}

static void
accept_management_cb(struct ev_loop* loop, struct ev_io* watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   char address[INET6_ADDRSTRLEN];
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_debug("accept_management_cb: invalid event: %s", strerror(errno));
      errno = 0;
      return;
   }

   memset(&address, 0, sizeof(address));

   config = (struct configuration*)shmem;

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
   struct configuration* config;

   config = (struct configuration*)shmem;

   pgmoneta_log_debug("shutdown requested (%p, %p, %d)", loop, w, revents);
   ev_break(loop, EVBREAK_ALL);
   keep_running = 0;
   config->running = false;
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
wal_cb(struct ev_loop* loop, ev_periodic* w, int revents)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("wal_cb: got invalid event: %s", strerror(errno));
      return;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      /* Compression is always in a fork() */
      if (!fork())
      {
         bool active = false;
         char* d = NULL;

         pgmoneta_set_proc_title(1, argv_ptr, "wal", config->servers[i].name);

         shutdown_ports();

         if (atomic_compare_exchange_strong(&config->servers[i].wal, &active, true))
         {
            d = pgmoneta_get_server_wal(i);

            if (config->compression_type == COMPRESSION_CLIENT_GZIP || config->compression_type == COMPRESSION_SERVER_GZIP)
            {
               pgmoneta_gzip_wal(d);
            }
            else if (config->compression_type == COMPRESSION_CLIENT_ZSTD || config->compression_type == COMPRESSION_SERVER_ZSTD)
            {
               pgmoneta_zstandardc_wal(d);
            }
            else if (config->compression_type == COMPRESSION_CLIENT_LZ4 || config->compression_type == COMPRESSION_SERVER_LZ4)
            {
               pgmoneta_lz4c_wal(d);
            }
            else if (config->compression_type == COMPRESSION_CLIENT_BZIP2)
            {
               pgmoneta_bzip2_wal(d);
            }

            if (config->encryption != ENCRYPTION_NONE)
            {
               pgmoneta_encrypt_wal(d);
            }

            free(d);

            atomic_store(&config->servers[i].wal, false);
         }

         exit(0);
      }
   }
}

static void
retention_cb(struct ev_loop* loop, ev_periodic* w, int revents)
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
valid_cb(struct ev_loop* loop, ev_periodic* w, int revents)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

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

      for (int i = 0; i < config->number_of_servers; i++)
      {
         pgmoneta_log_trace("Valid - Server %d Valid %d WAL %d", i, config->servers[i].valid, config->servers[i].wal_streaming);

         if (keep_running && !config->servers[i].valid)
         {
            pgmoneta_server_info(i);
         }
      }

      pgmoneta_memory_destroy();
      pgmoneta_stop_logging();

      exit(0);
   }
}

static void
wal_streaming_cb(struct ev_loop* loop, ev_periodic* w, int revents)
{
   bool start = false;
   int follow;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("wal_streaming_cb: got invalid event: %s", strerror(errno));
      return;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      pgmoneta_log_trace("WAL streaming - Server %d Valid %d WAL %d CHECKSUMS %d SUMMARIZE_WAL %d",
                         i, config->servers[i].valid, config->servers[i].wal_streaming,
                         config->servers[i].checksums, config->servers[i].summarize_wal);

      if (keep_running && !config->servers[i].wal_streaming)
      {
         start = false;

         if (strlen(config->servers[i].follow) == 0)
         {
            follow = -1;

            for (int j = 0; follow == -1 && j < config->number_of_servers; j++)
            {
               if (!strcmp(config->servers[j].follow, config->servers[i].name))
               {
                  follow = j;
               }
            }

            if (follow == -1)
            {
               start = true;
            }
            else if (!config->servers[follow].wal_streaming)
            {
               start = true;
            }
         }
         else
         {
            for (int j = 0; !start && j < config->number_of_servers; j++)
            {
               if (!strcmp(config->servers[i].follow, config->servers[j].name) && !config->servers[j].wal_streaming)
               {
                  start = true;
               }
            }
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
   struct configuration* config;

   config = (struct configuration*)shmem;

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
   int active = 0;
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_servers; i++)
   {
      if (strlen(config->servers[i].follow) == 0)
      {
         pid_t pid;

         pid = fork();
         if (pid == -1)
         {
            /* No process */
            pgmoneta_log_error("WAL - Cannot create process");
         }
         else if (pid == 0)
         {
            shutdown_ports();
            pgmoneta_wal(i, argv_ptr);
         }
         else
         {
            active++;
         }
      }
   }

   if (active == 0)
   {
      pgmoneta_log_error("No active WAL streaming");
   }
}

static int
init_replication_slots(void)
{
   int usr;
   int auth = AUTH_ERROR;
   int slot_status = INCORRECT_SLOT_TYPE;
   SSL* ssl = NULL;
   int socket;
   int ret = 0;
   struct message* slot_request_msg = NULL;
   struct message* slot_response_msg = NULL;
   struct configuration* config = NULL;
   bool create_slot = false;

   config = (struct configuration*)shmem;

   pgmoneta_memory_init();

   for (int srv = 0; srv < config->number_of_servers; srv++)
   {
      usr = -1;

      for (int i = 0; usr == -1 && i < config->number_of_users; i++)
      {
         if (!strcmp(config->servers[srv].username, config->users[i].username))
         {
            usr = i;
         }
      }

      if (usr != -1)
      {
         create_slot = config->servers[srv].create_slot == CREATE_SLOT_YES ||
                       (config->create_slot == CREATE_SLOT_YES && config->servers[srv].create_slot != CREATE_SLOT_NO);
         socket = 0;
         auth = pgmoneta_server_authenticate(srv, "postgres", config->users[usr].username, config->users[usr].password, false, &ssl, &socket);

         if (auth == AUTH_SUCCESS)
         {
            pgmoneta_server_info(srv);

            if (!pgmoneta_server_valid(srv))
            {
               pgmoneta_log_fatal("Could not get version for server %s", config->servers[srv].name);
               ret = 1;
               goto server_done;
            }

            if (config->servers[srv].version < POSTGRESQL_MIN_VERSION)
            {
               pgmoneta_log_fatal("PostgreSQL %d or higher is required for server %s", POSTGRESQL_MIN_VERSION, config->servers[srv].name);
               ret = 1;
               goto server_done;
            }

            if (config->servers[srv].version < 15 && (config->compression_type == COMPRESSION_SERVER_GZIP ||
                                                      config->compression_type == COMPRESSION_SERVER_ZSTD ||
                                                      config->compression_type == COMPRESSION_SERVER_LZ4))
            {
               pgmoneta_log_fatal("PostgreSQL 15 or higher is required for server %s for server side compression", config->servers[srv].name);
               ret = 1;
               goto server_done;
            }

            if (config->servers[srv].version >= 17 && !config->servers[srv].summarize_wal)
            {
               pgmoneta_log_fatal("PostgreSQL %d or higher requires summarize_wal for server %s",
                                  config->servers[srv].version, config->servers[srv].name);
               ret = 1;
               goto server_done;
            }

            /* Verify replication slot */
            slot_status = verify_replication_slot(config->servers[srv].wal_slot, srv, ssl, socket);
            if (slot_status == VALID_SLOT)
            {
               /* Ok */
            }
            else if (!create_slot)
            {
               if (slot_status == SLOT_NOT_FOUND)
               {
                  pgmoneta_log_fatal("Replication slot '%s' is not found for server %s", config->servers[srv].wal_slot, config->servers[srv].name);
                  ret = 1;
               }
               else if (slot_status == INCORRECT_SLOT_TYPE)
               {
                  pgmoneta_log_fatal("Replication slot '%s' should be physical", config->servers[srv].wal_slot);
                  ret = 1;
               }
            }
         }
         else
         {
            pgmoneta_log_error("Authentication failed for user %s on %s", config->users[usr].username, config->servers[srv].name);
            ret = 1;
         }

         pgmoneta_close_ssl(ssl);
         pgmoneta_disconnect(socket);
         socket = 0;

         if (create_slot && slot_status == SLOT_NOT_FOUND)
         {
            auth = pgmoneta_server_authenticate(srv, "postgres", config->users[usr].username, config->users[usr].password, true, &ssl, &socket);

            if (auth == AUTH_SUCCESS)
            {
               pgmoneta_log_trace("CREATE_SLOT: %s/%s", config->servers[srv].name, config->servers[srv].wal_slot);

               pgmoneta_create_replication_slot_message(config->servers[srv].wal_slot, &slot_request_msg, config->servers[srv].version);
               if (pgmoneta_write_message(ssl, socket, slot_request_msg) == MESSAGE_STATUS_OK)
               {
                  if (pgmoneta_read_block_message(ssl, socket, &slot_response_msg) == MESSAGE_STATUS_OK)
                  {
                     pgmoneta_log_info("Created replication slot %s on %s", config->servers[srv].wal_slot, config->servers[srv].name);
                  }
                  else
                  {
                     pgmoneta_log_error("Could not read CREATE_REPLICATION_SLOT response for %s", config->servers[srv].name);
                  }
               }
               else
               {
                  pgmoneta_log_error("Could not write CREATE_REPLICATION_SLOT request for %s", config->servers[srv].name);
               }

               pgmoneta_free_message(slot_request_msg);
               slot_request_msg = NULL;

               pgmoneta_clear_message();
               slot_response_msg = NULL;
            }
            else
            {
               pgmoneta_log_error("Authentication failed for user on %s", config->servers[srv].name);
            }

server_done:
            pgmoneta_close_ssl(ssl);
            pgmoneta_disconnect(socket);
         }
      }
      else
      {
         pgmoneta_log_error("Invalid user for %s", config->servers[srv].name);
      }
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
   struct configuration* config = NULL;
   struct tuple* current = NULL;

   config = (struct configuration*)shmem;

   pgmoneta_create_search_replication_slot_message(slot_name, &query);
   if (pgmoneta_query_execute(ssl, socket, query, &response) || response == NULL)
   {
      pgmoneta_log_error("Could not execute verify replication slot query for %s", config->servers[srv].name);
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
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (strlen(config->pidfile) == 0)
   {
      // no pidfile set, use a default one
      if (!pgmoneta_ends_with(config->unix_socket_dir, "/"))
      {
         snprintf(config->pidfile, sizeof(config->pidfile), "%s/pgmoneta.%s.pid",
                  config->unix_socket_dir,
                  !strncmp(config->host, "*", sizeof(config->host)) ? "all" : config->host);
      }
      else
      {
         snprintf(config->pidfile, sizeof(config->pidfile), "%spgmoneta.%s.pid",
                  config->unix_socket_dir,
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
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (strlen(config->pidfile) > 0 && access(config->pidfile, F_OK) == 0)
   {
      unlink(config->pidfile);
   }
}

static void
shutdown_ports(void)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (config->metrics > 0)
   {
      shutdown_metrics();
   }

   if (config->management > 0)
   {
      shutdown_management();
   }
}
