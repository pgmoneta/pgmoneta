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

/*
 * get_relation_path - construct path to a relation's file
 *
 * Result is a palloc'd string.
 *
 * Note: ideally, backendId would be declared as type backend_id, but relpath.h
 * would have to include a backend-only header to do that; doesn't seem worth
 * the trouble considering backend_id is just int anyway.
 */

#include <walfile/relpath.h>
#include <assert.h>
#include <utils.h>
#include <stdlib.h>

static inline bool
is_valid_fork_number(enum fork_number forkNumber)
{
   return forkNumber >= MAIN_FORKNUM && forkNumber <= INIT_FORKNUM;
}

// clang-format off
static const char* const FORK_NAMES[] =
{
   "main",
   "fsm",
   "vm",
   "init"
};
// clang-format on

char*
pgmoneta_wal_get_relation_path(oid dbNode, oid spcNode, oid relNode,
                               int backendId, enum fork_number forkNumber)
{
   char* path = NULL;

   if (!is_valid_fork_number(forkNumber))
   {
      goto error;
   }

   if (spcNode == GLOBALTABLESPACE_OID)
   {
      if (dbNode != 0 || backendId != INVALID_BACKEND_ID)
      {
         goto error;
      }

      if (forkNumber != MAIN_FORKNUM)
      {
         path = pgmoneta_format_and_append(path, "global/%u_%s", relNode, FORK_NAMES[forkNumber]);
      }
      else
      {
         path = pgmoneta_format_and_append(path, "global/%u", relNode);
      }
   }
   else if (spcNode == DEFAULTTABLESPACE_OID)
   {
      if (backendId == INVALID_BACKEND_ID)
      {
         if (forkNumber != MAIN_FORKNUM)
         {
            path = pgmoneta_format_and_append(path, "base/%u/%u_%s", dbNode, relNode, FORK_NAMES[forkNumber]);
         }
         else
         {
            path = pgmoneta_format_and_append(path, "base/%u/%u", dbNode, relNode);
         }
      }
      else
      {
         if (forkNumber != MAIN_FORKNUM)
         {
            path = pgmoneta_format_and_append(path, "base/%u/t%d_%u_%s", dbNode, backendId, relNode, FORK_NAMES[forkNumber]);
         }
         else
         {
            path = pgmoneta_format_and_append(path, "base/%u/t%d_%u", dbNode, backendId, relNode);
         }
      }
   }
   else
   {
      char* version_directory = pgmoneta_wal_get_tablespace_version_directory();
      if (!version_directory)
      {
         goto error;
      }

      if (backendId == INVALID_BACKEND_ID)
      {
         if (forkNumber != MAIN_FORKNUM)
         {
            path = pgmoneta_format_and_append(path, "pg_tblspc/%u/%s/%u/%u_%s",
                                              spcNode, version_directory, dbNode, relNode, FORK_NAMES[forkNumber]);
         }
         else
         {
            path = pgmoneta_format_and_append(path, "pg_tblspc/%u/%s/%u/%u",
                                              spcNode, version_directory, dbNode, relNode);
         }
      }
      else
      {
         if (forkNumber != MAIN_FORKNUM)
         {
            path = pgmoneta_format_and_append(path, "pg_tblspc/%u/%s/%u/t%d_%u_%s",
                                              spcNode, version_directory, dbNode, backendId, relNode, FORK_NAMES[forkNumber]);
         }
         else
         {
            path = pgmoneta_format_and_append(path, "pg_tblspc/%u/%s/%u/t%d_%u",
                                              spcNode, version_directory, dbNode, backendId, relNode);
         }
      }
      free(version_directory);
   }

   return path;

error:
   free(path);
   return NULL;
}

char*
pgmoneta_wal_get_tablespace_version_directory(void)
{
   char* result = NULL;
   char* catalog_version = NULL;

   if (!server_config)
   {
      goto error;
   }

   result = (char*)malloc(MAX_VERSION_DIR_SIZE);
   if (!result)
   {
      goto error;
   }

   if (!catalog_version)
   {
      goto error;
   }

   if (!pgmoneta_format_and_append(result, "PG_%d_%s",
                                   server_config->version, catalog_version))
   {
      goto error;
   }

   return result;

error:
   free(result);
   return NULL;
}

char*
pgmoneta_wal_get_catalog_version_number(void)
{
   switch (server_config->version)
   {
      case 13:
         return "202004022";
      case 14:
         return "202104081";
      case 15:
         return "202204062";
      case 16:
         return "202303311";
      case 17:
         return "202407111";
      default:
         return NULL;
   }
}