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
 *
 */

/* pgmoneta */
#include <pgmoneta.h>
#include <deque.h>
#include <logging.h>
#include <tscommon.h>
#include <tswalinfo.h>
#include <utils.h>
#include <value.h>
#include <walfile.h>
#include <walfile/wal_reader.h>

/* system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
pgmoneta_tswalinfo_describe(char* path, char** output)
{
   struct walfile* wf = NULL;
   struct column_widths widths = {0};
   struct deque_iterator* record_iterator = NULL;
   struct decoded_xlog_record* record = NULL;
   FILE* out = NULL;
   char* buffer = NULL;
   size_t size = 0;

   if (path == NULL || output == NULL)
   {
      goto error;
   }

   if (pgmoneta_read_walfile(-1, path, &wf))
   {
      goto error;
   }

   pgmoneta_calculate_column_widths(wf, 0, 0, NULL, NULL, NULL, &widths);

   out = open_memstream(&buffer, &size);
   if (out == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_iterator_create(wf->records, &record_iterator))
   {
      goto error;
   }

   while (pgmoneta_deque_iterator_next(record_iterator))
   {
      record = (struct decoded_xlog_record*)record_iterator->value->data;
      pgmoneta_wal_record_display(record, wf->long_phd->std.xlp_magic, ValueString, out, false, false,
                                  NULL, 0, 0, NULL, 0, NULL, &widths);
   }

   pgmoneta_deque_iterator_destroy(record_iterator);
   record_iterator = NULL;

   fclose(out);
   out = NULL;

   *output = buffer;
   pgmoneta_destroy_walfile(wf);
   wf = NULL;

   return 0;

error:
   pgmoneta_deque_iterator_destroy(record_iterator);
   record_iterator = NULL;

   if (out != NULL)
   {
      fclose(out);
      out = NULL;
   }
   free(buffer);
   buffer = NULL;

   pgmoneta_destroy_walfile(wf);
   wf = NULL;
   return 1;
}

int
pgmoneta_walinfo_cli(char* path, const char* arguments, char** output, int* exit_code)
{
   char walinfo_bin[MAX_PATH];
   char* command = NULL;
   int ret = 1;

   if (output == NULL || exit_code == NULL)
   {
      return 1;
   }

   if (pgmoneta_test_resolve_binary_path("pgmoneta-walinfo", walinfo_bin))
   {
      return 1;
   }

   if (path != NULL && arguments != NULL && strlen(arguments) > 0)
   {
      command = pgmoneta_format_and_append(command, "\"%s\" \"%s\" %s", walinfo_bin, path, arguments);
   }
   else if (path != NULL)
   {
      command = pgmoneta_format_and_append(command, "\"%s\" \"%s\"", walinfo_bin, path);
   }
   else if (arguments != NULL && strlen(arguments) > 0)
   {
      command = pgmoneta_format_and_append(command, "\"%s\" %s", walinfo_bin, arguments);
   }
   else
   {
      command = pgmoneta_format_and_append(command, "\"%s\"", walinfo_bin);
   }

   if (command == NULL)
   {
      return 1;
   }

   ret = pgmoneta_test_exec_command(command, output, exit_code);
   free(command);
   return ret;
}
