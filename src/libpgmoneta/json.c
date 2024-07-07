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
#include <logging.h>
#include <pgmoneta.h>
#include <json.h>
#include <message.h>
#include <utils.h>
/* System */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/**
 * Advance to the first array element
 * @param reader The reader
 * @return 0 if success, otherwise 1
 */
static int
advance_to_first_array_element(struct json_reader* reader);

static void
print_indent(int indent);

static void
print_json_item(struct json* item, int indent, int indent_per_level);

static void
print_json_array_value(struct json_value* value, int indent, int indent_per_level);

static void
print_json_array(struct json* array, int indent, int indent_per_level);

static void
print_json_state(enum json_reader_state state);

static bool
is_array_typed_value(enum json_value_type type);

int
pgmoneta_json_read(struct json_reader* reader)
{
   if (reader->fd < 0)
   {
      goto error;
   }
   int numbytes = 0;
   numbytes = read(reader->fd, reader->buffer->buffer + reader->buffer->end, reader->buffer->size - reader->buffer->end);

   if (numbytes > 0)
   {
      reader->buffer->end += numbytes;
      return MESSAGE_STATUS_OK;
   }
   else if (numbytes == 0)
   {
      return MESSAGE_STATUS_ZERO;
   }
   else
   {
      if (errno != 0)
      {
         errno = 0;
         pgmoneta_log_error("error creating json reader, %s", strerror(errno));
      }
      goto error;
   }
error:
   return MESSAGE_STATUS_ERROR;
}

bool
pgmoneta_json_next_char(struct json_reader* reader, char* next)
{
   int status = 0;
   if (reader->buffer->cursor == reader->buffer->end)
   {
      reader->buffer->end = 0;
      reader->buffer->cursor = 0;
      status = pgmoneta_json_read(reader);
      if (status == MESSAGE_STATUS_ZERO || status == MESSAGE_STATUS_ERROR)
      {
         return false;
      }
   }
   *next = *(reader->buffer->buffer + reader->buffer->cursor);
   reader->buffer->cursor++;
   return true;
}

bool
pgmoneta_json_peek_next_char(struct json_reader* reader, char* next)
{
   int status = 0;
   if (reader->buffer->cursor == reader->buffer->end)
   {
      reader->buffer->end = 0;
      reader->buffer->cursor = 0;
      status = pgmoneta_json_read(reader);
      if (status == MESSAGE_STATUS_ZERO || status == MESSAGE_STATUS_ERROR)
      {
         return false;
      }
   }
   *next = *(reader->buffer->buffer + reader->buffer->cursor);
   return true;
}

int
pgmoneta_json_reader_init(char* path, struct json_reader** reader)
{
   int fd = -1;
   struct json_reader* r = NULL;
   fd = open(path, O_RDONLY);
   if (fd < 0)
   {
      goto error;
   }
   r = malloc(sizeof(struct json_reader));
   pgmoneta_memory_stream_buffer_init(&r->buffer);
   r->fd = fd;
   r->state = InvalidReaderState;
   // read until the first '{' or '[', and set the state correspondingly
   char ch = 0;
   while (pgmoneta_json_next_char(r, &ch))
   {
      if (ch != '{' && ch != '[')
      {
         continue;
      }
      if (ch == '{')
      {
         r->state = ItemStart;
      }
      else
      {
         r->state = ArrayStart;
         if (advance_to_first_array_element(r))
         {
            goto error;
         }
      }
      break;
   }
   *reader = r;
   return 0;
error:
   return 1;
}

void
pgmoneta_json_close_reader(struct json_reader* reader)
{
   if (reader == NULL)
   {
      return;
   }
   if (reader->fd > 0)
   {
      close(reader->fd);
   }
   pgmoneta_memory_stream_buffer_free(reader->buffer);
   reader->buffer = NULL;
   free(reader);
}

int
pgmoneta_json_locate(struct json_reader* reader, char** key_path, int key_path_length)
{
   char ch = 0;
   char* cur_key = NULL;
   if (reader == NULL || reader->state == InvalidReaderState)
   {
      goto error;
   }
   if (reader->state == ArrayStart || reader->state == ArrayEnd)
   {
      if (key_path_length == 0)
      {
         return 0;
      }
      else
      {
         goto error;
      }
   }
   for (int i = 0; i < key_path_length; i++)
   {
      char* key = key_path[i];
      cur_key = NULL;
      while (pgmoneta_json_next_char(reader, &ch))
      {
         if (ch != '"' && ch != ':' && ch != '{' && ch != '}' &&
             !(reader->state == ValueStart && (isdigit(ch) || ch == '[')))
         {
            if (reader->state == KeyStart)
            {
               cur_key = pgmoneta_append_char(cur_key, ch);
            }
            continue;
         }
         if (reader->state == KeyStart)
         {
            if (ch == '"')
            {
               reader->state = KeyEnd;
            }
            else
            {
               goto error;
            }
         }
         else if (reader->state == KeyEnd)
         {
            if (ch == ':')
            {
               reader->state = ValueStart;
            }
            else
            {
               goto error;
            }
         }
         else if (reader->state == ValueStart)
         {
            if (cur_key == NULL)
            {
               goto error;
            }
            // if the cur_key matches current key in path
            if (!strcmp(cur_key, key))
            {
               if (i == key_path_length - 1)
               {
                  // the last key, hooray! Fast-forward to where value really starts and return
                  if (ch == '[' || ch == '{')
                  {
                     reader->state = ch == '{' ? ItemStart : ArrayStart;
                     if (reader->state == ArrayStart)
                     {
                        if (advance_to_first_array_element(reader))
                        {
                           goto error;
                        }
                     }
                     goto done;
                  }
                  else
                  {
                     goto error;
                  }
               }
               else
               {
                  if (ch == '{')
                  {
                     reader->state = ItemStart;
                     // break from while loop and start searching with the next key
                     break;
                  }
                  else
                  {
                     // we are expecting an item, anything else is illegal
                     goto error;
                  }
               }
            }
            else
            {
               // key is not in current path, fast-forward to value ends
               if (pgmoneta_json_fast_forward_value(reader, ch))
               {
                  goto error;
               }
               free(cur_key);
               cur_key = NULL;
            }
         }
         else if (reader->state == ValueEnd)
         {
            if (ch == '"')
            {
               reader->state = KeyStart;
            }
            else if (ch == '}')
            {
               reader->state = ItemEnd;
               goto error;
            }
            else
            {
               goto error;
            }
         }
         else if (reader->state == ArrayStart)
         {
            // currently it shouldn't end up here
            goto error;
         }
         else if (reader->state == ArrayEnd)
         {
            // currently it shouldn't end up here
            goto error;
         }
         else if (reader->state == ItemStart)
         {
            if (ch == '"')
            {
               reader->state = KeyStart;
            }
            else if (ch == '}')
            {
               reader->state = ItemEnd;
            }
            else
            {
               goto error;
            }
         }
         else if (reader->state == ItemEnd)
         {
            // dead end
            goto error;
         }
         else
         {
            goto error;
         }

      }
      free(cur_key);
      cur_key = NULL;
      if (!pgmoneta_json_peek_next_char(reader, &ch))
      {
         goto error;
      }
   }
done:
   free(cur_key);
   return 0;
error:
   free(cur_key);
   reader->state = InvalidReaderState;
   return 1;
}

