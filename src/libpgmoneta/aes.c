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
#include <aes.h>
#include <logging.h>
#include <management.h>
#include <progress.h>
#include <security.h>
#include <utils.h>
#include <workers.h>

/* System */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define NAME         "aes"
#define ENC_BUF_SIZE (1024 * 1024)

static _Thread_local unsigned char master_key_cache[EVP_MAX_KEY_LENGTH];
static _Thread_local unsigned char cached_password_hash[EVP_MAX_MD_SIZE];
static _Thread_local bool password_hash_cached = false;
static _Thread_local bool master_key_cached = false;
static unsigned char master_salt_cache[PBKDF2_SALT_LENGTH];
static bool master_salt_cached = false;

static void do_encrypt_file(struct worker_common* wc);
static void do_decrypt_file(struct worker_common* wc);
static int derive_key_iv(char* password, size_t password_length, unsigned char* salt, unsigned char* key, unsigned char* iv, int mode);
static int aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length, int mode);
static int aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext, int mode);
static const EVP_CIPHER* (*get_cipher(int mode))(void);
static const EVP_CIPHER* (*get_cipher_buffer(int mode))(void);
static int get_key_length(int mode);
static int encrypt_file(char* from, char* to, int enc);
static int pgmoneta_encrypt_data(int server, char* d, struct workers* workers, struct deque* excludes);
static int decrypt_data(char* d, struct workers* workers, struct deque* excludes);
static int dispatch_aes_operation(int server, char* from, char* to, int enc, struct workers* workers);
static void do_aes_operation(struct worker_common* wc);

static int encrypt_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** res_buffer, size_t* res_size, int enc, int mode);

static int create_aes_encryptor(int mode, struct encryptor** encryptor);
static int create_noop_encryptor(struct encryptor** encryptor);

static void aes_encryptor_reset(struct encryptor* encryptor);
static void aes_encryptor_close(struct encryptor* encryptor);
static int aes_encryptor_encrypt(struct encryptor* encryptor, void* in_buf, size_t in_size, bool last_chunk, void** out_buf, size_t* out_size);
static int aes_encryptor_decrypt(struct encryptor* encryptor, void* in_buf, size_t in_size, bool last_chunk, void** out_buf, size_t* out_size);
static int aes_encryptor_process(struct encryptor* encryptor, void* in_buf, size_t in_size, bool last_chunk, int enc, void** out_buf, size_t* out_size);

static void noop_encryptor_reset(struct encryptor* encryptor);
static void noop_encryptor_close(struct encryptor* encryptor);
static int noop_encryptor_encrypt(struct encryptor* encryptor, void* in_buf, size_t in_size, bool last_chunk, void** out_buf, size_t* out_size);
static int noop_encryptor_decrypt(struct encryptor* encryptor, void* in_buf, size_t in_size, bool last_chunk, void** out_buf, size_t* out_size);

struct aes_encryptor
{
   struct encryptor super;
   const EVP_CIPHER* (*cipher_fp)(void);
   EVP_CIPHER_CTX* ctx;
   int cipher_block_size;
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];
   bool key_derived;
   int mode;
   unsigned char* out_buf;                   /**< reusable output buffer */
   size_t out_capacity;                      /**< allocated capacity of out_buf */
   unsigned char tag_buffer[GCM_TAG_LENGTH]; /**< Buffer to hold the tag during streaming */
   size_t tag_buffer_size;                   /**< Current size of data in tag_buffer */
};

struct noop_encryptor
{
   struct encryptor super;
   unsigned char* out_buf; /**< reusable output buffer */
   size_t out_capacity;    /**< allocated capacity of out_buf */
};

struct aes_operation_task
{
   struct worker_common common;
   int enc;
   char from[MAX_PATH];
   char to[MAX_PATH];
   int server;
   bool progress_enabled;
};

static int
ensure_capacity(unsigned char** buf, size_t* capacity, size_t required)
{
   size_t new_cap;
   unsigned char* tmp = NULL;

   if (required == 0 || *capacity >= required)
   {
      return 0;
   }

   if (*capacity > SIZE_MAX / 2)
   {
      new_cap = required;
   }
   else if (*capacity == 0)
   {
      /* First allocation: add headroom to avoid immediate realloc */
      if (required > SIZE_MAX - required / 4)
      {
         new_cap = required;
      }
      else
      {
         new_cap = required + required / 4;
      }
   }
   else
   {
      new_cap = *capacity * 2;
   }

   if (new_cap < required)
   {
      new_cap = required;
   }

   pgmoneta_log_debug("ensure_capacity: grow buffer %zu -> %zu (required %zu)", *capacity, new_cap, required);

   tmp = realloc(*buf, new_cap);
   if (tmp == NULL)
   {
      pgmoneta_log_error("ensure_capacity: failed to allocate memory");
      return 1;
   }

   *buf = tmp;
   *capacity = new_cap;

   return 0;
}

static int
pgmoneta_encrypt_data(int server, char* d, struct workers* workers, struct deque* excludes)
{
   bool progress_enabled = (server >= 0 && pgmoneta_is_progress_enabled(server));
   char* from = NULL;
   char* to = NULL;
   DIR* dir;
   struct dirent* entry;

   if (!(dir = opendir(d)))
   {
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "pg_tblspc") == 0)
         {
            continue;
         }

         pgmoneta_snprintf(path, sizeof(path), "%s/%s", d, entry->d_name);
         pgmoneta_encrypt_data(server, path, workers, excludes);
      }
      else
      {
         if (!pgmoneta_ends_with(entry->d_name, ".aes") &&
             !pgmoneta_ends_with(entry->d_name, ".partial") &&
             !pgmoneta_ends_with(entry->d_name, ".history") &&
             !pgmoneta_ends_with(entry->d_name, ".tmp") &&
             !pgmoneta_ends_with(entry->d_name, "backup_label") &&
             !pgmoneta_ends_with(entry->d_name, "backup_manifest"))
         {
            if (excludes != NULL)
            {
               char* ext = strrchr(entry->d_name, '.');

               if (pgmoneta_deque_exists(excludes, entry->d_name) || (ext != NULL && pgmoneta_deque_exists(excludes, ext)))
               {
                  continue;
               }
            }

            from = pgmoneta_append(from, d);
            from = pgmoneta_append(from, "/");
            from = pgmoneta_append(from, entry->d_name);

            to = pgmoneta_append(to, d);
            to = pgmoneta_append(to, "/");
            to = pgmoneta_append(to, entry->d_name);
            to = pgmoneta_append(to, ".aes");

            if (pgmoneta_exists(from))
            {
               struct worker_input* wi = NULL;

               if (!pgmoneta_create_worker_input(NULL, from, to, 0, workers, &wi))
               {
                  if (workers != NULL)
                  {
                     if (workers->outcome)
                     {
                        pgmoneta_workers_add(workers, do_encrypt_file, (struct worker_common*)wi);
                     }
                     else
                     {
                        do_encrypt_file((struct worker_common*)wi);
                     }
                  }
                  else
                  {
                     do_encrypt_file((struct worker_common*)wi);
                  }

                  if (progress_enabled)
                  {
                     pgmoneta_progress_increment(server, 1);
                  }
               }
               else
               {
                  pgmoneta_log_error("Could not create a worker instance: %s -> %s", from, to);
               }
            }

            free(from);
            free(to);

            from = NULL;
            to = NULL;
         }
      }
   }

   closedir(dir);

   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   return 1;
}

