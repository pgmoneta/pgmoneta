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

#include <deque.h>
#include <utils.h>

#include <stdlib.h>
#include <string.h>

// tag is copied if not NULL, while data is not
static void
deque_offer(struct deque* deque, void* data, char* tag, enum node_type type);

// tag is copied if not NULL, while data is not
static void
deque_node_create(struct deque_node** node, void* data, char* tag, enum node_type type);

// if node type is not ref, data is freed. Tag will always be freed
static void
deque_node_destroy(struct deque_node* node);

int
pgmoneta_deque_create(struct deque** deque)
{
   struct deque* q = NULL;
   q = malloc(sizeof(struct deque));
   q->size = 0;
   deque_node_create(&q->start, NULL, NULL, NodeRef);
   deque_node_create(&q->end, NULL, NULL, NodeRef);
   q->start->next = q->end;
   q->end->prev = q->start;
   *deque = q;
   return 0;
}

int
pgmoneta_deque_offer_string(struct deque* deque, char* data, char* tag)
{
   char* s = NULL;
   if (data != NULL)
   {
      s = malloc(strlen(data) + 1);
      strcpy(s, data);
   }
   deque_offer(deque, (void*)s, tag, NodeString);
   return 0;
}

int
pgmoneta_deque_offer_int(struct deque* deque, int data, char* tag)
{
   int* i = NULL;
   i = malloc(sizeof(int));
   *i = data;
   deque_offer(deque, (void*)i, tag, NodeInt);
   return 0;
}

int
pgmoneta_deque_offer_bool(struct deque* deque, bool data, char* tag)
{
   bool* f = NULL;
   f = malloc(sizeof(bool));
   *f = data;
   deque_offer(deque, (void*)f, tag, NodeBool);
   return 0;
}

int
pgmoneta_deque_offer_ref(struct deque* deque, void* data, char* tag)
{
   deque_offer(deque, data, tag, NodeRef);
   return 0;
}

struct deque_node*
pgmoneta_deque_poll(struct deque* deque)
{
   struct deque_node* head = NULL;
   if (deque == NULL || deque->size == 0)
   {
      return NULL;
   }
   head = deque->start->next;
   // remove node
   deque->start->next = head->next;
   head->next->prev = deque->start;
   deque->size--;
   return head;
}

struct deque_node*
pgmoneta_deque_peek(struct deque* deque)
{
   if (deque == NULL || deque->size == 0)
   {
      return NULL;
   }
   return deque->start->next;
}

int
pgmoneta_deque_poll_int(struct deque* deque)
{
   int res = 0;
   struct deque_node* node = pgmoneta_deque_poll(deque);
   if (node == NULL || node->type != NodeInt)
   {
      return 0;
   }
   res = *((int*)node->data);
   deque_node_destroy(node);
   return res;
}

int
pgmoneta_deque_peek_int(struct deque* deque)
{
   int res = 0;
   struct deque_node* node = pgmoneta_deque_peek(deque);
   if (node == NULL || node->type != NodeInt)
   {
      return 0;
   }
   res = *((int*)node->data);
   return res;
}

char*
pgmoneta_deque_poll_string(struct deque* deque)
{
   char* res = NULL;
   struct deque_node* node = pgmoneta_deque_poll(deque);
   if (node == NULL || node->type != NodeString)
   {
      return NULL;
   }
   if (node->data != NULL)
   {
      res = malloc(strlen((char*)node->data) + 1);
      strcpy(res, (char*)node->data);
   }
   deque_node_destroy(node);
   return res;
}

char*
pgmoneta_deque_peek_string(struct deque* deque)
{
   struct deque_node* node = pgmoneta_deque_peek(deque);
   if (node == NULL || node->type != NodeString)
   {
      return NULL;
   }
   return (char*)node->data;
}

bool
pgmoneta_deque_poll_bool(struct deque* deque)
{
   bool res = 0;
   struct deque_node* node = pgmoneta_deque_poll(deque);
   if (node == NULL || node->type != NodeBool)
   {
      return false;
   }
   res = *((bool*)node->data);
   deque_node_destroy(node);
   return res;
}

bool
pgmoneta_deque_peek_bool(struct deque* deque)
{
   bool res = 0;
   struct deque_node* node = pgmoneta_deque_peek(deque);
   if (node == NULL || node->type != NodeBool)
   {
      return false;
   }
   res = *((bool*)node->data);
   return res;
}

void*
pgmoneta_deque_poll_ref(struct deque* deque)
{
   void* res = NULL;
   struct deque_node* node = pgmoneta_deque_poll(deque);
   if (node == NULL || node->type != NodeRef)
   {
      return false;
   }
   res = node->data;
   deque_node_destroy(node);
   return res;
}

void*
pgmoneta_deque_peek_ref(struct deque* deque)
{
   void* res = NULL;
   struct deque_node* node = pgmoneta_deque_peek(deque);
   if (node == NULL || node->type != NodeRef)
   {
      return false;
   }
   res = node->data;
   deque_node_destroy(node);
   return res;
}

bool
pgmoneta_deque_empty(struct deque* deque)
{
   return deque->size == 0;
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
   free(deque);
}

struct deque_node*
pgmoneta_deque_node_remove(struct deque* deque, struct deque_node* node)
{
   if (deque == NULL || node == NULL || node == deque->start || node == deque->end)
   {
      return NULL;
   }
   struct deque_node* prev = node->prev;
   struct deque_node* next = node->next;
   prev->next = next;
   next->prev = prev;
   deque_node_destroy(node);
   deque->size--;
   return next;
}

static void
deque_offer(struct deque* deque, void* data, char* tag, enum node_type type)
{
   struct deque_node* n = NULL;
   struct deque_node* last = NULL;
   deque_node_create(&n, data, tag, type);
   deque->size++;
   last = deque->end->prev;
   last->next = n;
   n->prev = last;
   n->next = deque->end;
   deque->end->prev = n;
}

static void
deque_node_create(struct deque_node** node, void* data, char* tag, enum node_type type)
{
   struct deque_node* n = NULL;
   n = malloc(sizeof(struct deque_node));
   n->type = type;
   n->data = data;
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
   if (node->type != NodeRef)
   {
      free(node->data);
   }
   free(node->tag);
   free(node);
}