bool
pgmoneta_json_next_array_item(struct json_reader* reader, struct json** item)
{
   char ch = 0;
   if (reader->state != ArrayStart)
   {
      goto done;
   }
   if (pgmoneta_json_peek_next_char(reader, &ch) && ch != '{')
   {
      goto done;
   }
   pgmoneta_json_next_char(reader, &ch);
   reader->state = ItemStart;
   // parse the item, it'll be nicer to push the state to stack, but is not necessary right now
   if (pgmoneta_json_stream_parse_item(reader, item))
   {
      goto done;
   }
   reader->state = ArrayStart;
   // fast-forward to the next item
   while (pgmoneta_json_peek_next_char(reader, &ch))
   {
      if (ch == '{')
      {
         break;
      }
      else if (ch == ']')
      {
         reader->state = ArrayEnd;
         pgmoneta_json_next_char(reader, &ch);
         break;
      }
      pgmoneta_json_next_char(reader, &ch);
   }
   return true;
done:
   reader->state = InvalidReaderState;
   return false;
}

int
pgmoneta_json_stream_parse_item(struct json_reader* reader, struct json** item)
{
   struct json* i = NULL;
   char* key = NULL;
   char ch = 0;
   pgmoneta_json_init(&i, JSONItem);
   if (reader->state != ItemStart)
   {
      goto error;
   }
   while (pgmoneta_json_next_char(reader, &ch))
   {
      if (ch != '"' && ch != ':' && ch != '{' && ch != '}' &&
          !(reader->state == ValueStart && (isdigit(ch) || ch == '[')))
      {
         if (reader->state == KeyStart)
         {
            key = pgmoneta_append_char(key, ch);
         }
         continue;
      }
      if (reader->state == ItemStart)
      {
         if (ch == '"')
         {
            reader->state = KeyStart;
         }
         else
         {
            goto error;
         }
      }
      else if (reader->state == KeyStart)
      {
         if (ch == '"')
         {
            reader->state = KeyEnd;
         }
         else
         {
            goto error;
         }
      }
      else if (reader->state == KeyEnd)
      {
         if (ch == ':')
         {
            reader->state = ValueStart;
         }
         else
         {
            goto error;
         }
      }
      else if (reader->state == ValueStart)
      {
         if (key == NULL)
         {
            goto error;
         }
         if (ch == '[' || ch == '{')
         {
            if (pgmoneta_json_fast_forward_value(reader, ch))
            {
               goto error;
            }
            free(key);
            key = NULL;
         }
         else if (ch == '"' || isdigit(ch))
         {
            if (ch == '"')
            {
               char* str = NULL;
               while (pgmoneta_json_next_char(reader, &ch) && ch != '"')
               {
                  str = pgmoneta_append_char(str, ch);
               }
               if (ch != '"')
               {
                  free(str);
                  goto error;
               }
               pgmoneta_json_item_put_string(i, key, str);
               free(key);
               free(str);
               key = NULL;
               str = NULL;
            }
            else
            {
               bool has_digit_point = false;
               char* str = NULL;
               str = pgmoneta_append_char(str, ch);
               // peek first in case we advance to non-digit accidentally
               while (pgmoneta_json_peek_next_char(reader, &ch) && (isdigit(ch) || ch == '.'))
               {
                  if (ch == '.')
                  {
                     if (has_digit_point)
                     {
                        free(str);
                        goto error;
                     }
                     else
                     {
                        has_digit_point = true;
                     }
                  }
                  str = pgmoneta_append_char(str, ch);
                  // advance
                  pgmoneta_json_next_char(reader, &ch);
               }
               if (isdigit(ch) || ch == '.')
               {
                  free(str);
                  goto error;
               }
               if (has_digit_point)
               {
                  float num = 0;
                  if (sscanf(str, "%f", &num) != 1)
                  {
                     free(str);
                     goto error;
                  }
                  free(str);
                  str = NULL;
                  pgmoneta_json_item_put_float(i, key, num);
               }
               else
               {
                  int64_t num = 0;
                  if (sscanf(str, "%" PRId64, &num) != 1)
                  {
                     free(str);
                     goto error;
                  }
                  free(str);
                  str = NULL;
                  pgmoneta_json_item_put_int64(i, key, num);
               }
               free(key);
               key = NULL;
            }
         }
         else
         {
            goto error;
         }
         reader->state = ValueEnd;
      }
      else if (reader->state == ValueEnd)
      {
         if (ch == '"')
         {
            reader->state = KeyStart;
         }
         else if (ch == '}')
         {
            reader->state = ItemEnd;
            break;
         }
      }
      else
      {
         goto error;
      }
   }
   *item = i;
   return 0;
error:
   pgmoneta_json_free(i);
   free(key);
   return 1;
}

struct json_value*
pgmoneta_json_get_value(struct json* item, char* key)
{
   struct json_element* e = NULL;
   if (item->type == JSONArray)
   {
      return NULL;
   }
   e = item->element;
   while (e != NULL)
   {
      if (!strcmp(e->key, key))
      {
         return e->value;
      }
      e = e->next;
   }
   return NULL;
}

int8_t
pgmoneta_json_get_int8_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return 0;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueInt8)
   {
      return 0;
   }
   return pgmoneta_read_byte(val->payload);
}

uint8_t
pgmoneta_json_get_uint8_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return 0;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueUInt8)
   {
      return 0;
   }
   return pgmoneta_read_uint8(val->payload);
}

