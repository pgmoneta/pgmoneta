/*
 * Copyright (C) 2023 Red Hat
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

#include <aes.h>
#include <logging.h>
#include <pgmoneta.h>
#include <security.h>
#include <utils.h>
/* System */
#include <dirent.h>

#define ENC_BUF_SIZE (1024 * 1024)

static int encrypt_file(char* from, char* to, int enc);
static int derive_key_iv(char* password, unsigned char* key, unsigned char* iv, int mode);
static int aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length, int mode);
static int aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext, int mode);
static const EVP_CIPHER* (*get_cipher(int mode))(void);

int
pgmoneta_encrypt_data(char* d)
{
   char* from = NULL;
   char* to = NULL;
   DIR* dir;
   struct dirent* entry;

   if (!(dir = opendir(d)))
   {
      return 1;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", d, entry->d_name);

         pgmoneta_encrypt_data(path);
      }
      else
      {
         if (!pgmoneta_ends_with(entry->d_name, ".aes") &&
             !pgmoneta_ends_with(entry->d_name, ".partial") &&
             !pgmoneta_ends_with(entry->d_name, ".history"))
         {
            from = NULL;

            from = pgmoneta_append(from, d);
            from = pgmoneta_append(from, "/");
            from = pgmoneta_append(from, entry->d_name);

            to = NULL;

            to = pgmoneta_append(to, d);
            to = pgmoneta_append(to, "/");
            to = pgmoneta_append(to, entry->d_name);
            to = pgmoneta_append(to, ".aes");

            if (pgmoneta_exists(from))
            {
               encrypt_file(from, to, 1);
               pgmoneta_delete_file(from);
            }

            free(from);
            free(to);
         }
      }
   }

   closedir(dir);
   return 0;
}

int
pgmoneta_encrypt_wal(char* d)
{
   char* from = NULL;
   char* to = NULL;
   DIR* dir;
   struct dirent* entry;
   char* compress_suffix = NULL;
   struct configuration* config;

   config = (struct configuration*)shmem;
   switch (config->compression_type)
   {
      case COMPRESSION_GZIP:
         compress_suffix = ".gz";
         break;
      case COMPRESSION_ZSTD:
         compress_suffix = ".zstd";
         break;
      case COMPRESSION_LZ4:
         compress_suffix = ".lz4";
         break;
      case COMPRESSION_BZIP2:
         compress_suffix = ".bz2";
         break;
      case COMPRESSION_NONE:
         compress_suffix = "";
         break;
      default:
         pgmoneta_log_error("encryption_execute: Unknown compression type");
         break;
   }

   if (!(dir = opendir(d)))
   {
      return 1;
   }
   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_REG)
      {
         if (!pgmoneta_ends_with(entry->d_name, compress_suffix))
         {
            continue;
         }

         from = NULL;

         from = pgmoneta_append(from, d);
         from = pgmoneta_append(from, "/");
         from = pgmoneta_append(from, entry->d_name);

         to = NULL;

         to = pgmoneta_append(to, d);
         to = pgmoneta_append(to, "/");
         to = pgmoneta_append(to, entry->d_name);
         to = pgmoneta_append(to, ".aes");

         if (pgmoneta_exists(from))
         {
            encrypt_file(from, to, 1);
            pgmoneta_delete_file(from);
            pgmoneta_permission(to, 6, 0, 0);
         }

         free(from);
         free(to);
      }
   }

   closedir(dir);
   return 0;
}

int
pgmoneta_encrypt_file(char* from, char* to)
{
   int flag = 0;
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

   encrypt_file(from, to, 1);
   pgmoneta_delete_file(from);
   if (flag)
   {
      free(to);
   }
   return 0;
}

