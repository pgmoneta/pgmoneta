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

#include <pgmoneta.h>
#include <art.h>
#include <deque.h>
#include <http.h>
#include <logging.h>
#include <prometheus_client.h>
#include <utils.h>
#include <value.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PROMETHEUS_LABEL_LENGTH 1024

static int parse_body_to_bridge(time_t timestamp, char* body, struct prometheus_bridge* bridge);
static int metric_find_create(struct prometheus_bridge* bridge, char* name, struct prometheus_metric** metric);
static int metric_set_name(struct prometheus_metric* metric, char* name);
static int metric_set_help(struct prometheus_metric* metric, char* help);
static int metric_set_type(struct prometheus_metric* metric, char* type);
static bool attributes_contains(struct deque* attributes, struct prometheus_attribute* attribute);
static int attributes_find_create(struct deque* definitions, struct deque* input, struct prometheus_attributes** attributes, bool* is_new);
static int add_attribute(struct deque* attributes, char* key, char* value);
static int add_value(struct deque* values, time_t timestamp, char* value);
static int add_line(struct main_configuration* config, struct prometheus_metric* metric, char* line, time_t timestamp);
static int parse_metric_name_from_line(char* line, char* metric_name, size_t size);

static void prometheus_metric_destroy_cb(uintptr_t data);
static char* deque_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static char* prometheus_metric_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static void prometheus_attributes_destroy_cb(uintptr_t data);
static char* prometheus_attributes_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static void prometheus_value_destroy_cb(uintptr_t data);
static char* prometheus_value_string_cb(uintptr_t data, int32_t format, char* tag, int indent);
static void prometheus_attribute_destroy_cb(uintptr_t data);
static char* prometheus_attribute_string_cb(uintptr_t data, int32_t format, char* tag, int indent);

int
pgmoneta_prometheus_client_create_bridge(struct prometheus_bridge** bridge)
{
   struct prometheus_bridge* b = NULL;

   if (bridge == NULL)
   {
      return 1;
   }

   *bridge = NULL;

   b = (struct prometheus_bridge*)malloc(sizeof(struct prometheus_bridge));
   if (b == NULL)
   {
      pgmoneta_log_error("Failed to allocate bridge");
      goto error;
   }

   memset(b, 0, sizeof(struct prometheus_bridge));

   if (pgmoneta_art_create(&b->metrics))
   {
      pgmoneta_log_error("Failed to create ART");
      goto error;
   }

   *bridge = b;

   return 0;

error:
   if (b != NULL)
   {
      pgmoneta_art_destroy(b->metrics);
      free(b);
   }

   return 1;
}

int
pgmoneta_prometheus_client_destroy_bridge(struct prometheus_bridge* bridge)
{
   if (bridge != NULL)
   {
      pgmoneta_art_destroy(bridge->metrics);
   }

   free(bridge);

   return 0;
}

int
pgmoneta_prometheus_client_get(int endpoint, struct prometheus_bridge* bridge)
{
   time_t timestamp;
   struct http* connection = NULL;
   struct http_request* request = NULL;
   struct http_response* response = NULL;
   struct main_configuration* config = NULL;
   bool secure = false;
   char* body_copy = NULL;
   const char* host = NULL;

   (void)endpoint;

   config = (struct main_configuration*)shmem;
   if (config == NULL || bridge == NULL)
   {
      goto error;
   }

   if (config->metrics <= 0)
   {
      pgmoneta_log_error("Metrics listener is not enabled");
      goto error;
   }

   host = config->host;

   secure = strlen(config->metrics_cert_file) > 0 && strlen(config->metrics_key_file) > 0;

   pgmoneta_log_debug("Endpoint %s://%s:%d/metrics", secure ? "https" : "http", host, config->metrics);

   if (pgmoneta_http_create((char*)host, config->metrics, secure, &connection))
   {
      pgmoneta_log_error("Failed to connect to metrics endpoint (%s:%d)", host, config->metrics);
      goto error;
   }

   if (pgmoneta_http_request_create(PGMONETA_HTTP_GET, "/metrics", &request))
   {
      pgmoneta_log_error("Failed to create HTTP request for /metrics");
      goto error;
   }

   if (pgmoneta_http_invoke(connection, request, &response))
   {
      pgmoneta_log_error("Failed to execute HTTP GET /metrics (%s:%d)", host, config->metrics);
      goto error;
   }

   if (response == NULL || response->payload.data == NULL || response->payload.data_size == 0)
   {
      pgmoneta_log_error("No response data from metrics endpoint");
      goto error;
   }

   body_copy = (char*)malloc(response->payload.data_size + 1);
   if (body_copy == NULL)
   {
      pgmoneta_log_error("Failed to allocate response copy");
      goto error;
   }

   memcpy(body_copy, response->payload.data, response->payload.data_size);
   body_copy[response->payload.data_size] = '\0';

   timestamp = time(NULL);
   if (parse_body_to_bridge(timestamp, body_copy, bridge))
   {
      goto error;
   }

   free(body_copy);
   pgmoneta_http_response_destroy(response);
   pgmoneta_http_request_destroy(request);
   pgmoneta_http_destroy(connection);

   return 0;

error:

   free(body_copy);

   if (response != NULL)
   {
      pgmoneta_http_response_destroy(response);
   }

   if (request != NULL)
   {
      pgmoneta_http_request_destroy(request);
   }

   if (connection != NULL)
   {
      pgmoneta_http_destroy(connection);
   }

   return 1;
}