int16_t
pgmoneta_json_get_int16_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return 0;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueInt16)
   {
      return 0;
   }
   return pgmoneta_read_int16(val->payload);
}

uint16_t
pgmoneta_json_get_uint16_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return 0;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueUInt16)
   {
      return 0;
   }
   return pgmoneta_read_uint16(val->payload);
}

int32_t
pgmoneta_json_get_int32_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return 0;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueInt32)
   {
      return 0;
   }
   return pgmoneta_read_int32(val->payload);
}

uint32_t
pgmoneta_json_get_uint32_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return 0;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueUInt32)
   {
      return 0;
   }
   return pgmoneta_read_uint32(val->payload);
}

int64_t
pgmoneta_json_get_int64_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return 0;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueInt64)
   {
      return 0;
   }
   return pgmoneta_read_int64(val->payload);
}

uint64_t
pgmoneta_json_get_uint64_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return 0;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueUInt64)
   {
      return 0;
   }
   return pgmoneta_read_uint64(val->payload);
}

bool
pgmoneta_json_get_bool_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return 0;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueBool)
   {
      return 0;
   }
   return pgmoneta_read_bool(val->payload);
}

int8_t*
pgmoneta_json_get_int8_array_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueInt8Array)
   {
      return NULL;
   }
   return (int8_t*)val->payload;
}

uint8_t*
pgmoneta_json_get_uint8_array_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueUInt8Array)
   {
      return NULL;
   }
   return (uint8_t*)val->payload;
}

int16_t*
pgmoneta_json_get_int16_array_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueInt16Array)
   {
      return NULL;
   }
   return (int16_t*)val->payload;
}

uint16_t*
pgmoneta_json_get_uint16_array_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueUInt16Array)
   {
      return NULL;
   }
   return (uint16_t*)val->payload;
}

int32_t*
pgmoneta_json_get_int32_array_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueInt32Array)
   {
      return NULL;
   }
   return (int32_t*)val->payload;
}

uint32_t*
pgmoneta_json_get_uint32_array_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueUInt32Array)
   {
      return NULL;
   }
   return (uint32_t*)val->payload;
}

int64_t*
pgmoneta_json_get_int64_array_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueInt64Array)
   {
      return NULL;
   }
   return (int64_t*)val->payload;
}

uint64_t*
pgmoneta_json_get_uint64_array_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueUInt64Array)
   {
      return NULL;
   }
   return (uint64_t*)val->payload;
}

char*
pgmoneta_json_get_string_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueString)
   {
      return NULL;
   }
   return (char*)val->payload;
}

char**
pgmoneta_json_get_string_array_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueStringArray)
   {
      return NULL;
   }
   return (char**)val->payload;
}

float
pgmoneta_json_get_float_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return 0;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueFloat)
   {
      return 0;
   }
   return *((float*)val->payload);
}

float*
pgmoneta_json_get_float_array_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueFloatArray)
   {
      return NULL;
   }
   return (float*)val->payload;
}

struct json*
pgmoneta_json_get_json_object_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || val->type != ValueObject)
   {
      return NULL;
   }
   return (struct json*)val->payload;
}

struct json**
pgmoneta_json_get_json_object_array_value(struct json* item, char* key)
{
   struct json_value* val = NULL;
   if (item == NULL)
   {
      return NULL;
   }
   val = pgmoneta_json_get_value(item, key);
   if (val == NULL || (val->type != ValueItemArray && val->type != ValueArrayArray))
   {
      return NULL;
   }
   return (struct json**)val->payload;
}

int
pgmoneta_json_fast_forward_value(struct json_reader* reader, char ch)
{
   if (reader == NULL || reader->state != ValueStart)
   {
      goto error;
   }
   bool has_next = true;
   while (ch != '{' && ch != '"' && ch != '[' && !isdigit(ch) && has_next)
   {
      has_next = pgmoneta_json_next_char(reader, &ch);
   }
   if (ch == '{')
   {
      int count = 1;
      while (pgmoneta_json_next_char(reader, &ch) && count != 0)
      {
         if (ch == '{')
         {
            count++;
         }
         else if (ch == '}')
         {
            count--;
         }
      }
      if (count != 0)
      {
         goto error;
      }
   }
   else if (ch == '"')
   {
      while (pgmoneta_json_next_char(reader, &ch))
      {
         if (ch == '"')
         {
            break;
         }
      }
      if (ch != '"')
      {
         goto error;
      }
   }
   else if (ch == '[')
   {
      int count = 1;
      while (pgmoneta_json_next_char(reader, &ch) && count != 0)
      {
         if (ch == '[')
         {
            count++;
         }
         else if (ch == ']')
         {
            count--;
         }
      }
      if (count != 0)
      {
         goto error;
      }
   }
   else if (isdigit(ch))
   {
      while (pgmoneta_json_next_char(reader, &ch))
      {
         if (!isdigit(ch) && ch != '.')
         {
            break;
         }
      }
      if (isdigit(ch) || ch == '.')
      {
         goto error;
      }
   }
   else
   {
      goto error;
   }
   reader->state = ValueEnd;
   return 0;
error:
   return 1;
}

int
pgmoneta_json_init(struct json** object, enum json_type type)
{
   struct json* o = malloc(sizeof(struct json));
   memset(o, 0, sizeof(struct json));
   o->type = type;
   *object = o;
   return 0;
}

int
pgmoneta_json_free(struct json* object)
{
   if (object == NULL)
   {
      return 0;
   }
   struct json_element* e = object->element;
   while (e != NULL)
   {
      e = pgmoneta_json_element_free(e);
   }
   free(object);
   return 0;
}

int
pgmoneta_json_element_init(struct json_element** element)
{
   struct json_element* e = malloc(sizeof(struct json_element));
   memset(e, 0, sizeof(struct json_element));
   *element = e;
   return 0;
}

struct json_element*
pgmoneta_json_element_free(struct json_element* element)
{
   if (element == NULL)
   {
      return NULL;
   }
   free(element->key);
   pgmoneta_json_value_free(element->value);
   struct json_element* e = element->next;
   free(element);
   return e;
}

int
pgmoneta_json_value_init(struct json_value** value, enum json_value_type type)
{
   struct json_value* v = malloc(sizeof(struct json_value));
   memset(v, 0, sizeof(struct json_value));
   v->type = type;
   *value = v;
   return 0;
}

