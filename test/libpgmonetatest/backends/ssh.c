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
 * SSH backend driver.
 *
 * Uses atmoz/sftp as the remote SSH/SFTP target. An Ed25519 key pair is
 * generated at test startup and the public key is injected into the
 * container via a volume mount (atmoz/sftp reads *.pub files from
 * ~/.ssh/keys/).
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <logging.h>
#include <mctf_container.h>
#include <mctf_se.h>
#include <utils.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SSH_HOST     "127.0.0.1"
#define SSH_PORT     2222
#define SSH_USER     "pgmoneta"
/* atmoz/sftp chroots to the user's home dir, so /upload here is /home/pgmoneta/upload in the container. */
#define SSH_BASE_DIR "/upload"

/* Paths derived from getpid() — same convention as mctf_container.c:start_sftp(). */
static char s_privkey_path[MAX_PATH];
static char s_pubkey_path[MAX_PATH];

/*
 * The container gets a new host key every run, so a stale known_hosts entry
 * would make se_ssh.c see SSH_KNOWN_HOSTS_CHANGED (hard error) instead of
 * SSH_KNOWN_HOSTS_UNKNOWN (auto-accepted). Clear it first.
 */
static int
provision(void)
{
   mctf_sh(NULL, "ssh-keygen -R \"[" SSH_HOST "]:%d\" 2>/dev/null", SSH_PORT);

   fprintf(stderr, "    - known_hosts cleaned\n");
   fflush(stderr);

   return MCTF_OK;
}

static int
ssh_start(struct mctf_se* s)
{
   int rc;

   /* Must exist before MCTF_START_CONTAINER mounts the public key. */
   pgmoneta_snprintf(s_privkey_path, sizeof(s_privkey_path), "/tmp/mctf-sftp-%d", (int)getpid());
   pgmoneta_snprintf(s_pubkey_path,  sizeof(s_pubkey_path),  "/tmp/mctf-sftp-%d.pub", (int)getpid());

   mctf_sh(NULL, "rm -f %s %s", s_privkey_path, s_pubkey_path);

   if (mctf_sh(NULL, "ssh-keygen -t ed25519 -f %s -N '' -q", s_privkey_path) != 0)
   {
      pgmoneta_log_error("ssh backend: failed to generate key pair");
      return MCTF_FAIL;
   }
   fprintf(stderr, "    - SSH key pair generated\n");
   fflush(stderr);

   rc = MCTF_START_CONTAINER(&s->container, MCTF_CONTAINER_SFTP);
   if (rc != MCTF_OK)
   {
      return rc; /* MCTF_SKIPPED bubbles up to mctf_se_up */
   }
   fprintf(stderr, "    - container started\n");
   fflush(stderr);

   if (provision() != MCTF_OK)
   {
      return MCTF_FAIL;
   }

   pgmoneta_snprintf(s->endpoint,   sizeof(s->endpoint),   "%s", SSH_HOST);
   s->port = SSH_PORT;
   /* Repurpose access_key for SSH username and bucket for SSH base dir. */
   pgmoneta_snprintf(s->access_key, sizeof(s->access_key), "%s", SSH_USER);
   pgmoneta_snprintf(s->bucket,     sizeof(s->bucket),     "%s", SSH_BASE_DIR);

   return MCTF_OK;
}

static void
ssh_stop(struct mctf_se* s)
{
   MCTF_STOP_CONTAINER(&s->container);

   if (s_privkey_path[0] != '\0')
   {
      mctf_sh(NULL, "rm -f %s %s", s_privkey_path, s_pubkey_path);
   }
}

/* SSH config keys are global-only, so reopen [pgmoneta] after [primary]. */
static int
ssh_write_server_conf(struct mctf_se* s, FILE* f)
{
   fprintf(f, "\n[pgmoneta]\n");
   fprintf(f, "ssh_hostname = %s\n", s->endpoint);
   fprintf(f, "ssh_port = %d\n", s->port);
   fprintf(f, "ssh_username = %s\n", s->access_key);
   fprintf(f, "ssh_base_dir = %s\n", s->bucket);
   fprintf(f, "ssh_public_key_file = %s\n", s_pubkey_path);
   fprintf(f, "ssh_private_key_file = %s\n", s_privkey_path);
   return MCTF_OK;
}

const struct mctf_se_driver mctf_ssh_driver = {
   .name = "ssh",
   .storage_engine = "ssh",
   .start = ssh_start,
   .stop = ssh_stop,
   .write_server_conf = ssh_write_server_conf,
};
