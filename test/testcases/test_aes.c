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
#include <configuration.h>
#include <mctf.h>
#include <shmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utils.h>
#include <unistd.h>
#include <sys/stat.h>
#include <openssl/evp.h>

static void
setup_test_salt(void)
{
   unsigned char salt[PBKDF2_SALT_LENGTH] = {0};
   pgmoneta_set_master_salt(salt);
}

struct test_env
{
   char test_home[MAX_PATH];
   char* original_home;
   char original_config_home[MAX_PATH];
   int original_encryption;
   bool shmem_locally_allocated;
};

static int
setup_mock_master_key(struct test_env* env)
{
   char pgmoneta_dir[MAX_PATH] = {0};
   char master_key_file[MAX_PATH] = {0};
   FILE* f = NULL;
   struct main_configuration* config = NULL;

   memset(env, 0, sizeof(struct test_env));
   env->original_home = getenv("HOME") ? strdup(getenv("HOME")) : NULL;

   if (shmem == NULL)
   {
      size_t shmem_size = sizeof(struct main_configuration);
      if (pgmoneta_create_shared_memory(shmem_size, HUGEPAGE_OFF, &shmem))
      {
         return 1;
      }
      pgmoneta_init_main_configuration(shmem);
      env->shmem_locally_allocated = true;
   }

   config = (struct main_configuration*)shmem;
   env->original_encryption = config->encryption;
   config->encryption = ENCRYPTION_AES_256_GCM;

   pgmoneta_snprintf(env->test_home, MAX_PATH, "%s/test_aes_home_XXXXXX", "/tmp");
   if (mkdtemp(env->test_home) == NULL)
   {
      return 1;
   }

   pgmoneta_snprintf(env->original_config_home, sizeof(env->original_config_home), "%s", config->common.home_dir);
   memset(config->common.home_dir, 0, sizeof(config->common.home_dir));
   pgmoneta_snprintf(config->common.home_dir, sizeof(config->common.home_dir), "%s", env->test_home);

   pgmoneta_snprintf(pgmoneta_dir, MAX_PATH, "%s/.pgmoneta", env->test_home);
   if (mkdir(pgmoneta_dir, 0700))
   {
      return 1;
   }

   pgmoneta_snprintf(master_key_file, MAX_PATH, "%s/master.key", pgmoneta_dir);
   f = fopen(master_key_file, "wb");
   if (f == NULL)
   {
      return 1;
   }
   /* Base64 encoded "pgmoneta-test-password" is "cGdtb25ldGEtdGVzdC1wYXNzd29yZA==" */
   fprintf(f, "cGdtb25ldGEtdGVzdC1wYXNzd29yZA==\n");
   /* Base64 encoded 16-byte zero salt */
   fprintf(f, "AAAAAAAAAAAAAAAAAAAAAA==\n");
   fclose(f);
   chmod(master_key_file, 0600);

   setenv("HOME", env->test_home, 1);
   return 0;
}

static void
teardown_mock_master_key(struct test_env* env)
{
   char pgmoneta_dir[MAX_PATH] = {0};
   char master_key_file[MAX_PATH] = {0};
   struct main_configuration* config = (struct main_configuration*)shmem;

   if (env->original_home)
   {
      setenv("HOME", env->original_home, 1);
      free(env->original_home);
   }
   else
   {
      unsetenv("HOME");
   }

   if (config != NULL)
   {
      config->encryption = env->original_encryption;
      memset(config->common.home_dir, 0, sizeof(config->common.home_dir));
      pgmoneta_snprintf(config->common.home_dir, sizeof(config->common.home_dir), "%s", env->original_config_home);
   }

   if (env->test_home[0] != '\0')
   {
      pgmoneta_snprintf(pgmoneta_dir, MAX_PATH, "%s/.pgmoneta", env->test_home);
      pgmoneta_snprintf(master_key_file, MAX_PATH, "%s/master.key", pgmoneta_dir);
      remove(master_key_file);
      rmdir(pgmoneta_dir);
      rmdir(env->test_home);
   }

   if (env->shmem_locally_allocated && shmem != NULL)
   {
      pgmoneta_destroy_shared_memory(shmem, sizeof(struct main_configuration));
      shmem = NULL;
   }
}

/**
 * Test: AES-256-GCM encrypt/decrypt round-trip.
 */