int
pgmoneta_json_value_free(struct json_value* value)
{
   if (value == NULL)
   {
      return 0;
   }
   switch (value->type)
   {
      case ValueInt8:
      case ValueUInt8:
      case ValueInt16:
      case ValueUInt16:
      case ValueInt32:
      case ValueUInt32:
      case ValueInt64:
      case ValueUInt64:
      case ValueBool:
      case ValueString:
      case ValueFloat:
      case ValueFloatArray:
      case ValueInt8Array:
      case ValueUInt8Array:
      case ValueInt16Array:
      case ValueUInt16Array:
      case ValueInt32Array:
      case ValueUInt32Array:
      case ValueInt64Array:
      case ValueUInt64Array:
         // payload points to actual memory
         free(value->payload);
         value->payload = NULL;
         break;
      case ValueStringArray:
         // payload points to an array of pointers to actual memories
         for (int i = 0; i < value->length; i++)
         {
            free(*((char**)value->payload + i));
         }
         free(value->payload);
         value->payload = NULL;
         break;
      case ValueObject:
         // payload points to actual memory that contains pointers
         pgmoneta_json_free((struct json*)value->payload);
         break;
      case ValueItemArray:
      case ValueArrayArray:
         // payload points to actual memory that contains pointers
         for (int i = 0; i < value->length; i++)
         {
            pgmoneta_json_free(*((struct json**)value->payload + i));
         }
         free(value->payload);
         value->payload = NULL;
         break;
   }
   value->payload = NULL;
   free(value);
   return 0;
}

int
pgmoneta_json_value_append(struct json_value* value, void* ptr)
{
   if (value == NULL || ptr == NULL)
   {
      goto error;
   }
   switch (value->type)
   {
      case ValueInt64Array:
         value->payload = realloc(value->payload, (value->length + 1) * sizeof(int64_t));
         ((int64_t*)value->payload)[value->length] = *((int64_t*)ptr);
         value->length++;
         break;
      case ValueFloatArray:
         value->payload = realloc(value->payload, (value->length + 1) * sizeof(float));
         ((float*)value->payload)[value->length] = *((float*)ptr);
         value->length++;
         break;
      case ValueStringArray:
      {
         char* str = NULL;
         str = pgmoneta_append(str, ptr);
         value->payload = realloc(value->payload, (value->length + 1) * sizeof(char*));
         ((char**)value->payload)[value->length] = str;
         value->length++;
         break;
      }
      case ValueArrayArray:
      case ValueItemArray:
         value->payload = realloc(value->payload, (value->length + 1) * sizeof(struct json**));
         ((struct json**)value->payload)[value->length] = (struct json*)ptr;
         value->length++;
         break;
      default:
         goto error;
   }
   return 0;
error:
   return 1;
}

int
pgmoneta_json_array_append(struct json* array, void* ptr)
{
   if (array == NULL || array->element == NULL)
   {
      return 1;
   }
   return pgmoneta_json_value_append(array->element->value, ptr);
}

int
pgmoneta_json_add(struct json* object, struct json_element* element)
{
   if (object == NULL || element == NULL)
   {
      goto error;
   }
   // a json array can have at most one json element, use append to add element in array
   if (object->type == JSONArray && object->element != NULL)
   {
      goto error;
   }
   element->next = object->element;
   object->element = element;
   return 0;
error:
   return 1;
}

int
pgmoneta_json_object_put(struct json* object, char* key, enum json_value_type type, void* payload, unsigned short length)
{
   struct json_element* element = NULL;
   if (object == NULL || (object->type == JSONItem && key == NULL) || (object->type == JSONItem && payload == NULL))
   {
      goto error;
   }
   // can't add element to json array if key is not null or it already has an element
   if (object->type == JSONArray && (key != NULL || object->element != NULL))
   {
      goto error;
   }
   // empty payload of an array object should have length of 0
   if (object->type == JSONArray && payload == NULL && length != 0)
   {
      goto error;
   }
   // Array type should match
   if (object->type == JSONArray && !is_array_typed_value(type))
   {
      goto error;
   }
   pgmoneta_json_element_init(&element);
   pgmoneta_json_value_init(&element->value, type);
   element->value->payload = payload;
   element->value->length = length;
   element->key = pgmoneta_append(element->key, key);
   if (pgmoneta_json_add(object, element))
   {
      goto error;
   }
   return 0;
error:
   // only free the element and key value space, not the payload
   if (element != NULL)
   {
      free(element->value);
      free(element->key);
   }
   free(element);
   return 1;
}

int
pgmoneta_json_item_put_string(struct json* item, char* key, char* val)
{
   if (val == NULL)
   {
      return 1;
   }
   char* payload = NULL;
   struct json_value* strval = pgmoneta_json_get_value(item, key);
   if (strval != NULL && strval->type != ValueString)
   {
      pgmoneta_log_error("json key exists but not the string type");
      return 1;
   }
   payload = pgmoneta_append(payload, val);
   if (strval == NULL)
   {
      return pgmoneta_json_object_put(item, key, ValueString, (void*)payload, 0);
   }
   else
   {
      free(strval->payload);
      strval->payload = (void*)payload;
   }
   return 0;
}

int
pgmoneta_json_item_put_int8(struct json* item, char* key, int8_t val)
{
   void* payload = NULL;
   struct json_value* intval = pgmoneta_json_get_value(item, key);
   if (intval != NULL && intval->type != ValueInt8)
   {
      pgmoneta_log_error("json key exists but not the int8 type");
      return 1;
   }
   payload = malloc(sizeof(int8_t));
   // int value is stored in place
   pgmoneta_write_byte(payload, val);
   if (intval == NULL)
   {
      return pgmoneta_json_object_put(item, key, ValueInt8, payload, 0);
   }
   else
   {
      free(intval->payload);
      intval->payload = payload;
   }
   return 0;
}

int
pgmoneta_json_item_put_uint8(struct json* item, char* key, uint8_t val)
{
   void* payload = NULL;
   struct json_value* intval = pgmoneta_json_get_value(item, key);
   if (intval != NULL && intval->type != ValueUInt8)
   {
      pgmoneta_log_error("json key exists but not the uint8 type");
      return 1;
   }
   payload = malloc(sizeof(uint8_t));
   // int value is stored in place
   pgmoneta_write_uint8(payload, val);
   if (intval == NULL)
   {
      return pgmoneta_json_object_put(item, key, ValueUInt8, payload, 0);
   }
   else
   {
      free(intval->payload);
      intval->payload = payload;
   }
   return 0;
}

