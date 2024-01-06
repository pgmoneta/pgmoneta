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

/* pgmoneta */
#include <pgmoneta.h>
#include <logging.h>
#include <node.h>

/* system */
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int
pgmoneta_create_node_string(char* s, char* t, struct node** result)
{
   struct node* node = NULL;

   *result = NULL;

   node = (struct node*)malloc(sizeof(struct node));
   if (node == NULL)
   {
      goto error;
   }

   memset(node, 0, sizeof(struct node));

   if (s != NULL)
   {
      node->data = malloc(strlen(s) + 1);
      if (node->data == NULL)
      {
         goto error;
      }

      memset(node->data, 0, strlen(s) + 1);
      memcpy(node->data, s, strlen(s));
   }
   else
   {
      node->data = NULL;
   }

   if (t != NULL)
   {
      node->tag = malloc(strlen(t) + 1);
      if (node->tag == NULL)
      {
         goto error;
      }

      memset(node->tag, 0, strlen(t) + 1);
      memcpy(node->tag, t, strlen(t));
   }
   else
   {
      goto error;
   }

   node->next = NULL;

   *result = node;

   return 0;

error:

   return 1;
}

int
pgmoneta_create_node_int(int val, char* t, struct node** result)
{
   struct node* node = NULL;
   *result = NULL;

   node = (struct node*)malloc(sizeof(struct node));

   if (node == NULL)
   {
      goto error;
   }

   memset(node, 0, sizeof(struct node));

   node->data = malloc(sizeof(int));

   if (node->data == NULL)
   {
      goto error;
   }

   memset(node->data, 0, sizeof(int));
   memcpy(node->data, &val, sizeof(int));

   if (t != NULL)
   {
      node->tag = malloc(strlen(t) + 1);
      if (node->tag == NULL)
      {
         goto error;
      }

      memset(node->tag, 0, strlen(t) + 1);
      memcpy(node->tag, t, strlen(t));
   }
   else
   {
      goto error;
   }

   node->next = NULL;

   *result = node;

   return 0;

error:
   return 1;
}

int
pgmoneta_create_node_bool(bool val, char* t, struct node** result)
{
   struct node* node = NULL;
   *result = NULL;

   node = (struct node*)malloc(sizeof(struct node));

   if (node == NULL)
   {
      goto error;
   }

   memset(node, 0, sizeof(struct node));

   node->data = malloc(sizeof(bool));

   if (node->data == NULL)
   {
      goto error;
   }

   memset(node->data, 0, sizeof(bool));
   memcpy(node->data, &val, sizeof(bool));

   if (t != NULL)
   {
      node->tag = malloc(strlen(t) + 1);
      if (node->tag == NULL)
      {
         goto error;
      }

      memset(node->tag, 0, strlen(t) + 1);
      memcpy(node->tag, t, strlen(t));
   }
   else
   {
      goto error;
   }

   node->next = NULL;

   *result = node;

   return 0;
error:
   return 1;
}

char*
pgmoneta_get_node_string(struct node* chain, char* t)
{
   struct node* current = NULL;

   current = chain;

   while (current != NULL)
   {

      if (!strcmp(current->tag, t))
      {
         return (char*)current->data;
      }

      current = current->next;
   }

   return NULL;
}

int
pgmoneta_get_node_int(struct node* chain, char* t)
{
   struct node* current = NULL;

   current = chain;

   while (current != NULL)
   {

      if (!strcmp(current->tag, t))
      {
         return *(int*)current->data;
      }

      current = current->next;
   }

   return INT_MIN;
}

bool
pgmoneta_get_node_bool(struct node* chain, char* t)
{
   struct node* current = NULL;

   current = chain;

   while (current != NULL)
   {

      if (!strcmp(current->tag, t))
      {
         return *(bool*)current->data;
      }

      current = current->next;
   }

   return false;
}

void
pgmoneta_append_node(struct node** chain, struct node* node)
{
   struct node* head = NULL;

   head = *chain;

   if (head == NULL)
   {
      *chain = node;
   }
   else
   {
      while (head->next != NULL)
      {
         head = head->next;
      }
      head->next = node;
   }
}

void
pgmoneta_list_nodes(struct node* chain)
{
   struct node* current = NULL;

   current = chain;

   if (current == NULL)
   {
      pgmoneta_log_trace("No nodes");
   }

   while (current != NULL)
   {
      pgmoneta_log_trace("Node: %s -> %p", current->tag, current->data);
      current = current->next;
   }
}

int
pgmoneta_free_nodes(struct node* node)
{
   struct node* current = NULL;
   struct node* nxt = NULL;

   if (node != NULL)
   {
      current = node;

      while (current != NULL)
      {
         nxt = current->next;

         free(current->data);
         free(current->tag);
         free(current);

         current = nxt;
      }
   }

   return 0;
}
