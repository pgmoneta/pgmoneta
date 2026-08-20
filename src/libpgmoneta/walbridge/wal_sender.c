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
#include <walfile.h>
#include <walfile/wal_reader.h>
#include <walbridge/lsn_map.h>
#include <walbridge/wal_sender.h>
#include <walbridge/wal_store.h>

/* system */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define PROTOCOL_VERSION_3   196608
#define DEFAULT_WAL_SEG_SIZE (16 * 1024 * 1024)

#define MSG_ROW_DESCRIPTION  'T'
#define MSG_DATA_ROW         'D'
#define MSG_COMMAND_COMPLETE 'C'
#define MSG_READY_FOR_QUERY  'Z'
#define MSG_AUTHENTICATION   'R'
#define MSG_COPY_BOTH        'W'
#define MSG_COPY_DATA        'd'
#define MSG_COPY_DONE        'c'
#define MSG_ERROR            'E'
#define MSG_SIMPLE_QUERY     'Q'
#define MSG_TERMINATE        'X'
#define MSG_PARSE            'P'
#define MSG_BIND             'B'
#define MSG_DESCRIBE         'D'
#define MSG_EXECUTE          'E'
#define MSG_SYNC             'S'
#define MSG_CLOSE            'C'
#define MSG_FLUSH            'H'

