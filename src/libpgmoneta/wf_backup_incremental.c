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
#include <backup.h>
#include <extension.h>
#include <json.h>
#include <logging.h>
#include <manifest.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <security.h>
#include <server.h>
#include <tablespace.h>
#include <utils.h>
#include <walfile/wal_reader.h>
#include <walfile/wal_summary.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPCOID_PG_DEFAULT 1663
#define SPCOID_PG_GLOBAL  1664

/* fetch/compute these from server configuration inside create workflow */
size_t block_size;       // size of each block (default 8KB)
size_t segment_size;     // segment size
size_t rel_seg_size;     // number of blocks in a segment
size_t wal_segment_size; // wal segment size

static int send_upload_manifest(SSL* ssl, int socket);
static int upload_manifest(SSL* ssl, int socket, char* path);
/**
 * Handle incremental backup for PostgreSQL version 17+
 */
static int incr_backup_execute_17_plus(char* name __attribute__((unused)), struct art* nodes);
/**
 * Handle incremental backup for PostgreSQL version 14-16
 */
static int incr_backup_execute_14_to_16(char* name __attribute__((unused)), struct art* nodes);
/**
 * Get the size of the incremental file
 */
static size_t get_incremental_file_size(uint32_t num_incr_blocks);
/**
 * Get the size of the header of incremental file
 */
static size_t get_incremental_header_size(uint32_t num_incr_blocks);
/**
 * Comparator for block number
 */
static int compare_block_numbers(const void* a, const void* b);
/**
 * Given a relative file path, derive the rlocator, fork number and segment number, also create
 * necessary directories
 */
static int parse_relation_file(char* backup_data, char* rel_file_path, struct rel_file_locator* rlocator, enum fork_number* frk, int* segno);
/**
 * Create standard directories inside data directory of backup, returns a set of paths of all the files
 * inside server data directory
 */
static int create_standard_directories(SSL* ssl, int socket, char* backup_data, char*** paths, int* count);
static char** get_paths(char* backup_data, struct query_response* data, int* count);
/**
 * free an array of string
 */
static void free_string_array(char** arr, int count);
/**
 * add additional fields for incremental backup in backup_label
 */
static int add_incremental_label_fields(char* label_file_path, char* prev_data);
/**
 * Serialize the incremental blocks for a relation file
 */
static int write_incremental_file(int server, SSL* ssl, int socket, char* backup_data,
                                  char* relative_filename, uint32_t num_incr_blocks,
                                  block_number* incr_blocks, uint32_t truncation_block_length, bool empty);
/**
 * Serialize all the blocks for a relation file
 */
static int write_full_file(int server, SSL* ssl, int socket, char* backup_data,
                           char* relative_filename, size_t expected_size);
/**
 * Append padding (0 bytes) to the file stream
 */
static int write_padding(FILE* file, size_t padding_length, size_t* bytes_written);
/**
 * copy the wal files from the archive, the idea is copy only wal files that are generated between
 * the backup was started and ended
 */
static int copy_wal_from_archive(char* start_wal_file, char* wal_dir, char* backup_data);
/*
 * Wait until the WAL segment file appears in the wal archive directory
 */
static int wait_for_wal_switch(char* wal_dir, char* wal_file);
static char* incr_backup_name(void);
static int incr_backup_execute(char*, struct art*);

struct workflow*
pgmoneta_create_incremental_backup(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   if (wf == NULL)
   {
      return NULL;
   }

   wf->name = &incr_backup_name;
   wf->setup = &pgmoneta_common_setup;
   wf->execute = &incr_backup_execute;
   wf->teardown = &pgmoneta_common_teardown;
   wf->next = NULL;

   return wf;
}

static char*
incr_backup_name(void)
{
   return "Incremental backup";
}

