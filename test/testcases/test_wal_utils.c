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
#include <logging.h>
#include <tsclient.h>
#include <tscommon.h>
#include <tssuite.h>
#include <tswalutils.h>
#include <utils.h>
#include <value.h>
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

static void test_walfile(struct walfile* (*generate)(void));
static void compare_walfile(struct walfile* wf1, struct walfile* wf2);
static bool compare_long_page_headers(struct xlog_long_page_header_data* h1, struct xlog_long_page_header_data* h2);
static void compare_deque(struct deque* dq1, struct deque* dq2, void (*compare)(void*, void*));
static bool compare_xlog_page_header(void* a, void* b);
static void compare_xlog_record(void* a, void* b);
static void destroy_walfile(struct walfile* wf);

START_TEST(test_check_point_shutdown_v17)
{
   fprintf(stderr, "TEST START: %s\n", __func__);
   test_walfile(pgmoneta_test_generate_check_point_shutdown_v17);
}
END_TEST

Suite*
pgmoneta_test_wal_utils_suite()
{
   Suite* s;
   TCase* tc_wal_utils;
   s = suite_create("pgmoneta_test_wal_utils");
   tc_wal_utils = tcase_create("test_wal_utils");
   tcase_add_checked_fixture(tc_wal_utils, pgmoneta_test_setup, pgmoneta_test_basedir_cleanup);
   tcase_set_timeout(tc_wal_utils, 60);
   tcase_add_test(tc_wal_utils, test_check_point_shutdown_v17);
   suite_add_tcase(s, tc_wal_utils);

   return s;
}

static void
test_walfile(struct walfile* (*generate)(void))
{
   struct walfile* wf = NULL;
   struct walfile* read_wf = NULL;
   char* path = NULL;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   path = pgmoneta_append(path, TEST_BASE_DIR);
   path = pgmoneta_append(path, "/walfiles");

   if (access(path, F_OK) != 0)
   {
      ck_assert_msg(mkdir(path, 0700) == 0, "failed to create walfiles directory");
   }

   path = pgmoneta_append(path, RANDOM_WALFILE_NAME);
   ck_assert_ptr_nonnull(path);

   // 1. Prepare walfile structure
   wf = generate();
   ck_assert_ptr_nonnull(wf);

   // 2. Write this structure to disk
   ck_assert_msg(!pgmoneta_write_walfile(wf, 0, path), "failed to write walfile to disk");

   partial_record = malloc(sizeof(struct partial_xlog_record));
   partial_record->data_buffer_bytes_read = 0;
   partial_record->xlog_record_bytes_read = 0;
   partial_record->xlog_record = NULL;
   partial_record->data_buffer = NULL;

   // 3. Read the walfile from disk
   ck_assert_msg(!pgmoneta_read_walfile(0, path, &read_wf), "failed to read walfile from disk");

   // 4. Validate the read data against the original walfile structure
   compare_walfile(wf, read_wf);

   destroy_walfile(wf);
   destroy_walfile(read_wf);
   if (partial_record != NULL)
   {
      if (partial_record->xlog_record != NULL)
      {
         free(partial_record->xlog_record);
      }
      if (partial_record->data_buffer != NULL)
      {
         free(partial_record->data_buffer);
      }
      free(partial_record);
      partial_record = NULL;
   }
   free(path);
}

