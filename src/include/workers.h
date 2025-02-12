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

#ifndef PGMONETA_WORKERS_H
#define PGMONETA_WORKERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pgmoneta.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

struct worker_input;

/** @struct semaphore
 * Defines a semaphore
 */
struct semaphore
{
   pthread_mutex_t mutex; /**< The mutex of the semaphore */
   pthread_cond_t cond;   /**< The condition of the semaphore */
   int value;             /**< The current value */
};

/** @struct task
 * Defines a task
 */
struct task
{
   struct task* previous;                  /**< The previous task */
   void (*function)(struct worker_input*); /**< The task */
   struct worker_input* wi;                /**< The input */
};

/** @struct queue
 * Defines a queue
 */
struct queue
{
   pthread_mutex_t rwmutex;     /**< The read/write mutex */
   struct task* front;          /**< The first task */
   struct task* rear;           /**< The last task */
   struct semaphore* has_tasks; /**< Are there any tasks ? */
   int number_of_tasks;         /**< The number of tasks */
};

/** @struct worker
 * Defines a worker
 */
struct worker
{
   pthread_t pthread;       /**< The worker thread */
   struct workers* workers; /**< Pointer to the root structure */
};

/** @struct workers
 * Defines the workers
 */
struct workers
{
   struct worker** worker;         /**< The list of workers */
   volatile int number_of_alive;   /**< The number of alive workers */
   volatile int number_of_working; /**< The number of workers */
   pthread_mutex_t worker_lock;    /**< The worker lock */
   pthread_cond_t worker_all_idle; /**< Are workers idle */
   bool outcome;                   /**< Outcome of the workers */
   struct queue queue;             /**< The queue */
};

/** @struct worker_input
 * Defines the worker input
 */
struct worker_input
{
   char directory[MAX_PATH]; /**< The directory */
   char from[MAX_PATH];      /**< The from directory */
   char to[MAX_PATH];        /**< The to directory */
   int level;                /**< The compression level */
   bool force;               /**< Force the operation */
   struct json* data;        /**< JSON data */
   struct deque* failed;     /**< Failed files */
   struct deque* all;        /**< All files */
   struct workers* workers;  /**< The root structure */
};

/**
 * Initialize workers
 * @param num The number of workers
 * @param workers The resulting workers
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_workers_initialize(int num, struct workers** workers);

/**
 * Add work to the queue
 * @param workers The workers
 * @param function The function pointer
 * @param wi The argument
 * @return 0 upon success, otherwise 1.
 */
int
pgmoneta_workers_add(struct workers* workers, void (*function)(struct worker_input*), struct worker_input* wi);

/**
 * Wait for all queued work units to finish
 * @param workers The workers
 */
void
pgmoneta_workers_wait(struct workers* workers);

/**
 * Destroy workers
 * @param workers The workers
 */
void
pgmoneta_workers_destroy(struct workers* workers);

/**
 * Get the number of workers for a server
 * @param server The server identifier
 */
int
pgmoneta_get_number_of_workers(int server);

/**
 * Create worker input
 * @param directory The directory path
 * @param from The from file path
 * @param to The to file path
 * @param level The level
 * @param force Force the operation
 * @param workers The workers
 * @param j The resulting worker input
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_worker_input(char* directory, char* from, char* to, int level, bool force,
                             struct workers* workers, struct worker_input** wi);

#ifdef __cplusplus
}
#endif

#endif