static void
do_encrypt_file(struct worker_common* wc)
{
   struct worker_input* wi = (struct worker_input*)wc;

   if (!encrypt_file(wi->from, wi->to, 1))
   {
      if (pgmoneta_exists(wi->from))
      {
         pgmoneta_delete_file(wi->from, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exists", wi->from);
      }
   }
   else
   {
      pgmoneta_log_warn("do_encrypt_file: %s -> %s", wi->from, wi->to);
      if (wi->common.workers != NULL)
      {
         wi->common.workers->outcome = false;
      }
   }

   free(wi);
}

int
pgmoneta_encrypt_directory(int server, char* d, struct workers* workers, struct deque* excludes)
{
   return pgmoneta_encrypt_data(server, d, workers, excludes);
}

void
pgmoneta_encrypt_request(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* from = NULL;
   char* to = NULL;
   char* en = NULL;
   int ec = -1;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct json* req = NULL;
   struct json* response = NULL;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   from = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_SOURCE_FILE);

   if (!pgmoneta_exists(from))
   {
      ec = MANAGEMENT_ERROR_ENCRYPT_NOFILE;
      pgmoneta_log_error("Encrypt: No file for %s", from);
      goto error;
   }

   to = pgmoneta_append(to, from);
   to = pgmoneta_append(to, ".aes");

   if (encrypt_file(from, to, 1))
   {
      ec = MANAGEMENT_ERROR_ENCRYPT_ERROR;
      pgmoneta_log_error("Encrypt: Error encrypting %s", from);
      goto error;
   }

   if (pgmoneta_exists(from))
   {
      pgmoneta_delete_file(from, NULL);
   }
   else
   {
      pgmoneta_log_debug("%s doesn't exists", from);
   }

   if (pgmoneta_management_create_response(payload, -1, &response))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      pgmoneta_log_error("Encrypt: Allocation error");
      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_DESTINATION_FILE, (uintptr_t)to, ValueString);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_ENCRYPT_NETWORK;
      pgmoneta_log_error("Encrypt: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Encrypt: %s (Elapsed: %s)", from, elapsed);

   free(to);
   free(elapsed);

   exit(0);

error:

   pgmoneta_management_response_error(ssl, client_fd, NULL,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_ENCRYPT_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   free(to);
   free(elapsed);

   exit(1);
}

int
pgmoneta_encrypt_file(char* from, char* to, struct workers* workers)
{
   int flag = 0;
   int ret = 0;

   if (!pgmoneta_exists(from))
   {
      pgmoneta_log_error("pgmoneta_encrypt_file: file not exist: %s", from);
      return 1;
   }

   if (!to)
   {
      to = pgmoneta_append(to, from);
      to = pgmoneta_append(to, ".aes");
      flag = 1;
   }

   if (workers != NULL)
   {
      ret = dispatch_aes_operation(-1, from, to, 1, workers);
      if (flag)
      {
         free(to);
      }
      return ret;
   }

   if (encrypt_file(from, to, 1) != 0)
   {
      if (flag)
      {
         free(to);
      }
      return 1;
   }

   if (pgmoneta_exists(from))
   {
      pgmoneta_delete_file(from, NULL);
   }
   else
   {
      pgmoneta_log_debug("%s doesn't exists", from);
   }

   if (flag)
   {
      free(to);
   }
   return 0;
}

int
pgmoneta_decrypt_file(char* from, char* to, struct workers* workers)
{
   int flag = 0;
   int ret = 0;

   if (!pgmoneta_exists(from))
   {
      pgmoneta_log_error("pgmoneta_decrypt_file: file not exist: %s", from);
      return 1;
   }

   if (!to)
   {
      if (pgmoneta_strip_extension(from, &to))
      {
         return 1;
      }
      flag = 1;
   }

   if (workers != NULL)
   {
      ret = dispatch_aes_operation(-1, from, to, 0, workers);
      if (flag)
      {
         free(to);
      }
      return ret;
   }

   if (encrypt_file(from, to, 0) != 0)
   {
      if (flag)
      {
         free(to);
      }
      return 1;
   }
   if (pgmoneta_exists(from))
   {
      pgmoneta_delete_file(from, NULL);
   }
   else
   {
      pgmoneta_log_debug("%s doesn't exists", from);
   }

   if (flag)
   {
      free(to);
   }
   return 0;
}

static int
decrypt_data(char* d, struct workers* workers, struct deque* excludes)
{
   char* from = NULL;
   char* to = NULL;
   char* name = NULL;
   DIR* dir = NULL;
   struct dirent* entry;

   if (!(dir = opendir(d)))
   {
      pgmoneta_log_error("decrypt_data: Could not open directory %s", d);
      goto error;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR || entry->d_type == DT_LNK)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         pgmoneta_snprintf(path, sizeof(path), "%s/%s", d, entry->d_name);
         decrypt_data(path, workers, excludes);
      }
      else
      {
         if (pgmoneta_ends_with(entry->d_name, ".aes"))
         {
            struct worker_input* wi = NULL;

            from = pgmoneta_append(from, d);
            from = pgmoneta_append(from, "/");
            from = pgmoneta_append(from, entry->d_name);

            name = malloc(strlen(entry->d_name) - 3);

            if (name == NULL)
            {
               goto error;
            }

            memset(name, 0, strlen(entry->d_name) - 3);
            memcpy(name, entry->d_name, strlen(entry->d_name) - 4);

            to = pgmoneta_append(to, d);
            to = pgmoneta_append(to, "/");
            to = pgmoneta_append(to, name);

            if (!pgmoneta_create_worker_input(NULL, from, to, 0, workers, &wi))
            {
               if (workers != NULL)
               {
                  if (workers->outcome)
                  {
                     pgmoneta_workers_add(workers, do_decrypt_file, (struct worker_common*)wi);
                  }
                  else
                  {
                     do_decrypt_file((struct worker_common*)wi);
                  }
               }
               else
               {
                  do_decrypt_file((struct worker_common*)wi);
               }
            }
            else
            {
               pgmoneta_log_error("Could not create a worker instance: %s -> %s", from, to);
            }

            free(name);
            free(from);
            free(to);

            name = NULL;
            from = NULL;
            to = NULL;
         }
      }
   }

   closedir(dir);
   return 0;

error:

   if (dir != NULL)
   {
      closedir(dir);
   }

   return 1;
}

int
pgmoneta_decrypt_directory(char* d, struct workers* workers, struct deque* excludes)
{
   int ret = decrypt_data(d, workers, excludes);

   return ret;
}

void
pgmoneta_encryptor_destroy(struct encryptor* encryptor)
{
   if (encryptor == NULL)
   {
      return;
   }
   encryptor->close(encryptor);
   free(encryptor);
}

int
pgmoneta_encryptor_create(int encryption_type, struct encryptor** encryptor)
{
   if (encryption_type == ENCRYPTION_NONE)
   {
      /* create noop encryptor */
      return create_noop_encryptor(encryptor);
   }
   return create_aes_encryptor(encryption_type, encryptor);
}

static void
do_decrypt_file(struct worker_common* wc)
{
   struct worker_input* wi = (struct worker_input*)wc;

   if (!encrypt_file(wi->from, wi->to, 0))
   {
      if (pgmoneta_exists(wi->from))
      {
         pgmoneta_delete_file(wi->from, NULL);
      }
      else
      {
         pgmoneta_log_debug("%s doesn't exists", wi->from);
      }
   }
   else
   {
      pgmoneta_log_warn("do_decrypt_file: %s -> %s", wi->from, wi->to);
      if (wi->common.workers != NULL)
      {
         wi->common.workers->outcome = false;
      }
   }

   free(wi);
}

void
pgmoneta_decrypt_request(SSL* ssl, int client_fd, uint8_t compression, uint8_t encryption, struct json* payload)
{
   char* from = NULL;
   char* to = NULL;
   char* en = NULL;
   int ec = -1;
   char* elapsed = NULL;
   struct timespec start_t;
   struct timespec end_t;
   double total_seconds;
   struct json* req = NULL;
   struct json* response = NULL;

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &start_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &start_t);
#endif

   req = (struct json*)pgmoneta_json_get(payload, MANAGEMENT_CATEGORY_REQUEST);
   from = (char*)pgmoneta_json_get(req, MANAGEMENT_ARGUMENT_SOURCE_FILE);

   if (!pgmoneta_exists(from))
   {
      ec = MANAGEMENT_ERROR_DECRYPT_NOFILE;
      pgmoneta_log_error("Decrypt: No file for %s", from);
      goto error;
   }

   to = malloc(strlen(from) - 3);
   if (to == NULL)
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      pgmoneta_log_error("Decrypt: Allocation error");
      goto error;
   }

   memset(to, 0, strlen(from) - 3);
   memcpy(to, from, strlen(from) - 4);

   if (encrypt_file(from, to, 0))
   {
      ec = MANAGEMENT_ERROR_DECRYPT_ERROR;
      pgmoneta_log_error("Decrypt: Error decrypting %s", from);
      goto error;
   }

   if (pgmoneta_exists(from))
   {
      pgmoneta_delete_file(from, NULL);
   }
   else
   {
      pgmoneta_log_debug("%s doesn't exists", from);
   }

   if (pgmoneta_management_create_response(payload, -1, &response))
   {
      ec = MANAGEMENT_ERROR_ALLOCATION;
      pgmoneta_log_error("Decrypt: Allocation error");
      goto error;
   }

   pgmoneta_json_put(response, MANAGEMENT_ARGUMENT_DESTINATION_FILE, (uintptr_t)to, ValueString);

