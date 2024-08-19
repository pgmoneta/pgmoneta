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
#include <art.h>
#include <logging.h>
#include <json.h>
#include <message.h>
#include <utils.h>
/* System */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int advance_to_first_array_element(struct json_reader* reader);
static int json_read(struct json_reader* reader);
static bool json_next_char(struct json_reader* reader, char* next);
static bool json_peek_next_char(struct json_reader* reader, char* next);
static int json_fast_forward_value(struct json_reader* reader, char ch);
static int json_stream_parse_item(struct json_reader* reader, struct json** item);
static bool type_allowed(enum value_type type);
static char* item_to_string(struct json* item, int32_t format, char* tag, int indent);
static char* array_to_string(struct json* array, int32_t format, char* tag, int indent);
static int parse_string(char* str, uint64_t* index, struct json** obj);
static int json_add(struct json* obj, char* key, uintptr_t val, enum value_type type);
static int fill_value(char* str, char* key, uint64_t* index, struct json* o);
static bool value_start(char ch);

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
   r->state = InvalidState;
   // read until the first '{' or '[', and set the state correspondingly
   char ch = 0;
   while (json_next_char(r, &ch))
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
pgmoneta_json_reader_close(struct json_reader* reader)
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
   if (reader == NULL || reader->state == InvalidState)
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
      while (json_next_char(reader, &ch))
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
               if (json_fast_forward_value(reader, ch))
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
      if (!json_peek_next_char(reader, &ch))
      {
         goto error;
      }
   }
done:
   free(cur_key);
   return 0;
error:
   free(cur_key);
   reader->state = InvalidState;
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
   if (json_peek_next_char(reader, &ch) && ch != '{')
   {
      goto done;
   }
   json_next_char(reader, &ch);
   reader->state = ItemStart;
   // parse the item, it'll be nicer to push the state to stack, but is not necessary right now
   if (json_stream_parse_item(reader, item))
   {
      goto done;
   }
   reader->state = ArrayStart;
   // fast-forward to the next item
   while (json_peek_next_char(reader, &ch))
   {
      if (ch == '{')
      {
         break;
      }
      else if (ch == ']')
      {
         reader->state = ArrayEnd;
         json_next_char(reader, &ch);
         break;
      }
      json_next_char(reader, &ch);
   }
   return true;
done:
   reader->state = InvalidState;
   return false;
}

int
pgmoneta_json_append(struct json* array, uintptr_t entry, enum value_type type)
{
   if (array != NULL && array->type == JSONUnknown)
   {
      array->type = JSONArray;
      pgmoneta_deque_create(false, (struct deque**)&array->elements);
   }
   if (array == NULL || array->type != JSONArray || !type_allowed(type))
   {
      goto error;
   }
   return pgmoneta_deque_add(array->elements, NULL, entry, type);
error:
   return 1;
}

int
pgmoneta_json_put(struct json* item, char* key, uintptr_t val, enum value_type type)
{
   if (item != NULL && item->type == JSONUnknown)
   {
      item->type = JSONItem;
      pgmoneta_art_create((struct art**)&item->elements);
   }
   if (item == NULL || item->type != JSONItem || !type_allowed(type) || key == NULL || strlen(key) == 0)
   {
      goto error;
   }
   return pgmoneta_art_insert((struct art*)item->elements, (unsigned char*)key, strlen(key) + 1, val, type);
error:
   return 1;
}

int
pgmoneta_json_create(struct json** object)
{
   struct json* o = malloc(sizeof(struct json));
   memset(o, 0, sizeof(struct json));
   o->type = JSONUnknown;
   *object = o;
   return 0;
}

int
pgmoneta_json_destroy(struct json* object)
{
   if (object == NULL)
   {
      return 0;
   }
   if (object->type == JSONArray)
   {
      pgmoneta_deque_destroy(object->elements);
   }
   else if (object->type == JSONItem)
   {
      pgmoneta_art_destroy(object->elements);
   }
   free(object);
   return 0;
}

char*
pgmoneta_json_to_string(struct json* object, int32_t format, char* tag, int indent)
{
   char* str = NULL;
   if (object == NULL || (object->type == JSONUnknown || object->elements == NULL))
   {
      str = pgmoneta_indent(str, tag, indent);
      if (format == FORMAT_JSON)
      {
         str = pgmoneta_append(str, "{}");
      }
      return str;
   }
   if (object->type != JSONArray)
   {
      return item_to_string(object, format, tag, indent);
   }
   else
   {
      return array_to_string(object, format, tag, indent);
   }
}