int
pgmoneta_decrypt_data(char* d)
{
   char* from = NULL;
   char* to = NULL;
   char* name = NULL;
   DIR* dir;
   struct dirent* entry;

   if (!(dir = opendir(d)))
   {
      pgmoneta_log_error("pgmoneta_decrypt_data: Could not open directory %s", d);
      return 1;
   }

   while ((entry = readdir(dir)) != NULL)
   {
      if (entry->d_type == DT_DIR)
      {
         char path[1024];

         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         {
            continue;
         }

         snprintf(path, sizeof(path), "%s/%s", d, entry->d_name);

         pgmoneta_decrypt_data(path);
      }
      else
      {
         if (pgmoneta_ends_with(entry->d_name, ".aes"))
         {
            from = NULL;

            from = pgmoneta_append(from, d);
            from = pgmoneta_append(from, "/");
            from = pgmoneta_append(from, entry->d_name);

            name = malloc(strlen(entry->d_name) - 3);
            memset(name, 0, strlen(entry->d_name) - 3);
            memcpy(name, entry->d_name, strlen(entry->d_name) - 4);

            to = NULL;

            to = pgmoneta_append(to, d);
            to = pgmoneta_append(to, "/");
            to = pgmoneta_append(to, name);

            encrypt_file(from, to, 0);
            pgmoneta_delete_file(from);

            free(name);
            free(from);
            free(to);
         }
      }
   }

   closedir(dir);
   return 0;
}

int
pgmoneta_decrypt_archive(char* path)
{
   char* to = NULL;

   if (!pgmoneta_exists(path))
   {
      pgmoneta_log_error("pgmoneta_decrypt_archive: file not exist: %s", path);
      return 1;

   }

   to = malloc(strlen(path) - 3);
   memset(to, 0, strlen(path) - 3);
   memcpy(to, path, strlen(path) - 4);

   if (encrypt_file(path, to, 0))
   {
      pgmoneta_log_error("pgmoneta_decrypt_archive: error on decrypt file");
      return 1;
   }

   pgmoneta_delete_file(path);
   free(to);
   return 0;
}

int
pgmoneta_encrypt(char* plaintext, char* password, char** ciphertext, int* ciphertext_length, int mode)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (derive_key_iv(password, key, iv, mode) != 0)
   {
      return 1;
   }

   return aes_encrypt(plaintext, key, iv, ciphertext, ciphertext_length, mode);
}

int
pgmoneta_decrypt(char* ciphertext, int ciphertext_length, char* password, char** plaintext, int mode)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];

   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));

   if (derive_key_iv(password, key, iv, mode) != 0)
   {
      return 1;
   }

   return aes_decrypt(ciphertext, ciphertext_length, key, iv, plaintext, mode);
}

// [private]
static int
derive_key_iv(char* password, unsigned char* key, unsigned char* iv, int mode)
{

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
   OpenSSL_add_all_algorithms();
#endif

   if (!EVP_BytesToKey(get_cipher(mode)(), EVP_sha1(), NULL,
                       (unsigned char*) password, strlen(password), 1,
                       key, iv))
   {
      return 1;
   }

   return 0;
}

// [private]
static int
aes_encrypt(char* plaintext, unsigned char* key, unsigned char* iv, char** ciphertext, int* ciphertext_length, int mode)
{
   EVP_CIPHER_CTX* ctx = NULL;
   int length;
   size_t size;
   unsigned char* ct = NULL;
   int ct_length;
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);
   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      goto error;
   }

   if (EVP_EncryptInit_ex(ctx, cipher_fp(), NULL, key, iv) != 1)
   {
      goto error;
   }

   size = strlen(plaintext) + EVP_CIPHER_block_size(cipher_fp());
   ct = malloc(size);
   memset(ct, 0, size);

   if (EVP_EncryptUpdate(ctx,
                         ct, &length,
                         (unsigned char*)plaintext, strlen((char*)plaintext)) != 1)
   {
      goto error;
   }

   ct_length = length;

   if (EVP_EncryptFinal_ex(ctx, ct + length, &length) != 1)
   {
      goto error;
   }

   ct_length += length;

   EVP_CIPHER_CTX_free(ctx);

   *ciphertext = (char*)ct;
   *ciphertext_length = ct_length;

   return 0;

error:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }

   free(ct);

   return 1;
}

// [private]
static int
aes_decrypt(char* ciphertext, int ciphertext_length, unsigned char* key, unsigned char* iv, char** plaintext, int mode)
{
   EVP_CIPHER_CTX* ctx = NULL;
   int plaintext_length;
   int length;
   size_t size;
   char* pt = NULL;
   const EVP_CIPHER* (*cipher_fp)(void) = get_cipher(mode);

   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      goto error;
   }

   if (EVP_DecryptInit_ex(ctx, cipher_fp(), NULL, key, iv) != 1)
   {
      goto error;
   }

   size = ciphertext_length + EVP_CIPHER_block_size(cipher_fp());
   pt = malloc(size);
   memset(pt, 0, size);

   if (EVP_DecryptUpdate(ctx,
                         (unsigned char*)pt, &length,
                         (unsigned char*)ciphertext, ciphertext_length) != 1)
   {
      goto error;
   }

   plaintext_length = length;

   if (EVP_DecryptFinal_ex(ctx, (unsigned char*)pt + length, &length) != 1)
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

   free(pt);

   return 1;
}

