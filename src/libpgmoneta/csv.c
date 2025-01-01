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
#include <utils.h>

#include <csv.h>
#include <stdlib.h>
#include <string.h>

int
pgmoneta_csv_reader_init(char* path, struct csv_reader** reader)
{
   struct csv_reader* r = malloc(sizeof(struct csv_reader));
   r->file = fopen(path, "r");
   memset(r->line, 0, sizeof(r->line));
   if (r->file == NULL)
   {
      goto error;
   }
   *reader = r;
   return 0;
error:
   if (r->file != NULL)
   {
      fclose(r->file);
   }
   free(r);
   return 1;
}

bool
pgmoneta_csv_next_row(struct csv_reader* reader, int* num_col, char*** cols)
{
   char** cs = NULL;
   char* col = NULL;
   char* last_tok = NULL;
   int num = 0;
   if (reader == NULL || reader->file == NULL)
   {
      goto error;
   }
   memset(reader->line, 0, sizeof(reader->line));
   if (fgets(reader->line, sizeof(reader->line), reader->file) == NULL)
   {
      goto error;
   }
   col = strtok(reader->line, ",");
   while (col != NULL)
   {
      cs = realloc(cs, (num + 1) * sizeof(char*));
      cs[num] = col;
      num++;
      col = strtok(NULL, ",");
   }
   // trim the new line from the last token
   if (num > 0)
   {
      last_tok = cs[num - 1];
      last_tok[strlen(last_tok) - 1] = '\0';
   }
   *cols = cs;
   *num_col = num;
   return true;
error:
   free(cs);
   return false;
}

int
pgmoneta_csv_reader_destroy(struct csv_reader* reader)
{
   if (reader == NULL)
   {
      return 0;
   }
   if (reader->file != NULL)
   {
      fclose(reader->file);
   }
   free(reader);
   return 0;
}

int
pgmoneta_csv_reader_reset(struct csv_reader* reader)
{
   if (reader == NULL || reader->file == NULL)
   {
      goto error;
   }
   rewind(reader->file);
   return 0;
error:
   return 1;
}

int
pgmoneta_csv_writer_init(char* path, struct csv_writer** writer)
{
   struct csv_writer* w = malloc(sizeof(struct csv_writer));
   w->file = fopen(path, "w+");
   if (w->file == NULL)
   {
      goto error;
   }
   *writer = w;
   return 0;
error:
   if (w->file != NULL)
   {
      fclose(w->file);
   }
   free(w);
   return 1;
}

int
pgmoneta_csv_write(struct csv_writer* writer, int num_col, char** cols)
{
   char* row = NULL;
   if (writer == NULL || writer->file == NULL)
   {
      goto error;
   }
   for (int i = 0; i < num_col; i++)
   {
      row = pgmoneta_append(row, cols[i]);
      if (i != num_col - 1)
      {
         row = pgmoneta_append(row, ",");
      }
      else
      {
         row = pgmoneta_append(row, "\n");
      }
   }
   fwrite(row, 1, strlen(row), writer->file);
   fflush(writer->file);
   free(row);
   return 0;
error:
   free(row);
   return 1;
}

int
pgmoneta_csv_writer_destroy(struct csv_writer* writer)
{
   if (writer == NULL)
   {
      return 0;
   }
   if (writer->file != NULL)
   {
      fclose(writer->file);
   }
   free(writer);
   return 0;
}