static void
prometheus_metric_destroy_cb(uintptr_t data)
{
   struct prometheus_metric* m = NULL;

   m = (struct prometheus_metric*)data;

   if (m != NULL)
   {
      free(m->name);
      free(m->help);
      free(m->type);

      pgmoneta_deque_destroy(m->definitions);
   }

   free(m);
}

static char*
deque_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   struct deque* d = NULL;

   d = (struct deque*)data;

   return pgmoneta_deque_to_string(d, format, tag, indent);
}

static char*
prometheus_metric_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct value_config vc = {.destroy_data = NULL,
                             .to_string = &deque_string_cb};
   struct prometheus_metric* m = NULL;

   m = (struct prometheus_metric*)data;

   if (pgmoneta_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgmoneta_art_insert(a, (char*)"Name", (uintptr_t)m->name, ValueString);
      pgmoneta_art_insert(a, (char*)"Help", (uintptr_t)m->help, ValueString);
      pgmoneta_art_insert(a, (char*)"Type", (uintptr_t)m->type, ValueString);
      pgmoneta_art_insert_with_config(a, (char*)"Definitions", (uintptr_t)m->definitions, &vc);

      s = pgmoneta_art_to_string(a, format, tag, indent);
   }

   pgmoneta_art_destroy(a);

   return s;

error:

   pgmoneta_art_destroy(a);

   return "Error";
}

static int
metric_find_create(struct prometheus_bridge* bridge, char* name,
                   struct prometheus_metric** metric)
{
   struct prometheus_metric* m = NULL;
   struct value_config vc = {.destroy_data = &prometheus_metric_destroy_cb,
                             .to_string = &prometheus_metric_string_cb};

   *metric = NULL;

   m = (struct prometheus_metric*)pgmoneta_art_search(bridge->metrics, (char*)name);

   if (m == NULL)
   {
      struct deque* defs = NULL;

      m = (struct prometheus_metric*)malloc(sizeof(struct prometheus_metric));
      if (m == NULL)
      {
         goto error;
      }

      memset(m, 0, sizeof(struct prometheus_metric));

      if (pgmoneta_deque_create(true, &defs))
      {
         free(m);
         goto error;
      }

      m->name = strdup(name);
      if (m->name == NULL)
      {
         pgmoneta_deque_destroy(defs);
         free(m);
         goto error;
      }

      m->definitions = defs;

      if (pgmoneta_art_insert_with_config(bridge->metrics, (char*)name,
                                          (uintptr_t)m, &vc))
      {
         prometheus_metric_destroy_cb((uintptr_t)m);
         goto error;
      }
   }

   *metric = m;

   return 0;

error:

   return 1;
}

static int
metric_set_name(struct prometheus_metric* metric, char* name)
{
   if (metric == NULL || name == NULL)
   {
      return 1;
   }

   if (metric->name != NULL)
   {
      return 0;
   }

   metric->name = strdup(name);
   if (metric->name == NULL)
   {
      errno = 0;
      return 1;
   }

   return 0;
}

