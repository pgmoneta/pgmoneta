/*
 * Copyright (C) 2025 The pgmoneta community
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
#include <prometheus.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define LINE_LENGTH 32
#define MAX_LENGTH  4096

FILE* log_file;

time_t next_log_rotation_age;  /* number of seconds at which the next location will happen */

char current_log_path[MAX_PATH]; /* the current log file */

static bool log_rotation_enabled(void);
static void log_rotation_disable(void);
static bool log_rotation_required(void);
static bool log_rotation_set_next_rotation_age(void);
static int log_file_open(void);
static void log_file_rotate(void);

static void output_log_line(char* l);

static char* levels[] =
{
   "TRACE",
   "DEBUG",
   "INFO",
   "WARN",
   "ERROR",
   "FATAL"
};

static char* colors[] =
{
   "\x1b[37m",
   "\x1b[36m",
   "\x1b[32m",
   "\x1b[91m",
   "\x1b[31m",
   "\x1b[35m"
};

/**
 *
 */
int
pgmoneta_start_logging(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config->common.log_type == PGMONETA_LOGGING_TYPE_FILE && !log_file)
   {
      log_file_open();

      if (!log_file)
      {
         printf("Failed to open log file %s due to %s\n", strlen(config->common.log_path) > 0 ? config->common.log_path : "pgmoneta.log", strerror(errno));
         errno = 0;
         log_rotation_disable();
         return 1;
      }
   }
   else if (config->common.log_type == PGMONETA_LOGGING_TYPE_SYSLOG)
   {
      openlog("pgmoneta", LOG_CONS | LOG_PERROR | LOG_PID, LOG_USER);
   }

   return 0;
}

/**
 *
 */
int
pgmoneta_stop_logging(void)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config->common.log_type == PGMONETA_LOGGING_TYPE_FILE)
   {
      if (log_file != NULL)
      {
         return fclose(log_file);
      }
      else
      {
         return 1;
      }
   }
   else if (config->common.log_type == PGMONETA_LOGGING_TYPE_SYSLOG)
   {
      closelog();
   }

   return 0;
}

bool
pgmoneta_log_is_enabled(int level)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (level >= config->common.log_level)
   {
      return true;
   }

   return false;
}