#ifdef HAVE_FREEBSD
   clock_gettime(CLOCK_MONOTONIC_FAST, &end_t);
#else
   clock_gettime(CLOCK_MONOTONIC_RAW, &end_t);
#endif

   if (pgmoneta_management_response_ok(NULL, client_fd, start_t, end_t, compression, encryption, payload))
   {
      ec = MANAGEMENT_ERROR_DECRYPT_NETWORK;
      pgmoneta_log_error("Decrypt: Error sending response");
      goto error;
   }

   elapsed = pgmoneta_get_timestamp_string(start_t, end_t, &total_seconds);

   pgmoneta_log_info("Decrypt: %s (Elapsed: %s)", from, elapsed);

   free(to);
   free(elapsed);

   exit(0);

error:

   pgmoneta_management_response_error(ssl, client_fd, NULL,
                                      ec != -1 ? ec : MANAGEMENT_ERROR_DECRYPT_ERROR, en != NULL ? en : NAME,
                                      compression, encryption, payload);

   free(to);
   free(elapsed);

   exit(1);
}

int
pgmoneta_encrypt(char* plaintext, char* password, size_t password_length, char** ciphertext, int* ciphertext_length, int mode)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];
   char* encrypted = NULL;
   int encrypted_length = 0;
   char* output = NULL;
   int ret = 1;

   const EVP_CIPHER* (*cipher_fp)(void) = NULL;

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   /* Generate a cryptographically random salt */
   if (!RAND_bytes(salt, PBKDF2_SALT_LENGTH))
   {
      goto cleanup;
   }

   if (derive_key_iv(password, password_length, salt, key, NULL, mode) != 0)
   {
      goto cleanup;
   }

   cipher_fp = get_cipher(mode);
   if (cipher_fp == NULL)
   {
      goto cleanup;
   }

   if (!RAND_bytes(iv, AES_GCM_IV_LENGTH))
   {
      goto cleanup;
   }

   if (aes_encrypt(plaintext, key, iv, &encrypted, &encrypted_length, mode))
   {
      goto cleanup;
   }

   /* Prepend salt and IV to ciphertext: [salt][iv][encrypted] */
   output = malloc(PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + encrypted_length);
   if (output == NULL)
   {
      goto cleanup;
   }

   memcpy(output, salt, PBKDF2_SALT_LENGTH);
   memcpy(output + PBKDF2_SALT_LENGTH, iv, AES_GCM_IV_LENGTH);
   memcpy(output + PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH, encrypted, encrypted_length);

   *ciphertext = output;
   *ciphertext_length = PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + encrypted_length;

   ret = 0;

cleanup:
   free(encrypted);

   /* Wipe key material from stack */
   pgmoneta_cleanse(key, sizeof(key));
   pgmoneta_cleanse(iv, sizeof(iv));
   pgmoneta_cleanse(salt, sizeof(salt));

   return ret;
}

int
pgmoneta_decrypt(char* ciphertext, int ciphertext_length, char* password, size_t password_length, char** plaintext, int mode)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];
   int ret = 1;
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);

   if (cipher_fp == NULL)
   {
      return 1;
   }

   /* The ciphertext must be at least salt_length + AES_GCM_IV_LENGTHgth + 1 byte */
   if (ciphertext_length <= PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH)
   {
      return 1;
   }

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   /* Extract salt from the first PBKDF2_SALT_LENGTH bytes */
   memcpy(salt, ciphertext, PBKDF2_SALT_LENGTH);

   /* Extract IV */
   memcpy(iv, ciphertext + PBKDF2_SALT_LENGTH, AES_GCM_IV_LENGTH);

   if (derive_key_iv(password, password_length, salt, key, NULL, mode) != 0)
   {
      goto cleanup;
   }

   ret = aes_decrypt(ciphertext + PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH,
                     ciphertext_length - PBKDF2_SALT_LENGTH - AES_GCM_IV_LENGTH,
                     key, iv, plaintext, mode);

cleanup:
   /* Wipe key material from stack */
   pgmoneta_cleanse(key, sizeof(key));
   pgmoneta_cleanse(iv, sizeof(iv));
   pgmoneta_cleanse(salt, sizeof(salt));

   return ret;
}

void
pgmoneta_clear_aes_cache(void)
{
   pgmoneta_cleanse(master_key_cache, sizeof(master_key_cache));
   pgmoneta_cleanse(cached_password_hash, sizeof(cached_password_hash));
   password_hash_cached = false;
   master_key_cached = false;
   pgmoneta_cleanse(master_salt_cache, sizeof(master_salt_cache));
   master_salt_cached = false;

   pgmoneta_clear_security_cache();
}

void
pgmoneta_set_master_salt(unsigned char* salt)
{
   if (salt == NULL)
   {
      pgmoneta_cleanse(master_salt_cache, sizeof(master_salt_cache));
      master_salt_cached = false;
      return;
   }
   memcpy(master_salt_cache, salt, PBKDF2_SALT_LENGTH);
   master_salt_cached = true;
}

/* [private] */
static int
derive_key_iv(char* password, size_t password_length, unsigned char* salt, unsigned char* key, unsigned char* iv, int mode)
{
   unsigned char ms[PBKDF2_SALT_LENGTH];
   int key_length;
   int iv_length;
   unsigned char derived[EVP_MAX_KEY_LENGTH + EVP_MAX_IV_LENGTH];
   int ret = 1;

   if (master_salt_cached)
   {
      memcpy(ms, master_salt_cache, PBKDF2_SALT_LENGTH);
   }
   else
   {
      pgmoneta_log_error("derive_key_iv: Master salt not initialized. Please run 'pgmoneta-admin user master-key' to generate a secure master key file.");
      return 1;
   }

   unsigned char current_password_hash[EVP_MAX_MD_SIZE];
   unsigned int hash_len = 0;

   key_length = get_key_length(mode);
   if (key_length == 0)
   {
      goto cleanup;
   }

   if (password_length > MAX_PASSWORD_LENGTH)
   {
      pgmoneta_log_error("derive_key_iv: password length exceeds maximum allowed length");
      goto cleanup;
   }

   /* Calculate SHA-256 hash of the input password for cache validation */
   if (!EVP_Digest(password, password_length, current_password_hash, &hash_len, EVP_sha256(), NULL))
   {
      pgmoneta_log_error("Failed to calculate password hash");
      goto cleanup;
   }

   /* Step 1: Ensure Master Key is derived and cached */
   if (!master_key_cached || !password_hash_cached || memcmp(current_password_hash, cached_password_hash, hash_len) != 0)
   {
      if (!PKCS5_PBKDF2_HMAC(password, password_length,
                             ms, PBKDF2_SALT_LENGTH,
                             PBKDF2_ITERATIONS,
                             EVP_sha256(),
                             EVP_MAX_KEY_LENGTH,
                             master_key_cache))
      {
         pgmoneta_log_error("Failed to derive Master Key");
         goto cleanup;
      }
      memcpy(cached_password_hash, current_password_hash, hash_len);
      password_hash_cached = true;
      master_key_cached = true;
   }

   /* Step 2: Use Master Key to derive File Key fast (1 iteration) */
   if (!PKCS5_PBKDF2_HMAC((char*)master_key_cache, EVP_MAX_KEY_LENGTH,
                          salt, PBKDF2_SALT_LENGTH,
                          1,
                          EVP_sha256(),
                          EVP_MAX_KEY_LENGTH + EVP_MAX_IV_LENGTH,
                          derived))
   {
      pgmoneta_log_error("Failed to derive File Key");
      goto cleanup;
   }

   if (key != NULL)
   {
      memcpy(key, derived, key_length);
   }
   if (iv != NULL)
   {
      iv_length = EVP_CIPHER_iv_length(get_cipher(mode)());
      memcpy(iv, derived + key_length, iv_length);
   }

   ret = 0;

cleanup:
   /* Wipe sensitive derived material */
   pgmoneta_cleanse(derived, sizeof(derived));
   pgmoneta_cleanse(current_password_hash, sizeof(current_password_hash));

   return ret;
}