static int
incr_backup_execute_14_to_16(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   char* backup_base = NULL;
   char* backup_data = NULL;
   char* backup_label = NULL;
   char* server_backup = NULL;
   char* incremental = NULL;
   char* incremental_label = NULL;
   int usr;
   char* tag = NULL;
   char* wal = NULL;
   SSL* ssl = NULL;
   int socket = -1;
   double incr_backup_elapsed_time;
   int hours;
   int minutes;
   double seconds;
   char elapsed[128];
   char version[10];
   char minor_version[10];
   unsigned int size;
   uint64_t biggest_file_size;
   char* prev_backup_data = NULL;
   char* wal_dir = NULL;
   char* chkpt_lsn = NULL;
   uint64_t prev_backup_chkpt_lsn = 0;
   uint64_t start_backup_lsn = 0;
   char* start_backup_xlog = NULL;
   char* stop_backup_xlog = NULL;
   struct label_file_contents lf = {0};
   uint32_t stop_tli = 0;
   block_ref_table* summarized_brt = NULL;
   char* start_wal_filename = NULL;
   int num_incr_blocks = 0;
   block_number* incr_blocks = NULL;
   uint32_t truncation_block_length = 0;

   block_ref_table_entry* brtentry = NULL;
   block_number start_blk = 0;
   block_number end_blk = 0;

   int segno = 0;
   struct rel_file_locator rlocator = {0};
   enum fork_number frk = MAIN_FORKNUM;
   struct file_stats fs = {0};

   struct backup* backup = NULL;
   struct main_configuration* config;

   struct message* msg = NULL;
   struct query_response* response = NULL;
   char** server_files = NULL;
   int num_of_server_files = 0;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_BASE));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_DATA));
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_BACKUP));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   backup_base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_BASE);
   backup_data = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA);
   server_backup = (char*)pgmoneta_art_search(nodes, NODE_SERVER_BACKUP);

   pgmoneta_log_debug("Incremental backup (execute): %s", config->common.servers[server].name, label);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   incremental = (char*)pgmoneta_art_search(nodes, NODE_INCREMENTAL_BASE);
   incremental_label = (char*)pgmoneta_art_search(nodes, NODE_INCREMENTAL_LABEL);

   if (incremental == NULL || incremental_label == NULL)
   {
      pgmoneta_log_error("Incremental label is required for incremental backup");
      goto error;
   }

   pgmoneta_memory_init();

   usr = -1;
   // find the corresponding user's index of the given server
   for (int i = 0; usr == -1 && i < config->common.number_of_users; i++)
   {
      if (!strcmp(config->common.servers[server].username, config->common.users[i].username))
      {
         usr = i;
      }
   }
   // establish a connection, with replication flag set
   if (pgmoneta_server_authenticate(server, "postgres", config->common.users[usr].username, config->common.users[usr].password, false, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_info("Invalid credentials for %s", config->common.users[usr].username);
      goto error;
   }

   if (!pgmoneta_server_valid(server))
   {
      pgmoneta_server_info(server, ssl, socket);

      if (!pgmoneta_server_valid(server))
      {
         goto error;
      }
   }
   memset(version, 0, sizeof(version));
   snprintf(version, sizeof(version), "%d", config->common.servers[server].version);
   memset(minor_version, 0, sizeof(minor_version));
   snprintf(minor_version, sizeof(minor_version), "%d", config->common.servers[server].minor_version);

   block_size = config->common.servers[server].block_size;
   segment_size = config->common.servers[server].segment_size;
   rel_seg_size = config->common.servers[server].relseg_size;
   wal_segment_size = config->common.servers[server].wal_size;

   /* Get the checkpoint information of the preceding backup using backup_label */
   prev_backup_data = pgmoneta_get_server_backup_identifier_data(server, incremental_label);
   pgmoneta_read_checkpoint_info(prev_backup_data, &chkpt_lsn);

   prev_backup_chkpt_lsn = pgmoneta_string_to_lsn(chkpt_lsn);

   tag = pgmoneta_append(tag, "pgmoneta_");
   tag = pgmoneta_append(tag, label);

   /* Start Backup */
   if (pgmoneta_server_start_backup(server, ssl, socket, tag, &start_backup_xlog))
   {
      pgmoneta_log_error("Incremental backup couldn't start");
      goto error;
   }
   start_backup_lsn = pgmoneta_string_to_lsn(start_backup_xlog);

   wal_dir = pgmoneta_get_server_wal(server);

   /* Do WAL Summarization */
   if (pgmoneta_summarize_wal(server, wal_dir, prev_backup_chkpt_lsn, start_backup_lsn, &summarized_brt))
   {
      pgmoneta_log_error("WAL summation for incremental backup failed");
      goto error;
   }

   pgmoneta_mkdir(backup_data);
   if (create_standard_directories(ssl, socket, backup_data, &server_files, &num_of_server_files))
   {
      pgmoneta_log_error("Incremental backup: Failed to creating standard directories");
      goto error;
   }

   for (int i = 0; i < num_of_server_files; i++)
   {
      block_number limit_block = InvalidBlockNumber;

      if (pgmoneta_starts_with(server_files[i], "pg_wal"))
      {
         continue;
      }

      /* handle other files and directories */
      if (!pgmoneta_starts_with(server_files[i], "base") && !pgmoneta_starts_with(server_files[i], "global"))
      {
         // full backup
         if (write_full_file(server, ssl, socket, backup_data, server_files[i], 0))
         {
            pgmoneta_log_error("Incremental backup: Error during backup of: %s", server_files[i]);
            goto error;
         }
         continue;
      }

      /* handle base and global directories */
      if (pgmoneta_ends_with(server_files[i], "pg_internal.init")) // ignore this file for backup
      {
         continue;
      }

      if (pgmoneta_ends_with(server_files[i], "pg_filenode.map") || pgmoneta_ends_with(server_files[i], "PG_VERSION") || pgmoneta_ends_with(server_files[i], "pg_control"))
      {
         /* undergo full backup */
         if (write_full_file(server, ssl, socket, backup_data, server_files[i], 0))
         {
            pgmoneta_log_error("Incremental backup: Error during backup of: %s", server_files[i]);
            goto error;
         }
         continue;
      }

      /* parse the relation file */
      if (parse_relation_file(backup_data, server_files[i], &rlocator, &frk, &segno))
      {
         pgmoneta_log_error("Incremental backup: Unable to parse: %s", server_files[i]);
         goto error;
      }

      /* find the file stat */
      if (pgmoneta_server_file_stat(server, ssl, socket, server_files[i], &fs))
      {
         pgmoneta_log_error("Incremental backup: Error getting stats for %s", server_files[i]);
         goto error;
      }

      /* file size is not multiple of block size */
      if (fs.size % block_size != 0)
      {
         if (write_full_file(server, ssl, socket, backup_data, server_files[i], fs.size))
         {
            pgmoneta_log_error("Incremental backup: Error doing backup of %s", server_files[i]);
            goto error;
         }
         continue;
      }

      /*
          The free-space map fork is not properly WAL-logged,  so we need to backup the
          entire file every time.
       */
      if (frk == FSM_FORKNUM)
      {
         if (write_full_file(server, ssl, socket, backup_data, server_files[i], fs.size))
         {
            pgmoneta_log_error("Incremental backup: Error during backup of %s", server_files[i]);
            goto error;
         }
         continue;
      }

      /* check if the brtentry for this path is available */
      brtentry = pgmoneta_brt_get_entry(summarized_brt, &rlocator, frk, &limit_block);

      /*
          If no entry exists, it means the relation hasn’t had any WAL-recorded
          modifications since the previous backup. In that case, we can include it
          as part of the incremental backup without copying any changed blocks.

          However, if the file’s size is zero, we should perform a full backup
          instead. Incremental files are never empty, and creating an incremental
          backup would actually be larger than a full one in this scenario.
       */
      if (brtentry == NULL)
      {
         if (fs.size == 0)
         {
            if (write_full_file(server, ssl, socket, backup_data, server_files[i], fs.size))
            {
               pgmoneta_log_error("Incremental backup: Error during backup of %s", server_files[i]);
               goto error;
            }
            continue;
         }

         num_incr_blocks = 0;
         truncation_block_length = fs.size / block_size;
         if (write_incremental_file(server, ssl, socket, backup_data, server_files[i],
                                    num_incr_blocks, NULL, truncation_block_length, true))
         {
            goto error;
         }

         continue;
      }

      /*
          Sometimes the smgr manager cuts the relation file to a block boundary, which means
          all the blocks beyond that cut are truncated/chopped. If that cut lies in a segment
          backup it fully
       */
      if (limit_block <= segno * rel_seg_size)
      {
         if (write_full_file(server, ssl, socket, backup_data, server_files[i], fs.size))
         {
            pgmoneta_log_error("Incremental backup: Error during backup of %s", server_files[i]);
            goto error;
         }
         continue;
      }

      start_blk = segno * rel_seg_size;
      end_blk = start_blk + rel_seg_size;

      if (start_blk / rel_seg_size != (size_t)segno || end_blk < start_blk)
      {
         pgmoneta_log_error("Incremental backup: Overflow computing block number bounds for segment %u with size %zu", segno, fs.size);
         goto error;
      }

      incr_blocks = (block_number*)malloc(rel_seg_size * sizeof(block_number));
      if (pgmoneta_brt_entry_get_blocks(brtentry, start_blk, end_blk, incr_blocks, rel_seg_size, &num_incr_blocks))
      {
         pgmoneta_log_error("Incremental backup: Error getting modified blocks from BRT entry");
         goto error;
      }

      /*
          sort the blocks numbers and translate the absolute block numbers to relative
       */
      qsort(incr_blocks, num_incr_blocks, sizeof(block_number), compare_block_numbers);
      if (start_blk != 0)
      {
         for (int i = 0; i < num_incr_blocks; i++)
         {
            incr_blocks[i] -= start_blk;
         }
      }

      /*
          Calculate truncation length which is minimum length of the reconstructed file. Any
          block numbers below this threshold that are not present in the backup need to be
          fetched from the prior backup.
       */
      truncation_block_length = fs.size / block_size;
      if (brtentry->limit_block != InvalidBlockNumber)
      {
         uint32_t relative_limit = brtentry->limit_block - segno * rel_seg_size;
         if (truncation_block_length < relative_limit)
         {
            truncation_block_length = relative_limit;
         }
      }

      /* serialize the incremental changes */
      if (write_incremental_file(server, ssl, socket, backup_data, server_files[i],
                                 num_incr_blocks, incr_blocks, truncation_block_length, false))
      {
         goto error;
      }
      free(incr_blocks);
      incr_blocks = NULL;
   }

   /* Stop Backup */
   if (pgmoneta_server_stop_backup(server, ssl, socket, backup_data, &stop_backup_xlog, &lf))
   {
      pgmoneta_log_error("Incremental backup: Couldn't stop backup because checkpoint failed");
      goto error;
   }

   /* Get stop timeline id */
   pgmoneta_create_query_message("SELECT timeline_id FROM pg_control_checkpoint();", &msg);
   if (pgmoneta_query_execute(ssl, socket, msg, &response) || response == NULL || response->number_of_columns != 1)
   {
      goto error;
   }

   stop_tli = pgmoneta_atoi(pgmoneta_query_response_get_data(response, 0));

   /* copy wal */
   start_wal_filename = pgmoneta_wal_file_name(lf.start_tli, start_backup_lsn / wal_segment_size, wal_segment_size);

   /* wait for start_wal_file to get switched */
   if (wait_for_wal_switch(wal_dir, start_wal_filename))
   {
      pgmoneta_log_error("Error during WAL switch for %s", start_wal_filename);
      goto error;
   }

   if (copy_wal_from_archive(start_wal_filename, wal_dir, backup_data))
   {
      pgmoneta_log_error("Incremental backup: Error copying WAL from archive");
      goto error;
   }

   /* add additional fields to backup_label */
   backup_label = pgmoneta_append(backup_label, backup_data);
   backup_label = pgmoneta_append(backup_label, "backup_label");

   if (add_incremental_label_fields(backup_label, prev_backup_data))
   {
      goto error;
   }

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   incr_backup_elapsed_time = pgmoneta_compute_duration(start_t, end_t);
   hours = (int)incr_backup_elapsed_time / 3600;
   minutes = ((int)incr_backup_elapsed_time % 3600) / 60;
   seconds = (int)incr_backup_elapsed_time % 60 + (incr_backup_elapsed_time - ((long)incr_backup_elapsed_time));

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

   size = pgmoneta_directory_size(backup_data);
   biggest_file_size = pgmoneta_biggest_file(backup_data);

   pgmoneta_log_debug("Incremental: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, &elapsed[0]);

   pgmoneta_read_wal(backup_data, &wal);

   backup->valid = VALID_TRUE;
   snprintf(backup->label, sizeof(backup->label), "%s", label);
   backup->number_of_tablespaces = 0;
   backup->compression = config->compression_type;
   backup->encryption = config->encryption;
   snprintf(backup->wal, sizeof(backup->wal), "%s", wal);
   backup->restore_size = size;
   backup->biggest_file_size = biggest_file_size;
   backup->major_version = atoi(version);
   backup->minor_version = atoi(minor_version);
   backup->keep = false;

   sscanf(start_backup_xlog, "%X/%X", &backup->start_lsn_hi32, &backup->start_lsn_lo32);
   sscanf(stop_backup_xlog, "%X/%X", &backup->end_lsn_hi32, &backup->end_lsn_lo32);
   backup->start_timeline = lf.start_tli;
   backup->end_timeline = stop_tli;
   backup->basebackup_elapsed_time = incr_backup_elapsed_time;
   backup->type = TYPE_INCREMENTAL;
   snprintf(backup->parent_label, sizeof(backup->parent_label), "%s", incremental_label);
   sscanf(lf.checkpoint_lsn, "%X/%X", &backup->checkpoint_lsn_hi32, &backup->checkpoint_lsn_lo32);

   if (pgmoneta_save_info(server_backup, backup))
   {
      pgmoneta_log_error("Incremental backup: Could not save backup %s", label);
      goto error;
   }

   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   free_string_array(server_files, num_of_server_files);

   free(chkpt_lsn);
   free(backup_label);
   free(start_backup_xlog);
   free(stop_backup_xlog);
   free(wal_dir);
   free(wal);
   free(tag);
   free(start_wal_filename);
   free(prev_backup_data);
   pgmoneta_free_message(msg);
   pgmoneta_free_query_response(response);
   pgmoneta_brt_destroy(summarized_brt);
   pgmoneta_memory_destroy();
   return 0;

error:
   if (backup_base == NULL)
   {
      backup_base = pgmoneta_get_server_backup_identifier(server, label);
   }

   if (pgmoneta_exists(backup_base))
   {
      pgmoneta_delete_directory(backup_base);
   }

   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   free_string_array(server_files, num_of_server_files);

   free(chkpt_lsn);
   free(backup_label);
   free(start_backup_xlog);
   free(stop_backup_xlog);
   free(incr_blocks);
   free(wal_dir);
   free(wal);
   free(tag);
   free(start_wal_filename);
   free(prev_backup_data);
   pgmoneta_free_message(msg);
   pgmoneta_free_query_response(response);
   pgmoneta_brt_destroy(summarized_brt);
   pgmoneta_memory_destroy();
   return 1;
}

static int
incr_backup_execute_17_plus(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   char* label = NULL;
   struct timespec start_t;
   struct timespec end_t;
   int status;
   char* backup_base = NULL;
   char* backup_data = NULL;
   char* server_backup = NULL;
   unsigned long size = 0;
   int usr;
   SSL* ssl = NULL;
   int socket = -1;
   double basebackup_elapsed_time;
   int hours;
   int minutes;
   double seconds;
   char elapsed[128];
   char* tag = NULL;
   char* incremental = NULL;
   char* incremental_label = NULL;
   char* manifest_path = NULL;
   char version[10];
   char minor_version[10];
   char* wal = NULL;
   char startpos[20];
   char endpos[20];
   char* chkptpos = NULL;
   uint32_t start_timeline = 0;
   uint32_t end_timeline = 0;
   char old_label_path[MAX_PATH];
   int backup_max_rate;
   int network_max_rate;
   uint64_t biggest_file_size;
   struct main_configuration* config;
   struct message* basebackup_msg = NULL;
   struct message* tablespace_msg = NULL;
   struct stream_buffer* buffer = NULL;
   struct query_response* response = NULL;
   struct tablespace* tablespaces = NULL;
   struct tablespace* current_tablespace = NULL;
   struct tuple* tup = NULL;
   struct token_bucket* bucket = NULL;
   struct token_bucket* network_bucket = NULL;
   struct backup* backup = NULL;

   config = (struct main_configuration*)shmem;

#ifdef DEBUG
   pgmoneta_dump_art(nodes);

   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_ID));
   assert(pgmoneta_art_contains_key(nodes, NODE_LABEL));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_BASE));
   assert(pgmoneta_art_contains_key(nodes, NODE_BACKUP_DATA));
   assert(pgmoneta_art_contains_key(nodes, NODE_SERVER_BACKUP));