static int
metric_set_help(struct prometheus_metric* metric, char* help)
{
   if (metric == NULL || help == NULL)
   {
      return 1;
   }

   if (metric->help != NULL)
   {
      free(metric->help);
      metric->help = NULL;
   }

   metric->help = strdup(help);

   if (metric->help == NULL)
   {
      errno = 0;
      return 1;
   }

   return 0;
}

static int
metric_set_type(struct prometheus_metric* metric, char* type)
{
   if (metric == NULL || type == NULL)
   {
      return 1;
   }

   if (metric->type != NULL)
   {
      free(metric->type);
      metric->type = NULL;
   }

   metric->type = strdup(type);

   if (metric->type == NULL)
   {
      errno = 0;
      return 1;
   }

   return 0;
}

static bool
attributes_contains(struct deque* attributes, struct prometheus_attribute* attribute)
{
   bool found = false;
   struct deque_iterator* attributes_iterator = NULL;

   if (!pgmoneta_deque_empty(attributes))
   {
      if (pgmoneta_deque_iterator_create(attributes, &attributes_iterator))
      {
         goto done;
      }

      while (!found && pgmoneta_deque_iterator_next(attributes_iterator))
      {
         struct prometheus_attribute* a = (struct prometheus_attribute*)attributes_iterator->value->data;

         if (!strcmp(a->key, attribute->key) && !strcmp(a->value, attribute->value))
         {
            found = true;
         }
      }
   }

done:

   pgmoneta_deque_iterator_destroy(attributes_iterator);

   return found;
}

static void
prometheus_attributes_destroy_cb(uintptr_t data)
{
   struct prometheus_attributes* m = NULL;

   m = (struct prometheus_attributes*)data;

   if (m != NULL)
   {
      pgmoneta_deque_destroy(m->attributes);
      pgmoneta_deque_destroy(m->values);
   }

   free(m);
}

static char*
prometheus_attributes_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct value_config vc = {.destroy_data = NULL,
                             .to_string = &deque_string_cb};
   struct prometheus_attributes* m = NULL;

   m = (struct prometheus_attributes*)data;

   if (pgmoneta_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgmoneta_art_insert_with_config(a, (char*)"Attributes", (uintptr_t)m->attributes, &vc);
      pgmoneta_art_insert_with_config(a, (char*)"Values", (uintptr_t)m->values, &vc);

      s = pgmoneta_art_to_string(a, format, tag, indent);
   }

   pgmoneta_art_destroy(a);

   return s;

error:

   pgmoneta_art_destroy(a);

   return "Error";
}

static int
attributes_find_create(struct deque* definitions, struct deque* input,
                       struct prometheus_attributes** attributes, bool* is_new)
{
   bool found = false;
   struct prometheus_attributes* m = NULL;
   struct deque_iterator* definition_iterator = NULL;
   struct deque_iterator* input_iterator = NULL;
   struct value_config vc = {.destroy_data = &prometheus_attributes_destroy_cb,
                             .to_string = &prometheus_attributes_string_cb};

   *attributes = NULL;
   *is_new = false;

   if (!pgmoneta_deque_empty(definitions))
   {
      if (pgmoneta_deque_iterator_create(definitions, &definition_iterator))
      {
         goto error;
      }

      while (!found && pgmoneta_deque_iterator_next(definition_iterator))
      {
         bool match = true;
         struct prometheus_attributes* a =
            (struct prometheus_attributes*)definition_iterator->value->data;

         if (pgmoneta_deque_iterator_create(input, &input_iterator))
         {
            goto error;
         }

         while (match && pgmoneta_deque_iterator_next(input_iterator))
         {
            struct prometheus_attribute* i = (struct prometheus_attribute*)input_iterator->value->data;

            if (!attributes_contains(a->attributes, i))
            {
               match = false;
            }
         }

         if (match)
         {
            *attributes = a;
            found = true;
         }

         pgmoneta_deque_iterator_destroy(input_iterator);
         input_iterator = NULL;
      }
   }