/* [private] */
static int
aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length, int mode)
{
   EVP_CIPHER_CTX* ctx = NULL;
   int length;
   size_t size;
   unsigned char* ct = NULL;
   int ct_length = 0;
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);

   if (cipher_fp == NULL)
   {
      goto error;
   }

   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      goto error;
   }

   if (!EVP_EncryptInit_ex(ctx, cipher_fp(), NULL, key, iv))
   {
      goto error;
   }

   size = strlen(plaintext) + GCM_TAG_LENGTH + EVP_CIPHER_block_size(cipher_fp());
   ct = malloc(size);

   if (ct == NULL)
   {
      goto error;
   }

   memset(ct, 0, size);

   if (!EVP_EncryptUpdate(ctx,
                          ct, &length,
                          (unsigned char*)plaintext, strlen((char*)plaintext)))
   {
      goto error;
   }

   ct_length = length;

   if (!EVP_EncryptFinal_ex(ctx, ct + length, &length))
   {
      goto error;
   }

   ct_length += length;

   if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_LENGTH, ct + ct_length))
   {
      goto error;
   }
   ct_length += GCM_TAG_LENGTH;

   EVP_CIPHER_CTX_free(ctx);

   *ciphertext = (char*)ct;
   *ciphertext_length = ct_length;

   return 0;

error:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   if (ct != NULL)
   {
      pgmoneta_cleanse(ct, size);
      free(ct);
   }

   return 1;
}

/* [private] */
static int
aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext, int mode)
{
   EVP_CIPHER_CTX* ctx = NULL;
   int plaintext_length;
   int length;
   size_t size;
   char* pt = NULL;
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);

   if (cipher_fp == NULL)
   {
      return 1;
   }

   if (ciphertext_length < GCM_TAG_LENGTH)
   {
      return 1;
   }

   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      goto error;
   }

   if (!EVP_DecryptInit_ex(ctx, cipher_fp(), NULL, key, iv))
   {
      goto error;
   }

   size = ciphertext_length + EVP_CIPHER_block_size(cipher_fp());
   pt = malloc(size);

   if (pt == NULL)
   {
      goto error;
   }

   memset(pt, 0, size);

   if (!EVP_DecryptUpdate(ctx,
                          (unsigned char*)pt, &length,
                          (unsigned char*)ciphertext, ciphertext_length - GCM_TAG_LENGTH))
   {
      goto error;
   }

   plaintext_length = length;

   if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_LENGTH, ciphertext + ciphertext_length - GCM_TAG_LENGTH))
   {
      goto error;
   }

   if (!EVP_DecryptFinal_ex(ctx, (unsigned char*)pt + length, &length))
   {
      goto error;
   }

   plaintext_length += length;

   EVP_CIPHER_CTX_free(ctx);

   pt[plaintext_length] = 0;
   *plaintext = pt;

   return 0;

error:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   if (pt != NULL)
   {
      pgmoneta_cleanse(pt, size);
      free(pt);
   }

   return 1;
}

static const EVP_CIPHER* (*get_cipher(int mode))(void)
{
   if (mode == ENCRYPTION_AES_256_GCM)
   {
      return &EVP_aes_256_gcm;
   }
   if (mode == ENCRYPTION_AES_192_GCM)
   {
      return &EVP_aes_192_gcm;
   }
   if (mode == ENCRYPTION_AES_128_GCM)
   {
      return &EVP_aes_128_gcm;
   }
   return NULL;
}

