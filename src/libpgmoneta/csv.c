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
#include <logging.h>
#include <utils.h>

#include <csv.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static bool csv_line_put(struct csv_reader* reader, size_t pos, char c);
static char* csv_append_field(char* row, char* field);

int
pgmoneta_csv_reader_init(char* path, struct csv_reader** reader)
{
   struct csv_reader* r = calloc(1, sizeof(struct csv_reader));

   if (r == NULL)
   {
      goto error;
   }

   if (pgmoneta_fopen_secure(path, "r", &r->file))
   {
      goto error;
   }
   *reader = r;
   return 0;
error:
   if (r != NULL && r->file != NULL)
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
   size_t* starts = NULL;
   size_t* tmp = NULL;
   size_t len = 0;
   int num = 0;
   int c;
   bool in_quotes = false;
   bool any = false;

   if (reader == NULL || reader->file == NULL)
   {
      goto error;
   }

   /* The first field starts at offset 0 */
   starts = malloc(sizeof(size_t));
   if (starts == NULL)
   {
      goto error;
   }
   starts[num++] = 0;

   while ((c = fgetc(reader->file)) != EOF)
   {
      any = true;

      if (in_quotes)
      {
         if (c == '"')
         {
            int next = fgetc(reader->file);

            if (next == '"')
            {
               /* An escaped quote */
            }
            else
            {
               in_quotes = false;
               if (next != EOF)
               {
                  ungetc(next, reader->file);
               }
               continue;
            }
         }
      }
      else if (c == '"' && len == starts[num - 1])
      {
         in_quotes = true;
         continue;
      }
      else if (c == '\r')
      {
         continue;
      }
      else if (c == '\n')
      {
         break;
      }
      else if (c == ',')
      {
         c = '\0';
      }

      if (!csv_line_put(reader, len++, (char)c))
      {
         goto error;
      }

      if (c == '\0')
      {
         tmp = realloc(starts, (num + 1) * sizeof(size_t));
         if (tmp == NULL)
         {
            goto error;
         }
         starts = tmp;
         starts[num++] = len;
      }
   }

   if (!any)
   {
      goto error;
   }

   if (!csv_line_put(reader, len, '\0'))
   {
      goto error;
   }

   cs = malloc(num * sizeof(char*));
   if (cs == NULL)
   {
      goto error;
   }
   for (int i = 0; i < num; i++)
   {
      cs[i] = reader->line + starts[i];
   }

   free(starts);
   *cols = cs;
   *num_col = num;
   return true;
error:
   free(starts);
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
   free(reader->line);
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

   if (w == NULL)
   {
      goto error;
   }
   if (pgmoneta_fopen_secure(path, "w+", &w->file))
   {
      goto error;
   }
   *writer = w;
   return 0;
error:
   if (w != NULL && w->file != NULL)
   {
      fflush(w->file);
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
      row = csv_append_field(row, cols[i]);
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
   int ret = 0;

   if (writer == NULL)
   {
      return 0;
   }
   /* fclose() writes the last buffer, so a full disk can show up only here */
   if (writer->file != NULL && fclose(writer->file) != 0)
   {
      pgmoneta_log_error("CSV: could not write file: %s", strerror(errno));
      ret = 1;
   }
   free(writer);
   return ret;
}

static bool
csv_line_put(struct csv_reader* reader, size_t pos, char c)
{
   if (pos >= reader->capacity)
   {
      size_t capacity = reader->capacity == 0 ? 512 : reader->capacity * 2;
      char* line = realloc(reader->line, capacity);

      if (line == NULL)
      {
         return false;
      }
      reader->line = line;
      reader->capacity = capacity;
   }
   reader->line[pos] = c;
   return true;
}

static char*
csv_append_field(char* row, char* field)
{
   if (field == NULL)
   {
      return pgmoneta_append(row, "");
   }

   /* Quote the field when it holds a character that has a meaning in CSV */
   if (strpbrk(field, ",\"\r\n") == NULL)
   {
      return pgmoneta_append(row, field);
   }

   row = pgmoneta_append_char(row, '"');
   for (char* p = field; *p != '\0'; p++)
   {
      if (*p == '"')
      {
         row = pgmoneta_append_char(row, '"');
      }
      row = pgmoneta_append_char(row, *p);
   }
   return pgmoneta_append_char(row, '"');
}