static void
compare_walfile(struct walfile* wf1, struct walfile* wf2)
{
   if (wf1 == NULL || wf2 == NULL)
   {
      ck_assert_msg(wf1 == wf2, "one of the walfile is NULL");
   }

   ck_assert_uint_eq(wf1->long_phd->std.xlp_magic, wf2->long_phd->std.xlp_magic);

   ck_assert(compare_long_page_headers(wf1->long_phd, wf2->long_phd));

   compare_deque(wf1->records, wf2->records, compare_xlog_record);
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

static void
compare_deque(struct deque* dq1, struct deque* dq2, void (*compare)(void*, void*))
{
   if (dq1 == NULL || dq2 == NULL)
   {
      ck_assert_msg(dq1 == dq2, "one of the deque is NULL");
   }

   ck_assert_uint_eq(pgmoneta_deque_size(dq1), pgmoneta_deque_size(dq2));

   struct deque_iterator* iter1 = NULL;
   struct deque_iterator* iter2 = NULL;
   bool equal = true;

   ck_assert(pgmoneta_deque_iterator_create(dq1, &iter1) == 0 &&
             pgmoneta_deque_iterator_create(dq2, &iter2) == 0);

   while (pgmoneta_deque_iterator_next(iter1) && pgmoneta_deque_iterator_next(iter2))
   {
      uintptr_t data1 = iter1->value->data;
      uintptr_t data2 = iter2->value->data;

      compare((void*)data1, (void*)data2);
   }

   ck_assert((!pgmoneta_deque_iterator_next(iter1)) && (!pgmoneta_deque_iterator_next(iter2)));

   pgmoneta_deque_iterator_destroy(iter1);
   pgmoneta_deque_iterator_destroy(iter2);
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

static void
compare_xlog_record(void* a, void* b)
{
   struct decoded_xlog_record* rec1 = (struct decoded_xlog_record*)a;
   struct decoded_xlog_record* rec2 = (struct decoded_xlog_record*)b;

   ck_assert_int_eq(rec1->oversized, rec2->oversized);

   ck_assert_mem_eq(&rec1->header, &rec2->header, sizeof(struct xlog_record));

   ck_assert_uint_eq(rec1->record_origin, rec2->record_origin);

   ck_assert_uint_eq(rec1->toplevel_xid, rec2->toplevel_xid);

   ck_assert_uint_eq(rec1->main_data_len, rec2->main_data_len);

   ck_assert_msg(rec1->main_data_len == 0 || memcmp(rec1->main_data, rec2->main_data, rec1->main_data_len) == 0, "xlog_record main_data mismatch");

   ck_assert_int_eq(rec1->max_block_id, rec2->max_block_id);

   for (int i = 0; i <= rec1->max_block_id && i < XLR_MAX_BLOCK_ID + 1; i++)
   {
      ck_assert_msg(rec1->blocks[i].in_use == rec2->blocks[i].in_use, "xlog_record blocks[%d] in_use mismatch", i);

      if (rec1->blocks[i].in_use)
      {
         ck_assert_msg(rec1->blocks[i].bimg_len == rec2->blocks[i].bimg_len, "xlog_record blocks[%d] bimg_len mismatch\n", i);

         ck_assert_msg(rec1->blocks[i].bimg_len == 0 ||
                          memcmp(rec1->blocks[i].bkp_image, rec2->blocks[i].bkp_image, rec1->blocks[i].bimg_len) == 0,
                       "xlog_record blocks[%d] bkp_image mismatch\n", i);

         ck_assert_msg(rec1->blocks[i].data_len == rec2->blocks[i].data_len, "xlog_record blocks[%d] data_len mismatch", i);

         ck_assert_msg(rec1->blocks[i].data_len == 0 ||
                          memcmp(rec1->blocks[i].data, rec2->blocks[i].data, rec1->blocks[i].data_len) == 0,
                       "xlog_record blocks[%d] data mismatch\n", i);
      }
   }

   ck_assert_int_eq(rec1->partial, rec2->partial);
}

static void
destroy_walfile(struct walfile* wf)
{
   if (wf == NULL)
   {
      return;
   }

   free(wf->long_phd);

   struct deque_iterator* iter = NULL;
   if (wf->page_headers != NULL)
   {
      if (pgmoneta_deque_iterator_create(wf->page_headers, &iter) == 0)
      {
         while (pgmoneta_deque_iterator_next(iter))
         {
            if (iter->value != NULL && iter->value->data != 0)
            {
               struct xlog_page_header_data* ph = (struct xlog_page_header_data*)iter->value->data;
               free(ph);
            }
         }
         pgmoneta_deque_iterator_destroy(iter);
      }
      pgmoneta_deque_destroy(wf->page_headers);
   }

   iter = NULL;
   if (wf->records != NULL)
   {
      if (pgmoneta_deque_iterator_create(wf->records, &iter) == 0)
      {
         while (pgmoneta_deque_iterator_next(iter))
         {
            if (iter->value != NULL && iter->value->data != 0)
            {
               struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;

               if (rec->main_data != NULL)
               {
                  free(rec->main_data);
               }

               for (int i = 0; i <= rec->max_block_id && i < XLR_MAX_BLOCK_ID + 1; i++)
               {
                  if (rec->blocks[i].in_use)
                  {
                     if (rec->blocks[i].bkp_image != NULL)
                     {
                        free(rec->blocks[i].bkp_image);
                     }

                     if (rec->blocks[i].data != NULL)
                     {
                        free(rec->blocks[i].data);
                     }
                  }
               }

               free(rec);
            }
         }
         pgmoneta_deque_iterator_destroy(iter);
      }
      pgmoneta_deque_destroy(wf->records);
   }

   free(wf);
}