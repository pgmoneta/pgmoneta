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

#include <pgmoneta.h>
#include <deque.h>
#include <logging.h>
#include <workers.h>
#include <value.h>

#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#ifdef HAVE_LINUX
#include <sys/sysinfo.h>
#endif

static volatile int worker_keepalive;

static int worker_init(struct workers* workers, struct worker** worker);
static void* worker_do(struct worker* worker);
static void worker_destroy(struct worker* worker);

static int semaphore_init(struct semaphore* semaphore);
static void semaphore_post(struct semaphore* semaphore);
static void semaphore_post_all(struct semaphore* semaphore);
static void semaphore_wait(struct semaphore* semaphore);
static void destroy_task_wrapper(uintptr_t data);

int
pgmoneta_workers_initialize(int num, struct workers** workers)
{
   struct workers* w = NULL;

   *workers = NULL;

   worker_keepalive = 1;

   if (num < 1)
   {
      goto error;
   }

   w = (struct workers*)malloc(sizeof(struct workers));
   if (w == NULL)
   {
      pgmoneta_log_error("Could not allocate memory for worker pool");
      goto error;
   }

   w->number_of_alive = 0;
   w->number_of_working = 0;
   w->outcome = true;

   if (pgmoneta_deque_create(true, &w->queue))
   {
      pgmoneta_log_error("Could not allocate memory for deque");
      goto error;
   }

   w->has_tasks = (struct semaphore*)malloc(sizeof(struct semaphore));
   if (w->has_tasks == NULL)
   {
      pgmoneta_log_error("Could not allocate memory for workers semaphore");
      goto error;
   }

   if (semaphore_init(w->has_tasks))
   {
      pgmoneta_log_error("Could not initialize workers semaphore");
      goto error;
   }

   w->worker = (struct worker**)malloc(num * sizeof(struct worker*));
   if (w->worker == NULL)
   {
      pgmoneta_log_error("Could not allocate memory for workers");
      goto error;
   }

   pthread_mutex_init(&(w->worker_lock), NULL);
   pthread_cond_init(&w->worker_all_idle, NULL);

   for (int n = 0; n < num; n++)
   {
      worker_init(w, &w->worker[n]);
   }

   while (w->number_of_alive != num)
   {
      SLEEP(10);
   }

   *workers = w;

   return 0;

error:

   if (w != NULL)
   {
      pgmoneta_deque_destroy(w->queue);
      free(w->has_tasks);
      free(w);
   }

   return 1;
}

int
pgmoneta_workers_add(struct workers* workers, void (*function)(struct worker_common*), struct worker_common* wc)
{
   struct worker_task* task = NULL;
   struct value_config config = {0};

   if (workers != NULL)
   {
      task = (struct worker_task*)malloc(sizeof(struct worker_task));
      if (task == NULL)
      {
         pgmoneta_log_error("Could not allocate memory for task");
         goto error;
      }

      task->function = function;
      task->wc = wc;

      config.destroy_data = destroy_task_wrapper;

      pgmoneta_deque_add_with_config(workers->queue, NULL, (uintptr_t)task, &config);

      semaphore_post(workers->has_tasks);

      return 0;
   }

error:

   return 1;
}

void
pgmoneta_workers_wait(struct workers* workers)
{
   if (workers != NULL)
   {
      pthread_mutex_lock(&workers->worker_lock);

      while (pgmoneta_deque_size(workers->queue) > 0 || workers->number_of_working > 0)
      {
         pgmoneta_log_trace("Waiting to finish (%d/%d)", workers->number_of_working, pgmoneta_deque_size(workers->queue));
         pthread_cond_wait(&workers->worker_all_idle, &workers->worker_lock);
      }

      pthread_mutex_unlock(&workers->worker_lock);
   }
}

void
pgmoneta_workers_destroy(struct workers* workers)
{
   volatile int worker_total;
   double timeout = 1.0;
   time_t start;
   time_t end;
   double tpassed = 0.0;

   if (workers != NULL)
   {
      worker_total = workers->number_of_alive;
      worker_keepalive = 0;

      time(&start);
      while (tpassed < timeout && workers->number_of_alive)
      {
         semaphore_post_all(workers->has_tasks);
         time(&end);
         tpassed = difftime(end, start);
      }

      while (workers->number_of_alive)
      {
         semaphore_post_all(workers->has_tasks);
         SLEEP(1000000000L);
      }

      pgmoneta_deque_destroy(workers->queue);
      free(workers->has_tasks);

      for (int n = 0; n < worker_total; n++)
      {
         worker_destroy(workers->worker[n]);
      }

      free(workers->worker);
      free(workers);
   }
}

