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

#include <pgmoneta.h>
#include <mctf.h>
#include <walfile/pg_control.h>
#include <walfile/rm_xlog.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * control_file_data is a union tagged by version, and the accessor picks the
 * member matching the version. Note what this test can and cannot show: as the
 * structures stand, these three fields sit at the same offset through v18.
 * PostgreSQL 19 moves them with its larger checkpoint copy, so the accessor
 * must select the matching union member.
 */
MCTF_TEST(test_pg_control_page_layout_per_version)
{
   struct control_file_data cd;
   uint32_t blcksz = 0;
   uint32_t relseg_size = 0;
   uint32_t checksum_version = 0;

   /* The versions sharing the v13 member. */
   enum control_file_version shared[] = {
      CONTROL_FILE_V13, CONTROL_FILE_V14, CONTROL_FILE_V15, CONTROL_FILE_V16};

   for (size_t i = 0; i < sizeof(shared) / sizeof(shared[0]); i++)
   {
      memset(&cd, 0, sizeof(cd));
      cd.version = shared[i];
      cd.data.v13.blcksz = 8192;
      cd.data.v13.relseg_size = 131072;
      cd.data.v13.data_checksum_version = 1;

      pgmoneta_control_data_page_layout(&cd, &blcksz, &relseg_size, &checksum_version);

      MCTF_ASSERT_INT_EQ(blcksz, 8192, cleanup, "wrong block size for a v13 layout");
      MCTF_ASSERT_INT_EQ(relseg_size, 131072, cleanup, "wrong segment size for a v13 layout");
      MCTF_ASSERT_INT_EQ(checksum_version, 1, cleanup, "wrong checksum version for a v13 layout");
   }

   memset(&cd, 0, sizeof(cd));
   cd.version = CONTROL_FILE_V17;
   cd.data.v17.blcksz = 16384;
   cd.data.v17.relseg_size = 65536;
   cd.data.v17.data_checksum_version = 1;

   pgmoneta_control_data_page_layout(&cd, &blcksz, &relseg_size, &checksum_version);

   MCTF_ASSERT_INT_EQ(blcksz, 16384, cleanup, "wrong block size for v17");
   MCTF_ASSERT_INT_EQ(relseg_size, 65536, cleanup, "wrong segment size for v17");
   MCTF_ASSERT_INT_EQ(checksum_version, 1, cleanup, "wrong checksum version for v17");

   memset(&cd, 0, sizeof(cd));
   cd.version = CONTROL_FILE_V18;
   cd.data.v18.blcksz = 8192;
   cd.data.v18.relseg_size = 131072;
   cd.data.v18.data_checksum_version = 1;

   pgmoneta_control_data_page_layout(&cd, &blcksz, &relseg_size, &checksum_version);

   MCTF_ASSERT_INT_EQ(blcksz, 8192, cleanup, "wrong block size for v18");
   MCTF_ASSERT_INT_EQ(relseg_size, 131072, cleanup, "wrong segment size for v18");
   MCTF_ASSERT_INT_EQ(checksum_version, 1, cleanup, "wrong checksum version for v18");

   memset(&cd, 0, sizeof(cd));
   cd.version = CONTROL_FILE_V19;
   cd.data.v19.blcksz = 32768;
   cd.data.v19.relseg_size = 32768;
   cd.data.v19.data_checksum_version = 1;

   pgmoneta_control_data_page_layout(&cd, &blcksz, &relseg_size, &checksum_version);

   MCTF_ASSERT_INT_EQ(blcksz, 32768, cleanup, "wrong block size for v19");
   MCTF_ASSERT_INT_EQ(relseg_size, 32768, cleanup, "wrong segment size for v19");
   MCTF_ASSERT_INT_EQ(checksum_version, 1, cleanup, "wrong checksum version for v19");

cleanup:
   MCTF_FINISH();
}

/* A cluster built without data checksums reports zero, which is what tells
 * verify there is nothing to check rather than that everything is broken. */
MCTF_TEST(test_pg_control_page_layout_checksums_off)
{
   struct control_file_data cd;
   uint32_t checksum_version = 1;

   memset(&cd, 0, sizeof(cd));
   cd.version = CONTROL_FILE_V17;
   cd.data.v17.blcksz = 8192;
   cd.data.v17.relseg_size = 131072;
   cd.data.v17.data_checksum_version = 0;

   pgmoneta_control_data_page_layout(&cd, NULL, NULL, &checksum_version);

   MCTF_ASSERT_INT_EQ(checksum_version, 0, cleanup, "checksums off was not reported as off");

cleanup:
   MCTF_FINISH();
}