int
pgmoneta_json_item_put_int16(struct json* item, char* key, int16_t val)
{
   void* payload = NULL;
   struct json_value* intval = pgmoneta_json_get_value(item, key);
   if (intval != NULL && intval->type != ValueInt16)
   {
      pgmoneta_log_error("json key exists but not the int16 type");
      return 1;
   }
   payload = malloc(sizeof(int16_t));
   // int value is stored in place
   pgmoneta_write_int16(payload, val);
   if (intval == NULL)
   {
      return pgmoneta_json_object_put(item, key, ValueInt16, payload, 0);
   }
   else
   {
      free(intval->payload);
      intval->payload = payload;
   }
   return 0;
}

int
pgmoneta_json_item_put_uint16(struct json* item, char* key, uint16_t val)
{
   void* payload = NULL;
   struct json_value* intval = pgmoneta_json_get_value(item, key);
   if (intval != NULL && intval->type != ValueUInt16)
   {
      pgmoneta_log_error("json key exists but not the uint16 type");
      return 1;
   }
   payload = malloc(sizeof(uint16_t));
   // int value is stored in place
   pgmoneta_write_uint16(payload, val);
   if (intval == NULL)
   {
      return pgmoneta_json_object_put(item, key, ValueUInt16, payload, 0);
   }
   else
   {
      free(intval->payload);
      intval->payload = payload;
   }
   return 0;
}

int
pgmoneta_json_item_put_int32(struct json* item, char* key, int32_t val)
{
   void* payload = NULL;
   struct json_value* intval = pgmoneta_json_get_value(item, key);
   if (intval != NULL && intval->type != ValueInt32)
   {
      pgmoneta_log_error("json key exists but not the int32 type");
      return 1;
   }
   payload = malloc(sizeof(int32_t));
   // int value is stored in place
   pgmoneta_write_int32(payload, val);
   if (intval == NULL)
   {
      return pgmoneta_json_object_put(item, key, ValueInt32, payload, 0);
   }
   else
   {
      free(intval->payload);
      intval->payload = payload;
   }
   return 0;
}

int
pgmoneta_json_item_put_uint32(struct json* item, char* key, uint32_t val)
{
   void* payload = NULL;
   struct json_value* intval = pgmoneta_json_get_value(item, key);
   if (intval != NULL && intval->type != ValueUInt32)
   {
      pgmoneta_log_error("json key exists but not the uint32 type");
      return 1;
   }
   payload = malloc(sizeof(uint32_t));
   // int value is stored in place
   pgmoneta_write_uint32(payload, val);
   if (intval == NULL)
   {
      return pgmoneta_json_object_put(item, key, ValueUInt32, payload, 0);
   }
   else
   {
      free(intval->payload);
      intval->payload = payload;
   }
   return 0;
}

int
pgmoneta_json_item_put_int64(struct json* item, char* key, int64_t val)
{
   void* payload = NULL;
   struct json_value* intval = pgmoneta_json_get_value(item, key);
   if (intval != NULL && intval->type != ValueInt64)
   {
      pgmoneta_log_error("json key exists but not the int64 type");
      return 1;
   }
   payload = malloc(sizeof(int64_t));
   // int value is stored in place
   pgmoneta_write_int64(payload, val);
   if (intval == NULL)
   {
      return pgmoneta_json_object_put(item, key, ValueInt64, payload, 0);
   }
   else
   {
      free(intval->payload);
      intval->payload = payload;
   }
   return 0;
}

int
pgmoneta_json_item_put_uint64(struct json* item, char* key, uint64_t val)
{
   void* payload = NULL;
   struct json_value* intval = pgmoneta_json_get_value(item, key);
   if (intval != NULL && intval->type != ValueUInt64)
   {
      pgmoneta_log_error("json key exists but not the uint64 type");
      return 1;
   }
   payload = malloc(sizeof(uint64_t));
   // int value is stored in place
   pgmoneta_write_uint64(payload, val);
   if (intval == NULL)
   {
      return pgmoneta_json_object_put(item, key, ValueUInt64, payload, 0);
   }
   else
   {
      free(intval->payload);
      intval->payload = payload;
   }
   return 0;
}

int
pgmoneta_json_item_put_bool(struct json* item, char* key, bool val)
{
   void* payload = NULL;
   struct json_value* intval = pgmoneta_json_get_value(item, key);
   if (intval != NULL && intval->type != ValueBool)
   {
      pgmoneta_log_error("json key exists but not the bool type");
      return 1;
   }
   payload = malloc(sizeof(bool));
   // int value is stored in place
   pgmoneta_write_byte(payload, val);
   if (intval == NULL)
   {
      return pgmoneta_json_object_put(item, key, ValueBool, payload, 0);
   }
   else
   {
      free(intval->payload);
      intval->payload = payload;
   }
   return 0;
}

int
pgmoneta_json_item_put_float(struct json* item, char* key, float val)
{
   float* payload = malloc(sizeof(float));
   *payload = val;
   return pgmoneta_json_object_put(item, key, ValueFloat, payload, 0);
}

int
pgmoneta_json_item_put_object(struct json* item, char* key, struct json* val)
{
   return pgmoneta_json_object_put(item, key, ValueObject, (void*)val, 0);
}

int
pgmoneta_json_item_put_int8_array(struct json* item, char* key, int8_t* vals, unsigned short length)
{
   return pgmoneta_json_object_put(item, key, ValueInt8Array, vals, length);
}

int
pgmoneta_json_item_put_uint8_array(struct json* item, char* key, uint8_t* vals, unsigned short length)
{
   return pgmoneta_json_object_put(item, key, ValueUInt8Array, vals, length);
}

int
pgmoneta_json_item_put_int16_array(struct json* item, char* key, int16_t* vals, unsigned short length)
{
   return pgmoneta_json_object_put(item, key, ValueInt16Array, vals, length);
}

int
pgmoneta_json_item_put_uint16_array(struct json* item, char* key, uint16_t* vals, unsigned short length)
{
   return pgmoneta_json_object_put(item, key, ValueUInt16Array, vals, length);
}
int
pgmoneta_json_item_put_int32_array(struct json* item, char* key, int32_t* vals, unsigned short length)
{
   return pgmoneta_json_object_put(item, key, ValueInt32Array, vals, length);
}

