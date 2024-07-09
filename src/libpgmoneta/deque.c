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

#include <pgmoneta.h>
#include <deque.h>
#include <logging.h>
#include <utils.h>

#include <stdlib.h>
#include <string.h>

// tag is copied if not NULL
static void
deque_offer(struct deque* deque, void* data, size_t data_size, char* tag, bool copied);

// tag is copied if not NULL
static void
deque_node_create(void* data, size_t data_size, char* tag, bool copied, struct deque_node** node);

// tag will always be freed
static void
deque_node_destroy(struct deque_node* node);

static void
deque_read_lock(struct deque* deque);

static void
deque_write_lock(struct deque* deque);

static void
deque_unlock(struct deque* deque);

int
pgmoneta_deque_create(bool thread_safe, struct deque** deque)
{
   struct deque* q = NULL;
   q = malloc(sizeof(struct deque));
   q->size = 0;
   q->thread_safe = thread_safe;
   if (thread_safe)
   {
      pthread_rwlock_init(&q->mutex, NULL);
   }
   deque_node_create(NULL, 0, NULL, false, &q->start);
   deque_node_create(NULL, 0, NULL, false, &q->end);
   q->start->next = q->end;
   q->end->prev = q->start;
   *deque = q;
   return 0;
}

int
pgmoneta_deque_put(struct deque* deque, char* tag, void* data, size_t data_size)
{
   void* val = NULL;
   if (data != NULL)
   {
      val = malloc(data_size);
      memcpy(val, data, data_size);
   }
   deque_offer(deque, val, data_size, tag, true);
   return 0;
}

int
pgmoneta_deque_add(struct deque* deque, char* tag, void* data)
{
   deque_offer(deque, data, 0, tag, false);
   return 0;
}

void*
pgmoneta_deque_poll(struct deque* deque, char** tag)
{
   struct deque_node* head = NULL;
   void* val = NULL;
   if (deque == NULL || pgmoneta_deque_size(deque) == 0)
   {
      return NULL;
   }
   deque_write_lock(deque);
   head = deque->start->next;
   // this should not happen when size is not 0, but just in case
   if (head == deque->end)
   {
      deque_unlock(deque);
      return NULL;
   }
   // remove node
   deque->start->next = head->next;
   head->next->prev = deque->start;
   deque->size--;
   val = head->data;
   if (tag != NULL)
   {
      *tag = head->tag;
   }
   free(head);
   deque_unlock(deque);
   return val;
}

void*
pgmoneta_deque_peek(struct deque* deque, char** tag)
{
   struct deque_node* head = NULL;
   void* val = NULL;
   if (deque == NULL || pgmoneta_deque_size(deque) == 0)
   {
      return NULL;
   }
   deque_read_lock(deque);
   head = deque->start->next;
   // this should not happen when size is not 0, but just in case
   if (head == deque->end)
   {
      deque_unlock(deque);
      return NULL;
   }
   val = head->data;
   if (tag != NULL)
   {
      *tag = head->tag;
   }
   deque_unlock(deque);
   return val;
}

void*
pgmoneta_deque_get(struct deque* deque, char* tag)
{
   struct deque_node* n = NULL;

   if (deque == NULL || pgmoneta_deque_size(deque) == 0 || tag == NULL || strlen(tag) == 0)
   {
      return NULL;
   }

   n = pgmoneta_deque_head(deque);

   while (n != NULL)
   {
      if (!strcmp(tag, n->tag))
      {
         return n->data;
      }

      n = pgmoneta_deque_next(deque, n);
   }

   return NULL;
}

struct deque_node*
pgmoneta_deque_next(struct deque* deque, struct deque_node* node)
{
   struct deque_node* next = NULL;
   if (deque == NULL || pgmoneta_deque_size(deque) == 0 || node == NULL)
   {
      return NULL;
   }
   deque_read_lock(deque);
   if (node->next == deque->end)
   {
      deque_unlock(deque);
      return NULL;
   }
   next = node->next;
   deque_unlock(deque);
   return next;
}

struct deque_node*
pgmoneta_deque_prev(struct deque* deque, struct deque_node* node)
{
   struct deque_node* prev = NULL;
   if (deque == NULL || pgmoneta_deque_size(deque) == 0 || node == NULL)
   {
      return NULL;
   }
   deque_read_lock(deque);
   if (node->prev == deque->start)
   {
      deque_unlock(deque);
      return NULL;
   }
   deque_unlock(deque);
   prev = node->prev;
   deque_unlock(deque);
   return prev;
}

