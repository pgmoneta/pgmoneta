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
#include <archive.h>
#include <backup.h>
#include <configuration.h>
#include <delete.h>
#include <gzip.h>
#include <info.h>
#include <keep.h>
#include <logging.h>
#include <management.h>
#include <memory.h>
#include <network.h>
#include <prometheus.h>
#include <remote.h>
#include <restore.h>
#include <retention.h>
#include <security.h>
#include <server.h>
#include <shmem.h>
#include <utils.h>
#include <wal.h>
#include <zstandard.h>

/* system */
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

#define MAX_FDS 64

static void accept_mgt_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void accept_metrics_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void accept_management_cb(struct ev_loop *loop, struct ev_io *watcher, int revents);
static void shutdown_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void reload_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void coredump_cb(struct ev_loop *loop, ev_signal *w, int revents);
static void wal_compress_cb(struct ev_loop *loop, ev_periodic *w, int revents);
static void retention_cb(struct ev_loop *loop, ev_periodic *w, int revents);
static void valid_cb(struct ev_loop *loop, ev_periodic *w, int revents);
static void wal_streaming_cb(struct ev_loop *loop, ev_periodic *w, int revents);
static bool accept_fatal(int error);
static void reload_configuration(void);
static void init_receivewals(void);
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
   printf("  -V, --version            Display version information\n");
   printf("  -?, --help               Display help\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

int
main(int argc, char **argv)
{
   char* configuration_path = NULL;
   char* users_path = NULL;
   char* admins_path = NULL;
   bool daemon = false;
   pid_t pid, sid;
   struct signal_info signal_watcher[5];
   struct ev_periodic wal_compress;
   struct ev_periodic retention;
   struct ev_periodic valid;
   struct ev_periodic wal_streaming;
   size_t shmem_size;
   struct configuration* config = NULL;
   int ret;
   int c;

   argv_ptr = argv;

   while (1)
   {
      static struct option long_options[] =
      {
         {"config",  required_argument, 0, 'c'},
         {"users", required_argument, 0, 'u'},
         {"admins", required_argument, 0, 'A'},
         {"daemon", no_argument, 0, 'd'},
         {"version", no_argument, 0, 'V'},
         {"help", no_argument, 0, '?'}
      };
      int option_index = 0;

      c = getopt_long (argc, argv, "dV?c:u:A:",
                       long_options, &option_index);

      if (c == -1)
         break;

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
      printf("pgmoneta: Using the root account is not allowed\n");
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Using the root account is not allowed");
#endif
      exit(1);
   }

   shmem_size = sizeof(struct configuration);
   if (pgmoneta_create_shared_memory(shmem_size, HUGEPAGE_OFF, &shmem))
   {
      printf("pgmoneta: Error in creating shared memory\n");
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Error in creating shared memory");
#endif
      exit(1);
   }

   pgmoneta_init_configuration(shmem);
   config = (struct configuration*)shmem;

   if (configuration_path != NULL)
   {
      if (pgmoneta_read_configuration(shmem, configuration_path))
      {
         printf("pgmoneta: Configuration not found: %s\n", configuration_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Configuration not found: %s", configuration_path);
#endif
         exit(1);
      }
   }
   else
   {
      if (pgmoneta_read_configuration(shmem, "/etc/pgmoneta/pgmoneta.conf"))
      {
         printf("pgmoneta: Configuration not found: /etc/pgmoneta/pgmoneta.conf\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Configuration not found: /etc/pgmoneta/pgmoneta.conf");
#endif
         exit(1);
      }
      configuration_path = "/etc/pgmoneta/pgmoneta.conf";
   }
   memcpy(&config->configuration_path[0], configuration_path, MIN(strlen(configuration_path), MAX_PATH - 1));

   if (users_path != NULL)
   {
      ret = pgmoneta_read_users_configuration(shmem, users_path);
      if (ret == 1)
      {
         printf("pgmoneta: USERS configuration not found: %s\n", users_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=USERS configuration not found: %s", users_path);
#endif
         exit(1);
      }
      else if (ret == 2)
      {
         printf("pgmoneta: Invalid master key file\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         exit(1);
      }
      else if (ret == 3)
      {
         printf("pgmoneta: USERS: Too many users defined %d (max %d)\n", config->number_of_users, NUMBER_OF_USERS);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=USERS: Too many users defined %d (max %d)", config->number_of_users, NUMBER_OF_USERS);
#endif
         exit(1);
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
         printf("pgmoneta: ADMINS configuration not found: %s\n", admins_path);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=ADMINS configuration not found: %s", admins_path);
#endif
         exit(1);
      }
      else if (ret == 2)
      {
         printf("pgmoneta: Invalid master key file\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Invalid master key file");
#endif
         exit(1);
      }
      else if (ret == 3)
      {
         printf("pgmoneta: ADMINS: Too many admins defined %d (max %d)\n", config->number_of_admins, NUMBER_OF_ADMINS);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=ADMINS: Too many admins defined %d (max %d)", config->number_of_admins, NUMBER_OF_ADMINS);
#endif
         exit(1);
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
      exit(1);
   }

   if (pgmoneta_start_logging())
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Failed to start logging");
#endif
      exit(1);
   }

   if (pgmoneta_validate_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid configuration");
#endif
      exit(1);
   }
   if (pgmoneta_validate_users_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid USERS configuration");
#endif
      exit(1);
   }
   if (pgmoneta_validate_admins_configuration(shmem))
   {
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid ADMINS configuration");
#endif
      exit(1);
   }

   config = (struct configuration*)shmem;

   if (daemon)
   {
      if (config->log_type == PGMONETA_LOGGING_TYPE_CONSOLE)
      {
         printf("pgmoneta: Daemon mode can't be used with console logging\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Daemon mode can't be used with console logging");
#endif
         exit(1);
      }

      pid = fork();

      if (pid < 0)
      {
         printf("pgmoneta: Daemon mode failed\n");
#ifdef HAVE_LINUX
         sd_notify(0, "STATUS=Daemon mode failed");
#endif
         exit(1);
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

   if (create_pidfile())
   {
      exit(1);
   }

   pgmoneta_set_proc_title(argc, argv, "main", NULL);

   /* Bind Unix Domain Socket */
   if (pgmoneta_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
   {
      pgmoneta_log_fatal("pgmoneta: Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=Could not bind to %s/%s", config->unix_socket_dir, MAIN_UDS);
#endif
      exit(1);
   }

   /* libev */
   main_loop = ev_default_loop(pgmoneta_libev(config->libev));
   if (!main_loop)
   {
      pgmoneta_log_fatal("pgmoneta: No loop implementation (%x) (%x)",
                     pgmoneta_libev(config->libev), ev_supported_backends());
#ifdef HAVE_LINUX
      sd_notifyf(0, "STATUS=No loop implementation (%x) (%x)", pgmoneta_libev(config->libev), ev_supported_backends());
#endif
      exit(1);
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
      pgmoneta_log_fatal("pgmoneta: Invalid TLS configuration");
#ifdef HAVE_LINUX
      sd_notify(0, "STATUS=Invalid TLS configuration");
#endif
      exit(1);
   }

   start_mgt();

   if (config->metrics > 0)
   {
      /* Bind metrics socket */
      if (pgmoneta_bind(config->host, config->metrics, &metrics_fds, &metrics_fds_length))
      {
         pgmoneta_log_fatal("pgmoneta: Could not bind to %s:%d", config->host, config->metrics);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->metrics);
#endif
         exit(1);
      }

      if (metrics_fds_length > MAX_FDS)
      {
         pgmoneta_log_fatal("pgmoneta: Too many descriptors %d", metrics_fds_length);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Too many descriptors %d", metrics_fds_length);
#endif
         exit(1);
      }

      start_metrics();
   }

   if (config->management > 0)
   {
      /* Bind management socket */
      if (pgmoneta_bind(config->host, config->management, &management_fds, &management_fds_length))
      {
         pgmoneta_log_fatal("pgmoneta: Could not bind to %s:%d", config->host, config->management);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Could not bind to %s:%d", config->host, config->management);
#endif
         exit(1);
      }

      if (management_fds_length > MAX_FDS)
      {
         pgmoneta_log_fatal("pgmoneta: Too many descriptors %d", management_fds_length);
#ifdef HAVE_LINUX
         sd_notifyf(0, "STATUS=Too many descriptors %d", management_fds_length);
#endif
         exit(1);
      }

      start_management();
   }

   /* Start all pg_receivewal processes */
   init_receivewals();

   /* Start WAL compression */
   if (config->compression_type != COMPRESSION_NONE)
   {
      ev_periodic_init (&wal_compress, wal_compress_cb, 0., 60, 0);
      ev_periodic_start (main_loop, &wal_compress);
   }

   ev_periodic_init (&retention, retention_cb, 0., 300, 0);
   ev_periodic_start (main_loop, &retention);

   ev_periodic_init (&valid, valid_cb, 0., 600, 0);
   ev_periodic_start (main_loop, &valid);

   ev_periodic_init (&wal_streaming, wal_streaming_cb, 0., 60, 0);
   ev_periodic_start (main_loop, &wal_streaming);

   pgmoneta_log_info("pgmoneta: started on %s", config->host);
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
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   pgmoneta_log_debug("%s", SSLeay_version(SSLEAY_VERSION));
#else
   pgmoneta_log_debug("%s", OpenSSL_version(OPENSSL_VERSION));
#endif
   pgmoneta_log_debug("Configuration size: %lu", shmem_size);
   pgmoneta_log_debug("Known users: %d", config->number_of_users);
   pgmoneta_log_debug("Known admins: %d", config->number_of_admins);

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

   pgmoneta_log_info("pgmoneta: shutdown");
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

   if (daemon || stop)
   {
      kill(0, SIGTERM);
   }

   return 0;
}

static void
accept_mgt_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
   struct sockaddr_in6 client_addr;
   socklen_t client_addr_length;
   int client_fd;
   signed char id;
   char* payload_s1 = NULL; 
   char* payload_s2 = NULL;
   char* payload_s3 = NULL;
   char* payload_s4 = NULL;
   int srv;
   pid_t pid;
   int number_of_results = 0;
   struct accept_io* ai;
   struct configuration* config;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("accept_mgt_cb: got invalid event: %s", strerror(errno));
      return;
   }

   config = (struct configuration*)shmem;
   ai = (struct accept_io*)watcher;

   client_addr_length = sizeof(client_addr);
   client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_addr_length);
   if (client_fd == -1)
   {
      if (accept_fatal(errno) && keep_running)
      {
         pgmoneta_log_warn("Restarting management due to: %s (%d)", strerror(errno), watcher->fd);

         shutdown_mgt();

         if (pgmoneta_bind_unix_socket(config->unix_socket_dir, MAIN_UDS, &unix_management_socket))
         {
            pgmoneta_log_fatal("pgmoneta: Could not bind to %s", config->unix_socket_dir);
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

   /* Process internal management request -- f.ex. returning a file descriptor to the pool */
   if (pgmoneta_management_read_header(client_fd, &id))
   {
      goto disconnect;
   }
   if (pgmoneta_management_read_payload(client_fd, id, &payload_s1, &payload_s2, &payload_s3, &payload_s4))
   {
      goto disconnect;
   }

   switch (id)
   {
      case MANAGEMENT_BACKUP:
         pgmoneta_log_debug("pgmoneta: Management backup: %s", payload_s1);

         if (!strcmp("all", payload_s1))
         {
            for (int i = 0; i < config->number_of_servers; i++)
            {
               if (config->servers[i].wal_streaming)
               {
                  number_of_results++;
               }
            }

            pgmoneta_management_write_int32(client_fd, number_of_results);

            for (int i = 0; i < config->number_of_servers; i++)
            {
               if (config->servers[i].wal_streaming)
               {
                  pid = fork();
                  if (pid == -1)
                  {
                     pgmoneta_management_write_int32(client_fd, 1);

                     /* No process */
                     pgmoneta_log_error("Cannot create process");
                  }
                  else if (pid == 0)
                  {
                     shutdown_ports();
                     pgmoneta_backup(client_fd, i, ai->argv);
                  }
               }
            }
         }
         else
         {
            srv = -1;
            for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
            {
               if (!strcmp(config->servers[i].name, payload_s1))
               {
                  srv = i;
               }
            }

            pgmoneta_management_write_int32(client_fd, 1);

            if (srv != -1)
            {
               pid = fork();
               if (pid == -1)
               {
                  pgmoneta_management_write_int32(client_fd, 1);

                  /* No process */
                  pgmoneta_log_error("Cannot create process");
               }
               else if (pid == 0)
               {
                  shutdown_ports();
                  pgmoneta_backup(client_fd, srv, ai->argv);
               }
            }
            else
            {
               pgmoneta_management_write_int32(client_fd, 1);

               pgmoneta_log_error("Backup: Unknown server %s", payload_s1);
            }
         }

         free(payload_s1);
         break;
      case MANAGEMENT_LIST_BACKUP:
         pgmoneta_log_debug("pgmoneta: Management list backup: %s", payload_s1);

         srv = -1;
         for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
         {
            if (!strcmp(config->servers[i].name, payload_s1))
            {
               srv = i;
            }  
         }

         pid = fork();
         if (pid == -1)
         {
            /* No process */
            pgmoneta_log_error("Cannot create process");
         }
         else if (pid == 0)
         {
            shutdown_ports();
            pgmoneta_management_write_list_backup(client_fd, srv);
            exit(0);
         }

         if (srv == -1)
         {
            pgmoneta_log_error("List backup: Unknown server %s", payload_s1);
         }

         free(payload_s1);
         break;
      case MANAGEMENT_DELETE:
         pgmoneta_log_debug("pgmoneta: Management delete: %s/%s", payload_s1, payload_s2);

         srv = -1;
         for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
         {
            if (!strcmp(config->servers[i].name, payload_s1))
            {
               srv = i;
            }  
         }

         pid = fork();
         if (pid == -1)
         {
            /* No process */
            pgmoneta_log_error("Cannot create process");
         }
         else if (pid == 0)
         {
            int result;
            char* backup_id = NULL;

            shutdown_ports();
            backup_id = malloc(strlen(payload_s2) + 1);
            memset(backup_id, 0, strlen(payload_s2) + 1);
            memcpy(backup_id, payload_s2, strlen(payload_s2));

            result = pgmoneta_delete(srv, backup_id);
            pgmoneta_delete_wal(srv);
            pgmoneta_management_write_delete(client_fd, srv, result);

            free(backup_id);
            exit(0);
         }

         if (srv == -1)
         {
            pgmoneta_log_error("Delete: Unknown server %s", payload_s1);
         }

         free(payload_s1);
         free(payload_s2);
         break;
      case MANAGEMENT_RESTORE:
         pgmoneta_log_debug("pgmoneta: Management restore: %s/%s (%s) -> %s", payload_s1, payload_s2, payload_s3 != NULL ? payload_s3 : "none", payload_s4);

         srv = -1;
         for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
         {
            if (!strcmp(config->servers[i].name, payload_s1))
            {
               srv = i;
            }
         }

         pgmoneta_management_write_int32(client_fd, 1);

         if (srv != -1)
         {
            pid = fork();
            if (pid == -1)
            {
               pgmoneta_management_write_int32(client_fd, 1);

               /* No process */
               pgmoneta_log_error("Cannot create process");
            }
            else if (pid == 0)
            {
               char* backup_id = NULL;
               char* position = NULL;
               char* directory = NULL;

               shutdown_ports();
               backup_id = malloc(strlen(payload_s2) + 1);
               memset(backup_id, 0, strlen(payload_s2) + 1);
               memcpy(backup_id, payload_s2, strlen(payload_s2));

               if (payload_s3 != NULL)
               {
                  position = malloc(strlen(payload_s3) + 1);
                  memset(position, 0, strlen(payload_s3) + 1);
                  memcpy(position, payload_s3, strlen(payload_s3));
               }

               directory = malloc(strlen(payload_s4) + 1);
               memset(directory, 0, strlen(payload_s4) + 1);
               memcpy(directory, payload_s4, strlen(payload_s4));

               pgmoneta_restore(client_fd, srv, backup_id, position, directory, ai->argv);
            }
         }
         else
         {
            pgmoneta_management_write_int32(client_fd, 1);

            pgmoneta_log_error("Restore: Unknown server %s", payload_s1);
         }

         free(payload_s1);
         free(payload_s2);
         free(payload_s3);
         free(payload_s4);
         break;
      case MANAGEMENT_ARCHIVE:
         pgmoneta_log_debug("pgmoneta: Management archive: %s/%s (%s) -> %s", payload_s1, payload_s2, payload_s3 != NULL ? payload_s3 : "none", payload_s4);

         if (!strcmp("all", payload_s1))
         {
            for (int i = 0; i < config->number_of_servers; i++)
            {
               int number_of_backups = pgmoneta_get_number_of_valid_backups(i);

               if (number_of_backups > 0 && (!strcmp("oldest", payload_s2) || !strcmp("newest", payload_s2)))
               {
                  number_of_results++;
               }
            }

            pgmoneta_management_write_int32(client_fd, number_of_results);

            for (int i = 0; i < config->number_of_servers; i++)
            {
               pid = fork();
               if (pid == -1)
               {
                  pgmoneta_management_write_int32(client_fd, 1);

                  /* No process */
                  pgmoneta_log_error("Cannot create process");
               }
               else if (pid == 0)
               {
                  int number_of_backups = pgmoneta_get_number_of_valid_backups(i);

                  if (number_of_backups > 0 && (!strcmp("oldest", payload_s2) || !strcmp("newest", payload_s2)))
                  {
                     char* backup_id = NULL;
                     char* position = NULL;
                     char* directory = NULL;

                     shutdown_ports();
                     backup_id = malloc(strlen(payload_s2) + 1);
                     memset(backup_id, 0, strlen(payload_s2) + 1);
                     memcpy(backup_id, payload_s2, strlen(payload_s2));

                     if (payload_s3 != NULL)
                     {
                        position = malloc(strlen(payload_s3) + 1);
                        memset(position, 0, strlen(payload_s3) + 1);
                        memcpy(position, payload_s3, strlen(payload_s3));
                     }

                     directory = malloc(strlen(payload_s4) + 1);
                     memset(directory, 0, strlen(payload_s4) + 1);
                     memcpy(directory, payload_s4, strlen(payload_s4));

                     pgmoneta_archive(client_fd, i, backup_id, position, directory, ai->argv);
                  }
               }
            }

            free(payload_s1);
            free(payload_s2);
            free(payload_s3);
            free(payload_s4);
         }
         else
         {
            srv = -1;
            for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
            {
               if (!strcmp(config->servers[i].name, payload_s1))
               {
                  srv = i;
               }
            }

            pgmoneta_management_write_int32(client_fd, 1);

            if (srv != -1)
            {
               pid = fork();
               if (pid == -1)
               {
                  pgmoneta_management_write_int32(client_fd, 1);

                  /* No process */
                  pgmoneta_log_error("Cannot create process");
               }
               else if (pid == 0)
               {
                  char* backup_id = NULL;
                  char* position = NULL;
                  char* directory = NULL;

                  shutdown_ports();
                  backup_id = malloc(strlen(payload_s2) + 1);
                  memset(backup_id, 0, strlen(payload_s2) + 1);
                  memcpy(backup_id, payload_s2, strlen(payload_s2));

                  if (payload_s3 != NULL)
                  {
                     position = malloc(strlen(payload_s3) + 1);
                     memset(position, 0, strlen(payload_s3) + 1);
                     memcpy(position, payload_s3, strlen(payload_s3));
                  }

                  directory = malloc(strlen(payload_s4) + 1);
                  memset(directory, 0, strlen(payload_s4) + 1);
                  memcpy(directory, payload_s4, strlen(payload_s4));

                  pgmoneta_archive(client_fd, srv, backup_id, position, directory, ai->argv);
               }
            }
            else
            {
               pgmoneta_management_write_int32(client_fd, 1);

               pgmoneta_log_error("Archive: Unknown server %s", payload_s1);
            }

            free(payload_s1);
            free(payload_s2);
            free(payload_s3);
            free(payload_s4);
         }
         break;
      case MANAGEMENT_STOP:
         pgmoneta_log_debug("pgmoneta: Management stop");
         ev_break(loop, EVBREAK_ALL);
         keep_running = 0;
         stop = 1;
         break;
      case MANAGEMENT_STATUS:
         pgmoneta_log_debug("pgmoneta: Management status");

         pid = fork();
         if (pid == -1)
         {
            /* No process */
            pgmoneta_log_error("Cannot create process");
         }
         else if (pid == 0)
         {
            shutdown_ports();
            pgmoneta_management_write_status(client_fd);
            exit(0);
         }

         break;
      case MANAGEMENT_DETAILS:
         pgmoneta_log_debug("pgmoneta: Management details");

         pid = fork();
         if (pid == -1)
         {
            /* No process */
            pgmoneta_log_error("Cannot create process");
         }
         else if (pid == 0)
         {
            shutdown_ports();
            pgmoneta_management_write_details(client_fd);
            exit(0);
         }

         break;
      case MANAGEMENT_ISALIVE:
         pgmoneta_log_debug("pgmoneta: Management isalive");
         pgmoneta_management_write_isalive(client_fd);
         break;
      case MANAGEMENT_RESET:
         pgmoneta_log_debug("pgmoneta: Management reset");
         pgmoneta_prometheus_reset();
         break;
      case MANAGEMENT_RELOAD:
         pgmoneta_log_debug("pgmoneta: Management reload");
         reload_configuration();
         break;
      case MANAGEMENT_RETAIN:
         pgmoneta_log_debug("pgmoneta: Management retain: %s/%s", payload_s1, payload_s2);

         srv = -1;
         for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
         {
            if (!strcmp(config->servers[i].name, payload_s1))
            {
               srv = i;
            }
         }

         pid = fork();
         if (pid == -1)
         {
            /* No process */
            pgmoneta_log_error("Cannot create process");
         }
         else if (pid == 0)
         {
            char* backup_id = NULL;

            shutdown_ports();
            backup_id = malloc(strlen(payload_s2) + 1);
            memset(backup_id, 0, strlen(payload_s2) + 1);
            memcpy(backup_id, payload_s2, strlen(payload_s2));

            pgmoneta_retain_backup(srv, backup_id);

            free(backup_id);
            exit(0);
         }

         if (srv == -1)
         {
            pgmoneta_log_error("Retain: Unknown server %s", payload_s1);
         }

         free(payload_s1);
         free(payload_s2);
         break;
      case MANAGEMENT_EXPUNGE:
         pgmoneta_log_debug("pgmoneta: Management expunge: %s/%s", payload_s1, payload_s2);

         srv = -1;
         for (int i = 0; srv == -1 && i < config->number_of_servers; i++)
         {
            if (!strcmp(config->servers[i].name, payload_s1))
            {
               srv = i;
            }
         }

         pid = fork();
         if (pid == -1)
         {
            /* No process */
            pgmoneta_log_error("Cannot create process");
         }
         else if (pid == 0)
         {
            char* backup_id = NULL;

            shutdown_ports();
            backup_id = malloc(strlen(payload_s2) + 1);
            memset(backup_id, 0, strlen(payload_s2) + 1);
            memcpy(backup_id, payload_s2, strlen(payload_s2));

            pgmoneta_expunge_backup(srv, backup_id);

            free(backup_id);
            exit(0);
         }

         if (srv == -1)
         {
            pgmoneta_log_error("Expunge: Unknown server %s", payload_s1);
         }

         free(payload_s1);
         free(payload_s2);
         break;
      default:
         pgmoneta_log_debug("pgmoneta: Unknown management id: %d", id);
         break;
   }

disconnect:

   pgmoneta_disconnect(client_fd);
}

static void
accept_metrics_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
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
   client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_addr_length);
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
            pgmoneta_log_fatal("pgmoneta: Could not bind to %s:%d", config->host, config->metrics);
            exit(1);
         }

         if (metrics_fds_length > MAX_FDS)
         {
            pgmoneta_log_fatal("pgmoneta: Too many descriptors %d", metrics_fds_length);
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
accept_management_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
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
   client_fd = accept(watcher->fd, (struct sockaddr *)&client_addr, &client_addr_length);
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
            pgmoneta_log_fatal("pgmoneta: Could not bind to %s:%d", config->host, config->management);
            exit(1);
         }

         if (management_fds_length > MAX_FDS)
         {
            pgmoneta_log_fatal("pgmoneta: Too many descriptors %d", management_fds_length);
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

   pgmoneta_get_address((struct sockaddr *)&client_addr, (char*)&address, sizeof(address));

   if (!fork())
   {
      char* addr = malloc(strlen(address) + 1);
      memset(addr, 0, strlen(address) + 1);
      memcpy(addr, address, strlen(address));

      ev_loop_fork(loop);
      shutdown_ports();
      /* We are leaving the socket descriptor valid such that the client won't reuse it */
      pgmoneta_remote_management(client_fd, addr);
   }

   pgmoneta_disconnect(client_fd);
}

static void
shutdown_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
   pgmoneta_log_debug("pgmoneta: shutdown requested");
   ev_break(loop, EVBREAK_ALL);
   keep_running = 0;
}

static void
reload_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
   pgmoneta_log_debug("pgmoneta: reload requested");
   reload_configuration();
}

static void
coredump_cb(struct ev_loop *loop, ev_signal *w, int revents)
{
   pgmoneta_log_info("pgmoneta: core dump requested");
   abort();
}

static void
wal_compress_cb(struct ev_loop *loop, ev_periodic *w, int revents)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("wal_compress_cb: got invalid event: %s", strerror(errno));
      return;
   }

   for (int i = 0; i < config->number_of_servers; i++)
   {
      /* Compression is always in a fork() */
      if (!fork())
      {
         char* d = NULL;

         pgmoneta_set_proc_title(1, argv_ptr, "wal compress", config->servers[i].name);

         shutdown_ports();

         d = pgmoneta_get_server_wal(i);

         if (config->compression_type == COMPRESSION_GZIP)
         {
            pgmoneta_gzip_wal(d);
         }
         else if (config->compression_type == COMPRESSION_ZSTD)
         {
            pgmoneta_zstandardc_wal(d);
         }

         free(d);

         exit(0);
      }
   }
}

