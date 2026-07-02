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

/* pgmoneta */
#include <pgmoneta.h>
#include <logging.h>
#include <mctf_container.h>
#include <tscommon.h>

/* system */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Garage (S3-compatible object store) container definition. */
#define GARAGE_IMAGE         "dxflrs/garage:v2.2.0"
#define GARAGE_TOML          "contrib/garage/garage-ci.toml"
#define GARAGE_READY_CMD     "/garage -c /etc/garage.toml status"
#define GARAGE_READY_RETRIES 30

/* Azurite (Azure Blob Storage emulator) container definition. */
#define AZURITE_IMAGE         "mcr.microsoft.com/azure-storage/azurite"
#define AZURITE_BLOB_PORT     10000
#define AZURITE_READY_RETRIES 30

/* Image-pull retry policy (transient registry failures). */
#define PULL_RETRIES         3
#define PULL_BACKOFF_SECONDS 2

static int start_garage(struct mctf_container* c);
static int start_azurite(struct mctf_container* c);

int
mctf_sh(char** output, const char* fmt, ...)
{
   va_list ap;
   char cmd[4096];
   char* out = NULL;
   int exit_code = MCTF_FAIL;
   int n;

   va_start(ap, fmt);
   n = vsnprintf(cmd, sizeof(cmd), fmt, ap);
   va_end(ap);

   if (n <= 0 || (size_t)n >= sizeof(cmd))
   {
      return MCTF_FAIL;
   }

   if (pgmoneta_test_exec_command(cmd, &out, &exit_code) != 0)
   {
      free(out);
      return MCTF_FAIL;
   }

   if (output != NULL)
   {
      *output = out;
   }
   else
   {
      free(out);
   }

   return exit_code;
}

int
mctf_container_engine(char* out, size_t size)
{
   if (mctf_sh(NULL, "podman info >/dev/null") == 0)
   {
      snprintf(out, size, "podman");
      return MCTF_OK;
   }

   if (mctf_sh(NULL, "docker info >/dev/null") == 0)
   {
      snprintf(out, size, "docker");
      return MCTF_OK;
   }

   return MCTF_SKIPPED;
}

int
mctf_project_root(char* out, size_t size)
{
   char self[MAX_PATH];
   ssize_t len;
   char* slash;
   int i;

   len = readlink("/proc/self/exe", self, sizeof(self) - 1);
   if (len <= 0)
   {
      return MCTF_FAIL;
   }
   self[len] = '\0';

   for (i = 0; i < 3; i++)
   {
      slash = strrchr(self, '/');
      if (slash == NULL)
      {
         return MCTF_FAIL;
      }
      *slash = '\0';
   }

   if (snprintf(out, size, "%s", self) <= 0)
   {
      return MCTF_FAIL;
   }

   return MCTF_OK;
}

int
mctf_container_pull(const char* engine, const char* image, int retries)
{
   int i;

   for (i = 0; i < retries; i++)
   {
      if (mctf_sh(NULL, "%s pull %s", engine, image) == 0)
      {
         return MCTF_OK;
      }
      if (i + 1 < retries)
      {
         sleep(PULL_BACKOFF_SECONDS);
      }
   }

   pgmoneta_log_warn("mctf_container: could not pull %s after %d attempts; "
                     "relying on a cached image",
                     image, retries);
   return MCTF_FAIL;
}

int
mctf_container_run(struct mctf_container* c, const char* name, const char* image,
                   const char* run_args, const char* ready_cmd, int retries)
{
   int i;

   snprintf(c->name, sizeof(c->name), "%s", name);

   /* Pull up front (with retry) so the run below is a local, network-free
    * operation. Best-effort: run auto-pulls too, and a cached image may exist. */
   mctf_container_pull(c->engine, image, PULL_RETRIES);

   /* Remove a stale container with the same name, then run fresh. */
   mctf_sh(NULL, "%s rm -f %s", c->engine, name);

   if (mctf_sh(NULL, "%s run -d --name %s --label %s %s %s",
               c->engine, name, MCTF_CONTAINER_LABEL,
               run_args != NULL ? run_args : "", image) != 0)
   {
      pgmoneta_log_error("mctf_container: failed to start %s", name);
      return MCTF_FAIL;
   }
   c->running = true;

   if (ready_cmd != NULL)
   {
      for (i = 0; i < retries; i++)
      {
         if (mctf_container_exec(c, ready_cmd, NULL) == 0)
         {
            return MCTF_OK;
         }
         sleep(1);
      }
      pgmoneta_log_error("mctf_container: %s not ready after %d s", name, retries);
      return MCTF_FAIL;
   }

   return MCTF_OK;
}

