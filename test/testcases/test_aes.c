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
#include <management.h>
#include <mctf.h>
#include <shmem.h>
#include <tscommon.h>
#include <utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <openssl/evp.h>

static void
setup_test_salt(void)
{
   unsigned char salt[PBKDF2_SALT_LENGTH] = {0};
   pgmoneta_set_master_salt(salt);
}

static int
write_all(int fd, const void* buf, size_t size)
{
   const unsigned char* ptr = (const unsigned char*)buf;
   size_t written = 0;

   while (written < size)
   {
      ssize_t bytes = write(fd, ptr + written, size - written);

      if (bytes <= 0)
      {
         return 1;
      }

      written += (size_t)bytes;
   }

   return 0;
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
   struct test_encryption_env env;
   char* plaintext = "wire-protocol-buffer-test";
   size_t plaintext_len = strlen(plaintext);
   unsigned char* ciphertext = NULL;
   size_t ciphertext_len = 0;
   unsigned char* decrypted = NULL;
   size_t decrypted_len = 0;

   MCTF_ASSERT(pgmoneta_test_setup_encryption_env(&env) == 0, cleanup, "Failed to setup mock environment");

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
   pgmoneta_test_teardown_encryption_env(&env);
   MCTF_FINISH();
}

/**
 * Test: Buffer encryption tamper detection.
 */
MCTF_TEST_NEGATIVE(test_aes_buffer_tamper_fails)
{
   struct test_encryption_env env;
   char* plaintext = "tamper-test-data";
   size_t plaintext_len = strlen(plaintext);
   unsigned char* ciphertext = NULL;
   size_t ciphertext_len = 0;
   unsigned char* decrypted = NULL;
   size_t decrypted_len = 0;

   MCTF_ASSERT(pgmoneta_test_setup_encryption_env(&env) == 0, cleanup, "Failed to setup mock environment");

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
   pgmoneta_test_teardown_encryption_env(&env);
   MCTF_FINISH();
}

/**
 * Test: Management wire protocol rejects tampered encrypted payloads.
 */
