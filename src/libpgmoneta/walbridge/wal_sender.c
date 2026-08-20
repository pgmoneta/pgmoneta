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

/* pgmoneta */
#include <pgmoneta.h>
#include <logging.h>
#include <utils.h>
#include <network.h>
#include <security.h>
#include <compression.h>
#include <walfile.h>
#include <walfile/wal_reader.h>
#include <walbridge/lsn_map.h>
#include <walbridge/wal_encode.h>
#include <walbridge/migration_engine.h>
#include <walbridge/wal_sender.h>
#include <walbridge/wal_store.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#define PROTOCOL_VERSION_3                         196608
#define DEFAULT_WAL_SEG_SIZE                       (16 * 1024 * 1024)

#define MSG_ROW_DESCRIPTION                        'T'
#define MSG_DATA_ROW                               'D'
#define MSG_COMMAND_COMPLETE                       'C'
#define MSG_READY_FOR_QUERY                        'Z'
#define MSG_AUTHENTICATION                         'R'
#define MSG_COPY_BOTH                              'W'
#define MSG_COPY_DATA                              'd'
#define MSG_COPY_DONE                              'c'
#define MSG_ERROR                                  'E'
#define MSG_NEGOTIATE                              'v'
#define MSG_PARAMETER_STATUS                       'S'
#define MSG_SIMPLE_QUERY                           'Q'
#define MSG_TERMINATE                              'X'
#define MSG_PARSE                                  'P'
#define MSG_BIND                                   'B'
#define MSG_DESCRIBE                               'D'
#define MSG_EXECUTE                                'E'
#define MSG_SYNC                                   'S'
#define MSG_CLOSE                                  'C'

#define XLOG_DATA                                  'w'
#define PRIMARY_KEEPALIVE                          'k'
#define STANDBY_STATUS                             'r'

#define XLOG_SEGMENTS_PER_XLOG_ID(wal_segsz_bytes) (0x100000000UL / (wal_segsz_bytes))

/* ---- endian helpers ---- */

static uint16_t
be16(uint16_t v)
{
   return (uint16_t)((v >> 8) | (v << 8));
}