void
pgmoneta_log_line(int level, char* file, int line, char* fmt, ...)
{
   FILE* output = NULL;
   signed char isfree;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config == NULL)
   {
      return;
   }

   if (level >= config->common.log_level)
   {
      switch (level)
      {
         case PGMONETA_LOGGING_LEVEL_INFO:
            pgmoneta_prometheus_logging(PGMONETA_LOGGING_LEVEL_INFO);
            break;
         case PGMONETA_LOGGING_LEVEL_WARN:
            pgmoneta_prometheus_logging(PGMONETA_LOGGING_LEVEL_WARN);
            break;
         case PGMONETA_LOGGING_LEVEL_ERROR:
            pgmoneta_prometheus_logging(PGMONETA_LOGGING_LEVEL_ERROR);
            break;
         case PGMONETA_LOGGING_LEVEL_FATAL:
            pgmoneta_prometheus_logging(PGMONETA_LOGGING_LEVEL_FATAL);
            break;
         default:
            break;
      }

      if (config->common.log_type == PGMONETA_LOGGING_TYPE_CONSOLE)
      {
         output = stdout;
      }
      else if (config->common.log_type == PGMONETA_LOGGING_TYPE_FILE)
      {
         output = log_file;
      }

retry:
      isfree = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->common.log_lock, &isfree, STATE_IN_USE))
      {
         char buf[1024];
         va_list vl;
         struct tm* tm;
         time_t t;
         char* filename;

         t = time(NULL);
         tm = localtime(&t);

         filename = strrchr(file, '/');
         if (filename != NULL)
         {
            filename = filename + 1;
         }
         else
         {
            filename = file;
         }

         if (strlen(config->common.log_line_prefix) == 0)
         {
            memcpy(config->common.log_line_prefix, PGMONETA_LOGGING_DEFAULT_LOG_LINE_PREFIX, strlen(PGMONETA_LOGGING_DEFAULT_LOG_LINE_PREFIX));
         }

         memset(&buf[0], 0, sizeof(buf));

#ifdef DEBUG
         if (level > 4)
         {
            char* bt = NULL;
            pgmoneta_backtrace_string(&bt);
            if (bt != NULL)
            {
               fprintf(output, "%s", bt);
               fflush(output);
            }
            free(bt);
            memset(&buf[0], 0, sizeof(buf));
         }
#endif

         va_start(vl, fmt);

         if (config->common.log_type == PGMONETA_LOGGING_TYPE_CONSOLE)
         {
            buf[strftime(buf, sizeof(buf), config->common.log_line_prefix, tm)] = '\0';
            fprintf(output, "%s %s%-5s\x1b[0m \x1b[90m%s:%d\x1b[0m ",
                    buf, colors[level - 1], levels[level - 1],
                    filename, line);
            vfprintf(output, fmt, vl);
            fprintf(output, "\n");
            fflush(output);
         }
         else if (config->common.log_type == PGMONETA_LOGGING_TYPE_FILE)
         {
            buf[strftime(buf, sizeof(buf), config->common.log_line_prefix, tm)] = '\0';
            fprintf(output, "%s %-5s %s:%d ",
                    buf, levels[level - 1], filename, line);
            vfprintf(output, fmt, vl);
            fprintf(output, "\n");
            fflush(output);

            if (log_rotation_required())
            {
               log_file_rotate();
            }
         }
         else if (config->common.log_type == PGMONETA_LOGGING_TYPE_SYSLOG)
         {
            switch (level)
            {
               case PGMONETA_LOGGING_LEVEL_DEBUG5:
                  vsyslog(LOG_DEBUG, fmt, vl);
                  break;
               case PGMONETA_LOGGING_LEVEL_DEBUG1:
                  vsyslog(LOG_DEBUG, fmt, vl);
                  break;
               case PGMONETA_LOGGING_LEVEL_INFO:
                  vsyslog(LOG_INFO, fmt, vl);
                  break;
               case PGMONETA_LOGGING_LEVEL_WARN:
                  vsyslog(LOG_WARNING, fmt, vl);
                  break;
               case PGMONETA_LOGGING_LEVEL_ERROR:
                  vsyslog(LOG_ERR, fmt, vl);
                  break;
               case PGMONETA_LOGGING_LEVEL_FATAL:
                  vsyslog(LOG_CRIT, fmt, vl);
                  break;
               default:
                  vsyslog(LOG_INFO, fmt, vl);
                  break;
            }
         }

         va_end(vl);

         atomic_store(&config->common.log_lock, STATE_FREE);
      }
      else
      {
         SLEEP_AND_GOTO(1000000L, retry)
      }
   }
}