MCTF_TEST_NEGATIVE(test_management_read_json_tampered_payload_fails)
{
   struct test_encryption_env env;
   char* plaintext = "{\"request\":\"ping\"}";
   unsigned char* ciphertext = NULL;
   size_t ciphertext_len = 0;
   char* encoded = NULL;
   size_t encoded_len = 0;
   struct json* json = NULL;
   int sockets[2] = {-1, -1};
   char header[4] = {0};
   uint8_t compression = MANAGEMENT_COMPRESSION_NONE;
   uint8_t encryption = MANAGEMENT_ENCRYPTION_NONE;

   MCTF_ASSERT(pgmoneta_test_setup_encryption_env(&env) == 0, cleanup, "Failed to setup mock environment");
   MCTF_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, cleanup, "socketpair should succeed");

   MCTF_ASSERT(pgmoneta_encrypt_buffer((unsigned char*)plaintext, strlen(plaintext), &ciphertext, &ciphertext_len, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_encrypt_buffer should succeed");
   MCTF_ASSERT_PTR_NONNULL(ciphertext, cleanup, "ciphertext should not be NULL");

   ciphertext[ciphertext_len - 1] ^= 0x24;

   MCTF_ASSERT(pgmoneta_base64_encode(ciphertext, ciphertext_len, &encoded, &encoded_len) == 0, cleanup, "pgmoneta_base64_encode should succeed");
   MCTF_ASSERT_PTR_NONNULL(encoded, cleanup, "encoded payload should not be NULL");

   MCTF_ASSERT(write_all(sockets[0], &(uint8_t){MANAGEMENT_COMPRESSION_NONE}, sizeof(uint8_t)) == 0, cleanup, "write compression should succeed");
   MCTF_ASSERT(write_all(sockets[0], &(uint8_t){MANAGEMENT_ENCRYPTION_AES256_GCM}, sizeof(uint8_t)) == 0, cleanup, "write encryption should succeed");

   pgmoneta_write_uint32(&header, (uint32_t)encoded_len);
   MCTF_ASSERT(write_all(sockets[0], &header, sizeof(header)) == 0, cleanup, "write encoded length should succeed");
   MCTF_ASSERT(write_all(sockets[0], encoded, encoded_len) == 0, cleanup, "write encoded payload should succeed");

   MCTF_ASSERT(pgmoneta_management_read_json(NULL, sockets[1], &compression, &encryption, &json) != 0, cleanup, "pgmoneta_management_read_json should fail for tampered encrypted payload");
   MCTF_ASSERT_PTR_NULL(json, cleanup, "json should remain NULL on malformed payload");
   MCTF_ASSERT(compression == MANAGEMENT_COMPRESSION_NONE, cleanup, "compression should be parsed before payload failure");
   MCTF_ASSERT(encryption == MANAGEMENT_ENCRYPTION_AES256_GCM, cleanup, "encryption should be parsed before payload failure");

cleanup:
   if (sockets[0] != -1)
   {
      close(sockets[0]);
   }
   if (sockets[1] != -1)
   {
      close(sockets[1]);
   }
   free(ciphertext);
   free(encoded);
   pgmoneta_json_destroy(json);
   pgmoneta_test_teardown_encryption_env(&env);
   MCTF_FINISH();
}

/**
 * Test: Error responses can be emitted without an existing payload.
 */
MCTF_TEST(test_management_response_error_without_payload)
{
   int sockets[2] = {-1, -1};
   struct json* json = NULL;
   struct json* outcome = NULL;
   struct json* response = NULL;
   int32_t error = 0;

   MCTF_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, cleanup, "socketpair should succeed");
   MCTF_ASSERT(pgmoneta_management_response_error(NULL, sockets[0], NULL, MANAGEMENT_ERROR_BAD_PAYLOAD, "remote",
                                                  MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, NULL) == 0,
               cleanup, "pgmoneta_management_response_error should succeed without payload");
   MCTF_ASSERT(pgmoneta_management_read_json(NULL, sockets[1], NULL, NULL, &json) == 0, cleanup,
               "pgmoneta_management_read_json should parse error response");
   MCTF_ASSERT(pgmoneta_json_contains_key(json, MANAGEMENT_CATEGORY_OUTCOME), cleanup,
               "error response should include outcome");
   MCTF_ASSERT(pgmoneta_json_contains_key(json, MANAGEMENT_CATEGORY_RESPONSE), cleanup,
               "error response should include response");

   outcome = (struct json*)pgmoneta_json_get(json, MANAGEMENT_CATEGORY_OUTCOME);
   response = (struct json*)pgmoneta_json_get(json, MANAGEMENT_CATEGORY_RESPONSE);

   MCTF_ASSERT(outcome != NULL, cleanup, "outcome should not be NULL");
   MCTF_ASSERT(response != NULL, cleanup, "response should not be NULL");

   error = (int32_t)pgmoneta_json_get(outcome, MANAGEMENT_ARGUMENT_ERROR);
   MCTF_ASSERT(error == MANAGEMENT_ERROR_BAD_PAYLOAD, cleanup,
               "error response should preserve MANAGEMENT_ERROR_BAD_PAYLOAD");

cleanup:
   if (sockets[0] != -1)
   {
      close(sockets[0]);
   }
   if (sockets[1] != -1)
   {
      close(sockets[1]);
   }
   pgmoneta_json_destroy(json);
   MCTF_FINISH();
}

/**
 * Test: Error responses without payload can still use encrypted management framing.
 */