/* enc: 1 for encrypt, 0 for decrypt */
static int
encrypt_file(char* from, char* to, int enc)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];
   unsigned char tag[GCM_TAG_LENGTH];
   char* master_key = NULL;
   EVP_CIPHER_CTX* ctx = NULL;
   struct main_configuration* config;
   const EVP_CIPHER* (*cipher_fp)(void) = NULL;
   int cipher_block_size = 0;
   int inbuf_size = 0;
   int outbuf_size = 0;
   FILE* in = NULL;
   FILE* out = NULL;
   int inl = 0;
   int outl = 0;
   int f_len = 0;
   int ret = 1;
   char* tmp_to = NULL;

   unsigned char* inbuf = NULL;
   unsigned char* outbuf = NULL;
   long remaining = 0;

   config = (struct main_configuration*)shmem;

   if (config->common.encryption == ENCRYPTION_NONE)
   {
      pgmoneta_log_error("encrypt_file: encryption is not configured (encryption = none)");
      goto error;
   }

   cipher_fp = get_cipher(config->common.encryption);
   if (cipher_fp == NULL)
   {
      pgmoneta_log_error("encrypt_file: unsupported encryption mode: %d", config->common.encryption);
      goto error;
   }
   cipher_block_size = EVP_CIPHER_block_size(cipher_fp());
   inbuf_size = ENC_BUF_SIZE;
   outbuf_size = inbuf_size + cipher_block_size - 1;
   inbuf = malloc(inbuf_size);
   outbuf = malloc(outbuf_size);
   if (inbuf == NULL || outbuf == NULL)
   {
      goto error;
   }

   size_t master_key_length = 0;
   unsigned char* master_salt = NULL;
   size_t master_salt_length = 0;

   if (pgmoneta_get_master_key(&master_key, &master_key_length, &master_salt, &master_salt_length))
   {
      pgmoneta_log_error("pgmoneta_get_master_key: Invalid master key");
      goto error;
   }

   if (master_salt != NULL)
   {
      pgmoneta_set_master_salt(master_salt);
      free(master_salt);
   }
   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (enc == 1)
   {
      /* Encryption: generate a random salt */
      if (!RAND_bytes(salt, PBKDF2_SALT_LENGTH))
      {
         pgmoneta_log_error("RAND_bytes: Failed to generate salt");
         goto error;
      }

      if (derive_key_iv(master_key, master_key_length, salt, key, NULL, config->common.encryption) != 0)
      {
         pgmoneta_log_error("derive_key_iv: Failed to derive key");
         goto error;
      }

      if (!RAND_bytes(iv, AES_GCM_IV_LENGTH))
      {
         pgmoneta_log_error("RAND_bytes: Failed to generate unique IV");
         goto error;
      }
   }
   else
   {
      in = fopen(from, "rb");
      if (in == NULL)
      {
         pgmoneta_log_error("fopen: Could not open %s", from);
         goto error;
      }

      if (fread(salt, 1, PBKDF2_SALT_LENGTH, in) != PBKDF2_SALT_LENGTH)
      {
         pgmoneta_log_error("fread: failed to read salt from %s", from);
         goto error;
      }

      if (fread(iv, 1, AES_GCM_IV_LENGTH, in) != AES_GCM_IV_LENGTH)
      {
         pgmoneta_log_error("fread: failed to read IV from %s", from);
         goto error;
      }

      long file_size;
      long header_size = PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH;

      if (fseek(in, 0L, SEEK_END) != 0)
      {
         pgmoneta_log_error("fseek: failed to seek to end of %s", from);
         goto error;
      }

      file_size = ftell(in);
      if (file_size < 0)
      {
         pgmoneta_log_error("ftell: failed to determine file size for %s", from);
         goto error;
      }

      if (file_size < header_size + GCM_TAG_LENGTH)
      {
         pgmoneta_log_error("Invalid encrypted file size for %s", from);
         goto error;
      }

      remaining = file_size - header_size - GCM_TAG_LENGTH;

      /* Seek to the end to read the GCM tag */
      if (fseek(in, -((long)GCM_TAG_LENGTH), SEEK_END) != 0)
      {
         pgmoneta_log_error("fseek: failed to find GCM tag in %s", from);
         goto error;
      }
      if (fread(tag, 1, GCM_TAG_LENGTH, in) != (size_t)GCM_TAG_LENGTH)
      {
         pgmoneta_log_error("fread: failed to read GCM tag from %s", from);
         goto error;
      }
      /* Seek back to data start (after salt + IV) */
      if (fseek(in, header_size, SEEK_SET) != 0)
      {
         pgmoneta_log_error("fseek: failed to return to data in %s", from);
         goto error;
      }

      if (derive_key_iv(master_key, master_key_length, salt, key, NULL, config->common.encryption) != 0)
      {
         pgmoneta_log_error("derive_key_iv: Failed to derive key");
         goto error;
      }
   }

   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      pgmoneta_log_error("EVP_CIPHER_CTX_new: Failed to get context");
      goto error;
   }

   if (enc == 1)
   {
      in = fopen(from, "rb");
      if (in == NULL)
      {
         pgmoneta_log_error("fopen: Could not open %s", from);
         goto error;
      }
   }

   if (pgmoneta_exists(to))
   {
      pgmoneta_log_error("encrypt_file: destination file %s already exists", to);
      goto error;
   }

   tmp_to = pgmoneta_append(tmp_to, to);
   tmp_to = pgmoneta_append(tmp_to, ".tmp");

   out = fopen(tmp_to, "wb");
   if (out == NULL)
   {
      pgmoneta_log_error("fopen: Could not open %s", tmp_to);
      goto error;
   }

   if (enc == 1)
   {
      /* Prepend salt and IV to the output file */
      if (fwrite(salt, 1, PBKDF2_SALT_LENGTH, out) != PBKDF2_SALT_LENGTH)
      {
         pgmoneta_log_error("fwrite: failed to write salt");
         goto error;
      }
      if (fwrite(iv, 1, AES_GCM_IV_LENGTH, out) != AES_GCM_IV_LENGTH)
      {
         pgmoneta_log_error("fwrite: failed to write IV");
         goto error;
      }
   }

   if (EVP_CipherInit_ex(ctx, cipher_fp(), NULL, key, iv, enc) == 0)
   {
      pgmoneta_log_error("EVP_CipherInit_ex: Failed to initialize context");
      goto error;
   }

   if (!enc)
   {
      if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_LENGTH, tag) == 0)
      {
         pgmoneta_log_error("EVP_CIPHER_CTX_ctrl: failed to set GCM tag");
         goto error;
      }
   }

   while ((inl = fread(inbuf, sizeof(char), (!enc && remaining < inbuf_size) ? remaining : inbuf_size, in)) > 0)
   {
      if (EVP_CipherUpdate(ctx, outbuf, &outl, inbuf, inl) == 0)
      {
         pgmoneta_log_error("EVP_CipherUpdate: failed to process block");
         goto error;
      }
      if (fwrite(outbuf, sizeof(char), outl, out) != (size_t)outl)
      {
         pgmoneta_log_error("fwrite: failed to write cipher");
         goto error;
      }

      if (!enc)
      {
         remaining -= inl;
         if (remaining == 0)
         {
            break;
         }
      }
   }

   if (ferror(in))
   {
      pgmoneta_log_error("fread: error reading from file: %s", from);
      goto error;
   }

   if (EVP_CipherFinal_ex(ctx, outbuf, &f_len) == 0)
   {
      pgmoneta_log_error("EVP_CipherFinal_ex: failed to process final cipher block");
      goto error;
   }

   if (f_len)
   {
      if (fwrite(outbuf, sizeof(char), f_len, out) != (size_t)f_len)
      {
         pgmoneta_log_error("fwrite: failed to write final block");
         goto error;
      }
   }

   if (enc)
   {
      if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_LENGTH, tag) == 0)
      {
         pgmoneta_log_error("EVP_CIPHER_CTX_ctrl: failed to get GCM tag");
         goto error;
      }
      if (fwrite(tag, sizeof(char), GCM_TAG_LENGTH, out) != (size_t)GCM_TAG_LENGTH)
      {
         pgmoneta_log_error("fwrite: failed to write GCM tag at the end");
         goto error;
      }
   }

   ret = 0;

cleanup:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   /* Wipe key material from stack */
   pgmoneta_cleanse(key, sizeof(key));
   pgmoneta_cleanse(iv, sizeof(iv));
   pgmoneta_cleanse(salt, sizeof(salt));
   pgmoneta_cleanse(tag, sizeof(tag));

   if (master_key != NULL)
   {
      pgmoneta_cleanse(master_key, master_key_length);
      free(master_key);
   }

   if (in != NULL)
   {
      fclose(in);
   }

   if (out != NULL)
   {
      fflush(out);
      fclose(out);
   }

   if (ret == 0)
   {
      pgmoneta_permission(tmp_to, 6, 0, 0);
      if (pgmoneta_move_file(tmp_to, to))
      {
         ret = 1;
      }
   }
   else
   {
      pgmoneta_delete_file(tmp_to, NULL);
   }

   free(tmp_to);
   if (inbuf != NULL)
   {
      pgmoneta_cleanse(inbuf, inbuf_size);
      free(inbuf);
   }
   if (outbuf != NULL)
   {
      pgmoneta_cleanse(outbuf, outbuf_size);
      free(outbuf);
   }

   return ret;

error:
   goto cleanup;
}

int
pgmoneta_encrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** enc_buffer, size_t* enc_size, int mode)
{
   return encrypt_decrypt_buffer(origin_buffer, origin_size, enc_buffer, enc_size, 1, mode);
}

int
pgmoneta_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** dec_buffer, size_t* dec_size, int mode)
{
   return encrypt_decrypt_buffer(origin_buffer, origin_size, dec_buffer, dec_size, 0, mode);
}

