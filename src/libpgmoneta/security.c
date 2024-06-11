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
#include <logging.h>
#include <memory.h>
#include <message.h>
#include <network.h>
#include <prometheus.h>
#include <security.h>
#include <utils.h>
#include <io.h>

/* system */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifdef HAVE_SSE42
#include <immintrin.h>
#endif

#define SECURITY_INVALID  -2
#define SECURITY_REJECT   -1
#define SECURITY_TRUST     0
#define SECURITY_PASSWORD  3
#define SECURITY_MD5       5
#define SECURITY_SCRAM256 10
#define SECURITY_ALL      99

#define NUMBER_OF_SECURITY_MESSAGES    5
#define SECURITY_BUFFER_SIZE        1024

static signed char has_security;
static ssize_t security_lengths[NUMBER_OF_SECURITY_MESSAGES];
static char security_messages[NUMBER_OF_SECURITY_MESSAGES][SECURITY_BUFFER_SIZE];

static int get_auth_type(struct message* msg, int* auth_type);
static int get_salt(void* data, char** salt);
static int generate_md5(char* str, int length, char** md5);

static int client_scram256(SSL* c_ssl, int client_fd, char* username, char* password, int slot);

static int server_trust(void);
static int server_password(char* username, char* password, SSL* ssl, int server_fd);
static int server_md5(char* username, char* password, SSL* ssl, int server_fd);
static int server_scram256(char* username, char* password, SSL* ssl, int server_fd);

static char* get_admin_password(char* username);

static int sasl_prep(char* password, char** password_prep);
static int generate_nounce(char** nounce);
static int get_scram_attribute(char attribute, char* input, size_t size, char** value);
static int client_proof(char* password, char* salt, int salt_length, int iterations,
                        char* client_first_message_bare, size_t client_first_message_bare_length,
                        char* server_first_message, size_t server_first_message_length,
                        char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
                        unsigned char** result, int* result_length);
static int  salted_password(char* password, char* salt, int salt_length, int iterations, unsigned char** result, int* result_length);
static int  salted_password_key(unsigned char* salted_password, int salted_password_length, char* key,
                                unsigned char** result, int* result_length);
static int  stored_key(unsigned char* client_key, int client_key_length, unsigned char** result, int* result_length);
static int  generate_salt(char** salt, int* size);
static int  server_signature(char* password, char* salt, int salt_length, int iterations,
                             char* server_key, int server_key_length,
                             char* client_first_message_bare, size_t client_first_message_bare_length,
                             char* server_first_message, size_t server_first_message_length,
                             char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
                             unsigned char** result, int* result_length);

static int  create_ssl_ctx(bool client, SSL_CTX** ctx);
static int  create_ssl_client(SSL_CTX* ctx, char* key, char* cert, char* root, int socket, SSL** ssl);
static int  create_ssl_server(SSL_CTX* ctx, int socket, SSL** ssl);

static int create_hash_file(char* filename, const char* algorithm, char** hash);