static void
retention_cb(struct ev_loop *loop, ev_periodic *w, int revents)
{
   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("retention_cb: got invalid event: %s", strerror(errno));
      return;
   }

   if (!fork())
   {
      shutdown_ports();
      pgmoneta_retention(argv_ptr);
   }
}

static void
valid_cb(struct ev_loop *loop, ev_periodic *w, int revents)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (EV_ERROR & revents)
   {
      pgmoneta_log_trace("valid_cb: got invalid event: %s", strerror(errno));
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
wal_streaming_cb(struct ev_loop *loop, ev_periodic *w, int revents)
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
      pgmoneta_log_trace("WAL streaming - Server %d Valid %d WAL %d", i, config->servers[i].valid, config->servers[i].wal_streaming);

      if (keep_running && !config->servers[i].wal_streaming)
      {
         start = false;

         if (strlen(config->servers[i].follow) == 0)
         {
            follow = -1;

            for (int j = 0; j == -1 && j < config->number_of_servers; j++)
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
               pgmoneta_log_error("WAL: Cannot create process");
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

static void
reload_configuration(void)
{
   int old_metrics;
   int old_management;
   struct configuration* config;

   config = (struct configuration*)shmem;

   old_metrics = config->metrics;
   old_management = config->management;

   pgmoneta_reload_configuration();

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
            pgmoneta_log_fatal("pgmoneta: Could not bind to %s:%d", config->host, config->metrics);
            exit(1);
         }

         if (metrics_fds_length > MAX_FDS)
         {
            pgmoneta_log_fatal("pgmoneta: Too many descriptors %d", metrics_fds_length);
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
            pgmoneta_log_fatal("pgmoneta: Could not bind to %s:%d", config->host, config->management);
            exit(1);
         }

         if (management_fds_length > MAX_FDS)
         {
            pgmoneta_log_fatal("pgmoneta: Too many descriptors %d", management_fds_length);
            exit(1);
         }

         start_management();

         for (int i = 0; i < management_fds_length; i++)
         {
            pgmoneta_log_debug("Remote management: %d", *(management_fds + i));
         }
      }
   }
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
            pgmoneta_log_error("WAL: Cannot create process");
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
      snprintf(config->pidfile, sizeof(config->pidfile), "%s/pgmoneta.%s.pid",
               config->unix_socket_dir,
               !strncmp(config->host,"*", sizeof(config->host)) ? "all" : config->host );
      pgmoneta_log_debug("PID file automatically set to: [%s]", config->pidfile);
   }
   
   if (strlen(config->pidfile) > 0)
   {

       if (strlen(config->pidfile) > 0)
       {
          // check pidfile is not there
          if (access(config->pidfile, F_OK) == 0)
          {
             pgmoneta_log_fatal("PID file [%s] exists, is there another instance running ?", config->pidfile);
             goto error;
          }
       }
       
      pid = getpid();

      fd = open(config->pidfile, O_WRONLY | O_CREAT | O_EXCL, 0644);
      if (fd < 0)
      {
         printf("Could not create PID file '%s' due to %s\n", config->pidfile, strerror(errno));
         goto error;
      }

      snprintf(&buffer[0], sizeof(buffer), "%u\n", (unsigned)pid);

      r = write(fd, &buffer[0], strlen(buffer));
      if (r < 0)
      {
         printf("Could not write pidfile '%s' due to %s\n", config->pidfile, strerror(errno));
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
