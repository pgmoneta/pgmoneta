/*
 * Copyright (C) 2024 The pgmoneta community
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

struct semaphore
{
   pthread_mutex_t mutex;
   pthread_cond_t cond;
   int value;
};

struct task
{
   struct task* previous;
   void (*function)(void* arg);
   void* arg;
};

struct queue
{
   pthread_mutex_t rwmutex;
   struct task* front;
   struct task* rear;
   struct semaphore* has_tasks;
   int number_of_tasks;
};

struct worker
{
   pthread_t pthread;
   struct workers* workers;
};

struct workers
{
   struct worker** worker;
   volatile int number_of_alive;
   volatile int number_of_working;
   pthread_mutex_t worker_lock;
   pthread_cond_t worker_all_idle;
   struct queue queue;
};

struct worker_input
{
   char directory[MAX_PATH];
   char from[MAX_PATH];
   char to[MAX_PATH];
   int level;
   struct workers* workers;
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
 * @param ap The arguments
 * @return 0 upon success, otherwise 1.
 */
int
pgmoneta_workers_add(struct workers* workers, void(*function)(void*), void* ap);

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
 * @param workers The workers
 * @param j The resulting worker input
 * @return 0 upon success, otherwise 1
 */
int
pgmoneta_create_worker_input(char* directory, char* from, char* to, int level, struct workers* workers, struct worker_input** wi);

#ifdef __cplusplus
}
#endif

#endif