int
pgmoneta_remote_management_auth(int client_fd, char* address, SSL** client_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   struct configuration* config;
   struct message* msg = NULL;
   struct message* request_msg = NULL;
   int32_t request;
   char* username = NULL;
   char* database = NULL;
   char* appname = NULL;
   char* password = NULL;
   SSL* c_ssl = NULL;

   config = (struct configuration*)shmem;

   *client_ssl = NULL;

   /* Receive client calls - at any point if client exits return AUTH_ERROR */
   status = pgmoneta_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   request = pgmoneta_get_request(msg);

   /* SSL request: 80877103 */
   if (request == 80877103)
   {
      pgmoneta_log_debug("SSL request from client: %d", client_fd);

      if (config->tls)
      {
         SSL_CTX* ctx = NULL;

         /* We are acting as a server against the client */
         if (create_ssl_ctx(false, &ctx))
         {
            goto error;
         }

         if (create_ssl_server(ctx, client_fd, &c_ssl))
         {
            goto error;
         }

         *client_ssl = c_ssl;

         /* Switch to TLS mode */
         status = pgmoneta_write_tls(NULL, client_fd);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgmoneta_free_message(msg);

         status = SSL_accept(c_ssl);
         if (status != 1)
         {
            unsigned long err;

            err = ERR_get_error();
            pgmoneta_log_error("SSL failed: %s", ERR_reason_error_string(err));
            goto error;
         }

         status = pgmoneta_read_timeout_message(c_ssl, client_fd, config->authentication_timeout, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         request = pgmoneta_get_request(msg);
      }
      else
      {
         status = pgmoneta_write_notice(NULL, client_fd);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         pgmoneta_free_message(msg);

         status = pgmoneta_read_timeout_message(NULL, client_fd, config->authentication_timeout, &msg);
         if (status != MESSAGE_STATUS_OK)
         {
            goto error;
         }
         request = pgmoneta_get_request(msg);
      }
   }

   /* 196608 -> Ok */
   if (request == 196608)
   {
      request_msg = pgmoneta_copy_message(msg);

      /* Extract parameters: username / database */
      pgmoneta_log_trace("remote_management_auth: username/database (%d)", client_fd);
      pgmoneta_extract_username_database(request_msg, &username, &database, &appname);

      /* Must be admin database */
      if (strcmp("admin", database) != 0)
      {
         pgmoneta_log_debug("remote_management_auth: admin: %s / %s", username, address);
         pgmoneta_write_connection_refused(c_ssl, client_fd);
         pgmoneta_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      password = get_admin_password(username);
      if (password == NULL)
      {
         pgmoneta_log_debug("remote_management_auth: password: %s / admin / %s", username, address);
         pgmoneta_write_connection_refused(c_ssl, client_fd);
         pgmoneta_write_empty(c_ssl, client_fd);
         goto bad_password;
      }

      status = client_scram256(c_ssl, client_fd, username, password, -1);
      if (status == AUTH_BAD_PASSWORD)
      {
         pgmoneta_write_connection_refused(c_ssl, client_fd);
         pgmoneta_write_empty(c_ssl, client_fd);
         goto bad_password;
      }
      else if (status == AUTH_ERROR)
      {
         pgmoneta_write_connection_refused(c_ssl, client_fd);
         pgmoneta_write_empty(c_ssl, client_fd);
         goto error;
      }

      status = pgmoneta_write_auth_success(c_ssl, client_fd);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }

      pgmoneta_free_copy_message(request_msg);
      free(username);
      free(database);
      free(appname);

      pgmoneta_log_debug("remote_management_auth: SUCCESS");
      return AUTH_SUCCESS;
   }
   else if (request == -1)
   {
      goto error;
   }
   else
   {
      pgmoneta_log_debug("remote_management_auth: old version: %d (%s)", request, address);
      pgmoneta_write_connection_refused_old(c_ssl, client_fd);
      pgmoneta_write_empty(c_ssl, client_fd);
      goto bad_password;
   }

bad_password:
   pgmoneta_free_message(msg);
   pgmoneta_free_copy_message(request_msg);

   free(username);
   free(database);
   free(appname);

   pgmoneta_log_debug("remote_management_auth: BAD_PASSWORD");
   return AUTH_BAD_PASSWORD;

error:
   pgmoneta_free_message(msg);
   pgmoneta_free_copy_message(request_msg);

   free(username);
   free(database);
   free(appname);

   pgmoneta_log_debug("remote_management_auth: ERROR");
   return AUTH_ERROR;
}

int
pgmoneta_remote_management_scram_sha256(char* username, char* password, int server_fd, SSL** s_ssl)
{
   int status = MESSAGE_STATUS_ERROR;
   SSL* ssl = NULL;
   char key_file[MISC_LENGTH];
   char cert_file[MISC_LENGTH];
   char root_file[MISC_LENGTH];
   struct stat st = {0};
   char* salt = NULL;
   int salt_length = 0;
   char* password_prep = NULL;
   char* client_nounce = NULL;
   char* combined_nounce = NULL;
   char* base64_salt = NULL;
   char* iteration_string = NULL;
   char* err = NULL;
   int iteration;
   char* client_first_message_bare = NULL;
   char* server_first_message = NULL;
   char wo_proof[58];
   unsigned char* proof = NULL;
   int proof_length;
   char* proof_base = NULL;
   char* base64_server_signature = NULL;
   char* server_signature_received = NULL;
   int server_signature_received_length;
   unsigned char* server_signature_calc = NULL;
   int server_signature_calc_length;
   struct message* sslrequest_msg = NULL;
   struct message* startup_msg = NULL;
   struct message* sasl_response = NULL;
   struct message* sasl_continue = NULL;
   struct message* sasl_continue_response = NULL;
   struct message* sasl_final = NULL;
   struct message* msg = NULL;

   pgmoneta_memory_size(DEFAULT_BUFFER_SIZE);

   if (pgmoneta_get_home_directory() == NULL)
   {
      goto error;
   }

   memset(&key_file, 0, sizeof(key_file));
   snprintf(&key_file[0], sizeof(key_file), "%s/.pgmoneta/pgmoneta.key", pgmoneta_get_home_directory());

   memset(&cert_file, 0, sizeof(cert_file));
   snprintf(&cert_file[0], sizeof(cert_file), "%s/.pgmoneta/pgmoneta.crt", pgmoneta_get_home_directory());

   memset(&root_file, 0, sizeof(root_file));
   snprintf(&root_file[0], sizeof(root_file), "%s/.pgmoneta/root.crt", pgmoneta_get_home_directory());

   if (stat(&key_file[0], &st) == 0)
   {
      if (S_ISREG(st.st_mode) && st.st_mode & (S_IRUSR | S_IWUSR) && !(st.st_mode & S_IRWXG) && !(st.st_mode & S_IRWXO))
      {
         if (stat(&cert_file[0], &st) == 0)
         {
            if (S_ISREG(st.st_mode))
            {
               SSL_CTX* ctx = NULL;

               status = pgmoneta_create_ssl_message(&sslrequest_msg);
               if (status != MESSAGE_STATUS_OK)
               {
                  goto error;
               }

               status = pgmoneta_write_message(NULL, server_fd, sslrequest_msg);
               if (status != MESSAGE_STATUS_OK)
               {
                  goto error;
               }

               status = pgmoneta_read_block_message(NULL, server_fd, &msg);
               if (status != MESSAGE_STATUS_OK)
               {
                  goto error;
               }

               if (msg->kind == 'S')
               {
                  if (create_ssl_ctx(true, &ctx))
                  {
                     goto error;
                  }

                  if (stat(&root_file[0], &st) == -1)
                  {
                     memset(&root_file, 0, sizeof(root_file));
                  }

                  if (create_ssl_client(ctx, &key_file[0], &cert_file[0], &root_file[0], server_fd, &ssl))
                  {
                     goto error;
                  }

                  *s_ssl = ssl;

                  do
                  {
                     status = SSL_connect(ssl);

                     if (status != 1)
                     {
                        int err = SSL_get_error(ssl, status);
                        switch (err)
                        {
                           case SSL_ERROR_ZERO_RETURN:
                           case SSL_ERROR_WANT_READ:
                           case SSL_ERROR_WANT_WRITE:
                           case SSL_ERROR_WANT_CONNECT:
                           case SSL_ERROR_WANT_ACCEPT:
                           case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
                           case SSL_ERROR_WANT_ASYNC:
                           case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
                           case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
#endif
#endif
                              break;
                           case SSL_ERROR_SYSCALL:
                              pgmoneta_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), server_fd);
                              errno = 0;
                              goto error;
                              break;
                           case SSL_ERROR_SSL:
                              pgmoneta_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), server_fd);
                              pgmoneta_log_error("%s", ERR_error_string(err, NULL));
                              pgmoneta_log_error("%s", ERR_lib_error_string(err));
                              pgmoneta_log_error("%s", ERR_reason_error_string(err));
                              errno = 0;
                              goto error;
                              break;
                        }
                        ERR_clear_error();
                     }
                  }
                  while (status != 1);
               }
            }
         }
      }
   }

   status = pgmoneta_create_startup_message(username, "admin", false, &startup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_write_message(ssl, server_fd, startup_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_read_block_message(ssl, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (msg->kind != 'R')
   {
      goto error;
   }

   status = sasl_prep(password, &password_prep);
   if (status)
   {
      goto error;
   }

   generate_nounce(&client_nounce);

   status = pgmoneta_create_auth_scram256_response(client_nounce, &sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_write_message(ssl, server_fd, sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_read_block_message(ssl, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   sasl_continue = pgmoneta_copy_message(msg);

   get_scram_attribute('r', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &combined_nounce);
   get_scram_attribute('s', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &base64_salt);
   get_scram_attribute('i', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &iteration_string);
   get_scram_attribute('e', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &err);

   if (err != NULL)
   {
      goto error;
   }

   pgmoneta_base64_decode(base64_salt, strlen(base64_salt), &salt, &salt_length);

   iteration = atoi(iteration_string);

   memset(&wo_proof[0], 0, sizeof(wo_proof));
   snprintf(&wo_proof[0], sizeof(wo_proof), "c=biws,r=%s", combined_nounce);

   /* n=,r=... */
   client_first_message_bare = sasl_response->data + 26;

   /* r=...,s=...,i=4096 */
   server_first_message = sasl_continue->data + 9;

   if (client_proof(password_prep, salt, salt_length, iteration,
                    client_first_message_bare, sasl_response->length - 26,
                    server_first_message, sasl_continue->length - 9,
                    &wo_proof[0], strlen(wo_proof),
                    &proof, &proof_length))
   {
      goto error;
   }

   pgmoneta_base64_encode((char*)proof, proof_length, &proof_base);

   status = pgmoneta_create_auth_scram256_continue_response(&wo_proof[0], (char*)proof_base, &sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_write_message(ssl, server_fd, sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_read_block_message(ssl, server_fd, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (pgmoneta_extract_message('R', msg, &sasl_final))
   {
      goto error;
   }

   /* Get 'v' attribute */
   base64_server_signature = sasl_final->data + 11;
   pgmoneta_base64_decode(base64_server_signature, sasl_final->length - 11, &server_signature_received, &server_signature_received_length);

   if (server_signature(password_prep, salt, salt_length, iteration,
                        NULL, 0,
                        client_first_message_bare, sasl_response->length - 26,
                        server_first_message, sasl_continue->length - 9,
                        &wo_proof[0], strlen(wo_proof),
                        &server_signature_calc, &server_signature_calc_length))
   {
      goto error;
   }

   if (server_signature_calc_length != server_signature_received_length ||
       memcmp(server_signature_received, server_signature_calc, server_signature_calc_length) != 0)
   {
      goto bad_password;
   }

   if (msg->length == 55)
   {
      status = pgmoneta_read_block_message(ssl, server_fd, &msg);
      if (status != MESSAGE_STATUS_OK)
      {
         goto error;
      }
   }

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgmoneta_free_message(msg);
   pgmoneta_free_copy_message(sslrequest_msg);
   pgmoneta_free_copy_message(startup_msg);
   pgmoneta_free_copy_message(sasl_response);
   pgmoneta_free_copy_message(sasl_continue);
   pgmoneta_free_copy_message(sasl_continue_response);
   pgmoneta_free_copy_message(sasl_final);

   pgmoneta_memory_destroy();

   return AUTH_SUCCESS;

bad_password:

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgmoneta_free_message(msg);
   pgmoneta_free_copy_message(sslrequest_msg);
   pgmoneta_free_copy_message(startup_msg);
   pgmoneta_free_copy_message(sasl_response);
   pgmoneta_free_copy_message(sasl_continue);
   pgmoneta_free_copy_message(sasl_continue_response);
   pgmoneta_free_copy_message(sasl_final);

   pgmoneta_memory_destroy();

   return AUTH_BAD_PASSWORD;

error:

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgmoneta_free_message(msg);
   pgmoneta_free_copy_message(sslrequest_msg);
   pgmoneta_free_copy_message(startup_msg);
   pgmoneta_free_copy_message(sasl_response);
   pgmoneta_free_copy_message(sasl_continue);
   pgmoneta_free_copy_message(sasl_continue_response);
   pgmoneta_free_copy_message(sasl_final);

   pgmoneta_memory_destroy();

   return AUTH_ERROR;
}

static int
get_auth_type(struct message* msg, int* auth_type)
{
   int32_t length;
   int32_t type = -1;
   int offset;

   if (msg->kind != 'R')
   {
      return 1;
   }

   length = pgmoneta_read_int32(msg->data + 1);
   type = pgmoneta_read_int32(msg->data + 5);
   offset = 9;

   if (type == 0 && msg->length > 8)
   {
      if ('E' == pgmoneta_read_byte(msg->data + 9))
      {
         *auth_type = -1;
         return 0;
      }
   }

   switch (type)
   {
      case 0:
         pgmoneta_log_trace("Backend: R - Success");
         break;
      case 2:
         pgmoneta_log_trace("Backend: R - KerberosV5");
         break;
      case 3:
         pgmoneta_log_trace("Backend: R - CleartextPassword");
         break;
      case 5:
         pgmoneta_log_trace("Backend: R - MD5Password");
         pgmoneta_log_trace("             Salt %02hhx%02hhx%02hhx%02hhx",
                            (signed char)(pgmoneta_read_byte(msg->data + 9) & 0xFF),
                            (signed char)(pgmoneta_read_byte(msg->data + 10) & 0xFF),
                            (signed char)(pgmoneta_read_byte(msg->data + 11) & 0xFF),
                            (signed char)(pgmoneta_read_byte(msg->data + 12) & 0xFF));
         break;
      case 6:
         pgmoneta_log_trace("Backend: R - SCMCredential");
         break;
      case 7:
         pgmoneta_log_trace("Backend: R - GSS");
         break;
      case 8:
         pgmoneta_log_trace("Backend: R - GSSContinue");
         break;
      case 9:
         pgmoneta_log_trace("Backend: R - SSPI");
         break;
      case 10:
         pgmoneta_log_trace("Backend: R - SASL");
         while (offset < length - 8)
         {
            char* mechanism = pgmoneta_read_string(msg->data + offset);
            pgmoneta_log_trace("             %s", mechanism);
            offset += strlen(mechanism) + 1;
         }
         break;
      case 11:
         pgmoneta_log_trace("Backend: R - SASLContinue");
         break;
      case 12:
         pgmoneta_log_trace("Backend: R - SASLFinal");
         offset += length - 8;

         if (offset < msg->length)
         {
            signed char peek = pgmoneta_read_byte(msg->data + offset);
            switch (peek)
            {
               case 'R':
                  type = pgmoneta_read_int32(msg->data + offset + 5);
                  break;
               default:
                  break;
            }
         }

         break;
      default:
         break;
   }

   *auth_type = type;

   return 0;
}

static int
get_salt(void* data, char** salt)
{
   char* result;

   result = malloc(4);

   if (result == NULL)
   {
      goto error;
   }

   memset(result, 0, 4);

   memcpy(result, data + 9, 4);

   *salt = result;

   return 0;

error:

   return 1;
}

static int
generate_md5(char* str, int length, char** md5)
{
   int n;
   MD5_CTX c;
   unsigned char digest[16];
   char* out;

   out = malloc(33);

   if (out == NULL)
   {
      goto error;
   }

   memset(out, 0, 33);

   MD5_Init(&c);
   MD5_Update(&c, str, length);
   MD5_Final(digest, &c);

   for (n = 0; n < 16; ++n)
   {
      snprintf(&(out[n * 2]), 32, "%02x", (unsigned int)digest[n]);
   }

   *md5 = out;

   return 0;

error:

   return 1;
}

static int
client_scram256(SSL* c_ssl, int client_fd, char* username, char* password, int slot)
{
   int status;
   time_t start_time;
   bool non_blocking;
   char* password_prep = NULL;
   char* client_first_message_bare = NULL;
   char* server_first_message = NULL;
   char* client_final_message_without_proof = NULL;
   char* client_nounce = NULL;
   char* server_nounce = NULL;
   char* salt = NULL;
   int salt_length = 0;
   char* base64_salt = NULL;
   char* base64_client_proof = NULL;
   char* client_proof_received = NULL;
   int client_proof_received_length = 0;
   unsigned char* client_proof_calc = NULL;
   int client_proof_calc_length = 0;
   unsigned char* server_signature_calc = NULL;
   int server_signature_calc_length = 0;
   char* base64_server_signature_calc = NULL;
   struct configuration* config;
   struct message* msg = NULL;
   struct message* sasl_continue = NULL;
   struct message* sasl_final = NULL;

   pgmoneta_log_debug("client_scram256 %d %d", client_fd, slot);

   config = (struct configuration*)shmem;

   status = pgmoneta_write_auth_scram256(c_ssl, client_fd);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   start_time = time(NULL);

   non_blocking = pgmoneta_socket_is_nonblocking(client_fd);
   pgmoneta_socket_nonblocking(client_fd, true);

   /* psql may just close the connection without word, so loop */
retry:
   status = pgmoneta_read_timeout_message(c_ssl, client_fd, 1, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      if (difftime(time(NULL), start_time) < config->authentication_timeout)
      {
         if (pgmoneta_socket_isvalid(client_fd))
         {
            /* Sleep for 100ms */
            SLEEP_AND_GOTO(100000000L, retry);
         }
      }
   }

   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (!non_blocking)
   {
      pgmoneta_socket_nonblocking(client_fd, false);
   }

   client_first_message_bare = malloc(msg->length - 25);

   if (client_first_message_bare == NULL)
   {
      goto error;
   }

   memset(client_first_message_bare, 0, msg->length - 25);
   memcpy(client_first_message_bare, msg->data + 26, msg->length - 26);

   get_scram_attribute('r', (char*)msg->data + 26, msg->length - 26, &client_nounce);
   generate_nounce(&server_nounce);
   generate_salt(&salt, &salt_length);
   pgmoneta_base64_encode(salt, salt_length, &base64_salt);

   server_first_message = malloc(89);

   if (server_first_message == NULL)
   {
      goto error;
   }

   memset(server_first_message, 0, 89);
   snprintf(server_first_message, 89, "r=%s%s,s=%s,i=4096", client_nounce, server_nounce, base64_salt);

   status = pgmoneta_create_auth_scram256_continue(client_nounce, server_nounce, base64_salt, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   sasl_continue = pgmoneta_copy_message(msg);

   pgmoneta_free_copy_message(msg);
   msg = NULL;

   status = pgmoneta_write_message(c_ssl, client_fd, sasl_continue);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_read_timeout_message(c_ssl, client_fd, config->authentication_timeout, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   get_scram_attribute('p', (char*)msg->data + 5, msg->length - 5, &base64_client_proof);
   pgmoneta_base64_decode(base64_client_proof, strlen(base64_client_proof), &client_proof_received, &client_proof_received_length);

   client_final_message_without_proof = malloc(58);
   memset(client_final_message_without_proof, 0, 58);
   memcpy(client_final_message_without_proof, msg->data + 5, 57);

   sasl_prep(password, &password_prep);

   if (client_proof(password_prep, salt, salt_length, 4096,
                    client_first_message_bare, strlen(client_first_message_bare),
                    server_first_message, strlen(server_first_message),
                    client_final_message_without_proof, strlen(client_final_message_without_proof),
                    &client_proof_calc, &client_proof_calc_length))
   {
      goto error;
   }

   if (client_proof_received_length != client_proof_calc_length ||
       memcmp(client_proof_received, client_proof_calc, client_proof_calc_length) != 0)
   {
      goto bad_password;
   }

   if (server_signature(password_prep, salt, salt_length, 4096,
                        NULL, 0,
                        client_first_message_bare, strlen(client_first_message_bare),
                        server_first_message, strlen(server_first_message),
                        client_final_message_without_proof, strlen(client_final_message_without_proof),
                        &server_signature_calc, &server_signature_calc_length))
   {
      goto error;
   }

   pgmoneta_base64_encode((char*)server_signature_calc, server_signature_calc_length, &base64_server_signature_calc);

   status = pgmoneta_create_auth_scram256_final(base64_server_signature_calc, &msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   sasl_final = pgmoneta_copy_message(msg);

   pgmoneta_free_copy_message(msg);
   msg = NULL;

   status = pgmoneta_write_message(c_ssl, client_fd, sasl_final);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   pgmoneta_log_debug("client_scram256 done");

   free(password_prep);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(salt);
   free(base64_salt);
   free(base64_client_proof);
   free(client_proof_received);
   free(client_proof_calc);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgmoneta_free_copy_message(sasl_continue);
   pgmoneta_free_copy_message(sasl_final);

   return AUTH_SUCCESS;

bad_password:
   free(password_prep);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(salt);
   free(base64_salt);
   free(base64_client_proof);
   free(client_proof_received);
   free(client_proof_calc);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgmoneta_free_copy_message(sasl_continue);
   pgmoneta_free_copy_message(sasl_final);

   return AUTH_BAD_PASSWORD;

error:
   free(password_prep);
   free(client_first_message_bare);
   free(server_first_message);
   free(client_final_message_without_proof);
   free(client_nounce);
   free(server_nounce);
   free(salt);
   free(base64_salt);
   free(base64_client_proof);
   free(client_proof_received);
   free(client_proof_calc);
   free(server_signature_calc);
   free(base64_server_signature_calc);

   pgmoneta_free_copy_message(sasl_continue);
   pgmoneta_free_copy_message(sasl_final);

   return AUTH_ERROR;
}

int
pgmoneta_server_authenticate(int server, char* database, char* username, char* password, bool replication, SSL** ssl, int* fd)
{
   int server_fd;
   int auth_type;
   int ret;
   int status = AUTH_ERROR;
   int connect;
   SSL* c_ssl = NULL;
   struct message* ssl_msg = NULL;
   struct message* startup_msg = NULL;
   struct message* msg = NULL;
   struct configuration* config;

   *ssl = NULL;
   *fd = -1;

   auth_type = SECURITY_INVALID;
   server_fd = -1;
   config = (struct configuration*)shmem;

   for (int i = 0; i < NUMBER_OF_SECURITY_MESSAGES; i++)
   {
      memset(&security_messages[i], 0, SECURITY_BUFFER_SIZE);
   }

   if (config->servers[server].host[0] == '/')
   {
      char pgsql[MISC_LENGTH];

      memset(&pgsql, 0, sizeof(pgsql));
      snprintf(&pgsql[0], sizeof(pgsql), ".s.PGSQL.%d", config->servers[server].port);
      ret = pgmoneta_connect_unix_socket(config->servers[server].host, &pgsql[0], &server_fd);
   }
   else
   {
      ret = pgmoneta_connect(config->servers[server].host, config->servers[server].port, &server_fd);
   }

   if (ret != 0)
   {
      goto error;
   }

   ret = pgmoneta_create_ssl_message(&ssl_msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   ret = pgmoneta_write_message(NULL, server_fd, ssl_msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   ret = pgmoneta_read_block_message(NULL, server_fd, &msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   if (msg->kind == 'S')
   {
      SSL_CTX* ctx = NULL;

      if (create_ssl_ctx(true, &ctx))
      {
         goto error;
      }

      pgmoneta_log_trace("%s: Key file @ %s", config->servers[server].name, config->servers[server].tls_key_file);
      pgmoneta_log_trace("%s: Certificate file @ %s", config->servers[server].name, config->servers[server].tls_cert_file);
      pgmoneta_log_trace("%s: CA file @ %s", config->servers[server].name, config->servers[server].tls_ca_file);

      if (create_ssl_client(ctx, config->servers[server].tls_key_file, config->servers[server].tls_cert_file, config->servers[server].tls_ca_file, server_fd, &c_ssl))
      {
         goto error;
      }

      do
      {
         connect = SSL_connect(c_ssl);

         if (connect != 1)
         {
            int err = SSL_get_error(c_ssl, connect);
            switch (err)
            {
               case SSL_ERROR_ZERO_RETURN:
               case SSL_ERROR_WANT_READ:
               case SSL_ERROR_WANT_WRITE:
               case SSL_ERROR_WANT_CONNECT:
               case SSL_ERROR_WANT_ACCEPT:
               case SSL_ERROR_WANT_X509_LOOKUP:
#ifndef HAVE_OPENBSD
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
               case SSL_ERROR_WANT_ASYNC:
               case SSL_ERROR_WANT_ASYNC_JOB:
#if (OPENSSL_VERSION_NUMBER >= 0x10101000L)
               case SSL_ERROR_WANT_CLIENT_HELLO_CB:
#endif
#endif
#endif
                  break;
               case SSL_ERROR_SYSCALL:
                  pgmoneta_log_error("SSL_ERROR_SYSCALL: %s (%d)", strerror(errno), server_fd);
                  errno = 0;
                  goto error;
                  break;
               case SSL_ERROR_SSL:
                  pgmoneta_log_error("SSL_ERROR_SSL: %s (%d)", strerror(errno), server_fd);
                  pgmoneta_log_error("%s", ERR_error_string(err, NULL));
                  pgmoneta_log_error("%s", ERR_lib_error_string(err));
                  pgmoneta_log_error("%s", ERR_reason_error_string(err));
                  errno = 0;
                  goto error;
                  break;
            }
            ERR_clear_error();
         }
      }
      while (connect != 1);
   }

   ret = pgmoneta_create_startup_message(username, database, replication, &startup_msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   ret = pgmoneta_write_message(c_ssl, server_fd, startup_msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      pgmoneta_log_info("pgmoneta_create_startup_message: %d", ret);
      goto error;
   }

   ret = pgmoneta_read_block_message(c_ssl, server_fd, &msg);
   if (ret != MESSAGE_STATUS_OK)
   {
      pgmoneta_log_info("pgmoneta_read_block_message (STARTUP): %d", ret);
      goto error;
   }

   get_auth_type(msg, &auth_type);

   if (auth_type == -1)
   {
      goto error;
   }
   else if (auth_type != SECURITY_TRUST && auth_type != SECURITY_PASSWORD && auth_type != SECURITY_MD5 && auth_type != SECURITY_SCRAM256)
   {
      goto error;
   }

   security_lengths[0] = msg->length;
   memcpy(&security_messages[0], msg->data, msg->length);

   if (auth_type == SECURITY_TRUST)
   {
      status = server_trust();
   }
   else if (auth_type == SECURITY_PASSWORD)
   {
      status = server_password(username, password, c_ssl, server_fd);
   }
   else if (auth_type == SECURITY_MD5)
   {
      status = server_md5(username, password, c_ssl, server_fd);
   }
   else if (auth_type == SECURITY_SCRAM256)
   {
      status = server_scram256(username, password, c_ssl, server_fd);
   }

   if (status == AUTH_BAD_PASSWORD)
   {
      goto bad_password;
   }
   else if (status == AUTH_ERROR)
   {
      goto error;
   }

   *fd = server_fd;
   *ssl = c_ssl;

   pgmoneta_free_copy_message(ssl_msg);
   pgmoneta_free_copy_message(startup_msg);
   pgmoneta_free_message(msg);

   return AUTH_SUCCESS;

bad_password:

   pgmoneta_free_copy_message(ssl_msg);
   pgmoneta_free_copy_message(startup_msg);
   pgmoneta_free_message(msg);

   pgmoneta_close_ssl(c_ssl);
   if (server_fd != -1)
   {
      pgmoneta_disconnect(server_fd);
   }

   return AUTH_BAD_PASSWORD;

error:

   pgmoneta_free_copy_message(ssl_msg);
   pgmoneta_free_copy_message(startup_msg);
   pgmoneta_free_message(msg);

   pgmoneta_close_ssl(c_ssl);
   if (server_fd != -1)
   {
      pgmoneta_disconnect(server_fd);
   }

   return AUTH_ERROR;
}

static int
server_trust(void)
{
   pgmoneta_log_trace("server_trust");

   has_security = SECURITY_TRUST;

   return AUTH_SUCCESS;
}

static int
server_password(char* username, char* password, SSL* ssl, int server_fd)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_index = 1;
   int auth_response = -1;
   struct message* auth_msg = NULL;
   struct message* password_msg = NULL;

   pgmoneta_log_trace("server_password");

   status = pgmoneta_create_auth_password_response(password, &password_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_write_message(ssl, server_fd, password_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   security_lengths[auth_index] = password_msg->length;
   memcpy(&security_messages[auth_index], password_msg->data, password_msg->length);
   auth_index++;

   status = pgmoneta_read_block_message(ssl, server_fd, &auth_msg);
   if (auth_msg->length > SECURITY_BUFFER_SIZE)
   {
      pgmoneta_log_message(auth_msg);
      pgmoneta_log_error("Security message too large: %ld", auth_msg->length);
      goto error;
   }

   get_auth_type(auth_msg, &auth_response);
   pgmoneta_log_trace("authenticate: auth response %d", auth_response);

   if (auth_response == 0)
   {
      if (auth_msg->length > SECURITY_BUFFER_SIZE)
      {
         pgmoneta_log_message(auth_msg);
         pgmoneta_log_error("Security message too large: %ld", auth_msg->length);
         goto error;
      }

      security_lengths[auth_index] = auth_msg->length;
      memcpy(&security_messages[auth_index], auth_msg->data, auth_msg->length);

      has_security = SECURITY_PASSWORD;
   }
   else
   {
      goto bad_password;
   }

   pgmoneta_free_copy_message(password_msg);
   pgmoneta_free_message(auth_msg);

   return AUTH_SUCCESS;

bad_password:

   pgmoneta_log_warn("Wrong password for user: %s", username);

   pgmoneta_free_copy_message(password_msg);
   pgmoneta_free_message(auth_msg);

   return AUTH_BAD_PASSWORD;

error:

   pgmoneta_free_copy_message(password_msg);
   pgmoneta_free_message(auth_msg);

   return AUTH_ERROR;
}

static int
server_md5(char* username, char* password, SSL* ssl, int server_fd)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_index = 1;
   int auth_response = -1;
   size_t size;
   char* pwdusr = NULL;
   char* shadow = NULL;
   char* md5_req = NULL;
   char* md5 = NULL;
   char md5str[36];
   char* salt = NULL;
   struct message* auth_msg = NULL;
   struct message* md5_msg = NULL;

   pgmoneta_log_trace("server_md5");

   if (get_salt(security_messages[0], &salt))
   {
      goto error;
   }

   size = strlen(username) + strlen(password) + 1;
   pwdusr = malloc(size);
   memset(pwdusr, 0, size);

   snprintf(pwdusr, size, "%s%s", password, username);

   if (generate_md5(pwdusr, strlen(pwdusr), &shadow))
   {
      goto error;
   }

   md5_req = malloc(36);
   memset(md5_req, 0, 36);
   memcpy(md5_req, shadow, 32);
   memcpy(md5_req + 32, salt, 4);

   if (generate_md5(md5_req, 36, &md5))
   {
      goto error;
   }

   memset(&md5str, 0, sizeof(md5str));
   snprintf(&md5str[0], 36, "md5%s", md5);

   status = pgmoneta_create_auth_md5_response(md5str, &md5_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_write_message(ssl, server_fd, md5_msg);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   security_lengths[auth_index] = md5_msg->length;
   memcpy(&security_messages[auth_index], md5_msg->data, md5_msg->length);
   auth_index++;

   status = pgmoneta_read_block_message(ssl, server_fd, &auth_msg);
   if (auth_msg->length > SECURITY_BUFFER_SIZE)
   {
      pgmoneta_log_message(auth_msg);
      pgmoneta_log_error("Security message too large: %ld", auth_msg->length);
      goto error;
   }

   get_auth_type(auth_msg, &auth_response);
   pgmoneta_log_trace("authenticate: auth response %d", auth_response);

   if (auth_response == 0)
   {
      if (auth_msg->length > SECURITY_BUFFER_SIZE)
      {
         pgmoneta_log_message(auth_msg);
         pgmoneta_log_error("Security message too large: %ld", auth_msg->length);
         goto error;
      }

      security_lengths[auth_index] = auth_msg->length;
      memcpy(&security_messages[auth_index], auth_msg->data, auth_msg->length);

      has_security = SECURITY_MD5;
   }
   else
   {
      goto bad_password;
   }

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgmoneta_free_copy_message(md5_msg);
   pgmoneta_free_message(auth_msg);

   return AUTH_SUCCESS;

bad_password:

   pgmoneta_log_warn("Wrong password for user: %s", username);

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgmoneta_free_copy_message(md5_msg);
   pgmoneta_free_message(auth_msg);

   return AUTH_BAD_PASSWORD;

error:

   free(pwdusr);
   free(shadow);
   free(md5_req);
   free(md5);
   free(salt);

   pgmoneta_free_copy_message(md5_msg);
   pgmoneta_free_message(auth_msg);

   return AUTH_ERROR;
}

static int
server_scram256(char* username, char* password, SSL* ssl, int server_fd)
{
   int status = MESSAGE_STATUS_ERROR;
   int auth_index = 1;
   char* salt = NULL;
   int salt_length = 0;
   char* password_prep = NULL;
   char* client_nounce = NULL;
   char* combined_nounce = NULL;
   char* base64_salt = NULL;
   char* iteration_string = NULL;
   char* err = NULL;
   int iteration;
   char* client_first_message_bare = NULL;
   char* server_first_message = NULL;
   char wo_proof[58];
   unsigned char* proof = NULL;
   int proof_length;
   char* proof_base = NULL;
   char* base64_server_signature = NULL;
   char* server_signature_received = NULL;
   int server_signature_received_length;
   unsigned char* server_signature_calc = NULL;
   int server_signature_calc_length;
   struct message* sasl_response = NULL;
   struct message* sasl_continue = NULL;
   struct message* sasl_continue_response = NULL;
   struct message* sasl_final = NULL;
   struct message* msg = NULL;

   pgmoneta_log_trace("server_scram256");

   status = sasl_prep(password, &password_prep);
   if (status)
   {
      goto error;
   }

   generate_nounce(&client_nounce);

   status = pgmoneta_create_auth_scram256_response(client_nounce, &sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   security_lengths[auth_index] = sasl_response->length;
   memcpy(&security_messages[auth_index], sasl_response->data, sasl_response->length);
   auth_index++;

   status = pgmoneta_write_message(ssl, server_fd, sasl_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_read_block_message(ssl, server_fd, &msg);
   if (msg->length > SECURITY_BUFFER_SIZE)
   {
      pgmoneta_log_message(msg);
      pgmoneta_log_error("Security message too large: %ld", msg->length);
      goto error;
   }

   sasl_continue = pgmoneta_copy_message(msg);

   security_lengths[auth_index] = sasl_continue->length;
   memcpy(&security_messages[auth_index], sasl_continue->data, sasl_continue->length);
   auth_index++;

   get_scram_attribute('r', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &combined_nounce);
   get_scram_attribute('s', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &base64_salt);
   get_scram_attribute('i', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &iteration_string);
   get_scram_attribute('e', (char*)(sasl_continue->data + 9), sasl_continue->length - 9, &err);

   if (err != NULL)
   {
      pgmoneta_log_error("SCRAM-SHA-256: %s", err);
      goto error;
   }

   pgmoneta_base64_decode(base64_salt, strlen(base64_salt), &salt, &salt_length);

   iteration = atoi(iteration_string);

   memset(&wo_proof[0], 0, sizeof(wo_proof));
   snprintf(&wo_proof[0], sizeof(wo_proof), "c=biws,r=%s", combined_nounce);

   /* n=,r=... */
   client_first_message_bare = security_messages[1] + 26;

   /* r=...,s=...,i=4096 */
   server_first_message = security_messages[2] + 9;

   if (client_proof(password_prep, salt, salt_length, iteration,
                    client_first_message_bare, security_lengths[1] - 26,
                    server_first_message, security_lengths[2] - 9,
                    &wo_proof[0], strlen(wo_proof),
                    &proof, &proof_length))
   {
      goto error;
   }

   pgmoneta_base64_encode((char*)proof, proof_length, &proof_base);

   status = pgmoneta_create_auth_scram256_continue_response(&wo_proof[0], (char*)proof_base, &sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   security_lengths[auth_index] = sasl_continue_response->length;
   memcpy(&security_messages[auth_index], sasl_continue_response->data, sasl_continue_response->length);
   auth_index++;

   status = pgmoneta_write_message(ssl, server_fd, sasl_continue_response);
   if (status != MESSAGE_STATUS_OK)
   {
      goto error;
   }

   status = pgmoneta_read_block_message(ssl, server_fd, &msg);
   if (msg->length > SECURITY_BUFFER_SIZE)
   {
      pgmoneta_log_message(msg);
      pgmoneta_log_error("Security message too large: %ld", msg->length);
      goto error;
   }

   security_lengths[auth_index] = msg->length;
   memcpy(&security_messages[auth_index], msg->data, msg->length);
   auth_index++;

   if (pgmoneta_extract_message('R', msg, &sasl_final))
   {
      goto error;
   }

   /* Get 'v' attribute */
   base64_server_signature = sasl_final->data + 11;
   pgmoneta_base64_decode(base64_server_signature, sasl_final->length - 11,
                          &server_signature_received, &server_signature_received_length);

   if (server_signature(password_prep, salt, salt_length, iteration,
                        NULL, 0,
                        client_first_message_bare, security_lengths[1] - 26,
                        server_first_message, security_lengths[2] - 9,
                        &wo_proof[0], strlen(wo_proof),
                        &server_signature_calc, &server_signature_calc_length))
   {
      goto error;
   }

   if (server_signature_calc_length != server_signature_received_length ||
       memcmp(server_signature_received, server_signature_calc, server_signature_calc_length) != 0)
   {
      goto bad_password;
   }

   has_security = SECURITY_SCRAM256;

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgmoneta_free_copy_message(sasl_response);
   pgmoneta_free_copy_message(sasl_continue);
   pgmoneta_free_copy_message(sasl_continue_response);
   pgmoneta_free_copy_message(sasl_final);

   return AUTH_SUCCESS;

bad_password:

   pgmoneta_log_warn("Wrong password for user: %s", username);

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgmoneta_free_copy_message(sasl_response);
   pgmoneta_free_copy_message(sasl_continue);
   pgmoneta_free_copy_message(sasl_continue_response);
   pgmoneta_free_copy_message(sasl_final);

   return AUTH_BAD_PASSWORD;

error:

   free(salt);
   free(err);
   free(password_prep);
   free(client_nounce);
   free(combined_nounce);
   free(base64_salt);
   free(iteration_string);
   free(proof);
   free(proof_base);
   free(server_signature_received);
   free(server_signature_calc);

   pgmoneta_free_copy_message(sasl_response);
   pgmoneta_free_copy_message(sasl_continue);
   pgmoneta_free_copy_message(sasl_continue_response);
   pgmoneta_free_copy_message(sasl_final);

   return AUTH_ERROR;
}

static char*
get_admin_password(char* username)
{
   struct configuration* config;

   config = (struct configuration*)shmem;

   for (int i = 0; i < config->number_of_admins; i++)
   {
      if (!strcmp(&config->admins[i].username[0], username))
      {
         return &config->admins[i].password[0];
      }
   }

   return NULL;
}

int
pgmoneta_get_master_key(char** masterkey)
{
   FILE* master_key_file = NULL;
   char buf[MISC_LENGTH];
   char line[MISC_LENGTH];
   char* mk = NULL;
   int mk_length = 0;
   struct stat st = {0};

   if (pgmoneta_get_home_directory() == NULL)
   {
      goto error;
   }

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s/.pgmoneta", pgmoneta_get_home_directory());

   if (stat(&buf[0], &st) == -1)
   {
      goto error;
   }
   else
   {
      if (S_ISDIR(st.st_mode) && st.st_mode & S_IRWXU && !(st.st_mode & S_IRWXG) && !(st.st_mode & S_IRWXO))
      {
         /* Ok */
      }
      else
      {
         goto error;
      }
   }

   memset(&buf, 0, sizeof(buf));
   snprintf(&buf[0], sizeof(buf), "%s/.pgmoneta/master.key", pgmoneta_get_home_directory());

   if (stat(&buf[0], &st) == -1)
   {
      goto error;
   }
   else
   {
      if (S_ISREG(st.st_mode) && st.st_mode & (S_IRUSR | S_IWUSR) && !(st.st_mode & S_IRWXG) && !(st.st_mode & S_IRWXO))
      {
         /* Ok */
      }
      else
      {
         goto error;
      }
   }

   master_key_file = pgmoneta_open_file(&buf[0], "r");
   if (master_key_file == NULL)
   {
      goto error;
   }

   memset(&line, 0, sizeof(line));
   if (fgets(line, sizeof(line), master_key_file) == NULL)
   {
      goto error;
   }

   pgmoneta_base64_decode(&line[0], strlen(&line[0]), &mk, &mk_length);

   *masterkey = mk;

   fclose(master_key_file);

   return 0;

error:

   free(mk);

   if (master_key_file)
   {
      fclose(master_key_file);
   }

   return 1;
}

int
pgmoneta_tls_valid(void)
{
   struct configuration* config;
   struct stat st = {0};

   config = (struct configuration*)shmem;

   if (config->tls)
   {
      if (strlen(config->tls_cert_file) == 0)
      {
         pgmoneta_log_error("No TLS certificate defined");
         goto error;
      }

      if (strlen(config->tls_key_file) == 0)
      {
         pgmoneta_log_error("No TLS private key defined");
         goto error;
      }

      if (stat(config->tls_cert_file, &st) == -1)
      {
         pgmoneta_log_error("Can't locate TLS certificate file: %s", config->tls_cert_file);
         goto error;
      }

      if (!S_ISREG(st.st_mode))
      {
         pgmoneta_log_error("TLS certificate file is not a regular file: %s", config->tls_cert_file);
         goto error;
      }

      if (st.st_uid && st.st_uid != geteuid())
      {
         pgmoneta_log_error("TLS certificate file not owned by user or root: %s", config->tls_cert_file);
         goto error;
      }

      memset(&st, 0, sizeof(struct stat));

      if (stat(config->tls_key_file, &st) == -1)
      {
         pgmoneta_log_error("Can't locate TLS private key file: %s", config->tls_key_file);
         goto error;
      }

      if (!S_ISREG(st.st_mode))
      {
         pgmoneta_log_error("TLS private key file is not a regular file: %s", config->tls_key_file);
         goto error;
      }

      if (st.st_uid == geteuid())
      {
         if (st.st_mode & (S_IRWXG | S_IRWXO))
         {
            pgmoneta_log_error("TLS private key file must have 0600 permissions when owned by a non-root user: %s", config->tls_key_file);
            goto error;
         }
      }
      else if (st.st_uid == 0)
      {
         if (st.st_mode & (S_IWGRP | S_IXGRP | S_IRWXO))
         {
            pgmoneta_log_error("TLS private key file must have at least 0640 permissions when owned by root: %s", config->tls_key_file);
            goto error;
         }

      }
      else
      {
         pgmoneta_log_error("TLS private key file not owned by user or root: %s", config->tls_key_file);
         goto error;
      }

      if (strlen(config->tls_ca_file) > 0)
      {
         memset(&st, 0, sizeof(struct stat));

         if (stat(config->tls_ca_file, &st) == -1)
         {
            pgmoneta_log_error("Can't locate TLS CA file: %s", config->tls_ca_file);
            goto error;
         }

         if (!S_ISREG(st.st_mode))
         {
            pgmoneta_log_error("TLS CA file is not a regular file: %s", config->tls_ca_file);
            goto error;
         }

         if (st.st_uid && st.st_uid != geteuid())
         {
            pgmoneta_log_error("TLS CA file not owned by user or root: %s", config->tls_ca_file);
            goto error;
         }
      }
      else
      {
         pgmoneta_log_debug("No TLS CA file");
      }
   }

   return 0;

error:

   return 1;
}

static int
sasl_prep(char* password, char** password_prep)
{
   char* p = NULL;

   /* Only support ASCII for now */
   for (int i = 0; i < strlen(password); i++)
   {
      if ((unsigned char)(*(password + i)) & 0x80)
      {
         goto error;
      }
   }

   p = strdup(password);

   *password_prep = p;

   return 0;

error:

   *password_prep = NULL;

   return 1;
}

static int
generate_nounce(char** nounce)
{
   size_t s = 18;
   unsigned char r[s + 1];
   char* base = NULL;
   int result;

   memset(&r[0], 0, sizeof(r));

   result = RAND_bytes(r, sizeof(r));
   if (result != 1)
   {
      goto error;
   }

   r[s] = '\0';

   pgmoneta_base64_encode((char*)&r[0], s, &base);

   *nounce = base;

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   return 0;

error:

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   return 1;
}

static int
get_scram_attribute(char attribute, char* input, size_t size, char** value)
{
   char* dup = NULL;
   char* result = NULL;
   char* ptr = NULL;
   size_t token_size;
   char match[2];

   match[0] = attribute;
   match[1] = '=';

   dup = (char*)malloc(size + 1);
   memset(dup, 0, size + 1);
   memcpy(dup, input, size);

   ptr = strtok(dup, ",");
   while (ptr != NULL)
   {
      if (!strncmp(ptr, &match[0], 2))
      {
         token_size = strlen(ptr) - 1;
         result = malloc(token_size);
         memset(result, 0, token_size);
         memcpy(result, ptr + 2, token_size);
         goto done;
      }

      ptr = strtok(NULL, ",");
   }

   if (result == NULL)
   {
      goto error;
   }

done:

   *value = result;

   free(dup);

   return 0;

error:

   *value = NULL;

   free(dup);

   return 1;
}

static int
client_proof(char* password, char* salt, int salt_length, int iterations,
             char* client_first_message_bare, size_t client_first_message_bare_length,
             char* server_first_message, size_t server_first_message_length,
             char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
             unsigned char** result, int* result_length)
{
   size_t size = 32;
   unsigned char* s_p = NULL;
   int s_p_length;
   unsigned char* c_k = NULL;
   int c_k_length;
   unsigned char* s_k = NULL;
   int s_k_length;
   unsigned char* c_s = NULL;
   unsigned int length;
   unsigned char* r = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX* ctx = HMAC_CTX_new();
#else
   HMAC_CTX hctx;
   HMAC_CTX* ctx = &hctx;

   HMAC_CTX_init(ctx);
#endif

   if (salted_password(password, salt, salt_length, iterations, &s_p, &s_p_length))
   {
      goto error;
   }

   if (salted_password_key(s_p, s_p_length, "Client Key", &c_k, &c_k_length))
   {
      goto error;
   }

   if (stored_key(c_k, c_k_length, &s_k, &s_k_length))
   {
      goto error;
   }

   c_s = malloc(size);
   memset(c_s, 0, size);

   r = malloc(size);
   memset(r, 0, size);

   /* Client signature: HMAC(StoredKey, AuthMessage) */
   if (HMAC_Init_ex(ctx, s_k, s_k_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_first_message_bare, client_first_message_bare_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)server_first_message, server_first_message_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_final_message_wo_proof, client_final_message_wo_proof_length) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, c_s, &length) != 1)
   {
      goto error;
   }

   /* ClientProof: ClientKey XOR ClientSignature */
   for (int i = 0; i < size; i++)
   {
      *(r + i) = *(c_k + i) ^ *(c_s + i);
   }

   *result = r;
   *result_length = size;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX_free(ctx);
#else
   HMAC_CTX_cleanup(ctx);
#endif

   free(s_p);
   free(c_k);
   free(s_k);
   free(c_s);

   return 0;

error:

   *result = NULL;
   *result_length = 0;

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      HMAC_CTX_free(ctx);
#else
      HMAC_CTX_cleanup(ctx);
#endif
   }

   free(s_p);
   free(c_k);
   free(s_k);
   free(c_s);

   return 1;
}

static int
salted_password(char* password, char* salt, int salt_length, int iterations, unsigned char** result, int* result_length)
{
   size_t size = 32;
   int password_length;
   unsigned int one;
   unsigned char Ui[size];
   unsigned char Ui_prev[size];
   unsigned int Ui_length;
   unsigned char* r = NULL;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX* ctx = HMAC_CTX_new();
#else
   HMAC_CTX hctx;
   HMAC_CTX* ctx = &hctx;

   HMAC_CTX_init(ctx);
#endif

   if (ctx == NULL)
   {
      goto error;
   }

   password_length = strlen(password);

   if (!pgmoneta_bigendian())
   {
      one = pgmoneta_swap(1);
   }
   else
   {
      one = 1;
   }

   r = malloc(size);
   memset(r, 0, size);

   /* SaltedPassword: Hi(Normalize(password), salt, iterations) */
   if (HMAC_Init_ex(ctx, password, password_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)salt, salt_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)&one, sizeof(one)) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, &Ui_prev[0], &Ui_length) != 1)
   {
      goto error;
   }
   memcpy(r, &Ui_prev[0], size);

   for (int i = 2; i <= iterations; i++)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      if (HMAC_CTX_reset(ctx) != 1)
      {
         goto error;
      }
#endif

      if (HMAC_Init_ex(ctx, password, password_length, EVP_sha256(), NULL) != 1)
      {
         goto error;
      }

      if (HMAC_Update(ctx, &Ui_prev[0], size) != 1)
      {
         goto error;
      }

      if (HMAC_Final(ctx, &Ui[0], &Ui_length) != 1)
      {
         goto error;
      }

      for (int j = 0; j < size; j++)
      {
         *(r + j) ^= *(Ui + j);
      }
      memcpy(&Ui_prev[0], &Ui[0], size);
   }

   *result = r;
   *result_length = size;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX_free(ctx);
#else
   HMAC_CTX_cleanup(ctx);
#endif

   return 0;

error:

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      HMAC_CTX_free(ctx);
#else
      HMAC_CTX_cleanup(ctx);
#endif
   }

   *result = NULL;
   *result_length = 0;

   return 1;
}

static int
salted_password_key(unsigned char* salted_password, int salted_password_length, char* key, unsigned char** result, int* result_length)
{
   size_t size = 32;
   unsigned char* r = NULL;
   unsigned int length;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX* ctx = HMAC_CTX_new();
#else
   HMAC_CTX hctx;
   HMAC_CTX* ctx = &hctx;

   HMAC_CTX_init(ctx);
#endif

   if (ctx == NULL)
   {
      goto error;
   }

   r = malloc(size);
   memset(r, 0, size);

   /* HMAC(SaltedPassword, Key) */
   if (HMAC_Init_ex(ctx, salted_password, salted_password_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)key, strlen(key)) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, r, &length) != 1)
   {
      goto error;
   }

   *result = r;
   *result_length = size;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX_free(ctx);
#else
   HMAC_CTX_cleanup(ctx);
#endif

   return 0;

error:

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      HMAC_CTX_free(ctx);
#else
      HMAC_CTX_cleanup(ctx);
#endif
   }

   *result = NULL;
   *result_length = 0;

   return 1;
}

static int
stored_key(unsigned char* client_key, int client_key_length, unsigned char** result, int* result_length)
{
   size_t size = 32;
   unsigned char* r = NULL;
   unsigned int length;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   EVP_MD_CTX* ctx = EVP_MD_CTX_new();
#else
   EVP_MD_CTX* ctx = EVP_MD_CTX_create();

   EVP_MD_CTX_init(ctx);
#endif

   if (ctx == NULL)
   {
      goto error;
   }

   r = malloc(size);
   memset(r, 0, size);

   /* StoredKey: H(ClientKey) */
   if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (EVP_DigestUpdate(ctx, client_key, client_key_length) != 1)
   {
      goto error;
   }

   if (EVP_DigestFinal_ex(ctx, r, &length) != 1)
   {
      goto error;
   }

   *result = r;
   *result_length = size;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   EVP_MD_CTX_free(ctx);
#else
   EVP_MD_CTX_destroy(ctx);
#endif

   return 0;

error:

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      EVP_MD_CTX_free(ctx);
#else
      EVP_MD_CTX_destroy(ctx);
#endif
   }

   *result = NULL;
   *result_length = 0;

   return 1;
}

static int
generate_salt(char** salt, int* size)
{
   size_t s = 16;
   unsigned char* r = NULL;
   int result;

   r = malloc(s);
   memset(r, 0, s);

   result = RAND_bytes(r, s);
   if (result != 1)
   {
      goto error;
   }

   *salt = (char*)r;
   *size = s;

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   return 0;

error:

#if OPENSSL_API_COMPAT < 0x10100000L
   RAND_cleanup();
#endif

   free(r);

   *salt = NULL;
   *size = 0;

   return 1;
}

static int
server_signature(char* password, char* salt, int salt_length, int iterations,
                 char* s_key, int s_key_length,
                 char* client_first_message_bare, size_t client_first_message_bare_length,
                 char* server_first_message, size_t server_first_message_length,
                 char* client_final_message_wo_proof, size_t client_final_message_wo_proof_length,
                 unsigned char** result, int* result_length)
{
   size_t size = 32;
   unsigned char* r = NULL;
   unsigned char* s_p = NULL;
   int s_p_length;
   unsigned char* s_k = NULL;
   int s_k_length;
   unsigned int length;
   bool do_free = true;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX* ctx = HMAC_CTX_new();
#else
   HMAC_CTX hctx;
   HMAC_CTX* ctx = &hctx;

   HMAC_CTX_init(ctx);
#endif

   if (ctx == NULL)
   {
      goto error;
   }

   r = malloc(size);
   memset(r, 0, size);

   if (password != NULL)
   {
      if (salted_password(password, salt, salt_length, iterations, &s_p, &s_p_length))
      {
         goto error;
      }

      if (salted_password_key(s_p, s_p_length, "Server Key", &s_k, &s_k_length))
      {
         goto error;
      }
   }
   else
   {
      do_free = false;
      s_k = (unsigned char*)s_key;
      s_k_length = s_key_length;
   }

   /* Server signature: HMAC(ServerKey, AuthMessage) */
   if (HMAC_Init_ex(ctx, s_k, s_k_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_first_message_bare, client_first_message_bare_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)server_first_message, server_first_message_length) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)",", 1) != 1)
   {
      goto error;
   }

   if (HMAC_Update(ctx, (unsigned char*)client_final_message_wo_proof, client_final_message_wo_proof_length) != 1)
   {
      goto error;
   }

   if (HMAC_Final(ctx, r, &length) != 1)
   {
      goto error;
   }

   *result = r;
   *result_length = length;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX_free(ctx);
#else
   HMAC_CTX_cleanup(ctx);
#endif

   free(s_p);
   if (do_free)
   {
      free(s_k);
   }

   return 0;

error:

   *result = NULL;
   *result_length = 0;

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      HMAC_CTX_free(ctx);
#else
      HMAC_CTX_cleanup(ctx);
#endif
   }

   free(s_p);
   if (do_free)
   {
      free(s_k);
   }

   return 1;
}

static int
create_ssl_ctx(bool client, SSL_CTX** ctx)
{
   SSL_CTX* c = NULL;

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   OpenSSL_add_all_algorithms();
#endif

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   if (client)
   {
      c = SSL_CTX_new(TLSv1_2_client_method());
   }
   else
   {
      c = SSL_CTX_new(TLSv1_2_server_method());
   }
#else
   if (client)
   {
      c = SSL_CTX_new(TLS_client_method());
   }
   else
   {
      c = SSL_CTX_new(TLS_server_method());
   }
#endif

   if (c == NULL)
   {
      goto error;
   }

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   SSL_CTX_set_options(c, SSL_OP_NO_SSLv3);
   SSL_CTX_set_options(c, SSL_OP_NO_TLSv1);
   SSL_CTX_set_options(c, SSL_OP_NO_TLSv1_1);
#else
   if (SSL_CTX_set_min_proto_version(c, TLS1_2_VERSION) == 0)
   {
      goto error;
   }
#endif

   SSL_CTX_set_mode(c, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
   SSL_CTX_set_options(c, SSL_OP_NO_TICKET);
   SSL_CTX_set_session_cache_mode(c, SSL_SESS_CACHE_OFF);

   *ctx = c;

   return 0;

error:

   if (c != NULL)
   {
      SSL_CTX_free(c);
   }

   return 1;
}

static int
create_ssl_client(SSL_CTX* ctx, char* key, char* cert, char* root, int socket, SSL** ssl)
{
   SSL* s = NULL;
   bool have_cert = false;
   bool have_rootcert = false;

   if (root != NULL && strlen(root) > 0)
   {
      if (SSL_CTX_load_verify_locations(ctx, root, NULL) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgmoneta_log_error("Couldn't load TLS CA: %s", root);
         pgmoneta_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      have_rootcert = true;
   }

   if (cert != NULL && strlen(cert) > 0)
   {
      if (SSL_CTX_use_certificate_chain_file(ctx, cert) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgmoneta_log_error("Couldn't load TLS certificate: %s", cert);
         pgmoneta_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      have_cert = true;
   }

   s = SSL_new(ctx);

   if (s == NULL)
   {
      goto error;
   }

   if (SSL_set_fd(s, socket) == 0)
   {
      goto error;
   }

   if (have_cert && key != NULL && strlen(key) > 0)
   {
      if (SSL_use_PrivateKey_file(s, key, SSL_FILETYPE_PEM) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgmoneta_log_error("Couldn't load TLS private key: %s", key);
         pgmoneta_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      if (SSL_check_private_key(s) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgmoneta_log_error("TLS private key check failed: %s", key);
         pgmoneta_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }
   }

   if (have_rootcert)
   {
      SSL_set_verify(s, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, NULL);
   }

   *ssl = s;

   return 0;

error:

   pgmoneta_close_ssl(s);

   return 1;
}

static int
create_ssl_server(SSL_CTX* ctx, int socket, SSL** ssl)
{
   SSL* s = NULL;
   STACK_OF(X509_NAME) * root_cert_list = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;

   if (strlen(config->tls_cert_file) == 0)
   {
      pgmoneta_log_error("No TLS certificate defined");
      goto error;
   }

   if (strlen(config->tls_key_file) == 0)
   {
      pgmoneta_log_error("No TLS private key defined");
      goto error;
   }

   if (SSL_CTX_use_certificate_chain_file(ctx, config->tls_cert_file) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgmoneta_log_error("Couldn't load TLS certificate: %s", config->tls_cert_file);
      pgmoneta_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (SSL_CTX_use_PrivateKey_file(ctx, config->tls_key_file, SSL_FILETYPE_PEM) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgmoneta_log_error("Couldn't load TLS private key: %s", config->tls_key_file);
      pgmoneta_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (SSL_CTX_check_private_key(ctx) != 1)
   {
      unsigned long err;

      err = ERR_get_error();
      pgmoneta_log_error("TLS private key check failed: %s", config->tls_key_file);
      pgmoneta_log_error("Reason: %s", ERR_reason_error_string(err));
      goto error;
   }

   if (strlen(config->tls_ca_file) > 0)
   {
      if (SSL_CTX_load_verify_locations(ctx, config->tls_ca_file, NULL) != 1)
      {
         unsigned long err;

         err = ERR_get_error();
         pgmoneta_log_error("Couldn't load TLS CA: %s", config->tls_ca_file);
         pgmoneta_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      root_cert_list = SSL_load_client_CA_file(config->tls_ca_file);
      if (root_cert_list == NULL)
      {
         unsigned long err;

         err = ERR_get_error();
         pgmoneta_log_error("Couldn't load TLS CA: %s", config->tls_ca_file);
         pgmoneta_log_error("Reason: %s", ERR_reason_error_string(err));
         goto error;
      }

      SSL_CTX_set_verify(ctx, (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT | SSL_VERIFY_CLIENT_ONCE), NULL);
      SSL_CTX_set_client_CA_list(ctx, root_cert_list);
   }

   s = SSL_new(ctx);

   if (s == NULL)
   {
      goto error;
   }

   if (SSL_set_fd(s, socket) == 0)
   {
      goto error;
   }

   *ssl = s;

   return 0;

error:

   pgmoneta_close_ssl(s);

   return 1;
}

static int
create_hash_file(char* filename, const char* algorithm, char** hash)
{
   EVP_MD_CTX* md_ctx;
   const EVP_MD* md;
   unsigned char md_value[EVP_MAX_MD_SIZE];
   unsigned int md_len;
   FILE* file = NULL;
   char read_buf[16384];
   unsigned long read_bytes = 0;
   int i = 0;
   char* hash_buf;
   unsigned int hash_len;

   md = EVP_get_digestbyname(algorithm);
   if (md == NULL)
   {
      pgmoneta_log_error("Invalid message digest: %s", algorithm);
      return 1;
   }

   if (strcmp("SHA224", algorithm) == 0)
   {
      hash_len = 57;
   }
   else if (strcmp("SHA256", algorithm) == 0)
   {
      hash_len = 65;
   }
   else if (strcmp("SHA384", algorithm) == 0)
   {
      hash_len = 97;
   }
   else
   {
      hash_len = 129;
   }

   hash_buf = malloc(hash_len);

   memset(hash_buf, 0, hash_len);

   md_ctx = EVP_MD_CTX_new();

   if (!EVP_DigestInit_ex2(md_ctx, md, NULL))
   {
      pgmoneta_log_error("Message digest initialization failed");
      EVP_MD_CTX_free(md_ctx);
      return 1;
   }

   file = pgmoneta_open_file(filename, "rb");
   if (file == NULL)
   {
      return 1;
   }

   memset(read_buf, 0, sizeof(read_buf));

   while ((read_bytes = fread(read_buf, 1, sizeof(read_buf), file)) > 0)
   {
      if (!EVP_DigestUpdate(md_ctx, read_buf, read_bytes))
      {
         pgmoneta_log_error("Message digest update failed");
         EVP_MD_CTX_free(md_ctx);
         return 1;
      }
   }

   if (!EVP_DigestFinal_ex(md_ctx, md_value, &md_len))
   {
      pgmoneta_log_error("Message digest finalization failed");
      EVP_MD_CTX_free(md_ctx);
      return 1;
   }

   EVP_MD_CTX_free(md_ctx);

   for (i = 0; i < md_len; i++)
   {
      sprintf(&hash_buf[i * 2], "%02x", md_value[i]);
   }

   hash_buf[hash_len - 1] = 0;
   *hash = hash_buf;

   fclose(file);

   return 0;
}

int
pgmoneta_create_sha224_file(char* filename, char** sha224)
{
   return create_hash_file(filename, "SHA224", sha224);
}

int
pgmoneta_create_sha256_file(char* filename, char** sha256)
{
   return create_hash_file(filename, "SHA256", sha256);
}

int
pgmoneta_create_sha384_file(char* filename, char** sha384)
{
   return create_hash_file(filename, "SHA384", sha384);
}

int
pgmoneta_create_sha512_file(char* filename, char** sha512)
{
   return create_hash_file(filename, "SHA512", sha512);
}

int
pgmoneta_generate_string_sha256_hash(char* string, char** sha256)
{
   int i = 0;
   SHA256_CTX sha256_ctx;
   unsigned char hash[SHA256_DIGEST_LENGTH];
   char* sha256_buf;

   *sha256 = NULL;

   sha256_buf = malloc(65);
   memset(sha256_buf, 0, 65);

   SHA256_Init(&sha256_ctx);
   SHA256_Update(&sha256_ctx, string, strlen(string));
   SHA256_Final(hash, &sha256_ctx);

   for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
   {
      sprintf(&sha256_buf[i * 2], "%02x", hash[i]);
   }

   sha256_buf[64] = 0;

   *sha256 = sha256_buf;

   return 0;
}

int
pgmoneta_generate_string_hmac_sha256_hash(char* key, int key_length, char* value,
                                          int value_length, unsigned char** hmac,
                                          int* hmac_length)
{
   int i = 0;
   size_t size = 33;
   unsigned char hash[SHA256_DIGEST_LENGTH];
   char* hmac_buf;
   unsigned int hmac_length_buf;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX* ctx = HMAC_CTX_new();
#else
   HMAC_CTX hctx;
   HMAC_CTX* ctx = &hctx;
   HMAC_CTX_init(ctx);
#endif
   if (ctx == NULL)
   {
      goto error;
   }

   *hmac = NULL;

   hmac_buf = malloc(size);
   memset(hmac_buf, 0, size);

   if (HMAC_Init_ex(ctx, key, key_length, EVP_sha256(), NULL) != 1)
   {
      goto error;
   }
   if (HMAC_Update(ctx, (unsigned char*)value, value_length) != 1)
   {
      goto error;
   }
   if (HMAC_Final(ctx, hash, &hmac_length_buf) != 1)
   {
      goto error;
   }

   for (i = 0; i < SHA256_DIGEST_LENGTH; i++)
   {
      hmac_buf[i] = hash[i];
   }
   hmac_buf[size - 1] = 0;

   *hmac = (unsigned char*)hmac_buf;
   *hmac_length = hmac_length_buf;

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
   HMAC_CTX_free(ctx);
#else
   HMAC_CTX_cleanup(ctx);
#endif

   return 0;

error:
   *hmac = NULL;
   *hmac_length = 0;

   if (ctx != NULL)
   {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
      HMAC_CTX_free(ctx);
#else
      HMAC_CTX_cleanup(ctx);
#endif
   }
   return 1;
}

int
pgmoneta_create_crc32c_buffer(void* buffer, size_t size, uint32_t* crc)
{
   if (buffer == NULL)
   {
      return 1;
   }

   #ifdef HAVE_SSE42

   uint64_t crc_long = (uint64_t) ~(*crc);

   for (int i = 0; i < size / 8; i++)
   {
      crc_long = _mm_crc32_u64(crc_long, *((uint64_t*)buffer));
      buffer += 8;
   }
   for (int i = 0; i < (size % 8); i++)
   {
      crc_long = (uint64_t)_mm_crc32_u8((uint32_t)crc_long, *((unsigned char*)buffer));
      buffer++;
   }

   crc_long = ~crc_long;

   *crc = (uint32_t)crc_long;
   return 0;

   #else
   uint32_t crc_int = ~(*crc);

   static const uint32_t crc32_tab[256] = {
      0x00000000L, 0xF26B8303L, 0xE13B70F7L, 0x1350F3F4L,
      0xC79A971FL, 0x35F1141CL, 0x26A1E7E8L, 0xD4CA64EBL,
      0x8AD958CFL, 0x78B2DBCCL, 0x6BE22838L, 0x9989AB3BL,
      0x4D43CFD0L, 0xBF284CD3L, 0xAC78BF27L, 0x5E133C24L,
      0x105EC76FL, 0xE235446CL, 0xF165B798L, 0x030E349BL,
      0xD7C45070L, 0x25AFD373L, 0x36FF2087L, 0xC494A384L,
      0x9A879FA0L, 0x68EC1CA3L, 0x7BBCEF57L, 0x89D76C54L,
      0x5D1D08BFL, 0xAF768BBCL, 0xBC267848L, 0x4E4DFB4BL,
      0x20BD8EDEL, 0xD2D60DDDL, 0xC186FE29L, 0x33ED7D2AL,
      0xE72719C1L, 0x154C9AC2L, 0x061C6936L, 0xF477EA35L,
      0xAA64D611L, 0x580F5512L, 0x4B5FA6E6L, 0xB93425E5L,
      0x6DFE410EL, 0x9F95C20DL, 0x8CC531F9L, 0x7EAEB2FAL,
      0x30E349B1L, 0xC288CAB2L, 0xD1D83946L, 0x23B3BA45L,
      0xF779DEAEL, 0x05125DADL, 0x1642AE59L, 0xE4292D5AL,
      0xBA3A117EL, 0x4851927DL, 0x5B016189L, 0xA96AE28AL,
      0x7DA08661L, 0x8FCB0562L, 0x9C9BF696L, 0x6EF07595L,
      0x417B1DBCL, 0xB3109EBFL, 0xA0406D4BL, 0x522BEE48L,
      0x86E18AA3L, 0x748A09A0L, 0x67DAFA54L, 0x95B17957L,
      0xCBA24573L, 0x39C9C670L, 0x2A993584L, 0xD8F2B687L,
      0x0C38D26CL, 0xFE53516FL, 0xED03A29BL, 0x1F682198L,
      0x5125DAD3L, 0xA34E59D0L, 0xB01EAA24L, 0x42752927L,
      0x96BF4DCCL, 0x64D4CECFL, 0x77843D3BL, 0x85EFBE38L,
      0xDBFC821CL, 0x2997011FL, 0x3AC7F2EBL, 0xC8AC71E8L,
      0x1C661503L, 0xEE0D9600L, 0xFD5D65F4L, 0x0F36E6F7L,
      0x61C69362L, 0x93AD1061L, 0x80FDE395L, 0x72966096L,
      0xA65C047DL, 0x5437877EL, 0x4767748AL, 0xB50CF789L,
      0xEB1FCBADL, 0x197448AEL, 0x0A24BB5AL, 0xF84F3859L,
      0x2C855CB2L, 0xDEEEDFB1L, 0xCDBE2C45L, 0x3FD5AF46L,
      0x7198540DL, 0x83F3D70EL, 0x90A324FAL, 0x62C8A7F9L,
      0xB602C312L, 0x44694011L, 0x5739B3E5L, 0xA55230E6L,
      0xFB410CC2L, 0x092A8FC1L, 0x1A7A7C35L, 0xE811FF36L,
      0x3CDB9BDDL, 0xCEB018DEL, 0xDDE0EB2AL, 0x2F8B6829L,
      0x82F63B78L, 0x709DB87BL, 0x63CD4B8FL, 0x91A6C88CL,
      0x456CAC67L, 0xB7072F64L, 0xA457DC90L, 0x563C5F93L,
      0x082F63B7L, 0xFA44E0B4L, 0xE9141340L, 0x1B7F9043L,
      0xCFB5F4A8L, 0x3DDE77ABL, 0x2E8E845FL, 0xDCE5075CL,
      0x92A8FC17L, 0x60C37F14L, 0x73938CE0L, 0x81F80FE3L,
      0x55326B08L, 0xA759E80BL, 0xB4091BFFL, 0x466298FCL,
      0x1871A4D8L, 0xEA1A27DBL, 0xF94AD42FL, 0x0B21572CL,
      0xDFEB33C7L, 0x2D80B0C4L, 0x3ED04330L, 0xCCBBC033L,
      0xA24BB5A6L, 0x502036A5L, 0x4370C551L, 0xB11B4652L,
      0x65D122B9L, 0x97BAA1BAL, 0x84EA524EL, 0x7681D14DL,
      0x2892ED69L, 0xDAF96E6AL, 0xC9A99D9EL, 0x3BC21E9DL,
      0xEF087A76L, 0x1D63F975L, 0x0E330A81L, 0xFC588982L,
      0xB21572C9L, 0x407EF1CAL, 0x532E023EL, 0xA145813DL,
      0x758FE5D6L, 0x87E466D5L, 0x94B49521L, 0x66DF1622L,
      0x38CC2A06L, 0xCAA7A905L, 0xD9F75AF1L, 0x2B9CD9F2L,
      0xFF56BD19L, 0x0D3D3E1AL, 0x1E6DCDEEL, 0xEC064EEDL,
      0xC38D26C4L, 0x31E6A5C7L, 0x22B65633L, 0xD0DDD530L,
      0x0417B1DBL, 0xF67C32D8L, 0xE52CC12CL, 0x1747422FL,
      0x49547E0BL, 0xBB3FFD08L, 0xA86F0EFCL, 0x5A048DFFL,
      0x8ECEE914L, 0x7CA56A17L, 0x6FF599E3L, 0x9D9E1AE0L,
      0xD3D3E1ABL, 0x21B862A8L, 0x32E8915CL, 0xC083125FL,
      0x144976B4L, 0xE622F5B7L, 0xF5720643L, 0x07198540L,
      0x590AB964L, 0xAB613A67L, 0xB831C993L, 0x4A5A4A90L,
      0x9E902E7BL, 0x6CFBAD78L, 0x7FAB5E8CL, 0x8DC0DD8FL,
      0xE330A81AL, 0x115B2B19L, 0x020BD8EDL, 0xF0605BEEL,
      0x24AA3F05L, 0xD6C1BC06L, 0xC5914FF2L, 0x37FACCF1L,
      0x69E9F0D5L, 0x9B8273D6L, 0x88D28022L, 0x7AB90321L,
      0xAE7367CAL, 0x5C18E4C9L, 0x4F48173DL, 0xBD23943EL,
      0xF36E6F75L, 0x0105EC76L, 0x12551F82L, 0xE03E9C81L,
      0x34F4F86AL, 0xC69F7B69L, 0xD5CF889DL, 0x27A40B9EL,
      0x79B737BAL, 0x8BDCB4B9L, 0x988C474DL, 0x6AE7C44EL,
      0xBE2DA0A5L, 0x4C4623A6L, 0x5F16D052L, 0xAD7D5351L
   };

   for (int i = 0; i < size; i++)
   {
      crc_int = crc32_tab[(((unsigned char)crc_int) ^ ((unsigned char*)buffer)[i])] ^ (crc_int >> 8);
   }
   crc_int = ~crc_int;

   *crc = crc_int;

   return 0;
   #endif
}

int
pgmoneta_create_crc32c_file(char* path, char** crc)
{
   FILE* file = NULL;
   char read_buf[16384];
   unsigned long read_bytes = 0;
   char* crc_string;
   uint32_t crc_buf = 0;

   file = pgmoneta_open_file(path, "rb");

   if (file == NULL)
   {
      goto error;
   }

   memset(read_buf, 0, sizeof(read_buf));

   while ((read_bytes = fread(read_buf, 1, sizeof(read_buf), file)) > 0)
   {
      pgmoneta_create_crc32c_buffer(read_buf, read_bytes, &crc_buf);
   }

   crc_string = malloc(9);

   if (crc_string == NULL)
   {
      goto error;
   }

   memset(crc_string, 0, 9);

   for (int i = 0; i < 8; i++)
   {
      sprintf(crc_string, "%08x", crc_buf);
   }

   *crc = crc_string;

   fclose(file);

   return 0;

error:

   if (file != NULL)
   {
      fclose(file);
   }

   return 1;
}

void
pgmoneta_close_ssl(SSL* ssl)
{
   int res;
   SSL_CTX* ctx;

   if (ssl != NULL)
   {
      ctx = SSL_get_SSL_CTX(ssl);
      res = SSL_shutdown(ssl);
      if (res == 0)
      {
         SSL_shutdown(ssl);
      }
      SSL_free(ssl);
      SSL_CTX_free(ctx);
   }
}
