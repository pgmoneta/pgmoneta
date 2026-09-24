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
 * Azurite backend driver.
 *
 * Azurite is a local Azure Blob Storage emulator; it stands in for Azure so
 * the real se_azure code path is exercised. The container lifecycle is handled
 * by MCTF_START_CONTAINER; this driver adds the provisioning step (create the
 * blob container) and the server configuration lines.
 *
 * Azurite well-known credentials (public, for local development only):
 *   Account : devstoreaccount1
 *   Key     : Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/
 *             K1SZFPTOtr/KBHBeksoGMGw==
 *
 * Container creation uses the SharedKey REST API via a Python3 stdlib script
 * (no external packages). Python runs as a subprocess so it is isolated from
 * the test process's pgmoneta context, which is not yet initialized at
 * provisioning time (pgmoneta daemon starts after provision completes).
 */

#include <pgmoneta.h>
#include <logging.h>
#include <mctf_container.h>
#include <mctf_se.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AZURITE_ACCOUNT   "devstoreaccount1"
#define AZURITE_KEY       "Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw=="
#define AZURITE_CONTAINER "pgmoneta-test-container"
#define AZURITE_BLOB_PORT 10000

/*
 * Create the blob container in Azurite via Python3 stdlib (no external packages).
 *
 * Python runs as a subprocess so it is fully isolated from the test process's
 * pgmoneta context. provision() is called before the managed pgmoneta daemon
 * starts, so pgmoneta's shmem and HTTP stack are not yet usable here.
 *
 * Azurite quirks vs. production Azure SharedKey:
 *   1. Content-Length in the canonical string must be '' (empty), not '0',
 *      even for a PUT with an empty body.
 *   2. Canonical resource uses the account name twice for path-style URLs:
 *      /{account}/{account}/{container}\nrestype:container
 */
static int
provision(struct mctf_se* s)
{
   char script_path[256];
   FILE* f;
   int rc;

   (void)s;

   snprintf(script_path, sizeof(script_path), "/tmp/mctf-azurite-provision-%d.py", (int)getpid());
   f = fopen(script_path, "w");
   if (f == NULL)
   {
      pgmoneta_log_error("azurite: cannot write provisioning script");
      return MCTF_FAIL;
   }

   fprintf(f,
      "import base64, hashlib, hmac, datetime, http.client, sys\n"
      "\n"
      "ACCOUNT   = '%s'\n"
      "KEY_B64   = '%s'\n"
      "CONTAINER = '%s'\n"
      "PORT      = %d\n"
      "VERSION   = '2020-08-04'\n"
      "\n"
      "now = datetime.datetime.utcnow().strftime('%%a, %%d %%b %%Y %%H:%%M:%%S GMT')\n"
      "\n"
      "string_to_sign = (\n"
      "    'PUT\\n'\n"
      "    '\\n'   # Content-Encoding\n"
      "    '\\n'   # Content-Language\n"
      "    '\\n'   # Content-Length (empty, not '0' — Azurite quirk)\n"
      "    '\\n'   # Content-MD5\n"
      "    '\\n'   # Content-Type\n"
      "    '\\n'   # Date (empty; using x-ms-date)\n"
      "    '\\n'   # If-Modified-Since\n"
      "    '\\n'   # If-Match\n"
      "    '\\n'   # If-None-Match\n"
      "    '\\n'   # If-Unmodified-Since\n"
      "    '\\n'   # Range\n"
      "    f'x-ms-date:{now}\\n'\n"
      "    f'x-ms-version:{VERSION}\\n'\n"
      "    f'/{ACCOUNT}/{ACCOUNT}/{CONTAINER}\\nrestype:container'\n"
      ")\n"
      "\n"
      "key = base64.b64decode(KEY_B64)\n"
      "sig = base64.b64encode(\n"
      "    hmac.new(key, string_to_sign.encode('utf-8'), hashlib.sha256).digest()\n"
      ").decode()\n"
      "\n"
      "conn = http.client.HTTPConnection('127.0.0.1', PORT)\n"
      "conn.request('PUT', f'/{ACCOUNT}/{CONTAINER}?restype=container', body=b'', headers={\n"
      "    'x-ms-date':     now,\n"
      "    'x-ms-version':  VERSION,\n"
      "    'Content-Length': '0',\n"
      "    'Authorization': f'SharedKey {ACCOUNT}:{sig}',\n"
      "})\n"
      "resp = conn.getresponse()\n"
      "body = resp.read().decode(errors='replace')\n"
      "conn.close()\n"
      "if resp.status == 201:\n"
      "    print(f'container {CONTAINER!r} created')\n"
      "elif resp.status == 409:\n"
      "    print(f'container {CONTAINER!r} already exists')\n"
      "else:\n"
      "    print(f'HTTP {resp.status}: {body}', file=sys.stderr)\n"
      "    sys.exit(1)\n",
      AZURITE_ACCOUNT, AZURITE_KEY, AZURITE_CONTAINER, AZURITE_BLOB_PORT);

   fclose(f);

   rc = mctf_sh(NULL, "python3 %s", script_path);
   unlink(script_path);

   return (rc == 0) ? MCTF_OK : MCTF_FAIL;
}

static int
azurite_start(struct mctf_se* s)
{
   int rc;

   rc = MCTF_START_CONTAINER(&s->container, MCTF_CONTAINER_AZURITE);
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
   fprintf(stderr, "    - blob container provisioned\n");
   fflush(stderr);

   snprintf(s->endpoint,   sizeof(s->endpoint),   "127.0.0.1");
   snprintf(s->access_key, sizeof(s->access_key),  "%s", AZURITE_ACCOUNT);
   snprintf(s->secret_key, sizeof(s->secret_key),  "%s", AZURITE_KEY);
   snprintf(s->bucket,     sizeof(s->bucket),      "%s", AZURITE_CONTAINER);
   s->port    = AZURITE_BLOB_PORT;
   s->use_tls = false;

   return MCTF_OK;
}

static void
azurite_stop(struct mctf_se* s)
{
   MCTF_STOP_CONTAINER(&s->container);
}

static int
azurite_write_global_conf(struct mctf_se* s, FILE* f)
{
   /* Azure settings are global (main_configuration), not per-server. */
   fprintf(f, "azure_storage_account = %s\n", s->access_key);
   fprintf(f, "azure_shared_key = %s\n",      s->secret_key);
   fprintf(f, "azure_container = %s\n",        s->bucket);
   fprintf(f, "azure_base_dir = pgmoneta\n");
   fprintf(f, "azure_endpoint = %s\n",         s->endpoint);
   fprintf(f, "azure_port = %d\n",             s->port);
   fprintf(f, "azure_use_tls = %s\n",          s->use_tls ? "on" : "off");
   return MCTF_OK;
}

const struct mctf_se_driver mctf_azurite_driver = {
   .name              = "azurite",
   .storage_engine    = "azure",
   .start             = azurite_start,
   .stop              = azurite_stop,
   .write_global_conf = azurite_write_global_conf,
   .write_server_conf = NULL,
};