struct deque_node*
pgmoneta_deque_head(struct deque* deque)
{
   if (deque == NULL)
   {
      return NULL;
   }

   return pgmoneta_deque_next(deque, deque->start);
}

struct deque_node*
pgmoneta_deque_tail(struct deque* deque)
{
   if (deque == NULL)
   {
      return NULL;
   }

   return pgmoneta_deque_prev(deque, deque->end);
}

bool
pgmoneta_deque_empty(struct deque* deque)
{
   return pgmoneta_deque_size(deque) == 0;
}

void
pgmoneta_deque_list(struct deque* deque)
{
   struct deque_node* n = NULL;

   if (deque != NULL && pgmoneta_deque_size(deque) > 0)
   {
      n = pgmoneta_deque_head(deque);

      pgmoneta_log_trace("Deque:");
      while (n != NULL)
      {
         pgmoneta_log_trace("%s", n->tag);
         pgmoneta_log_mem(n->data, n->data_size);

         n = pgmoneta_deque_next(deque, n);
      }
   }
   else
   {
      pgmoneta_log_trace("Deque: Empty");
   }
}

void
pgmoneta_deque_destroy(struct deque* deque)
{
   struct deque_node* n = NULL;
   struct deque_node* next = NULL;
   if (deque == NULL)
   {
      return;
   }
   n = deque->start;
   while (n != NULL)
   {
      next = n->next;
      deque_node_destroy(n);
      n = next;
   }
   if (deque->thread_safe)
   {
      pthread_rwlock_destroy(&deque->mutex);
   }
   free(deque);
}

struct deque_node*
pgmoneta_deque_remove(struct deque* deque, struct deque_node* node)
{
   if (deque == NULL || node == NULL || node == deque->start || node == deque->end)
   {
      return NULL;
   }
   deque_write_lock(deque);
   struct deque_node* prev = node->prev;
   struct deque_node* next = node->next;
   prev->next = next;
   next->prev = prev;
   deque_node_destroy(node);
   deque->size--;
   if (next == deque->end)
   {
      deque_unlock(deque);
      return NULL;
   }
   deque_unlock(deque);
   return next;
}

uint32_t
pgmoneta_deque_size(struct deque* deque)
{
   uint32_t size = 0;
   if (deque == NULL)
   {
      return 0;
   }
   deque_read_lock(deque);
   size = deque->size;
   deque_unlock(deque);
   return size;
}

static void
deque_offer(struct deque* deque, void* data, size_t data_size, char* tag, bool copied)
{
   struct deque_node* n = NULL;
   struct deque_node* last = NULL;
   deque_node_create(data, data_size, tag, copied, &n);
   deque_write_lock(deque);
   deque->size++;
   last = deque->end->prev;
   last->next = n;
   n->prev = last;
   n->next = deque->end;
   deque->end->prev = n;
   deque_unlock(deque);
}

static void
deque_node_create(void* data, size_t data_size, char* tag, bool copied, struct deque_node** node)
{
   struct deque_node* n = NULL;
   n = malloc(sizeof(struct deque_node));
   n->copied = copied;
   n->data = data;
   n->data_size = data_size;
   n->prev = NULL;
   n->next = NULL;
   if (tag != NULL)
   {
      n->tag = malloc(strlen(tag) + 1);
      strcpy(n->tag, tag);
   }
   else
   {
      n->tag = NULL;
   }
   *node = n;
}

static void
deque_node_destroy(struct deque_node* node)
{
   if (node == NULL)
   {
      return;
   }
   if (node->copied)
   {
      free(node->data);
   }
   free(node->tag);
   free(node);
}

static void
deque_read_lock(struct deque* deque)
{
   if (deque == NULL || !deque->thread_safe)
   {
      return;
   }
   pthread_rwlock_rdlock(&deque->mutex);
}

static void
deque_write_lock(struct deque* deque)
{
   if (deque == NULL || !deque->thread_safe)
   {
      return;
   }
   pthread_rwlock_wrlock(&deque->mutex);
}

static void
deque_unlock(struct deque* deque)
{
   if (deque == NULL || !deque->thread_safe)
   {
      return;
   }
   pthread_rwlock_unlock(&deque->mutex);
}
