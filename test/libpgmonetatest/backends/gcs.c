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
 * fake-gcs-server backend driver.
 *
 * fake-gcs-server (https://github.com/fsouza/fake-gcs-server) is a local
 * Google Cloud Storage emulator; it stands in for GCS so the real se_gcs
 * code path is exercised. The container lifecycle is handled by
 * MCTF_START_CONTAINER; this driver adds the provisioning step (create the
 * bucket) and the server configuration lines.
 *
 * fake-gcs-server does not validate the Authorization header content by
 * default -- the same convention the official GCS client libraries use when
 * STORAGE_EMULATOR_HOST is set. Accordingly no gcs_credentials_file is
 * configured here, exercising se_gcs.c's unauthenticated code path; the real
 * service-account JWT flow is only reachable against production GCS.
 */

#include <pgmoneta.h>
#include <logging.h>
#include <mctf_container.h>
#include <mctf_se.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FAKE_GCS_BUCKET "pgmoneta-test-bucket"
#define FAKE_GCS_PORT   4443

static int
provision(struct mctf_se* s)
{
   char script_path[256];
   FILE* f;
   int rc;

   (void)s;

   snprintf(script_path, sizeof(script_path), "/tmp/mctf-fake-gcs-provision-%d.py", (int)getpid());
   f = fopen(script_path, "w");
   if (f == NULL)
   {
      pgmoneta_log_error("fake-gcs: cannot write provisioning script");
      return MCTF_FAIL;
   }

   fprintf(f,
      "import json, urllib.error, urllib.request, sys\n"
      "\n"
      "BUCKET = '%s'\n"
      "PORT   = %d\n"
      "\n"
      "req = urllib.request.Request(\n"
      "    f'http://127.0.0.1:{PORT}/storage/v1/b',\n"
      "    data=json.dumps({'name': BUCKET}).encode(),\n"
      "    headers={'Content-Type': 'application/json'},\n"
      "    method='POST',\n"
      ")\n"
      "try:\n"
      "    urllib.request.urlopen(req, timeout=5)\n"
      "    print(f'bucket {BUCKET!r} created')\n"
      "except urllib.error.HTTPError as e:\n"
      "    if e.code == 409:\n"
      "        print(f'bucket {BUCKET!r} already exists')\n"
      "    else:\n"
      "        print(f'HTTP {e.code}: {e.read().decode(errors=\"replace\")}', file=sys.stderr)\n"
      "        sys.exit(1)\n",
      FAKE_GCS_BUCKET, FAKE_GCS_PORT);

   fclose(f);

   rc = mctf_sh(NULL, "python3 %s", script_path);
   unlink(script_path);

   return (rc == 0) ? MCTF_OK : MCTF_FAIL;
}

static int
fake_gcs_start(struct mctf_se* s)
{
   int rc;

   rc = MCTF_START_CONTAINER(&s->container, MCTF_CONTAINER_FAKE_GCS);
   if (rc != MCTF_OK)
   {
      return rc;
   }
   fprintf(stderr, "    - container started\n");
   fflush(stderr);

   if (provision(s) != MCTF_OK)
   {
      return MCTF_FAIL;
   }
   fprintf(stderr, "    - bucket provisioned\n");
   fflush(stderr);

   snprintf(s->endpoint, sizeof(s->endpoint), "127.0.0.1");
   snprintf(s->bucket,   sizeof(s->bucket),   "%s", FAKE_GCS_BUCKET);
   s->port    = FAKE_GCS_PORT;
   s->use_tls = false;

   return MCTF_OK;
}

static void
fake_gcs_stop(struct mctf_se* s)
{
   MCTF_STOP_CONTAINER(&s->container);
}

static int
fake_gcs_write_global_conf(struct mctf_se* s, FILE* f)
{
   /* GCS settings are global (main_configuration), not per-server. No
    * gcs_credentials_file: the emulator is unauthenticated (see header). */
   fprintf(f, "gcs_bucket = %s\n",    s->bucket);
   fprintf(f, "gcs_base_dir = pgmoneta\n");
   fprintf(f, "gcs_endpoint = %s\n", s->endpoint);
   fprintf(f, "gcs_port = %d\n",     s->port);
   fprintf(f, "gcs_use_tls = %s\n",  s->use_tls ? "on" : "off");
   return MCTF_OK;
}

const struct mctf_se_driver mctf_fake_gcs_driver = {
   .name              = "fake-gcs",
   .storage_engine    = "gcs",
   .start             = fake_gcs_start,
   .stop              = fake_gcs_stop,
   .write_global_conf = fake_gcs_write_global_conf,
   .write_server_conf = NULL,
};