   if (!found)
   {
      m = (struct prometheus_attributes*)malloc(sizeof(struct prometheus_attributes));
      if (m == NULL)
      {
         goto error;
      }

      memset(m, 0, sizeof(struct prometheus_attributes));

      if (pgmoneta_deque_create(false, &m->values))
      {
         free(m);
         goto error;
      }

      m->attributes = input;

      if (pgmoneta_deque_add_with_config(definitions, NULL, (uintptr_t)m, &vc))
      {
         prometheus_attributes_destroy_cb((uintptr_t)m);
         goto error;
      }

      *attributes = m;
      *is_new = true;
   }

   pgmoneta_deque_iterator_destroy(definition_iterator);

   return 0;

error:

   pgmoneta_deque_iterator_destroy(input_iterator);
   pgmoneta_deque_iterator_destroy(definition_iterator);

   return 1;
}

static void
prometheus_value_destroy_cb(uintptr_t data)
{
   struct prometheus_value* m = NULL;

   m = (struct prometheus_value*)data;

   if (m != NULL)
   {
      free(m->value);
   }

   free(m);
}

static char*
prometheus_value_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct prometheus_value* m = NULL;

   m = (struct prometheus_value*)data;

   if (pgmoneta_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgmoneta_art_insert(a, (char*)"Timestamp", (uintptr_t)m->timestamp, ValueInt64);
      pgmoneta_art_insert(a, (char*)"Value", (uintptr_t)m->value, ValueString);

      s = pgmoneta_art_to_string(a, format, tag, indent);
   }

   pgmoneta_art_destroy(a);

   return s;

error:

   pgmoneta_art_destroy(a);

   return "Error";
}

static int
add_value(struct deque* values, time_t timestamp, char* value)
{
   struct value_config vc = {.destroy_data = &prometheus_value_destroy_cb,
                             .to_string = &prometheus_value_string_cb};
   struct prometheus_value* val = NULL;

   val = (struct prometheus_value*)malloc(sizeof(struct prometheus_value));
   if (val == NULL)
   {
      goto error;
   }

   memset(val, 0, sizeof(struct prometheus_value));
   val->timestamp = timestamp;
   val->value = strdup(value);

   if (val->value == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_size(values) >= 100)
   {
      struct prometheus_value* v = NULL;

      v = (struct prometheus_value*)pgmoneta_deque_poll(values, NULL);
      prometheus_value_destroy_cb((uintptr_t)v);
   }

   if (pgmoneta_deque_add_with_config(values, NULL, (uintptr_t)val, &vc))
   {
      goto error;
   }

   return 0;

error:

   if (val != NULL)
   {
      free(val->value);
      free(val);
   }

   return 1;
}

static void
prometheus_attribute_destroy_cb(uintptr_t data)
{
   struct prometheus_attribute* m = NULL;

   m = (struct prometheus_attribute*)data;

   if (m != NULL)
   {
      free(m->key);
      free(m->value);
   }

   free(m);
}

static char*
prometheus_attribute_string_cb(uintptr_t data, int32_t format, char* tag, int indent)
{
   char* s = NULL;
   struct art* a = NULL;
   struct prometheus_attribute* m = NULL;

   m = (struct prometheus_attribute*)data;

   if (pgmoneta_art_create(&a))
   {
      goto error;
   }

   if (m != NULL)
   {
      pgmoneta_art_insert(a, (char*)"Key", (uintptr_t)m->key, ValueString);
      pgmoneta_art_insert(a, (char*)"Value", (uintptr_t)m->value, ValueString);

      s = pgmoneta_art_to_string(a, format, tag, indent);
   }

   pgmoneta_art_destroy(a);

   return s;

error:

   pgmoneta_art_destroy(a);

   return "Error";
}

static int
add_attribute(struct deque* attributes, char* key, char* value)
{
   struct prometheus_attribute* attr = NULL;
   struct value_config vc = {.destroy_data = &prometheus_attribute_destroy_cb,
                             .to_string = &prometheus_attribute_string_cb};

   attr = (struct prometheus_attribute*)malloc(sizeof(struct prometheus_attribute));
   if (attr == NULL)
   {
      goto error;
   }
   memset(attr, 0, sizeof(struct prometheus_attribute));

   attr->key = strdup(key);
   if (attr->key == NULL)
   {
      goto error;
   }

   attr->value = strdup(value);
   if (attr->value == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_add_with_config(attributes, NULL, (uintptr_t)attr, &vc))
   {
      goto error;
   }

   return 0;

error:

   if (attr != NULL)
   {
      free(attr->key);
      free(attr->value);
      free(attr);
   }

   return 1;
}

