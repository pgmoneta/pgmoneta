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
#include <bzip2_compression.h>
#include <gzip_compression.h>
#include <json.h>
#include <logging.h>
#include <lz4_compression.h>
#include <management.h>
#include <utils.h>
#include <zstandard_compression.h>

/* system */
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

static int create_header(int32_t command, int32_t output_format, struct json** json);
static int create_request(struct json* json, struct json** request);
static int create_outcome_success(struct json* json, time_t start_time, time_t end_time, struct json** outcome);
static int create_outcome_failure(struct json* json, int32_t error, struct json** outcome);

static int read_uint8(char* prefix, SSL* ssl, int socket, uint8_t* i);
static int read_string(char* prefix, SSL* ssl, int socket, char** str);
static int write_uint8(char* prefix, SSL* ssl, int socket, uint8_t i);
static int write_string(char* prefix, SSL* ssl, int socket, char* str);
static int read_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_complete(SSL* ssl, int socket, void* buf, size_t size);
static int write_socket(int socket, void* buf, size_t size);
static int write_ssl(SSL* ssl, void* buf, size_t size);

int
pgmoneta_management_request_backup(SSL* ssl, int socket, char* server, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_BACKUP, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_list_backup(SSL* ssl, int socket, char* server, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_LIST_BACKUP, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_restore(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_RESTORE, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backup_id, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_POSITION, (uintptr_t)position, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_DIRECTORY, (uintptr_t)directory, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_verify(SSL* ssl, int socket, char* server, char* backup_id, char* directory, char* files, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_VERIFY, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backup_id, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_DIRECTORY, (uintptr_t)directory, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_FILES, (uintptr_t)files, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_archive(SSL* ssl, int socket, char* server, char* backup_id, char* position, char* directory, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_ARCHIVE, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backup_id, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_POSITION, (uintptr_t)position, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_DIRECTORY, (uintptr_t)directory, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_delete(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_DELETE, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backup_id, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_stop(SSL* ssl, int socket, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_STOP, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_status(SSL* ssl, int socket, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_STATUS, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_status_details(SSL* ssl, int socket, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_STATUS_DETAILS, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_ping(SSL* ssl, int socket, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_PING, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_reset(SSL* ssl, int socket, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_RESET, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_reload(SSL* ssl, int socket, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_RELOAD, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_conf_get(SSL* ssl, int socket,char* config_key, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_CONFIG_GET, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request,MANAGEMENT_ARGUMENT_CONFIG_KEY,(uintptr_t) config_key,ValueString);
   
   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_retain(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_RETAIN, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backup_id, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_expunge(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_EXPUNGE, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backup_id, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_decrypt(SSL* ssl, int socket, char* path, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_DECRYPT, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SOURCE_FILE, (uintptr_t)path, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_encrypt(SSL* ssl, int socket, char* path, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_ENCRYPT, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SOURCE_FILE, (uintptr_t)path, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_decompress(SSL* ssl, int socket, char* path, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_DECOMPRESS, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SOURCE_FILE, (uintptr_t)path, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_compress(SSL* ssl, int socket, char* path, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_COMPRESS, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SOURCE_FILE, (uintptr_t)path, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_info(SSL* ssl, int socket, char* server, char* backup_id, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_INFO, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backup_id, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_request_annotate(SSL* ssl, int socket, char* server, char* backup_id, char* action, char* key, char* comment, uint8_t compression, int32_t output_format)
{
   struct json* j = NULL;
   struct json* request = NULL;

   if (create_header(MANAGEMENT_ANNOTATE, output_format, &j))
   {
      goto error;
   }

   if (create_request(j, &request))
   {
      goto error;
   }

   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_BACKUP, (uintptr_t)backup_id, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_ACTION, (uintptr_t)action, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_KEY, (uintptr_t)key, ValueString);
   pgmoneta_json_put(request, MANAGEMENT_ARGUMENT_COMMENT, (uintptr_t)comment, ValueString);

   if (pgmoneta_management_write_json(ssl, socket, compression, j))
   {
      goto error;
   }

   pgmoneta_json_destroy(j);

   return 0;

error:

   pgmoneta_json_destroy(j);

   return 1;
}

int
pgmoneta_management_create_response(struct json* json, int server, struct json** response)
{
   struct json* r = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   *response = NULL;

   if (pgmoneta_json_create(&r))
   {
      goto error;
   }

   pgmoneta_json_put(json, MANAGEMENT_CATEGORY_RESPONSE, (uintptr_t)r, ValueJSON);

   if (server >= 0)
   {
      pgmoneta_json_put(r, MANAGEMENT_ARGUMENT_MAJOR_VERSION, (uintptr_t)config->servers[server].version, ValueInt32);
      pgmoneta_json_put(r, MANAGEMENT_ARGUMENT_MINOR_VERSION, (uintptr_t)config->servers[server].minor_version, ValueInt32);
      pgmoneta_json_put(r, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)config->servers[server].name, ValueString);
   }

   pgmoneta_json_put(r, MANAGEMENT_ARGUMENT_SERVER_VERSION, (uintptr_t)VERSION, ValueString);

   *response = r;

   return 0;

error:

   pgmoneta_json_destroy(r);

   return 1;
}

int
pgmoneta_management_response_ok(SSL* ssl, int socket, time_t start_time, time_t end_time, uint8_t compression, struct json* payload)
{
   struct json* outcome = NULL;

   if (create_outcome_success(payload, start_time, end_time, &outcome))
   {
      goto error;
   }

   if (pgmoneta_management_write_json(ssl, socket, compression, payload))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_response_error(SSL* ssl, int socket, char* server, int32_t error, uint8_t compression, struct json* payload)
{
   int srv = -1;
   struct json* response = NULL;
   struct json* outcome = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (create_outcome_failure(payload, error, &outcome))
   {
      goto error;
   }

   if (server != NULL && strlen(server) > 0)
   {
      if (pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_RESPONSE) != 0)
      {
         response = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_RESPONSE);
      }
      else
      {
         for (int i = 0; i < config->number_of_servers; i++)
         {
            if (!strcmp(server, config->servers[i].name))
            {
               srv = i;
            }
         }

         if (pgmoneta_management_create_response(payload, srv, &response))
         {
            goto error;
         }

         pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_SERVER, (uintptr_t)server, ValueString);
      }
   }

   if (pgmoneta_management_write_json(ssl, socket, compression, payload))
   {
      goto error;
   }

   return 0;

error:

   return 1;
}

int
pgmoneta_management_read_json(SSL* ssl, int socket, uint8_t* compression, struct json** json)
{
   uint8_t compress_method = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;

   char* s = NULL;
   struct json* r = NULL;
   unsigned char* decoded = NULL;
   size_t decoded_length = 0;
   char* decompressed_string = NULL;

   if (read_uint8("pgmoneta-cli", ssl, socket, &compress_method))
   {
      goto error;
   }

   if (compression != NULL)
   {
      *compression = compress_method;
   }

   if (read_uint8("pgmoneta-cli", ssl, socket, &encryption))
   {
      goto error;
   }

   if (read_string("pgmoneta-cli", ssl, socket, &s))
   {
      goto error;
   }

   if (compress_method || encryption)
   {
      // First, perform decode
      if (pgmoneta_base64_decode(s, strlen(s), (void**)&decoded, &decoded_length) != 0)
      {
         pgmoneta_log_error("pgmoneta_management_read_json: Decoding failedg");
         goto error;
      }

      // Second, perform dencrypt

      // Third, perform decompress
      switch (compress_method)
      {
         case MANAGEMENT_COMPRESSION_GZIP:
            if (pgmoneta_gunzip_string(decoded, decoded_length, &decompressed_string) != 0)
            {
               pgmoneta_log_error("pgmoneta_management_read_json: GZIP decompress failed");
               goto error;
            }
            break;
         case MANAGEMENT_COMPRESSION_ZSTD:
            if (pgmoneta_zstdd_string(decoded, decoded_length, &decompressed_string) != 0)
            {
               pgmoneta_log_error("pgmoneta_management_read_json: ZSTD decompress failed");
               goto error;
            }
            break;
         case MANAGEMENT_COMPRESSION_LZ4:
            if (pgmoneta_lz4d_string(decoded, decoded_length, &decompressed_string) != 0)
            {
               pgmoneta_log_error("pgmoneta_management_read_json: LZ4 decompress failed");
               goto error;
            }
            break;
         case MANAGEMENT_COMPRESSION_BZIP2:
            if (pgmoneta_bunzip2_string(decoded, decoded_length, &decompressed_string) != 0)
            {
               pgmoneta_log_error("pgmoneta_management_read_json: bzip2 decompress failed");
               goto error;
            }
            break;
         default:
            break;
      }

      free(decoded);
      free(s);
      s = decompressed_string;
      decompressed_string = NULL;
   }

   if (pgmoneta_json_parse_string(s, &r))
   {
      goto error;
   }

   *json = r;

   free(s);

   return 0;

error:

   pgmoneta_json_destroy(r);

   if (s != NULL)
   {
      free(s);
   }
   if (decoded != NULL)
   {
      free(decoded);
   }
   if (decompressed_string != NULL)
   {
      free(decompressed_string);
   }

   return 1;
}

int
pgmoneta_management_write_json(SSL* ssl, int socket, uint8_t compression, struct json* json)
{
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;

   char* s = NULL;
   unsigned char* compressed_buffer = NULL;
   size_t compressed_size = 0;
   char* encoded = NULL;
   size_t encoded_length = 0;

   s = pgmoneta_json_to_string(json, FORMAT_JSON, NULL, 0);

   if (write_uint8("pgmoneta-cli", ssl, socket, compression))
   {
      goto error;
   }

   if (write_uint8("pgmoneta-cli", ssl, socket, encryption))
   {
      goto error;
   }

   if (compression || encryption)
   {
      // First, perform compress
      switch (compression)
      {
         case MANAGEMENT_COMPRESSION_GZIP:
            if (pgmoneta_gzip_string(s, &compressed_buffer, &compressed_size) != 0)
            {
               pgmoneta_log_error("pgmoneta_management_write_json: Failed to gzip the string");
               goto error;
            }
            break;
         case MANAGEMENT_COMPRESSION_ZSTD:
            if (pgmoneta_zstdc_string(s, &compressed_buffer, &compressed_size) != 0)
            {
               pgmoneta_log_error("pgmoneta_management_write_json: Failed to zstd the string");
               goto error;
            }
            break;
         case MANAGEMENT_COMPRESSION_LZ4:
            if (pgmoneta_lz4c_string(s, &compressed_buffer, &compressed_size) != 0)
            {
               pgmoneta_log_error("pgmoneta_management_write_json: Failed to lz4 the string");
               goto error;
            }
            break;
         case MANAGEMENT_COMPRESSION_BZIP2:
            if (pgmoneta_bzip2_string(s, &compressed_buffer, &compressed_size) != 0)
            {
               pgmoneta_log_error("pgmoneta_management_write_json: Failed to bzip2 the string");
               goto error;
            }
            break;
         default:
            break;
      }

      // Second, perform encrypt

      // Third, perform base64 encode
      if (pgmoneta_base64_encode(compressed_buffer, compressed_size, &encoded, &encoded_length) != 0)
      {
         pgmoneta_log_error("pgmoneta_management_write_json: Encoding failed");
         goto error;
      }

      free(compressed_buffer);
      free(s);
      s = encoded;
      encoded = NULL;
   }

   if (write_string("pgmoneta-cli", ssl, socket, s))
   {
      goto error;
   }

   free(s);

   return 0;

error:
   if (s != NULL)
   {
      free(s);
   }
   if (compressed_buffer != NULL)
   {
      free(compressed_buffer);
   }
   if (encoded != NULL)
   {
      free(encoded);
   }

   return 1;
}

static int
read_uint8(char* prefix, SSL* ssl, int socket, uint8_t* i)
{
   char buf1[1] = {0};

   *i = 0;

   if (read_complete(ssl, socket, &buf1[0], sizeof(buf1)))
   {
      pgmoneta_log_warn("%s: read_byte: %p %d %s", prefix, ssl, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   *i = pgmoneta_read_uint8(&buf1);

   return 0;

error:

   return 1;
}

static int
read_string(char* prefix, SSL* ssl, int socket, char** str)
{
   char* s = NULL;
   char buf4[4] = {0};
   uint32_t size;

   *str = NULL;

   if (read_complete(ssl, socket, &buf4[0], sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: read_string: %p %d %s", prefix, ssl, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   size = pgmoneta_read_uint32(&buf4);
   if (size > 0)
   {
      s = malloc(size + 1);

      if (s == NULL)
      {
         goto error;
      }

      memset(s, 0, size + 1);

      if (read_complete(ssl, socket, s, size))
      {
         pgmoneta_log_warn("%s: read_string: %p %d %s", prefix, ssl, socket, strerror(errno));
         errno = 0;
         goto error;
      }

      *str = s;
   }

   return 0;

error:

   free(s);

   return 1;
}

static int
write_uint8(char* prefix, SSL* ssl, int socket, uint8_t i)
{
   char buf1[1] = {0};

   pgmoneta_write_uint8(&buf1, i);
   if (write_complete(ssl, socket, &buf1, sizeof(buf1)))
   {
      pgmoneta_log_warn("%s: write_string: %p %d %s", prefix, ssl, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   return 0;

error:

   return 1;
}

static int
write_string(char* prefix, SSL* ssl, int socket, char* str)
{
   char buf4[4] = {0};

   pgmoneta_write_uint32(&buf4, str != NULL ? strlen(str) : 0);
   if (write_complete(ssl, socket, &buf4, sizeof(buf4)))
   {
      pgmoneta_log_warn("%s: write_string: %p %d %s", prefix, ssl, socket, strerror(errno));
      errno = 0;
      goto error;
   }

   if (str != NULL)
   {
      if (write_complete(ssl, socket, str, strlen(str)))
      {
         pgmoneta_log_warn("%s: write_string: %p %d %s", prefix, ssl, socket, strerror(errno));
         errno = 0;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
read_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   ssize_t r;
   size_t offset;
   size_t needs;
   int retries;

   offset = 0;
   needs = size;
   retries = 0;

read:
   if (ssl == NULL)
   {
      r = read(socket, buf + offset, needs);
   }
   else
   {
      r = SSL_read(ssl, buf + offset, needs);
   }

   if (r == -1)
   {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
         errno = 0;
         goto read;
      }

      goto error;
   }
   else if (r < (ssize_t)needs)
   {
      /* Sleep for 10ms */
      SLEEP(10000000L);

      if (retries < 100)
      {
         offset += r;
         needs -= r;
         retries++;
         goto read;
      }
      else
      {
         errno = EINVAL;
         goto error;
      }
   }

   return 0;

error:

   return 1;
}

static int
write_complete(SSL* ssl, int socket, void* buf, size_t size)
{
   if (ssl == NULL)
   {
      return write_socket(socket, buf, size);
   }

   return write_ssl(ssl, buf, size);
}

static int
write_socket(int socket, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = write(socket, buf + offset, remaining);

      if (likely(numbytes == (ssize_t)size))
      {
         return 0;
      }
      else if (numbytes != -1)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == (ssize_t)size)
         {
            return 0;
         }

         pgmoneta_log_trace("Write %d - %zd/%zd vs %zd", socket, numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         switch (errno)
         {
            case EAGAIN:
               keep_write = true;
               errno = 0;
               break;
            default:
               keep_write = false;
               break;
         }
      }
   }
   while (keep_write);

   return 1;
}

static int
write_ssl(SSL* ssl, void* buf, size_t size)
{
   bool keep_write = false;
   ssize_t numbytes;
   int offset;
   ssize_t totalbytes;
   ssize_t remaining;

   numbytes = 0;
   offset = 0;
   totalbytes = 0;
   remaining = size;

   do
   {
      numbytes = SSL_write(ssl, buf + offset, remaining);

      if (likely(numbytes == (ssize_t)size))
      {
         return 0;
      }
      else if (numbytes > 0)
      {
         offset += numbytes;
         totalbytes += numbytes;
         remaining -= numbytes;

         if (totalbytes == (ssize_t)size)
         {
            return 0;
         }

         pgmoneta_log_trace("SSL/Write %d - %zd/%zd vs %zd", SSL_get_fd(ssl), numbytes, totalbytes, size);
         keep_write = true;
         errno = 0;
      }
      else
      {
         int err = SSL_get_error(ssl, numbytes);

         switch (err)
         {
            case SSL_ERROR_ZERO_RETURN:
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_CONNECT:
            case SSL_ERROR_WANT_ACCEPT:
            case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
            case SSL_ERROR_WANT_ASYNC:
            case SSL_ERROR_WANT_ASYNC_JOB:
            case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
               errno = 0;
               keep_write = true;
               break;
            case SSL_ERROR_SYSCALL:
               pgmoneta_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
            case SSL_ERROR_SSL:
               pgmoneta_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), SSL_get_fd(ssl));
               errno = 0;
               keep_write = false;
               break;
         }
         ERR_clear_error();

         if (!keep_write)
         {
            return 1;
         }
      }
   }
   while (keep_write);

   return 1;
}

static int
create_header(int32_t command, int32_t output_format, struct json** json)
{
   time_t t;
   char timestamp[128];
   struct tm* time_info;
   struct json* j = NULL;
   struct json* header = NULL;

   *json = NULL;

   if (pgmoneta_json_create(&j))
   {
      goto error;
   }

   if (pgmoneta_json_create(&header))
   {
      goto error;
   }

   time(&t);
   time_info = localtime(&t);
   strftime(&timestamp[0], sizeof(timestamp), "%Y%m%d%H%M%S", time_info);

   pgmoneta_json_put(header, MANAGEMENT_ARGUMENT_COMMAND, (uintptr_t)command, ValueInt32);
   pgmoneta_json_put(header, MANAGEMENT_ARGUMENT_CLIENT_VERSION, (uintptr_t)VERSION, ValueString);
   pgmoneta_json_put(header, MANAGEMENT_ARGUMENT_OUTPUT, (uintptr_t)output_format, ValueUInt8);
   pgmoneta_json_put(header, MANAGEMENT_ARGUMENT_TIMESTAMP, (uintptr_t)timestamp, ValueString);

   pgmoneta_json_put(j, MANAGEMENT_CATEGORY_HEADER, (uintptr_t)header, ValueJSON);

   *json = j;

   return 0;

error:

   pgmoneta_json_destroy(header);
   pgmoneta_json_destroy(j);

   *json = NULL;

   return 1;
}

static int
create_request(struct json* json, struct json** request)
{
   struct json* r = NULL;

   *request = NULL;

   if (pgmoneta_json_create(&r))
   {
      goto error;
   }

   pgmoneta_json_put(json, MANAGEMENT_CATEGORY_REQUEST, (uintptr_t)r, ValueJSON);

   *request = r;

   return 0;

error:

   pgmoneta_json_destroy(r);

   return 1;
}

static int
create_outcome_success(struct json* json, time_t start_time, time_t end_time, struct json** outcome)
{
   int32_t total_seconds = 0;
   char* elapsed = NULL;
   struct json* r = NULL;

   *outcome = NULL;

   if (pgmoneta_json_create(&r))
   {
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_time, end_time, &total_seconds);

   pgmoneta_json_put(r, MANAGEMENT_ARGUMENT_STATUS, (uintptr_t)true, ValueBool);
   pgmoneta_json_put(r, MANAGEMENT_ARGUMENT_TIME, (uintptr_t)elapsed, ValueString);

   pgmoneta_json_put(json, MANAGEMENT_CATEGORY_OUTCOME, (uintptr_t)r, ValueJSON);

   *outcome = r;

   free(elapsed);

   return 0;

error:

   free(elapsed);

   pgmoneta_json_destroy(r);

   return 1;
}

static int
create_outcome_failure(struct json* json, int32_t error, struct json** outcome)
{
   struct json* r = NULL;

   *outcome = NULL;

   if (pgmoneta_json_create(&r))
   {
      goto error;
   }

   pgmoneta_json_put(r, MANAGEMENT_ARGUMENT_STATUS, (uintptr_t)false, ValueBool);
   pgmoneta_json_put(r, MANAGEMENT_ARGUMENT_ERROR, (uintptr_t)error, ValueInt32);

   pgmoneta_json_put(json, MANAGEMENT_CATEGORY_OUTCOME, (uintptr_t)r, ValueJSON);

   *outcome = r;

   return 0;

error:

   pgmoneta_json_destroy(r);

   return 1;
}