/* Callers ask for only what they need. */
MCTF_TEST(test_pg_control_page_layout_accepts_null)
{
   struct control_file_data cd;
   uint32_t blcksz = 0;

   memset(&cd, 0, sizeof(cd));
   cd.version = CONTROL_FILE_V13;
   cd.data.v13.blcksz = 8192;

   pgmoneta_control_data_page_layout(&cd, &blcksz, NULL, NULL);
   MCTF_ASSERT_INT_EQ(blcksz, 8192, cleanup, "wrong block size when the rest were not wanted");

   pgmoneta_control_data_page_layout(NULL, &blcksz, NULL, NULL);
   MCTF_ASSERT_INT_EQ(blcksz, 0, cleanup, "no control data did not give zero");

cleanup:
   MCTF_FINISH();
}

/*
 * The accessor dispatches on version because the union has a member per
 * version. Versions 13 through 18 happen to put the geometry fields in the
 * same place. PostgreSQL 19's enlarged checkpoint copy moves them, so this
 * test ensures the version dispatch is materially required for beta4.
 */
MCTF_TEST(test_pg_control_layouts_still_coincide)
{
   MCTF_ASSERT_INT_EQ(offsetof(struct control_file_data_v13, blcksz),
                      offsetof(struct control_file_data_v17, blcksz), cleanup,
                      "blcksz moved between v13 and v17");
   MCTF_ASSERT_INT_EQ(offsetof(struct control_file_data_v13, blcksz),
                      offsetof(struct control_file_data_v18, blcksz), cleanup,
                      "blcksz moved between v13 and v18");

   MCTF_ASSERT_INT_EQ(offsetof(struct control_file_data_v13, relseg_size),
                      offsetof(struct control_file_data_v17, relseg_size), cleanup,
                      "relseg_size moved between v13 and v17");
   MCTF_ASSERT_INT_EQ(offsetof(struct control_file_data_v13, relseg_size),
                      offsetof(struct control_file_data_v18, relseg_size), cleanup,
                      "relseg_size moved between v13 and v18");

   MCTF_ASSERT_INT_EQ(offsetof(struct control_file_data_v13, data_checksum_version),
                      offsetof(struct control_file_data_v17, data_checksum_version), cleanup,
                      "data_checksum_version moved between v13 and v17");
   MCTF_ASSERT_INT_EQ(offsetof(struct control_file_data_v13, data_checksum_version),
                      offsetof(struct control_file_data_v18, data_checksum_version), cleanup,
                      "data_checksum_version moved between v13 and v18");

   MCTF_ASSERT(offsetof(struct control_file_data_v13, blcksz) !=
                  offsetof(struct control_file_data_v19, blcksz),
               cleanup,
               "blcksz should move in the v19 beta4 layout");
   MCTF_ASSERT(offsetof(struct control_file_data_v13, data_checksum_version) !=
                  offsetof(struct control_file_data_v19, data_checksum_version),
               cleanup,
               "data_checksum_version should move in the v19 beta4 layout");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_pg19_beta4_wal_layout)
{
   MCTF_ASSERT_INT_EQ(WAL_MAGIC_V19, 0xD122, cleanup,
                      "PostgreSQL 19 beta4 WAL magic is incorrect");
   MCTF_ASSERT_INT_EQ(sizeof(struct check_point_v19), 96, cleanup,
                      "PostgreSQL 19 beta4 checkpoint layout has the wrong size");
   MCTF_ASSERT_INT_EQ(offsetof(struct check_point_v19, data_checksum_state), 92, cleanup,
                      "PostgreSQL 19 beta4 checkpoint checksum-state offset is incorrect");
   MCTF_ASSERT_INT_EQ(offsetof(struct control_file_data_v19, crc), 308, cleanup,
                      "PostgreSQL 19 beta4 pg_control CRC offset is incorrect");

cleanup:
   MCTF_FINISH();
}

MCTF_TEST(test_pg19_beta4_checkpoint_redo_description)
{
   struct decoded_xlog_record record;
   struct xl_checkpoint_redo_v19 redo = {.wal_level = WAL_LEVEL_REPLICA};
   char* description = NULL;

   memset(&record, 0, sizeof(record));
   record.header.xl_info = XLOG_CHECKPOINT_REDO;
   record.main_data = (char*)&redo;
   record.main_data_len = sizeof(redo);

   description = pgmoneta_wal_xlog_desc(NULL, &record);
   MCTF_ASSERT(description != NULL, cleanup, "PostgreSQL 19 beta4 checkpoint redo was not described");
   MCTF_ASSERT(strstr(description, "wal_level replica") != NULL, cleanup,
               "PostgreSQL 19 beta4 checkpoint redo has the wrong payload or opcode");

cleanup:
   free(description);
   MCTF_FINISH();
}