int
pgmoneta_get_number_of_workers(int server)
{
   int nw = 0;
   struct main_configuration* config;

   config = (struct main_configuration*)shmem;

   if (config->common.servers[server].workers != -1)
   {
      nw = config->common.servers[server].workers;
   }
   else
   {
      nw = config->workers;
   }

#ifdef HAVE_LINUX
   nw = MIN(nw, get_nprocs());
#else
   nw = MIN(nw, 16);
#endif

   return nw;
}

int
pgmoneta_create_worker_input(char* directory, char* from, char* to, int level,
                             struct workers* workers, struct worker_input** wi)
{
   struct worker_input* w = NULL;

   *wi = NULL;

   w = (struct worker_input*)malloc(sizeof(struct worker_input));

   if (w == NULL)
   {
      goto error;
   }

   memset(w, 0, sizeof(struct worker_input));

   if (directory != NULL && strlen(directory) > 0)
   {
      if (strlen(directory) >= sizeof(w->directory))
      {
         pgmoneta_log_error("Worker input directory too long: %s", directory);
         goto error;
      }
      memcpy(w->directory, directory, strlen(directory));
   }

   if (from != NULL && strlen(from) > 0)
   {
      if (strlen(from) >= sizeof(w->from))
      {
         pgmoneta_log_error("Worker input source too long: %s", from);
         goto error;
      }
      memcpy(w->from, from, strlen(from));
   }

   if (to != NULL && strlen(to) > 0)
   {
      if (strlen(to) >= sizeof(w->to))
      {
         pgmoneta_log_error("Worker input destination too long: %s", to);
         goto error;
      }
      memcpy(w->to, to, strlen(to));
   }

   w->level = level;
   w->data = NULL;
   w->failed = NULL;
   w->all = NULL;
   w->common.workers = workers;

   *wi = w;

   return 0;

error:

   free(w);

   return 1;
}

static int
worker_init(struct workers* workers, struct worker** worker)
{
   struct worker* w = NULL;

   *worker = NULL;

   w = (struct worker*)malloc(sizeof(struct worker));
   if (w == NULL)
   {
      pgmoneta_log_error("Could not allocate memory for worker");
      goto error;
   }

   w->workers = workers;

   pthread_create(&w->pthread, NULL, (void* (*)(void*))worker_do, w);
   pthread_detach(w->pthread);

   *worker = w;

   return 0;

error:

   return 1;
}

static void*
worker_do(struct worker* worker)
{
   struct worker_task* task;
   struct workers* workers = worker->workers;

   pthread_mutex_lock(&workers->worker_lock);
   workers->number_of_alive += 1;
   pthread_mutex_unlock(&workers->worker_lock);

   while (worker_keepalive)
   {
      semaphore_wait(workers->has_tasks);

      if (worker_keepalive)
      {
         pthread_mutex_lock(&workers->worker_lock);
         workers->number_of_working++;
         pthread_mutex_unlock(&workers->worker_lock);

         task = (struct worker_task*)pgmoneta_deque_poll(workers->queue, NULL);

         if (task)
         {
            task->function(task->wc);
            free(task);
         }

         pthread_mutex_lock(&workers->worker_lock);
         workers->number_of_working--;
         if (!workers->number_of_working)
         {
            pthread_cond_signal(&workers->worker_all_idle);
         }
         pthread_mutex_unlock(&workers->worker_lock);
      }
   }
   pthread_mutex_lock(&workers->worker_lock);
   workers->number_of_alive--;
   pthread_mutex_unlock(&workers->worker_lock);

   return NULL;
}

static void
worker_destroy(struct worker* w)
{
   free(w);
}

static int
semaphore_init(struct semaphore* semaphore)
{
   if (semaphore == NULL)
   {
      pgmoneta_log_error("Invalid semaphore");
      goto error;
   }

   pthread_mutex_init(&(semaphore->mutex), NULL);
   pthread_cond_init(&(semaphore->cond), NULL);
   semaphore->count = 0;

   return 0;

error:

   return 1;
}

static void
semaphore_post(struct semaphore* semaphore)
{
   pthread_mutex_lock(&semaphore->mutex);
   semaphore->count++;
   pthread_cond_signal(&semaphore->cond);
   pthread_mutex_unlock(&semaphore->mutex);
}

static void
semaphore_post_all(struct semaphore* semaphore)
{
   pthread_mutex_lock(&semaphore->mutex);
   semaphore->count++;
   pthread_cond_broadcast(&semaphore->cond);
   pthread_mutex_unlock(&semaphore->mutex);
}

static void
semaphore_wait(struct semaphore* semaphore)
{
   pthread_mutex_lock(&semaphore->mutex);
   while (semaphore->count == 0)
   {
      pthread_cond_wait(&semaphore->cond, &semaphore->mutex);
   }
   semaphore->count--;
   pthread_mutex_unlock(&semaphore->mutex);
}

static void
destroy_task_wrapper(uintptr_t data)
{
   struct worker_task* task = (struct worker_task*)data;
   free(task);
}