void
pgmoneta_log_mem(void* data, size_t size)
{
   signed char isfree;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config == NULL)
   {
      return;
   }

   if (size > 0)
   {
      if (config->common.log_level == PGMONETA_LOGGING_LEVEL_DEBUG5 &&
          (config->common.log_type == PGMONETA_LOGGING_TYPE_CONSOLE || config->common.log_type == PGMONETA_LOGGING_TYPE_FILE))
      {
retry:
         isfree = STATE_FREE;

         if (atomic_compare_exchange_strong(&config->common.log_lock, &isfree, STATE_IN_USE))
         {
            if (size > MAX_LENGTH)
            {
               int index = 0;
               size_t count = 0;

               /* Display the first 1024 bytes */
               index = 0;
               count = 1024;
               while (count > 0)
               {
                  char* t = NULL;
                  char* n = NULL;
                  char* l = NULL;

                  for (int i = 0; i < LINE_LENGTH; i++)
                  {
                     signed char c;
                     char buf[3] = {0};

                     c = (signed char)*((char*)data + index + i);
                     pgmoneta_snprintf(&buf[0], sizeof(buf), "%02X", c);

                     l = pgmoneta_append(l, &buf[0]);

                     if (c >= 32)
                     {
                        n = pgmoneta_append_char(n, c);
                     }
                     else
                     {
                        n = pgmoneta_append_char(n, '?');
                     }
                  }

                  t = pgmoneta_append(t, l);
                  t = pgmoneta_append_char(t, ' ');
                  t = pgmoneta_append(t, n);

                  output_log_line(t);

                  free(t);
                  t = NULL;

                  free(l);
                  l = NULL;

                  free(n);
                  n = NULL;

                  count -= LINE_LENGTH;
                  index += LINE_LENGTH;
               }

               output_log_line("---------------------------------------------------------------- --------------------------------");

               /* Display the last 1024 bytes */
               index = size - 1024;
               count = 1024;
               while (count > 0)
               {
                  char* t = NULL;
                  char* n = NULL;
                  char* l = NULL;

                  for (int i = 0; i < LINE_LENGTH; i++)
                  {
                     signed char c;
                     char buf[3] = {0};

                     c = (signed char)*((char*)data + index + i);
                     pgmoneta_snprintf(&buf[0], sizeof(buf), "%02X", c);

                     l = pgmoneta_append(l, &buf[0]);

                     if (c >= 32)
                     {
                        n = pgmoneta_append_char(n, c);
                     }
                     else
                     {
                        n = pgmoneta_append_char(n, '?');
                     }
                  }

                  t = pgmoneta_append(t, l);
                  t = pgmoneta_append_char(t, ' ');
                  t = pgmoneta_append(t, n);

                  output_log_line(t);

                  free(t);
                  t = NULL;

                  free(l);
                  l = NULL;

                  free(n);
                  n = NULL;

                  count -= LINE_LENGTH;
                  index += LINE_LENGTH;
               }
            }
            else
            {
               size_t offset = 0;
               size_t remaining = size;
               bool full_line = false;

               while (remaining > 0)
               {
                  char* t = NULL;
                  char* n = NULL;
                  char* l = NULL;
                  size_t count = MIN((int)remaining, (int)LINE_LENGTH);

                  for (size_t i = 0; i < count; i++)
                  {
                     signed char c;
                     char buf[3] = {0};

                     c = (signed char)*((char*)data + offset + i);
                     pgmoneta_snprintf(&buf[0], sizeof(buf), "%02X", c);

                     l = pgmoneta_append(l, &buf[0]);

                     if (c >= 32)
                     {
                        n = pgmoneta_append_char(n, c);
                     }
                     else
                     {
                        n = pgmoneta_append_char(n, '?');
                     }
                  }

                  if (strlen(l) == LINE_LENGTH * 2)
                  {
                     full_line = true;
                  }
                  else if (full_line)
                  {
                     if (strlen(l) < LINE_LENGTH * 2)
                     {
                        int chars_missing = (LINE_LENGTH * 2) - strlen(l);
                        for (int i = 0; i < chars_missing; i++)
                        {
                           l = pgmoneta_append_char(l, ' ');
                        }
                     }
                  }

                  t = pgmoneta_append(t, l);
                  t = pgmoneta_append_char(t, ' ');
                  t = pgmoneta_append(t, n);

                  output_log_line(t);

                  free(t);
                  t = NULL;

                  free(l);
                  l = NULL;

                  free(n);
                  n = NULL;

                  remaining -= count;
                  offset += count;
               }
            }

            atomic_store(&config->common.log_lock, STATE_FREE);
         }
         else
         {
            SLEEP_AND_GOTO(1000000L, retry)
         }
      }
   }
}

static void
output_log_line(char* l)
{
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config->common.log_type == PGMONETA_LOGGING_TYPE_CONSOLE)
   {
      fprintf(stdout, "%s", l);
      fprintf(stdout, "\n");
      fflush(stdout);
   }
   else if (config->common.log_type == PGMONETA_LOGGING_TYPE_FILE)
   {
      fprintf(log_file, "%s", l);
      fprintf(log_file, "\n");
      fflush(log_file);
   }
}

