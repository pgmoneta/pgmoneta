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

/* pgmoneta */
#include <pgmoneta.h>
#include <logging.h>
#include <management.h>
#include <network.h>
#include <security.h>
#include <utils.h>

/* system */
#include <errno.h>
#include <ev.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>

#define NAME "remote"

static void drain_client_socket(int fd);

void
pgmoneta_remote_management(int client_fd, char* address)
{
   int server_fd = -1;
   int exit_code;
   int auth_status;
   uint8_t compression = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;
   SSL* client_ssl = NULL;
   struct json* payload = NULL;
   struct main_configuration* config;
   bool drain_client = false;

   pgmoneta_start_logging();
   pgmoneta_memory_init();

   exit_code = 0;

   config = (struct main_configuration*)shmem;

   pgmoneta_log_debug("pgmoneta_remote_management: connect %d", client_fd);

   auth_status = pgmoneta_remote_management_auth(client_fd, address, &client_ssl);
   if (auth_status == AUTH_SUCCESS)
   {
      if (pgmoneta_management_read_json(client_ssl, client_fd, &compression, &encryption, &payload))
      {
         int primary_rc;
         int fallback_rc = 1;

         primary_rc = pgmoneta_management_response_error(client_ssl, client_fd, NULL, MANAGEMENT_ERROR_BAD_PAYLOAD, NAME,
                                                         compression, encryption, NULL);
         if (primary_rc == 0)
         {
            drain_client = true;
         }
         if (primary_rc)
         {
            fallback_rc = pgmoneta_management_response_error(client_ssl, client_fd, NULL, MANAGEMENT_ERROR_BAD_PAYLOAD, NAME,
                                                             MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, NULL);
            if (fallback_rc == 0)
            {
               drain_client = true;
            }
         }
         pgmoneta_log_error("Remote management: Bad payload (%d)", MANAGEMENT_ERROR_BAD_PAYLOAD);
         goto done;
      }

      if (pgmoneta_connect_unix_socket(config->common.unix_socket_dir, MAIN_UDS, &server_fd))
      {
         goto done;
      }

      if (pgmoneta_management_write_json(NULL, server_fd, compression, encryption, payload))
      {
         goto done;
      }

      pgmoneta_json_destroy(payload);
      payload = NULL;

      if (pgmoneta_management_read_json(NULL, server_fd, &compression, &encryption, &payload))
      {
         goto done;
      }

      if (pgmoneta_management_write_json(client_ssl, client_fd, compression, encryption, payload))
      {
         goto done;
      }
   }
   else
   {
      exit_code = 1;
   }

done:

   pgmoneta_json_destroy(payload);
   payload = NULL;

   if (client_ssl != NULL)
   {
      int res;
      SSL_CTX* ctx = SSL_get_SSL_CTX(client_ssl);
      res = SSL_shutdown(client_ssl);
      if (res == 0)
      {
         res = SSL_shutdown(client_ssl);
      }
      SSL_free(client_ssl);
      SSL_CTX_free(ctx);
   }

   if (drain_client)
   {
      drain_client_socket(client_fd);
   }

   pgmoneta_disconnect(client_fd);
   pgmoneta_disconnect(server_fd);

   free(address);

   pgmoneta_memory_destroy();
   pgmoneta_stop_logging();

   exit(exit_code);
}

static void
drain_client_socket(int fd)
{
   char buffer[1024];
   ssize_t bytes = 0;
   bool was_nonblocking = false;

   if (fd == -1)
   {
      return;
   }

   was_nonblocking = pgmoneta_socket_is_nonblocking(fd);

   if (shutdown(fd, SHUT_WR) != 0)
   {
      errno = 0;
   }

   pgmoneta_socket_nonblocking(fd, true);

   for (;;)
   {
      bytes = read(fd, buffer, sizeof(buffer));
      if (bytes > 0)
      {
         continue;
      }

      if (bytes == 0)
      {
         break;
      }

      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
         errno = 0;
         break;
      }

      errno = 0;
      break;
   }

   pgmoneta_socket_nonblocking(fd, was_nonblocking);
}
