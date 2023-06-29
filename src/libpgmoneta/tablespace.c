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
#include <pgmoneta.h>
#include <logging.h>
#include <tablespace.h>

/* system */
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int
pgmoneta_create_tablespace(char* name, char* path, struct tablespace** result)
{
   struct tablespace* tablespace = NULL;

   *result = NULL;

   tablespace = (struct tablespace*)malloc(sizeof(struct tablespace));
   if (tablespace == NULL)
   {
      goto error;
   }

   memset(tablespace, 0, sizeof(struct tablespace));

   tablespace->name = malloc(strlen(name) + 1);
   if (tablespace->name == NULL)
   {
      goto error;
   }

   memset(tablespace->name, 0, strlen(name) + 1);
   memcpy(tablespace->name, name, strlen(name));

   tablespace->path = malloc(strlen(path) + 1);
   if (tablespace->path == NULL)
   {
      goto error;
   }

   memset(tablespace->path, 0, strlen(path) + 1);
   memcpy(tablespace->path, path, strlen(path));

   tablespace->next = NULL;

   *result = tablespace;

   return 0;

error:

   return 1;
}

void
pgmoneta_append_tablespace(struct tablespace** chain, struct tablespace* tablespace)
{
   struct tablespace* head = NULL;

   head = *chain;

   if (head == NULL)
   {
      *chain = tablespace;
   }
   else
   {
      while (head->next != NULL)
      {
         head = head->next;
      }
      head->next = tablespace;
   }
}

void
pgmoneta_list_tablespaces(struct tablespace* chain)
{
   struct tablespace* current = NULL;

   current = chain;

   if (current == NULL)
   {
      pgmoneta_log_trace("No tablespaces");
   }

   while (current != NULL)
   {
      pgmoneta_log_trace("Tablespace: %s -> %p", current->name, current->path);
      current = current->next;
   }
}

int
pgmoneta_free_tablespaces(struct tablespace* tablespace)
{
   struct tablespace* current = NULL;
   struct tablespace* nxt = NULL;

   if (tablespace != NULL)
   {
      current = tablespace;
      nxt = current->next;

      while (current != NULL)
      {
         nxt = current->next;

         free(current->name);
         free(current->path);
         free(current);

         current = nxt;
      }
   }

   return 0;
}