static int
add_line(struct main_configuration* config, struct prometheus_metric* metric, char* line, time_t timestamp)
{
   char* endpoint_attr = NULL;
   bool is_new = false;
   char* line_value = NULL;
   struct deque* line_attrs = NULL;
   struct prometheus_attributes* attributes = NULL;
   char* p = NULL;
   char* labels_end = NULL;
   char* value_start = NULL;
   char* value_end = NULL;
   const char* host = NULL;

   if (line == NULL || metric == NULL || config == NULL)
   {
      goto error;
   }

   if (pgmoneta_deque_create(false, &line_attrs))
   {
      goto error;
   }

   host = config->host;

   endpoint_attr = pgmoneta_append(endpoint_attr, host);
   endpoint_attr = pgmoneta_append_char(endpoint_attr, ':');
   endpoint_attr = pgmoneta_append_int(endpoint_attr, config->metrics);

   if (add_attribute(line_attrs, (char*)"endpoint", endpoint_attr))
   {
      goto error;
   }

   p = line;

   if (strncmp(p, metric->name, strlen(metric->name)))
   {
      goto error;
   }

   p += strlen(metric->name);

   if (*p == '{')
   {
      bool in_quotes = false;
      bool escaped = false;

      p++;

      labels_end = p;
      while (*labels_end != '\0')
      {
         if (!escaped && *labels_end == '"')
         {
            in_quotes = !in_quotes;
         }
         else if (!in_quotes && *labels_end == '}')
         {
            break;
         }

         if (*labels_end == '\\' && !escaped)
         {
            escaped = true;
         }
         else
         {
            escaped = false;
         }

         labels_end++;
      }

      if (*labels_end != '}')
      {
         goto error;
      }

      while (p < labels_end)
      {
         char key[PROMETHEUS_LABEL_LENGTH] = {0};
         char value[PROMETHEUS_LABEL_LENGTH] = {0};
         size_t key_len = 0;
         size_t value_len = 0;

         while (p < labels_end && isspace((unsigned char)*p))
         {
            p++;
         }

         if (p >= labels_end)
         {
            break;
         }

         while (p < labels_end && *p != '=' && !isspace((unsigned char)*p))
         {
            if (key_len + 1 >= sizeof(key))
            {
               goto error;
            }

            key[key_len++] = *p;
            p++;
         }

         while (p < labels_end && isspace((unsigned char)*p))
         {
            p++;
         }

         if (p >= labels_end || *p != '=')
         {
            goto error;
         }
         p++;

         while (p < labels_end && isspace((unsigned char)*p))
         {
            p++;
         }

         if (p >= labels_end || *p != '"')
         {
            goto error;
         }
         p++;

         while (p < labels_end)
         {
            if (*p == '"')
            {
               p++;
               break;
            }

            if (*p == '\\' && (p + 1) < labels_end)
            {
               p++;

               if (value_len + 1 >= sizeof(value))
               {
                  goto error;
               }

               switch (*p)
               {
                  case 'n':
                     value[value_len++] = '\n';
                     break;
                  case 't':
                     value[value_len++] = '\t';
                     break;
                  case 'r':
                     value[value_len++] = '\r';
                     break;
                  default:
                     value[value_len++] = *p;
                     break;
               }

               p++;
               continue;
            }

            if (value_len + 1 >= sizeof(value))
            {
               goto error;
            }

            value[value_len++] = *p;
            p++;
         }

         if (key_len == 0)
         {
            goto error;
         }

         if (add_attribute(line_attrs, key, value))
         {
            goto error;
         }

         while (p < labels_end && isspace((unsigned char)*p))
         {
            p++;
         }

         if (p < labels_end)
         {
            if (*p != ',')
            {
               goto error;
            }
            p++;
         }
      }

      value_start = labels_end + 1;
   }
   else
   {
      value_start = p;
   }

   while (*value_start != '\0' && isspace((unsigned char)*value_start))
   {
      value_start++;
   }

   if (*value_start == '\0')
   {
      goto error;
   }

   value_end = value_start;
   while (*value_end != '\0' && !isspace((unsigned char)*value_end))
   {
      value_end++;
   }

   if (value_end == value_start)
   {
      goto error;
   }

   line_value = strndup(value_start, (size_t)(value_end - value_start));
   if (line_value == NULL)
   {
      goto error;
   }

   if (attributes_find_create(metric->definitions, line_attrs, &attributes, &is_new))
   {
      goto error;
   }

   if (add_value(attributes->values, timestamp, line_value))
   {
      goto error;
   }

   if (!is_new)
   {
      pgmoneta_deque_destroy(line_attrs);
   }

   free(endpoint_attr);
   free(line_value);

   return 0;

error:

   pgmoneta_deque_destroy(line_attrs);

   free(endpoint_attr);
   free(line_value);

   return 1;
}

