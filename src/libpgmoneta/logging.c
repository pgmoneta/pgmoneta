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

FILE* log_file;

time_t next_log_rotation_age;  /* number of seconds at which the next location will happen */

char current_log_path[MAX_PATH]; /* the current log file */

static bool log_rotation_enabled(void);
static void log_rotation_disable(void);
static bool log_rotation_required(void);
static bool log_rotation_set_next_rotation_age(void);
static int log_file_open(void);
static void log_file_rotate(void);

static char *levels[] =
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

retry:
      isfree = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->common.log_lock, &isfree, STATE_IN_USE))
      {
         char buf[256];
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

         va_start(vl, fmt);

         if (config->common.log_type == PGMONETA_LOGGING_TYPE_CONSOLE)
         {
            buf[strftime(buf, sizeof(buf), config->common.log_line_prefix, tm)] = '\0';
            fprintf(stdout, "%s %s%-5s\x1b[0m \x1b[90m%s:%d\x1b[0m ",
                    buf, colors[level - 1], levels[level - 1],
                    filename, line);
            vfprintf(stdout, fmt, vl);
            fprintf(stdout, "\n");
            fflush(stdout);
         }
         else if (config->common.log_type == PGMONETA_LOGGING_TYPE_FILE)
         {
            buf[strftime(buf, sizeof(buf), config->common.log_line_prefix, tm)] = '\0';
            fprintf(log_file, "%s %-5s %s:%d ",
                    buf, levels[level - 1], filename, line);
            vfprintf(log_file, fmt, vl);
            fprintf(log_file, "\n");
            fflush(log_file);

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
        SLEEP_AND_GOTO(1000000L,retry)
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

   if (config->common.log_level == PGMONETA_LOGGING_LEVEL_DEBUG5 &&
       size > 0 &&
       (config->common.log_type == PGMONETA_LOGGING_TYPE_CONSOLE || config->common.log_type == PGMONETA_LOGGING_TYPE_FILE))
   {
retry:
      isfree = STATE_FREE;

      if (atomic_compare_exchange_strong(&config->common.log_lock, &isfree, STATE_IN_USE))
      {
         char buf[(3 * size) + (2 * ((size / LINE_LENGTH) + 1)) + 1 + 1];
         int j = 0;
         int k = 0;

         memset(&buf, 0, sizeof(buf));

         for (size_t i = 0; i < size; i++)
         {
            if (k == LINE_LENGTH)
            {
               buf[j] = '\n';
               j++;
               k = 0;
            }
            sprintf(&buf[j], "%02X", (signed char) *((char*)data + i));
            j += 2;
            k++;
         }

         buf[j] = '\n';
         j++;
         k = 0;

         for (size_t i = 0; i < size; i++)
         {
            signed char c = (signed char) *((char*)data + i);
            if (k == LINE_LENGTH)
            {
               buf[j] = '\n';
               j++;
               k = 0;
            }
            if (c >= 32)
            {
               buf[j] = c;
            }
            else
            {
               buf[j] = '?';
            }
            j++;
            k++;
         }

         if (config->common.log_type == PGMONETA_LOGGING_TYPE_CONSOLE)
         {
            fprintf(stdout, "%s", buf);
            fprintf(stdout, "\n");
            fflush(stdout);
         }
         else if (config->common.log_type == PGMONETA_LOGGING_TYPE_FILE)
         {
            fprintf(log_file, "%s", buf);
            fprintf(log_file, "\n");
            fflush(log_file);
         }

         atomic_store(&config->common.log_lock, STATE_FREE);
      }
      else
        SLEEP_AND_GOTO(1000000L,retry)
   }
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