static int
encrypt_decrypt_buffer(unsigned char* origin_buffer, size_t origin_size, unsigned char** res_buffer, size_t* res_size, int enc, int mode)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[PBKDF2_SALT_LENGTH];
   unsigned char tag[GCM_TAG_LENGTH];
   char* master_key = NULL;
   EVP_CIPHER_CTX* ctx = NULL;
   const EVP_CIPHER* (*cipher_fp)(void) = NULL;
   size_t cipher_block_size = 0;
   size_t outbuf_size = 0;
   size_t outl = 0;
   size_t f_len = 0;
   int outl_int = 0;
   int f_len_int = 0;

   unsigned char* actual_input = NULL;
   size_t actual_input_size = 0;

   int ret = 1;

   *res_buffer = NULL;

   cipher_fp = get_cipher_buffer(mode);
   if (cipher_fp == NULL)
   {
      pgmoneta_log_error("Invalid encryption method specified");
      goto error;
   }

   cipher_block_size = EVP_CIPHER_block_size(cipher_fp());

   size_t master_key_length = 0;
   unsigned char* master_salt = NULL;
   size_t master_salt_length = 0;

   if (pgmoneta_get_master_key(&master_key, &master_key_length, &master_salt, &master_salt_length))
   {
      pgmoneta_log_error("pgmoneta_get_master_key: Invalid master key");
      goto error;
   }

   if (master_salt != NULL)
   {
      pgmoneta_set_master_salt(master_salt);
      free(master_salt);
   }

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));
   memset(&tag, 0, sizeof(tag));

   if (enc == 1)
   {
      /* Encryption: generate a random salt */
      if (!RAND_bytes(salt, PBKDF2_SALT_LENGTH))
      {
         pgmoneta_log_error("RAND_bytes: Failed to generate salt");
         goto error;
      }

      if (derive_key_iv(master_key, master_key_length, salt, key, NULL, mode) != 0)
      {
         pgmoneta_log_error("derive_key_iv: Failed to derive key");
         goto error;
      }

      if (!RAND_bytes(iv, AES_GCM_IV_LENGTH))
      {
         pgmoneta_log_error("RAND_bytes: Failed to generate unique IV");
         goto error;
      }

      /* Output buffer: salt (16) + iv field (12) + encrypted data + padding + tag (16) */
      outbuf_size = PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH;
      if (origin_size > SIZE_MAX - outbuf_size - cipher_block_size - GCM_TAG_LENGTH)
      {
         pgmoneta_log_error("pgmoneta_encrypt_decrypt_buffer: Size overflow computing output buffer");
         goto error;
      }
      outbuf_size += origin_size + cipher_block_size + GCM_TAG_LENGTH;

      if (outbuf_size > SIZE_MAX - 1)
      {
         pgmoneta_log_error("pgmoneta_encrypt_decrypt_buffer: Size overflow computing output buffer (+1)");
         goto error;
      }
      *res_buffer = (unsigned char*)malloc(outbuf_size + 1);
      if (*res_buffer == NULL)
      {
         pgmoneta_log_error("pgmoneta_encrypt_decrypt_buffer: Allocation failure");
         goto error;
      }

      /* Prepend salt and iv */
      memcpy(*res_buffer, salt, PBKDF2_SALT_LENGTH);
      memcpy(*res_buffer + PBKDF2_SALT_LENGTH, iv, AES_GCM_IV_LENGTH);

      if (!(ctx = EVP_CIPHER_CTX_new()))
      {
         pgmoneta_log_error("EVP_CIPHER_CTX_new: Failed to create context");
         goto error;
      }

      if (EVP_CipherInit_ex(ctx, cipher_fp(), NULL, key, iv, enc) == 0)
      {
         pgmoneta_log_error("EVP_CipherInit_ex: Failed to initialize cipher context");
         goto error;
      }

      if (origin_size > INT_MAX)
      {
         pgmoneta_log_error("pgmoneta_encrypt_decrypt_buffer: Input size exceeds INT_MAX");
         goto error;
      }

      if (EVP_CipherUpdate(ctx, *res_buffer + PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH, &outl_int, origin_buffer, (int)origin_size) == 0)
      {
         pgmoneta_log_error("EVP_CipherUpdate: Failed to process data");
         goto error;
      }

      outl = (size_t)outl_int;
      *res_size = PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + outl;

      if (EVP_CipherFinal_ex(ctx, *res_buffer + *res_size, &f_len_int) == 0)
      {
         pgmoneta_log_error("EVP_CipherFinal_ex: Failed to finalize operation");
         goto error;
      }
      f_len = (size_t)f_len_int;
      *res_size += f_len;

      if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_LENGTH, tag))
      {
         pgmoneta_log_error("EVP_CIPHER_CTX_ctrl: Failed to get GCM tag");
         goto error;
      }
      /* Append tag to the end */
      memcpy(*res_buffer + *res_size, tag, GCM_TAG_LENGTH);
      *res_size += GCM_TAG_LENGTH;
   }
   else
   {
      /* Decryption: extract metadata from the first bytes */
      if (origin_size < (size_t)(PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + GCM_TAG_LENGTH))
      {
         pgmoneta_log_error("encrypt_decrypt_buffer: Input too short for decryption");
         goto error;
      }

      memcpy(salt, origin_buffer, PBKDF2_SALT_LENGTH);
      memcpy(iv, origin_buffer + PBKDF2_SALT_LENGTH, AES_GCM_IV_LENGTH);

      /* Read tag from the end of the buffer */
      memcpy(tag, origin_buffer + origin_size - GCM_TAG_LENGTH, GCM_TAG_LENGTH);
      actual_input_size = origin_size - (PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + GCM_TAG_LENGTH);

      actual_input = origin_buffer + PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH;

      if (derive_key_iv(master_key, master_key_length, salt, key, NULL, mode) != 0)
      {
         pgmoneta_log_error("derive_key_iv: Failed to derive key");
         goto error;
      }

      if (actual_input_size > SIZE_MAX - cipher_block_size)
      {
         pgmoneta_log_error("pgmoneta_encrypt_decrypt_buffer: Size overflow computing output buffer (decrypt)");
         goto error;
      }
      outbuf_size = actual_input_size + cipher_block_size;

      if (outbuf_size > SIZE_MAX - 1)
      {
         pgmoneta_log_error("pgmoneta_encrypt_decrypt_buffer: Size overflow computing output buffer (+1)");
         goto error;
      }
      *res_buffer = (unsigned char*)malloc(outbuf_size + 1);
      if (*res_buffer == NULL)
      {
         pgmoneta_log_error("pgmoneta_encrypt_decrypt_buffer: Allocation failure");
         goto error;
      }

      if (!(ctx = EVP_CIPHER_CTX_new()))
      {
         pgmoneta_log_error("EVP_CIPHER_CTX_new: Failed to create context");
         goto error;
      }

      if (EVP_CipherInit_ex(ctx, cipher_fp(), NULL, key, iv, enc) == 0)
      {
         pgmoneta_log_error("EVP_CipherInit_ex: Failed to initialize cipher context");
         goto error;
      }

      if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_LENGTH, tag))
      {
         pgmoneta_log_error("EVP_CIPHER_CTX_ctrl: Failed to set GCM tag");
         goto error;
      }

      if (actual_input_size > INT_MAX)
      {
         pgmoneta_log_error("pgmoneta_encrypt_decrypt_buffer: Input size exceeds INT_MAX");
         goto error;
      }

      if (EVP_CipherUpdate(ctx, *res_buffer, &outl_int, actual_input, (int)actual_input_size) == 0)
      {
         pgmoneta_log_error("EVP_CipherUpdate: Failed to process data (size: %zu)", actual_input_size);
         goto error;
      }

      outl = (size_t)outl_int;
      *res_size = outl;

      if (EVP_CipherFinal_ex(ctx, *res_buffer + outl, &f_len_int) == 0)
      {
         pgmoneta_log_error("EVP_CipherFinal_ex: Failed to finalize operation (size: %zu, outl: %zu)", actual_input_size, outl);
         goto error;
      }

      f_len = (size_t)f_len_int;

      *res_size += f_len;
      (*res_buffer)[*res_size] = '\0';
   }

   ret = 0;

cleanup:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   /* Wipe material from stack */
   pgmoneta_cleanse(key, sizeof(key));
   pgmoneta_cleanse(iv, sizeof(iv));
   pgmoneta_cleanse(salt, sizeof(salt));
   pgmoneta_cleanse(tag, sizeof(tag));

   if (master_key != NULL)
   {
      pgmoneta_cleanse(master_key, master_key_length);
      free(master_key);
   }

   if (ret != 0 && *res_buffer != NULL)
   {
      pgmoneta_cleanse(*res_buffer, outbuf_size);
      free(*res_buffer);
      *res_buffer = NULL;
   }

   return ret;

error:
   goto cleanup;
}

static const EVP_CIPHER* (*get_cipher_buffer(int mode))(void)
{
   if (mode == ENCRYPTION_AES_256_GCM)
   {
      return &EVP_aes_256_gcm;
   }
   if (mode == ENCRYPTION_AES_192_GCM)
   {
      return &EVP_aes_192_gcm;
   }
   if (mode == ENCRYPTION_AES_128_GCM)
   {
      return &EVP_aes_128_gcm;
   }
   return NULL;
}

static int
create_aes_encryptor(int mode, struct encryptor** encryptor)
{
   struct aes_encryptor* ae = NULL;

   ae = (struct aes_encryptor*)malloc(sizeof(struct aes_encryptor));
   if (ae == NULL)
   {
      return 1;
   }
   memset(ae, 0, sizeof(struct aes_encryptor));

   ae->super.close = aes_encryptor_close;
   ae->super.decrypt = aes_encryptor_decrypt;
   ae->super.encrypt = aes_encryptor_encrypt;
   ae->super.reset = aes_encryptor_reset;
   ae->cipher_fp = get_cipher(mode);
   if (ae->cipher_fp == NULL)
   {
      free(ae);
      return 1;
   }
   ae->cipher_block_size = EVP_CIPHER_block_size(ae->cipher_fp());
   ae->mode = mode;

   *encryptor = (struct encryptor*)ae;

   return 0;
}

