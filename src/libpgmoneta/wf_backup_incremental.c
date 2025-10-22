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
#include <backup.h>
#include <logging.h>
#include <memory.h>
#include <network.h>
#include <security.h>
#include <server.h>
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

/* fetch/compute these from server configuration inside create workflow */
size_t block_size;       // size of each block (default 8KB)
size_t segment_size;     // segment size
size_t rel_seg_size;     // number of blocks in a segment
size_t wal_segemnt_size; // wal segment size

char* STANDARD_DIRECTORIES[] = {"base/", "global/", "pg_wal/"};

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
 * Given a rlocator and fork pair, derive the relative file path inside the data directory
 */
static int prepare_relation_file(char* backup_data, struct rel_file_locator rlocator, enum fork_number frk, int segno, char** fp);

/**
 * Create standard directories inside data directory of backup
 */
static int create_standard_directories(char* backup_data);

/**
 * Serialize the incremental blocks for a relation file
 */
static int write_incremental_file(int server, SSL* ssl, int socket, char* backup_data, 
                            char* relative_filename, uint32_t num_incr_blocks, 
                            block_number* incr_blocks, uint32_t truncation_block_length);

/**
 * Serialize all the blocks for a relation file
 */
static int write_full_file(int server, SSL* ssl, int socket, char* backup_data,
                            char* relative_filename, size_t expected_size);

/**
 * Append padding (0 bytes) to the file stream
 */
static int write_padding(FILE* file, size_t padding_length, size_t* bytes_written);

/*
 * Wait until the WAL segment file appears in the wal archive directory
 */
static int wait_for_wal_switch(char* wal_dir, char* wal_file);

/**
 * copy the wal files from the archive, the idea is copy only wal files that are generated after
 * the backup was started (including the file which was the current WAL segment at the time of backup)
 */
