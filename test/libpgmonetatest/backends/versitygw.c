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
 *
 */

/*
 * versitygw S3 backend driver.
 *
 * versitygw is an S3 gateway over a local directory; it stands in for AWS S3
 * so the real se_s3 code path is exercised. It enforces S3 the way AWS does,
 * including Object Lock and the required checksum headers. The container is
 * started by MCTF_START_CONTAINER with fixed root credentials and the test
 * bucket already in place, so the driver only fills in the connection details.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <mctf_container.h>
#include <mctf_se.h>

/* system */
#include <stdio.h>

static int
versitygw_start(struct mctf_se* s)
{
   int rc;

   rc = MCTF_START_CONTAINER(&s->container, MCTF_CONTAINER_VERSITYGW);
   if (rc != MCTF_OK)
   {
      return rc; /* MCTF_SKIPPED bubbles up to mctf_se_up */
   }
   fprintf(stderr, "    - container started\n");
   fflush(stderr);

   snprintf(s->endpoint, sizeof(s->endpoint), "localhost");
   s->port = MCTF_VERSITYGW_PORT;
   s->use_tls = false;
   snprintf(s->region, sizeof(s->region), "%s", MCTF_VERSITYGW_REGION);
   snprintf(s->bucket, sizeof(s->bucket), "%s", MCTF_VERSITYGW_BUCKET);
   snprintf(s->access_key, sizeof(s->access_key), "%s", MCTF_VERSITYGW_ACCESS_KEY);
   snprintf(s->secret_key, sizeof(s->secret_key), "%s", MCTF_VERSITYGW_SECRET_KEY);

   return MCTF_OK;
}

static void
versitygw_stop(struct mctf_se* s)
{
   MCTF_STOP_CONTAINER(&s->container);
}

static int
versitygw_write_server_conf(struct mctf_se* s, FILE* f)
{
   fprintf(f, "s3_endpoint = %s\n", s->endpoint);
   fprintf(f, "s3_port = %d\n", s->port);
   fprintf(f, "s3_region = %s\n", s->region);
   fprintf(f, "s3_use_tls = %s\n", s->use_tls ? "on" : "off");
   fprintf(f, "s3_bucket = %s\n", s->bucket);
   fprintf(f, "s3_access_key_id = %s\n", s->access_key);
   fprintf(f, "s3_secret_access_key = %s\n", s->secret_key);
   return MCTF_OK;
}

const struct mctf_se_driver mctf_versitygw_driver = {
   .name = "versitygw",
   .storage_engine = "s3",
   .start = versitygw_start,
   .stop = versitygw_stop,
   .write_server_conf = versitygw_write_server_conf,
};