static int
parse_metric_name_from_line(char* line, char* metric_name, size_t size)
{
   size_t idx = 0;
   char* p = line;

   if (line == NULL || metric_name == NULL || size == 0)
   {
      return 1;
   }

   while (*p != '\0' && isspace((unsigned char)*p))
   {
      p++;
   }

   if (*p == '\0')
   {
      return 1;
   }

   while (*p != '\0' && *p != '{' && !isspace((unsigned char)*p))
   {
      if (idx + 1 >= size)
      {
         return 1;
      }
      metric_name[idx++] = *p;
      p++;
   }

   metric_name[idx] = '\0';

   return idx == 0 ? 1 : 0;
}

static int
parse_body_to_bridge(time_t timestamp, char* body, struct prometheus_bridge* bridge)
{
   char* line = NULL;
   char* saveptr = NULL;
   char name[MISC_LENGTH] = {0};
   char help[MAX_PATH] = {0};
   char type[MISC_LENGTH] = {0};
   char sample_name[MISC_LENGTH] = {0};
   struct prometheus_metric* metric = NULL;
   struct main_configuration* config = NULL;

   config = (struct main_configuration*)shmem;
   if (config == NULL)
   {
      goto error;
   }

   line = strtok_r(body, "\n", &saveptr);

   while (line != NULL)
   {
      if (line[0] == '\0' || !strcmp(line, "\r"))
      {
         line = strtok_r(NULL, "\n", &saveptr);
         continue;
      }

      if (line[0] == '#')
      {
         if (!strncmp(&line[1], "HELP", 4))
         {
            memset(name, 0, sizeof(name));
            memset(help, 0, sizeof(help));

            if (sscanf(line + 6, "%127s %1023[^\n]", name, help) == 2)
            {
               if (metric_find_create(bridge, name, &metric))
               {
                  goto error;
               }

               if (metric_set_name(metric, name) || metric_set_help(metric, help))
               {
                  goto error;
               }
            }
         }
         else if (!strncmp(&line[1], "TYPE", 4))
         {
            memset(name, 0, sizeof(name));
            memset(type, 0, sizeof(type));

            if (sscanf(line + 6, "%127s %127[^\n]", name, type) == 2)
            {
               if (metric_find_create(bridge, name, &metric))
               {
                  goto error;
               }

               if (metric_set_type(metric, type))
               {
                  goto error;
               }
            }
         }

         line = strtok_r(NULL, "\n", &saveptr);
         continue;
      }

      memset(sample_name, 0, sizeof(sample_name));
      if (parse_metric_name_from_line(line, sample_name, sizeof(sample_name)))
      {
         line = strtok_r(NULL, "\n", &saveptr);
         continue;
      }

      if (metric_find_create(bridge, sample_name, &metric))
      {
         goto error;
      }

      if (add_line(config, metric, line, timestamp))
      {
         goto error;
      }

      line = strtok_r(NULL, "\n", &saveptr);
   }

   return 0;

error:
   pgmoneta_art_destroy(bridge->metrics);
   bridge->metrics = NULL;

   return 1;
}