static uint32_t
be32(uint32_t v)
{
   return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

static uint64_t
be64(uint64_t v)
{
   uint64_t hi = (uint64_t)be32((uint32_t)(v >> 32));
   uint64_t lo = (uint64_t)be32((uint32_t)v);
   return (lo << 32) | hi;
}

static int64_t
sender_now_usec(void)
{
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static int
sender_persist_feedback(const char* map_path, uint64_t upstream, uint64_t downstream)
{
   char path[PATH_MAX];
   char tmp[PATH_MAX];
   FILE* file;

   pgmoneta_snprintf(path, sizeof(path), "%s.feedback", map_path);
   pgmoneta_snprintf(tmp, sizeof(tmp), "%s.tmp", path);
   /* Delayed status messages must never move the retention floor backwards. */
   file = fopen(path, "r");
   if (file != NULL)
   {
      uint64_t old_upstream = 0;
      uint64_t old_downstream = 0;
      if (fscanf(file, "%" SCNu64 " %" SCNu64, &old_upstream, &old_downstream) == 2 && old_downstream > downstream)
      {
         fclose(file);
         return 0;
      }
      fclose(file);
   }

   file = fopen(tmp, "w");
   if (file == NULL)
   {
      pgmoneta_log_error("wal_sender: could not persist downstream feedback: %m");
      return 1;
   }
   fprintf(file, "%" PRIu64 " %" PRIu64 "\n", upstream, downstream);
   fflush(file);
   fsync(fileno(file));
   fclose(file);
   if (rename(tmp, path) != 0)
   {
      unlink(tmp);
      return 1;
   }
   return 0;
}

static int
sender_segment_path(char* dir, uint64_t segno, uint32_t seg_size, char* out, size_t outsz)
{
   DIR* d = opendir(dir);
   struct dirent* e;
   char best[PATH_MAX] = "";
   uint64_t want_log = segno / (uint64_t)XLOG_SEGMENTS_PER_XLOG_ID(seg_size);
   uint64_t want_seg = segno % (uint64_t)XLOG_SEGMENTS_PER_XLOG_ID(seg_size);

   if (!d)
   {
      return 1;
   }

   while ((e = readdir(d)) != NULL)
   {
      unsigned int tli, log, seg;

      if (strlen(e->d_name) != 24)
      {
         continue;
      }
      if (sscanf(e->d_name, "%08X%08X%08X", &tli, &log, &seg) != 3)
      {
         continue;
      }
      if ((uint64_t)log == want_log && (uint64_t)seg == want_seg)
      {
         pgmoneta_snprintf(best, sizeof(best), "%s/%s", dir, e->d_name);
      }
   }
   closedir(d);

   if (best[0] == '\0')
   {
      return 1;
   }
   pgmoneta_snprintf(out, outsz, "%s", best);
   return 0;
}

static int
sender_read_fully(int fd, void* buf, size_t n)
{
   size_t off = 0;
   char* p = buf;

   while (off < n)
   {
      ssize_t r = read(fd, p + off, n - off);
      if (r == -1)
      {
         if (errno == EINTR)
         {
            continue;
         }
         return 1;
      }
      if (r == 0)
      {
         return 1; /* EOF */
      }
      off += (size_t)r;
   }
   return 0;
}

static int
sender_write_fully(int fd, const void* buf, size_t n)
{
   size_t off = 0;
   const char* p = buf;

   while (off < n)
   {
      ssize_t w = write(fd, p + off, n - off);
      if (w == -1)
      {
         if (errno == EINTR)
         {
            continue;
         }
         return 1;
      }
      off += (size_t)w;
   }
   return 0;
}

static int
sender_send_msg(int fd, char type, const void* body, uint32_t body_len)
{
   char hdr[5];
   uint32_t len = body_len + 4;

   hdr[0] = type;
   {
      uint32_t net = be32(len);
      memcpy(hdr + 1, &net, 4);
   }

   if (sender_write_fully(fd, hdr, sizeof(hdr)))
   {
      return 1;
   }
   if (body_len > 0 && sender_write_fully(fd, body, body_len))
   {
      return 1;
   }
   return 0;
}

static int
sender_send_error(int fd, const char* msg)
{
   char body[512];
   uint32_t off = 0;

   body[off++] = 'S';
   strcpy(body + off, "ERROR");
   off += strlen(body + off) + 1;

   body[off++] = 'V';
   strcpy(body + off, "ERROR");
   off += strlen(body + off) + 1;

   body[off++] = 'C';
   strcpy(body + off, "XX000");
   off += strlen(body + off) + 1;

   body[off++] = 'M';
   pgmoneta_snprintf(body + off, sizeof(body) - off, "%s", msg);
   off += strlen(body + off) + 1;

   body[off++] = '\0';

   return sender_send_msg(fd, MSG_ERROR, body, off);
}

static int
sender_send_ready(int fd)
{
   return sender_send_msg(fd, MSG_READY_FOR_QUERY, "I", 1);
}

/* ErrorResponse followed by ReadyForQuery, as required to end a simple-query
 * cycle. Startup-phase errors must NOT use this. */
static int
sender_send_error_ready(int fd, const char* msg)
{
   if (sender_send_error(fd, msg))
   {
      return 1;
   }
   return sender_send_ready(fd);
}

/* ParameterStatus 'S': name\0 value\0, reported during startup */
static int
sender_send_parameter_status(int fd, const char* name, const char* value)
{
   uint32_t len = (uint32_t)(strlen(name) + 1 + strlen(value) + 1);
   char* body = malloc(len);
   char* off = body;
   int ret;

   if (body == NULL)
   {
      return 1;
   }
   memcpy(off, name, strlen(name) + 1);
   off += strlen(name) + 1;
   memcpy(off, value, strlen(value) + 1);

   ret = sender_send_msg(fd, MSG_PARAMETER_STATUS, body, len);
   free(body);
   return ret;
}

static int
sender_send_command_complete(int fd, const char* tag)
{
   size_t len = strlen(tag) + 1;
   return sender_send_msg(fd, MSG_COMMAND_COMPLETE, tag, (uint32_t)len);
}

/* ---- startup / authentication ---- */

static int
sender_read_msg(int fd, char* type, char** body, uint32_t* body_len)
{
   uint32_t len;

   *body = NULL;
   *body_len = 0;

   if (sender_read_fully(fd, type, 1))
   {
      return 1;
   }
   if (sender_read_fully(fd, &len, 4))
   {
      return 1;
   }
   len = be32(len);
   if (len < 4)
   {
      return 1;
   }
   len -= 4;
   if (len > 0)
   {
      *body = malloc(len);
      if (*body == NULL)
      {
         return 1;
      }
      if (sender_read_fully(fd, *body, len))
      {
         free(*body);
         *body = NULL;
         return 1;
      }
   }
   *body_len = len;
   return 0;
}

static char*
sender_base64_encode(const unsigned char* raw, size_t raw_len)
{
   char* enc = NULL;
   size_t enc_len = 0;

   if (pgmoneta_base64_encode((void*)raw, raw_len, &enc, &enc_len))
   {
      return NULL;
   }
   return enc;
}

static int
sender_base64_decode(const char* enc, unsigned char** raw, size_t* raw_len)
{
   void* out = NULL;
   size_t out_len = 0;

   if (pgmoneta_base64_decode((char*)enc, strlen(enc), &out, &out_len))
   {
      return 1;
   }
   *raw = (unsigned char*)out;
   *raw_len = out_len;
   return 0;
}

/* Extract a SCRAM attribute value (single letter) from a buffer. The value is
 * the substring following "<attr>=" up to the next ',' or end of buffer. */
static char*
sender_scram_attr(char attr, const char* buf, size_t len)
{
   size_t i;

   for (i = 0; i + 1 < len; i++)
   {
      if (buf[i] == attr && buf[i + 1] == '=')
      {
         size_t start = i + 2;
         size_t end = start;
         char* out;

         while (end < len && buf[end] != ',')
         {
            end++;
         }
         out = malloc(end - start + 1);
         if (out == NULL)
         {
            return NULL;
         }
         memcpy(out, buf + start, end - start);
         out[end - start] = '\0';
         return out;
      }
   }
   return NULL;
}

/* Standard PostgreSQL SCRAM-SHA-256 server handshake over the raw socket.
 * Returns 0 on success (client authenticated), 1 on failure. */
static int
sender_auth_scram256(int fd, const char* password)
{
   char auth[32] = {0};
   uint32_t code;
   char mtype;
   char* msg = NULL;
   uint32_t mlen = 0;
   char* salt = NULL;
   int salt_len = 0;
   char* server_nounce = NULL;
   char* base64_salt = NULL;
   char* client_first = NULL;
   char* client_first_bare = NULL;
   char* client_nounce = NULL;
   char* combined_nounce = NULL;
   char* server_first = NULL;
   char* client_final = NULL;
   char* client_final_wo = NULL;
   char* base64_proof = NULL;
   unsigned char* proof_recv = NULL;
   size_t proof_recv_len = 0;
   unsigned char* proof_calc = NULL;
   size_t proof_calc_len = 0;
   unsigned char* sig_calc = NULL;
   size_t sig_calc_len = 0;
   char* base64_sig = NULL;
   int iterations = 4096;

   /* AuthenticationSASL: 'R', Int32(10), "SCRAM-SHA-256\0" */
   code = be32(10);
   memcpy(auth, &code, 4);
   strcpy(auth + 4, "SCRAM-SHA-256");
   if (sender_send_msg(fd, MSG_AUTHENTICATION, auth, 4 + 15))
   {
      goto error;
   }

   /* Client: 'p' + "SCRAM-SHA-256\0" + client-first-message ("n,,n=<u>,r=<cn>") */
   if (sender_read_msg(fd, &mtype, &msg, &mlen))
   {
      pgmoneta_log_error("wal_sender: scram could not read client-first");
      goto error;
   }
   if (mtype != 'p' || mlen < 18)
   {
      pgmoneta_log_error("wal_sender: scram unexpected SASL response (type %c len %u)", mtype, mlen);
      free(msg);
      goto error;
   }
   /* The SASLInitialResponse payload embeds a 4-byte length of the actual
    * client-first-message: "mechanism\0" + Int32(len) + client-first. */
   {
      uint32_t initial_len;
      memcpy(&initial_len, msg + 14, 4);
      initial_len = be32(initial_len);
      if (initial_len > mlen - 18)
      {
         pgmoneta_log_error("wal_sender: scram bad SASL initial length %u (msg %u)", initial_len, mlen);
         free(msg);
         goto error;
      }
      client_first = malloc(initial_len + 1);
      if (client_first == NULL)
      {
         free(msg);
         goto error;
      }
      memcpy(client_first, msg + 18, initial_len);
      client_first[initial_len] = '\0';
   }
   free(msg);
   msg = NULL;

   pgmoneta_log_info("wal_sender: scram client SASL payload len %u nonce-attr [%s]", mlen, client_first);
   {
      char hex[200] = "";
      for (uint32_t h = 0; h < strlen(client_first) && (int)sizeof(hex) - (int)strlen(hex) > 3; h++)
      {
         char b[4];
         snprintf(b, sizeof(b), "%02x ", (unsigned char)client_first[h]);
         strncat(hex, b, sizeof(hex) - strlen(hex) - 1);
      }
      pgmoneta_log_info("wal_sender: scram payload hex: %s", hex);
   }

   /* client-first-message-bare = client-first without the ",,n" gs2 header */
   if (strlen(client_first) > 3)
   {
      client_first_bare = strdup(client_first + 3);
   }
   else
   {
      client_first_bare = strdup(client_first);
   }

   client_nounce = sender_scram_attr('r', client_first, strlen(client_first));
   if (client_nounce == NULL)
   {
      pgmoneta_log_error("wal_sender: scram no client nonce in [%s]", client_first);
      free(client_first);
      client_first = NULL;
      free(client_first_bare);
      client_first_bare = NULL;
      goto error;
   }

   /* Generate server nonce + salt, build server-first-message */
   if (pgmoneta_generate_nounce(&server_nounce))
   {
      pgmoneta_log_error("wal_sender: scram generate_nounce failed");
      goto error;
   }
   if (pgmoneta_generate_salt(&salt, &salt_len))
   {
      pgmoneta_log_error("wal_sender: scram generate_salt failed");
      goto error;
   }

   {
      char* b64_salt = NULL;
      size_t b64_salt_len = 0;
      if (pgmoneta_base64_encode((void*)salt, (size_t)salt_len, &b64_salt, &b64_salt_len))
      {
         pgmoneta_log_error("wal_sender: scram base64_salt encode failed (salt_len=%d)", salt_len);
         goto error;
      }
      base64_salt = b64_salt;
   }

   combined_nounce = malloc(strlen(client_nounce) + strlen(server_nounce) + 1);
   if (combined_nounce == NULL)
   {
      pgmoneta_log_error("wal_sender: scram combined_nounce malloc failed");
      goto error;
   }
   strcpy(combined_nounce, client_nounce);
   strcat(combined_nounce, server_nounce);

   {
      size_t slen = strlen(combined_nounce) + strlen(base64_salt) + 32;
      server_first = malloc(slen);
      if (server_first == NULL)
      {
         pgmoneta_log_error("wal_sender: scram server_first malloc failed");
         goto error;
      }
      pgmoneta_snprintf(server_first, slen, "r=%s,s=%s,i=%d", combined_nounce, base64_salt, iterations);
   }

   /* AuthenticationSASLContinue: 'R', Int32(11), server-first-message */
   {
      size_t sflen = strlen(server_first);
      uint32_t code11 = be32(11);
      char* cbody = malloc(4 + sflen);
      if (cbody == NULL)
      {
         pgmoneta_log_error("wal_sender: scram cbody malloc failed");
         goto error;
      }
      memcpy(cbody, &code11, 4);
      memcpy(cbody + 4, server_first, sflen);
      if (sender_send_msg(fd, MSG_AUTHENTICATION, cbody, (uint32_t)(4 + sflen)))
      {
         pgmoneta_log_error("wal_sender: scram failed sending SASLContinue (%s)", strerror(errno));
         free(cbody);
         goto error;
      }
      free(cbody);
   }

   /* Client: 'p' + client-final-message ("c=biws,r=<combined>,p=<proof>") */
   if (sender_read_msg(fd, &mtype, &msg, &mlen))
   {
      pgmoneta_log_error("wal_sender: scram failed reading client-final (%s)", strerror(errno));
      goto error;
   }
   if (mtype != 'p' || mlen == 0)
   {
      pgmoneta_log_error("wal_sender: scram unexpected client-final (type %c len %u)", mtype, mlen);
      free(msg);
      goto error;
   }
   client_final = malloc(mlen + 1);
   if (client_final == NULL)
   {
      free(msg);
      goto error;
   }
   memcpy(client_final, msg, mlen);
   client_final[mlen] = '\0';
   free(msg);
   msg = NULL;

   /* client-final-message-wo-proof = "c=biws,r=<combined>" */
   base64_proof = sender_scram_attr('p', client_final, mlen);
   if (base64_proof == NULL)
   {
      pgmoneta_log_error("wal_sender: scram no proof in client-final [%.*s]", (int)mlen, client_final);
      goto error;
   }
   {
      char* comma = strstr(client_final, ",p=");
      if (comma == NULL)
      {
         pgmoneta_log_error("wal_sender: scram no ,p= in client-final [%.*s]", (int)mlen, client_final);
         goto error;
      }
      client_final_wo = malloc((size_t)(comma - client_final) + 1);
      if (client_final_wo == NULL)
      {
         goto error;
      }
      memcpy(client_final_wo, client_final, (size_t)(comma - client_final));
      client_final_wo[comma - client_final] = '\0';
   }

   /* Decode the client proof */
   if (sender_base64_decode(base64_proof, &proof_recv, &proof_recv_len))
   {
      pgmoneta_log_error("wal_sender: scram base64 proof decode failed");
      goto error;
   }

   /* Compute expected client proof; compare */
   {
      char* password_prep = NULL;
      if (pgmoneta_sasl_prep((char*)password, &password_prep))
      {
         pgmoneta_log_error("wal_sender: scram sasl_prep failed");
         goto error;
      }
      if (pgmoneta_client_proof(password_prep, salt, salt_len, iterations,
                                client_first_bare, strlen(client_first_bare),
                                server_first, strlen(server_first),
                                client_final_wo, strlen(client_final_wo),
                                &proof_calc, &proof_calc_len))
      {
         pgmoneta_log_error("wal_sender: scram client_proof computation failed");
         free(password_prep);
         goto error;
      }
      free(password_prep);

      if (proof_calc_len != proof_recv_len ||
          memcmp(proof_calc, proof_recv, proof_calc_len) != 0)
      {
         pgmoneta_log_error("wal_sender: scram proof mismatch (calc %zu recv %zu)",
                            proof_calc_len, proof_recv_len);
         goto bad_password;
      }
   }

   /* Compute server signature; send SASLFinal */
   {
      char* password_prep = NULL;
      if (pgmoneta_sasl_prep((char*)password, &password_prep))
      {
         goto error;
      }
      if (pgmoneta_server_signature(password_prep, salt, salt_len, iterations,
                                    NULL, 0,
                                    client_first_bare, strlen(client_first_bare),
                                    server_first, strlen(server_first),
                                    client_final_wo, strlen(client_final_wo),
                                    &sig_calc, &sig_calc_len))
      {
         free(password_prep);
         goto error;
      }
      free(password_prep);
   }

   base64_sig = sender_base64_encode(sig_calc, sig_calc_len);
   if (base64_sig == NULL)
   {
      goto error;
   }

   /* AuthenticationSASLFinal: 'R', Int32(12), "v=<sig>" */
   {
      size_t vlen = 2 + strlen(base64_sig);
      uint32_t code12 = be32(12);
      char* fbody = malloc(4 + vlen);
      if (fbody == NULL)
      {
         goto error;
      }
      memcpy(fbody, &code12, 4);
      memcpy(fbody + 4, "v=", 2);
      memcpy(fbody + 6, base64_sig, strlen(base64_sig));
      if (sender_send_msg(fd, MSG_AUTHENTICATION, fbody, (uint32_t)(4 + vlen)))
      {
         free(fbody);
         goto error;
      }
      free(fbody);
   }

   /* AuthenticationOk: 'R', Int32(0) */
   {
      uint32_t ok = be32(0);
      if (sender_send_msg(fd, MSG_AUTHENTICATION, &ok, 4))
      {
         goto error;
      }
   }

   free(salt);
   free(server_nounce);
   free(base64_salt);
   free(client_first);
   free(client_first_bare);
   free(client_nounce);
   free(combined_nounce);
   free(server_first);
   free(client_final);
   free(client_final_wo);
   free(base64_proof);
   free(proof_recv);
   free(proof_calc);
   free(sig_calc);
   free(base64_sig);
   return 0;

error:
   sender_send_error(fd, "walbridge: SCRAM-SHA-256 authentication failed");
   goto cleanup;

bad_password:
   sender_send_error(fd, "walbridge: SCRAM-SHA-256 authentication failed");

cleanup:
   free(salt);
   free(server_nounce);
   free(base64_salt);
   free(client_first);
   free(client_first_bare);
   free(client_nounce);
   free(combined_nounce);
   free(server_first);
   free(client_final);
   free(client_final_wo);
   free(base64_proof);
   free(proof_recv);
   free(proof_calc);
   free(sig_calc);
   free(base64_sig);
   return 1;
}

static int
sender_handle_startup(int fd, int srv)
{
   uint32_t len;
   uint32_t version;
   char* body = NULL;
   char* username = NULL;

   if (sender_read_fully(fd, &len, 4))
   {
      return 1;
   }
   len = be32(len);
   if (len < 8)
   {
      return 1;
   }
   if (sender_read_fully(fd, &version, 4))
   {
      return 1;
   }
   version = be32(version);

   body = malloc(len - 8 + 1);
   if (!body)
   {
      return 1;
   }
   memset(body, 0, len - 8 + 1);
   if (sender_read_fully(fd, body, len - 8))
   {
      free(body);
      return 1;
   }

   /* Parse key/value pairs in the startup body: "key\0value\0...\0".
    * Keys with the reserved "_pq_." prefix are protocol extension requests;
    * we support none, so each one must be echoed in the NegotiateProtocolVersion
    * reply. */
   char** extensions = NULL;
   int nextensions = 0;
   {
      char* p = body;
      char* end = body + (len - 8);
      while (p < end && *p != '\0')
      {
         char* key = p;
         size_t klen = strlen(p);
         char* val;

         p += klen + 1;
         if (p >= end)
         {
            break;
         }
         val = p;
         p += strlen(p) + 1;

         if (strncmp(key, "_pq_.", 5) == 0)
         {
            if (nextensions == 0)
            {
               extensions = malloc(sizeof(char*));
            }
            else
            {
               char** tmp = realloc(extensions, sizeof(char*) * (nextensions + 1));
               if (!tmp)
               {
                  free(extensions);
                  extensions = NULL;
                  nextensions = 0;
                  break;
               }
               extensions = tmp;
            }
            if (extensions != NULL)
            {
               extensions[nextensions++] = strdup(key);
            }
         }
         if (pgmoneta_compare_string(key, "user"))
         {
            username = strdup(val);
         }
      }
   }

   if (version == 80877103) /* SSLRequest */
   {
      char no[1] = {'N'};
      sender_write_fully(fd, no, 1);
      free(body);
      return sender_handle_startup(fd, srv);
   }

   /*
    * PostgreSQL protocol 3 keeps the major version in the high 16 bits.
    * Newer clients (including PostgreSQL 19) may advertise a newer minor
    * version while remaining wire compatible with protocol 3.0.
    */
   if ((version >> 16) != (PROTOCOL_VERSION_3 >> 16))
   {
      sender_send_error(fd, "walbridge: unsupported protocol version");
      free(body);
      free(username);
      return 1;
   }

   /* Protocol minor versions above our supported one (3.0), or any requested
    * "_pq_." protocol extensions, get a NegotiateProtocolVersion ('v') reply
    * before authentication, exactly like a real PostgreSQL server. Newer
    * clients (including PostgreSQL 19) probe with an intentionally bogus minor
    * version and a reserved "_pq_" parameter, and reject servers that blindly
    * accept them without negotiating. The first Int32 is the highest protocol
    * version we support (3.0) as a full version number; the next Int32 is the
    * number of rejected extensions, followed by the extension names. */
   if (version > PROTOCOL_VERSION_3 || nextensions > 0)
   {
      size_t nblen = 8;
      size_t noff;
      for (int i = 0; i < nextensions; i++)
      {
         nblen += strlen(extensions[i]) + 1;
      }
      {
         char* nbody = malloc(nblen);
         if (!nbody)
         {
            for (int i = 0; i < nextensions; i++)
            {
               free(extensions[i]);
            }
            free(extensions);
            free(body);
            free(username);
            return 1;
         }
         uint32_t newest = be32(PROTOCOL_VERSION_3);
         uint32_t cnt = be32((uint32_t)nextensions);
         memcpy(nbody, &newest, 4);
         memcpy(nbody + 4, &cnt, 4);
         noff = 8;
         for (int i = 0; i < nextensions; i++)
         {
            memcpy(nbody + noff, extensions[i], strlen(extensions[i]));
            noff += strlen(extensions[i]);
            nbody[noff++] = '\0';
         }
         if (sender_send_msg(fd, MSG_NEGOTIATE, nbody, nblen))
         {
            for (int i = 0; i < nextensions; i++)
            {
               free(extensions[i]);
            }
            free(extensions);
            free(body);
            free(username);
            return 1;
         }
         free(nbody);
      }
   }

   for (int i = 0; i < nextensions; i++)
   {
      free(extensions[i]);
   }
   free(extensions);

   /* SCRAM-SHA-256 authentication */
   {
      struct main_configuration* config = (struct main_configuration*)shmem;
      const char* password = NULL;

      if (username != NULL)
      {
         for (int i = 0; i < config->common.number_of_users; i++)
         {
            if (pgmoneta_compare_string(config->common.users[i].username, username))
            {
               password = config->common.users[i].password;
               break;
            }
         }
      }

      if (password == NULL)
      {
         sender_send_error(fd, "walbridge: unknown user");
         free(body);
         free(username);
         return 1;
      }

      if (sender_auth_scram256(fd, password))
      {
         free(body);
         free(username);
         return 1;
      }
   }

   free(body);
   free(username);

   if (sender_send_parameter_status(fd, "server_version", "19.0") ||
       sender_send_parameter_status(fd, "server_encoding", "UTF8") ||
       sender_send_parameter_status(fd, "client_encoding", "UTF8") ||
       sender_send_parameter_status(fd, "application_name", "walbridge") ||
       sender_send_parameter_status(fd, "is_superuser", "on") ||
       sender_send_parameter_status(fd, "session_authorization", "repl") ||
       sender_send_parameter_status(fd, "DateStyle", "ISO, MDY") ||
       sender_send_parameter_status(fd, "IntervalStyle", "postgres") ||
       sender_send_parameter_status(fd, "TimeZone", "UTC") ||
       sender_send_parameter_status(fd, "integer_datetimes", "on") ||
       sender_send_parameter_status(fd, "standard_conforming_strings", "on"))
   {
      return 1;
   }

   return sender_send_ready(fd);
}

/* ---- IDENTIFY_SYSTEM ---- */

static int
sender_get_stream_info(int srv, uint64_t* sysid, uint32_t* tli, uint32_t* seg_size)
{
   char* raw_dir = pgmoneta_get_server_wal(srv);
   DIR* dir;
   struct dirent* ent;
   char path[PATH_MAX];
   struct walfile* wf = NULL;
   char first[PATH_MAX] = "";

   if (!raw_dir)
   {
      return 1;
   }
   dir = opendir(raw_dir);
   if (!dir)
   {
      free(raw_dir);
      return 1;
   }

   while ((ent = readdir(dir)) != NULL)
   {
      if (ent->d_type == DT_DIR)
      {
         continue;
      }
      if (strlen(ent->d_name) != 24)
      {
         continue;
      }
      if (first[0] == '\0' || strcmp(ent->d_name, first) < 0)
      {
         pgmoneta_snprintf(first, sizeof(first), "%s", ent->d_name);
      }
   }
   closedir(dir);

   if (first[0] == '\0')
   {
      free(raw_dir);
      return 1;
   }

   pgmoneta_snprintf(path, sizeof(path), "%s/%s", raw_dir, first);
   free(raw_dir);

   wf = calloc(1, sizeof(*wf));
   if (!wf)
   {
      return 1;
   }
   if (pgmoneta_deque_create(false, &wf->records) || pgmoneta_deque_create(false, &wf->page_headers))
   {
      pgmoneta_destroy_walfile(wf);
      return 1;
   }
   if (pgmoneta_wal_parse_wal_file(path, -1, wf) != 0)
   {
      pgmoneta_destroy_walfile(wf);
      return 1;
   }

   if (sysid)
   {
      *sysid = wf->long_phd->xlp_sysid;
   }
   if (tli)
   {
      *tli = wf->long_phd->std.xlp_tli;
   }
   if (seg_size)
   {
      *seg_size = wf->long_phd->xlp_seg_size ? wf->long_phd->xlp_seg_size : DEFAULT_WAL_SEG_SIZE;
   }

   pgmoneta_destroy_walfile(wf);
   return 0;
}

/* Read the highest (newest) pair from the LSN map file, returning the downstream
 * translation of the last emitted record. Returns 1 if no usable entry exists. */
static int
sender_map_top(char* map_path, uint64_t* downstream_top)
{
   FILE* f = fopen(map_path, "r");
   uint64_t up = 0;
   uint64_t down = 0;
   int found = 0;

   if (!f)
   {
      return 1;
   }
   while (fscanf(f, "%" SCNu64 " %" SCNu64, &up, &down) == 2)
   {
      found = 1;
   }
   fclose(f);

   if (!found)
   {
      return 1;
   }
   *downstream_top = down;
   return 0;
}

/* Refresh the in-memory LSN map view incrementally. The receiver persists the
 * map file append-only, so the sender resumes from the byte offset it already
 * loaded and parses only the appended tail: O(new bytes) per refresh instead
 * of O(whole map), which matters once the map reaches tens/hundreds of MB.
 * Falls back to a full reload only when the file shrank (reset/recreated) or
 * the tail read failed. */
static void
sender_refresh_map(char* map_path, struct lsn_map** map, uint64_t* last_map_offset)
{
   struct stat st;
   struct lsn_map* fresh = NULL;

   if (stat(map_path, &st) != 0)
   {
      /* map missing: keep the current view (likely transient) */
      return;
   }
   if ((uint64_t)st.st_size < *last_map_offset)
   {
      /* file was reset/truncated: rebuild from scratch */
      if (pgmoneta_lsn_map_create(map_path, &fresh) == 0)
      {
         pgmoneta_lsn_map_destroy(*map);
         *map = fresh;
         *last_map_offset = pgmoneta_lsn_map_durable_bytes(fresh);
      }
      return;
   }
   if ((uint64_t)st.st_size > *last_map_offset)
   {
      if (pgmoneta_lsn_map_append_tail(*map, *last_map_offset, last_map_offset))
      {
         /* I/O failure reading the tail: fall back to a fresh load */
         if (pgmoneta_lsn_map_create(map_path, &fresh) == 0)
         {
            pgmoneta_lsn_map_destroy(*map);
            *map = fresh;
            *last_map_offset = pgmoneta_lsn_map_durable_bytes(fresh);
         }
      }
   }
}

static int
sender_send_identify_system(int fd, int srv, char* map_path)
{
   uint64_t sysid = 0;
   uint32_t tli = 1;
   char sysid_str[32];
   char tli_str[16];
   char xlogpos[24];
   char body[1024];
   uint32_t off = 0;

   if (sender_get_stream_info(srv, &sysid, &tli, NULL))
   {
      sysid = 0;
      tli = 1;
   }

   pgmoneta_snprintf(sysid_str, sizeof(sysid_str), "%" PRIu64, sysid);
   pgmoneta_snprintf(tli_str, sizeof(tli_str), "%u", tli);
   {
      uint64_t top = 0;
      if (sender_map_top(map_path, &top) == 0 && top > 0)
      {
         pgmoneta_snprintf(xlogpos, sizeof(xlogpos), "%X/%X", (uint32_t)(top >> 32), (uint32_t)top);
      }
      else
      {
         pgmoneta_snprintf(xlogpos, sizeof(xlogpos), "%X/%X", 0, 0);
      }
   }

   /* RowDescription: systemid text, timeline int4, xlogpos text, dbname name */
   {
      int16_t ncols = be16(4);
      uint32_t table_oid = be32(0);
      uint16_t attnum = be16(0);
      int32_t typmod = be32(-1);
      uint16_t format = be16(0);
      int32_t oid_text = be32(25);
      int16_t len_text = be16(-1);
      int32_t oid_int4 = be32(23);
      int16_t len_int4 = be16(4);
      int32_t oid_name = be32(19);
      int16_t len_name = be16(64);

      memset(body, 0, sizeof(body));
      memcpy(body + off, &ncols, 2);
      off += 2;

      memcpy(body + off, "systemid", 9);
      off += 9;
      memcpy(body + off, &table_oid, 4);
      off += 4;
      memcpy(body + off, &attnum, 2);
      off += 2;
      memcpy(body + off, &oid_text, 4);
      off += 4;
      memcpy(body + off, &len_text, 2);
      off += 2;
      memcpy(body + off, &typmod, 4);
      off += 4;
      memcpy(body + off, &format, 2);
      off += 2;

      memcpy(body + off, "timeline", 9);
      off += 9;
      memcpy(body + off, &table_oid, 4);
      off += 4;
      memcpy(body + off, &attnum, 2);
      off += 2;
      memcpy(body + off, &oid_int4, 4);
      off += 4;
      memcpy(body + off, &len_int4, 2);
      off += 2;
      memcpy(body + off, &typmod, 4);
      off += 4;
      memcpy(body + off, &format, 2);
      off += 2;

      memcpy(body + off, "xlogpos", 8);
      off += 8;
      memcpy(body + off, &table_oid, 4);
      off += 4;
      memcpy(body + off, &attnum, 2);
      off += 2;
      memcpy(body + off, &oid_text, 4);
      off += 4;
      memcpy(body + off, &len_text, 2);
      off += 2;
      memcpy(body + off, &typmod, 4);
      off += 4;
      memcpy(body + off, &format, 2);
      off += 2;

      memcpy(body + off, "dbname", 7);
      off += 7;
      memcpy(body + off, &table_oid, 4);
      off += 4;
      memcpy(body + off, &attnum, 2);
      off += 2;
      memcpy(body + off, &oid_name, 4);
      off += 4;
      memcpy(body + off, &len_name, 2);
      off += 2;
      memcpy(body + off, &typmod, 4);
      off += 4;
      memcpy(body + off, &format, 2);
      off += 2;

      if (sender_send_msg(fd, MSG_ROW_DESCRIPTION, body, off))
      {
         return 1;
      }
   }

   /* DataRow: one row, text format for all columns */
   {
      int16_t ncols = be16(4);
      int32_t c1len = be32((uint32_t)strlen(sysid_str));
      int32_t c2len = be32((uint32_t)strlen(tli_str));
      int32_t c3len = be32((uint32_t)strlen(xlogpos));
      int32_t c4len = be32(-1); /* dbname is SQL NULL for physical streaming */

      memset(body, 0, sizeof(body));
      off = 0;
      memcpy(body + off, &ncols, 2);
      off += 2;
      memcpy(body + off, &c1len, 4);
      off += 4;
      memcpy(body + off, sysid_str, strlen(sysid_str));
      off += strlen(sysid_str);
      memcpy(body + off, &c2len, 4);
      off += 4;
      memcpy(body + off, tli_str, strlen(tli_str));
      off += strlen(tli_str);
      memcpy(body + off, &c3len, 4);
      off += 4;
      memcpy(body + off, xlogpos, strlen(xlogpos));
      off += strlen(xlogpos);
      memcpy(body + off, &c4len, 4);
      off += 4;

      if (sender_send_msg(fd, MSG_DATA_ROW, body, off))
      {
         return 1;
      }
   }

   if (sender_send_command_complete(fd, "SELECT 1"))
   {
      return 1;
   }

   return sender_send_ready(fd);
}

/* READ_REPLICATION_SLOT <name>: report the requested physical slot. The slot
 * always exists from this proxy's point of view; restart_lsn is the newest
 * downstream position we have emitted so far. */
static int
sender_send_read_replication_slot(int fd, char* map_path)
{
   char body[1024];
   uint32_t off = 0;
   uint64_t restart = 0;
   char restart_str[24];
   char tli_str[16];

   /* A physical slot's restart LSN is the retained floor, not the head. */
   {
      char feedback[PATH_MAX];
      FILE* file;
      uint64_t ignored_upstream = 0;
      pgmoneta_snprintf(feedback, sizeof(feedback), "%s.feedback", map_path);
      file = fopen(feedback, "r");
      if (file != NULL)
      {
         if (fscanf(file, "%" SCNu64 " %" SCNu64, &ignored_upstream, &restart) != 2)
         {
            restart = 0;
         }
         fclose(file);
      }
   }
   if (restart == 0)
   {
      restart = 0;
   }
   pgmoneta_snprintf(restart_str, sizeof(restart_str), "%X/%X", (uint32_t)(restart >> 32), (uint32_t)restart);
   pgmoneta_snprintf(tli_str, sizeof(tli_str), "1");

   /* RowDescription: slot_type name, restart_lsn text, restart_tli text */
   {
      int16_t ncols = be16(3);
      uint32_t table_oid = be32(0);
      uint16_t attnum = be16(0);
      int32_t typmod = be32(-1);
      uint16_t format = be16(0);
      int32_t oid_text = be32(25);
      int32_t oid_name = be32(19);
      int16_t len_text = be16(-1);
      int16_t len_name = be16(64);

      memset(body, 0, sizeof(body));
      memcpy(body + off, &ncols, 2);
      off += 2;

      memcpy(body + off, "slot_type", 10);
      off += 10;
      memcpy(body + off, &table_oid, 4);
      off += 4;
      memcpy(body + off, &attnum, 2);
      off += 2;
      memcpy(body + off, &oid_name, 4);
      off += 4;
      memcpy(body + off, &len_name, 2);
      off += 2;
      memcpy(body + off, &typmod, 4);
      off += 4;
      memcpy(body + off, &format, 2);
      off += 2;

      memcpy(body + off, "restart_lsn", 12);
      off += 12;
      memcpy(body + off, &table_oid, 4);
      off += 4;
      memcpy(body + off, &attnum, 2);
      off += 2;
      memcpy(body + off, &oid_text, 4);
      off += 4;
      memcpy(body + off, &len_text, 2);
      off += 2;
      memcpy(body + off, &typmod, 4);
      off += 4;
      memcpy(body + off, &format, 2);
      off += 2;

      memcpy(body + off, "restart_tli", 12);
      off += 12;
      memcpy(body + off, &table_oid, 4);
      off += 4;
      memcpy(body + off, &attnum, 2);
      off += 2;
      memcpy(body + off, &oid_text, 4);
      off += 4;
      memcpy(body + off, &len_text, 2);
      off += 2;
      memcpy(body + off, &typmod, 4);
      off += 4;
      memcpy(body + off, &format, 2);
      off += 2;

      if (sender_send_msg(fd, MSG_ROW_DESCRIPTION, body, off))
      {
         return 1;
      }
   }

   /* DataRow: physical, restart_lsn, restart_tli */
   {
      int16_t ncols = be16(3);
      int32_t c1len = be32((uint32_t)strlen("physical"));
      int32_t c2len = be32((uint32_t)strlen(restart_str));
      int32_t c3len = be32((uint32_t)strlen(tli_str));

      memset(body, 0, sizeof(body));
      off = 0;
      memcpy(body + off, &ncols, 2);
      off += 2;
      memcpy(body + off, &c1len, 4);
      off += 4;
      memcpy(body + off, "physical", strlen("physical"));
      off += strlen("physical");
      memcpy(body + off, &c2len, 4);
      off += 4;
      memcpy(body + off, restart_str, strlen(restart_str));
      off += strlen(restart_str);
      memcpy(body + off, &c3len, 4);
      off += 4;
      memcpy(body + off, tli_str, strlen(tli_str));
      off += strlen(tli_str);

      if (sender_send_msg(fd, MSG_DATA_ROW, body, off))
      {
         return 1;
      }
   }

   if (sender_send_command_complete(fd, "READ_REPLICATION_SLOT"))
   {
      return 1;
   }

   return sender_send_ready(fd);
}

static int
sender_send_show(int fd, char* name, char* value)
{
   char body[512];
   uint32_t off = 0;
   uint16_t format = be16(0);

   /* RowDescription: a single text column named after the GUC */
   {
      int16_t ncols = be16(1);
      uint32_t table_oid = be32(0);
      uint16_t attnum = be16(0);
      int32_t typmod = be32(-1);
      int32_t oid_text = be32(25);
      int16_t len_text = be16(-1);

      memset(body, 0, sizeof(body));
      memcpy(body + off, &ncols, 2);
      off += 2;
      memcpy(body + off, name, strlen(name) + 1);
      off += strlen(name) + 1;
      memcpy(body + off, &table_oid, 4);
      off += 4;
      memcpy(body + off, &attnum, 2);
      off += 2;
      memcpy(body + off, &oid_text, 4);
      off += 4;
      memcpy(body + off, &len_text, 2);
      off += 2;
      memcpy(body + off, &typmod, 4);
      off += 4;
      memcpy(body + off, &format, 2);
      off += 2;

      if (sender_send_msg(fd, MSG_ROW_DESCRIPTION, body, off))
      {
         return 1;
      }
   }

   /* DataRow: one column */
   {
      int16_t ncols = be16(1);
      int32_t vlen = be32((uint32_t)strlen(value));

      memset(body, 0, sizeof(body));
      off = 0;
      memcpy(body + off, &ncols, 2);
      off += 2;
      memcpy(body + off, &vlen, 4);
      off += 4;
      memcpy(body + off, value, strlen(value));
      off += strlen(value);

      if (sender_send_msg(fd, MSG_DATA_ROW, body, off))
      {
         return 1;
      }
   }

   if (sender_send_command_complete(fd, "SHOW"))
   {
      return 1;
   }

   return sender_send_ready(fd);
}

/* ---- START_REPLICATION ---- */

static int
sender_parse_start_replication(char* query, uint64_t* start_lsn, uint32_t* timeline)
{
   /* Accept the START_REPLICATION forms used by contemporary clients:
    *   START_REPLICATION 0/02000000
    *   START_REPLICATION 0/02000000 TIMELINE 1
    *   START_REPLICATION PHYSICAL 0/02000000
    *   START_REPLICATION SLOT name [PHYSICAL] 0/02000000
    * We only chew through the tokens looking for the LSN and an optional
    * TIMELINE; everything else (slot names, PHYSICAL) is skipped. */
   char* p = NULL;
   unsigned int hi = 0, lo = 0;
   int tli = 0;

   *timeline = 0;

   p = strstr(query, "START_REPLICATION");
   if (!p)
   {
      return 1;
   }
   p += strlen("START_REPLICATION");

   while (*p != '\0')
   {
      char* end = NULL;

      while (*p == ' ')
      {
         p++;
      }
      if (*p == '\0')
      {
         break;
      }
      end = p;
      while (*end != ' ' && *end != '\0')
      {
         end++;
      }

      if (strncmp(p, "SLOT", 4) == 0 && (p[4] == ' ' || p[4] == '\0'))
      {
         /* slot name follows; skip it */
         p = end;
         while (*p == ' ')
         {
            p++;
         }
         end = p;
         while (*end != ' ' && *end != '\0')
         {
            end++;
         }
         p = end;
         continue;
      }
      if (strncmp(p, "TIMELINE", 8) == 0 && (p[8] == ' ' || p[8] == '\0'))
      {
         /* timeline value follows */
         p = end;
         while (*p == ' ')
         {
            p++;
         }
         if (sscanf(p, "%d", &tli) == 1)
         {
            *timeline = (uint32_t)tli;
         }
         p = end;
         continue;
      }
      if (sscanf(p, "%X/%X", &hi, &lo) == 2)
      {
         if ((*p < '0' || *p > '9') && (*p < 'A' || *p > 'F'))
         {
            return 1;
         }
         *start_lsn = ((uint64_t)hi << 32) | lo;
         return 0;
      }
      p = end;
   }

   return 1;
}

/* ---------------------------------------------------------------------------
 * Live-tail upstream decode helpers
 *
 * The sender serves the archived portion of the downstream stream verbatim
 * from the wal_store segment files (phase A). Once the store can no longer
 * provide bytes (its on-disk copy lags the live translation, or the client's
 * requested position lies beyond the receiver's frontier), the sender decodes
 * the upstream 18.x WAL directly, translates each record through the migration
 * engine, and re-encodes it with the wal_encode encoder (phase B). Because the
 * encoder is byte-identical to the store, the two phases join seamlessly at
 * any custom resume position (no segment-boundary flooring).
 * --------------------------------------------------------------------------- */

struct sender_up_seg
{
   uint64_t segno;
   bool active; /* file is still being received (.partial) */
};

static int
sender_up_parse_name(char* name, uint64_t* segno, bool* active, uint32_t* tli, uint32_t seg_size)
{
   char base[25];
   size_t len = strlen(name);
   size_t blen = len;
   unsigned int tl, log, seg;
   int items;

   if (active)
   {
      *active = false;
   }
   if (len > 8 && strcmp(name + len - 8, ".partial") == 0)
   {
      if (active)
      {
         *active = true;
      }
      blen = len - 8;
   }
   else if (len > 5 && strcmp(name + len - 5, ".zstd") == 0)
   {
      /* Sealed and compressed: the WAL client compresses every sealed raw
       * segment, so a *.zstd file is a present (not pruned) segment. */
      blen = len - 5;
   }
   if (blen != 24)
   {
      return 1;
   }
   memcpy(base, name, blen);
   base[blen] = '\0';
   items = sscanf(base, "%08X%08X%08X", &tl, &log, &seg);
   if (items != 3)
   {
      return 1;
   }
   if (segno)
   {
      *segno = (uint64_t)log * XLOG_SEGMENTS_PER_XLOG_ID(seg_size) + seg;
   }
   if (tli)
   {
      *tli = tl;
   }
   return 0;
}

static int
sender_up_collect(char* dir, uint32_t seg_size, struct sender_up_seg** out, int* count)
{
   DIR* d = opendir(dir);
   struct dirent* e;
   int i, j;
   int capacity = 0;

   *out = NULL;
   *count = 0;
   if (!d)
   {
      return 1;
   }

   while ((e = readdir(d)) != NULL)
   {
      uint64_t segno;
      bool active;

      if (sender_up_parse_name(e->d_name, &segno, &active, NULL, seg_size))
      {
         continue;
      }

      for (i = 0; i < *count; i++)
      {
         if ((*out)[i].segno == segno)
         {
            if (!active)
            {
               (*out)[i].active = false; /* a completed segment beats a .partial */
            }
            break;
         }
      }
      if (i < *count)
      {
         continue;
      }
      if (*count == capacity)
      {
         int new_capacity = capacity == 0 ? 128 : capacity * 2;
         struct sender_up_seg* new_out = realloc(*out, (size_t)new_capacity * sizeof(**out));

         if (!new_out)
         {
            free(*out);
            *out = NULL;
            *count = 0;
            closedir(d);
            return 1;
         }
         *out = new_out;
         capacity = new_capacity;
      }

      (*out)[*count].segno = segno;
      (*out)[*count].active = active;
      (*count)++;
      /* insertion sort ascending by segno */
      for (j = *count - 1; j > 0 && (*out)[j].segno < (*out)[j - 1].segno; j--)
      {
         struct sender_up_seg tmp = (*out)[j];
         (*out)[j] = (*out)[j - 1];
         (*out)[j - 1] = tmp;
      }
   }
   closedir(d);
   return 0;
}

static int
sender_up_segment_path(char* dir, uint64_t segno, uint32_t seg_size, char* out,
                       size_t outsz, bool* is_partial, uint64_t* size)
{
   DIR* d = opendir(dir);
   struct dirent* e;
   char bare[PATH_MAX] = "";
   char part[PATH_MAX] = "";
   char comp[PATH_MAX] = "";
   struct stat sb;
   struct stat sp;
   struct stat sc;

   if (!d)
   {
      return 1;
   }
   while ((e = readdir(d)) != NULL)
   {
      uint64_t n;
      bool active;

      if (sender_up_parse_name(e->d_name, &n, &active, NULL, seg_size) || n != segno)
      {
         continue;
      }
      if (pgmoneta_ends_with(e->d_name, ".zstd"))
      {
         pgmoneta_snprintf(comp, sizeof(comp), "%s/%s", dir, e->d_name);
         if (stat(comp, &sc) != 0)
         {
            comp[0] = '\0';
         }
      }
      else if (active)
      {
         pgmoneta_snprintf(part, sizeof(part), "%s/%s", dir, e->d_name);
         if (stat(part, &sp) != 0)
         {
            part[0] = '\0';
         }
      }
      else
      {
         pgmoneta_snprintf(bare, sizeof(bare), "%s/%s", dir, e->d_name);
         if (stat(bare, &sb) != 0)
         {
            bare[0] = '\0';
         }
      }
   }
   closedir(d);

   if (bare[0])
   {
      pgmoneta_snprintf(out, outsz, "%s", bare);
      if (is_partial)
      {
         *is_partial = false;
      }
      if (size)
      {
         *size = (uint64_t)sb.st_size;
      }
      return 0;
   }
   if (comp[0])
   {
      pgmoneta_snprintf(out, outsz, "%s", comp);
      if (is_partial)
      {
         *is_partial = false;
      }
      if (size)
      {
         *size = (uint64_t)sc.st_size;
      }
      return 0;
   }
   if (part[0])
   {
      pgmoneta_snprintf(out, outsz, "%s", part);
      if (is_partial)
      {
         *is_partial = true;
      }
      if (size)
      {
         *size = (uint64_t)sp.st_size;
      }
      return 0;
   }
   return 1;
}

/* Probe for an upstream segment with a bounded retry window (~2.5s). A
 * segment can be momentarily absent between the collector listing it and
 * the pick: the WAL client renames .partial -> bare at seal, and the
 * compressor stages .zstd.tmp -> .zstd. Only a persistent absence is data
 * loss, so ride out the transient windows before the caller gives up. */
static int
sender_up_expect_segment(char* dir, uint64_t segno, uint32_t seg_size, char* out,
                         size_t outsz, bool* is_partial, uint64_t* size)
{
   int retries = 0;

   while (retries < 25)
   {
      if (sender_up_segment_path(dir, segno, seg_size, out, outsz, is_partial, size) == 0)
      {
         return 0;
      }
      retries++;
      usleep(100000);
   }
   return 1;
}

/* Parameters shared between the run-up probe encoder and the live encoder. */
struct sender_encode_ctx
{
   uint64_t start_lsn; /* downstream position the peer reconnects at (D) */
   uint64_t sysid;     /* from the first parsed upstream segment */
   uint32_t seg_size;
   uint32_t blksz;
   uint32_t tli;
   struct wal_encoder* real; /* live downstream encoder */
   bool checksums;           /* propagate the primary's data-checksum state */
};

/* Translate one upstream record and deliver it to the live downstream
 * encoder. The encoder was resumed exactly at the peer's resume position D
 * with xl_prev set to the downstream start of the last record the peer has,
 * so the first record fed here (the first upstream record after the run-up
 * anchor) is placed exactly at D and byte-chains to the peer's stream.
 * Returns 0 on success (including DROP), 1 on a hard error. */
static int
sender_feed_record(struct decoded_xlog_record* rec, struct lsn_map* map,
                   struct sender_encode_ctx* ctx, uint64_t* placed_out)
{
   int result;
   uint64_t placed = 0;

   result = pgmoneta_migration_engine_translate(rec, WAL_MAGIC_V18, WAL_MAGIC_V19, map,
                                                ctx->checksums);
   if (result < 0)
   {
      pgmoneta_log_error("wal_sender: translation failed for record at %X/%X",
                         LSN_FORMAT_ARGS(rec->lsn));
      if (placed_out)
      {
         *placed_out = 0;
      }
      return 1;
   }
   if (result == PGMONETA_MIGRATION_DROP)
   {
      if (placed_out)
      {
         *placed_out = 0;
      }
      /* no downstream bytes, no position advance, no mapping */
      return 0;
   }

   if (pgmoneta_wal_encoder_write_record(ctx->real, rec, &placed))
   {
      if (placed_out)
      {
         *placed_out = 0;
      }
      return 1;
   }

   /* The map belongs to the raw->downstream conversion, not to a cache
    * writer.  Populate it as records are actually encoded for delivery.
    * Replaying a retained raw segment after a sender restart can encounter a
    * durable entry again; it must be byte-for-byte identical, never appended
    * as a conflicting second mapping. */
   {
      uint64_t known = 0;
      if (pgmoneta_lsn_map_get_downstream(map, rec->lsn, &known) == 0)
      {
         if (known != placed)
         {
            pgmoneta_log_error("wal_sender: inconsistent LSN map for upstream %X/%X: %X/%X != %X/%X",
                               LSN_FORMAT_ARGS(rec->lsn),
                               (uint32_t)(known >> 32), (uint32_t)known,
                               (uint32_t)(placed >> 32), (uint32_t)placed);
            return 1;
         }
      }
      else if (pgmoneta_lsn_map_put(map, rec->lsn, placed))
      {
         pgmoneta_log_error("wal_sender: could not persist LSN mapping for %X/%X",
                            LSN_FORMAT_ARGS(rec->lsn));
         return 1;
      }
   }

   if (placed_out)
   {
      *placed_out = placed;
   }

   return 0;
}

/* Emit everything the encoder has buffered as WALData messages, advancing the
 * downstream byte position tracked for the wire protocol. */
static int
sender_drain_encoder(int fd, struct wal_encoder* enc, uint64_t* stream_pos,
                     uint64_t deliver_from)
{
   while (true)
   {
      char* chunk = NULL;
      size_t len = 0;
      uint64_t base = *stream_pos;
      size_t off = 0;

      if (pgmoneta_wal_encoder_take(enc, &chunk, &len))
      {
         return 1;
      }
      if (!chunk)
      {
         break;
      }
      /* Re-encoding begins at the mapped record boundary, which can precede
       * an arbitrary requested resume LSN.  Build the complete record/page
       * layout, but put only the suffix the peer requested on the wire. */
      if (base + len <= deliver_from)
      {
         *stream_pos = base + len;
         free(chunk);
         continue;
      }
      if (base < deliver_from)
      {
         off = (size_t)(deliver_from - base);
      }
      while (off < len)
      {
         size_t n = len - off;
         if (n > (size_t)(64 * 1024 - 25))
         {
            n = 64 * 1024 - 25;
         }
         {
            char body[64 * 1024];
            uint32_t boff = 0;
            uint64_t start64 = be64(base + off);
            uint64_t end64 = be64(base + off + (uint64_t)n);
            int64_t now64 = be64((uint64_t)sender_now_usec());

            body[boff++] = XLOG_DATA;
            memcpy(body + boff, &start64, 8);
            boff += 8;
            memcpy(body + boff, &end64, 8);
            boff += 8;
            memcpy(body + boff, &now64, 8);
            boff += 8;
            memcpy(body + boff, chunk + off, n);
            boff += (uint32_t)n;

            if (sender_send_msg(fd, MSG_COPY_DATA, body, boff))
            {
               free(chunk);
               return 1;
            }
         }
         off += n;
      }
      *stream_pos = base + len;
      free(chunk);
   }
   return 0;
}

/* Non-blocking service of client messages during a COPY BOTH stream. Returns
 * 1 on a connection-level error, 0 otherwise. Sets *done on CopyDone or
 * Terminate. */
static int
sender_service_messages(int fd, char* map_path, struct lsn_map** map, uint64_t sent_lsn, bool* done)
{
   struct pollfd pfd;
   int pr;

   pfd.fd = fd;
   pfd.events = POLLIN;
   pfd.revents = 0;
   pr = poll(&pfd, 1, 0);
   if (pr < 0)
   {
      if (errno == EINTR)
      {
         return 0;
      }
      return 1;
   }
   if (pr == 0 || !(pfd.revents & (POLLIN | POLLHUP)))
   {
      return 0;
   }

   {
      char type;
      uint32_t len;

      if (sender_read_fully(fd, &type, 1))
      {
         return 1;
      }
      if (sender_read_fully(fd, &len, 4))
      {
         return 1;
      }
      len = be32(len);
      if (len < 4 || len > (uint32_t)1024 * 1024 * 64)
      {
         return 1;
      }
      {
         char* body = malloc(len - 4);
         if (!body)
         {
            return 1;
         }
         if (sender_read_fully(fd, body, len - 4))
         {
            free(body);
            return 1;
         }

         if (type == MSG_COPY_DATA)
         {
            if (len - 4 >= 25 && body[0] == STANDBY_STATUS)
            {
               uint64_t flush_lsn;
               uint64_t upstream_lsn;
               memcpy(&flush_lsn, body + 9, sizeof(flush_lsn));
               flush_lsn = be64(flush_lsn);
               /* A client may reconnect with any requested start LSN, but a
                * feedback message is only durable evidence for bytes this
                * connection has actually received. Never let an initial or
                * malformed status update advance local retention past the
                * XLogData high-water mark. */
               if (!pgmoneta_wal_sender_accepts_flush_lsn(flush_lsn, sent_lsn))
               {
                  pgmoneta_log_warn("wal_sender: ignoring downstream flush %X/%X beyond sent %X/%X",
                                    (uint32_t)(flush_lsn >> 32), (uint32_t)flush_lsn,
                                    (uint32_t)(sent_lsn >> 32), (uint32_t)sent_lsn);
               }
               else if (pgmoneta_lsn_map_get_upstream_at_or_before(*map, flush_lsn, &upstream_lsn) == 0)
               {
                  sender_persist_feedback(map_path, upstream_lsn, flush_lsn);
                  pgmoneta_log_debug("wal_sender: downstream flush %X/%X maps to upstream %X/%X",
                                     (uint32_t)(flush_lsn >> 32), (uint32_t)flush_lsn,
                                     (uint32_t)(upstream_lsn >> 32), (uint32_t)upstream_lsn);
               }
            }
         }
         else if (type == MSG_COPY_DONE)
         {
            free(body);
            sender_send_command_complete(fd, "COPY 0");
            sender_send_ready(fd);
            *done = true;
            return 0;
         }
         else if (type == MSG_TERMINATE)
         {
            free(body);
            *done = true;
            return 0;
         }
         free(body);
      }
   }
   return 0;
}

bool
pgmoneta_wal_sender_accepts_flush_lsn(uint64_t flush_lsn, uint64_t sent_lsn)
{
   return flush_lsn <= sent_lsn;
}

/*
 * sender_store_segment_last_lsn
 *
 * Return the downstream LSN of the last record inside the *sealed* store
 * segment `segno`. A completed store segment always ends with the
 * XLOG_SWITCH record the receiver's encoder wrote when it rotated past the
 * segment boundary, and that switch is exactly what a peer resuming on the
 * next segment boundary already holds as its last byte. The LSN map only
 * tracks translated *upstream* records, so the switch position is not in the
 * map - it must be recovered from the store file itself.
 *
 * Returns 0 with *lsn set on success, nonzero if the segment is missing,
 * compressed, unparsable or empty (caller falls back to the map floor).
 */
static int __attribute__((unused))
sender_store_segment_last_lsn(char* downstream_dir, uint64_t segno,
                              uint32_t seg_size, int srv, uint64_t* lsn)
{
   char path[PATH_MAX];
   struct walfile* wf = NULL;
   struct deque_iterator* iter = NULL;
   uint64_t last = 0;
   bool found = false;
   int rc = 1;

   if (!downstream_dir || !lsn || segno == 0)
   {
      return 1;
   }

   if (sender_segment_path(downstream_dir, segno - 1, seg_size, path, sizeof(path)))
   {
      return 1;
   }

   wf = calloc(1, sizeof(*wf));
   if (!wf)
   {
      return 1;
   }
   if (pgmoneta_deque_create(false, &wf->records) ||
       pgmoneta_deque_create(false, &wf->page_headers))
   {
      pgmoneta_destroy_walfile(wf);
      return 1;
   }

   /*
    * This probe parses a *downstream* store segment with the shared reader,
    * whose cross-file continuation state (partial_record) carries a
    * segment-boundary record from the preceding parse. On a seamless resume
    * the probe runs in the same phase-B pass that just parsed the run-up
    * segment (start_segno): its parse saved the run-up segment's crossing
    * record as partial (from_seg = start_segno). The probe's parse would
    * enter the reader guard with a from_seg mismatch and free that state,
    * so parsing the successor segment later finds no continuation and
    * silently drops the crossing record. Preserve the continuation state
    * across the probe and restore it afterwards.
    */
   struct partial_xlog_record saved_partial = {0};
   bool save_valid = false;
   if (partial_record != NULL)
   {
      saved_partial = *partial_record;
      /* detach: the probe's parse must see a clean reader */
      memset(partial_record, 0, sizeof(*partial_record));
      save_valid = true;
   }

   int parse_rc = pgmoneta_wal_parse_wal_file(path, srv, wf);

   if (save_valid && partial_record != NULL)
   {
      /* discard whatever the probe's parse left behind and re-attach the
       * upstream scan's continuation state */
      free(partial_record->xlog_record);
      free(partial_record->data_buffer);
      *partial_record = saved_partial;
   }

   if (parse_rc != 0)
   {
      pgmoneta_destroy_walfile(wf);
      return 1;
   }

   if (pgmoneta_deque_iterator_create(wf->records, &iter) == 0)
   {
      while (pgmoneta_deque_iterator_next(iter))
      {
         struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;

         if (rec && !rec->partial && rec->lsn > last)
         {
            last = rec->lsn;
            found = true;
         }
      }
      pgmoneta_deque_iterator_destroy(iter);
   }

   pgmoneta_destroy_walfile(wf);

   if (found)
   {
      *lsn = last;
      rc = 0;
   }
   return rc;
}

static int
sender_stream(int fd, int srv, char* downstream_dir, char* map_path, uint64_t start_lsn)
{
   static const uint32_t STREAM_CHUNK = 8192;
   struct lsn_map* map = NULL;
   char* wal_dir = NULL;
   uint64_t pos = start_lsn;
   uint64_t sent_lsn = 0;
   uint32_t seg_size = DEFAULT_WAL_SEG_SIZE;
   int64_t last_keepalive = 0;
   uint64_t last_map_offset = 0;
   bool done = false;

   sender_get_stream_info(srv, NULL, NULL, &seg_size);

   if (pgmoneta_lsn_map_create(map_path, &map))
   {
      pgmoneta_log_error("wal_sender: could not open LSN map %s", map_path);
      return 1;
   }
   last_map_offset = pgmoneta_lsn_map_durable_bytes(map);

   pgmoneta_log_info("wal_sender: streaming downstream from %X/%X",
                     (uint32_t)(pos >> 32), (uint32_t)pos);

   /* The raw archive is the only durable stream source. */
   goto phase_b_transcode;

   /* Phase A: serve the archived portion of the downstream stream verbatim
    * from the wal_store segment files, starting exactly at the requested LSN.
    * A physical standby may reconnect at any record boundary, so the stream
    * is never floored to a segment boundary. */
phase_a_serve:
   while (!done)
   {
      char path[PATH_MAX];
      struct stat st;
      uint64_t base;

      if (sender_service_messages(fd, map_path, &map, sent_lsn, &done))
      {
         done = false;
         goto sender_stream_done;
      }
      if (done)
      {
         break;
      }

      /* Keep the LSN-map view fresh while serving archived bytes. The map is
       * persisted append-only by the receiver and grows one entry per
       * translated record (tens of MB for a few GB of stream), so the refresh
       * is incremental: only the appended tail is re-parsed, never the whole
       * file. */
      if (sender_now_usec() - last_keepalive > 1000000)
      {
         sender_refresh_map(map_path, &map, &last_map_offset);
         last_keepalive = sender_now_usec();
      }

      base = (pos / seg_size) * seg_size;
      if (sender_segment_path(downstream_dir, pos / seg_size, seg_size, path, sizeof(path)) ||
          stat(path, &st) != 0 ||
          pos >= base + (uint64_t)st.st_size)
      {
         /* The store has no more bytes at pos: either the segment for pos
          * has not been sealed yet (bootstrap, or the receiver is mid-seal),
          * or the stream is genuinely at the seal frontier. In the former
          * case the correct move is to wait for the seal and re-enter the
          * serving loop; only a store head that stays frozen gives way to
          * phase B (live transcode). Breaking into phase B while the store
          * is still empty or while the map lags pos would resolve a bogus
          * seam (e.g. the stream origin pos=0 degenerates to "upstream
          * segment 1") and surface "pruned or missing" fatals. */
         {
            bool seal_pending = false;
            uint64_t last_seen_top = 0;
            int64_t wait_start = sender_now_usec();
            int64_t hard_deadline = sender_now_usec() + 60LL * 1000000;

            while (!done)
            {
               /* The segment for pos may have just been sealed: serve it. */
               if (sender_segment_path(downstream_dir, pos / seg_size, seg_size, path, sizeof(path)) == 0 &&
                   stat(path, &st) == 0)
               {
                  seal_pending = true;
                  break;
               }

               /* refresh the in-memory map incrementally so a later phase-B
                * floor resolution sees the newest entries */
               sender_refresh_map(map_path, &map, &last_map_offset);

               /* Watch the persisted map top: the receiver flushes it at a
                * 1s cadence while translating. A top that keeps advancing
                * means the seal for pos is coming; a top frozen at/below pos
                * for the stall quota (and pos > 0) means the store head is
                * really parked at the frontier. */
               {
                  uint64_t top = 0;
                  if (sender_map_top(map_path, &top) == 0)
                  {
                     if (top != last_seen_top)
                     {
                        last_seen_top = top;
                        wait_start = sender_now_usec();
                     }
                  }
               }

               /* Only a map top frozen strictly BELOW pos is a genuine parked
                * store head. While the top tracks at/above pos the seal of the
                * segment covering pos is still in progress, so the right move
                * is to keep waiting for the file to appear. The previous
                * time-only quota fired spuriously during the receiver's
                * ordinary between-segment pauses (the map is flushed once per
                * translated segment, so its top legitimately freezes for the
                * duration of each seal) and stranded the stream in the slow
                * phase-B fallback even though the sealed store was racing
                * ahead. */
               if (pos > 0 && last_seen_top > 0 && last_seen_top < pos && sender_now_usec() - wait_start > 3000000)
               {
                  pgmoneta_log_warn("wal_sender: store head frozen at map top %X/%X below %X/%X; "
                                    "continuing with live transcode",
                                    (uint32_t)(last_seen_top >> 32), (uint32_t)last_seen_top,
                                    (uint32_t)(pos >> 32), (uint32_t)pos);
                  break;
               }
               if (sender_now_usec() > hard_deadline)
               {
                  pgmoneta_log_warn("wal_sender: store did not reach %X/%X within 60s; "
                                    "continuing with live transcode",
                                    (uint32_t)(pos >> 32), (uint32_t)pos);
                  break;
               }
               if (sender_service_messages(fd, map_path, &map, sent_lsn, &done))
               {
                  done = false;
                  goto sender_stream_done;
               }
               SLEEP(10000L);
            }
            if (done)
            {
               break;
            }
            if (seal_pending)
            {
               /* the segment for pos is being sealed into place: re-enter the
                * serving loop and pick it up from the top */
               continue;
            }
            /* genuine frontier: continue with the live tail */
            break;
         }
      }

      {
         uint64_t fileoff = pos - base;
         uint64_t avail = (uint64_t)st.st_size - fileoff;
         uint32_t chunk = (uint32_t)(avail < STREAM_CHUNK ? avail : STREAM_CHUNK);
         char body[64 * 1024];
         uint32_t boff = 0;
         uint64_t start64;
         uint64_t end64;
         int64_t now64;
         ssize_t got;
         int rfd;

         rfd = open(path, O_RDONLY);
         if (rfd < 0)
         {
            break;
         }
         got = pread(rfd, body + 25, chunk, (off_t)fileoff);
         close(rfd);
         if (got <= 0)
         {
            break;
         }

         start64 = be64(pos);
         end64 = be64(pos + (uint64_t)got);
         now64 = be64((uint64_t)sender_now_usec());

         body[boff++] = XLOG_DATA;
         memcpy(body + boff, &start64, 8);
         boff += 8;
         memcpy(body + boff, &end64, 8);
         boff += 8;
         memcpy(body + boff, &now64, 8);
         boff += 8;

         if (sender_send_msg(fd, MSG_COPY_DATA, body, boff + (uint32_t)got))
         {
            done = true;
            break;
         }

         pos += (uint64_t)got;
         sent_lsn = pos;
         continue;
      }
   }

phase_b_transcode:
   /* Decode, translate and encode retained upstream WAL on demand. */
   if (!done)
   {
      wal_dir = pgmoneta_get_server_wal(srv);
      char up_dir[PATH_MAX];
      uint64_t floor_up = 0;
      uint64_t floor_down = 0;
      uint64_t run_start_up = 0;
      uint64_t start_segno = 0;
      uint64_t last_consumed = 0;
      uint64_t last_complete = 0;
      uint64_t active_segno = UINT64_MAX;
      uint64_t active_size = 0;
      uint64_t stream_pos = pos;
      uint64_t last_scan = 0;
      bool have_floor = false;
      bool first_segment_pending = false;
      struct sender_encode_ctx ctx;

      memset(&ctx, 0, sizeof(ctx));
      ctx.start_lsn = pos;
      ctx.seg_size = seg_size;
      {
         struct main_configuration* config = (struct main_configuration*)shmem;
         ctx.checksums = config->common.servers[srv].checksums;
      }

      if (!wal_dir)
      {
         pgmoneta_log_error("wal_sender: no WAL directory for server %d", srv);
         done = false;
         goto sender_stream_done;
      }
      pgmoneta_snprintf(up_dir, sizeof(up_dir), "%s%s",
                        wal_dir, wal_dir[strlen(wal_dir) - 1] == '/' ? "" : "/");

      if (pos > 0)
      {
         /* Resolve the resume point D=pos backwards through the map: the
          * entry with the greatest downstream position at or below D anchors
          * the run-up, and its downstream start is the xl_prev the peer
          * expects on the first record delivered from D onwards. */
         if (pgmoneta_lsn_map_get_upstream_at_or_before(map, pos, &floor_up) == 0 &&
             pgmoneta_lsn_map_get_downstream(map, floor_up, &floor_down) == 0)
         {
            have_floor = true;
         }
         else
         {
            pgmoneta_log_warn("wal_sender: no LSN map entry at or before %X/%X; live transcode cannot place records",
                              (uint32_t)(pos >> 32), (uint32_t)pos);
            done = false;
            goto sender_stream_done;
         }
      }
      else if (pgmoneta_lsn_map_get_first(map, &floor_up, &floor_down) == 0)
      {
         /* Stream origin: a peer starting at 0/0 must be served from the very
          * first translated record, whose upstream LSN anchors the floor. */
         have_floor = true;
      }
      else if (pos == 0)
      {
         /* First downstream connection: no cache-derived map is needed.
          * Start from the first retained raw record and build the map while
          * encoding it for this client. */
         have_floor = true;
         floor_up = 0;
         floor_down = 0;
      }
      else
      {
         pgmoneta_log_warn("wal_sender: no LSN map entry at or before %X/%X; live transcode cannot place records",
                           (uint32_t)(pos >> 32), (uint32_t)pos);
         done = false;
         goto sender_stream_done;
      }

      run_start_up = have_floor ? floor_up : 0;
      start_segno = run_start_up / seg_size;
      /* Rebuild from the record whose downstream span contains the requested
       * LSN, then sender_drain_encoder() discards the already received prefix.
       * This is what makes a reconnect inside a record deterministic without
       * a translated-WAL cache. */
      if (pos > 0)
      {
         ctx.start_lsn = floor_down;
         stream_pos = floor_down;
         last_consumed = run_start_up > 0 ? run_start_up - 1 : 0;
      }
      else
      {
         last_consumed = 0;
      }
      /* Sealed frontier: the newest sealed upstream segment already served.
       * Anchor it just before the first segment this stream must serve so the
       * recoverable-data-loss guard treats start_segno itself as required. */
      last_complete = start_segno > 0 ? start_segno - 1 : 0;
      first_segment_pending = start_segno == 0;

      while (!done)
      {
         int64_t now = sender_now_usec();

         if (sender_service_messages(fd, map_path, &map, sent_lsn, &done))
         {
            done = false;
            goto phase_b_done;
         }
         if (done)
         {
            break;
         }

         /* Fast-path hand-back: as soon as the sealed store has caught up with
          * the live tail, return to phase A which serves the sealed files
          * directly. Phase B is only the fallback for a store head parked at
          * the frontier; without this the stream stays in the slow transcode
          * path forever even though the receiver keeps sealing the store far
          * ahead. The encoder's emitted bytes are identical to the store's
          * (the seam contract this whole fallback is built on), so resuming
          * phase A at stream_pos is seamless - any not-yet-drained encoder
          * tail is discarded and re-served by phase A from the store. */
         {
            char spath[PATH_MAX];
            struct stat sst;
            if (false && sender_segment_path(downstream_dir, stream_pos / seg_size, seg_size, spath, sizeof(spath)) == 0 &&
                stat(spath, &sst) == 0 &&
                stream_pos < (stream_pos / seg_size) * seg_size + (uint64_t)sst.st_size)
            {
               pgmoneta_log_info("wal_sender: sealed store caught up at %X/%X; returning to phase A",
                                 (uint32_t)(stream_pos >> 32), (uint32_t)stream_pos);
               pgmoneta_wal_encoder_destroy(ctx.real);
               ctx.real = NULL;
               pos = stream_pos;
               goto phase_a_serve;
            }
         }

         /* rescan the upstream WAL directory for new or changed segments */
         if (now - last_scan >= 100000)
         {
            struct sender_up_seg* segs = NULL;
            int count = 0;
            int i;

            if (sender_up_collect(up_dir, seg_size, &segs, &count))
            {
               pgmoneta_log_warn("wal_sender: could not scan upstream WAL directory %s", up_dir);
               last_scan = now;
               continue;
            }
            last_scan = now;

            for (i = 0; i < count; i++)
            {
               char path[PATH_MAX];
               bool is_partial = false;
               uint64_t fsize = 0;
               struct walfile* wf = NULL;
               struct deque_iterator* iter = NULL;
               bool fatal = false;
               uint64_t s = segs[i].segno;

               if (s < start_segno)
               {
                  continue;
               }
               if (s <= last_complete && !(first_segment_pending && s == 0))
               {
                  /* completed, and the .partial that may have preceded it was
                   * already consumed */
                  continue;
               }

               /* Recoverable-data-loss guard: a *sealed* gap between the served
                * frontier and s means the raw store lost WAL the stream cannot
                * proceed without. Surface the spec-mandated "take fresh base
                * backup" failure to the peer instead of silently skipping or
                * stalling forever. A .partial at the frontier is fine - it is
                * the in-progress head and will be served once sealed. */
               if (!segs[i].active && s > last_complete + 1)
               {
                  char probe[PATH_MAX];
                  bool probe_partial = false;
                  uint64_t probe_size = 0;

                  if (sender_up_expect_segment(up_dir, last_complete + 1, seg_size,
                                               probe, sizeof(probe),
                                               &probe_partial, &probe_size))
                  {
                     pgmoneta_log_warn("wal_sender: upstream WAL segment %" PRIu64 " pruned or missing; "
                                       "the replica cannot be caught up without a fresh base backup",
                                       last_complete + 1);
                     sender_send_error(fd, "wal_sender: upstream WAL segment pruned or missing; "
                                           "take a fresh base backup");
                     free(segs);
                     done = false;
                     goto phase_b_done;
                  }
               }

               if (sender_up_segment_path(up_dir, s, seg_size, path, sizeof(path),
                                          &is_partial, &fsize))
               {
                  /* vanished between the list and the pick. A transient
                   * absence (compress/rename handover) rides out a bounded
                   * retry; only a persistent disappearance of the required
                   * frontier segment is recoverable data loss. */
                  if (!segs[i].active && s == last_complete + 1)
                  {
                     if (sender_up_expect_segment(up_dir, s, seg_size, path, sizeof(path),
                                                  &is_partial, &fsize))
                     {
                        pgmoneta_log_warn("wal_sender: upstream WAL segment %" PRIu64 " pruned or missing; "
                                          "the replica cannot be caught up without a fresh base backup",
                                          s);
                        sender_send_error(fd, "wal_sender: upstream WAL segment pruned or missing; "
                                              "take a fresh base backup");
                        free(segs);
                        done = false;
                        goto phase_b_done;
                     }
                  }
                  else
                  {
                     /* sealed skip or non-frontier segment: re-probe once to
                      * ride out the transient, then move on either way */
                     if (sender_up_segment_path(up_dir, s, seg_size, path, sizeof(path),
                                                &is_partial, &fsize))
                     {
                        continue;
                     }
                  }
               }
               if (is_partial && s == active_segno && fsize == active_size)
               {
                  continue; /* unchanged since the last parse */
               }

               if (pgmoneta_ends_with(path, ".zstd"))
               {
                  /* The WAL client seals every raw segment compressed, so a
                   * sealed-but-compressed file is a present segment: decompress
                   * into a scratch dir and parse that instead.
                   *
                   * Decompress from a COPY of the archive: pgmoneta's
                   * decompressor unlinks its input on success, and the
                   * retained upstream archive must survive for the receiver
                   * (it is only ever removed by prune_local_wal).  A separate
                   * scratch dir keeps this forked sender clear of the
                   * receiver's in-process scratch files. */
                  char decomp_dir[PATH_MAX];
                  char decompressed[PATH_MAX];
                  char scratch[PATH_MAX];
                  char bare[PATH_MAX];
                  char* base = strrchr(path, '/');
                  size_t blen;

                  base = base ? base + 1 : path;
                  blen = strlen(base);
                  if (blen > 5)
                  {
                     blen -= 5;
                  }
                  memcpy(bare, base, blen);
                  bare[blen] = '\0';

                  pgmoneta_snprintf(decomp_dir, sizeof(decomp_dir), "%s/walbridge-decomp-sender", up_dir);
                  pgmoneta_mkdir(decomp_dir);
                  pgmoneta_snprintf(scratch, sizeof(scratch), "%s/%s.zstd", decomp_dir, bare);
                  if (pgmoneta_copy_file(path, scratch, NULL))
                  {
                     pgmoneta_log_error("wal_sender: could not copy upstream WAL segment %s; "
                                        "the replica cannot be caught up without a fresh base backup",
                                        path);
                     sender_send_error(fd, "wal_sender: upstream WAL segment corrupt; "
                                           "take a fresh base backup");
                     free(segs);
                     done = false;
                     goto phase_b_done;
                  }
                  pgmoneta_snprintf(decompressed, sizeof(decompressed), "%s/%s", decomp_dir, bare);
                  if (pgmoneta_decompress_file(scratch, decompressed, COMPRESSION_NONE, NULL))
                  {
                     pgmoneta_log_error("wal_sender: could not decompress upstream WAL segment %s; "
                                        "the replica cannot be caught up without a fresh base backup",
                                        path);
                     sender_send_error(fd, "wal_sender: upstream WAL segment corrupt; "
                                           "take a fresh base backup");
                     free(segs);
                     done = false;
                     goto phase_b_done;
                  }
                  pgmoneta_snprintf(path, sizeof(path), "%s", decompressed);
                  is_partial = false;
               }

               wf = calloc(1, sizeof(*wf));
               if (!wf)
               {
                  free(segs);
                  done = false;
                  goto phase_b_done;
               }
               if (pgmoneta_deque_create(false, &wf->records) ||
                   pgmoneta_deque_create(false, &wf->page_headers))
               {
                  pgmoneta_destroy_walfile(wf);
                  free(segs);
                  done = false;
                  goto phase_b_done;
               }

               if (pgmoneta_wal_parse_wal_file(path, srv, wf) != 0)
               {
                  if (!is_partial)
                  {
                     /* A sealed segment that cannot be parsed is corrupt WAL the
                      * stream requires - surfacing "take fresh base backup" is
                      * the only honest recovery. */
                     pgmoneta_log_error("wal_sender: upstream WAL segment %s corrupt or truncated; "
                                        "the replica cannot be caught up without a fresh base backup",
                                        path);
                     sender_send_error(fd, "wal_sender: upstream WAL segment corrupt; "
                                           "take a fresh base backup");
                     pgmoneta_destroy_walfile(wf);
                     free(segs);
                     done = false;
                     goto phase_b_done;
                  }
                  pgmoneta_log_warn("wal_sender: could not parse upstream %s; skipping", path);
                  pgmoneta_destroy_walfile(wf);
                  continue;
               }

               if (!ctx.real)
               {
                  /* first usable file: capture the stream parameters and
                   * bootstrap the encoder for this resume point */
                  char* name = strrchr(path, '/');
                  uint32_t tl = 1;

                  ctx.sysid = wf->long_phd->xlp_sysid;
                  ctx.blksz = wf->long_phd->xlp_xlog_blcksz;
                  if (wf->long_phd->xlp_seg_size)
                  {
                     ctx.seg_size = wf->long_phd->xlp_seg_size;
                  }
                  sender_up_parse_name(name ? name + 1 : path, NULL, NULL, &tl, ctx.seg_size);
                  ctx.tli = tl;

                  if (pos == 0)
                  {
                     /* genuine fresh stream: long page header at LSN 0 */
                     if (pgmoneta_wal_encoder_create(ctx.sysid, ctx.seg_size, ctx.blksz,
                                                     ctx.tli, ctx.checksums, &ctx.real))
                     {
                        pgmoneta_destroy_walfile(wf);
                        free(segs);
                        done = false;
                        goto phase_b_done;
                     }
                  }
                  else
                  {
                     uint64_t resume_prev = 0;
                     uint64_t previous_up = 0;

                     /* resume exactly at the peer's position. The next record
                      * starts at D and must chain to the last record the peer
                      * already holds. When D lands exactly on a segment
                      * boundary, that last record is the XLOG_SWITCH the
                      * receiver's encoder wrote when it sealed the previous
                      * store segment - the LSN map (which tracks translated
                      * upstream records) only knows the record *before* the
                      * switch, so anchoring on floor_down would skip the
                      * switch and break the peer's prev-link chain. Recover
                      * the switch position from the sealed store segment. */
                     if (floor_down > 0 &&
                         pgmoneta_lsn_map_get_upstream_at_or_before(map, floor_down - 1,
                                                                    &previous_up) == 0)
                     {
                        pgmoneta_lsn_map_get_downstream(map, previous_up, &resume_prev);
                     }

                     if (pgmoneta_wal_encoder_create(ctx.sysid, ctx.seg_size, ctx.blksz,
                                                     ctx.tli, ctx.checksums, &ctx.real))
                     {
                        pgmoneta_destroy_walfile(wf);
                        free(segs);
                        done = false;
                        goto phase_b_done;
                     }
                     if (pgmoneta_wal_encoder_resume(ctx.real, resume_prev, ctx.start_lsn))
                     {
                        pgmoneta_destroy_walfile(wf);
                        done = false;
                        goto phase_b_done;
                     }
                  }
               }

               if (pgmoneta_deque_iterator_create(wf->records, &iter) == 0)
               {
                  while (pgmoneta_deque_iterator_next(iter))
                  {
                     struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;

                     if (!rec || rec->partial || rec->lsn < run_start_up || rec->lsn <= last_consumed)
                     {
                        continue;
                     }
                     if (sender_feed_record(rec, map, &ctx, NULL))
                     {
                        fatal = true;
                        break;
                     }
                     last_consumed = rec->lsn;
                  }
                  pgmoneta_deque_iterator_destroy(iter);
               }

               pgmoneta_destroy_walfile(wf);

               if (fatal)
               {
                  free(segs);
                  done = false;
                  goto phase_b_done;
               }

               if (is_partial)
               {
                  active_segno = s;
                  active_size = fsize;
               }
               else if (s > last_complete)
               {
                  last_complete = s;
               }
               first_segment_pending = false;

               /* Serve incrementally: translate at most one upstream segment
                * per scan pass so the peer receives bytes as soon as they are
                * encoded. Re-encoding the whole backlog in a single pass would
                * delay the first byte by the entire backlog's transcode time
                * (the migration engine runs at storage pace), stranding the
                * stream even though bytes are ready; it also lets the store
                * catch up between segments so the phase-A hand-back fires
                * promptly instead of after the backlog is exhausted. */
               break;
            }
            free(segs);
         }

         /* hand the newly encoded bytes to the peer */
         if (ctx.real)
         {
            if (sender_drain_encoder(fd, ctx.real, &stream_pos, pos))
            {
               done = false;
               goto phase_b_done;
            }
            sent_lsn = stream_pos;
         }

         /* periodic keepalive and incremental LSN-map refresh so feedback
          * stays current (see sender_refresh_map: the map can reach tens of
          * MB, so only the appended tail is re-parsed). */
         if (sender_now_usec() - last_keepalive > 1000000)
         {
            char body[18];
            uint64_t walend = be64(stream_pos > 0 ? stream_pos - 1 : 0);
            int64_t nowb = be64((uint64_t)sender_now_usec());

            sender_refresh_map(map_path, &map, &last_map_offset);

            body[0] = PRIMARY_KEEPALIVE;
            memcpy(body + 1, &walend, 8);
            memcpy(body + 9, &nowb, 8);
            body[17] = 0;
            if (sender_send_msg(fd, MSG_COPY_DATA, body, 18))
            {
               done = false;
               break;
            }
            last_keepalive = sender_now_usec();
         }

         usleep(10000);
      }

phase_b_done:
      pgmoneta_wal_encoder_destroy(ctx.real);
   }

sender_stream_done:
   free(wal_dir);
   /* A clean CopyDone/Terminate may arrive less than one second after the
    * first encoded record, before lsn_map_put's throttle fires.  Persist the
    * sender-built tail before dropping the in-memory map so reconnect always
    * has a durable raw-record anchor. */
   if (map && pgmoneta_lsn_map_flush(map))
   {
      pgmoneta_log_error("wal_sender: could not flush LSN map %s", map_path);
      done = false;
   }
   pgmoneta_lsn_map_destroy(map);
   return done ? 0 : 1;
}

static int
sender_handle_query(int fd, int srv, char* downstream_dir, char* map_path, char* query)
{
   /* trim whitespace/semicolon */
   size_t len = strlen(query);
   while (len > 0 && (query[len - 1] == ';' || query[len - 1] == ' ' || query[len - 1] == '\n'))
   {
      query[--len] = '\0';
   }

   if (strcasecmp(query, "IDENTIFY_SYSTEM") == 0)
   {
      return sender_send_identify_system(fd, srv, map_path);
   }
   else if (strncasecmp(query, "READ_REPLICATION_SLOT", 21) == 0)
   {
      return sender_send_read_replication_slot(fd, map_path);
   }
   else if (strncasecmp(query, "START_REPLICATION", 17) == 0)
   {
      uint64_t start_lsn = 0;
      uint32_t timeline = 0;
      char copyboth[3] = {0, 0, 0};

      if (sender_parse_start_replication(query, &start_lsn, &timeline))
      {
         sender_send_error_ready(fd, "walbridge: could not parse START_REPLICATION request");
         return 0;
      }

      pgmoneta_log_info("wal_sender: START_REPLICATION at %X/%X (timeline %u)",
                        (uint32_t)(start_lsn >> 32), (uint32_t)start_lsn, timeline);

      /* CopyBothResponse: overall format (text) + zero columns */
      if (sender_send_msg(fd, MSG_COPY_BOTH, copyboth, 3))
      {
         return 1;
      }

      return sender_stream(fd, srv, downstream_dir, map_path, start_lsn);
   }
   else if (strncasecmp(query, "TIMELINE_HISTORY", 16) == 0)
   {
      sender_send_error_ready(fd, "walbridge: timeline history files are not supported");
      return 0;
   }
   else if (strcasecmp(query, "SHOW") == 0 || strncasecmp(query, "SHOW ", 5) == 0)
   {
      char* name = query + 5;
      while (*name == ' ')
      {
         name++;
      }
      if (strncasecmp(name, "integer_datetimes", 18) == 0)
      {
         return sender_send_show(fd, name, "on");
      }
      else if (strncasecmp(name, "wal_level", 10) == 0)
      {
         struct main_configuration* config = (struct main_configuration*)shmem;
         return sender_send_show(fd, name, config->common.servers[srv].valid ? "replica" : "minimal");
      }
      else if (strncasecmp(name, "wal_segment_size", 17) == 0)
      {
         struct main_configuration* config = (struct main_configuration*)shmem;
         char size[32];
         pgmoneta_snprintf(size, sizeof(size), "%dMB",
                           config->common.servers[srv].wal_size / 1024 / 1024);
         return sender_send_show(fd, name, size);
      }
      else if (strncasecmp(name, "max_wal_senders", 16) == 0)
      {
         return sender_send_show(fd, name, "10");
      }
      else if (strncasecmp(name, "server_version", 15) == 0)
      {
         return sender_send_show(fd, name, "19.0");
      }
      else if (strncasecmp(name, "server_version_num", 19) == 0)
      {
         return sender_send_show(fd, name, "190000");
      }
      else if (strncasecmp(name, "data_checksums", 15) == 0)
      {
         struct main_configuration* config = (struct main_configuration*)shmem;
         return sender_send_show(fd, name, config->common.servers[srv].checksums ? "on" : "off");
      }
      else if (strncasecmp(name, "standby_status_update", 22) == 0)
      {
         return sender_send_show(fd, name, "on");
      }
      else if (strncasecmp(name, "data_directory_mode", 19) == 0)
      {
         return sender_send_show(fd, name, "0700");
      }
      sender_send_error_ready(fd, "walbridge: unsupported SHOW parameter");
      return 0;
   }

   sender_send_error_ready(fd, "walbridge: unsupported query");
   return 0;
}

static int
sender_handle_connection(int fd, int srv, char* downstream_dir, char* map_path)
{
   if (sender_handle_startup(fd, srv))
   {
      close(fd);
      return 1;
   }

   while (true)
   {
      char type;
      uint32_t len;
      char* body = NULL;

      if (sender_read_fully(fd, &type, 1))
      {
         close(fd);
         return 1;
      }

      if (type == MSG_TERMINATE)
      {
         close(fd);
         return 0;
      }

      if (sender_read_fully(fd, &len, 4))
      {
         close(fd);
         return 1;
      }
      len = be32(len);
      if (len < 4 || len > (uint32_t)1024 * 1024 * 64)
      {
         close(fd);
         return 1;
      }

      body = malloc(len - 4);
      if (!body)
      {
         close(fd);
         return 1;
      }
      if (sender_read_fully(fd, body, len - 4))
      {
         free(body);
         close(fd);
         return 1;
      }

      if (type == MSG_SIMPLE_QUERY)
      {
         body[len - 4 - 1] = '\0'; /* ensure termination */
         sender_handle_query(fd, srv, downstream_dir, map_path, body);
         free(body);
      }
      else
      {
         /* extended protocol / anything else: not supported (PoC) */
         pgmoneta_log_warn("wal_sender: unsupported message type %c (0x%02X)", type, (unsigned char)type);
         sender_send_error(fd, "walbridge: only simple query protocol is supported");
         free(body);
         close(fd);
         return 1;
      }
   }
}

int
pgmoneta_walbridge_run_sender(int srv, char* downstream_dir, char* map_path)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   int* fds = NULL;
   int length = 0;

   /* Reap per-connection children automatically */
   signal(SIGCHLD, SIG_IGN);

   if (pgmoneta_bind(config->walbridge_host[0] ? config->walbridge_host : "*", config->walbridge, &fds, &length))
   {
      pgmoneta_log_error("wal_sender: could not bind walbridge port %d", config->walbridge);
      return 1;
   }

   pgmoneta_log_info("wal_sender: listening on %s:%d", config->walbridge_host[0] ? config->walbridge_host : "*", config->walbridge);

   while (config->running)
   {
      int client = -1;

      for (int i = 0; i < length; i++)
      {
         client = accept(fds[i], NULL, NULL);
         if (client != -1)
         {
            break;
         }
      }

      if (client == -1)
      {
         if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
         {
            /* the listener is non-blocking: idle with no pending connection
             * would otherwise busy-spin at line rate, pinning a CPU core */
            usleep(250000);
            continue;
         }
         pgmoneta_log_warn("wal_sender: accept: %m");
         usleep(500000);
         continue;
      }

      pgmoneta_log_info("wal_sender: accepted connection");

      /* Serve each client in its own process so one stalled peer cannot block
       * the accept loop for everyone else. */
      {
         pid_t child = fork();
         if (child == 0)
         {
            for (int i = 0; i < length; i++)
            {
               close(fds[i]);
            }
            sender_handle_connection(client, srv, downstream_dir, map_path);
            pgmoneta_log_info("wal_sender: connection closed");
            exit(0);
         }
         else if (child > 0)
         {
            close(client);
         }
         else
         {
            pgmoneta_log_warn("wal_sender: fork failed: %m");
            close(client);
         }
      }
   }

   for (int i = 0; i < length; i++)
   {
      close(fds[i]);
   }
   free(fds);

   return 0;
}