void
pgmoneta_print_bytes_binary(void* ptr, size_t n)
{
   unsigned char* p = (unsigned char*)ptr;
   for (size_t i = 0; i < n; i++)
   {
      for (int bit = 7; bit >= 0; bit--)
      {
         putchar((p[i] & (1 << bit)) ? '1' : '0');
      }
      putchar(' ');
   }
   putchar('\n');
}

static bool
log_rotation_enabled(void)
{
   struct main_configuration* config;
   config = (struct main_configuration*)shmem;

   // disable log rotation in the case
   // logging is not to a file
   if (config->common.log_type != PGMONETA_LOGGING_TYPE_FILE)
   {
      log_rotation_disable();
      return false;
   }

   // log rotation is enabled if either log_rotation_age or
   // log_rotation_size is enabled
   return config->common.log_rotation_age != PGMONETA_LOGGING_ROTATION_DISABLED
          || config->common.log_rotation_size != PGMONETA_LOGGING_ROTATION_DISABLED;
}

static void
log_rotation_disable(void)
{
   struct main_configuration* config;
   config = (struct main_configuration*)shmem;

   config->common.log_rotation_age = PGMONETA_LOGGING_ROTATION_DISABLED;
   config->common.log_rotation_size = PGMONETA_LOGGING_ROTATION_DISABLED;
   next_log_rotation_age = 0;
}

static bool
log_rotation_required(void)
{
   struct stat log_stat;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (!log_rotation_enabled())
   {
      return false;
   }

   if (stat(current_log_path, &log_stat))
   {
      return false;
   }

   if (config->common.log_rotation_size > 0 && log_stat.st_size >= config->common.log_rotation_size)
   {
      return true;
   }

   if (config->common.log_rotation_age > 0 && next_log_rotation_age > 0 && next_log_rotation_age <= log_stat.st_ctime)
   {
      return true;
   }

   return false;
}

static bool
log_rotation_set_next_rotation_age(void)
{
   struct main_configuration* config;
   time_t now;

   config = (struct main_configuration*)shmem;

   if (config->common.log_type == PGMONETA_LOGGING_TYPE_FILE && config->common.log_rotation_age > 0)
   {
      now = time(NULL);
      if (!now)
      {
         config->common.log_rotation_age = PGMONETA_LOGGING_ROTATION_DISABLED;
         return false;
      }

      next_log_rotation_age = now + config->common.log_rotation_age;
      return true;
   }
   else
   {
      config->common.log_rotation_age = PGMONETA_LOGGING_ROTATION_DISABLED;
      return false;
   }
}

static int
log_file_open(void)
{
   struct main_configuration* config;
   time_t htime;
   struct tm* tm;

   config = (struct main_configuration*)shmem;

   if (config->common.log_type == PGMONETA_LOGGING_TYPE_FILE)
   {
      htime = time(NULL);
      if (!htime)
      {
         log_file = NULL;
         return 1;
      }

      tm = localtime(&htime);
      if (tm == NULL)
      {
         log_file = NULL;
         return 1;
      }

      if (strftime(current_log_path, sizeof(current_log_path), config->common.log_path, tm) <= 0)
      {
         // cannot parse the format string, fallback to default logging
         memcpy(current_log_path, "pgmoneta.log", strlen("pgmoneta.log"));
         log_rotation_disable();
      }

      log_file = fopen(current_log_path, config->common.log_mode == PGMONETA_LOGGING_MODE_APPEND ? "a" : "w");

      if (!log_file)
      {
         return 1;
      }

      log_rotation_set_next_rotation_age();
      return 0;
   }

   return 1;
}

static void
log_file_rotate(void)
{
   if (log_rotation_enabled())
   {
      fflush(log_file);
      fclose(log_file);
      log_file_open();
   }
}
