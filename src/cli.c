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
#include <aes.h>
#include <bzip2_compression.h>
#include <cmd.h>
#include <configuration.h>
#include <gzip_compression.h>
#include <info.h>
#include <json.h>
#include <logging.h>
#include <lz4_compression.h>
#include <management.h>
#include <network.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <value.h>
#include <verify.h>
#include <zstandard_compression.h>

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

#define HELP 99

#define COMMAND_BACKUP "backup"
#define COMMAND_LIST_BACKUP "list-backup"
#define COMMAND_RESTORE "restore"
#define COMMAND_VERIFY "verify"
#define COMMAND_ARCHIVE "archive"
#define COMMAND_DELETE "delete"
#define COMMAND_RETAIN "retain"
#define COMMAND_RESET "reset"
#define COMMAND_RELOAD "reload"
#define COMMAND_EXPUNGE "expunge"
#define COMMAND_ENCRYPT "encrypt"
#define COMMAND_DECRYPT "decrypt"
#define COMMAND_COMPRESS "compress"
#define COMMAND_DECOMPRESS "decompress"
#define COMMAND_PING "ping"
#define COMMAND_SHUTDOWN "shutdown"
#define COMMAND_STATUS "status"
#define COMMAND_STATUS_DETAILS "status-details"
#define COMMAND_CONF "conf"
#define COMMAND_CLEAR "clear"
#define COMMAND_INFO "info"
#define COMMAND_ANNOTATE "annotate"

#define OUTPUT_FORMAT_JSON "json"
#define OUTPUT_FORMAT_TEXT "text"

#define UNSPECIFIED "Unspecified"

/* Global variables */
static void help_backup(void);
static void help_list_backup(void);
static void help_restore(void);
static void help_verify(void);
static void help_archive(void);
static void help_delete(void);
static void help_retain(void);
static void help_expunge(void);
static void help_decrypt(void);
static void help_encrypt(void);
static void help_decompress(void);
static void help_compress(void);
static void help_shutdown(void);
static void help_ping(void);
static void help_status_details(void);
static void help_conf(void);
static void help_clear(void);
static void help_info(void);
static void help_annotate(void);
static void display_helper(char* command);

