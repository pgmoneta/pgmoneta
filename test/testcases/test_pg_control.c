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

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * control_file_data is a union tagged by version, and the accessor picks the
 * member matching the version. Note what this test can and cannot show: as the
 * structures stand, these three fields sit at the same offset in every variant
 * (see test_pg_control_layouts_still_coincide), so reading the wrong member
 * would still give the right answer today. This pins the values the accessor
 * returns; the test below is the one that notices if that stops being true.
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
 * version, but today every variant puts these three fields in the same place,
 * which is why a wrong member still reads correctly. That is worth knowing
 * rather than assuming: if a future version moves them, the dispatch stops
 * being cosmetic and starts being what keeps verify from reading a block size
 * out of the wrong field, so this fails to say so.
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

cleanup:
   MCTF_FINISH();
}