static int copy_wal_from_archive(char* min_wal_file, char* wal_dir, char* backup_data);

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
incr_backup_execute(char* name __attribute__((unused)), struct art* nodes)
{
    int server = -1;
    char* label = NULL;
    struct timespec start_t;
    struct timespec end_t;
    char* backup_base = NULL;
    char* backup_data = NULL;
    char* server_backup = NULL;
    char* incremental = NULL;
    char* incremental_label = NULL;
    int usr;
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
    uint64_t start_backup_chkpt_lsn = 0;
    uint64_t stop_backup_chkpt_lsn = 0;
    char* start_chkpt = NULL;
    char* stop_chkpt = NULL;
    uint32_t start_tli = 0;
    uint32_t stop_tli = 0;
    block_ref_table* summarized_brt = NULL;
    char* wal_filename = NULL;
    int num_incr_blocks = 0;
    block_number* incr_blocks = NULL;
    uint32_t truncation_block_length = 0;
    struct art_iterator* iter = NULL;

    char* relation_filename = NULL;
    block_ref_table_entry* brtentry = NULL;
    block_number start_blk = 0;
    block_number end_blk = 0;

    int segno = 0;
    int segs = 0;
    struct rel_file_locator rlocator = {0};
    enum fork_number frk = MAIN_FORKNUM;
    struct file_stats fs = {0};

    struct backup* backup = NULL;
    struct main_configuration* config;

    struct message* msg = NULL;
    struct query_response* response = NULL;

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

    if ((incremental != NULL && incremental_label == NULL) ||
        (incremental == NULL && incremental_label != NULL))
    {
        pgmoneta_log_error("base and label for incremental should either be both NULL or both non-NULL");
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
    wal_segemnt_size = config->common.servers[server].wal_size;

    /* Get the checkpoint information of the preceding backup using backup_label */
    prev_backup_data = pgmoneta_get_server_backup_identifier_data(server, incremental_label);
    pgmoneta_read_checkpoint_info(prev_backup_data, &chkpt_lsn);

    prev_backup_chkpt_lsn = pgmoneta_string_to_lsn(chkpt_lsn);

    /* Start Backup */
    if (pgmoneta_server_checkpoint(server, ssl, socket, &start_backup_chkpt_lsn, &start_tli))
    {
        pgmoneta_log_error("Incr Backup: couldn't start backup because checkpoint failed");
        goto error;
    }

    /* Switch WAL segment */
    pgmoneta_create_query_message("SELECT pg_switch_wal();", &msg);
    if (pgmoneta_query_execute(ssl, socket, msg, &response) || response == NULL)
    {
        goto error;
    }

    wal_dir = pgmoneta_get_server_wal(server);
    wal_filename = pgmoneta_wal_file_name(start_tli, start_backup_chkpt_lsn/wal_segemnt_size, wal_segemnt_size);
    if (wal_filename == NULL)
    {
        pgmoneta_log_warn("Failed to generate WAL file name for timeline_id: %u and xlogpos: %ld", start_tli, start_backup_chkpt_lsn);
        goto error;
    }

    /* wait till the wal segment is not partial anymore */
    if (wait_for_wal_switch(wal_dir, wal_filename))
    {
        goto error;
    }

    /* Do WAL Summarization */
    if (pgmoneta_summarize_wal(server, wal_dir, prev_backup_chkpt_lsn, start_backup_chkpt_lsn, &summarized_brt))
    {
        pgmoneta_log_error("Incr Backup: wal summarization failed");
        goto error;
    }

    pgmoneta_mkdir(backup_data);
    create_standard_directories(backup_data);

    /* Iterate the BRT table */
    if (pgmoneta_art_iterator_create(summarized_brt->table, &iter))
    {
        pgmoneta_log_error("Incr Backup: error creating iterator for BRT");
        goto error;
    }

    while(pgmoneta_art_iterator_next(iter))
    {
        brtentry = (block_ref_table_entry*)iter->value->data;
        /* 
            just in case we get an empty brt entry, means the relation file has not changed 
            since the preceding backup.
        
            In such cases don't create incremental file.
        */
        if (brtentry == NULL)
        {
            continue;
        }

        rlocator = brtentry->key.rlocator;
        frk = brtentry->key.forknum;

        segs = brtentry->max_block_number / rel_seg_size;
        /*
            The format of the relation file name is as follows:
            <relationfile_oid>[_<fork_identifier>][.<segment_number>]

            - fork identifier: tells us about the variant of the relation file (heap/index)
                - main data file
                - free space map file (`_fsm`)
                - visibility map file (`_vm`)
                - init file (`_init`)

            - segment number: When a table or index exceeds 1 GB, it is divided into 
            gigabyte-sized segments. The first segment's file name is the same as the filenode; 
            subsequent segments are named filenode.1, filenode.2, etc. This arrangement avoids 
            problems on platforms that have file size limitations.
         */
        for (segno = segs; segno >= 0; segno--)
        {
            start_blk = segs * rel_seg_size;
            end_blk = start_blk + rel_seg_size;

            /*
                Prepare the relation file, may create sub directory if not present
            */
            if (prepare_relation_file(backup_data, rlocator, frk, segno, &relation_filename))
            {
                pgmoneta_log_error("Incr Backup: error preparing relation file");
                goto error;
            }

            if (pgmoneta_server_file_stat(server, ssl, socket, relation_filename, &fs))
            {
                pgmoneta_log_error("Incr Backup: error finding stats of the file: %s", relation_filename);
                goto error;
            }

            /*
                if the fork number are such that they are not properly WAL logged, full backups
            */
            if (frk == FSM_FORKNUM || frk == INIT_FORKNUM)
            {
                if (write_full_file(server, ssl, socket, backup_data, relation_filename, fs.size))
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
            if (brtentry->limit_block <= segno * rel_seg_size)
            {
                if (write_full_file(server, ssl, socket, backup_data, relation_filename, fs.size))
                {
                    goto error;
                }
                continue;
            }
            
            incr_blocks = (block_number*)malloc(rel_seg_size * sizeof(block_number));
            if (pgmoneta_brt_entry_get_blocks(brtentry, start_blk, end_blk, incr_blocks, rel_seg_size, &num_incr_blocks))
            {
                pgmoneta_log_error("Incr Backup: error getting modified blocks from brt entry");
                goto error;
            }
            
            /*
                sort the blocks numbers and translate the absolute block numbers to relative
            */
            qsort(incr_blocks, num_incr_blocks, sizeof(block_number), compare_block_numbers);
            if (start_blk != 0)
            {
                for (int i = 0; i < num_incr_blocks; i++)
                    incr_blocks[i] -= start_blk;
            }

            /* 
                Calculate truncation length which is minimum length of the reconstructed file. Any 
                block numbers below this threshold that are not present in the backup need to be 
                fetched from the prior backup.
            */
            truncation_block_length = fs.size;
            if (brtentry->limit_block != InvalidBlockNumber)
            {
                uint32_t relative_limit = brtentry->limit_block - segno * rel_seg_size;
                if (truncation_block_length < relative_limit)
                    truncation_block_length = relative_limit;
            }
            
            /* serialize the incremental changes */
            if (write_incremental_file(server, ssl, socket, backup_data, relation_filename, 
                num_incr_blocks, incr_blocks, truncation_block_length))
            {
                goto error;
            }
            free(incr_blocks);
            free(relation_filename);
        }
    }
    pgmoneta_art_iterator_destroy(iter);

    /* Stop Backup */
    if (pgmoneta_server_checkpoint(server, ssl, socket, &stop_backup_chkpt_lsn, &stop_tli))
    {
        pgmoneta_log_error("Incr Backup: couldn't stop backup because checkpoint failed");
        goto error;
    }

    /* copy wal */
    if (copy_wal_from_archive(wal_filename, wal_dir, backup_data))
    {
        pgmoneta_log_error("Incr Backup: error copying wal from archive");
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

    start_chkpt = pgmoneta_lsn_to_string(start_backup_chkpt_lsn);
    stop_chkpt = pgmoneta_lsn_to_string(stop_backup_chkpt_lsn);

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

    sscanf(start_chkpt, "%X/%X", &backup->start_lsn_hi32, &backup->start_lsn_lo32);    
    sscanf(stop_chkpt, "%X/%X", &backup->end_lsn_hi32, &backup->end_lsn_lo32);
    backup->start_timeline = start_tli;
    backup->end_timeline = stop_tli;
    backup->basebackup_elapsed_time = incr_backup_elapsed_time;
    backup->type = TYPE_INCREMENTAL;
    snprintf(backup->parent_label, sizeof(backup->parent_label), "%s", incremental_label);
    sscanf(start_chkpt, "%X/%X", &backup->checkpoint_lsn_hi32, &backup->checkpoint_lsn_lo32);

    if (pgmoneta_save_info(server_backup, backup))
    {
        pgmoneta_log_error("Incr Backup: Could not save backup %s", label);
        goto error;
    }
    pgmoneta_close_ssl(ssl);
    if (socket != -1)
    {
        pgmoneta_disconnect(socket);
    }

    free(chkpt_lsn);
    free(start_chkpt);
    free(stop_chkpt);
    free(wal_dir);
    free(wal);
    free(wal_filename);
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

    free(chkpt_lsn);
    free(start_chkpt);
    free(stop_chkpt);
    free(incr_blocks);
    free(wal_dir);
    free(wal);
    free(wal_filename);
    free(relation_filename);
    free(prev_backup_data);
    pgmoneta_free_message(msg);
    pgmoneta_free_query_response(response);
    pgmoneta_brt_destroy(summarized_brt);
    pgmoneta_memory_destroy();
    return 1;
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
    
    if (aa < bb) return -1;
    if (aa > bb) return 1;
    return 0;
}

static int
create_standard_directories(char* backup_data)
{
    char* standard_dir = NULL;
    int number_of_directories;

    number_of_directories = (sizeof(STANDARD_DIRECTORIES) / sizeof(char*));

    for (int i = 0; i < number_of_directories; i++)
    {
        standard_dir = pgmoneta_append(standard_dir, backup_data);
        standard_dir = pgmoneta_append(standard_dir, STANDARD_DIRECTORIES[i]);

        if (!pgmoneta_ends_with(standard_dir, "/"))
        {
            standard_dir = pgmoneta_append(standard_dir, "/");
        }

        if (pgmoneta_mkdir(standard_dir))
        {
            goto error;
        }

        free(standard_dir);
        standard_dir = NULL;
    }

    free(standard_dir);
    return 0;
error:
    free(standard_dir);
    return 1;
}

static int
prepare_relation_file(char* backup_data, struct rel_file_locator rlocator, enum fork_number frk, int segno, char** fp)
{
    char* rel_file_path = NULL;
    char* full_file_path = NULL;

    if (rlocator.dbOid == 0) // global file
    {
        rel_file_path = pgmoneta_append(rel_file_path, "global/");
        rel_file_path = pgmoneta_append_int(rel_file_path, rlocator.relNumber);
    }
    else // base file
    {
        rel_file_path = pgmoneta_append(rel_file_path, "base/");
        rel_file_path = pgmoneta_append_int(rel_file_path, rlocator.dbOid);
        rel_file_path = pgmoneta_append(rel_file_path, "/");

        full_file_path = pgmoneta_append(full_file_path, backup_data);
        if (!pgmoneta_ends_with(full_file_path, "/"))
        {
            full_file_path = pgmoneta_append_char(full_file_path, '/');
        }
        full_file_path = pgmoneta_append(full_file_path, rel_file_path);
        if (!pgmoneta_exists(full_file_path) && pgmoneta_mkdir(full_file_path))
        {
            goto error;
        }

        rel_file_path = pgmoneta_append_int(rel_file_path, rlocator.relNumber);
    }

    /* handle fork */
    switch (frk)
    {
    case MAIN_FORKNUM:
        /* ok */
        break;
    case VISIBILITYMAP_FORKNUM:
        rel_file_path = pgmoneta_append(rel_file_path, "_vm");
        break;
    default:
        goto error;
    }

    /* handle segno */
    if (segno > 0)
    {
        rel_file_path = pgmoneta_append(rel_file_path, ".");
        rel_file_path = pgmoneta_append_int(rel_file_path, segno);
    }

    *fp = rel_file_path;

    free(full_file_path);
    return 0;
error:
    free(full_file_path);
    free(rel_file_path);
    return 1;
}

static int
write_incremental_file(int server, SSL* ssl, int socket, char* backup_data, 
                            char* relative_filename, uint32_t num_incr_blocks, 
                            block_number* incr_blocks, uint32_t truncation_block_length)
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

    while(true)
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
wait_for_wal_switch(char* wal_dir, char* wal_file)
{
    int loop = 1;
    int num_files = 0;
    char** files = NULL;

    while(loop)
    {
        files = NULL;
        if (pgmoneta_get_wal_files(wal_dir, &num_files, &files))
        {
            pgmoneta_log_warn("Unable to get WAL segments under %s", wal_dir);
            goto error;
        }

        for (int i = 0; i < num_files; i++)
        {
            if (strcmp(files[i], wal_file) == 0)
            {
                loop = 0;
            }
            free(files[i]);
        }

        free(files);
        SLEEP(1); // avoid wasting CPU cycles for searching
    }

    return 0;
error:
    for (int i = 0; i < num_files; i++)
    {
        free(files[i]);
    }
    free(files);
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

static int
copy_wal_from_archive(char* min_wal_file, char* wal_dir, char* backup_data)
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

    int num_files = 0;
    char** files = NULL;

    if (pgmoneta_get_wal_files(wal_dir, &num_files, &files))
    {
        pgmoneta_log_warn("Unable to get WAL segments under %s", wal_dir);
        goto error;
    }

    for (int i = 0; i < num_files; i++)
    {
        if (strcmp(files[i], min_wal_file) >= 0)
        {
            dst_file = pgmoneta_append(dst_file, pg_wal_dir);
            dst_file = pgmoneta_append(dst_file, files[i]);
            src_file = pgmoneta_append(src_file, wal_dir);
            src_file = pgmoneta_append(src_file, files[i]);

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
        free(files[i]);
    }

    free(files);
    free(pg_wal_dir);
    return 0;
error:
    for (int i = 0; i < num_files; i++)
    {
        free(files[i]);
    }
    free(files);
    free(dst_file);
    free(src_file);
    free(pg_wal_dir);
    return 1;
}