MCTF_TEST(test_management_response_error_without_payload_encrypted)
{
   struct test_encryption_env env;
   int sockets[2] = {-1, -1};
   struct json* json = NULL;
   struct json* outcome = NULL;
   int32_t error = 0;

   MCTF_ASSERT(pgmoneta_test_setup_encryption_env(&env) == 0, cleanup, "Failed to setup mock environment");
   MCTF_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, cleanup, "socketpair should succeed");
   MCTF_ASSERT(pgmoneta_management_response_error(NULL, sockets[0], NULL, MANAGEMENT_ERROR_BAD_PAYLOAD, "remote",
                                                  MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_AES256_GCM, NULL) == 0,
               cleanup, "encrypted pgmoneta_management_response_error should succeed without payload");
   MCTF_ASSERT(pgmoneta_management_read_json(NULL, sockets[1], NULL, NULL, &json) == 0, cleanup,
               "pgmoneta_management_read_json should parse encrypted error response");

   outcome = (struct json*)pgmoneta_json_get(json, MANAGEMENT_CATEGORY_OUTCOME);
   MCTF_ASSERT(outcome != NULL, cleanup, "outcome should not be NULL");

   error = (int32_t)pgmoneta_json_get(outcome, MANAGEMENT_ARGUMENT_ERROR);
   MCTF_ASSERT(error == MANAGEMENT_ERROR_BAD_PAYLOAD, cleanup,
               "encrypted error response should preserve MANAGEMENT_ERROR_BAD_PAYLOAD");

cleanup:
   if (sockets[0] != -1)
   {
      close(sockets[0]);
   }
   if (sockets[1] != -1)
   {
      close(sockets[1]);
   }
   pgmoneta_json_destroy(json);
   pgmoneta_test_teardown_encryption_env(&env);
   MCTF_FINISH();
}

/**
 * Test: Writing a management error to a closed peer fails without terminating the process.
 */
MCTF_TEST(test_management_response_error_closed_socket_fails)
{
   int sockets[2] = {-1, -1};

   MCTF_ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, cleanup, "socketpair should succeed");
   close(sockets[1]);
   sockets[1] = -1;

   MCTF_ASSERT(pgmoneta_management_response_error(NULL, sockets[0], NULL, MANAGEMENT_ERROR_BAD_PAYLOAD, "remote",
                                                  MANAGEMENT_COMPRESSION_NONE, MANAGEMENT_ENCRYPTION_NONE, NULL) != 0,
               cleanup, "pgmoneta_management_response_error should fail cleanly on a closed peer");

cleanup:
   if (sockets[0] != -1)
   {
      close(sockets[0]);
   }
   if (sockets[1] != -1)
   {
      close(sockets[1]);
   }
   MCTF_FINISH();
}

/**
 * Test: Buffer encryption with empty payload.
 */