static int
create_noop_encryptor(struct encryptor** encryptor)
{
   struct noop_encryptor* ne = NULL;

   ne = (struct noop_encryptor*)malloc(sizeof(struct noop_encryptor));
   if (ne == NULL)
   {
      pgmoneta_log_error("create_noop_encryptor: failed to allocate memory");
      return 1;
   }

   memset(ne, 0, sizeof(struct noop_encryptor));
   ne->super.close = noop_encryptor_close;
   ne->super.encrypt = noop_encryptor_encrypt;
   ne->super.decrypt = noop_encryptor_decrypt;
   ne->super.reset = noop_encryptor_reset;

   *encryptor = (struct encryptor*)ne;
   return 0;
}

static void
aes_encryptor_reset(struct encryptor* encryptor)
{
   struct aes_encryptor* this = (struct aes_encryptor*)encryptor;
   if (this == NULL)
   {
      return;
   }
   if (this->ctx)
   {
      EVP_CIPHER_CTX_free(this->ctx);
      this->ctx = NULL;
   }
   this->tag_buffer_size = 0;
   memset(this->tag_buffer, 0, sizeof(this->tag_buffer));
}

static void
noop_encryptor_reset(struct encryptor* encryptor)
{
   (void)encryptor;
}

static void
aes_encryptor_close(struct encryptor* encryptor)
{
   struct aes_encryptor* this = (struct aes_encryptor*)encryptor;

   if (this == NULL)
   {
      return;
   }

   if (this->ctx)
   {
      EVP_CIPHER_CTX_free(this->ctx);
      this->ctx = NULL;
   }

   if (this->out_buf != NULL)
   {
      pgmoneta_cleanse(this->out_buf, this->out_capacity);
      free(this->out_buf);
      this->out_buf = NULL;
   }
   this->out_capacity = 0;

   pgmoneta_cleanse(this->key, sizeof(this->key));
   pgmoneta_cleanse(this->iv, sizeof(this->iv));
   pgmoneta_cleanse(this->salt, sizeof(this->salt));
   pgmoneta_cleanse(this->tag_buffer, sizeof(this->tag_buffer));
}

static int
aes_encryptor_encrypt(struct encryptor* encryptor, void* in_buf, size_t in_size, bool last_chunk, void** out_buf, size_t* out_size)
{
   return aes_encryptor_process(encryptor, in_buf, in_size, last_chunk, 1, out_buf, out_size);
}

static int
aes_encryptor_decrypt(struct encryptor* encryptor, void* in_buf, size_t in_size, bool last_chunk, void** out_buf, size_t* out_size)
{
   return aes_encryptor_process(encryptor, in_buf, in_size, last_chunk, 0, out_buf, out_size);
}

static int
aes_encryptor_process(struct encryptor* encryptor, void* in_buf, size_t in_size, bool last_chunk, int enc, void** out_buf, size_t* out_size)
{
   struct aes_encryptor* this = (struct aes_encryptor*)encryptor;
   size_t required = 0;
   int size = 0;
   int final_size = 0;
   bool write_header = false;
   int offset = 0;

   unsigned char tag[GCM_TAG_LENGTH];
   char* master_key = NULL;
   size_t master_key_length = 0;

   if (this == NULL || (in_buf == NULL && in_size > 0))
   {
      goto error;
   }

   if (in_size == 0 && !last_chunk)
   {
      return 0;
   }

   *out_buf = NULL;
   *out_size = 0;

   if (this->ctx == NULL)
   {
      unsigned char* master_salt = NULL;
      size_t master_salt_length = 0;

      if (pgmoneta_get_master_key(&master_key, &master_key_length, &master_salt, &master_salt_length))
      {
         pgmoneta_log_error("pgmoneta_get_master_key: Invalid master key");
         goto error;
      }

      if (master_salt != NULL)
      {
         pgmoneta_set_master_salt(master_salt);
         free(master_salt);
      }

      if (enc == 1)
      {
         if (!this->key_derived)
         {
            if (!RAND_bytes(this->salt, PBKDF2_SALT_LENGTH))
            {
               pgmoneta_log_error("RAND_bytes: Failed to generate salt");
               goto error;
            }
            if (derive_key_iv(master_key, master_key_length, this->salt, this->key, NULL, this->mode) != 0)
            {
               pgmoneta_log_error("derive_key_iv: Failed to derive master key");
               goto error;
            }
            this->key_derived = true;
         }

         if (!RAND_bytes(this->iv, AES_GCM_IV_LENGTH))
         {
            pgmoneta_log_error("RAND_bytes: Failed to generate unique IV");
            goto error;
         }

         write_header = true;
      }
      else
      {
         size_t header_len = PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH;

         if (in_size < header_len)
         {
            pgmoneta_log_error("Unable to load Salt+IV header");
            goto error;
         }

         /* Only re-derive the key if we haven't already, or if the stream salt changed somehow */
         if (!this->key_derived || memcmp(this->salt, in_buf, PBKDF2_SALT_LENGTH) != 0)
         {
            memcpy(this->salt, in_buf, PBKDF2_SALT_LENGTH);
            if (derive_key_iv(master_key, master_key_length, this->salt, this->key, NULL, this->mode) != 0)
            {
               pgmoneta_log_error("derive_key_iv: Failed to derive master key");
               goto error;
            }
            this->key_derived = true;
         }

         memcpy(this->iv, (unsigned char*)in_buf + PBKDF2_SALT_LENGTH, (size_t)AES_GCM_IV_LENGTH);
         in_buf = (unsigned char*)in_buf + PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH;
         in_size -= (PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH);
      }

      if (!(this->ctx = EVP_CIPHER_CTX_new()))
      {
         pgmoneta_log_error("EVP_CIPHER_CTX_new: Failed to get context");
         goto error;
      }

      if (EVP_CipherInit_ex(this->ctx, this->cipher_fp(), NULL, this->key, this->iv, enc) == 0)
      {
         pgmoneta_log_error("EVP_CipherInit_ex: failed to initialize context");
         goto error;
      }
   }

   if (in_size > INT_MAX)
   {
      pgmoneta_log_error("aes_encryptor_process: input size exceeds INT_MAX");
      goto error;
   }

   required = in_size;

   if (required > SIZE_MAX - (size_t)this->cipher_block_size)
   {
      goto error;
   }
   required += (size_t)this->cipher_block_size;

   if (last_chunk)
   {
      if (required > SIZE_MAX - (size_t)this->cipher_block_size)
      {
         goto error;
      }
      required += (size_t)this->cipher_block_size;
      if (required > SIZE_MAX - (size_t)GCM_TAG_LENGTH)
      {
         goto error;
      }
      required += (size_t)GCM_TAG_LENGTH;
   }

   if (write_header)
   {
      if (required > SIZE_MAX - (PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH))
      {
         goto error;
      }
      required += PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH;
   }

   if (ensure_capacity(&this->out_buf, &this->out_capacity, required))
   {
      pgmoneta_log_error("aes_encryptor_process: failed to ensure buffer capacity");
      goto error;
   }

   if (write_header)
   {
      memcpy(this->out_buf, this->salt, PBKDF2_SALT_LENGTH);
      memcpy(this->out_buf + PBKDF2_SALT_LENGTH, this->iv, (size_t)AES_GCM_IV_LENGTH);
      offset += PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH;
   }

   if (enc)
   {
      if (EVP_CipherUpdate(this->ctx, this->out_buf + offset, &size, in_buf, (int)in_size) == 0)
      {
         pgmoneta_log_error("EVP_CipherUpdate: failed to process block");
         goto error;
      }

      if (last_chunk)
      {
         if (EVP_CipherFinal_ex(this->ctx, this->out_buf + size + offset, &final_size) == 0)
         {
            pgmoneta_log_error("EVP_CipherFinal_ex: failed to process final cipher block");
            goto error;
         }
         size += final_size;

         if (EVP_CIPHER_CTX_ctrl(this->ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_LENGTH, tag) == 0)
         {
            pgmoneta_log_error("EVP_CIPHER_CTX_ctrl: failed to get GCM tag");
            goto error;
         }
         memcpy(this->out_buf + size + offset, tag, (size_t)GCM_TAG_LENGTH);
         size += GCM_TAG_LENGTH;
      }
   }
   else
   {
      /* Decryption sliding window for GCM tag */
      size_t total_in = this->tag_buffer_size + in_size;
      if (total_in <= (size_t)GCM_TAG_LENGTH)
      {
         /* All current input + what we had is still potentially just the tag */
         memcpy(this->tag_buffer + this->tag_buffer_size, in_buf, in_size);
         this->tag_buffer_size += in_size;
         size = 0;
      }
      else
      {
         /* We have more than GCM_TAG_LENGTH bytes. */
         size_t to_decrypt = total_in - (size_t)GCM_TAG_LENGTH;
         size_t from_tag_buf = (to_decrypt < this->tag_buffer_size) ? to_decrypt : this->tag_buffer_size;
         size_t from_in_buf = to_decrypt - from_tag_buf;
         int out_len;

         size = 0;
         /* Decrypt from tag_buffer */
         if (from_tag_buf > 0)
         {
            if (EVP_CipherUpdate(this->ctx, this->out_buf + offset, &out_len, this->tag_buffer, (int)from_tag_buf) == 0)
            {
               pgmoneta_log_error("EVP_CipherUpdate: failed to process tag_buffer block");
               goto error;
            }
            size += out_len;
         }

         /* Decrypt from in_buf */
         if (from_in_buf > 0)
         {
            if (EVP_CipherUpdate(this->ctx, this->out_buf + offset + size, &out_len, in_buf, (int)from_in_buf) == 0)
            {
               pgmoneta_log_error("EVP_CipherUpdate: failed to process in_buf block");
               goto error;
            }
            size += out_len;
         }

         /* Update tag_buffer to hold the last GCM_TAG_LENGTH bytes */
         if (in_size >= (size_t)GCM_TAG_LENGTH)
         {
            /* All new tag_buffer bytes come from the end of in_buf */
            memcpy(this->tag_buffer, (unsigned char*)in_buf + (in_size - GCM_TAG_LENGTH), (size_t)GCM_TAG_LENGTH);
         }
         else
         {
            /* New tag_buffer is a mix of old tag_buffer and in_buf */
            size_t keep_old = (size_t)GCM_TAG_LENGTH - in_size;
            memmove(this->tag_buffer, this->tag_buffer + (this->tag_buffer_size - keep_old), keep_old);
            memcpy(this->tag_buffer + keep_old, in_buf, in_size);
         }
         this->tag_buffer_size = (size_t)GCM_TAG_LENGTH;
      }

      if (last_chunk)
      {
         if (this->tag_buffer_size != (size_t)GCM_TAG_LENGTH)
         {
            pgmoneta_log_error("aes_encryptor_process: GCM tag missing or truncated");
            goto error;
         }
         if (EVP_CIPHER_CTX_ctrl(this->ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_LENGTH, this->tag_buffer) == 0)
         {
            pgmoneta_log_error("EVP_CIPHER_CTX_ctrl: failed to set GCM tag");
            goto error;
         }

         if (EVP_CipherFinal_ex(this->ctx, this->out_buf + size + offset, &final_size) == 0)
         {
            pgmoneta_log_error("EVP_CipherFinal_ex: failed to process final cipher block");
            goto error;
         }
         size += final_size;
      }
   }

   *out_buf = (void*)this->out_buf;
   *out_size = (size_t)(size + offset);

   if (master_key != NULL)
   {
      pgmoneta_cleanse(master_key, master_key_length);
      free(master_key);
   }

   return 0;

error:

   if (master_key != NULL)
   {
      pgmoneta_cleanse(master_key, master_key_length);
      free(master_key);
   }
   if (out_buf != NULL)
   {
      *out_buf = NULL;
   }
   if (out_size != NULL)
   {
      *out_size = 0;
   }

   return 1;
}