int
pgmoneta_json_item_put_uint32_array(struct json* item, char* key, uint32_t* vals, unsigned short length)
{
   return pgmoneta_json_object_put(item, key, ValueUInt32Array, vals, length);
}
int
pgmoneta_json_item_put_int64_array(struct json* item, char* key, int64_t* vals, unsigned short length)
{
   return pgmoneta_json_object_put(item, key, ValueInt64Array, vals, length);
}

int
pgmoneta_json_item_put_uint64_array(struct json* item, char* key, uint64_t* vals, unsigned short length)
{
   return pgmoneta_json_object_put(item, key, ValueUInt64Array, vals, length);
}

int
pgmoneta_json_item_put_item_array(struct json* item, char* key, struct json* items, unsigned short length)
{
   return pgmoneta_json_object_put(item, key, ValueItemArray, items, length);
}

int
pgmoneta_json_array_append_int8(struct json* array, int8_t val)
{
   if (array == NULL)
   {
      goto error;
   }
   if (array->element == NULL)
   {
      pgmoneta_json_object_put(array, NULL, ValueInt8Array, NULL, 0);
   }
   struct json_value* value = array->element->value;
   // type check
   if (array->type != JSONArray || value->type != ValueInt8Array)
   {
      goto error;
   }
   return pgmoneta_json_value_append(value, &val);
error:
   return 1;
}

int
pgmoneta_json_array_append_uint8(struct json* array, uint8_t val)
{
   if (array == NULL)
   {
      goto error;
   }
   if (array->element == NULL)
   {
      pgmoneta_json_object_put(array, NULL, ValueUInt8Array, NULL, 0);
   }
   struct json_value* value = array->element->value;
   // type check
   if (array->type != JSONArray || value->type != ValueUInt8Array)
   {
      goto error;
   }
   return pgmoneta_json_value_append(value, &val);
error:
   return 1;
}

int
pgmoneta_json_array_append_int16(struct json* array, int16_t val)
{
   if (array == NULL)
   {
      goto error;
   }
   if (array->element == NULL)
   {
      pgmoneta_json_object_put(array, NULL, ValueInt16Array, NULL, 0);
   }
   struct json_value* value = array->element->value;
   // type check
   if (array->type != JSONArray || value->type != ValueInt16Array)
   {
      goto error;
   }
   return pgmoneta_json_value_append(value, &val);
error:
   return 1;
}

int
pgmoneta_json_array_append_uint16(struct json* array, uint16_t val)
{
   if (array == NULL)
   {
      goto error;
   }
   if (array->element == NULL)
   {
      pgmoneta_json_object_put(array, NULL, ValueUInt16Array, NULL, 0);
   }
   struct json_value* value = array->element->value;
   // type check
   if (array->type != JSONArray || value->type != ValueUInt16Array)
   {
      goto error;
   }
   return pgmoneta_json_value_append(value, &val);
error:
   return 1;
}

int
pgmoneta_json_array_append_int32(struct json* array, int32_t val)
{
   if (array == NULL)
   {
      goto error;
   }
   if (array->element == NULL)
   {
      pgmoneta_json_object_put(array, NULL, ValueInt32Array, NULL, 0);
   }
   struct json_value* value = array->element->value;
   // type check
   if (array->type != JSONArray || value->type != ValueInt32Array)
   {
      goto error;
   }
   return pgmoneta_json_value_append(value, &val);
error:
   return 1;
}

int
pgmoneta_json_array_append_uint32(struct json* array, uint32_t val)
{
   if (array == NULL)
   {
      goto error;
   }
   if (array->element == NULL)
   {
      pgmoneta_json_object_put(array, NULL, ValueUInt32Array, NULL, 0);
   }
   struct json_value* value = array->element->value;
   // type check
   if (array->type != JSONArray || value->type != ValueUInt32Array)
   {
      goto error;
   }
   return pgmoneta_json_value_append(value, &val);
error:
   return 1;
}

int
pgmoneta_json_array_append_int64(struct json* array, int64_t val)
{
   if (array == NULL)
   {
      goto error;
   }
   if (array->element == NULL)
   {
      pgmoneta_json_object_put(array, NULL, ValueInt64Array, NULL, 0);
   }
   struct json_value* value = array->element->value;
   // type check
   if (array->type != JSONArray || value->type != ValueInt64Array)
   {
      goto error;
   }
   return pgmoneta_json_value_append(value, &val);
error:
   return 1;
}

int
pgmoneta_json_array_append_uint64(struct json* array, uint64_t val)
{
   if (array == NULL)
   {
      goto error;
   }
   if (array->element == NULL)
   {
      pgmoneta_json_object_put(array, NULL, ValueUInt64Array, NULL, 0);
   }
   struct json_value* value = array->element->value;
   // type check
   if (array->type != JSONArray || value->type != ValueUInt64Array)
   {
      goto error;
   }
   return pgmoneta_json_value_append(value, &val);
error:
   return 1;
}

int
pgmoneta_json_array_append_string(struct json* array, char* str)
{
   if (array == NULL || array->element == NULL)
   {
      goto error;
   }
   struct json_value* value = array->element->value;
   // type check
   if (array->type != JSONArray || value->type != ValueStringArray)
   {
      goto error;
   }
   return pgmoneta_json_value_append(value, str);
error:
   return 1;
}

int
pgmoneta_json_array_append_object(struct json* array, struct json* object)
{
   if (array == NULL || object == NULL)
   {
      goto error;
   }
   if (array->element == NULL)
   {
      if (object->type == JSONArray)
      {
         pgmoneta_json_object_put(array, NULL, ValueArrayArray, NULL, 0);
      }
      else if (object->type == JSONItem)
      {
         pgmoneta_json_object_put(array, NULL, ValueItemArray, NULL, 0);
      }
   }
   struct json_value* value = array->element->value;
   // type check
   if (array->type != JSONArray ||
       (!(value->type == ValueItemArray && object->type == JSONItem) &&
        !(value->type == ValueArrayArray && object->type == JSONArray)))
   {
      goto error;
   }
   return pgmoneta_json_value_append(value, object);
error:
   return 1;
}

void
pgmoneta_json_print(struct json* object, int indent_per_level)
{
   if (object == NULL)
   {
      printf("Null\n");
      return;
   }
   if (object->type == JSONArray)
   {
      print_json_array_value(object->element->value, 0, indent_per_level);
   }
   else
   {
      print_json_item(object, 0, indent_per_level);
   }
}

