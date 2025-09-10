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
#include <network.h>
#include <security.h>
#include <server.h>
#include <utils.h>
#include <walfile/wal_reader.h>
#include <workflow.h>

/* system */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* fetch/compute these from server configuration inside create workflow */
size_t block_size;     // size of each block (default 8KB)
size_t segment_size;   // segment size
size_t rel_seg_size;   // number of blocks in a segment

/**
 * Get the size of the incremental file
 */
static size_t get_incremental_file_size(uint32_t num_incr_blocks);

/**
 * Get the size of the header of incremental file
 */
static size_t get_incremental_header_size(uint32_t num_incr_blocks);

/**
 * Serialize the incremental blocks for a relation file
 */
static int write_incremental_file(int server, SSL* ssl, int socket, 
                                    char* filename, struct rel_file_locator rlocator, 
                                    enum fork_number frk, uint32_t num_incr_blocks, 
                                    block_number* incr_blocks, uint32_t truncation_block_length);

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
    int usr;
    SSL* ssl = NULL;
    int socket = -1;
    char* filename = NULL;
    struct rel_file_locator rlocator = {0};
    enum fork_number frk = MAIN_FORKNUM;
    uint32_t num_incr_blocks = 0;
    block_number* incr_blocks = NULL;
    uint32_t truncation_block_length = 0;
    struct main_configuration* config;

    config = (struct main_configuration*)shmem;

    server = (int)pgmoneta_art_search(nodes, NODE_SERVER_ID);

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

    block_size = config->common.servers[server].block_size;
    segment_size = config->common.servers[server].segment_size;
    rel_seg_size = config->common.servers[server].relseg_size;

    /* TODO: start backup */

    /* write the INCREMENTAL file */
    write_incremental_file(server, ssl, socket, filename, rlocator, frk, 
        num_incr_blocks, incr_blocks, truncation_block_length);

    /* TODO: stop backup */

    return 0;

error:
    return 1;
}

static size_t get_incremental_header_size(uint32_t num_incr_blocks)
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

static size_t get_incremental_file_size(uint32_t num_incr_blocks)
{
    size_t result;
    
    result = get_incremental_header_size(num_incr_blocks);
    result += block_size * num_incr_blocks;

    return result;
}

static int write_incremental_file(int server, SSL* ssl, int socket,
                                     char* filename, struct rel_file_locator rlocator, 
                                    enum fork_number frk, uint32_t num_incr_blocks, 
                                    block_number* incr_blocks, uint32_t truncation_block_length)
{
    FILE* file = NULL;
    size_t expected_file_size;
    int segno;
    uint32_t magic = INCREMENTAL_MAGIC;
    char* relative_relation_base_path = NULL;
    char* relative_relation_path = NULL;
    uint32_t incr_blocks_index = 0;
    uint32_t padding_length = 0;
    uint32_t blkno;
    size_t bytes_written = 0;
    uint8_t* binary_data = NULL;
    int binary_data_length = 0;
    char zero_byte = 0;

    /* Open the file in write mode, if not present create one */
    file = fopen(filename, "w");
    if (file == NULL)
    {
        pgmoneta_log_error("Write incremental file: failed to open the file at %s", filename);
        goto error;
    }

    /* Write the file header */
    bytes_written += fwrite(&magic, sizeof(magic), 1, file);
    bytes_written += fwrite(&num_incr_blocks, sizeof(num_incr_blocks), 1, file);
    bytes_written += fwrite(&truncation_block_length, sizeof(truncation_block_length), 1, file);
    bytes_written += fwrite(incr_blocks, sizeof(block_number), num_incr_blocks, file);
    
    // Apply padding if applicable
    if ((num_incr_blocks > 0) && (bytes_written % block_size != 0))
    {
        padding_length = (block_size - (bytes_written % block_size));
        bytes_written += fwrite(&zero_byte, sizeof(zero_byte), padding_length, file);
    }

    if (bytes_written != get_incremental_header_size(num_incr_blocks))
    {
        pgmoneta_log_error("Write incremental file: failed to open the file at %s", filename);
        goto error;
    }

    /* Get the relative file path to extract data from */
    if (rlocator.spcOid != 0)
    {
        pgmoneta_log_warn("Write incremental file: tablespaces are not currently supported");
        goto done;
    }

    if (rlocator.dbOid == 0) 
    {
        relative_relation_base_path = pgmoneta_append(relative_relation_base_path, "global/");
    }
    else
    {
        relative_relation_base_path = pgmoneta_append(relative_relation_base_path, "base/");
        relative_relation_base_path = pgmoneta_append_int(relative_relation_base_path, rlocator.dbOid);
        relative_relation_base_path = pgmoneta_append_char(relative_relation_base_path, '/');
    }
    relative_relation_base_path = pgmoneta_append_int(relative_relation_base_path, rlocator.relNumber);

    /* Handle forknumber */
    if (frk == FSM_FORKNUM || frk == INIT_FORKNUM)
    {
        /* skip */
        goto done;
    }
    else if (frk == VISIBILITYMAP_FORKNUM)
    {
        relative_relation_base_path = pgmoneta_append(relative_relation_base_path, "_vm");
    }

    expected_file_size = get_incremental_file_size(num_incr_blocks) - bytes_written;
    /* 
        Request the blocks from the server 

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

        Assume the incremental block array is sorted, also note that we may have to consider 
        the filename with their segment number, which can be determined using the block number
        segment_number = block_number / (# of blocks in each segment)

        Will try to fetch untill either we get all the blocks (from server) with block number
        provided by the caller or request failed due to side effects like concurrent truncation.
    */
    while (1)
    {
        /* our work is done here, done reading all the desired blocks */
        if (incr_blocks_index >= num_incr_blocks)
        {
            break;
        }

        blkno = incr_blocks[incr_blocks_index++];
        /* check if the segment changes, if so change the filename */
        if (segno != blkno / rel_seg_size)
        {
            free(relative_relation_path);
            segno = blkno / rel_seg_size;
            relative_relation_path = pgmoneta_append_char(relative_relation_base_path, '.');
            relative_relation_path = pgmoneta_append_int(relative_relation_base_path, segno);
        }

        if (pgmoneta_server_read_binary_file(server, ssl, relative_relation_path, 
            block_size * (blkno - segno * rel_seg_size), block_size, socket, &binary_data, &binary_data_length))
        {
            pgmoneta_log_error("Write incremental file: ");
            goto error;
        }

        /*
            If partial read, means the relation is truncated after the incremental workflow has started.
            Not to worry, just fill all the blocks including this one with 0, untill we wrote the number
             of bytes expected by caller, WAL replay will take care of it later.
        */
        if (binary_data_length < block_size)
        {
            break;
        }

        bytes_written += fwrite(binary_data, sizeof(uint8_t), binary_data_length, file);
        /* read/write content must be of multiple of block size length */
        if (bytes_written % block_size)
        {
            pgmoneta_log_error("Write incremental file: partial write/read");
            goto error;
        }
    }

    /* Handle truncation, by padding with 0 */
    while (bytes_written < expected_file_size)
    {
        size_t diff = expected_file_size - bytes_written;
        bytes_written += fwrite(&zero_byte, sizeof(zero_byte), diff, file);
        if (bytes_written <= 0)
        {   
            pgmoneta_log_error("Write incremental file: failed to write bytes to a file");
            goto error;
        }
    }

done:
    free(relative_relation_base_path);
    free(relative_relation_path);
    fclose(file);
    return 0;

error:

    free(relative_relation_base_path);
    free(relative_relation_path);
    fclose(file);
    return 1;
}