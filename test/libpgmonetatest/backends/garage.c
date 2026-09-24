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
 * Garage S3 backend driver.
 *
 * Garage is an S3-compatible object store; it stands in for AWS S3 so the
 * real se_s3 code path is exercised. The container lifecycle is handled by
 * MCTF_START_CONTAINER; this driver adds the S3-specific provisioning
 * (bucket + access key) and the server configuration lines. The provisioning
 * sequence mirrors the one validated in .github/workflows/storage.yml.
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <logging.h>
#include <mctf_container.h>
#include <mctf_se.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GARAGE_BUCKET   "pgmoneta-test-bucket"
#define GARAGE_KEY_NAME "pgmoneta-app-key"
#define GARAGE_S3_PORT  3900
#define GARAGE_REGION   "garage"
#define GARAGE_CMD      "/garage -c /etc/garage.toml"

/*
 * Copy the last whitespace-separated token of the first line containing
 * @p needle into @p out.
 */
static int
parse_line_last_token(const char* text, const char* needle, char* out, size_t size)
{
   const char* line = text;

   while (line != NULL && *line != '\0')
   {
      const char* eol = strchr(line, '\n');
      size_t line_len = (eol != NULL) ? (size_t)(eol - line) : strlen(line);
      const char* hit = strstr(line, needle);

      if (hit != NULL && (size_t)(hit - line) < line_len)
      {
         char buf[512];
         char* tok;
         char* last = NULL;
         char* save = NULL;

         if (line_len >= sizeof(buf))
         {
            line_len = sizeof(buf) - 1;
         }
         memcpy(buf, line, line_len);
         buf[line_len] = '\0';

         tok = strtok_r(buf, " \t\r", &save);
         while (tok != NULL)
         {
            last = tok;
            tok = strtok_r(NULL, " \t\r", &save);
         }

         if (last != NULL)
         {
            snprintf(out, size, "%s", last);
            return MCTF_OK;
         }
         return MCTF_FAIL;
      }

      line = (eol != NULL) ? eol + 1 : NULL;
   }

   return MCTF_FAIL;
}

/*
 * Find the first line whose first token looks like a Garage node id
 * (a hex run of at least 16 chars).
 */
static int
parse_node_id(const char* status_text, char* out, size_t size)
{
   const char* line = status_text;

   while (line != NULL && *line != '\0')
   {
      const char* eol = strchr(line, '\n');
      size_t line_len = (eol != NULL) ? (size_t)(eol - line) : strlen(line);
      size_t i = 0;

      while (i < line_len && ((line[i] >= '0' && line[i] <= '9') ||
                              (line[i] >= 'a' && line[i] <= 'f')))
      {
         i++;
      }

      if (i >= 16 && (i == line_len || line[i] == ' ' || line[i] == '\t'))
      {
         if (i >= size)
         {
            i = size - 1;
         }
         memcpy(out, line, i);
         out[i] = '\0';
         return MCTF_OK;
      }

      line = (eol != NULL) ? eol + 1 : NULL;
   }

   return MCTF_FAIL;
}

static int
provision(struct mctf_se* s)
{
   struct mctf_container* c = &s->container;
   char cmd[1024];
   char* out = NULL;
   char node_id[256];
   int rc = MCTF_FAIL;

   if (mctf_container_exec(c, GARAGE_CMD " status", &out) != 0)
   {
      goto done;
   }
   if (parse_node_id(out, node_id, sizeof(node_id)) != MCTF_OK)
   {
      pgmoneta_log_error("garage: could not parse node id");
      goto done;
   }
   free(out);
   out = NULL;

   snprintf(cmd, sizeof(cmd), GARAGE_CMD " layout assign -z dc1 -c 1G %s", node_id);
   if (mctf_container_exec(c, cmd, NULL) != 0)
   {
      goto done;
   }
   if (mctf_container_exec(c, GARAGE_CMD " layout apply --version 1", NULL) != 0)
   {
      goto done;
   }

   /* create may already exist on a retry; tolerate and verify via key info. */
   mctf_container_exec(c, GARAGE_CMD " bucket create " GARAGE_BUCKET, NULL);
   mctf_container_exec(c, GARAGE_CMD " key create " GARAGE_KEY_NAME, NULL);

   if (mctf_container_exec(c, GARAGE_CMD " bucket allow --read --write --owner " GARAGE_BUCKET " --key " GARAGE_KEY_NAME, NULL) != 0)
   {
      goto done;
   }

   if (mctf_container_exec(c, GARAGE_CMD " key info " GARAGE_KEY_NAME " --show-secret", &out) != 0)
   {
      goto done;
   }

   if (parse_line_last_token(out, "Key ID", s->access_key, sizeof(s->access_key)) != MCTF_OK ||
       parse_line_last_token(out, "Secret key", s->secret_key, sizeof(s->secret_key)) != MCTF_OK)
   {
      pgmoneta_log_error("garage: could not parse credentials");
      goto done;
   }

   rc = MCTF_OK;

done:
   free(out);
   return rc;
}

static int
garage_start(struct mctf_se* s)
{
   int rc;

   rc = MCTF_START_CONTAINER(&s->container, MCTF_CONTAINER_GARAGE);
   if (rc != MCTF_OK)
   {
      return rc; /* MCTF_SKIPPED bubbles up to mctf_se_up */
   }
   fprintf(stderr, "    - container started\n");
   fflush(stderr);

   if (provision(s) != MCTF_OK)
   {
      return MCTF_FAIL;
   }
   fprintf(stderr, "    - bucket provisioned\n");
   fflush(stderr);

   snprintf(s->endpoint, sizeof(s->endpoint), "localhost");
   s->port = GARAGE_S3_PORT;
   s->use_tls = false;
   snprintf(s->region, sizeof(s->region), "%s", GARAGE_REGION);
   snprintf(s->bucket, sizeof(s->bucket), "%s", GARAGE_BUCKET);

   return MCTF_OK;
}

static void
garage_stop(struct mctf_se* s)
{
   MCTF_STOP_CONTAINER(&s->container);
}

static int
garage_write_server_conf(struct mctf_se* s, FILE* f)
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

const struct mctf_se_driver mctf_garage_driver = {
   .name = "garage",
   .storage_engine = "s3",
   .start = garage_start,
   .stop = garage_stop,
   .write_server_conf = garage_write_server_conf,
};