static void
noop_encryptor_close(struct encryptor* encryptor)
{
   struct noop_encryptor* this = (struct noop_encryptor*)encryptor;

   if (this == NULL)
   {
      return;
   }

   free(this->out_buf);
   this->out_buf = NULL;
   this->out_capacity = 0;
}

static int
noop_encryptor_encrypt(struct encryptor* encryptor, void* in_buf, size_t in_size, bool last_chunk, void** out_buf, size_t* out_size)
{
   struct noop_encryptor* this = (struct noop_encryptor*)encryptor;

   if (this == NULL || in_buf == NULL || in_size == 0)
   {
      goto error;
   }

   (void)last_chunk;

   if (ensure_capacity(&this->out_buf, &this->out_capacity, in_size))
   {
      pgmoneta_log_error("noop_encryptor_encrypt: failed to ensure buffer capacity");
      goto error;
   }

   memcpy(this->out_buf, in_buf, in_size);
   *out_buf = (void*)this->out_buf;
   *out_size = in_size;

   return 0;

error:

   if (out_buf != NULL)
   {
      *out_buf = NULL;
   }
   if (out_size != NULL)
   {
      *out_size = 0;
   }

   return 1;
}

static int
noop_encryptor_decrypt(struct encryptor* encryptor, void* in_buf, size_t in_size, bool last_chunk, void** out_buf, size_t* out_size)
{
   struct noop_encryptor* this = (struct noop_encryptor*)encryptor;

   if (this == NULL || in_buf == NULL || in_size == 0)
   {
      goto error;
   }

   (void)last_chunk;

   if (ensure_capacity(&this->out_buf, &this->out_capacity, in_size))
   {
      pgmoneta_log_error("noop_encryptor_decrypt: failed to ensure buffer capacity");
      goto error;
   }

   memcpy(this->out_buf, in_buf, in_size);
   *out_buf = (void*)this->out_buf;
   *out_size = in_size;

   return 0;

error:

   if (out_buf != NULL)
   {
      *out_buf = NULL;
   }
   if (out_size != NULL)
   {
      *out_size = 0;
   }

   return 1;
}

static int
get_key_length(int mode)
{
   switch (mode)
   {
      case ENCRYPTION_AES_128_GCM:
         return 16;
      case ENCRYPTION_AES_192_GCM:
         return 24;
      case ENCRYPTION_AES_256_GCM:
         return 32;
      default:
         return 0;
   }
}

bool
pgmoneta_is_encrypted(char* file_path)
{
   if (pgmoneta_ends_with(file_path, ".aes"))
   {
      return true;
   }

   return false;
}

static int
dispatch_aes_operation(int server, char* from, char* to, int enc, struct workers* workers)
{
   struct aes_operation_task* task = NULL;

   task = (struct aes_operation_task*)malloc(sizeof(struct aes_operation_task));
   if (task == NULL)
   {
      return 1;
   }

   memset(task, 0, sizeof(struct aes_operation_task));

   memcpy(task->from, from, strlen(from));
   memcpy(task->to, to, strlen(to));
   task->enc = enc;
   task->common.workers = workers;
   task->server = server;
   task->progress_enabled = (server >= 0 && pgmoneta_is_progress_enabled(server));

   if (workers != NULL)
   {
      if (workers->outcome)
      {
         if (pgmoneta_workers_add(workers, do_aes_operation, (struct worker_common*)task))
         {
            free(task);
            return 1;
         }
      }
      else
      {
         do_aes_operation((struct worker_common*)task);
      }
   }
   else
   {
      do_aes_operation((struct worker_common*)task);
   }

   return 0;
}

static void
do_aes_operation(struct worker_common* wc)
{
   struct aes_operation_task* task = (struct aes_operation_task*)wc;
   int result;

   if (task->enc)
   {
      result = pgmoneta_encrypt_file(task->from, task->to, NULL);
   }
   else
   {
      result = pgmoneta_decrypt_file(task->from, task->to, NULL);
   }

   if (result != 0 && task->common.workers != NULL)
   {
      task->common.workers->outcome = false;
   }

   if (task->progress_enabled)
   {
      pgmoneta_progress_increment(task->server, 1);
   }

   free(task);
}

/**
 * Automatically clear the AES cache on process exit
 */
__attribute__((destructor)) static void
pgmoneta_aes_destructor(void)
{
   pgmoneta_clear_aes_cache();
}