int
mctf_container_exec(struct mctf_container* c, const char* cmd, char** output)
{
   return mctf_sh(output, "%s exec %s %s", c->engine, c->name, cmd);
}

void
mctf_container_stop(struct mctf_container* c)
{
   if (c != NULL && c->engine[0] != '\0' && c->name[0] != '\0')
   {
      mctf_sh(NULL, "%s rm -f %s", c->engine, c->name);
      c->running = false;
   }
}

void
mctf_container_sweep(const char* engine)
{
   if (engine != NULL && engine[0] != '\0')
   {
      mctf_sh(NULL, "%s rm -f $(%s ps -aq --filter label=%s) 2>/dev/null",
              engine, engine, MCTF_CONTAINER_LABEL);
   }
}

int
mctf_container_start(struct mctf_container* c, enum mctf_container_kind kind)
{
   int rc;

   memset(c, 0, sizeof(*c));

   rc = mctf_container_engine(c->engine, sizeof(c->engine));
   if (rc != MCTF_OK)
   {
      return rc; /* MCTF_SKIPPED */
   }

   mctf_container_sweep(c->engine);

   switch (kind)
   {
      case MCTF_CONTAINER_GARAGE:
         return start_garage(c);
      case MCTF_CONTAINER_AZURITE:
         return start_azurite(c);
      default:
         pgmoneta_log_error("mctf_container: unknown kind %d", (int)kind);
         return MCTF_FAIL;
   }
}

static int
start_azurite(struct mctf_container* c)
{
   char name[128];
   int i;

   snprintf(name, sizeof(name), "pgmoneta-mctf-azurite-%d", (int)getpid());
   snprintf(c->name, sizeof(c->name), "%s", name);

   mctf_container_pull(c->engine, AZURITE_IMAGE, PULL_RETRIES);

   /* Remove any stale container with the same name. */
   mctf_sh(NULL, "%s rm -f %s 2>/dev/null", c->engine, name);

   /* Start blob-only with --skipApiVersionCheck (modern SDK API versions). */
   if (mctf_sh(NULL, "%s run -d --name %s --label %s --network host %s "
               "azurite-blob --blobHost 0.0.0.0 --skipApiVersionCheck",
               c->engine, name, MCTF_CONTAINER_LABEL, AZURITE_IMAGE) != 0)
   {
      pgmoneta_log_error("mctf_container: failed to start azurite");
      return MCTF_FAIL;
   }
   c->running = true;

   /* Wait until the blob port accepts TCP connections (checked from the host). */
   for (i = 0; i < AZURITE_READY_RETRIES; i++)
   {
      if (mctf_sh(NULL, "python3 -c \""
                  "import socket, sys; s=socket.socket(); s.settimeout(1); "
                  "s.connect(('127.0.0.1', %d)); s.close()"
                  "\" 2>/dev/null", AZURITE_BLOB_PORT) == 0)
      {
         return MCTF_OK;
      }
      sleep(1);
   }

   pgmoneta_log_error("mctf_container: azurite not ready after %d s", AZURITE_READY_RETRIES);
   return MCTF_FAIL;
}

static int
start_garage(struct mctf_container* c)
{
   char root[MAX_PATH];
   char toml[MAX_PATH];
   char name[128];
   char run_args[2 * MAX_PATH];

   if (mctf_project_root(root, sizeof(root)) != MCTF_OK)
   {
      pgmoneta_log_error("mctf_container: cannot resolve project root");
      return MCTF_FAIL;
   }
   snprintf(toml, sizeof(toml), "%s/%s", root, GARAGE_TOML);

   if (access(toml, R_OK) != 0)
   {
      pgmoneta_log_error("mctf_container: garage config not found: %s", toml);
      return MCTF_FAIL;
   }

   snprintf(name, sizeof(name), "pgmoneta-mctf-garage-%d", (int)getpid());
   snprintf(run_args, sizeof(run_args), "--network host -v %s:/etc/garage.toml:ro", toml);

   /* The image's default command is already ["/garage","server"] and reads
    * /etc/garage.toml; do not append a command (that would override it). */
   return mctf_container_run(c, name, GARAGE_IMAGE, run_args,
                             GARAGE_READY_CMD, GARAGE_READY_RETRIES);
}