MCTF_TEST(test_aes_buffer_empty_payload)
{
   struct test_encryption_env env;
   char* plaintext = "";
   size_t plaintext_len = 0;
   unsigned char* ciphertext = NULL;
   size_t ciphertext_len = 0;
   unsigned char* decrypted = NULL;
   size_t decrypted_len = 0;

   MCTF_ASSERT(pgmoneta_test_setup_encryption_env(&env) == 0, cleanup, "Failed to setup mock environment");

   MCTF_ASSERT(pgmoneta_encrypt_buffer((unsigned char*)plaintext, plaintext_len, &ciphertext, &ciphertext_len, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_encrypt_buffer should succeed for empty payload");
   MCTF_ASSERT(ciphertext_len == PBKDF2_SALT_LENGTH + AES_GCM_IV_LENGTH + GCM_TAG_LENGTH, cleanup, "ciphertext_len should be exactly salt + IV + tag");

   MCTF_ASSERT(pgmoneta_decrypt_buffer(ciphertext, ciphertext_len, &decrypted, &decrypted_len, ENCRYPTION_AES_256_GCM) == 0, cleanup, "pgmoneta_decrypt_buffer should succeed for empty payload");
   MCTF_ASSERT(decrypted_len == 0, cleanup, "decrypted_len should be 0");

cleanup:
   free(ciphertext);
   free(decrypted);
   pgmoneta_test_teardown_encryption_env(&env);
   MCTF_FINISH();
}

/**
 * Test: AES-GCM file encryption/decryption round-trip.
 * This test verifies the fix for the GCM file decryption loop bug.
 */
MCTF_TEST(test_aes_file_gcm_roundtrip)
{
   struct test_encryption_env env;
   char* plaintext = "This is a comprehensive test for file-based encryption and decryption in GCM mode. It ensures that the trailing authentication tag is correctly handled and not included in the decryption stream.";
   char from[MAX_PATH] = {0};
   char encrypted[MAX_PATH] = {0};
   char decrypted[MAX_PATH] = {0};
   FILE* f = NULL;
   char* decrypted_content = NULL;
   size_t size = 0;

   MCTF_ASSERT(pgmoneta_test_setup_encryption_env(&env) == 0, cleanup, "Failed to setup mock environment");

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

   pgmoneta_test_teardown_encryption_env(&env);
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

/*
 * Reference vectors generated with the OpenSSL CLI, which produces the same
 * envelope pgBackRest uses (EVP_BytesToKey, one round, "Salted__" header):
 *
 *   printf '%s' "$PLAINTEXT" | openssl enc -aes-256-cbc -md sha1 -pass pass:test-cipher-pass
 */
static const char* cbc_vector_plaintext = "pgmoneta-migration-test: pgBackRest stores backups as OpenSSL Salted__ AES-256-CBC";
static const char* cbc_vector_passphrase = "test-cipher-pass";

/* openssl enc -aes-256-cbc -md sha1 (the pgBackRest scheme) */
static const char* cbc_vector_sha1 =
   "53616c7465645f5f176443bd7ab461f798ef6533f0f46d40517da2183d3fce87"
   "f68817a15a1c7afb56355d609743269e8ab3ece0f18ad026ca64f4f5c1b9db4b"
   "48b44c6ee71c50b899933fbc1bf082bdaa5750c43eb46215adb1743f9395e607"
   "198a55266ccf6de00aae1bcbf793fa1e";

/* openssl enc -aes-256-cbc -md sha256 */
static const char* cbc_vector_sha256 =
   "53616c7465645f5fc438b2add6847776390d852d2f6d6f24c7e6ac39796b1859"
   "1153eabaa86e064424f9ae532ba9397ecd02093c4ff125606f50e0cd634dc088"
   "7be037384bdd38340d6ef9609508681d32599a9f566dda8aa5b503fb81811f86"
   "f5522e850480b2b7cb00277beb3ce061";

static int
hex_decode(const char* hex, unsigned char** data, size_t* size)
{
   size_t length = strlen(hex);
   unsigned char* d = NULL;

   if (length % 2 != 0)
   {
      return 1;
   }

   d = malloc(length / 2);
   if (d == NULL)
   {
      return 1;
   }

   for (size_t i = 0; i < length / 2; i++)
   {
      unsigned int byte = 0;
      if (sscanf(hex + 2 * i, "%2x", &byte) != 1)
      {
         free(d);
         return 1;
      }
      d[i] = (unsigned char)byte;
   }

   *data = d;
   *size = length / 2;

   return 0;
}

/**
 * Test: decrypt an OpenSSL "Salted__" AES-256-CBC envelope with the SHA-1
 * derivation default — the exact format pgBackRest produces.
 */
MCTF_TEST(test_aes_cbc_salted_sha1)
{
   unsigned char* envelope = NULL;
   size_t envelope_size = 0;
   unsigned char* plaintext = NULL;
   size_t plaintext_size = 0;

   MCTF_ASSERT(hex_decode(cbc_vector_sha1, &envelope, &envelope_size) == 0, cleanup, "Failed to decode vector");

   MCTF_ASSERT(pgmoneta_cbc_decrypt_salted_buffer(NULL, false,
                                                  (unsigned char*)cbc_vector_passphrase, strlen(cbc_vector_passphrase),
                                                  envelope, envelope_size,
                                                  &plaintext, &plaintext_size) == 0,
               cleanup, "Salted decrypt should succeed");
   MCTF_ASSERT(plaintext_size == strlen(cbc_vector_plaintext), cleanup, "Plaintext size should match");
   MCTF_ASSERT(memcmp(plaintext, cbc_vector_plaintext, plaintext_size) == 0, cleanup, "Plaintext should match");

cleanup:
   free(envelope);
   free(plaintext);
   MCTF_FINISH();
}

/**
 * Test: the digest parameter is honored (openssl enc -md sha256).
 */
MCTF_TEST(test_aes_cbc_salted_sha256)
{
   unsigned char* envelope = NULL;
   size_t envelope_size = 0;
   unsigned char* plaintext = NULL;
   size_t plaintext_size = 0;

   MCTF_ASSERT(hex_decode(cbc_vector_sha256, &envelope, &envelope_size) == 0, cleanup, "Failed to decode vector");

   MCTF_ASSERT(pgmoneta_cbc_decrypt_salted_buffer("sha256", false,
                                                  (unsigned char*)cbc_vector_passphrase, strlen(cbc_vector_passphrase),
                                                  envelope, envelope_size,
                                                  &plaintext, &plaintext_size) == 0,
               cleanup, "Salted decrypt with sha256 should succeed");
   MCTF_ASSERT(plaintext_size == strlen(cbc_vector_plaintext), cleanup, "Plaintext size should match");
   MCTF_ASSERT(memcmp(plaintext, cbc_vector_plaintext, plaintext_size) == 0, cleanup, "Plaintext should match");

cleanup:
   free(envelope);
   free(plaintext);
   MCTF_FINISH();
}

/**
 * Test: pgBackRest "raw" framing — a bare 8-byte salt with no "Salted__"
 * magic, as used for bundled backup files, manifests and block deltas.
 */
MCTF_TEST(test_aes_cbc_raw_header)
{
   unsigned char* envelope = NULL;
   size_t envelope_size = 0;
   unsigned char* plaintext = NULL;
   size_t plaintext_size = 0;

   MCTF_ASSERT(hex_decode(cbc_vector_sha1, &envelope, &envelope_size) == 0, cleanup, "Failed to decode vector");

   /* Strip the magic so the input starts directly with the salt */
   MCTF_ASSERT(pgmoneta_cbc_decrypt_salted_buffer(NULL, true,
                                                  (unsigned char*)cbc_vector_passphrase, strlen(cbc_vector_passphrase),
                                                  envelope + AES_CBC_SALTED_MAGIC_SIZE,
                                                  envelope_size - AES_CBC_SALTED_MAGIC_SIZE,
                                                  &plaintext, &plaintext_size) == 0,
               cleanup, "Raw-framed decrypt should succeed");
   MCTF_ASSERT(plaintext_size == strlen(cbc_vector_plaintext), cleanup, "Plaintext size should match");
   MCTF_ASSERT(memcmp(plaintext, cbc_vector_plaintext, plaintext_size) == 0, cleanup, "Plaintext should match");

cleanup:
   free(envelope);
   free(plaintext);
   MCTF_FINISH();
}

/**
 * Test: the fully-explicit path — derive the key/IV from the salt and
 * passphrase, then decrypt the ciphertext with the explicit key/IV.
 * This is the flow where the caller extracts the metadata themselves.
 */
MCTF_TEST(test_aes_cbc_explicit_key_iv)
{
   unsigned char* envelope = NULL;
   size_t envelope_size = 0;
   unsigned char* plaintext = NULL;
   size_t plaintext_size = 0;
   unsigned char salt[AES_CBC_SALT_SIZE];
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   size_t header = AES_CBC_SALTED_MAGIC_SIZE + AES_CBC_SALT_SIZE;

   MCTF_ASSERT(hex_decode(cbc_vector_sha1, &envelope, &envelope_size) == 0, cleanup, "Failed to decode vector");

   memcpy(salt, envelope + AES_CBC_SALTED_MAGIC_SIZE, AES_CBC_SALT_SIZE);

   MCTF_ASSERT(pgmoneta_cbc_derive_key_iv(NULL, salt,
                                          (unsigned char*)cbc_vector_passphrase, strlen(cbc_vector_passphrase),
                                          key, iv) == 0,
               cleanup, "Key derivation should succeed");

   MCTF_ASSERT(pgmoneta_cbc_decrypt_buffer(key, iv,
                                           envelope + header, envelope_size - header,
                                           &plaintext, &plaintext_size) == 0,
               cleanup, "Explicit key/IV decrypt should succeed");
   MCTF_ASSERT(plaintext_size == strlen(cbc_vector_plaintext), cleanup, "Plaintext size should match");
   MCTF_ASSERT(memcmp(plaintext, cbc_vector_plaintext, plaintext_size) == 0, cleanup, "Plaintext should match");

cleanup:
   free(envelope);
   free(plaintext);
   MCTF_FINISH();
}

/**
 * Test: file based decryption, both the salted envelope and the explicit
 * key/IV variant. The source file must be kept.
 */
MCTF_TEST(test_aes_cbc_decrypt_files)
{
   unsigned char* envelope = NULL;
   size_t envelope_size = 0;
   unsigned char key[EVP_MAX_KEY_LENGTH];
   unsigned char iv[EVP_MAX_IV_LENGTH];
   unsigned char salt[AES_CBC_SALT_SIZE];
   char dir[] = "/tmp/test_aes_cbc_XXXXXX";
   char from[MAX_PATH] = {0};
   char to[MAX_PATH] = {0};
   char raw_from[MAX_PATH] = {0};
   char raw_to[MAX_PATH] = {0};
   char* content = NULL;
   size_t content_size = 0;
   FILE* f = NULL;
   size_t header = AES_CBC_SALTED_MAGIC_SIZE + AES_CBC_SALT_SIZE;

   MCTF_ASSERT(hex_decode(cbc_vector_sha1, &envelope, &envelope_size) == 0, cleanup, "Failed to decode vector");
   MCTF_ASSERT_PTR_NONNULL(mkdtemp(dir), cleanup, "Failed to create temp dir");

   pgmoneta_snprintf(from, MAX_PATH, "%s/backup.enc", dir);
   pgmoneta_snprintf(to, MAX_PATH, "%s/backup.dec", dir);
   pgmoneta_snprintf(raw_from, MAX_PATH, "%s/bundle.enc", dir);
   pgmoneta_snprintf(raw_to, MAX_PATH, "%s/bundle.dec", dir);

   /* Salted envelope file */
   f = fopen(from, "wb");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "Failed to create encrypted file");
   MCTF_ASSERT(fwrite(envelope, 1, envelope_size, f) == envelope_size, cleanup, "Failed to write encrypted file");
   fclose(f);
   f = NULL;

   MCTF_ASSERT(pgmoneta_cbc_decrypt_salted_file(NULL, false,
                                                (unsigned char*)cbc_vector_passphrase, strlen(cbc_vector_passphrase),
                                                from, to) == 0,
               cleanup, "Salted file decrypt should succeed");
   MCTF_ASSERT(pgmoneta_exists(from), cleanup, "Source file must be kept");
   MCTF_ASSERT(pgmoneta_exists(to), cleanup, "Destination file should exist");

   f = fopen(to, "rb");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "Failed to open decrypted file");
   fseek(f, 0, SEEK_END);
   content_size = (size_t)ftell(f);
   fseek(f, 0, SEEK_SET);
   content = malloc(content_size + 1);
   MCTF_ASSERT_PTR_NONNULL(content, cleanup, "Failed to allocate content");
   MCTF_ASSERT(fread(content, 1, content_size, f) == content_size, cleanup, "Failed to read decrypted file");
   fclose(f);
   f = NULL;

   MCTF_ASSERT(content_size == strlen(cbc_vector_plaintext), cleanup, "Decrypted size should match");
   MCTF_ASSERT(memcmp(content, cbc_vector_plaintext, content_size) == 0, cleanup, "Decrypted content should match");

   /* Headerless file decrypted with an explicit key/IV */
   memcpy(salt, envelope + AES_CBC_SALTED_MAGIC_SIZE, AES_CBC_SALT_SIZE);
   MCTF_ASSERT(pgmoneta_cbc_derive_key_iv(NULL, salt,
                                          (unsigned char*)cbc_vector_passphrase, strlen(cbc_vector_passphrase),
                                          key, iv) == 0,
               cleanup, "Key derivation should succeed");

   f = fopen(raw_from, "wb");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "Failed to create headerless file");
   MCTF_ASSERT(fwrite(envelope + header, 1, envelope_size - header, f) == envelope_size - header, cleanup, "Failed to write headerless file");
   fclose(f);
   f = NULL;

   MCTF_ASSERT(pgmoneta_cbc_decrypt_file(key, iv, raw_from, raw_to) == 0,
               cleanup, "Explicit key/IV file decrypt should succeed");
   MCTF_ASSERT(pgmoneta_exists(raw_to), cleanup, "Destination file should exist");

   f = fopen(raw_to, "rb");
   MCTF_ASSERT_PTR_NONNULL(f, cleanup, "Failed to open decrypted headerless file");
   fseek(f, 0, SEEK_END);
   content_size = (size_t)ftell(f);
   fseek(f, 0, SEEK_SET);
   MCTF_ASSERT(content_size == strlen(cbc_vector_plaintext), cleanup, "Decrypted headerless size should match");

cleanup:
   if (f)
   {
      fclose(f);
   }

   if (from[0] != '\0')
   {
      remove(from);
      remove(to);
      remove(raw_from);
      remove(raw_to);
      rmdir(dir);
   }

   free(envelope);
   free(content);
   MCTF_FINISH();
}