static const EVP_CIPHER* (*get_cipher(int mode))(void)
{
   if (mode == ENCRYPTION_AES_256_CBC)
   {
      return &EVP_aes_256_cbc;
   }
   if (mode == ENCRYPTION_AES_192_CBC)
   {
      return &EVP_aes_192_cbc;
   }
   if (mode == ENCRYPTION_AES_128_CBC)
   {
      return &EVP_aes_128_cbc;
   }
   if (mode == ENCRYPTION_AES_256_CTR)
   {
      return &EVP_aes_256_ctr;
   }
   if (mode == ENCRYPTION_AES_192_CTR)
   {
      return &EVP_aes_192_ctr;
   }
   if (mode == ENCRYPTION_AES_128_CTR)
   {
      return &EVP_aes_128_ctr;
   }
   return &EVP_aes_256_cbc;
}

// enc: 1 for encrypt, 0 for decrypt
static int
encrypt_file(char* from, char* to, int enc)
{
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   char* master_key = NULL;
   EVP_CIPHER_CTX* ctx = NULL;
   struct configuration* config;
   const EVP_CIPHER* (*cipher_fp)(void) = NULL;
   int cipher_block_size = 0;
   int inbuf_size = 0;
   int outbuf_size = 0;
   FILE* in = NULL;
   FILE* out = NULL;
   int inl = 0;
   int outl = 0;
   int f_len = 0;

   config = (struct configuration*)shmem;
   cipher_fp = get_cipher(config->encryption);
   cipher_block_size = EVP_CIPHER_block_size(cipher_fp());
   inbuf_size = ENC_BUF_SIZE;
   outbuf_size = inbuf_size + cipher_block_size - 1;
   unsigned char inbuf[inbuf_size];
   unsigned char outbuf[outbuf_size];

   if (pgmoneta_get_master_key(&master_key))
   {
      pgmoneta_log_fatal("pgmoneta_get_master_key: Invalid master key");
      goto error;
   }
   memset(&key, 0, sizeof(key));
   memset(&iv, 0, sizeof(iv));
   if (derive_key_iv(master_key, key, iv, config->encryption) != 0)
   {
      pgmoneta_log_fatal("derive_key_iv: Failed to derive key and iv");
      goto error;
   }

   if (!(ctx = EVP_CIPHER_CTX_new()))
   {
      pgmoneta_log_fatal("EVP_CIPHER_CTX_new: Failed to get context");
      goto error;
   }

   in = fopen(from, "rb");
   if (in == NULL)
   {
      pgmoneta_log_error("fopen: Could not open %s", from);
      goto error;
   }

   out = fopen(to, "w");
   if (in == NULL)
   {
      pgmoneta_log_error("fopen: Could not open %s", to);
      goto error;
   }

   if (EVP_CipherInit_ex(ctx, cipher_fp(), NULL, key, iv, enc) == 0)
   {
      pgmoneta_log_error("EVP_CipherInit_ex: ailed to initialize context");
      goto error;
   }

   while ((inl = fread(inbuf, sizeof(char), inbuf_size, in)) > 0)
   {
      if (EVP_CipherUpdate(ctx, outbuf, &outl, inbuf, inl) == 0)
      {
         pgmoneta_log_error("EVP_CipherUpdate: failed to process block");
         goto error;
      }
      if (fwrite(outbuf, sizeof(char), outl, out) != outl)
      {
         pgmoneta_log_error("fwrite: failed to write cipher");
         goto error;
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
      if (fwrite(outbuf, sizeof(char), f_len, out) != f_len)
      {
         pgmoneta_log_error("fwrite: failed to write final block");
         goto error;
      }
   }

   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }
   free(master_key);
   fclose(in);
   fclose(out);
   return 0;

error:
   if (ctx)
   {
      EVP_CIPHER_CTX_free(ctx);
   }
   free(master_key);
   fclose(in);
   fclose(out);
   return 1;
}