void
pgmoneta_json_print(struct json* object, int32_t format)
{
   char* str = pgmoneta_json_to_string(object, format, NULL, 0);
   printf("%s\n", str);
   free(str);
}

uint32_t
pgmoneta_json_array_length(struct json* array)
{
   if (array == NULL || array->type != JSONArray)
   {
      goto error;
   }
   return pgmoneta_deque_size(array->elements);
error:
   return 0;
}

uintptr_t
pgmoneta_json_get(struct json* item, char* tag)
{
   if (item == NULL || item->type != JSONItem || tag == NULL || strlen(tag) == 0)
   {
      return 0;
   }
   return pgmoneta_art_search(item->elements, (unsigned char*)tag, strlen(tag) + 1);
}

int
pgmoneta_json_iterator_create(struct json* object, struct json_iterator** iter)
{
   struct json_iterator* i = NULL;
   if (object == NULL || object->type == JSONUnknown)
   {
      return 1;
   }
   i = malloc(sizeof (struct json_iterator));
   memset(i, 0, sizeof (struct json_iterator));
   i->obj = object;
   if (object->type == JSONItem)
   {
      pgmoneta_art_iterator_create(object->elements, (struct art_iterator**)(&i->iter));
   }
   else
   {
      pgmoneta_deque_iterator_create(object->elements, (struct deque_iterator**)(&i->iter));
   }
   *iter = i;
   return 0;
}

void
pgmoneta_json_iterator_destroy(struct json_iterator* iter)
{
   if (iter == NULL)
   {
      return;
   }
   if (iter->obj->type == JSONArray)
   {
      pgmoneta_deque_iterator_destroy((struct deque_iterator*)iter->iter);
   }
   else
   {
      pgmoneta_art_iterator_destroy((struct art_iterator*)iter->iter);
   }
   free(iter);
}

bool
pgmoneta_json_iterator_next(struct json_iterator* iter)
{
   bool has_next = false;
   if (iter == NULL || iter->iter == NULL)
   {
      return false;
   }
   if (iter->obj->type == JSONArray)
   {
      has_next = pgmoneta_deque_iterator_next((struct deque_iterator*)iter->iter);
      if (has_next)
      {
         iter->value = ((struct deque_iterator*)iter->iter)->value;
      }
   }
   else
   {
      has_next = pgmoneta_art_iterator_next((struct art_iterator*)iter->iter);
      if (has_next)
      {
         iter->value = ((struct art_iterator*)iter->iter)->value;
         iter->key = (char*)((struct art_iterator*)iter->iter)->key;
      }
   }
   return has_next;
}

bool
pgmoneta_json_iterator_has_next(struct json_iterator* iter)
{
   bool has_next = false;
   if (iter == NULL || iter->iter == NULL)
   {
      return false;
   }
   if (iter->obj->type == JSONArray)
   {
      has_next = pgmoneta_deque_iterator_has_next((struct deque_iterator*)iter->iter);
   }
   else
   {
      has_next = pgmoneta_art_iterator_has_next((struct art_iterator*)iter->iter);
   }
   return has_next;
}

int
pgmoneta_json_parse_string(char* str, struct json** obj)
{
   uint64_t idx = 0;
   if (str == NULL || strlen(str) < 2)
   {
      return 1;
   }

   return parse_string(str, &idx, obj);
}

int
pgmoneta_json_clone(struct json* from, struct json** to)
{
   struct json* o = NULL;
   char* str = NULL;
   str = pgmoneta_json_to_string(from, FORMAT_JSON, NULL, 0);
   if (pgmoneta_json_parse_string(str, &o))
   {
      goto error;
   }
   *to = o;
   free(str);
   return 0;
error:
   free(str);
   return 1;
}

