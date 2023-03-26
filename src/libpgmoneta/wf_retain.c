/*
 * Copyright (C) 2023 Red Hat
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
#include <node.h>
#include <pgmoneta.h>
#include <info.h>
#include <link.h>
#include <logging.h>
#include <delete.h>
#include <utils.h>
#include <workflow.h>

/* system */
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static int retain_setup(int, char*, struct node*, struct node**);
static int retain_execute(int, char*, struct node*, struct node**);
static int retain_teardown(int, char*, struct node*, struct node**);
static void mark_retain(bool** retain_flags, int retention[MAX_RETENTION_LENGTH], int number_of_backups, struct backup** backups);

struct workflow*
pgmoneta_workflow_create_retention(void)
{
   struct workflow* wf = NULL;

   wf = (struct workflow*)malloc(sizeof(struct workflow));

   wf->setup = &retain_setup;
   wf->execute = &retain_execute;
   wf->teardown = &retain_teardown;
   wf->next = NULL;

   return wf;
}

static int
retain_setup(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static int
retain_execute(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   char* d;
   int number_of_backups = 0;
   struct backup** backups = NULL;
   bool* retain_flags = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_servers; i++)
   {
      int retention[MAX_RETENTION_LENGTH];
      retention[0] = config->servers[i].retention_days;
      if (retention[0] <= 0)
      {
         retention[0] = config->retention_days;
      }
      retention[1] = config->servers[i].retention_weeks;
      if (retention[1] <= 0)
      {
         retention[1] = config->retention_weeks;
      }
      retention[2] = config->servers[i].retention_months;
      if (retention[2] <= 0)
      {
         retention[2] = config->retention_months;
      }
      retention[3] = config->servers[i].retention_years;
      if (retention[3] <= 0)
      {
         retention[3] = config->retention_years;
      }

      number_of_backups = 0;
      backups = NULL;

      d = pgmoneta_get_server_backup(i);

      pgmoneta_get_backups(d, &number_of_backups, &backups);

      if (number_of_backups > 0)
      {
         mark_retain(&retain_flags, retention, number_of_backups, backups);
         for (int j = 0; j < number_of_backups; j++)
         {
            if (!retain_flags[j])
            {
               if (!backups[j]->keep)
               {
                  if (!atomic_load(&config->servers[i].delete))
                  {
                     pgmoneta_delete(i, backups[j]->label);
                     pgmoneta_log_info("Retention: %s/%s", config->servers[i].name, backups[j]->label);
                  }
               }
            }
            else
            {
               break;
            }
         }
      }

      pgmoneta_delete_wal(i);

      for (int j = 0; j < number_of_backups; j++)
      {
         free(backups[j]);
      }
      free(backups);
      free(retain_flags);
      free(d);
   }

   return 0;
}

static int
retain_teardown(int server, char* identifier, struct node* i_nodes, struct node** o_nodes)
{
   return 0;
}