MCTF_TEST(test_aes_256_gcm_roundtrip)
{
   char* plaintext = "pgmoneta-test-gcm-round-trip";
   char* password = "pgmoneta-test-password";

   setup_test_salt();
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;

   MCTF_ASSERT(pgmoneta_encrypt(plaintext, password, strlen(password), &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_encrypt with AES-256-GCM should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext, cleanup, "ciphertext should not be NULL");

   MCTF_ASSERT(pgmoneta_decrypt(ciphertext, ciphertext_length, password, strlen(password), &decrypted, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_decrypt with AES-256-GCM should succeed");
   MCTF_ASSERT_PTR_NONNULL(decrypted, cleanup, "decrypted should not be NULL");
   MCTF_ASSERT_STR_EQ(decrypted, plaintext, cleanup, "decrypted text should match original for AES-256-GCM");

cleanup:
   free(ciphertext);
   free(decrypted);
   MCTF_FINISH();
}

/**
 * Test: AES-GCM authentication failure.
 *
 * Encrypts data with GCM, modifies the ciphertext (bit-flip),
 * and verifies that decryption fails due to tag mismatch.
 */
MCTF_TEST(test_aes_gcm_authentication_failure)
{
   char* plaintext = "highly-sensitive-data-for-gcm-test";
   char* password = "pgmoneta-test-password";

   setup_test_salt();
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;

   MCTF_ASSERT(pgmoneta_encrypt(plaintext, password, strlen(password), &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_encrypt should succeed");
   MCTF_ASSERT(ciphertext_length > PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + GCM_TAG_LENGTH, cleanup, "ciphertext_length should be greater than salt + IV + tag size");

   /* Flip a bit in the encrypted data area (between IV and tag) */
   /* Format: [salt(16)][iv(12)][data...][tag(16)] */
   ciphertext[PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH] ^= 0x01;

   MCTF_ASSERT(pgmoneta_decrypt(ciphertext, ciphertext_length, password, strlen(password), &decrypted, ENCRYPTION_AES_256_GCM) != 0, cleanup, "pgmoneta_decrypt should fail if ciphertext is tampered (GCM)");
   MCTF_ASSERT_PTR_NULL(decrypted, cleanup, "decrypted should be NULL on authentication failure");

cleanup:
   free(ciphertext);
   free(decrypted);
   MCTF_FINISH();
}

/**
 * Test: AES-GCM tag tampering.
 */
MCTF_TEST(test_aes_gcm_tag_tampering_fails)
{
   char* plaintext = "data-to-protect";
   char* password = "pgmoneta-test-password";
   setup_test_salt();
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;

   MCTF_ASSERT(pgmoneta_encrypt(plaintext, password, strlen(password), &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_encrypt should succeed");
   MCTF_ASSERT(ciphertext_length > PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + GCM_TAG_LENGTH, cleanup, "ciphertext_length should be greater than salt + iv + tag size");

   /* Tamper with the GCM tag (at the very end of the ciphertext) */
   ciphertext[ciphertext_length - 1] ^= 0xFF;

   MCTF_ASSERT(pgmoneta_decrypt(ciphertext, ciphertext_length, password, strlen(password), &decrypted, ENCRYPTION_AES_256_GCM) != 0, cleanup, "pgmoneta_decrypt should fail if tag is tampered");

cleanup:
   free(ciphertext);
   free(decrypted);
   MCTF_FINISH();
}

/**
 * Test: Buffer encrypt/decrypt round-trip (wire protocol).
 */
MCTF_TEST(test_aes_buffer_roundtrip)
{
   struct test_env env;
   char* plaintext = "wire-protocol-buffer-test";
   size_t plaintext_len = strlen(plaintext);
   unsigned char* ciphertext = NULL;
   size_t ciphertext_len = 0;
   unsigned char* decrypted = NULL;
   size_t decrypted_len = 0;

   MCTF_ASSERT(setup_mock_master_key(&env) == 0, cleanup, "Failed to setup mock environment");

   /* Test AES-256-GCM (default for management) */
   MCTF_ASSERT(pgmoneta_encrypt_buffer((unsigned char*)plaintext, plaintext_len, &ciphertext, &ciphertext_len, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_encrypt_buffer should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext, cleanup, "ciphertext should not be NULL");
   MCTF_ASSERT(ciphertext_len > PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + GCM_TAG_LENGTH, cleanup, "ciphertext should contain salt, IV and tag");

   MCTF_ASSERT(pgmoneta_decrypt_buffer(ciphertext, ciphertext_len, &decrypted, &decrypted_len, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_decrypt_buffer should succeed");
   MCTF_ASSERT_PTR_NONNULL(decrypted, cleanup, "decrypted buffer should not be NULL");
   MCTF_ASSERT(decrypted_len == plaintext_len, cleanup, "decrypted length should match original");
   MCTF_ASSERT(memcmp(decrypted, plaintext, plaintext_len) == 0, cleanup, "decrypted content should match original");

cleanup:
   free(ciphertext);
   free(decrypted);
   teardown_mock_master_key(&env);
   MCTF_FINISH();
}

/**
 * Test: Buffer encryption tamper detection.
 */
MCTF_TEST_NEGATIVE(test_aes_buffer_tamper_fails)
{
   struct test_env env;
   char* plaintext = "tamper-test-data";
   size_t plaintext_len = strlen(plaintext);
   unsigned char* ciphertext = NULL;
   size_t ciphertext_len = 0;
   unsigned char* decrypted = NULL;
   size_t decrypted_len = 0;

   MCTF_ASSERT(setup_mock_master_key(&env) == 0, cleanup, "Failed to setup mock environment");

   MCTF_ASSERT(pgmoneta_encrypt_buffer((unsigned char*)plaintext, plaintext_len, &ciphertext, &ciphertext_len, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_encrypt_buffer should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext, cleanup, "ciphertext should not be NULL");
   MCTF_ASSERT(ciphertext_len > PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + GCM_TAG_LENGTH, cleanup, "ciphertext should contain salt, IV and tag");

   /* Tamper with the tag area (last byte of tag) */
   /* Format: [salt(16)][iv(12)][ciphertext...][tag(16)] */
   ciphertext[ciphertext_len - 1] ^= 0x42;

   MCTF_ASSERT(pgmoneta_decrypt_buffer(ciphertext, ciphertext_len, &decrypted, &decrypted_len, ENCRYPTION_AES_256_GCM) != 0, cleanup, "pgmoneta_decrypt_buffer should fail on tampered tag");
   MCTF_ASSERT_PTR_NULL(decrypted, cleanup, "decrypted buffer should be NULL on failure");

cleanup:
   free(ciphertext);
   free(decrypted);
   teardown_mock_master_key(&env);
   MCTF_FINISH();
}

/**
 * Test: Buffer encryption with empty payload.
 */
MCTF_TEST(test_aes_buffer_empty_payload)
{
   struct test_env env;
   char* plaintext = "";
   size_t plaintext_len = 0;
   unsigned char* ciphertext = NULL;
   size_t ciphertext_len = 0;
   unsigned char* decrypted = NULL;
   size_t decrypted_len = 0;

   MCTF_ASSERT(setup_mock_master_key(&env) == 0, cleanup, "Failed to setup mock environment");

   MCTF_ASSERT(pgmoneta_encrypt_buffer((unsigned char*)plaintext, plaintext_len, &ciphertext, &ciphertext_len, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_encrypt_buffer should succeed for empty payload");
   MCTF_ASSERT(ciphertext_len == PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + GCM_TAG_LENGTH, cleanup, "ciphertext_len should be exactly salt + IV + tag");

   MCTF_ASSERT(pgmoneta_decrypt_buffer(ciphertext, ciphertext_len, &decrypted, &decrypted_len, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_decrypt_buffer should succeed for empty payload");
   MCTF_ASSERT(decrypted_len == 0, cleanup, "decrypted_len should be 0");

cleanup:
   free(ciphertext);
   free(decrypted);
   teardown_mock_master_key(&env);
   MCTF_FINISH();
}

/**
 * Test: AES-GCM file encryption/decryption round-trip.
 * This test verifies the fix for the GCM file decryption loop bug.
 */
MCTF_TEST(test_aes_file_gcm_roundtrip)
{
   struct test_env env;
   char* plaintext = "This is a comprehensive test for file-based encryption and decryption in GCM mode. It ensures that the trailing authentication tag is correctly handled and not included in the decryption stream.";
   char from[MAX_PATH] = {0};
   char encrypted[MAX_PATH] = {0};
   char decrypted[MAX_PATH] = {0};
   FILE* f = NULL;
   char* decrypted_content = NULL;
   size_t size = 0;

   MCTF_ASSERT(setup_mock_master_key(&env) == 0, cleanup, "Failed to setup mock environment");

   /* Setup test files */
   pgmoneta_snprintf(from, MAX_PATH, "%s/plaintext.txt", env.test_home);
   pgmoneta_snprintf(encrypted, MAX_PATH, "%s/encrypted.aes", env.test_home);
   pgmoneta_snprintf(decrypted, MAX_PATH, "%s/decrypted.txt", env.test_home);

   f = fopen(from, "wb");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "Failed to create test file");
   fwrite(plaintext, 1, strlen(plaintext), f);
   fclose(f);
   f = NULL;

   /* Encrypt */
   MCTF_ASSERT(pgmoneta_encrypt_file(from, encrypted, NULL) == 0, cleanup, "pgmoneta_encrypt_file failed");
   MCTF_ASSERT(pgmoneta_exists(encrypted), cleanup, "Encrypted file does not exist");

   /* Decrypt */
   MCTF_ASSERT(pgmoneta_decrypt_file(encrypted, decrypted, NULL) == 0, cleanup, "pgmoneta_decrypt_file failed");
   MCTF_ASSERT(pgmoneta_exists(decrypted), cleanup, "Decrypted file does not exist");

   /* Verify */
   f = fopen(decrypted, "rb");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "Failed to open decrypted file");
   {
      long file_size;
      fseek(f, 0, SEEK_END);
      file_size = ftell(f);
      MCTF_ASSERT(file_size >= 0, cleanup, "Failed to determine decrypted file size");
      size = (size_t)file_size;
      fseek(f, 0, SEEK_SET);
   }
   decrypted_content = malloc(size + 1);
   MCTF_ASSERT_PTR_NONNULL(decrypted_content, cleanup, "Failed to allocate memory for decrypted content");
   MCTF_ASSERT(fread(decrypted_content, 1, size, f) == size, cleanup, "Failed to read decrypted file");
   decrypted_content[size] = '\0';
   fclose(f);
   f = NULL;

   MCTF_ASSERT_STR_EQ(decrypted_content, plaintext, cleanup, "Decrypted content mismatch");

cleanup:
   if (f)
      fclose(f);

   /* Deep cleanup of temp files in home */
   if (from[0] != '\0')
      remove(from);
   if (encrypted[0] != '\0')
      remove(encrypted);
   if (decrypted[0] != '\0')
      remove(decrypted);

   teardown_mock_master_key(&env);
   free(decrypted_content);
   MCTF_FINISH();
}

/**
 * Test: AES-128-GCM encrypt/decrypt round-trip.
 */
MCTF_TEST(test_aes_128_gcm_roundtrip)
{
   char* plaintext = "pgmoneta-test-128-gcm";
   char* password = "pgmoneta-test-password";
   setup_test_salt();
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;

   MCTF_ASSERT(pgmoneta_encrypt(plaintext, password, strlen(password), &ciphertext, &ciphertext_length, ENCRYPTION_AES_128_GCM) == 0, cleanup, "pgmoneta_encrypt with AES-128-GCM should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext, cleanup, "ciphertext should not be NULL");

   MCTF_ASSERT(pgmoneta_decrypt(ciphertext, ciphertext_length, password, strlen(password), &decrypted, ENCRYPTION_AES_128_GCM) == 0, cleanup, "pgmoneta_decrypt with AES-128-GCM should succeed");
   MCTF_ASSERT_PTR_NONNULL(decrypted, cleanup, "decrypted pointer should not be NULL");
   MCTF_ASSERT_STR_EQ(decrypted, plaintext, cleanup, "decrypted text should match original for AES-128-GCM");

cleanup:
   free(ciphertext);
   free(decrypted);
   MCTF_FINISH();
}

/**
 * Test: AES-192-GCM encrypt/decrypt round-trip.
 */
MCTF_TEST(test_aes_192_gcm_roundtrip)
{
   char* plaintext = "pgmoneta-test-192-gcm";
   char* password = "pgmoneta-test-password";
   setup_test_salt();
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;

   MCTF_ASSERT(pgmoneta_encrypt(plaintext, password, strlen(password), &ciphertext, &ciphertext_length, ENCRYPTION_AES_192_GCM) == 0, cleanup, "pgmoneta_encrypt with AES-192-GCM should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext, cleanup, "ciphertext should not be NULL");

   MCTF_ASSERT(pgmoneta_decrypt(ciphertext, ciphertext_length, password, strlen(password), &decrypted, ENCRYPTION_AES_192_GCM) == 0, cleanup, "pgmoneta_decrypt with AES-192-GCM should succeed");
   MCTF_ASSERT_PTR_NONNULL(decrypted, cleanup, "decrypted pointer should not be NULL");
   MCTF_ASSERT_STR_EQ(decrypted, plaintext, cleanup, "decrypted text should match original for AES-192-GCM");

cleanup:
   free(ciphertext);
   free(decrypted);
   MCTF_FINISH();
}

/**
 * Test: Random salt producing unique ciphertexts for same plaintext.
 */
MCTF_TEST(test_aes_salt_produces_unique_ciphertext)
{
   char* plaintext = "same-plaintext";
   char* password = "pgmoneta-test-password";
   setup_test_salt();
   char* ciphertext_a = NULL;
   int ciphertext_a_len = 0;
   char* ciphertext_b = NULL;
   int ciphertext_b_len = 0;

   MCTF_ASSERT(pgmoneta_encrypt(plaintext, password, strlen(password), &ciphertext_a, &ciphertext_a_len, ENCRYPTION_AES_256_GCM) == 0, cleanup, "First encryption should succeed");
   MCTF_ASSERT(pgmoneta_encrypt(plaintext, password, strlen(password), &ciphertext_b, &ciphertext_b_len, ENCRYPTION_AES_256_GCM) == 0, cleanup, "Second encryption should succeed");

   MCTF_ASSERT(ciphertext_a_len == ciphertext_b_len, cleanup, "Ciphertext lengths should match");
   MCTF_ASSERT(memcmp(ciphertext_a, ciphertext_b, ciphertext_a_len) != 0, cleanup, "Ciphertexts should be unique due to different salts");

cleanup:
   free(ciphertext_a);
   free(ciphertext_b);
   MCTF_FINISH();
}

/**
 * Test: Decryption with wrong password fails and returns NULL.
 */
MCTF_TEST(test_aes_decrypt_wrong_password_no_leak)
{
   char* plaintext = "top-secret";
   char* password = "pgmoneta-test-password";
   char* wrong_password = "wrong-password";
   setup_test_salt();
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;

   MCTF_ASSERT(pgmoneta_encrypt(plaintext, password, strlen(password), &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_GCM) == 0, cleanup, "Encryption should succeed");
   MCTF_ASSERT(pgmoneta_decrypt(ciphertext, ciphertext_length, wrong_password, strlen(wrong_password), &decrypted, ENCRYPTION_AES_256_GCM) != 0, cleanup, "Decryption with wrong password should fail");
   MCTF_ASSERT_PTR_NULL(decrypted, cleanup, "Decrypted pointer should be NULL on failure");

cleanup:
   free(ciphertext);
   free(decrypted);
   MCTF_FINISH();
}

/**
 * Test: Decryption of truncated ciphertext fails.
 */
MCTF_TEST(test_aes_decrypt_truncated_ciphertext_fails)
{
   char* plaintext = "long-plaintext-for-truncation-test";
   char* password = "pgmoneta-test-password";
   setup_test_salt();
   char* ciphertext = NULL;
   int ciphertext_length = 0;
   char* decrypted = NULL;

   MCTF_ASSERT(pgmoneta_encrypt(plaintext, password, strlen(password), &ciphertext, &ciphertext_length, ENCRYPTION_AES_256_GCM) == 0, cleanup, "Encryption should succeed");

   /* Truncate to just the salt + half IV */
   MCTF_ASSERT(pgmoneta_decrypt(ciphertext, PBKDF2_SALT_LENGTH + 6, password, strlen(password), &decrypted, ENCRYPTION_AES_256_GCM) != 0, cleanup, "Decryption of truncated IV should fail");
   MCTF_ASSERT_PTR_NULL(decrypted, cleanup, "Decrypted pointer should be NULL");

   /* Truncate to salt + IV + partial data (no tag) */
   MCTF_ASSERT(pgmoneta_decrypt(ciphertext, PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + 5, password, strlen(password), &decrypted, ENCRYPTION_AES_256_GCM) != 0, cleanup, "Decryption without tag should fail");
   MCTF_ASSERT_PTR_NULL(decrypted, cleanup, "Decrypted pointer should be NULL");

cleanup:
   free(ciphertext);
   free(decrypted);
   MCTF_FINISH();
}

MCTF_TEST(test_aes_is_encrypted)
{
   MCTF_ASSERT(pgmoneta_is_encrypted("file.aes"), cleanup, "is_encrypted positive failed");
   MCTF_ASSERT(!pgmoneta_is_encrypted("file.txt"), cleanup, "is_encrypted negative failed");
   MCTF_ASSERT(!pgmoneta_is_encrypted(NULL), cleanup, "is_encrypted NULL failed");

cleanup:
   MCTF_FINISH();
}