#define XLOG_DATA            'w'
#define PRIMARY_KEEPALIVE    'k'
#define STANDBY_STATUS       'r'

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
sender_segment_path(char* dir, uint64_t segno, char* out, size_t outsz)
{
   DIR* d = opendir(dir);
   struct dirent* e;
   char best[PATH_MAX] = "";

   if (!d)
   {
      return 1;
   }

   while ((e = readdir(d)) != NULL)
   {
      unsigned int tli, log, seg;
      uint64_t n;

      if (strlen(e->d_name) != 24)
      {
         continue;
      }
      if (sscanf(e->d_name, "%08X%08X%08X", &tli, &log, &seg) != 3)
      {
         continue;
      }
      n = ((uint64_t)log << 32) | seg;
      if (n == segno)
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

static int
sender_send_command_complete(int fd, const char* tag)
{
   size_t len = strlen(tag) + 1;
   return sender_send_msg(fd, MSG_COMMAND_COMPLETE, tag, (uint32_t)len);
}

/* ---- startup / authentication ---- */

static int
sender_handle_startup(int fd)
{
   uint32_t len;
   uint32_t version;
   char* body = NULL;

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

   body = malloc(len - 8);
   if (!body)
   {
      return 1;
   }
   if (sender_read_fully(fd, body, len - 8))
   {
      free(body);
      return 1;
   }
   free(body);

   if (version == 80877103) /* SSLRequest */
   {
      char no[1] = {'N'};
      sender_write_fully(fd, no, 1);
      return sender_handle_startup(fd);
   }

   if (version != PROTOCOL_VERSION_3)
   {
      sender_send_error(fd, "walbridge: unsupported protocol version");
      return 1;
   }

   /* trust authentication (PoC): AuthenticationOk */
   {
      uint32_t auth = be32(0);
      if (sender_send_msg(fd, MSG_AUTHENTICATION, &auth, 4))
      {
         return 1;
      }
   }

   return sender_send_ready(fd);
}

/* ---- IDENTIFY_SYSTEM ---- */

static int
sender_get_stream_info(char* downstream_dir, uint64_t* sysid, uint32_t* tli, uint32_t* seg_size)
{
   DIR* dir = opendir(downstream_dir);
   struct dirent* ent;
   char path[PATH_MAX];
   struct walfile* wf = NULL;
   char first[PATH_MAX] = "";

   if (!dir)
   {
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
      return 1;
   }

   pgmoneta_snprintf(path, sizeof(path), "%s/%s", downstream_dir, first);

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

static int
sender_send_identify_system(int fd, char* downstream_dir)
{
   uint64_t sysid = 0;
   uint32_t tli = 1;
   char sysid_str[32];
   char tli_str[16];
   char xlogpos[24];
   char body[1024];
   uint32_t off = 0;

   if (sender_get_stream_info(downstream_dir, &sysid, &tli, NULL))
   {
      sysid = 0;
      tli = 1;
   }

   pgmoneta_snprintf(sysid_str, sizeof(sysid_str), "%llu", (unsigned long long)sysid);
   pgmoneta_snprintf(tli_str, sizeof(tli_str), "%u", tli);
   pgmoneta_snprintf(xlogpos, sizeof(xlogpos), "%X/%X", 0, 0);

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
      int32_t c4len = be32(0);

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

/* ---- START_REPLICATION ---- */

static int
sender_parse_start_replication(char* query, uint64_t* start_lsn, uint32_t* timeline)
{
   char* p = NULL;
   char* tok = NULL;
   unsigned int hi = 0, lo = 0;
   int tli = 0;

   *timeline = 0;

   p = strstr(query, "PHYSICAL");
   if (!p)
   {
      return 1;
   }
   p += strlen("PHYSICAL");
   while (*p == ' ')
   {
      p++;
   }

   if (sscanf(p, "%X/%X", &hi, &lo) != 2)
   {
      return 1;
   }
   *start_lsn = ((uint64_t)hi << 32) | lo;

   tok = strstr(p, "TIMELINE");
   if (tok && sscanf(tok + strlen("TIMELINE"), "%d", &tli) == 1)
   {
      *timeline = (uint32_t)tli;
   }

   return 0;
}

static int
sender_stream(int fd, int srv, char* downstream_dir, char* map_path, uint64_t start_lsn)
{
   struct lsn_map* map = NULL;
   struct lsn_map* fresh = NULL;
   uint64_t target = 0;
   uint64_t last_sent = 0;
   uint64_t segno;
   uint32_t seg_size = DEFAULT_WAL_SEG_SIZE;
   int64_t last_keepalive = 0;
   bool done = false;

   sender_get_stream_info(downstream_dir, NULL, NULL, &seg_size);

   if (lsn_map_create(map_path, &map))
   {
      pgmoneta_log_error("wal_sender: could not open LSN map %s", map_path);
      return 1;
   }

   if (lsn_map_get_downstream_at_or_before(map, start_lsn, NULL, &target))
   {
      pgmoneta_log_error("wal_sender: no downstream record found at or before requested %X/%X",
                         (uint32_t)(start_lsn >> 32), (uint32_t)start_lsn);
      lsn_map_destroy(map);
      return 1;
   }

   last_sent = target > 0 ? target - 1 : 0;
   pgmoneta_log_info("wal_sender: streaming downstream from %X/%X (requested upstream %X/%X)",
                     (uint32_t)(target >> 32), (uint32_t)target,
                     (uint32_t)(start_lsn >> 32), (uint32_t)start_lsn);

   while (!done)
   {
      struct pollfd pfd;
      int pr;
      bool sent_anything = false;

      /* service client messages (status updates, CopyDone, terminate) */
      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      pr = poll(&pfd, 1, 0);
      if (pr < 0)
      {
         if (errno == EINTR)
         {
            continue;
         }
         break;
      }
      if (pr > 0 && (pfd.revents & (POLLIN | POLLHUP)))
      {
         char type;
         uint32_t len;

         if (sender_read_fully(fd, &type, 1))
         {
            break;
         }
         if (sender_read_fully(fd, &len, 4))
         {
            break;
         }
         len = be32(len);
         if (len < 4 || len > (uint32_t)1024 * 1024 * 64)
         {
            break;
         }
         {
            char* body = malloc(len - 4);
            if (!body)
            {
               break;
            }
            if (sender_read_fully(fd, body, len - 4))
            {
               free(body);
               break;
            }

            if (type == MSG_COPY_DATA)
            {
               /* standby status update ('r') or other copy data: ignore */
               if (len - 4 > 0 && body[0] == STANDBY_STATUS)
               {
                  pgmoneta_log_debug("wal_sender: standby status update");
               }
            }
            else if (type == MSG_COPY_DONE)
            {
               free(body);
               sender_send_command_complete(fd, "COPY 0");
               sender_send_ready(fd);
               done = true;
               break;
            }
            else if (type == MSG_TERMINATE)
            {
               free(body);
               done = true;
               break;
            }
            free(body);
         }
      }

      if (done)
      {
         break;
      }

      /* find the next record to stream */
      segno = (last_sent + 1) / seg_size;
      {
         char path[PATH_MAX];
         struct walfile* wf = NULL;
         struct stat st;
         struct deque_iterator* iter = NULL;

         if (sender_segment_path(downstream_dir, segno, path, sizeof(path)))
         {
            /* not translated yet: keepalive + wait */
            if (last_keepalive == 0 || sender_now_usec() - last_keepalive > 1000000)
            {
               char body[17];
               uint64_t walend = be64(last_sent);
               int64_t now = be64((uint64_t)sender_now_usec());

               memcpy(body, &walend, 8);
               memcpy(body + 8, &now, 8);
               body[16] = 0;
               if (sender_send_msg(fd, MSG_COPY_DATA, body, 17))
               {
                  done = true;
                  break;
               }
               last_keepalive = sender_now_usec();
            }
            usleep(100000);
            continue;
         }

         if (stat(path, &st) != 0)
         {
            usleep(100000);
            continue;
         }

         wf = calloc(1, sizeof(*wf));
         if (!wf)
         {
            break;
         }
         if (pgmoneta_deque_create(false, &wf->records) || pgmoneta_deque_create(false, &wf->page_headers))
         {
            pgmoneta_destroy_walfile(wf);
            break;
         }
         if (pgmoneta_wal_parse_wal_file(path, srv, wf) != 0)
         {
            pgmoneta_log_error("wal_sender: could not parse downstream segment %s", path);
            pgmoneta_destroy_walfile(wf);
            usleep(100000);
            continue;
         }

         if (pgmoneta_deque_iterator_create(wf->records, &iter) == 0)
         {
            while (pgmoneta_deque_iterator_next(iter))
            {
               struct decoded_xlog_record* rec = (struct decoded_xlog_record*)iter->value->data;
               char* encoded = NULL;
               uint32_t total_len;
               char body[64 * 1024];
               uint32_t boff = 0;
               uint64_t send_time64;
               uint64_t start64;
               uint64_t end64;

               if (!rec || rec->partial)
               {
                  continue;
               }
               if (rec->lsn <= last_sent)
               {
                  continue;
               }

               encoded = pgmoneta_wal_encode_xlog_record(rec, WAL_MAGIC_V19, NULL);
               if (!encoded)
               {
                  continue;
               }
               total_len = ((struct xlog_record*)encoded)->xl_tot_len;

               if (total_len + 1 + 24 > (uint32_t)sizeof(body))
               {
                  pgmoneta_log_warn("wal_sender: record too large to stream (%u bytes); skipping",
                                    total_len);
                  free(encoded);
                  continue;
               }

               ((struct xlog_record*)encoded)->xl_crc = wal_store_compute_crc(encoded, total_len);

               start64 = be64((uint64_t)rec->lsn);
               end64 = be64(rec->next_lsn != 0 ? (uint64_t)rec->next_lsn
                                               : (uint64_t)rec->lsn + MAXALIGN(total_len));
               send_time64 = be64((uint64_t)sender_now_usec());

               body[boff++] = XLOG_DATA;
               memcpy(body + boff, &start64, 8);
               boff += 8;
               memcpy(body + boff, &end64, 8);
               boff += 8;
               memcpy(body + boff, &send_time64, 8);
               boff += 8;
               memcpy(body + boff, encoded, total_len);
               boff += total_len;

               free(encoded);

               if (sender_send_msg(fd, MSG_COPY_DATA, body, boff))
               {
                  pgmoneta_deque_iterator_destroy(iter);
                  pgmoneta_destroy_walfile(wf);
                  free(wf);
                  done = true;
                  break;
               }

               last_sent = rec->lsn;
               sent_anything = true;
            }
            pgmoneta_deque_iterator_destroy(iter);
         }

         pgmoneta_destroy_walfile(wf);
         free(wf);
      }

      if (done)
      {
         break;
      }

      if (!sent_anything)
      {
         /* reload the map so newly translated records are picked up */
         if (lsn_map_create(map_path, &fresh))
         {
            usleep(100000);
            continue;
         }
         lsn_map_destroy(map);
         map = fresh;
         fresh = NULL;

         if (last_keepalive == 0 || sender_now_usec() - last_keepalive > 1000000)
         {
            char body[17];
            uint64_t walend = be64(last_sent);
            int64_t now = be64((uint64_t)sender_now_usec());

            memcpy(body, &walend, 8);
            memcpy(body + 8, &now, 8);
            body[16] = 0;
            if (sender_send_msg(fd, MSG_COPY_DATA, body, 17))
            {
               done = true;
               break;
            }
            last_keepalive = sender_now_usec();
         }
         usleep(100000);
      }
   }

   lsn_map_destroy(map);
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
      return sender_send_identify_system(fd, downstream_dir);
   }
   else if (strncasecmp(query, "START_REPLICATION", 17) == 0)
   {
      uint64_t start_lsn = 0;
      uint32_t timeline = 0;
      char copyboth[4] = {0, 0, 0, 0};

      if (sender_parse_start_replication(query, &start_lsn, &timeline))
      {
         sender_send_error(fd, "walbridge: could not parse START_REPLICATION request");
         return 1;
      }

      pgmoneta_log_info("wal_sender: START_REPLICATION at %X/%X (timeline %u)",
                        (uint32_t)(start_lsn >> 32), (uint32_t)start_lsn, timeline);

      /* CopyBothResponse */
      if (sender_send_msg(fd, MSG_COPY_BOTH, copyboth, 4))
      {
         return 1;
      }

      return sender_stream(fd, srv, downstream_dir, map_path, start_lsn);
   }
   else if (strncasecmp(query, "TIMELINE_HISTORY", 16) == 0)
   {
      sender_send_error(fd, "walbridge: timeline history files are not supported");
      return 1;
   }
   else if (strcasecmp(query, "SHOW") == 0 || strncasecmp(query, "SHOW ", 5) == 0)
   {
      sender_send_error(fd, "walbridge: SHOW is not supported");
      return 1;
   }

   sender_send_error(fd, "walbridge: unsupported query");
   return 1;
}

static int
sender_handle_connection(int fd, int srv, char* downstream_dir, char* map_path)
{
   if (sender_handle_startup(fd))
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
walbridge_run_sender(int srv, char* downstream_dir, char* map_path)
{
   struct main_configuration* config = (struct main_configuration*)shmem;
   int* fds = NULL;
   int length = 0;

   if (pgmoneta_bind("*", config->walbridge, &fds, &length))
   {
      pgmoneta_log_error("wal_sender: could not bind walbridge port %d", config->walbridge);
      return 1;
   }

   pgmoneta_log_info("wal_sender: listening on port %d", config->walbridge);

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
         if (errno != EINTR && errno != EAGAIN)
         {
            client = -1;
         }
      }

      if (client == -1)
      {
         if (errno == EINTR)
         {
            continue;
         }
         pgmoneta_log_warn("wal_sender: accept: %m");
         usleep(500000);
         continue;
      }

      pgmoneta_log_info("wal_sender: accepted connection");
      sender_handle_connection(client, srv, downstream_dir, map_path);
      pgmoneta_log_info("wal_sender: connection closed");
   }

   for (int i = 0; i < length; i++)
   {
      close(fds[i]);
   }
   free(fds);

   return 0;
}