struct json*
pgmoneta_json_create_new_command_object(char* command_name, bool success, char* executable_name)
{
   struct json* json = NULL;
   struct json* command = NULL;
   struct json* output = NULL;
   struct json* application = NULL;
   // root of the JSON structure
   pgmoneta_json_init(&json, JSONItem);

   if (!json)
   {
      goto error;
   }

   // the command structure
   pgmoneta_json_init(&command, JSONItem);
   if (!command)
   {
      goto error;
   }

   pgmoneta_json_item_put_string(command, JSON_TAG_COMMAND_NAME, command_name);
   pgmoneta_json_item_put_string(command, JSON_TAG_COMMAND_STATUS, success ? JSON_STRING_SUCCESS : JSON_STRING_ERROR);
   pgmoneta_json_item_put_int64(command, JSON_TAG_COMMAND_ERROR, success ? JSON_BOOL_SUCCESS : JSON_BOOL_ERROR);
   pgmoneta_json_item_put_string(command, JSON_TAG_APPLICATION_VERSION, VERSION);

   // the output of the command, this has to be filled by the caller
   pgmoneta_json_init(&output, JSONItem);
   if (!output)
   {
      goto error;
   }

   pgmoneta_json_item_put_object(command, JSON_TAG_COMMAND_OUTPUT, output);

   pgmoneta_json_init(&application, JSONItem);
   if (!application)
   {
      goto error;
   }
   pgmoneta_json_item_put_string(application, JSON_TAG_APPLICATION_NAME, executable_name);
   // add objects to the whole json thing
   pgmoneta_json_item_put_object(json, "command", command);
   pgmoneta_json_item_put_object(json, "application", application);

   return json;
error:
   if (json)
   {
      pgmoneta_json_free(json);
   }

   return NULL;
}