static void
mark_retain(bool** retain_flags, int retention[MAX_RETENTION_LENGTH], int number_of_backups, struct backup** backups)
{
   bool* flags = NULL;
   time_t t;
   char check_date[128];
   struct tm* time_info;

   flags = (bool*) malloc(sizeof (bool*) * number_of_backups);
   for (int i = 0; i < number_of_backups; i++)
   {
      flags[i] = false;
   }
   t = time(NULL);
   memset(&check_date[0], 0, sizeof(check_date));
   for (int i = 0; i < MAX_RETENTION_LENGTH; i++)
   {
      time_t tmp_time = t;
      if (retention[i] == 0)
      {
         continue;
      }
      switch (i)
      {
         case 0:
         {
            // days
            tmp_time = tmp_time - (retention[i] * 24 * 60 * 60);
            time_info = localtime(&tmp_time);
            strftime(&check_date[0], sizeof(check_date), "%Y%m%d%H%M%S", time_info);
            // this is the same logic as previous implementation
            // construct the timestamp 7 days before
            // and mark retain for backups later than that
            for (int j = number_of_backups - 1; j >= 0; j--)
            {
               if (strcmp(backups[j]->label, &check_date[0]) >= 0)
               {
                  flags[j] = true;
               }
               else
               {
                  break;
               }
            }
            break;
         }
         case 1:
         {
            int weeks = retention[i];
            // use global variable k to traverse backups from latest to oldest
            int k = number_of_backups - 1;
            for (int j = 0; j < weeks; j++)
            {
               // for every week, find the Monday of that week
               tmp_time = tmp_time - (j * 7 * 24 * 60 * 60);
               time_info = localtime(&tmp_time);
               // tm_wday starts with Sunday
               tmp_time = tmp_time - ((time_info->tm_wday + 6) % 7) * 24 * 60 * 60;
               time_info = localtime(&tmp_time);
               // scan will resume from where it left off in the previous loop
               // and try to find the first backup whose date with the new Monday
               while (k >= 0)
               {
                  // find the latest label on that Monday
                  // check backups from latest to earliest,
                  // mark retain for the first backup whose date matches with that Monday
                  struct tm backup_time_info = {0};
                  // construct tm struct from the timestamp label
                  strptime(backups[k]->label, "%Y%m%d%H%M%S", &backup_time_info);
                  if (time_info->tm_year == backup_time_info.tm_year &&
                      time_info->tm_yday == backup_time_info.tm_yday)
                  {
                     flags[k--] = true;
                     break;
                  }
                  else if ((time_info->tm_year == backup_time_info.tm_year &&
                            time_info->tm_yday > backup_time_info.tm_yday) ||
                           time_info->tm_year > backup_time_info.tm_year)
                  {
                     // stop if one week's Monday doesn't backup and k goes too far back
                     break;
                  }
                  k--;
               }
            }
            break;
         }
         case 2:
         {
            // months
            int months = retention[i];
            // use global variable k to traverse backups from latest to oldest
            int k = number_of_backups - 1;
            // get the time info for the current time
            time_info = localtime(&tmp_time);
            int cur_year = time_info->tm_year;
            int cur_month = time_info->tm_mon;

            for (int j = 0; j < months; j++)
            {
               // first we look at the first day on this month,
               // then push the time one month back at a time
               if (j > 0)
               {
                  cur_month--;
               }
               // if we cross years, change month to December of the previous year
               if (cur_month < 0)
               {
                  cur_month = 11;
                  cur_year--;
               }
               // scan through backups from latest to earliest,
               // scan will resume from where it left off in the previous loop
               // and try to find the first backup whose month and year matches with cur_month and cur_year
               while (k >= 0)
               {
                  struct tm backup_time_info = {0};
                  strptime(backups[k]->label, "%Y%m%d%H%M%S", &backup_time_info);
                  // find the latest backup on the first day of that month
                  if (cur_month == backup_time_info.tm_mon &&
                      cur_year == backup_time_info.tm_year &&
                      backup_time_info.tm_mday == 1)
                  {
                     flags[k--] = true;
                     break;
                  }
                  else if ((cur_year == backup_time_info.tm_year &&
                            cur_month > backup_time_info.tm_mon) ||
                           cur_year > backup_time_info.tm_year)
                  {
                     // stop when k goes too far back
                     break;
                  }
                  k--;
               }
            }
            break;
         }
         case 3:
         {
            // years
            int years = retention[i];
            int k = number_of_backups - 1;
            time_info = localtime(&tmp_time);
            int cur_year = time_info->tm_year;

            for (int j = 0; j < years; j++)
            {
               // go to previous year
               if (j > 0)
               {
                  cur_year--;
               }

               while (k >= 0)
               {
                  struct tm backup_time_info = {0};
                  strptime(backups[k]->label, "%Y%m%d%H%M%S", &backup_time_info);
                  // find the latest backup on the first day of that year
                  if (cur_year == backup_time_info.tm_year && backup_time_info.tm_yday == 0)
                  {
                     flags[k--] = true;
                     break;
                  }
                  else if (cur_year > backup_time_info.tm_year)
                  {
                     // in case one year doesn't have backups and the pointer k goes too far back
                     break;
                  }
                  k--;
               }
            }
            break;
         }
         default:
            pgmoneta_log_fatal("invalid number of retention inputs: %d", i);
            break;
      }
   }
   *retain_flags = flags;
}
