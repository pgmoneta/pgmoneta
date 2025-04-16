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
 *
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <configuration.h>
#include <deque.h>
#include <json.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <walfile/pg_control.h>
#include <security.h>
#include <shmem.h>
#include <utils.h>
#include <value.h>
#include <tsclient.h>
#include <walfile.h>

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

char project_directory[BUFFER_SIZE];

static int check_output_outcome(int socket);
static int get_connection();
static char* get_configuration_path();
static char* get_restore_path();
static int compare_walfile(struct walfile* wf1, struct walfile* wf2);
static bool compare_long_page_headers(struct xlog_long_page_header_data* h1, struct xlog_long_page_header_data* h2);
static bool compare_deque(struct deque* dq1, struct deque* dq2, bool (*compare)(void*, void*));
static bool compare_xlog_page_header(void* a, void* b);
static bool compare_xlog_record(void* a, void* b);
static void pgmoneta_free_walfile(struct walfile* wf);

int
pgmoneta_tsclient_init(char* base_dir)
{
    struct main_configuration* config = NULL;
    int ret;
    size_t size;
    char* configuration_path = NULL;
    size_t configuration_path_length = 0;

    memset(project_directory, 0, sizeof(project_directory));
    memcpy(project_directory, base_dir, strlen(base_dir));

    configuration_path = get_configuration_path();

    // Create the shared memory for the configuration
    size = sizeof(struct main_configuration);
    if (pgmoneta_create_shared_memory(size, HUGEPAGE_OFF, &shmem))
    {
        goto error;
    }
    pgmoneta_init_main_configuration(shmem);
    // Try reading configuration from the configuration path
    if (configuration_path != NULL)
    {
        ret = pgmoneta_read_main_configuration(shmem, configuration_path);
        if (ret)
        {
            goto error;
        }

        config = (struct main_configuration*)shmem;
    }
    else
    {
        goto error;
    } 

    free(configuration_path);
    return 0;
error:
    free(configuration_path);
    return 1;
}

int
pgmoneta_tsclient_destroy()
{
    size_t size;

    size = sizeof(struct main_configuration);
    return pgmoneta_destroy_shared_memory(shmem, size);
}

int
pgmoneta_tsclient_execute_backup(char* server, char* incremental)
{
    int socket = -1;
    
    socket = get_connection();
    // Security Checks
    if (!pgmoneta_socket_isvalid(socket) || server == NULL)
    {
        goto error;
    }
    // Create a backup request to the main server
    if (pgmoneta_management_request_backup(NULL, socket, server, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, incremental, MANAGEMENT_OUTPUT_FORMAT_JSON))
    {
        goto error;
    }

    // Check the outcome field of the output, if true success, else failure
    if (check_output_outcome(socket))
    {
        goto error;
    }

    pgmoneta_disconnect(socket);
    return 0;
error:
    pgmoneta_disconnect(socket);
    return 1;
}