struct json*
pgmoneta_json_extract_command_output_object(struct json* json)
{
   struct json* command = pgmoneta_json_get_json_object_value(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   return pgmoneta_json_get_json_object_value(command, JSON_TAG_COMMAND_OUTPUT);

error:
   return NULL;

}

bool
pgmoneta_json_command_name_equals_to(struct json* json, char* command_name)
{
   if (!json || !command_name || strlen(command_name) <= 0)
   {
      goto error;
   }

   struct json* command = pgmoneta_json_get_json_object_value(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   char* cName = pgmoneta_json_get_string_value(command, JSON_TAG_COMMAND_NAME);
   if (!cName)
   {
      goto error;
   }

   return !strncmp(command_name, cName, MISC_LENGTH);

error:
   return false;
}

int
pgmoneta_json_set_command_object_faulty(struct json* json, char* message)
{
   if (!json)
   {
      goto error;
   }

   struct json* command = pgmoneta_json_get_json_object_value(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   pgmoneta_json_item_put_string(command, JSON_TAG_COMMAND_STATUS, message);
   pgmoneta_json_item_put_int64(command, JSON_TAG_COMMAND_ERROR, JSON_BOOL_ERROR);

   return 0;

error:
   return 1;
}

bool
pgmoneta_json_is_command_object_faulty(struct json* json)
{
   if (!json)
   {
      goto error;
   }

   struct json* command = pgmoneta_json_get_json_object_value(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   int64_t status = pgmoneta_json_get_int64_value(command, JSON_TAG_COMMAND_ERROR);

   return status == JSON_BOOL_SUCCESS ? false : true;

error:
   return false;
}

const char*
pgmoneta_json_get_command_object_status(struct json* json)
{
   if (!json)
   {
      goto error;
   }

   struct json* command = pgmoneta_json_get_json_object_value(json, JSON_TAG_COMMAND);
   if (!command)
   {
      goto error;
   }

   return pgmoneta_json_get_string_value(command, JSON_TAG_COMMAND_STATUS);
error:
   return NULL;

}

void
pgmoneta_json_print_and_free_json_object(struct json* json)
{
   pgmoneta_json_print(json, 2);
   pgmoneta_json_free(json);
}

struct json*
pgmoneta_json_array_get(struct json* array, int index)
{
   if (array == NULL || array->type != JSONArray ||
       array->element == NULL || array->element->value == NULL)
   {
      goto error;
   }
   if (array->element->value->type != ValueArrayArray && array->element->value->type != ValueItemArray)
   {
      goto error;
   }
   if (array->element->value->length <= index)
   {
      goto error;
   }
   return ((struct json**)array->element->value->payload)[index];
error:
   return NULL;
}

unsigned short
pgmoneta_json_array_length(struct json* array)
{
   if (array == NULL || array->type != JSONArray
       || array->element == NULL || array->element->value == NULL)
   {
      goto error;
   }
   return array->element->value->length;
error:
   return 0;
}

static int
advance_to_first_array_element(struct json_reader* reader)
{
   if (reader->state != ArrayStart)
   {
      return 1;
   }
   // fast-forward to right before the first element
   char ch = 0;
   while (pgmoneta_json_peek_next_char(reader, &ch))
   {
      if (ch == '{' || ch == '"' || ch == '[' || isdigit(ch))
      {
         return 0;
      }
      else if (ch == ']')
      {
         // empty array
         reader->state = ArrayEnd;
         pgmoneta_json_next_char(reader, &ch);
         return 0;
      }
      pgmoneta_json_next_char(reader, &ch);
   }
   return 1;
}

static void
print_json_array(struct json* array, int indent, int indent_per_level)
{
   if (array == NULL || array->type != JSONArray)
   {
      return;
   }
   if (array->element == NULL || array->element->value == NULL)
   {
      print_json_array_value(NULL, indent, indent_per_level);
   }
   else
   {
      print_json_array_value(array->element->value, indent, indent_per_level);
   }
}

static void
print_json_array_value(struct json_value* value, int indent, int indent_per_level)
{
   if (value == NULL)
   {
      printf("[]");
      return;
   }
   printf("[\n");
   indent += indent_per_level;
   unsigned short length = 0;
   if (value != NULL)
   {
      length = value->length;
   }
   for (int i = 0; i < length; i++)
   {
      print_indent(indent);
      bool has_next = i != value->length - 1;
      switch (value->type)
      {
         case ValueStringArray:
         {
            char* str = ((char**)value->payload)[i];
            printf("\"%s\"%s\n", str, has_next?",":"");
            break;
         }
         case ValueInt64Array:
         {
            int64_t num = ((int64_t*)value->payload)[i];
            printf("%" PRId64 "%s\n", num, has_next?",":"");
            break;
         }
         case ValueFloatArray:
         {
            float num = ((float*)value->payload)[i];
            printf("%.4f%s\n", num, has_next?",":"");
            break;
         }
         case ValueItemArray:
         {
            struct json* item = ((struct json**)value->payload)[i];
            print_json_item(item, indent, indent_per_level);
            printf("%s\n", has_next?",":"");
            break;
         }
         case ValueArrayArray:
         {
            struct json* array = ((struct json**)value->payload)[i];
            print_json_array(array, indent, indent_per_level);
            printf("%s\n", has_next?",":"");
            break;
         }
         default:
            return;
      }
   }
   indent -= indent_per_level;
   print_indent(indent);
   printf("]");
}

static void
print_indent(int indent)
{
   for (int i = 0; i < indent; i++)
   {
      printf(" ");
   }
}

__attribute__((used))
static void
print_json_state(enum json_reader_state state)
{
   switch (state)
   {
      case KeyStart:
         printf("key start\n");
         break;
      case KeyEnd:
         printf("key end\n");
         break;
      case ValueStart:
         printf("value start\n");
         break;
      case ValueEnd:
         printf("value end\n");
         break;
      case ArrayStart:
         printf("array start\n");
         break;
      case ArrayEnd:
         printf("array end\n");
         break;
      case ItemStart:
         printf("item start\n");
         break;
      case ItemEnd:
         printf("item end\n");
         break;
      case InvalidReaderState:
         printf("invalid state\n");
         break;
   }
}

static bool
is_array_typed_value(enum json_value_type type)
{
   switch (type)
   {

      case ValueInt8:
      case ValueUInt8:
      case ValueInt16:
      case ValueUInt16:
      case ValueInt32:
      case ValueUInt32:
      case ValueInt64:
      case ValueUInt64:
      case ValueBool:
      case ValueString:
      case ValueFloat:
      case ValueObject:
         return false;
      case ValueInt8Array:
      case ValueUInt8Array:
      case ValueInt16Array:
      case ValueUInt16Array:
      case ValueInt32Array:
      case ValueUInt32Array:
      case ValueInt64Array:
      case ValueUInt64Array:
      case ValueStringArray:
      case ValueFloatArray:
      case ValueItemArray:
      case ValueArrayArray:
         return true;
   }
   return false;
}

static void
print_json_item(struct json* item, int indent, int indent_per_level)
{
   if (item == NULL || item->type != JSONItem)
   {
      return;
   }
   struct json_element* element = NULL;
   printf("{\n");
   indent += indent_per_level;
   element = item->element;
   while (element != NULL)
   {
      bool has_next = element->next != NULL;
      struct json_value* val = element->value;
      print_indent(indent);
      printf("\"%s\": ", element->key);
      switch (val->type)
      {
         case ValueInt8:
            printf("%" PRId8 "%s\n", pgmoneta_read_byte(val->payload), has_next?",":"");
            break;
         case ValueUInt8:
            printf("%" PRIu8 "%s\n", pgmoneta_read_uint8(val->payload), has_next?",":"");
            break;
         case ValueInt16:
            printf("%" PRId16 "%s\n", pgmoneta_read_int16(val->payload), has_next?",":"");
            break;
         case ValueUInt16:
            printf("%" PRIu16 "%s\n", pgmoneta_read_uint16(val->payload), has_next?",":"");
            break;
         case ValueInt32:
            printf("%" PRId32 "%s\n", pgmoneta_read_int32(val->payload), has_next?",":"");
            break;
         case ValueUInt32:
            printf("%" PRIu32 "%s\n", pgmoneta_read_uint32(val->payload), has_next?",":"");
            break;
         case ValueInt64:
            printf("%" PRId64 "%s\n", pgmoneta_read_int64(val->payload), has_next?",":"");
            break;
         case ValueUInt64:
            printf("%" PRIu64 "%s\n", pgmoneta_read_uint64(val->payload), has_next?",":"");
            break;
         case ValueBool:
            printf("%s%s\n", pgmoneta_read_bool(val->payload)? "true": "false", has_next?",":"");
            break;
         case ValueFloat:
            printf("%.4f%s\n", *((float*)val->payload), has_next?",":"");
            break;
         case ValueString:
            printf("\"%s\"%s\n", (char*)val->payload, has_next?",":"");
            break;
         case ValueObject:
         {
            struct json* obj = (struct json*)val->payload;
            if (obj != NULL && obj->type == JSONArray)
            {
               print_json_array(obj, indent, indent_per_level);
            }
            else
            {
               print_json_item(obj, indent, indent_per_level);
            }
            printf("%s\n", has_next?",":"");
         }
         break;
         case ValueStringArray:
         case ValueInt8Array:
         case ValueUInt8Array:
         case ValueInt16Array:
         case ValueUInt16Array:
         case ValueInt32Array:
         case ValueUInt32Array:
         case ValueInt64Array:
         case ValueUInt64Array:
         case ValueFloatArray:
         case ValueItemArray:
         case ValueArrayArray:
            print_json_array_value(val, indent + indent_per_level, indent_per_level);
            printf("%s\n", has_next?",":"");
            break;
      }
      element = element->next;
   }
   indent -= indent_per_level;
   print_indent(indent);
   printf("}");
}

int
pgmoneta_json_reader_reset(struct json_reader* reader)
{
   if (reader == NULL || reader->buffer == NULL)
   {
      goto error;
   }
   off_t offset = lseek(reader->fd, 0, SEEK_SET);
   if (offset < 0)
   {
      pgmoneta_log_error("error resetting json reader, %s", strerror(errno));
      errno = 0;
      goto error;
   }
   reader->buffer->start = reader->buffer->end = reader->buffer->cursor = 0;
   reader->state = InvalidReaderState;
   // read until the first '{' or '[', and set the state correspondingly
   char ch = 0;
   while (pgmoneta_json_next_char(reader, &ch))
   {
      if (ch != '{' && ch != '[')
      {
         continue;
      }
      if (ch == '{')
      {
         reader->state = ItemStart;
      }
      else
      {
         reader->state = ArrayStart;
         if (advance_to_first_array_element(reader))
         {
            goto error;
         }
      }
      break;
   }
   return 0;
error:
   return 1;
}