/**
 * Test: wrong passphrase, wrong digest, invalid magic and truncated input
 * all fail cleanly.
 */
MCTF_TEST_NEGATIVE(test_aes_cbc_decrypt_failures)
{
   unsigned char* envelope = NULL;
   size_t envelope_size = 0;
   unsigned char* plaintext = NULL;
   size_t plaintext_size = 0;
   char* wrong_passphrase = "wrong-cipher-pass";

   MCTF_ASSERT(hex_decode(cbc_vector_sha1, &envelope, &envelope_size) == 0, cleanup, "Failed to decode vector");

   /* Wrong passphrase: padding check must reject */
   MCTF_ASSERT(pgmoneta_cbc_decrypt_salted_buffer(NULL, false,
                                                  (unsigned char*)wrong_passphrase, strlen(wrong_passphrase),
                                                  envelope, envelope_size,
                                                  &plaintext, &plaintext_size) != 0,
               cleanup, "Wrong passphrase should fail");
   MCTF_ASSERT_PTR_NULL(plaintext, cleanup, "Plaintext should be NULL on failure");

   /* Wrong digest: derives a different key, must reject */
   MCTF_ASSERT(pgmoneta_cbc_decrypt_salted_buffer("sha256", false,
                                                  (unsigned char*)cbc_vector_passphrase, strlen(cbc_vector_passphrase),
                                                  envelope, envelope_size,
                                                  &plaintext, &plaintext_size) != 0,
               cleanup, "Wrong digest should fail");

   /* Corrupt the magic */
   envelope[0] ^= 0xFF;
   MCTF_ASSERT(pgmoneta_cbc_decrypt_salted_buffer(NULL, false,
                                                  (unsigned char*)cbc_vector_passphrase, strlen(cbc_vector_passphrase),
                                                  envelope, envelope_size,
                                                  &plaintext, &plaintext_size) != 0,
               cleanup, "Invalid magic should fail");
   envelope[0] ^= 0xFF;

   /* Truncated to inside the header */
   MCTF_ASSERT(pgmoneta_cbc_decrypt_salted_buffer(NULL, false,
                                                  (unsigned char*)cbc_vector_passphrase, strlen(cbc_vector_passphrase),
                                                  envelope, AES_CBC_SALTED_MAGIC_SIZE + 4,
                                                  &plaintext, &plaintext_size) != 0,
               cleanup, "Truncated header should fail");

   /* Truncated ciphertext (not a multiple of the block size) */
   MCTF_ASSERT(pgmoneta_cbc_decrypt_salted_buffer(NULL, false,
                                                  (unsigned char*)cbc_vector_passphrase, strlen(cbc_vector_passphrase),
                                                  envelope, envelope_size - 5,
                                                  &plaintext, &plaintext_size) != 0,
               cleanup, "Truncated ciphertext should fail");

   /* Unknown digest is rejected */
   MCTF_ASSERT(pgmoneta_cbc_decrypt_salted_buffer("no-such-digest", false,
                                                  (unsigned char*)cbc_vector_passphrase, strlen(cbc_vector_passphrase),
                                                  envelope, envelope_size,
                                                  &plaintext, &plaintext_size) != 0,
               cleanup, "Unknown digest should fail");

cleanup:
   free(envelope);
   free(plaintext);
   MCTF_FINISH();
}