static int
parse_string(char* str, uint64_t* index, struct json** obj)
{
   enum json_type type;
   struct json* o = NULL;
   uint64_t idx = *index;
   char ch = str[idx];
   char* key = NULL;
   uint64_t len = strlen(str);

   if (ch == '{')
   {
      type = JSONItem;
   }
   else if (ch == '[')
   {
      type = JSONArray;
   }
   else
   {
      goto error;
   }
   idx++;
   pgmoneta_json_create(&o);
   if (type == JSONItem)
   {
      while (idx < len)
      {
         // pre key
         while (idx < len && isspace(str[idx]))
         {
            idx++;
         }
         if (idx == len)
         {
            goto error;
         }
         if (str[idx] == ',')
         {
            idx++;
         }
         else if (str[idx] == '}')
         {
            idx++;
            break;
         }
         else if (!(str[idx] == '"' && o->type == JSONUnknown))
         {
            // if it's first key we won't see comma, otherwise we must see comma
            goto error;
         }
         while (idx < len && str[idx] != '"')
         {
            idx++;
         }
         if (idx == len)
         {
            goto error;
         }
         idx++;
         // The key
         while (idx < len && str[idx] != '"')
         {
            key = pgmoneta_append_char(key, str[idx++]);
         }
         if (idx == len || key == NULL)
         {
            goto error;
         }
         // The lands between
         while (idx < len && (str[idx] == '"' || isspace(str[idx])))
         {
            idx++;
         }
         if (idx == len || str[idx] != ':')
         {
            goto error;
         }
         while (idx < len && (str[idx] == ':' || isspace(str[idx])))
         {
            idx++;
         }
         if (idx == len)
         {
            goto error;
         }
         // The value
         if (fill_value(str, key, &idx, o))
         {
            goto error;
         }
         free(key);
         key = NULL;
      }
   }
   else
   {
      while (idx < len)
      {
         while (idx < len && isspace(str[idx]))
         {
            idx++;
         }
         if (idx == len)
         {
            goto error;
         }
         if (str[idx] == ',')
         {
            idx++;
         }
         else if (str[idx] == ']')
         {
            idx++;
            break;
         }
         else if (!(value_start(str[idx]) && o->type == JSONUnknown))
         {
            // if it's first key we won't see comma, otherwise we must see comma
            goto error;
         }
         while (idx < len && !value_start(str[idx]))
         {
            idx++;
         }
         if (idx == len)
         {
            goto error;
         }

         if (fill_value(str, key, &idx, o))
         {
            goto error;
         }
      }
   }

   *index = idx;
   *obj = o;
   return 0;
error:
   pgmoneta_json_destroy(o);
   free(key);
   return 1;
}

static int
json_add(struct json* obj, char* key, uintptr_t val, enum value_type type)
{
   if (obj == NULL)
   {
      return 1;
   }
   if (key == NULL)
   {
      return pgmoneta_json_append(obj, val, type);
   }
   return pgmoneta_json_put(obj, key, val, type);
}

static bool
value_start(char ch)
{
   return (isdigit(ch) || ch == '-' || ch == '+') || // number
          (ch == '[') || // array
          (ch == '{') || // item
          (ch == '"' || ch == 'n') || // string or null string
          (ch == 't' || ch == 'f'); // potential boolean value
}

static int
fill_value(char* str, char* key, uint64_t* index, struct json* o)
{
   uint64_t idx = *index;
   uint64_t len = strlen(str);
   if (str[idx] == '"')
   {
      char* val = NULL;
      idx++;
      while (idx < len && str[idx] != '"')
      {
         val = pgmoneta_append_char(val, str[idx++]);
      }
      if (idx == len)
      {
         goto error;
      }
      if (val == NULL)
      {
         json_add(o, key, (uintptr_t)"", ValueString);
      }
      else
      {
         json_add(o, key, (uintptr_t)val, ValueString);
      }
      idx++;
      free(val);
   }
   else if (str[idx] == '-' || str[idx] == '+' || isdigit(str[idx]))
   {
      bool has_digit = false;
      char* val_str = NULL;
      while (idx < len && (isdigit(str[idx]) || str[idx] == '.' || str[idx] == '-' || str[idx] == '+'))
      {
         if (str[idx] == '.')
         {
            has_digit = true;
         }
         val_str = pgmoneta_append_char(val_str, str[idx++]);
      }
      if (has_digit)
      {
         double val = 0.;
         if (sscanf(val_str, "%lf", &val) != 1)
         {
            free(val_str);
            goto error;
         }
         json_add(o, key, pgmoneta_value_from_double(val), ValueDouble);
         free(val_str);
      }
      else
      {
         int64_t val = 0;
         if (sscanf(val_str, "%" PRId64, &val) != 1)
         {
            free(val_str);
            goto error;
         }
         json_add(o, key, (uintptr_t)val, ValueInt64);
         free(val_str);
      }
   }
   else if (str[idx] == '{')
   {
      struct json* val = NULL;
      if (parse_string(str, &idx, &val))
      {
         goto error;
      }
      json_add(o, key, (uintptr_t)val, ValueJSON);
   }
   else if (str[idx] == '[')
   {
      struct json* val = NULL;
      if (parse_string(str, &idx, &val))
      {
         goto error;
      }
      json_add(o, key, (uintptr_t)val, ValueJSON);
   }
   else if (str[idx] == 'n' || str[idx] == 't' || str[idx] == 'f')
   {
      char* val = NULL;
      while (idx < len && str[idx] >= 'a' && str[idx] <= 'z')
      {
         val = pgmoneta_append_char(val, str[idx++]);
      }
      if (pgmoneta_compare_string(val, "null"))
      {
         json_add(o, key, 0, ValueString);
      }
      else if (pgmoneta_compare_string(val, "true"))
      {
         json_add(o, key, true, ValueBool);
      }
      else if (pgmoneta_compare_string(val, "false"))
      {
         json_add(o, key, false, ValueBool);
      }
      else
      {
         free(val);
         goto error;
      }
      free(val);
   }
   else
   {
      goto error;
   }
   *index = idx;
   return 0;
error:
   return 1;
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
   while (json_peek_next_char(reader, &ch))
   {
      if (ch == '{' || ch == '"' || ch == '[' || isdigit(ch))
      {
         return 0;
      }
      else if (ch == ']')
      {
         // empty array
         reader->state = ArrayEnd;
         json_next_char(reader, &ch);
         return 0;
      }
      json_next_char(reader, &ch);
   }
   return 1;
}