#endif

   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);
   label = (char*)pgmoneta_art_search(nodes, NODE_LABEL);
   backup = (struct backup*)pgmoneta_art_search(nodes, NODE_BACKUP);
   backup_base = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_BASE);
   backup_data = (char*)pgmoneta_art_search(nodes, NODE_BACKUP_DATA);
   server_backup = (char*)pgmoneta_art_search(nodes, NODE_SERVER_BACKUP);

   pgmoneta_log_debug("Incremental backup (execute): %s", config->common.servers[server].name, label);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   incremental = (char*)pgmoneta_art_search(nodes, NODE_INCREMENTAL_BASE);
   incremental_label = (char*)pgmoneta_art_search(nodes, NODE_INCREMENTAL_LABEL);

   if (incremental == NULL || incremental_label == NULL)
   {
      pgmoneta_log_error("Incremental label is required for incremental backup");
      goto error;
   }

   pgmoneta_memory_init();

   backup_max_rate = pgmoneta_get_backup_max_rate(server);
   if (backup_max_rate)
   {
      bucket = (struct token_bucket*)malloc(sizeof(struct token_bucket));
      if (pgmoneta_token_bucket_init(bucket, backup_max_rate))
      {
         pgmoneta_log_error("Failed to initialize the token bucket for backup");
         goto error;
      }
   }

   network_max_rate = pgmoneta_get_network_max_rate(server);
   if (network_max_rate)
   {
      network_bucket = (struct token_bucket*)malloc(sizeof(struct token_bucket));
      if (pgmoneta_token_bucket_init(network_bucket, network_max_rate))
      {
         pgmoneta_log_error("Failed to initialize the network token bucket for backup");
         goto error;
      }
   }
   usr = -1;
   // find the corresponding user's index of the given server
   for (int i = 0; usr == -1 && i < config->common.number_of_users; i++)
   {
      if (!strcmp(config->common.servers[server].username, config->common.users[i].username))
      {
         usr = i;
      }
   }
   // establish a connection, with replication flag set
   if (pgmoneta_server_authenticate(server, "postgres", config->common.users[usr].username, config->common.users[usr].password, false, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_info("Invalid credentials for %s", config->common.users[usr].username);
      goto error;
   }

   if (!pgmoneta_server_valid(server))
   {
      pgmoneta_server_info(server, ssl, socket);

      if (!pgmoneta_server_valid(server))
      {
         goto error;
      }
   }
   memset(version, 0, sizeof(version));
   snprintf(version, sizeof(version), "%d", config->common.servers[server].version);
   memset(minor_version, 0, sizeof(minor_version));
   snprintf(minor_version, sizeof(minor_version), "%d", config->common.servers[server].minor_version);

   pgmoneta_create_query_message("SELECT spcname, pg_tablespace_location(oid) FROM pg_tablespace;", &tablespace_msg);
   if (pgmoneta_query_execute(ssl, socket, tablespace_msg, &response) || response == NULL)
   {
      goto error;
   }

   tup = response->tuples;
   while (tup != NULL)
   {
      char* tablespace_name = tup->data[0];
      char* tablespace_path = tup->data[1];

      if (tablespace_name != NULL && tablespace_path != NULL)
      {
         pgmoneta_log_debug("tablespace_name: %s", tablespace_name);
         pgmoneta_log_debug("tablespace_path: %s", tablespace_path);

         if (tablespaces == NULL)
         {
            pgmoneta_create_tablespace(tablespace_name, tablespace_path, &tablespaces);
         }
         else
         {
            struct tablespace* append = NULL;

            pgmoneta_create_tablespace(tablespace_name, tablespace_path, &append);
            pgmoneta_append_tablespace(&tablespaces, append);
         }
      }
      tup = tup->next;
   }
   pgmoneta_free_query_response(response);
   response = NULL;
   pgmoneta_close_ssl(ssl);
   pgmoneta_disconnect(socket);

   if (pgmoneta_server_authenticate(server, "postgres", config->common.users[usr].username, config->common.users[usr].password, true, &ssl, &socket) != AUTH_SUCCESS)
   {
      pgmoneta_log_info("Invalid credentials for %s", config->common.users[usr].username);
      goto error;
   }

   pgmoneta_memory_stream_buffer_init(&buffer);

   // send UPLOAD_MANIFEST
   if (send_upload_manifest(ssl, socket))
   {
      pgmoneta_log_error("Fail to send UPLOAD_MANIFEST to server %s", config->common.servers[server].name);
      goto error;
   }
   manifest_path = pgmoneta_append(NULL, incremental);
   manifest_path = pgmoneta_append(manifest_path, "data/backup_manifest");
   if (upload_manifest(ssl, socket, manifest_path))
   {
      pgmoneta_log_error("Fail to upload manifest to server %s", config->common.servers[server].name);
      goto error;
   }
   // receive and ignore the result set for UPLOAD_MANIFEST
   if (pgmoneta_consume_data_row_messages(server, ssl, socket, buffer, &response))
   {
      goto error;
   }
   pgmoneta_free_query_response(response);
   response = NULL;

   tag = pgmoneta_append(tag, "pgmoneta_");
   tag = pgmoneta_append(tag, label);

   pgmoneta_create_base_backup_message(config->common.servers[server].version, true, tag, true,
                                       config->compression_type, config->compression_level,
                                       &basebackup_msg);

   status = pgmoneta_write_message(ssl, socket, basebackup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   // Receive the first result set, which contains the WAL starting point
   if (pgmoneta_consume_data_row_messages(server, ssl, socket, buffer, &response))
   {
      goto error;
   }
   memset(startpos, 0, sizeof(startpos));
   memcpy(startpos, response->tuples[0].data[0], strlen(response->tuples[0].data[0]));
   start_timeline = atoi(response->tuples[0].data[1]);
   pgmoneta_free_query_response(response);
   response = NULL;

   pgmoneta_mkdir(backup_base);

   if (pgmoneta_receive_archive_stream(server, ssl, socket, buffer, backup_base, tablespaces, bucket, network_bucket))
   {
      pgmoneta_log_error("Incremental backup: Could not backup %s", config->common.servers[server].name);

      backup->valid = VALID_FALSE;
      snprintf(backup->label, sizeof(backup->label), "%s", label);
      if (pgmoneta_save_info(server_backup, backup))
      {
         pgmoneta_log_error("Incremental backup: Could not save backup %s", label);
         goto error;
      }

      goto error;
   }

   // Receive the final result set, which contains the WAL ending point
   if (pgmoneta_consume_data_row_messages(server, ssl, socket, buffer, &response))
   {
      goto error;
   }
   memset(endpos, 0, sizeof(endpos));
   memcpy(endpos, response->tuples[0].data[0], strlen(response->tuples[0].data[0]));
   end_timeline = atoi(response->tuples[0].data[1]);
   pgmoneta_free_query_response(response);
   response = NULL;

   // remove backup_label.old if it exists
   memset(old_label_path, 0, MAX_PATH);
   if (pgmoneta_ends_with(backup_base, "/"))
   {
      snprintf(old_label_path, MAX_PATH, "%sdata/%s", backup_base, "backup_label.old");
   }
   else
   {
      snprintf(old_label_path, MAX_PATH, "%s/data/%s", backup_base, "backup_label.old");
   }

   if (pgmoneta_exists(old_label_path))
   {
      if (pgmoneta_exists(old_label_path))
      {
         pgmoneta_delete_file(old_label_path, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exists", old_label_path);
      }
   }

   // receive and ignore the last result set, it's just a summary
   pgmoneta_consume_data_row_messages(server, ssl, socket, buffer, &response);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   basebackup_elapsed_time = pgmoneta_compute_duration(start_t, end_t);
   hours = (int)basebackup_elapsed_time / 3600;
   minutes = ((int)basebackup_elapsed_time % 3600) / 60;
   seconds = (int)basebackup_elapsed_time % 60 + (basebackup_elapsed_time - ((long)basebackup_elapsed_time));

   memset(&elapsed[0], 0, sizeof(elapsed));
   sprintf(&elapsed[0], "%02i:%02i:%.4f", hours, minutes, seconds);

   pgmoneta_log_debug("Incremental Backup: %s/%s (Elapsed: %s)", config->common.servers[server].name, label, &elapsed[0]);

   if (pgmoneta_backup_size(server, label, &size, &biggest_file_size))
   {
      pgmoneta_log_error("Incremental Backup: Could not get incremental size for %s", config->common.servers[server].name);
      goto error;
   }
   pgmoneta_read_wal(backup_data, &wal);
   pgmoneta_read_checkpoint_info(backup_data, &chkptpos);

   backup->valid = VALID_TRUE;
   snprintf(backup->label, sizeof(backup->label), "%s", label);
   backup->number_of_tablespaces = 0;
   backup->compression = config->compression_type;
   backup->encryption = config->encryption;
   snprintf(backup->wal, sizeof(backup->wal), "%s", wal);
   backup->restore_size = size;
   backup->biggest_file_size = biggest_file_size;
   backup->major_version = atoi(version);
   backup->minor_version = atoi(minor_version);
   backup->keep = false;
   sscanf(startpos, "%X/%X", &backup->start_lsn_hi32, &backup->start_lsn_lo32);
   sscanf(endpos, "%X/%X", &backup->end_lsn_hi32, &backup->end_lsn_lo32);
   backup->start_timeline = start_timeline;
   backup->end_timeline = end_timeline;
   backup->basebackup_elapsed_time = basebackup_elapsed_time;

   backup->type = TYPE_INCREMENTAL;
   snprintf(backup->parent_label, sizeof(backup->parent_label), "%s", incremental_label);

   // in case of parsing error
   if (chkptpos != NULL)
   {
      sscanf(chkptpos, "%X/%X", &backup->checkpoint_lsn_hi32, &backup->checkpoint_lsn_lo32);
   }

   current_tablespace = tablespaces;
   backup->number_of_tablespaces = 0;

   while (current_tablespace != NULL && backup->number_of_tablespaces < MAX_NUMBER_OF_TABLESPACES)
   {
      int i = backup->number_of_tablespaces;

      snprintf(backup->tablespaces[i], sizeof(backup->tablespaces[i]), "tblspc_%s", current_tablespace->name);
      snprintf(backup->tablespaces_oids[i], sizeof(backup->tablespaces_oids[i]), "%u", current_tablespace->oid);
      snprintf(backup->tablespaces_paths[i], sizeof(backup->tablespaces_paths[i]), "%s", current_tablespace->path);

      backup->number_of_tablespaces++;
      current_tablespace = current_tablespace->next;
   }

   if (pgmoneta_save_info(server_backup, backup))
   {
      pgmoneta_log_error("Backup: Could not save backup %s", label);
      goto error;
   }
   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   pgmoneta_memory_destroy();
   pgmoneta_memory_stream_buffer_free(buffer);
   pgmoneta_free_tablespaces(tablespaces);
   pgmoneta_free_message(basebackup_msg);
   pgmoneta_free_message(tablespace_msg);
   pgmoneta_free_query_response(response);
   pgmoneta_token_bucket_destroy(bucket);
   pgmoneta_token_bucket_destroy(network_bucket);
   free(manifest_path);
   free(chkptpos);
   free(tag);
   free(wal);

   return 0;

error:

   if (backup_base == NULL)
   {
      backup_base = pgmoneta_get_server_backup_identifier(server, label);
   }

   if (pgmoneta_exists(backup_base))
   {
      pgmoneta_delete_directory(backup_base);
   }

   pgmoneta_close_ssl(ssl);
   if (socket != -1)
   {
      pgmoneta_disconnect(socket);
   }
   pgmoneta_memory_destroy();
   pgmoneta_memory_stream_buffer_free(buffer);
   pgmoneta_free_message(tablespace_msg);
   pgmoneta_free_tablespaces(tablespaces);
   pgmoneta_free_message(basebackup_msg);
   pgmoneta_free_query_response(response);
   pgmoneta_token_bucket_destroy(bucket);
   pgmoneta_token_bucket_destroy(network_bucket);
   free(manifest_path);
   free(chkptpos);
   free(tag);
   free(wal);

   return 1;
}

static int
incr_backup_execute(char* name __attribute__((unused)), struct art* nodes)
{
   int server = -1;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;
   server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);

   if (config->common.servers[server].version < 17)
   {
      return incr_backup_execute_14_to_16(name, nodes);
   }
   else
   {
      return incr_backup_execute_17_plus(name, nodes);
   }
}

static size_t
get_incremental_header_size(uint32_t num_incr_blocks)
{
   size_t result;

   /*
      compute header size
      (magic_number, truncation block length, block count) followed by block numbers
    */
   result = 3 * sizeof(uint32_t) + (sizeof(block_number) * num_incr_blocks);
   /* round the header to a multiple of Block Size */
   if ((num_incr_blocks > 0) && (result % block_size != 0))
   {
      result += block_size - (result % block_size);
   }

   return result;
}

static size_t
get_incremental_file_size(uint32_t num_incr_blocks)
{
   size_t result;

   result = get_incremental_header_size(num_incr_blocks);
   result += block_size * num_incr_blocks;

   return result;
}

static int
compare_block_numbers(const void* a, const void* b)
{
   block_number aa = *(block_number*)a;
   block_number bb = *(block_number*)b;

   if (aa < bb)
   {
      return -1;
   }
   else if (aa > bb)
   {
      return 1;
   }
   return 0;
}

static int
create_standard_directories(SSL* ssl, int socket, char* backup_data, char*** p, int* c)
{
   struct query_response* qr = NULL;
   char** paths = NULL;
   int count = 0;

   /* get all the paths */
   pgmoneta_ext_get_files(ssl, socket, ".", &qr);
   if (qr != NULL && qr->number_of_columns == 3)
   {
      paths = get_paths(backup_data, qr, &count);
   }
   else
   {
      pgmoneta_log_warn("Retrieving extra files: Query failed");
      goto error;
   }

   *p = paths;
   *c = count;

   pgmoneta_free_query_response(qr);
   return 0;
error:
   if (paths)
   {
      for (int i = 0; i < count; i++)
      {
         free(paths[i]);
      }
      free(paths);
   }
   pgmoneta_free_query_response(qr);
   return 1;
}

static void
free_string_array(char** arr, int count)
{
   if (arr)
   {
      for (int i = 0; i < count; i++)
      {
         free(arr[i]);
      }
      free(arr);
      arr = NULL;
   }
}

static int
add_incremental_label_fields(char* label_file_path, char* prev_data)
{
   char label[MAX_PATH];
   char read_buffer[MAX_PATH];
   char* write_buffer = NULL;
   FILE* label_file = NULL;
   FILE* prev_label_file = NULL;

   memset(label, 0, MAX_PATH);
   snprintf(label, MAX_PATH, "%s/backup_label", prev_data);

   label_file = fopen(label_file_path, "a");
   if (label_file == NULL)
   {
      pgmoneta_log_error("Unable to open backup_label file: %s", label_file_path);
      goto error;
   }

   prev_label_file = fopen(label, "r");
   if (prev_label_file == NULL)
   {
      pgmoneta_log_error("Unable to open backup_label file: %s", label);
      goto error;
   }
   while (fgets(read_buffer, sizeof(read_buffer), prev_label_file) != NULL)
   {
      if (pgmoneta_starts_with(read_buffer, "START WAL LOCATION"))
      {
         char buf[MAX_PATH];

         memset(buf, 0, sizeof(buf));
         if (sscanf(read_buffer, "START WAL LOCATION: %s\n", buf) != 1)
         {
            pgmoneta_log_error("Error parsing start wal location");
            goto error;
         }

         write_buffer = pgmoneta_append(write_buffer, "INCREMENTAL FROM LSN: ");
         write_buffer = pgmoneta_append(write_buffer, buf);
         write_buffer = pgmoneta_append(write_buffer, "\n");

         if (fwrite(write_buffer, 1, strlen(write_buffer), label_file) <= 0)
         {
            pgmoneta_log_error("Error writing line to: %s", label_file_path);
            goto error;
         }
         free(write_buffer);
         write_buffer = NULL;
      }
      if (pgmoneta_starts_with(read_buffer, "START TIMELINE"))
      {
         char buf[MAX_PATH];

         memset(buf, 0, sizeof(buf));
         if (sscanf(read_buffer, "START TIMELINE: %s\n", buf) != 1)
         {
            pgmoneta_log_error("Error parsing start timeline");
            goto error;
         }

         write_buffer = pgmoneta_append(write_buffer, "INCREMENTAL FROM TLI: ");
         write_buffer = pgmoneta_append(write_buffer, buf);
         write_buffer = pgmoneta_append(write_buffer, "\n");

         if (fwrite(write_buffer, 1, strlen(write_buffer), label_file) <= 0)
         {
            pgmoneta_log_error("Error writing line to: %s", label_file_path);
            goto error;
         }
         free(write_buffer);
         write_buffer = NULL;
      }
      memset(read_buffer, 0, sizeof(read_buffer));
   }

   fclose(label_file);
   fclose(prev_label_file);
   return 0;
error:
   if (label_file)
   {
      fclose(label_file);
   }
   if (prev_label_file)
   {
      fclose(prev_label_file);
   }
   free(write_buffer);
   return 1;
}

static int
parse_relation_file(char* backup_data, char* rel_file_path, struct rel_file_locator* r, enum fork_number* f, int* s)
{
   char** results = NULL;
   int count = 0;
   char* base_directory_path = NULL;
   char* relation_file = NULL;
   struct rel_file_locator rlocator = {0};
   enum fork_number frk = MAIN_FORKNUM;
   int segno = 0;

   /* split the file path */
   if (pgmoneta_split(rel_file_path, &results, &count, '/'))
   {
      pgmoneta_log_error("Cannot split the file: %s with delimiter '/'", rel_file_path);
      goto error;
   }

   if (count < 2)
   {
      pgmoneta_log_error("Invalid relation path: %s", rel_file_path);
      goto error;
   }

   if (!strcmp(results[0], "base"))
   {
      /*
          Parse the database path

          file_format: base/<dboid>/<relation_file_format>
       */
      if (count < 3)
      {
         pgmoneta_log_error("Invalid base relation path: %s", rel_file_path);
         goto error;
      }

      rlocator.spcOid = SPCOID_PG_DEFAULT;
      rlocator.dbOid = pgmoneta_atoi(results[1]);
      relation_file = pgmoneta_append(relation_file, results[2]);

      /* create directory */
      base_directory_path = pgmoneta_append(base_directory_path, backup_data);
      base_directory_path = pgmoneta_append(base_directory_path, "/base/");
      base_directory_path = pgmoneta_append(base_directory_path, results[1]);

      pgmoneta_mkdir(base_directory_path);
   }
   else if (!strcmp(results[0], "global"))
   {
      rlocator.spcOid = SPCOID_PG_GLOBAL;
      rlocator.dbOid = 0;
      relation_file = pgmoneta_append(relation_file, results[1]);
   }
   else
   {
      // do nothing
      goto done;
   }

   free_string_array(results, count);
   /*
       Parse the relation file

       relation_file_format: <relation_file_oid>_<fork_identifier>.<segment_number>

       - fork_identifier: tells us about the variant of the relation file (heap/index)
           - main data file
           - free space map file (`_fsm`)
           - visibility map file (`_vm`)
           - init file (`_init`)

       - segment_number: When a table or index exceeds 1 GB, it is divided into
       gigabyte-sized segments. The first segment's file name is the same as the filenode;
       subsequent segments are named filenode.1, filenode.2, etc. This arrangement avoids
       problems on platforms that have file size limitations.
    */

   /* parse seg number */
   if (pgmoneta_split(relation_file, &results, &count, '.'))
   {
      pgmoneta_log_error("Cannot split the file: %s with delimiter '.'", rel_file_path);
      goto error;
   }

   if (count == 2)
   {
      segno = pgmoneta_atoi(results[1]);
   }
   free(relation_file);
   relation_file = NULL;

   relation_file = pgmoneta_append(relation_file, results[0]);

   /* parse forknumber */
   free_string_array(results, count);

   if (pgmoneta_split(relation_file, &results, &count, '_'))
   {
      pgmoneta_log_error("Cannot split the file: %s with delimiter '_'", rel_file_path);
      goto error;
   }

   if (count == 2)
   {
      if (!strcmp(results[1], "fsm"))
      {
         frk = FSM_FORKNUM;
      }
      else if (!strcmp(results[1], "vm"))
      {
         frk = VISIBILITYMAP_FORKNUM;
      }
      else if (!strcmp(results[1], "init"))
      {
         frk = INIT_FORKNUM;
      }
      else
      {
         pgmoneta_log_error("Invalid fork number: %s encountered", results[1]);
         goto error;
      }
   }

   rlocator.relNumber = pgmoneta_atoi(results[0]);

   *r = rlocator;
   *f = frk;
   *s = segno;
done:
   free_string_array(results, count);
   free(relation_file);
   free(base_directory_path);
   return 0;
error:
   free_string_array(results, count);
   free(relation_file);
   free(base_directory_path);
   return 1;
}

static int
write_incremental_file(int server, SSL* ssl, int socket, char* backup_data,
                       char* relative_filename, uint32_t num_incr_blocks,
                       block_number* incr_blocks, uint32_t truncation_block_length, bool empty)
{
   FILE* file = NULL;
   size_t expected_file_size;
   uint32_t magic = INCREMENTAL_MAGIC;
   char* filepath = NULL;
   char* file_name = NULL;
   char* rel_path = NULL;
   size_t padding_length = 0;
   size_t padding_bytes = 0;
   block_number blkno;
   size_t bytes_written = 0;
   uint8_t* binary_data = NULL;
   int binary_data_length = 0;

   /* preprocessing of incremental filename */
   rel_path = pgmoneta_append(rel_path, relative_filename);
   rel_path = dirname(rel_path);
   file_name = pgmoneta_append(file_name, rel_path + strlen(rel_path) + 1);

   filepath = pgmoneta_append(filepath, backup_data);
   filepath = pgmoneta_append(filepath, rel_path);
   if (!pgmoneta_ends_with(filepath, "/"))
   {
      filepath = pgmoneta_append(filepath, "/");
   }
   filepath = pgmoneta_append(filepath, INCREMENTAL_PREFIX);
   filepath = pgmoneta_append(filepath, file_name);

   /* Open the file in write mode, if not present create one */
   file = fopen(filepath, "w+");
   if (file == NULL)
   {
      pgmoneta_log_error("Write incremental file: failed to open the file at %s", relative_filename);
      goto error;
   }

   /* Write the file header */
   bytes_written += fwrite(&magic, sizeof(magic), 1, file);
   bytes_written += fwrite(&num_incr_blocks, sizeof(num_incr_blocks), 1, file);
   bytes_written += fwrite(&truncation_block_length, sizeof(truncation_block_length), 1, file);

   if (empty)
   {
      goto done;
   }

   bytes_written += fwrite(incr_blocks, sizeof(block_number), num_incr_blocks, file);

   if ((num_incr_blocks > 0) && (bytes_written % block_size != 0))
   {
      padding_length = (block_size - (bytes_written % block_size));
      if (write_padding(file, padding_length, &padding_bytes))
      {
         goto error;
      }
      bytes_written += padding_bytes;
   }

   if (bytes_written != get_incremental_header_size(num_incr_blocks))
   {
      pgmoneta_log_error("Write incremental file: failed to open the file at %s", relative_filename);
      goto error;
   }

   expected_file_size = get_incremental_file_size(num_incr_blocks);
   /*
       Request the blocks from the server

       Assume the incremental block array is sorted, also note that we may have to consider
       the filename with their segment number, which can be determined using the block number
       segment_number = block_number / (# of blocks in each segment)

       Will try to fetch untill either we get all the blocks (from server) with block number
       provided by the caller or request failed due to side effects like concurrent truncation.
    */
   for (uint32_t i = 0; i < num_incr_blocks; i++)
   {
      blkno = incr_blocks[i];

      if (pgmoneta_server_read_binary_file(server, ssl, relative_filename,
                                           block_size * blkno, block_size, socket, &binary_data, &binary_data_length))
      {
         pgmoneta_log_error("Write incremental file: error fetching the block#%d of file: %s from the server", blkno, relative_filename);
         goto error;
      }

      /*
          If partial read, means the relation is truncated after the incremental workflow has started.
          Not to worry, just fill all the blocks including this one with 0, untill we wrote the number
           of bytes expected by caller, WAL replay will take care of it later.
       */
      if ((size_t)binary_data_length < block_size)
      {
         free(binary_data);
         break;
      }

      bytes_written += fwrite(binary_data, sizeof(uint8_t), binary_data_length, file);
      /* read/write content must be of multiple of block size length */
      if (bytes_written % block_size)
      {
         pgmoneta_log_error("Write incremental file: partial write/read");
         goto error;
      }

      free(binary_data);
   }

   /* Handle truncation, by padding with 0 */
   padding_length = expected_file_size - bytes_written;
   if (write_padding(file, padding_length, &padding_bytes))
   {
      goto error;
   }
   bytes_written += padding_bytes;

done:
   free(filepath);
   free(file_name);
   free(rel_path);
   fclose(file);
   return 0;

error:
   free(binary_data);
   free(filepath);
   free(file_name);
   free(rel_path);
   fclose(file);
   return 1;
}

static int
write_full_file(int server, SSL* ssl, int socket, char* backup_data,
                char* relative_filename, size_t expected_size)
{
   FILE* file = NULL;
   size_t chunk_size = block_size * 1024;
   size_t offset = 0;
   char* filepath = NULL;
   uint8_t* binary_data = NULL;
   int binary_data_length = 0;
   size_t bytes_written = 0;

   if (expected_size % block_size)
   {
      pgmoneta_log_error("expected size: %ld is not block aligned for file: %s", expected_size, relative_filename);
      goto error;
   }

   filepath = pgmoneta_append(filepath, backup_data);
   filepath = pgmoneta_append(filepath, relative_filename);
   /* Open the file in write mode, if not present create one */
   file = fopen(filepath, "w+");
   if (file == NULL)
   {
      pgmoneta_log_error("Write full file: failed to open the file at %s", relative_filename);
      goto error;
   }

   while (true)
   {
      if (pgmoneta_server_read_binary_file(server, ssl, relative_filename, offset, chunk_size,
                                           socket, &binary_data, &binary_data_length))
      {
         goto error;
      }

      /* EOF */
      if (binary_data_length == 0)
      {
         free(binary_data);
         break;
      }

      /* write the output */
      bytes_written = fwrite(binary_data, sizeof(uint8_t), binary_data_length, file);
      if (bytes_written != (size_t)binary_data_length)
      {
         goto error;
      }

      offset += binary_data_length;
      free(binary_data);
   }

   free(filepath);
   fclose(file);
   return 0;
error:
   free(binary_data);
   free(filepath);
   fclose(file);
   return 1;
}

static int
write_padding(FILE* file, size_t padding_length, size_t* bw)
{
   size_t bytes_written = 0;
   size_t chunk;
   size_t written;

   /* Use a fixed-size zero buffer to minimize syscalls */
   char zero_byte_buf[DEFAULT_BURST] = {0};

   while (padding_length > 0)
   {
      chunk = padding_length < DEFAULT_BURST ? padding_length : DEFAULT_BURST;

      written = fwrite(zero_byte_buf, 1, chunk, file);
      if (written != chunk)
      {
         pgmoneta_log_error("Write incremental file: failed to write padding to file");
         goto error;
      }

      bytes_written += written;
      padding_length -= written;
   }

   *bw = bytes_written;

   return 0;
error:
   return 1;
}

static char**
get_paths(char* backup_data, struct query_response* response, int* c)
{
   char** paths = NULL;
   int count = 0;
   int idx = 0;
   char* dest_path = NULL;
   struct tuple* tuple = NULL;

   if (response == NULL || response->number_of_columns != 3)
   {
      goto error;
   }

   /* count the number of server file and create any directory along the way */
   tuple = response->tuples;
   while (tuple != NULL)
   {
      if (pgmoneta_compare_string(tuple->data[1], "f"))
      {
         count++;
      }
      else
      {
         /* create the directory */
         dest_path = pgmoneta_append(dest_path, backup_data);

         if (pgmoneta_starts_with(tuple->data[0], "./"))
         {
            dest_path = pgmoneta_append(dest_path, tuple->data[0] + 2);
         }
         else
         {
            dest_path = pgmoneta_append(dest_path, tuple->data[0]);
         }

         if (pgmoneta_mkdir(dest_path))
         {
            pgmoneta_log_error("error creating directory: %s", dest_path);
            goto error;
         }

         free(dest_path);
         dest_path = NULL;
      }
      tuple = tuple->next;
   }

   paths = (char**)calloc(count + 1, sizeof(char*));

   /* get the server files */
   tuple = response->tuples;
   while (tuple != NULL && idx < count)
   {
      if (pgmoneta_compare_string(tuple->data[1], "f"))
      {
         if (pgmoneta_starts_with(tuple->data[0], "./"))
         {
            dest_path = pgmoneta_append(dest_path, tuple->data[0] + 2);
         }
         else
         {
            dest_path = pgmoneta_append(dest_path, tuple->data[0]);
         }

         paths[idx++] = dest_path;
         dest_path = NULL;
      }
      tuple = tuple->next;
   }

   *c = count;

   return paths;
error:
   free(dest_path);
   return NULL;
}

static int
copy_wal_from_archive(char* start_wal_file, char* wal_dir, char* backup_data)
{
   char* pg_wal_dir = NULL;
   char* dst_file = NULL;
   char* src_file = NULL;

   pg_wal_dir = pgmoneta_append(pg_wal_dir, backup_data);
   if (!pgmoneta_ends_with(pg_wal_dir, "/"))
   {
      pg_wal_dir = pgmoneta_append_char(pg_wal_dir, '/');
   }
   pg_wal_dir = pgmoneta_append(pg_wal_dir, "pg_wal/");

   struct deque* files = NULL;
   struct deque_iterator* it = NULL;

   if (pgmoneta_get_wal_files(wal_dir, &files))
   {
      pgmoneta_log_warn("Unable to get WAL segments under %s", wal_dir);
      goto error;
   }

   pgmoneta_deque_iterator_create(files, &it);
   while (pgmoneta_deque_iterator_next(it))
   {
      char* file_name = (char*)it->value->data;
      if (strcmp(file_name, start_wal_file) >= 0)
      {
         dst_file = pgmoneta_append(dst_file, pg_wal_dir);
         dst_file = pgmoneta_append(dst_file, file_name);
         src_file = pgmoneta_append(src_file, wal_dir);
         src_file = pgmoneta_append(src_file, file_name);

         if (!pgmoneta_is_file(src_file))
         {
            pgmoneta_log_warn("WAL segment %s does not exist in source", file_name);
            goto error;
         }

         // copy and extract
         if (pgmoneta_copy_and_extract_file(src_file, &dst_file))
         {
            goto error;
         }

         free(dst_file);
         free(src_file);
         dst_file = NULL;
         src_file = NULL;
      }
   }
   pgmoneta_deque_iterator_destroy(it);
   it = NULL;

   pgmoneta_deque_destroy(files);
   free(pg_wal_dir);
   return 0;
error:
   pgmoneta_deque_destroy(files);
   pgmoneta_deque_iterator_destroy(it);
   free(dst_file);
   free(src_file);
   free(pg_wal_dir);
   return 1;
}

static int
wait_for_wal_switch(char* wal_dir, char* wal_file)
{
   int loop = 1;
   struct deque* files = NULL;
   struct deque_iterator* it = NULL;

   while (loop)
   {
      files = NULL;
      if (pgmoneta_get_wal_files(wal_dir, &files))
      {
         pgmoneta_log_warn("Unable to get WAL segments under %s", wal_dir);
         goto error;
      }
      pgmoneta_deque_iterator_create(files, &it);
      while (pgmoneta_deque_iterator_next(it))
      {
         char* file_name = (char*)it->value->data;
         if (strcmp(file_name, wal_file) == 0)
         {
            loop = 0;
         }
      }
      pgmoneta_deque_iterator_destroy(it);
      it = NULL;

      pgmoneta_deque_destroy(files);
      SLEEP(1); // avoid wasting CPU cycles for searching
   }

   return 0;
error:
   pgmoneta_deque_destroy(files);
   pgmoneta_deque_iterator_destroy(it);
   return 1;
}

static int
send_upload_manifest(SSL* ssl, int socket)
{
   struct message* msg = NULL;
   int status;
   pgmoneta_create_query_message("UPLOAD_MANIFEST", &msg);
   status = pgmoneta_write_message(ssl, socket, msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgmoneta_free_message(msg);
   return 0;

error:
   pgmoneta_free_message(msg);
   return 1;
}

static int
upload_manifest(SSL* ssl, int socket, char* path)
{
   FILE* manifest = NULL;
   size_t nbytes = 0;
   char buffer[65536];

   manifest = fopen(path, "r");
   if (manifest == NULL)
   {
      pgmoneta_log_error("Upload manifest: failed to open manifest file at %s", path);
      goto error;
   }
   while ((nbytes = fread(buffer, 1, sizeof(buffer), manifest)) > 0)
   {
      if (pgmoneta_send_copy_data(ssl, socket, buffer, nbytes))
      {
         pgmoneta_log_error("Upload manifest: failed to send copy data");
         goto error;
      }
   }
   if (pgmoneta_send_copy_done_message(ssl, socket))
   {
      goto error;
   }

   if (manifest != NULL)
   {
      fclose(manifest);
   }
   return 0;

error:
   if (manifest != NULL)
   {
      fclose(manifest);
   }
   return 1;
}