int
pgmoneta_tsclient_execute_restore(char* server, char* backup_id, char* position)
{
    char* restore_path = NULL;
    int socket = -1;
    
    socket = get_connection();
    // Security Checks
    if (!pgmoneta_socket_isvalid(socket) || server == NULL)
    {
        goto error;
    }

    // Fallbacks
    if (backup_id == NULL)
    {
        backup_id = "newest";
    }

    restore_path = get_restore_path();
    // Create a restore request to the main server
    if (pgmoneta_management_request_restore(NULL, socket, server, backup_id, position, restore_path, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
    {
        goto error;
    }

    // Check the outcome field of the output, if true success, else failure
    if (check_output_outcome(socket))
    {
        goto error;
    }

    free(restore_path);
    pgmoneta_disconnect(socket);
    return 0;
error:
    free(restore_path);
    pgmoneta_disconnect(socket);
    return 1;
}

int
pgmoneta_tsclient_execute_delete(char* server, char* backup_id)
{
    int socket = -1;

    socket = get_connection();
    // Security Checks
    if (!pgmoneta_socket_isvalid(socket) || server == NULL)
    {
        return 1;
    }

    // Fallbacks
    if (!backup_id) {
        backup_id = "oldest";
    }

    // Create a delete request to the main server
    if (pgmoneta_management_request_delete(NULL, socket, server, backup_id, MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, MANAGEMENT_OUTPUT_FORMAT_JSON))
    {
        goto error;
    }

    // Check the outcome field of the output, if true success, else failure
    if (check_output_outcome(socket))
    {
        goto error;
    }

    pgmoneta_disconnect(socket);
    return 0;
error:
    pgmoneta_disconnect(socket);
    return 1;
}

static int 
check_output_outcome(int socket)
{
    struct json* read = NULL;
    struct json* outcome = NULL;

    if (pgmoneta_management_read_json(NULL, socket, NULL, NULL, &read))
    {
        goto error;
    }
    
    if (!pgmoneta_json_contains_key(read, MANAGEMENT_CATEGORY_OUTCOME))
    {
        goto error;
    }

    outcome = (struct json*)pgmoneta_json_get(read, MANAGEMENT_CATEGORY_OUTCOME);
    if (!pgmoneta_json_contains_key(outcome, MANAGEMENT_ARGUMENT_STATUS) && !(bool)pgmoneta_json_get(outcome, MANAGEMENT_ARGUMENT_STATUS))
    {
        goto error;
    }

    pgmoneta_json_destroy(read);
    return 0;
error:
    pgmoneta_json_destroy(read);
    return 1;
}

static int
get_connection()
{
    int socket = -1;
    struct main_configuration* config;

    config = (struct main_configuration*)shmem;
    if (!strlen(config->common.configuration_path))
    {
        if (pgmoneta_connect_unix_socket(config->unix_socket_dir, MAIN_UDS, &socket))
        {
            return -1;
        }
    }
    return socket;
}

static char*
get_restore_path()
{
   char* restore_path = NULL;
   int project_directory_length = strlen(project_directory);
   int restore_trail_length = strlen(PGMONETA_RESTORE_TRAIL);

   restore_path = (char*)calloc(project_directory_length + restore_trail_length + 1, sizeof(char));

   memcpy(restore_path, project_directory, project_directory_length);
   memcpy(restore_path + project_directory_length, PGMONETA_RESTORE_TRAIL, restore_trail_length);

   return restore_path;
}

static char*
get_configuration_path()
{
   char* configuration_path = NULL;
   int project_directory_length = strlen(project_directory);
   int configuration_trail_length = strlen(PGMONETA_CONFIGURATION_TRAIL);

   configuration_path = (char*)calloc(project_directory_length + configuration_trail_length + 1, sizeof(char));

   memcpy(configuration_path, project_directory, project_directory_length);
   memcpy(configuration_path + project_directory_length, PGMONETA_CONFIGURATION_TRAIL, configuration_trail_length);

   return configuration_path;
}

static int
compare_walfile(struct walfile* wf1, struct walfile* wf2)
{
   if (wf1 == NULL || wf2 == NULL)
   {
      return (wf1 == wf2) ? 0 : -1;
   }

   if (wf1->magic_number != wf2->magic_number)
   {
      pgmoneta_log_error("Magic number mismatch: %u != %u\n", wf1->magic_number, wf2->magic_number);
      return -1;
   }

   if (!compare_long_page_headers(wf1->long_phd, wf2->long_phd))
   {
      pgmoneta_log_error("Long page header mismatch\n");
      return -1;
   }

   if (!compare_deque(wf1->page_headers, wf2->page_headers, compare_xlog_page_header))
   {
      pgmoneta_log_error("Page headers deque mismatch\n");
      return -1;
   }

   if (!compare_deque(wf1->records, wf2->records, compare_xlog_record))
   {
      pgmoneta_log_error("Records deque mismatch\n");
      return -1;
   }

   return 0;
}

static bool
compare_long_page_headers(struct xlog_long_page_header_data* h1, struct xlog_long_page_header_data* h2)
{
   if (h1 == NULL || h2 == NULL)
   {
      return (h1 == h2);
   }

   return (h1->std.xlp_magic == h2->std.xlp_magic &&
           h1->std.xlp_info == h2->std.xlp_info &&
           h1->std.xlp_tli == h2->std.xlp_tli &&
           h1->std.xlp_pageaddr == h2->std.xlp_pageaddr &&
           h1->xlp_seg_size == h2->xlp_seg_size &&
           h1->xlp_xlog_blcksz == h2->xlp_xlog_blcksz);
}

static bool
compare_deque(struct deque* dq1, struct deque* dq2, bool (*compare)(void*, void*))
{
   if (dq1 == NULL || dq2 == NULL)
   {
      return (dq1 == dq2);
   }

   if (pgmoneta_deque_size(dq1) != pgmoneta_deque_size(dq2))
   {
      pgmoneta_log_error("Deque sizes mismatch: %u != %u\n", pgmoneta_deque_size(dq1), pgmoneta_deque_size(dq2));
      return false;
   }

   struct deque_iterator* iter1 = NULL;
   struct deque_iterator* iter2 = NULL;
   bool equal = true;

   if (pgmoneta_deque_iterator_create(dq1, &iter1) != 0 ||
       pgmoneta_deque_iterator_create(dq2, &iter2) != 0)
   {
      equal = false;
      goto cleanup;
   }

   while (pgmoneta_deque_iterator_next(iter1) && pgmoneta_deque_iterator_next(iter2))
   {
      uintptr_t data1 = iter1->value->data;
      uintptr_t data2 = iter2->value->data;

      if (!compare((void*) data1, (void*) data2))
      {
         pgmoneta_log_error("Deque elements mismatch: %p != %p\n", (void*)data1, (void*)data2);
         equal = false;
         goto cleanup;
      }
   }

   if (pgmoneta_deque_iterator_next(iter1) || pgmoneta_deque_iterator_next(iter2))
   {
      equal = false;
   }

cleanup:
   pgmoneta_deque_iterator_destroy(iter1);
   pgmoneta_deque_iterator_destroy(iter2);
   return equal;
}

static bool
compare_xlog_page_header(void* a, void* b)
{
   struct xlog_page_header_data* ph1 = (struct xlog_page_header_data*)a;
   struct xlog_page_header_data* ph2 = (struct xlog_page_header_data*)b;

   return (ph1->xlp_magic == ph2->xlp_magic &&
           ph1->xlp_info == ph2->xlp_info &&
           ph1->xlp_tli == ph2->xlp_tli &&
           ph1->xlp_pageaddr == ph2->xlp_pageaddr);
}

static bool
compare_xlog_record(void* a, void* b)
{
   struct decoded_xlog_record* rec1 = (struct decoded_xlog_record*)a;
   struct decoded_xlog_record* rec2 = (struct decoded_xlog_record*)b;

   if (memcmp(&rec1->header, &rec2->header, sizeof(struct xlog_record)) != 0)
   {
      pgmoneta_log_error("xlog_record header mismatch\n");
      return false;
   }

   if (rec1->main_data_len != rec2->main_data_len)
   {
      pgmoneta_log_error("xlog_record length mismatch\n");
      return false;
   }

   if (rec1->main_data_len != 0 && memcmp(rec1->main_data, rec2->main_data, rec1->main_data_len) != 0)
   {
      pgmoneta_log_error("xlog_record data mismatch\n");
      return false;
   }

   return true;
}

int
test_walfile(struct walfile* (*generate)(void))
{
   char* path = NULL;
   struct walfile* wf = NULL;
   struct walfile* read_wf = NULL;

   path = pgmoneta_append(NULL, project_directory);
   if (path == NULL)
   {
      goto error;
   }
   path = pgmoneta_append(path, "/walfiles/00000001000000000000001D");
   if (path == NULL)
   {
      goto error;
   }

   // 1. Prepare walfile structure
   wf = generate();
   if (wf == NULL)
   {
      goto error;
   }

   // 2. Write this structure to disk
   if (pgmoneta_write_walfile(wf, 0, path))
   {
      pgmoneta_log_error("Error writing walfile to disk\n");
      goto error;
   }

   // 3. Read the walfile from disk
   if (pgmoneta_read_walfile(0, path, &read_wf))
   {
      pgmoneta_log_error("Error reading walfile from disk\n");
      goto error;
   }

   // 4. Validate the read data against the original walfile structure
   if (compare_walfile(wf, read_wf))
   {
      pgmoneta_log_error("Walfile data mismatch\n");
      goto error;
   }

   pgmoneta_free_walfile(wf);
   pgmoneta_free_walfile(read_wf);
   free(path);
   return 0;

error:
   if (wf != NULL)
      pgmoneta_free_walfile(wf);
   if (read_wf != NULL)
      pgmoneta_free_walfile(read_wf);
   if (path != NULL)
      free(path);
   return 1;
}

static void
pgmoneta_free_walfile(struct walfile* wf)
{
   if (wf == NULL)
      return;

   free(wf->long_phd);

   struct deque_iterator* iter = NULL;
   if (pgmoneta_deque_iterator_create(wf->page_headers, &iter) == 0)
   {
      while (pgmoneta_deque_iterator_next(iter))
      {
         struct xlog_page_header_data* ph = (struct xlog_page_header_data*)iter->value->data;
         free(ph);
      }
      pgmoneta_deque_iterator_destroy(iter);
   }
   pgmoneta_deque_destroy(wf->page_headers);

   if (pgmoneta_deque_iterator_create(wf->records, &iter) == 0)
   {
      while (pgmoneta_deque_iterator_next(iter))
      {
         struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;
         free(rec->main_data);
         free(rec);
      }
      pgmoneta_deque_iterator_destroy(iter);
   }
   pgmoneta_deque_destroy(wf->records);

   free(wf);
}

struct walfile*
generate_xlog_checkpoint_shutdown()
{
    struct walfile* wf = NULL;
    struct xlog_long_page_header_data* long_phd = NULL;
    struct deque* page_headers = NULL;
    struct deque* records = NULL;
    struct xlog_page_header_data* ph = NULL;
    struct decoded_xlog_record* rec = NULL;

    wf = (struct walfile*)malloc(sizeof(struct walfile));
    if (wf == NULL)
        goto error;

    wf->magic_number = 0xD116;

    long_phd = (struct xlog_long_page_header_data*)malloc(sizeof(struct xlog_long_page_header_data));
    if (long_phd == NULL)
        goto error;
    long_phd->std.xlp_magic = XLOG_PAGE_MAGIC;
    long_phd->std.xlp_info = 0;
    long_phd->std.xlp_tli = 1;
    long_phd->std.xlp_pageaddr = 0;
    long_phd->xlp_seg_size = 16777216;
    long_phd->xlp_xlog_blcksz = 8192;
    wf->long_phd = long_phd;

    if (pgmoneta_deque_create(false, &page_headers))
        goto error;
    wf->page_headers = page_headers;

    ph = (struct xlog_page_header_data*)malloc(sizeof(struct xlog_page_header_data));
    if (ph == NULL)
        goto error;
    ph->xlp_magic = XLOG_PAGE_MAGIC;
    ph->xlp_info = 0;
    ph->xlp_tli = 1;
    ph->xlp_pageaddr = 0;
    if (pgmoneta_deque_add(page_headers, NULL, (uintptr_t)ph, 0))
        goto error;

    if (pgmoneta_deque_create(false, &records))
        goto error;
    wf->records = records;

    rec = (struct decoded_xlog_record*)malloc(sizeof(struct decoded_xlog_record));
    if (rec == NULL)
        goto error;
    rec->header.xl_tot_len = sizeof(struct xlog_record) + sizeof(uint32_t);
    rec->header.xl_xid = 0;
    rec->header.xl_prev = 0;
    rec->header.xl_info = XLOG_CHECKPOINT_SHUTDOWN;
    rec->header.xl_rmid = 0;  // XLOG
    rec->header.xl_crc = 0;
    rec->main_data_len = sizeof(uint32_t);
    rec->main_data = (char*)malloc(rec->main_data_len);
    if (rec->main_data == NULL)
        goto error;
    *(uint32_t*)rec->main_data = 0xDEADBEEF;
    if (pgmoneta_deque_add(records, NULL, (uintptr_t)rec, 0))
        goto error;

    return wf;

error:
    if (wf != NULL) {
        if (long_phd != NULL)
            free(long_phd);
        if (page_headers != NULL) {
            struct deque_iterator* iter = NULL;
            if (pgmoneta_deque_iterator_create(page_headers, &iter) == 0) {
                while (pgmoneta_deque_iterator_next(iter)) {
                    free((void*)iter->value->data);
                }
                pgmoneta_deque_iterator_destroy(iter);
            }
            pgmoneta_deque_destroy(page_headers);
        }
        if (records != NULL) {
            struct deque_iterator* iter = NULL;
            if (pgmoneta_deque_iterator_create(records, &iter) == 0) {
                while (pgmoneta_deque_iterator_next(iter)) {
                    struct decoded_xlog_record* r = (struct decoded_xlog_record*)iter->value->data;
                    free(r->main_data);
                    free(r);
                }
                pgmoneta_deque_iterator_destroy(iter);
            }
            pgmoneta_deque_destroy(records);
        }
        free(wf);
    }
    return NULL;
}