__attribute__((used))
static void
print_json_state(enum parse_state state)
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
      case InvalidState:
         printf("invalid state\n");
         break;
   }
}

static int
json_read(struct json_reader* reader)
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

static bool
json_next_char(struct json_reader* reader, char* next)
{
   int status = 0;
   if (reader->buffer->cursor == reader->buffer->end)
   {
      reader->buffer->end = 0;
      reader->buffer->cursor = 0;
      status = json_read(reader);
      if (status == MESSAGE_STATUS_ZERO || status == MESSAGE_STATUS_ERROR)
      {
         return false;
      }
   }
   *next = *(reader->buffer->buffer + reader->buffer->cursor);
   reader->buffer->cursor++;
   return true;
}

static bool
json_peek_next_char(struct json_reader* reader, char* next)
{
   int status = 0;
   if (reader->buffer->cursor == reader->buffer->end)
   {
      reader->buffer->end = 0;
      reader->buffer->cursor = 0;
      status = json_read(reader);
      if (status == MESSAGE_STATUS_ZERO || status == MESSAGE_STATUS_ERROR)
      {
         return false;
      }
   }
   *next = *(reader->buffer->buffer + reader->buffer->cursor);
   return true;
}

static int
json_fast_forward_value(struct json_reader* reader, char ch)
{
   if (reader == NULL || reader->state != ValueStart)
   {
      goto error;
   }
   bool has_next = true;
   while (ch != '{' && ch != '"' && ch != '[' && !isdigit(ch) && has_next)
   {
      has_next = json_next_char(reader, &ch);
   }
   if (ch == '{')
   {
      int count = 1;
      while (json_next_char(reader, &ch) && count != 0)
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
      while (json_next_char(reader, &ch))
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
      while (json_next_char(reader, &ch) && count != 0)
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
      while (json_next_char(reader, &ch))
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

static int
json_stream_parse_item(struct json_reader* reader, struct json** item)
{
   struct json* i = NULL;
   char* key = NULL;
   char ch = 0;
   pgmoneta_json_create(&i);
   if (reader->state != ItemStart)
   {
      goto error;
   }
   while (json_next_char(reader, &ch))
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
            if (json_fast_forward_value(reader, ch))
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
               while (json_next_char(reader, &ch) && ch != '"')
               {
                  str = pgmoneta_append_char(str, ch);
               }
               if (ch != '"')
               {
                  free(str);
                  goto error;
               }
               pgmoneta_json_put(i, key, (uintptr_t)str, ValueString);
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
               while (json_peek_next_char(reader, &ch) && (isdigit(ch) || ch == '.'))
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
                  json_next_char(reader, &ch);
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
                  pgmoneta_json_put(i, key, (uintptr_t)num, ValueFloat);
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
                  pgmoneta_json_put(i, key, (uintptr_t)num, ValueInt64);
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
   pgmoneta_json_destroy(i);
   free(key);
   return 1;
}

static bool
type_allowed(enum value_type type)
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
      case ValueDouble:
      case ValueJSON:
         return true;
      default:
         return false;
   }
}

static char*
item_to_string(struct json* item, int32_t format, char* tag, int indent)
{
   return pgmoneta_art_to_string(item->elements, format, tag, indent);
}

static char*
array_to_string(struct json* array, int32_t format, char* tag, int indent)
{
   return pgmoneta_deque_to_string(array->elements, format, tag, indent);
}