static int backup(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, char* incremental, int32_t output_format);
static int list_backup(SSL* ssl, int socket, char* server, char* sort_order, uint8_t compression, uint8_t encryption, int32_t output_format);
static int restore(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory, uint8_t compression, uint8_t encryption, int32_t output_format);
static int verify(SSL* ssl, int socket, char* server, char* backup_id, char* directory, char* files, uint8_t compression, uint8_t encryption, int32_t output_format);
static int archive(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory, uint8_t compression, uint8_t encryption, int32_t output_format);
static int delete(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, uint8_t encryption, int32_t output_format);
static int pgmoneta_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int status(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int details(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int ping(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int reset(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int reload(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int retain(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, uint8_t encryption, int32_t output_format);
static int expunge(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, uint8_t encryption, int32_t output_format);
static int decrypt_data_client(char* from);
static int encrypt_data_client(char* from);
static int decompress_data_client(char* from);
static int compress_data_client(char* from, uint8_t compression);
static int decrypt_data_server(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format);
static int encrypt_data_server(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format);
static int decompress_data_server(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format);
static int compress_data_server(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format);
static int info(SSL* ssl, int socket, char* server, char* backup, uint8_t compression, uint8_t encryption, int32_t output_format);
static int annotate(SSL* ssl, int socket, char* server, char* backup, char* command, char* key, char* comment, uint8_t compression, uint8_t encryption, int32_t output_format);
static int conf_ls(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format);
static int conf_get(SSL* ssl, int socket, char* config_key, uint8_t compression, uint8_t encryption, int32_t output_format);
static int conf_set(SSL* ssl, int socket, char* config_key, char* config_value, uint8_t compression, uint8_t encryption, int32_t output_format);

static int process_result(SSL* ssl, int socket, int32_t output_format);
static int process_get_result(SSL* ssl, int socket, char* param, int32_t output_format);
static int process_ls_result(SSL* ssl, int socket, int32_t output_format);
static int process_set_result(SSL* ssl, int socket, char* config_key, int32_t output_format);

static int get_config_key_result(char* config_key, struct json* j, uintptr_t* r, int32_t output_format);
static int get_conf_path_result(struct json* j, uintptr_t* r);

static char* translate_command(int32_t cmd_code);
static char* translate_output_format(int32_t out_code);
static char* translate_valid(int32_t valid);
static char* translate_compression(int32_t compression_code);
static char* translate_encryption(int32_t encryption_code);
static char* translate_storage_engine(int32_t storage_engine);
static char* translate_create_slot(int32_t create_slot);
static char* translate_hugepage(int32_t hugepage);
static char* translate_log_type(int32_t log_type);
static char* translate_log_level(int32_t log_level);
static char* translate_log_mode(int32_t log_mode);
static char* int_to_hex(uint32_t num);
static void translate_backup_argument(struct json* j);
static void translate_configuration(struct json* j);
static void translate_response_argument(struct json* j);
static void translate_servers_argument(struct json* j);
static void translate_server_retention_argument(struct json* j, char* tag);
static void translate_json_object(struct json* j);

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
   printf("  -c, --config CONFIG_FILE                       Set the path to the pgmoneta.conf file\n");
   printf("  -h, --host HOST                                Set the host name\n");
   printf("  -p, --port PORT                                Set the port number\n");
   printf("  -U, --user USERNAME                            Set the user name\n");
   printf("  -P, --password PASSWORD                        Set the password\n");
   printf("  -L, --logfile FILE                             Set the log file\n");
   printf("  -v, --verbose                                  Output text string of result\n");
   printf("  -V, --version                                  Display version information\n");
   printf("  -F, --format text|json|raw                     Set the output format\n");
   printf("  -C, --compress none|gz|zstd|lz4|bz2            Compress the wire protocol\n");
   printf("  -E, --encrypt none|aes|aes256|aes192|aes128    Encrypt the wire protocol\n");
   printf("  -s, --sort asc|desc                            Sort result (for list-backup)\n");
   printf("  -?, --help                                     Display help\n");
   printf("\n");
   printf("Commands:\n");
   printf("  annotate                 Annotate a backup with comments\n");
   printf("  archive                  Archive a backup from a server\n");
   printf("  backup                   Backup a server\n");
   printf("  clear <what>             Clear data, with:\n");
   printf("                           - 'prometheus' to reset the Prometheus statistics\n");
   printf("  compress                 Compress a file using configured method\n");
   printf("  conf <action>            Manage the configuration, with one of subcommands:\n");
   printf("                           - 'get' to obtain information about a runtime configuration value\n");
   printf("                             conf get <parameter_name>\n");
   printf("                           - 'ls' to print the configurations used\n");
   printf("                           - 'reload' to reload the configuration\n");
   printf("                           - 'set' to modify a configuration value;\n");
   printf("                             conf set <parameter_name> <parameter_value>;\n");
   printf("  decompress               Decompress a file using configured method\n");
   printf("  decrypt                  Decrypt a file using master-key\n");
   printf("  delete                   Delete a backup from a server\n");
   printf("  encrypt                  Encrypt a file using master-key\n");
   printf("  expunge                  Expunge a backup from a server\n");
   printf("  info                     Information about a backup\n");
   printf("  list-backup              List the backups for a server\n");
   printf("  ping                     Check if pgmoneta is alive\n");
   printf("  restore                  Restore a backup from a server\n");
   printf("  retain                   Retain a backup from a server\n");
   printf("  shutdown                 Shutdown pgmoneta\n");
   printf("  status [details]         Status of pgmoneta, with optional details\n");
   printf("  verify                   Verify a backup from a server\n");
   printf("\n");
   printf("pgmoneta: %s\n", PGMONETA_HOMEPAGE);
   printf("Report bugs: %s\n", PGMONETA_ISSUES);
}

struct pgmoneta_command command_table[] = {
   {
      .command = "backup",
      .subcommand = "",
      .accepted_argument_count = {1, 2},
      .action = MANAGEMENT_BACKUP,
      .deprecated = false,
      .log_message = "<backup> [%s]",
   },
   {
      .command = "list-backup",
      .subcommand = "",
      .accepted_argument_count = {1, 2},
      .action = MANAGEMENT_LIST_BACKUP,
      .deprecated = false,
      .log_message = "<list-backup> [%s]",
   },
   {
      .command = "restore",
      .subcommand = "",
      .accepted_argument_count = {3, 4},
      .action = MANAGEMENT_RESTORE,
      .deprecated = false,
      .log_message = "<restore> [%s]",
   },
   {
      .command = "verify",
      .subcommand = "",
      .accepted_argument_count = {3, 4},
      .action = MANAGEMENT_VERIFY,
      .deprecated = false,
      .log_message = "<verify> [%s]",
   },
   {
      .command = "archive",
      .subcommand = "",
      .accepted_argument_count = {3, 4},
      .action = MANAGEMENT_ARCHIVE,
      .deprecated = false,
      .log_message = "<archive> [%s]"
   },
   {
      .command = "delete",
      .subcommand = "",
      .accepted_argument_count = {2},
      .action = MANAGEMENT_DELETE,
      .deprecated = false,
      .log_message = "<delete> [%s]"
   },
   {
      .command = "retain",
      .subcommand = "",
      .accepted_argument_count = {2},
      .action = MANAGEMENT_RETAIN,
      .deprecated = false,
      .log_message = "<retain> [%s]"
   },
   {
      .command = "expunge",
      .subcommand = "",
      .accepted_argument_count = {2},
      .action = MANAGEMENT_EXPUNGE,
      .deprecated = false,
      .log_message = "<expunge [%s]>",
   },
   {
      .command = "decrypt",
      .subcommand = "",
      .accepted_argument_count = {1},
      .action = MANAGEMENT_DECRYPT,
      .deprecated = false,
      .log_message = "<decrypt> [%s]"
   },
   {
      .command = "encrypt",
      .subcommand = "",
      .accepted_argument_count = {1},
      .action = MANAGEMENT_ENCRYPT,
      .deprecated = false,
      .log_message = "<encrypt> [%s]"
   },
   {
      .command = "decompress",
      .subcommand = "",
      .accepted_argument_count = {1},
      .action = MANAGEMENT_DECOMPRESS,
      .deprecated = false,
      .log_message = "<decompress> [%s]"
   },
   {
      .command = "compress",
      .subcommand = "",
      .accepted_argument_count = {1},
      .action = MANAGEMENT_COMPRESS,
      .deprecated = false,
      .log_message = "<compress> [%s]"
   },
   {
      .command = "ping",
      .subcommand = "",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_PING,
      .deprecated = false,
      .log_message = "<ping>"
   },
   {
      .command = "shutdown",
      .subcommand = "",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_SHUTDOWN,
      .deprecated = false,
      .log_message = "<shutdown>"
   },
   {
      .command = "status",
      .subcommand = "",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_STATUS,
      .deprecated = false,
      .log_message = "<status>"
   },
   {
      .command = "status",
      .subcommand = "details",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_STATUS_DETAILS,
      .deprecated = false,
      .log_message = "<status details>"
   },
   {
      .command = "conf",
      .subcommand = "reload",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_RELOAD,
      .deprecated = false,
      .log_message = "<conf reload>"
   },
   {
      .command = "conf",
      .subcommand = "ls",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_CONF_LS,
      .deprecated = false,
      .log_message = "<conf ls>"
   },
   {
      .command = "conf",
      .subcommand = "get",
      .accepted_argument_count = {0, 1},
      .action = MANAGEMENT_CONF_GET,
      .deprecated = false,
      .log_message = "<conf get> [%s]"
   },
   {
      .command = "conf",
      .subcommand = "set",
      .accepted_argument_count = {2},
      .action = MANAGEMENT_CONF_SET,
      .deprecated = false,
      .log_message = "<conf set> [%s]"
   },
   {
      .command = "clear",
      .subcommand = "prometheus",
      .accepted_argument_count = {0},
      .action = MANAGEMENT_RESET,
      .deprecated = false,
      .log_message = "<clear prometheus>"
   },
   {
      .command = "info",
      .subcommand = "",
      .accepted_argument_count = {2},
      .action = MANAGEMENT_INFO,
      .deprecated = false,
      .log_message = "<info> [%s]"
   },
   {
      .command = "annotate",
      .subcommand = "",
      .accepted_argument_count = {4, 5},
      .action = MANAGEMENT_ANNOTATE,
      .deprecated = false,
      .log_message = "<annotate> [%s]"
   }
};

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
   bool do_free = true;
   int need_server_conn = 1;
   int is_server_conn = 0;
   /* Store the result from command parser*/
   size_t size;
   char un[MAX_USERNAME_LENGTH];
   struct main_configuration* config = NULL;
   int32_t output_format = MANAGEMENT_OUTPUT_FORMAT_TEXT;
   int32_t compression = MANAGEMENT_COMPRESSION_NONE;
   int32_t encryption = MANAGEMENT_ENCRYPTION_NONE;
   size_t command_count = sizeof(command_table) / sizeof(struct pgmoneta_command);
   struct pgmoneta_parsed_command parsed = {.cmd = NULL, .args = {0}};
   char* filepath = NULL;
   int optind = 0;
   int num_options = 0;
   int num_results = 0;
   char* sort_option = NULL;

   cli_option options[] = {
      {"c", "config", true},
      {"h", "host", true},
      {"p", "port", true},
      {"U", "user", true},
      {"P", "password", true},
      {"L", "logfile", true},
      {"v", "verbose", false},
      {"V", "version", false},
      {"F", "format", true},
      {"C", "compress", true},
      {"E", "encrypt", true},
      {"s", "sort", true},
      {"?", "help", false}
   };

   // Disable stdout buffering (i.e. write to stdout immediatelly).
   setbuf(stdout, NULL);

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
      else if (!strcmp(optname, "h") || !strcmp(optname, "host"))
      {
         host = optarg;
      }
      else if (!strcmp(optname, "p") || !strcmp(optname, "port"))
      {
         port = optarg;
      }
      else if (!strcmp(optname, "U") || !strcmp(optname, "user"))
      {
         username = optarg;
      }
      else if (!strcmp(optname, "P") || !strcmp(optname, "password"))
      {
         password = optarg;
      }
      else if (!strcmp(optname, "L") || !strcmp(optname, "logfile"))
      {
         logfile = optarg;
      }
      else if (!strcmp(optname, "v") || !strcmp(optname, "verbose"))
      {
         verbose = true;
      }
      else if (!strcmp(optname, "V") || !strcmp(optname, "version"))
      {
         version();
      }
      else if (!strcmp(optname, "F") || !strcmp(optname, "format"))
      {
         if (!strncmp(optarg, "json", MISC_LENGTH))
         {
            output_format = MANAGEMENT_OUTPUT_FORMAT_JSON;
         }
         else if (!strncmp(optarg, "raw", MISC_LENGTH))
         {
            output_format = MANAGEMENT_OUTPUT_FORMAT_RAW;
         }
         else if (!strncmp(optarg, "text", MISC_LENGTH))
         {
            output_format = MANAGEMENT_OUTPUT_FORMAT_TEXT;
         }
         else
         {
            warnx("pgmoneta-cli: Format type is not correct");
            exit(1);
         }
      }
      else if (!strcmp(optname, "C") || !strcmp(optname, "compress"))
      {
         if (!strncmp(optarg, "gz", MISC_LENGTH))
         {
            compression = MANAGEMENT_COMPRESSION_GZIP;
         }
         else if (!strncmp(optarg, "zstd", MISC_LENGTH))
         {
            compression = MANAGEMENT_COMPRESSION_ZSTD;
         }
         else if (!strncmp(optarg, "lz4", MISC_LENGTH))
         {
            compression = MANAGEMENT_COMPRESSION_LZ4;
         }
         else if (!strncmp(optarg, "bz2", MISC_LENGTH))
         {
            compression = MANAGEMENT_COMPRESSION_BZIP2;
         }
         else if (!strncmp(optarg, "none", MISC_LENGTH))
         {
            break;
         }
         else
         {
            warnx("pgmoneta-cli: Invalid compression method. Allowed values: gz, zstd, lz4, bz2, none.");
            exit(1);
         }
      }
      else if (!strcmp(optname, "E") || !strcmp(optname, "encrypt"))
      {
         if (!strncmp(optarg, "aes", MISC_LENGTH))
         {
            encryption = MANAGEMENT_ENCRYPTION_AES256;
         }
         else if (!strncmp(optarg, "aes256", MISC_LENGTH))
         {
            encryption = MANAGEMENT_ENCRYPTION_AES256;
         }
         else if (!strncmp(optarg, "aes192", MISC_LENGTH))
         {
            encryption = MANAGEMENT_ENCRYPTION_AES192;
         }
         else if (!strncmp(optarg, "aes128", MISC_LENGTH))
         {
            encryption = MANAGEMENT_ENCRYPTION_AES128;
         }
         else if (!strncmp(optarg, "none", MISC_LENGTH))
         {
            break;
         }
         else
         {
            warnx("pgmoneta-cli: Invalid encryption method. Allowed values: aes, aes256, aes192, aes128, none.");
            exit(1);
         }
      }
      else if (!strcmp(optname, "s") || !strcmp(optname, "sort"))
      {
         if (!strncmp(optarg, "asc", 3) || !strncmp(optarg, "desc", 4))
         {
            sort_option = optarg;
         }
         else
         {
            warnx("pgmoneta-cli: Invalid sort order. Allowed values: asc, desc.");
            exit(1);
         }
      }
      else if (!strcmp(optname, "?") || !strcmp(optname, "help"))
      {
         usage();
         exit(0);
      }
   }

   if (getuid() == 0)
   {
      warnx("pgmoneta-cli: Running as root is not allowed for security reasons.");
      exit(1);
   }

   if (configuration_path != NULL && (host != NULL || port != NULL))
   {
      warnx("pgmoneta-cli: Conflicting options: Use either '-c' for config or '-h/-p' for manual endpoint definition, not both.");
      exit(1);
   }

   if (argc <= 1)
   {
      usage();
      exit(1);
   }

   size = sizeof(struct main_configuration);
   if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
   {
      warnx("pgmoneta-cli: Failed to allocate shared memory. Check system resources and permissions.");
      exit(1);
   }
   pgmoneta_init_main_configuration(shmem);

   if (!parse_command(argc, argv, optind, &parsed, command_table, command_count))
   {
      if (argc > optind)
      {
         char* command = argv[optind];
         display_helper(command);
      }
      else
      {
         usage();
      }
      exit_code = 1;
      goto done;
   }

   need_server_conn = parsed.cmd->action != MANAGEMENT_COMPRESS && parsed.cmd->action != MANAGEMENT_DECOMPRESS && parsed.cmd->action != MANAGEMENT_ENCRYPT && parsed.cmd->action != MANAGEMENT_DECRYPT;

   if (configuration_path != NULL)
   {
      ret = pgmoneta_read_main_configuration(shmem, configuration_path);
      if (ret)
      {
         warnx("pgmoneta-cli: Configuration file not found at '%s'. Ensure the file exists and the path is correct.", configuration_path);
         exit(1);
      }

      if (logfile)
      {
         config = (struct main_configuration*)shmem;

         config->common.log_type = PGMONETA_LOGGING_TYPE_FILE;
         memset(&config->common.log_path[0], 0, MISC_LENGTH);
         memcpy(&config->common.log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
      }

      if (pgmoneta_start_logging())
      {
         exit(1);
      }

      config = (struct main_configuration*)shmem;
   }
   else
   {
      ret = pgmoneta_read_main_configuration(shmem, "/etc/pgmoneta/pgmoneta.conf");
      if (ret)
      {
         if (need_server_conn && (host == NULL || port == NULL))
         {
            warnx("pgmoneta-cli: Missing required arguments: Both '--host' (-h) and '--port' (-p) must be provided.");
            exit(1);
         }
      }
      else
      {
         configuration_path = "/etc/pgmoneta/pgmoneta.conf";

         if (logfile)
         {
            config = (struct main_configuration*)shmem;

            config->common.log_type = PGMONETA_LOGGING_TYPE_FILE;
            memset(&config->common.log_path[0], 0, MISC_LENGTH);
            memcpy(&config->common.log_path[0], logfile, MIN(MISC_LENGTH - 1, strlen(logfile)));
         }

         if (pgmoneta_start_logging())
         {
            exit(1);
         }

         config = (struct main_configuration*)shmem;
      }
   }

   if (configuration_path != NULL)
   {
      /* Local connection */
      if (pgmoneta_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
      {
         if (need_server_conn)
         {
            exit_code = 1;
            goto done;
         }
         else
         {
            goto execute;
         }
      }
      is_server_conn = 1;
   }
   else
   {
      /* Local command */
      if (!need_server_conn)
      {
         goto execute;
      }

      /* Remote connection */
      if (pgmoneta_connect(host, atoi(port), &socket))
      {
         if (need_server_conn)
         {
            warnx("pgmoneta-cli: Cannot reach the server at '%s:%s'. Check network connection and firewall settings.", host, port);
            goto done;
         }
         else
         {
            goto execute;
         }
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

      for (size_t i = 0; i < strlen(password); i++)
      {
         if ((unsigned char)(*(password + i)) & 0x80)
         {
            goto password;
         }
      }

      /* Authenticate */
      if (pgmoneta_remote_management_scram_sha256(username, password, socket, &s_ssl) != AUTH_SUCCESS)
      {
         if (need_server_conn)
         {
            warnx("pgmoneta-cli: Authentication failed for user '%s'. Verify username and password.", username);
            goto done;
         }
         else
         {
            goto  execute;
         }
      }

      is_server_conn = 1;
   }

execute:
   if (parsed.cmd->action == MANAGEMENT_BACKUP)
   {
      if (parsed.args[1])
      {
         exit_code = backup(s_ssl, socket, parsed.args[0], compression, encryption, parsed.args[1], output_format);
      }
      else
      {
         exit_code = backup(s_ssl, socket, parsed.args[0], compression, encryption, NULL, output_format);
      }
   }
   else if (parsed.cmd->action == MANAGEMENT_LIST_BACKUP)
   {
      exit_code = list_backup(s_ssl, socket, parsed.args[0], sort_option, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_RESTORE)
   {
      if (parsed.args[3])
      {
         exit_code = restore(s_ssl, socket, parsed.args[0], parsed.args[1], parsed.args[2], parsed.args[3], compression, encryption, output_format);
      }
      else
      {
         exit_code = restore(s_ssl, socket, parsed.args[0], parsed.args[1], NULL, parsed.args[2], compression, encryption, output_format);
      }
   }
   else if (parsed.cmd->action == MANAGEMENT_VERIFY)
   {
      if (parsed.args[3])
      {
         exit_code = verify(s_ssl, socket, parsed.args[0], parsed.args[1], parsed.args[2], parsed.args[3], compression, encryption, output_format);
      }
      else
      {
         exit_code = verify(s_ssl, socket, parsed.args[0], parsed.args[1], parsed.args[2], "failed", compression, encryption, output_format);
      }
   }
   else if (parsed.cmd->action == MANAGEMENT_ARCHIVE)
   {
      if (parsed.args[3])
      {
         exit_code = archive(s_ssl, socket, parsed.args[0], parsed.args[1], parsed.args[2], parsed.args[3], compression, encryption, output_format);
      }
      else
      {
         exit_code = archive(s_ssl, socket, parsed.args[0], parsed.args[1], NULL, parsed.args[2], compression, encryption, output_format);
      }
   }
   else if (parsed.cmd->action == MANAGEMENT_DELETE)
   {
      exit_code = delete(s_ssl, socket, parsed.args[0], parsed.args[1], compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_SHUTDOWN)
   {
      exit_code = pgmoneta_shutdown(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_STATUS)
   {
      exit_code = status(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_STATUS_DETAILS)
   {
      exit_code = details(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_PING)
   {
      exit_code = ping(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_RESET)
   {
      exit_code = reset(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_RELOAD)
   {
      exit_code = reload(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_RETAIN)
   {
      exit_code = retain(s_ssl, socket, parsed.args[0], parsed.args[1], compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_EXPUNGE)
   {
      exit_code = expunge(s_ssl, socket, parsed.args[0], parsed.args[1], compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_DECRYPT)
   {
      if (is_server_conn)
      {
         exit_code = decrypt_data_server(s_ssl, socket, parsed.args[0], compression, encryption, output_format);
      }
      else
      {
         exit_code = decrypt_data_client(parsed.args[0]);
      }
   }
   else if (parsed.cmd->action == MANAGEMENT_ENCRYPT)
   {
      if (is_server_conn)
      {
         exit_code = encrypt_data_server(s_ssl, socket, parsed.args[0], compression, encryption, output_format);
      }
      else
      {
         exit_code = encrypt_data_client(parsed.args[0]);
      }
   }
   else if (parsed.cmd->action == MANAGEMENT_DECOMPRESS)
   {
      if (is_server_conn)
      {
         exit_code = decompress_data_server(s_ssl, socket, parsed.args[0], compression, encryption, output_format);
      }
      else
      {
         exit_code = decompress_data_client(parsed.args[0]);
      }
   }
   else if (parsed.cmd->action == MANAGEMENT_COMPRESS)
   {
      if (is_server_conn)
      {
         exit_code = compress_data_server(s_ssl, socket, parsed.args[0], compression, encryption, output_format);
      }
      else
      {
         uint8_t local_compression = config ? config->compression_type : COMPRESSION_CLIENT_ZSTD;
         exit_code = compress_data_client(parsed.args[0], local_compression);
      }
   }
   else if (parsed.cmd->action == MANAGEMENT_INFO)
   {
      exit_code = info(s_ssl, socket, parsed.args[0], parsed.args[1], compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_ANNOTATE)
   {
      exit_code = annotate(s_ssl, socket, parsed.args[0], parsed.args[1], parsed.args[2], parsed.args[3], parsed.args[4], compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_CONF_LS)
   {
      exit_code = conf_ls(s_ssl, socket, compression, encryption, output_format);
   }
   else if (parsed.cmd->action == MANAGEMENT_CONF_GET)
   {
      if (parsed.args[0])
      {
         exit_code = conf_get(s_ssl, socket, parsed.args[0], compression, encryption, output_format);
      }
      else
      {
         exit_code = conf_get(s_ssl, socket, NULL, compression, encryption, output_format);
      }
   }
   else if (parsed.cmd->action == MANAGEMENT_CONF_SET)
   {
      exit_code = conf_set(s_ssl, socket, parsed.args[0], parsed.args[1], compression, encryption, output_format);
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

static void
help_backup(void)
{
   printf("Backup a server\n");
   printf("  pgmoneta-cli backup <server> [identifier]\n");
}

static void
help_list_backup(void)
{
   printf("List backups for a server\n");
   printf("  pgmoneta-cli list-backup <server> [--sort asc|desc]\n");
}

static void
help_restore(void)
{
   printf("Restore a backup for a server\n");
   printf("  pgmoneta-cli restore <server> <timestamp|oldest|newest> [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>\n");
}

static void
help_verify(void)
{
   printf("Verify a backup for a server\n");
   printf("  pgmoneta-cli verify <server> <timestamp|oldest|newest> <directory> [failed|all]\n");
}

static void
help_archive(void)
{
   printf("Archive a backup for a server\n");
   printf("  pgmoneta-cli archive <server> <timestamp|oldest|newest> [[current|name=X|xid=X|lsn=X|time=X|inclusive=X|timeline=X|action=X|primary|replica],*] <directory>\n");
}

static void
help_delete(void)
{
   printf("Delete a backup for a server\n");
   printf("  pgmoneta-cli delete <server> <timestamp|oldest|newest>\n");
}

static void
help_retain(void)
{
   printf("Retain a backup for a server\n");
   printf("  pgmoneta-cli retain <server> <timestamp|oldest|newest>\n");
}

static void
help_expunge(void)
{
   printf("Expunge a backup for a server\n");
   printf("  pgmoneta-cli expunge <server> <timestamp|oldest|newest>\n");
}

static void
help_decrypt(void)
{
   printf("Decrypt an .aes file created by pgmoneta-cli archive\n");
   printf("  pgmoneta-cli decrypt <file>\n");
}

static void
help_encrypt(void)
{
   printf("Encrypt a single file in place\n");
   printf("  pgmoneta-cli encrypt <file>\n");
}

static void
help_decompress(void)
{
   printf("Decompress a file using configured method\n");
   printf("  pgmoneta-cli decompress <file>\n");
}

static void
help_compress(void)
{
   printf("Compress a single file using configured method\n");
   printf("  pgmoneta-cli compress <file>\n");
}

static void
help_shutdown(void)
{
   printf("Shutdown pgmoneta\n");
   printf("  pgmoneta-cli shutdown\n");
}

static void
help_ping(void)
{
   printf("Check if pgmoneta is alive\n");
   printf("  pgmoneta-cli ping\n");
}

static void
help_status_details(void)
{
   printf("Status of pgmoneta\n");
   printf("  pgmoneta-cli status [details]\n");
}

static void
help_conf(void)
{
   printf("Manage the configuration\n");
   printf("  pgmoneta-cli conf [reload]\n");
   printf("  pgmoneta-cli conf [ls]\n");
   printf("  pgmoneta-cli conf [get] <parameter_name>\n");
   printf("  pgmoneta-cli conf [set] <parameter_name> <parameter_value>\n");
}

static void
help_clear(void)
{
   printf("Reset data\n");
   printf("  pgmoneta-cli clear [prometheus]\n");
}

static void
help_info(void)
{
   printf("Information about a backup\n");
   printf("  pgmoneta-cli info <server> <timestamp|oldest|newest>\n");
}

static void
help_annotate(void)
{
   printf("Annotate a backup with comments\n");
   printf("  pgmoneta-cli annotate <server> <timestamp|oldest|newest> <add|update|remove> <key> [comment]\n");
}

static void
display_helper(char* command)
{
   if (!strcmp(command, COMMAND_BACKUP))
   {
      help_backup();
   }
   else if (!strcmp(command, COMMAND_LIST_BACKUP))
   {
      help_list_backup();
   }
   else if (!strcmp(command, COMMAND_RESTORE))
   {
      help_restore();
   }
   else if (!strcmp(command, COMMAND_VERIFY))
   {
      help_verify();
   }
   else if (!strcmp(command, COMMAND_ARCHIVE))
   {
      help_archive();
   }
   else if (!strcmp(command, COMMAND_DELETE))
   {
      help_delete();
   }
   else if (!strcmp(command, COMMAND_RETAIN))
   {
      help_retain();
   }
   else if (!strcmp(command, COMMAND_EXPUNGE))
   {
      help_expunge();
   }
   else if (!strcmp(command, COMMAND_DECRYPT))
   {
      help_decrypt();
   }
   else if (!strcmp(command, COMMAND_ENCRYPT))
   {
      help_encrypt();
   }
   else if (!strcmp(command, COMMAND_DECOMPRESS))
   {
      help_decompress();
   }
   else if (!strcmp(command, COMMAND_COMPRESS))
   {
      help_compress();
   }
   else if (!strcmp(command, COMMAND_PING))
   {
      help_ping();
   }
   else if (!strcmp(command, COMMAND_SHUTDOWN))
   {
      help_shutdown();
   }
   else if (!strcmp(command, COMMAND_STATUS))
   {
      help_status_details();
   }
   else if (!strcmp(command, COMMAND_CONF))
   {
      help_conf();
   }
   else if (!strcmp(command, COMMAND_CLEAR))
   {
      help_clear();
   }
   else if (!strcmp(command, COMMAND_INFO))
   {
      help_info();
   }
   else if (!strcmp(command, COMMAND_ANNOTATE))
   {
      help_annotate();
   }
   else
   {
      usage();
   }
}

static int
backup(SSL* ssl, int socket, char* server, uint8_t compression, uint8_t encryption, char* incremental, int32_t output_format)
{
   if (pgmoneta_management_request_backup(ssl, socket, server, compression, encryption, incremental, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
list_backup(SSL* ssl, int socket, char* server, char* sort_order, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_list_backup(ssl, socket, server, sort_order, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
restore(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_restore(ssl, socket, server, backup_id, position, directory, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
verify(SSL* ssl, int socket, char* server, char* backup_id, char* directory, char* files, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_verify(ssl, socket, server, backup_id, directory, files, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
archive(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_archive(ssl, socket, server, backup_id, position, directory, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
delete(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_delete(ssl, socket, server, backup_id, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
pgmoneta_shutdown(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_shutdown(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
status(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_status(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
details(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_status_details(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
ping(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_ping(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
reset(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_reset(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
reload(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_reload(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
retain(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_retain(ssl, socket, server, backup_id, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
expunge(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_expunge(ssl, socket, server, backup_id, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
decrypt_data_client(char* from)
{
   char* to = NULL;

   if (!pgmoneta_exists(from))
   {
      pgmoneta_log_error("Decryption: File doesn't exist: %s", from);
      goto error;
   }

   if (!pgmoneta_ends_with(from, ".aes"))
   {
      pgmoneta_log_error("Decryption: Unknown file type: %s", from);
      goto error;
   }

   to = pgmoneta_remove_suffix(from, ".aes");

   if (pgmoneta_decrypt_file(from, to))
   {
      pgmoneta_log_error("Decryption: File encryption failed: %s", from);
      goto error;
   }

   free(to);
   return 0;

error:
   free(to);
   return 1;
}

static int
encrypt_data_client(char* from)
{
   char* to = NULL;

   if (!pgmoneta_exists(from))
   {
      pgmoneta_log_error("Encryption: File doesn't exist: %s", from);
      return 1;
   }

   to = pgmoneta_append(to, from);
   to = pgmoneta_append(to, ".aes");

   if (pgmoneta_encrypt_file(from, to))
   {
      pgmoneta_log_error("Encryption: File encryption failed: %s", from);
      goto error;
   }

   free(to);
   return 0;

error:
   free(to);
   return 1;
}

static int
decompress_data_client(char* from)
{
   char* to = NULL;

   if (!pgmoneta_exists(from))
   {
      pgmoneta_log_error("Decompress: File doesn't exist: %s", from);
      goto error;
   }

   if (pgmoneta_ends_with(from, ".gz"))
   {
      to = pgmoneta_remove_suffix(from, ".gz");
      if (pgmoneta_gunzip_file(from, to))
      {
         pgmoneta_log_error("Decompress: GZIP decompression failed");
         goto error;
      }
   }
   else if (pgmoneta_ends_with(from, ".zstd"))
   {
      to = pgmoneta_remove_suffix(from, ".zstd");
      if (pgmoneta_zstandardd_file(from, to))
      {
         pgmoneta_log_error("Decompress: ZSTD decompression failed");
         goto error;
      }
   }
   else if (pgmoneta_ends_with(from, ".lz4"))
   {
      to = pgmoneta_remove_suffix(from, ".lz4");
      if (pgmoneta_lz4d_file(from, to))
      {
         pgmoneta_log_error("Decompress: LZ4 decompression failed");
         goto error;
      }
   }
   else if (pgmoneta_ends_with(from, ".bz2"))
   {
      to = pgmoneta_remove_suffix(from, ".bz2");
      if (pgmoneta_bunzip2_file(from, to))
      {
         pgmoneta_log_error("Decompress: BZIP2 decompression failed");
         goto error;
      }
   }
   else
   {
      pgmoneta_log_error("Decompress: Unknown file type");
      goto error;
   }

   free(to);
   return 0;

error:
   free(to);
   return 1;
}

static int
compress_data_client(char* from, uint8_t compression)
{
   char* to = NULL;

   if (!pgmoneta_exists(from))
   {
      pgmoneta_log_error("Compress: File doesn't exist: %s", from);
      goto error;
   }

   to = pgmoneta_append(to, from);

   if (compression == COMPRESSION_CLIENT_GZIP || compression == COMPRESSION_SERVER_GZIP)
   {
      to = pgmoneta_append(to, ".gz");
      if (pgmoneta_gzip_file(from, to))
      {
         pgmoneta_log_error("Compress: GZIP compression failed");
         goto error;
      }
   }
   else if (compression == COMPRESSION_CLIENT_ZSTD || compression == COMPRESSION_SERVER_ZSTD)
   {
      to = pgmoneta_append(to, ".zstd");
      if (pgmoneta_zstandardc_file(from, to))
      {
         pgmoneta_log_error("Compress: ZSTD compression failed");
         goto error;
      }
   }
   else if (compression == COMPRESSION_CLIENT_LZ4 || compression == COMPRESSION_SERVER_LZ4)
   {
      to = pgmoneta_append(to, ".lz4");
      if (pgmoneta_lz4c_file(from, to))
      {
         pgmoneta_log_error("Compress: LZ4 compression failed");
         goto error;
      }
   }
   else if (compression == COMPRESSION_CLIENT_BZIP2)
   {
      to = pgmoneta_append(to, ".bz2");
      if (pgmoneta_bzip2_file(from, to))
      {
         pgmoneta_log_error("Compress: BZIP2 compression failed");
         goto error;
      }
   }
   else
   {
      pgmoneta_log_error("Compress: Unknown compression type: %d", compression);
      goto error;
   }

   free(to);
   return 0;

error:
   free(to);
   return 1;
}

static int
decrypt_data_server(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_decrypt(ssl, socket, path, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
encrypt_data_server(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_encrypt(ssl, socket, path, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
decompress_data_server(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_decompress(ssl, socket, path, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
compress_data_server(SSL* ssl, int socket, char* path, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_compress(ssl, socket, path, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
info(SSL* ssl, int socket, char* server, char* backup, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_info(ssl, socket, server, backup, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
annotate(SSL* ssl, int socket, char* server, char* backup, char* action, char* key, char* comment, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (!strcmp(action, "add") || !strcmp(action, "remove") || !strcmp(action, "update"))
   {
      /* Ok */
   }
   else
   {
      printf("Unknown action: %s\n", action);
      goto error;
   }

   if (pgmoneta_management_request_annotate(ssl, socket, server, backup, action, key, comment, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
conf_ls(SSL* ssl, int socket, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_conf_ls(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_ls_result(ssl, socket, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
conf_get(SSL* ssl, int socket, char* config_key, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_conf_get(ssl, socket, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_get_result(ssl, socket, config_key, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
conf_set(SSL* ssl, int socket, char* config_key, char* config_value, uint8_t compression, uint8_t encryption, int32_t output_format)
{
   if (pgmoneta_management_request_conf_set(ssl, socket, config_key, config_value, compression, encryption, output_format))
   {
      goto error;
   }

   if (process_set_result(ssl, socket, config_key, output_format))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
process_result(SSL* ssl, int socket, int32_t output_format)
{
   struct json* read = NULL;

   if (pgmoneta_management_read_json(ssl, socket, NULL, NULL, &read))
   {
      goto error;
   }

   if (MANAGEMENT_OUTPUT_FORMAT_RAW != output_format)
   {
      translate_json_object(read);
   }

   if (MANAGEMENT_OUTPUT_FORMAT_TEXT == output_format)
   {
      pgmoneta_json_print(read, FORMAT_TEXT);
   }
   else
   {
      pgmoneta_json_print(read, FORMAT_JSON);
   }

   pgmoneta_json_destroy(read);

   return 0;

error:

   pgmoneta_json_destroy(read);

   return 1;
}

static int
process_get_result(SSL* ssl, int socket, char* config_key, int32_t output_format)
{
   struct json* read = NULL;
   bool is_char = false;
   char* char_res = NULL;
   struct json* json_res = NULL;
   uintptr_t res;

   if (pgmoneta_management_read_json(ssl, socket, NULL, NULL, &read))
   {
      goto error;
   }

   if (get_config_key_result(config_key, read, &res, output_format))
   {
      if (MANAGEMENT_OUTPUT_FORMAT_JSON == output_format)
      {
         json_res = (struct json*)res;
         pgmoneta_json_print(json_res, FORMAT_JSON_COMPACT);
      }
      else
      {
         is_char = true;
         char_res = (char*)res;
         printf("%s\n", char_res);
      }
      goto error;
   }

   if (!config_key)  // error response | complete configuration
   {
      json_res = (struct json*)res;

      if (MANAGEMENT_OUTPUT_FORMAT_RAW != output_format)
      {
         translate_json_object(json_res);
      }

      if (MANAGEMENT_OUTPUT_FORMAT_TEXT == output_format)
      {
         pgmoneta_json_print(json_res, FORMAT_TEXT);
      }
      else
      {
         pgmoneta_json_print(json_res, FORMAT_JSON);
      }
   }
   else
   {
      if (MANAGEMENT_OUTPUT_FORMAT_JSON == output_format)
      {
         json_res = (struct json*)res;
         pgmoneta_json_print(json_res, FORMAT_JSON_COMPACT);
      }
      else
      {
         is_char = true;
         char_res = (char*)res;
         printf("%s\n", char_res);
      }
   }

   pgmoneta_json_destroy(read);
   if (config_key)
   {
      if (is_char)
      {
         free(char_res);
      }
      else
      {
         pgmoneta_json_destroy(json_res);
      }
   }

   return 0;

error:

   pgmoneta_json_destroy(read);
   if (config_key)
   {
      if (is_char)
      {
         free(char_res);
      }
      else
      {
         pgmoneta_json_destroy(json_res);
      }
   }

   return 1;
}

static int
process_set_result(SSL* ssl, int socket, char* config_key, int32_t output_format)
{
   struct json* read = NULL;
   bool is_char = false;
   char* char_res = NULL;
   int status = 0;
   struct json* json_res = NULL;
   uintptr_t res;

   if (pgmoneta_management_read_json(ssl, socket, NULL, NULL, &read))
   {
      goto error;
   }

   status = get_config_key_result(config_key, read, &res, output_format);
   if (MANAGEMENT_OUTPUT_FORMAT_JSON == output_format)
   {
      json_res = (struct json*)res;
      pgmoneta_json_print(json_res, FORMAT_JSON_COMPACT);
   }
   else
   {
      is_char = true;
      char_res = (char*)res;
      printf("%s\n", char_res);
   }

   if (status == 1)
   {
      goto error;
   }

   pgmoneta_json_destroy(read);
   if (config_key)
   {
      if (is_char)
      {
         free(char_res);
      }
      else
      {
         pgmoneta_json_destroy(json_res);
      }
   }

   return 0;

error:

   pgmoneta_json_destroy(read);
   if (config_key)
   {
      if (is_char)
      {
         free(char_res);
      }
      else
      {
         pgmoneta_json_destroy(json_res);
      }
   }

   return 1;
}

static int
process_ls_result(SSL* ssl, int socket, int32_t output_format)
{
   struct json* read = NULL;
   struct json* json_res = NULL;
   uintptr_t res;

   if (pgmoneta_management_read_json(ssl, socket, NULL, NULL, &read))
   {
      goto error;
   }

   if (get_conf_path_result(read, &res))
   {
      goto error;
   }

   json_res = (struct json*)res;

   if (MANAGEMENT_OUTPUT_FORMAT_JSON == output_format)
   {
      pgmoneta_json_print(json_res, FORMAT_JSON_COMPACT);
   }
   else
   {
      struct json_iterator* iter = NULL;
      pgmoneta_json_iterator_create(json_res, &iter);
      while (pgmoneta_json_iterator_next(iter))
      {
         char* value = pgmoneta_value_to_string(iter->value, FORMAT_TEXT, NULL, 0);
         printf("%s\n", value);
         free(value);
      }
      pgmoneta_json_iterator_destroy(iter);
   }

   pgmoneta_json_destroy(read);
   pgmoneta_json_destroy(json_res);
   return 0;

error:

   pgmoneta_json_destroy(read);
   pgmoneta_json_destroy(json_res);
   return 1;
}

static int
get_conf_path_result(struct json* j, uintptr_t* r)
{
   struct json* conf_path_response = NULL;
   struct json* response = NULL;

   response = (struct json*)pgmoneta_json_get(j, MANAGEMENT_CATEGORY_RESPONSE);

   if (!response)
   {
      goto error;
   }

   if (pgmoneta_json_create(&conf_path_response))
   {
      goto error;
   }

   if (pgmoneta_json_contains_key(response, CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH))
   {
      pgmoneta_json_put(conf_path_response, CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH, (uintptr_t)pgmoneta_json_get(response, CONFIGURATION_ARGUMENT_ADMIN_CONF_PATH), ValueString);
   }
   if (pgmoneta_json_contains_key(response, CONFIGURATION_ARGUMENT_MAIN_CONF_PATH))
   {
      pgmoneta_json_put(conf_path_response, CONFIGURATION_ARGUMENT_MAIN_CONF_PATH, (uintptr_t)pgmoneta_json_get(response, CONFIGURATION_ARGUMENT_MAIN_CONF_PATH), ValueString);
   }
   if (pgmoneta_json_contains_key(response, CONFIGURATION_ARGUMENT_USER_CONF_PATH))
   {
      pgmoneta_json_put(conf_path_response, CONFIGURATION_ARGUMENT_USER_CONF_PATH, (uintptr_t)pgmoneta_json_get(response, CONFIGURATION_ARGUMENT_USER_CONF_PATH), ValueString);
   }

   *r = (uintptr_t)conf_path_response;

   return 0;
error:

   return 1;

}

static int
get_config_key_result(char* config_key, struct json* j, uintptr_t* r, int32_t output_format)
{
   char server[MISC_LENGTH];
   char key[MISC_LENGTH];

   struct json* configuration_js = NULL;
   struct json* filtered_response = NULL;
   struct json* response = NULL;
   struct json* outcome = NULL;
   struct json_iterator* iter;
   char* config_value = NULL;
   int begin = -1, end = -1;

   if (!config_key)
   {
      *r = (uintptr_t)j;
      return 0;
   }

   if (pgmoneta_json_create(&filtered_response))
   {
      goto error;
   }

   memset(server, 0, MISC_LENGTH);
   memset(key, 0, MISC_LENGTH);

   for (int i = 0; i < strlen(config_key); i++)
   {
      if (config_key[i] == '.')
      {
         if (!strlen(server))
         {
            memcpy(server, &config_key[begin], end - begin + 1);
            server[end - begin + 1] = '\0';
            begin = end = -1;
            continue;
         }
      }

      if (begin < 0)
      {
         begin = i;
      }

      end = i;

   }

   // if the key has not been found, since there is no ending dot,
   // try to extract it from the string
   if (!strlen(key))
   {
      memcpy(key, &config_key[begin], end - begin + 1);
      key[end - begin + 1] = '\0';
   }

   response = (struct json*)pgmoneta_json_get(j, MANAGEMENT_CATEGORY_RESPONSE);
   outcome = (struct json*)pgmoneta_json_get(j, MANAGEMENT_CATEGORY_OUTCOME);
   if (!response || !outcome)
   {
      goto error;
   }

   // Check if error response
   if (pgmoneta_json_contains_key(outcome, MANAGEMENT_ARGUMENT_ERROR))
   {
      goto error;
   }

   // translate the complete configuration in response
   if (MANAGEMENT_OUTPUT_FORMAT_RAW != output_format)
   {
      translate_configuration(response);
   }

   if (strlen(server) > 0)
   {
      configuration_js = (struct json*)pgmoneta_json_get(response, server);
      if (!configuration_js)
      {
         goto error;
      }
   }
   else
   {
      configuration_js = response;
   }

   pgmoneta_json_iterator_create(configuration_js, &iter);
   while (pgmoneta_json_iterator_next(iter))
   {
      if (!strcmp(key, iter->key))
      {
         config_value = pgmoneta_value_to_string(iter->value, FORMAT_TEXT, NULL, 0);
         if (iter->value->type == ValueJSON)
         {
            struct json* server_data = NULL;
            pgmoneta_json_clone((struct json*)iter->value->data, &server_data);
            pgmoneta_json_put(filtered_response, key, (uintptr_t)server_data, iter->value->type);
         }
         else
         {
            pgmoneta_json_put(filtered_response, key, (uintptr_t)iter->value->data, iter->value->type);
         }
      }
   }
   pgmoneta_json_iterator_destroy(iter);

   if (!config_value)  // if key doesn't match with any field in configuration
   {
      goto error;
   }

   if (output_format == MANAGEMENT_OUTPUT_FORMAT_JSON || !config_key)
   {
      *r = (uintptr_t)filtered_response;
      free(config_value);
   }
   else
   {
      *r = (uintptr_t)config_value;
      pgmoneta_json_destroy(filtered_response);
   }

   return 0;

error:

   if (output_format == MANAGEMENT_OUTPUT_FORMAT_JSON)
   {
      pgmoneta_json_put(filtered_response, "Outcome", (uintptr_t)false, ValueBool);
      *r = (uintptr_t)filtered_response;
      free(config_value);
   }
   else
   {
      config_value = (char*)malloc(6);
      memcpy(config_value, "Error\0", 6);
      *r = (uintptr_t)config_value;
      pgmoneta_json_destroy(filtered_response);
   }

   return 1;
}

static char*
translate_command(int32_t cmd_code)
{
   char* command_output = NULL;
   switch (cmd_code)
   {
      case MANAGEMENT_BACKUP:
         command_output = pgmoneta_append(command_output, COMMAND_BACKUP);
         break;
      case MANAGEMENT_LIST_BACKUP:
         command_output = pgmoneta_append(command_output, COMMAND_LIST_BACKUP);
         break;
      case MANAGEMENT_RESTORE:
         command_output = pgmoneta_append(command_output, COMMAND_RESTORE);
         break;
      case MANAGEMENT_ARCHIVE:
         command_output = pgmoneta_append(command_output, COMMAND_ARCHIVE);
         break;
      case MANAGEMENT_DELETE:
         command_output = pgmoneta_append(command_output, COMMAND_DELETE);
         break;
      case MANAGEMENT_SHUTDOWN:
         command_output = pgmoneta_append(command_output, COMMAND_SHUTDOWN);
         break;
      case MANAGEMENT_STATUS:
         command_output = pgmoneta_append(command_output, COMMAND_STATUS);
         break;
      case MANAGEMENT_STATUS_DETAILS:
         command_output = pgmoneta_append(command_output, COMMAND_STATUS_DETAILS);
         break;
      case MANAGEMENT_PING:
         command_output = pgmoneta_append(command_output, COMMAND_PING);
         break;
      case MANAGEMENT_RESET:
         command_output = pgmoneta_append(command_output, COMMAND_RESET);
         break;
      case MANAGEMENT_RELOAD:
         command_output = pgmoneta_append(command_output, COMMAND_RELOAD);
         break;
      case MANAGEMENT_RETAIN:
         command_output = pgmoneta_append(command_output, COMMAND_RETAIN);
         break;
      case MANAGEMENT_EXPUNGE:
         command_output = pgmoneta_append(command_output, COMMAND_EXPUNGE);
         break;
      case MANAGEMENT_DECRYPT:
         command_output = pgmoneta_append(command_output, COMMAND_DECRYPT);
         break;
      case MANAGEMENT_DECOMPRESS:
         command_output = pgmoneta_append(command_output, COMMAND_DECOMPRESS);
         break;
      case MANAGEMENT_COMPRESS:
         command_output = pgmoneta_append(command_output, COMMAND_COMPRESS);
         break;
      case MANAGEMENT_INFO:
         command_output = pgmoneta_append(command_output, COMMAND_INFO);
         break;
      case MANAGEMENT_VERIFY:
         command_output = pgmoneta_append(command_output, COMMAND_VERIFY);
         break;
      case MANAGEMENT_ANNOTATE:
         command_output = pgmoneta_append(command_output, COMMAND_ANNOTATE);
         break;
      case MANAGEMENT_CONF_LS:
         command_output = pgmoneta_append(command_output, COMMAND_CONF);
         command_output = pgmoneta_append_char(command_output, ' ');
         command_output = pgmoneta_append(command_output, "ls");
         break;
      case MANAGEMENT_CONF_GET:
         command_output = pgmoneta_append(command_output, COMMAND_CONF);
         command_output = pgmoneta_append_char(command_output, ' ');
         command_output = pgmoneta_append(command_output, "get");
         break;
      case MANAGEMENT_CONF_SET:
         command_output = pgmoneta_append(command_output, COMMAND_CONF);
         command_output = pgmoneta_append_char(command_output, ' ');
         command_output = pgmoneta_append(command_output, "set");
         break;
      default:
         break;
   }
   return command_output;
}

static char*
translate_output_format(int32_t out_code)
{
   char* output_format_output = NULL;
   switch (out_code)
   {
      case MANAGEMENT_OUTPUT_FORMAT_JSON:
         output_format_output = pgmoneta_append(output_format_output, OUTPUT_FORMAT_JSON);
         break;
      case MANAGEMENT_OUTPUT_FORMAT_TEXT:
         output_format_output = pgmoneta_append(output_format_output, OUTPUT_FORMAT_TEXT);
         break;
      default:
         break;
   }
   return output_format_output;
}

static char*
translate_valid(int32_t valid)
{
   char* valid_output = NULL;
   switch (valid)
   {
      case VALID_TRUE:
         valid_output = pgmoneta_append(valid_output, "yes");
         break;
      case VALID_FALSE:
         valid_output = pgmoneta_append(valid_output, "no");
         break;
      default:
         valid_output = pgmoneta_append(valid_output, "unknown");
         break;
   }
   return valid_output;
}

static char*
translate_compression(int32_t compression_code)
{
   char* compression_output = NULL;
   switch (compression_code)
   {
      case COMPRESSION_CLIENT_GZIP:
      case COMPRESSION_SERVER_GZIP:
         compression_output = pgmoneta_append(compression_output, "gzip");
         break;
      case COMPRESSION_CLIENT_ZSTD:
      case COMPRESSION_SERVER_ZSTD:
         compression_output = pgmoneta_append(compression_output, "zstd");
         break;
      case COMPRESSION_CLIENT_LZ4:
      case COMPRESSION_SERVER_LZ4:
         compression_output = pgmoneta_append(compression_output, "lz4");
         break;
      case COMPRESSION_CLIENT_BZIP2:
         compression_output = pgmoneta_append(compression_output, "bzip2");
         break;
      case COMPRESSION_NONE:
         compression_output = pgmoneta_append(compression_output, "none");
         break;
      default:
         return NULL;
   }
   return compression_output;
}

static char*
translate_encryption(int32_t encryption_code)
{
   char* encryption_output = NULL;
   switch (encryption_code)
   {
      case ENCRYPTION_AES_256_CBC:
         encryption_output = pgmoneta_append(encryption_output, "aes-256-cbc");
         break;
      case ENCRYPTION_AES_192_CBC:
         encryption_output = pgmoneta_append(encryption_output, "aes-192-cbc");
         break;
      case ENCRYPTION_AES_128_CBC:
         encryption_output = pgmoneta_append(encryption_output, "aes-128-cbc");
         break;
      case ENCRYPTION_AES_256_CTR:
         encryption_output = pgmoneta_append(encryption_output, "aes-256-ctr");
         break;
      case ENCRYPTION_AES_192_CTR:
         encryption_output = pgmoneta_append(encryption_output, "aes-192-ctr");
         break;
      case ENCRYPTION_AES_128_CTR:
         encryption_output = pgmoneta_append(encryption_output, "aes-128-ctr");
         break;
      default:
         encryption_output = pgmoneta_append(encryption_output, "none");
         break;
   }
   return encryption_output;
}

static char*
translate_storage_engine(int32_t storage_engine)
{
   char* storage_engine_output = NULL;
   switch (storage_engine)
   {
      case STORAGE_ENGINE_LOCAL:
         storage_engine_output = pgmoneta_append(storage_engine_output, "local");
         break;
      case STORAGE_ENGINE_SSH:
         storage_engine_output = pgmoneta_append(storage_engine_output, "ssh");
         break;
      case STORAGE_ENGINE_S3:
         storage_engine_output = pgmoneta_append(storage_engine_output, "s3");
         break;
      case STORAGE_ENGINE_AZURE:
         storage_engine_output = pgmoneta_append(storage_engine_output, "azure");
         break;
      default:
         storage_engine_output = pgmoneta_append(storage_engine_output, "unknown");
         break;
   }
   return storage_engine_output;
}

static char*
translate_create_slot(int32_t create_slot)
{
   char* create_slot_output = NULL;
   switch (create_slot)
   {
      case CREATE_SLOT_UNDEFINED:
         create_slot_output = pgmoneta_append(create_slot_output, "undefined");
         break;
      case CREATE_SLOT_YES:
         create_slot_output = pgmoneta_append(create_slot_output, "yes");
         break;
      case CREATE_SLOT_NO:
         create_slot_output = pgmoneta_append(create_slot_output, "no");
         break;
      default:
         return NULL;
   }
   return create_slot_output;
}

static char*
translate_hugepage(int32_t hugepage)
{
   char* hugepage_output = NULL;
   switch (hugepage)
   {
      case HUGEPAGE_OFF:
         hugepage_output = pgmoneta_append(hugepage_output, "off");
         break;
      case HUGEPAGE_TRY:
         hugepage_output = pgmoneta_append(hugepage_output, "try");
         break;
      case HUGEPAGE_ON:
         hugepage_output = pgmoneta_append(hugepage_output, "on");
         break;
      default:
         return NULL;
   }
   return hugepage_output;
}

static char*
translate_log_type(int32_t log_type)
{
   char* log_type_output = NULL;
   switch (log_type)
   {
      case PGMONETA_LOGGING_TYPE_FILE:
         log_type_output = pgmoneta_append(log_type_output, "file");
         break;
      case PGMONETA_LOGGING_TYPE_CONSOLE:
         log_type_output = pgmoneta_append(log_type_output, "console");
         break;
      case PGMONETA_LOGGING_TYPE_SYSLOG:
         log_type_output = pgmoneta_append(log_type_output, "syslog");
         break;
      default:
         return NULL;
   }
   return log_type_output;
}

static char*
translate_log_level(int32_t log_level)
{
   char* log_level_output = NULL;
   switch (log_level)
   {
      case PGMONETA_LOGGING_LEVEL_DEBUG1:
      case PGMONETA_LOGGING_LEVEL_DEBUG2:
         log_level_output = pgmoneta_append(log_level_output, "debug");
         break;
      case PGMONETA_LOGGING_LEVEL_INFO:
         log_level_output = pgmoneta_append(log_level_output, "info");
         break;
      case PGMONETA_LOGGING_LEVEL_FATAL:
         log_level_output = pgmoneta_append(log_level_output, "fatal");
         break;
      case PGMONETA_LOGGING_LEVEL_ERROR:
         log_level_output = pgmoneta_append(log_level_output, "error");
         break;
      case PGMONETA_LOGGING_LEVEL_WARN:
         log_level_output = pgmoneta_append(log_level_output, "warn");
         break;
      default:
         return NULL;
   }
   return log_level_output;
}

static char*
translate_log_mode(int32_t log_mode)
{
   char* log_mode_output = NULL;
   switch (log_mode)
   {
      case PGMONETA_LOGGING_MODE_CREATE:
         log_mode_output = pgmoneta_append(log_mode_output, "create");
         break;
      case PGMONETA_LOGGING_MODE_APPEND:
         log_mode_output = pgmoneta_append(log_mode_output, "append");
         break;
      default:
         return NULL;
   }
   return log_mode_output;
}

static char*
int_to_hex(uint32_t num)
{
   char buf[MISC_LENGTH];
   char* ret = NULL;
   memset(buf, 0, MISC_LENGTH);
   snprintf(buf, MISC_LENGTH, "%X", num);
   ret = pgmoneta_append(ret, buf);
   return ret;
}

static void
translate_backup_argument(struct json* response)
{
   char* translated_valid = NULL;
   char* translated_compression = NULL;
   char* translated_encryption = NULL;
   char* translated_backup_size = NULL;
   char* translated_restore_size = NULL;
   char* translated_biggest_file_size = NULL;
   char* translated_lsn = NULL;
   char* translated_wal = NULL;
   char* translated_delta = NULL;

   translated_backup_size = pgmoneta_translate_file_size((int32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_BACKUP_SIZE));
   if (translated_backup_size)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BACKUP_SIZE, (uintptr_t)translated_backup_size, ValueString);
   }
   if (pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_VALID))
   {
      translated_valid = translate_valid((int32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_VALID));
      if (translated_valid)
      {
         pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_VALID, (uintptr_t)translated_valid, ValueString);
      }
   }
   translated_compression = translate_compression((int32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_COMPRESSION));
   if (translated_compression)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)translated_compression, ValueString);
   }
   translated_encryption = translate_encryption((int32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_ENCRYPTION));
   if (translated_encryption)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)translated_encryption, ValueString);
   }
   translated_restore_size = pgmoneta_translate_file_size((int32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_RESTORE_SIZE));
   if (translated_restore_size)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_RESTORE_SIZE, (uintptr_t)translated_restore_size, ValueString);
   }
   translated_biggest_file_size =
      pgmoneta_translate_file_size((int32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_BIGGEST_FILE_SIZE));
   if (translated_restore_size)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_BIGGEST_FILE_SIZE, (uintptr_t)translated_biggest_file_size, ValueString);
   }
   translated_wal = pgmoneta_translate_file_size((int32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_WAL));
   if (translated_wal)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_WAL, (uintptr_t)translated_wal, ValueString);
   }
   translated_delta = pgmoneta_translate_file_size((int32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_DELTA));
   if (translated_delta)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_DELTA, (uintptr_t)translated_delta, ValueString);
   }

   if (pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_CHECKPOINT_HILSN))
   {
      translated_lsn = int_to_hex((uint32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_CHECKPOINT_HILSN));
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_CHECKPOINT_HILSN, (uintptr_t) translated_lsn, ValueString);
      free(translated_lsn);
      translated_lsn = NULL;
   }

   if (pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_CHECKPOINT_LOLSN))
   {
      translated_lsn = int_to_hex((uint32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_CHECKPOINT_LOLSN));
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_CHECKPOINT_LOLSN, (uintptr_t) translated_lsn, ValueString);
      free(translated_lsn);
      translated_lsn = NULL;
   }

   if (pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_START_HILSN))
   {
      translated_lsn = int_to_hex((uint32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_START_HILSN));
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_START_HILSN, (uintptr_t) translated_lsn, ValueString);
      free(translated_lsn);
      translated_lsn = NULL;
   }

   if (pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_START_LOLSN))
   {
      translated_lsn = int_to_hex((uint32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_START_LOLSN));
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_START_LOLSN, (uintptr_t) translated_lsn, ValueString);
      free(translated_lsn);
      translated_lsn = NULL;
   }

   if (pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_END_HILSN))
   {
      translated_lsn = int_to_hex((uint32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_END_HILSN));
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_END_HILSN, (uintptr_t) translated_lsn, ValueString);
      free(translated_lsn);
      translated_lsn = NULL;
   }

   if (pgmoneta_json_contains_key(response, MANAGEMENT_ARGUMENT_END_LOLSN))
   {
      translated_lsn = int_to_hex((uint32_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_END_LOLSN));
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_END_LOLSN, (uintptr_t) translated_lsn, ValueString);
      free(translated_lsn);
      translated_lsn = NULL;
   }

   free(translated_valid);
   free(translated_lsn);
   free(translated_compression);
   free(translated_encryption);
   free(translated_backup_size);
   free(translated_restore_size);
   free(translated_biggest_file_size);
   free(translated_wal);
   free(translated_delta);
}

static void
translate_response_argument(struct json* response)
{
   char* translated_total_space = NULL;
   char* translated_free_space = NULL;
   char* translated_used_space = NULL;

   translated_total_space = pgmoneta_translate_file_size((int64_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_TOTAL_SPACE));
   if (translated_total_space)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_TOTAL_SPACE, (uintptr_t)translated_total_space, ValueString);
   }
   translated_free_space = pgmoneta_translate_file_size((int64_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_FREE_SPACE));
   if (translated_free_space)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_FREE_SPACE, (uintptr_t)translated_free_space, ValueString);
   }
   translated_used_space = pgmoneta_translate_file_size((int64_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_USED_SPACE));
   if (translated_used_space)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_USED_SPACE, (uintptr_t)translated_used_space, ValueString);
   }

   free(translated_total_space);
   free(translated_free_space);
   free(translated_used_space);
}

static void
translate_server_retention_argument(struct json* response, char* tag)
{
   if ((int32_t)pgmoneta_json_get(response, tag) < 0)
   {
      pgmoneta_json_put(response, tag, (uintptr_t)UNSPECIFIED, ValueString);
   }
}

static void
translate_servers_argument(struct json* response)
{
   char* translated_workspace_size = NULL;
   char* translated_hotstandby_size = NULL;
   char* translated_server_size = NULL;

   translated_workspace_size = pgmoneta_translate_file_size((int64_t)pgmoneta_json_get(response,
                                                                                       MANAGEMENT_ARGUMENT_WORKSPACE_FREE_SPACE));
   if (translated_workspace_size)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_WORKSPACE_FREE_SPACE, (uintptr_t)translated_workspace_size, ValueString);
   }

   translated_hotstandby_size = pgmoneta_translate_file_size((int64_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_HOT_STANDBY_SIZE));
   if (translated_hotstandby_size)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_HOT_STANDBY_SIZE, (uintptr_t)translated_hotstandby_size, ValueString);
   }

   translated_server_size = pgmoneta_translate_file_size((int64_t)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_SERVER_SIZE));
   if (translated_server_size)
   {
      pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER_SIZE, (uintptr_t)translated_server_size, ValueString);
   }

   translate_server_retention_argument(response, MANAGEMENT_ARGUMENT_RETENTION_DAYS);
   translate_server_retention_argument(response, MANAGEMENT_ARGUMENT_RETENTION_WEEKS);
   translate_server_retention_argument(response, MANAGEMENT_ARGUMENT_RETENTION_MONTHS);
   translate_server_retention_argument(response, MANAGEMENT_ARGUMENT_RETENTION_YEARS);

   free(translated_server_size);
   free(translated_hotstandby_size);
   free(translated_workspace_size);
}

static void
translate_configuration(struct json* response)
{
   char* translated_compression = NULL;
   char* translated_encryption = NULL;
   char* translated_storage_engine = NULL;
   char* translated_create_slot = NULL;
   char* translated_hugepage = NULL;
   char* translated_log_type = NULL;
   char* translated_log_level = NULL;
   char* translated_log_mode = NULL;

   if (pgmoneta_json_contains_key(response, CONFIGURATION_ARGUMENT_COMPRESSION))
   {
      translated_compression = translate_compression((int32_t)pgmoneta_json_get(response, CONFIGURATION_ARGUMENT_COMPRESSION));
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_COMPRESSION, (uintptr_t)translated_compression, ValueString);
   }
   if (pgmoneta_json_contains_key(response, CONFIGURATION_ARGUMENT_ENCRYPTION))
   {
      translated_encryption = translate_encryption((int32_t)pgmoneta_json_get(response, CONFIGURATION_ARGUMENT_ENCRYPTION));
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_ENCRYPTION, (uintptr_t)translated_encryption, ValueString);
   }
   if (pgmoneta_json_contains_key(response, CONFIGURATION_ARGUMENT_STORAGE_ENGINE))
   {
      translated_storage_engine = translate_storage_engine((int32_t)pgmoneta_json_get(response, CONFIGURATION_ARGUMENT_STORAGE_ENGINE));
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_STORAGE_ENGINE, (uintptr_t)translated_storage_engine, ValueString);
   }
   if (pgmoneta_json_contains_key(response, CONFIGURATION_ARGUMENT_CREATE_SLOT))
   {
      translated_create_slot = translate_create_slot((int32_t)pgmoneta_json_get(response, CONFIGURATION_ARGUMENT_CREATE_SLOT));
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_CREATE_SLOT, (uintptr_t)translated_create_slot, ValueString);
   }
   if (pgmoneta_json_contains_key(response, CONFIGURATION_ARGUMENT_HUGEPAGE))
   {
      translated_hugepage = translate_hugepage((int32_t)pgmoneta_json_get(response, CONFIGURATION_ARGUMENT_HUGEPAGE));
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_HUGEPAGE, (uintptr_t)translated_hugepage, ValueString);
   }
   if (pgmoneta_json_contains_key(response, CONFIGURATION_ARGUMENT_LOG_TYPE))
   {
      translated_log_type = translate_log_type((int32_t)pgmoneta_json_get(response, CONFIGURATION_ARGUMENT_LOG_TYPE));
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_LOG_TYPE, (uintptr_t)translated_log_type, ValueString);
   }
   if (pgmoneta_json_contains_key(response, CONFIGURATION_ARGUMENT_LOG_LEVEL))
   {
      translated_log_level = translate_log_level((int32_t)pgmoneta_json_get(response, CONFIGURATION_ARGUMENT_LOG_LEVEL));
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_LOG_LEVEL, (uintptr_t)translated_log_level, ValueString);
   }
   if (pgmoneta_json_contains_key(response, CONFIGURATION_ARGUMENT_LOG_MODE))
   {
      translated_log_mode = translate_log_mode((int32_t)pgmoneta_json_get(response, CONFIGURATION_ARGUMENT_LOG_MODE));
      pgmoneta_json_put(response, CONFIGURATION_ARGUMENT_LOG_MODE, (uintptr_t)translated_log_mode, ValueString);
   }

   free(translated_compression);
   free(translated_encryption);
   free(translated_storage_engine);
   free(translated_create_slot);
   free(translated_hugepage);
   free(translated_log_type);
   free(translated_log_level);
   free(translated_log_mode);
}

static void
translate_json_object(struct json* j)
{
   struct json* header = NULL;
   int32_t command = 0;
   char* translated_command = NULL;
   int32_t out_format = -1;
   char* translated_out_format = NULL;
   int32_t out_compression = -1;
   char* translated_compression = NULL;
   int32_t out_encryption = -1;
   char* translated_encryption = NULL;
   struct json* response = NULL;
   struct json* outcome = NULL;

   struct json* backups = NULL;
   struct json* backup = NULL;
   struct json* servers = NULL;
   struct json_iterator* server_it = NULL;
   struct json_iterator* backup_it = NULL;

   // Translate arguments of header
   header = (struct json*)pgmoneta_json_get(j, MANAGEMENT_CATEGORY_HEADER);

   if (header)
   {
      command = (int32_t)pgmoneta_json_get(header, MANAGEMENT_ARGUMENT_COMMAND);
      translated_command = translate_command(command);
      if (translated_command)
      {
         pgmoneta_json_put(header, MANAGEMENT_ARGUMENT_COMMAND, (uintptr_t)translated_command, ValueString);
      }

      out_format = (int32_t)pgmoneta_json_get(header, MANAGEMENT_ARGUMENT_OUTPUT);
      translated_out_format = translate_output_format(out_format);
      if (translated_out_format)
      {
         pgmoneta_json_put(header, MANAGEMENT_ARGUMENT_OUTPUT, (uintptr_t)translated_out_format, ValueString);
      }

      out_compression = (int32_t)pgmoneta_json_get(header, MANAGEMENT_ARGUMENT_COMPRESSION);
      translated_compression = translate_compression(out_compression);
      if (translated_compression)
      {
         pgmoneta_json_put(header, MANAGEMENT_ARGUMENT_COMPRESSION, (uintptr_t)translated_compression, ValueString);
      }

      out_encryption = (int32_t)pgmoneta_json_get(header, MANAGEMENT_ARGUMENT_ENCRYPTION);
      translated_encryption = translate_encryption(out_encryption);
      if (translated_encryption)
      {
         pgmoneta_json_put(header, MANAGEMENT_ARGUMENT_ENCRYPTION, (uintptr_t)translated_encryption, ValueString);
      }

      free(translated_command);
      free(translated_out_format);
      free(translated_compression);
      free(translated_encryption);
   }

   // Outcome
   outcome = (struct json*)pgmoneta_json_get(j, MANAGEMENT_CATEGORY_OUTCOME);

   if ((bool)pgmoneta_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
   {
      // Translate the response
      response = (struct json*)pgmoneta_json_get(j, MANAGEMENT_CATEGORY_RESPONSE);

      if (response && command)
      {
         switch (command)
         {
            case MANAGEMENT_BACKUP:
            case MANAGEMENT_RESTORE:
            case MANAGEMENT_RETAIN:
            case MANAGEMENT_EXPUNGE:
            case MANAGEMENT_INFO:
            case MANAGEMENT_ANNOTATE:
               translate_backup_argument(response);
               break;
            case MANAGEMENT_STATUS:
               translate_response_argument(response);
               servers = (struct json*)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_SERVERS);
               pgmoneta_json_iterator_create(servers, &server_it);
               while (pgmoneta_json_iterator_next(server_it))
               {
                  translate_servers_argument((struct json*)pgmoneta_value_data(server_it->value));
               }
               pgmoneta_json_iterator_destroy(server_it);
               break;
            case MANAGEMENT_LIST_BACKUP:
               backups = (struct json*)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_BACKUPS);
               pgmoneta_json_iterator_create(backups, &backup_it);
               while (pgmoneta_json_iterator_next(backup_it))
               {
                  backup = (struct json*)pgmoneta_value_data(backup_it->value);
                  translate_backup_argument(backup);
               }
               pgmoneta_json_iterator_destroy(backup_it);
               break;
            case MANAGEMENT_STATUS_DETAILS:
               translate_response_argument(response);
               servers = (struct json*)pgmoneta_json_get(response, MANAGEMENT_ARGUMENT_SERVERS);
               pgmoneta_json_iterator_create(servers, &server_it);
               while (pgmoneta_json_iterator_next(server_it))
               {
                  backups = (struct json*)pgmoneta_json_get((struct json*)pgmoneta_value_data(server_it->value), MANAGEMENT_ARGUMENT_BACKUPS);
                  pgmoneta_json_iterator_create(backups, &backup_it);
                  while (pgmoneta_json_iterator_next(backup_it))
                  {
                     backup = (struct json*)pgmoneta_value_data(backup_it->value);
                     translate_backup_argument(backup);
                  }
                  pgmoneta_json_iterator_destroy(backup_it);

                  translate_servers_argument((struct json*)pgmoneta_value_data(server_it->value));
               }
               pgmoneta_json_iterator_destroy(server_it);
               break;
            case MANAGEMENT_CONF_GET:
               translate_configuration(response);
               break;
            default:
               break;
         }
      }
   }
}
