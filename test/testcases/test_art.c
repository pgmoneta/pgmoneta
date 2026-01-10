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
 *
 */

#include <pgmoneta.h>
#include <art.h>
#include <tscommon.h>
#include <mctf.h>
#include <utils.h>
#include <value.h>

#include <stdio.h>
#include <string.h>
#include <libgen.h>

struct art_test_obj
{
   char* str;
   int idx;
};

static void test_obj_create(int idx, struct art_test_obj** obj);
static void test_obj_destroy(struct art_test_obj* obj);
static void test_obj_destroy_cb(uintptr_t obj);

MCTF_TEST(test_art_create)
{
   struct art* t = NULL;

   pgmoneta_test_setup();

   pgmoneta_art_create(&t);

   MCTF_ASSERT_PTR_NONNULL(t, "ART creation failed", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 0, "ART size should be 0", cleanup);

cleanup:
   if (t)
   {
      pgmoneta_art_destroy(t);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_art_insert)
{
   struct art* t = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   pgmoneta_test_setup();

   pgmoneta_art_create(&t);
   mem = malloc(10);

   MCTF_ASSERT_PTR_NONNULL(t, "ART creation failed", cleanup);

   MCTF_ASSERT(pgmoneta_art_insert(t, "key_none", 0, ValueNone), "Insert key_none failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_insert(t, NULL, 0, ValueInt8), "Insert NULL key failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_insert(NULL, "key_none", 0, ValueInt8), "Insert into NULL ART failed", cleanup);

   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), "Insert key_str failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_int", 1, ValueInt32), "Insert key_int failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_bool", true, ValueBool), "Insert key_bool failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_float", pgmoneta_value_from_float(2.5), ValueFloat), "Insert key_float failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_double", pgmoneta_value_from_double(2.5), ValueDouble), "Insert key_double failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem), "Insert key_mem failed", cleanup);

   test_obj_create(0, &obj);
   MCTF_ASSERT(!pgmoneta_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config), "Insert key_obj failed", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 7, "ART size should be 7", cleanup);

cleanup:
   pgmoneta_art_destroy(t);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_art_search)
{
   struct art* t = NULL;
   struct art_test_obj* obj1 = NULL;
   struct art_test_obj* obj2 = NULL;
   enum value_type type = ValueNone;
   char* value2 = NULL;
   char* key_str = NULL;
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   pgmoneta_test_setup();

   pgmoneta_art_create(&t);

   MCTF_ASSERT_PTR_NONNULL(t, "ART creation failed", cleanup);

   MCTF_ASSERT(pgmoneta_art_insert(t, "key_none", 0, ValueNone), "Insert key_none failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_none"), "Contains key_none should be false", cleanup);
   MCTF_ASSERT_INT_EQ((int)pgmoneta_art_search(t, "key_none"), 0, "Search key_none should be 0", cleanup);
   MCTF_ASSERT_INT_EQ((int)pgmoneta_art_search_typed(t, "key_none", &type), 0, "Search typed key_none should be 0", cleanup);
   MCTF_ASSERT_INT_EQ(type, ValueNone, "Type should be ValueNone", cleanup);

   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), "Insert key_str failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_str"), "Contains key_str should be true", cleanup);
   MCTF_ASSERT_STR_EQ((char*)pgmoneta_art_search(t, "key_str"), "value1", "Search key_str mismatch", cleanup);

   // inserting string makes a copy
   key_str = pgmoneta_append(key_str, "key_str");
   value2 = pgmoneta_append(value2, "value2");
   MCTF_ASSERT(!pgmoneta_art_insert(t, key_str, (uintptr_t)value2, ValueString), "Insert key_str replacement failed", cleanup);
   MCTF_ASSERT_STR_EQ((char*)pgmoneta_art_search(t, "key_str"), "value2", "Search key_str replacement mismatch", cleanup);

   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_int", -1, ValueInt32), "Insert key_int failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_int"), "Contains key_int should be true", cleanup);
   MCTF_ASSERT_INT_EQ((int)pgmoneta_art_search(t, "key_int"), -1, "Search key_int mismatch", cleanup);

   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_bool", true, ValueBool), "Insert key_bool failed", cleanup);
   MCTF_ASSERT((bool)pgmoneta_art_search(t, "key_bool"), "Search key_bool mismatch", cleanup);

   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_float", pgmoneta_value_from_float(2.5), ValueFloat), "Insert key_float failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_double", pgmoneta_value_from_double(2.5), ValueDouble), "Insert key_double failed", cleanup);

   float f_val = pgmoneta_value_to_float(pgmoneta_art_search(t, "key_float"));
   MCTF_ASSERT(f_val == 2.5f, "Search key_float mismatch", cleanup);

   double d_val = pgmoneta_value_to_double(pgmoneta_art_search(t, "key_double"));
   MCTF_ASSERT(d_val == 2.5, "Search key_double mismatch", cleanup);

   test_obj_create(1, &obj1);
   MCTF_ASSERT(!pgmoneta_art_insert_with_config(t, "key_obj", (uintptr_t)obj1, &test_obj_config), "Insert key_obj failed", cleanup);
   MCTF_ASSERT_INT_EQ(((struct art_test_obj*)pgmoneta_art_search(t, "key_obj"))->idx, 1, "Search key_obj idx mismatch", cleanup);
   MCTF_ASSERT_STR_EQ(((struct art_test_obj*)pgmoneta_art_search(t, "key_obj"))->str, "obj1", "Search key_obj str mismatch", cleanup);
   pgmoneta_art_search_typed(t, "key_obj", &type);
   MCTF_ASSERT_INT_EQ(type, ValueRef, "Type should be ValueRef", cleanup);

   // test obj overwrite with memory free up
   test_obj_create(2, &obj2);
   MCTF_ASSERT(!pgmoneta_art_insert_with_config(t, "key_obj", (uintptr_t)obj2, &test_obj_config), "Insert key_obj overwrite failed", cleanup);
   MCTF_ASSERT_INT_EQ(((struct art_test_obj*)pgmoneta_art_search(t, "key_obj"))->idx, 2, "Search key_obj overwrite idx mismatch", cleanup);
   MCTF_ASSERT_STR_EQ(((struct art_test_obj*)pgmoneta_art_search(t, "key_obj"))->str, "obj2", "Search key_obj overwrite str mismatch", cleanup);

cleanup:
   if (key_str)
   {
      free(key_str);
      key_str = NULL;
   }
   if (value2)
   {
      free(value2);
      value2 = NULL;
   }
   pgmoneta_art_destroy(t);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_art_basic_delete)
{
   struct art* t = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   pgmoneta_test_setup();

   pgmoneta_art_create(&t);
   mem = malloc(10);

   MCTF_ASSERT_PTR_NONNULL(t, "ART creation failed", cleanup);
   test_obj_create(0, &obj);

   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), "Insert key_str failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_int", 1, ValueInt32), "Insert key_int failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_bool", true, ValueBool), "Insert key_bool failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_float", pgmoneta_value_from_float(2.5), ValueFloat), "Insert key_float failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_double", pgmoneta_value_from_double(2.5), ValueDouble), "Insert key_double failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem), "Insert key_mem failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config), "Insert key_obj failed", cleanup);

   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_str"), "Contains key_str failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_int"), "Contains key_int failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_bool"), "Contains key_bool failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_mem"), "Contains key_mem failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_float"), "Contains key_float failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_double"), "Contains key_double failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_obj"), "Contains key_obj failed", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 7, "ART size should be 7", cleanup);

   MCTF_ASSERT(pgmoneta_art_delete(t, NULL), "Delete NULL key should fail", cleanup);
   MCTF_ASSERT(pgmoneta_art_delete(NULL, "key_str"), "Delete from NULL ART should fail", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "key_str"), "Delete key_str failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_str"), "Contains key_str should be false", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 6, "ART size should be 6", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "key_int"), "Delete key_int failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_int"), "Contains key_int should be false", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 5, "ART size should be 5", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "key_bool"), "Delete key_bool failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_bool"), "Contains key_bool should be false", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 4, "ART size should be 4", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "key_mem"), "Delete key_mem failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_mem"), "Contains key_mem should be false", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 3, "ART size should be 3", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "key_float"), "Delete key_float failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_float"), "Contains key_float should be false", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 2, "ART size should be 2", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "key_double"), "Delete key_double failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_double"), "Contains key_double should be false", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 1, "ART size should be 1", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "key_obj"), "Delete key_obj failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_obj"), "Contains key_obj should be false", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 0, "ART size should be 0", cleanup);

cleanup:
   pgmoneta_art_destroy(t);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_art_double_delete)
{
   struct art* t = NULL;

   pgmoneta_test_setup();

   pgmoneta_art_create(&t);

   MCTF_ASSERT_PTR_NONNULL(t, "ART creation failed", cleanup);

   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), "Insert key_str failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_int", 1, ValueInt32), "Insert key_int failed", cleanup);

   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_str"), "Contains key_str failed", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 2, "ART size should be 2", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "key_str"), "Delete key_str failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_str"), "Contains key_str should be false", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 1, "ART size should be 1", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "key_str"), "Second delete of key_str should fail gracefully", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_str"), "Contains key_str should still be false", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 1, "ART size should be 1", cleanup);

cleanup:
   pgmoneta_art_destroy(t);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_art_clear)
{
   struct art* t = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   pgmoneta_test_setup();

   pgmoneta_art_create(&t);
   mem = malloc(10);

   MCTF_ASSERT_PTR_NONNULL(t, "ART creation failed", cleanup);
   test_obj_create(0, &obj);

   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), "Insert key_str failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_int", 1, ValueInt32), "Insert key_int failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_bool", true, ValueBool), "Insert key_bool failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_float", pgmoneta_value_from_float(2.5), ValueFloat), "Insert key_float failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_double", pgmoneta_value_from_double(2.5), ValueDouble), "Insert key_double failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem), "Insert key_mem failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config), "Insert key_obj failed", cleanup);

   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_str"), "Contains key_str failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_int"), "Contains key_int failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_bool"), "Contains key_bool failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_mem"), "Contains key_mem failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_float"), "Contains key_float failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_double"), "Contains key_double failed", cleanup);
   MCTF_ASSERT(pgmoneta_art_contains_key(t, "key_obj"), "Contains key_obj failed", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 7, "ART size should be 7", cleanup);

   MCTF_ASSERT(!pgmoneta_art_clear(t), "Clear failed", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 0, "ART size should be 0", cleanup);
   MCTF_ASSERT_PTR_NULL(t->root, "Root should be NULL", cleanup);

cleanup:
   pgmoneta_art_destroy(t);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_art_iterator_read)
{
   struct art* t = NULL;
   struct art_iterator* iter = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   pgmoneta_test_setup();

   pgmoneta_art_create(&t);
   mem = malloc(10);

   MCTF_ASSERT_PTR_NONNULL(t, "ART creation failed", cleanup);
   test_obj_create(1, &obj);

   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), "Insert key_str failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_int", 1, ValueInt32), "Insert key_int failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_bool", true, ValueBool), "Insert key_bool failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_float", pgmoneta_value_from_float(2.5), ValueFloat), "Insert key_float failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_double", pgmoneta_value_from_double(2.5), ValueDouble), "Insert key_double failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem), "Insert key_mem failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config), "Insert key_obj failed", cleanup);

   MCTF_ASSERT(pgmoneta_art_iterator_create(NULL, &iter), "Iterator creation should fail with NULL ART", cleanup);
   MCTF_ASSERT_PTR_NULL(iter, "Iterator should be NULL", cleanup);
   MCTF_ASSERT(!pgmoneta_art_iterator_create(t, &iter), "Iterator creation failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(iter, "Iterator should not be NULL", cleanup);
   MCTF_ASSERT(pgmoneta_art_iterator_has_next(iter), "Iterator should have next", cleanup);

   int cnt = 0;
   while (pgmoneta_art_iterator_next(iter))
   {
      if (pgmoneta_compare_string(iter->key, "key_str"))
      {
         MCTF_ASSERT_STR_EQ((char*)pgmoneta_value_data(iter->value), "value1", "value1 mismatch", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_int"))
      {
         MCTF_ASSERT_INT_EQ((int)pgmoneta_value_data(iter->value), 1, "value int mismatch", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_bool"))
      {
         MCTF_ASSERT((bool)pgmoneta_value_data(iter->value), "value bool mismatch", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_float"))
      {
         float f_val = pgmoneta_value_to_float(pgmoneta_value_data(iter->value));
         MCTF_ASSERT(f_val == 2.5f, "value float mismatch", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_double"))
      {
         double d_val = pgmoneta_value_to_double(pgmoneta_value_data(iter->value));
         MCTF_ASSERT(d_val == 2.5, "value double mismatch", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_mem"))
      {
         // as long as it exists...
         MCTF_ASSERT(true, "true", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_obj"))
      {
         MCTF_ASSERT_INT_EQ(((struct art_test_obj*)pgmoneta_value_data(iter->value))->idx, 1, "obj idx mismatch", cleanup);
         MCTF_ASSERT_STR_EQ(((struct art_test_obj*)pgmoneta_value_data(iter->value))->str, "obj1", "obj str mismatch", cleanup);
      }
      else
      {
         MCTF_ASSERT(false, "found key not inserted", cleanup);
      }

      cnt++;
   }
   MCTF_ASSERT_INT_EQ(cnt, t->size, "count mismatch", cleanup);
   MCTF_ASSERT(!pgmoneta_art_iterator_has_next(iter), "iterator should not have next", cleanup);

cleanup:
   if (iter)
   {
      pgmoneta_art_iterator_destroy(iter);
   }
   if (t)
   {
      pgmoneta_art_destroy(t);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_art_iterator_remove)
{
   struct art* t = NULL;
   struct art_iterator* iter = NULL;
   void* mem = NULL;
   struct art_test_obj* obj = NULL;
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};

   pgmoneta_test_setup();

   pgmoneta_art_create(&t);
   mem = malloc(10);

   MCTF_ASSERT_PTR_NONNULL(t, "ART creation failed", cleanup);
   test_obj_create(1, &obj);

   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_str", (uintptr_t)"value1", ValueString), "Insert key_str failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_int", 1, ValueInt32), "Insert key_int failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_bool", true, ValueBool), "Insert key_bool failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_float", pgmoneta_value_from_float(2.5), ValueFloat), "Insert key_float failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_double", pgmoneta_value_from_double(2.5), ValueDouble), "Insert key_double failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, "key_mem", (uintptr_t)mem, ValueMem), "Insert key_mem failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert_with_config(t, "key_obj", (uintptr_t)obj, &test_obj_config), "Insert key_obj failed", cleanup);

   MCTF_ASSERT_INT_EQ(t->size, 7, "ART size should be 7", cleanup);

   MCTF_ASSERT(!pgmoneta_art_iterator_create(t, &iter), "Iterator creation failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(iter, "Iterator should not be NULL", cleanup);
   MCTF_ASSERT(pgmoneta_art_iterator_has_next(iter), "Iterator should have next", cleanup);

   int cnt = 0;
   while (pgmoneta_art_iterator_next(iter))
   {
      cnt++;
      if (pgmoneta_compare_string(iter->key, "key_str"))
      {
         MCTF_ASSERT_STR_EQ((char*)pgmoneta_value_data(iter->value), "value1", "value1 mismatch", cleanup);
         pgmoneta_art_iterator_remove(iter);
         MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_str"), "Contains key_str should be false", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_int"))
      {
         MCTF_ASSERT_INT_EQ((int)pgmoneta_value_data(iter->value), 1, "value int mismatch", cleanup);
         pgmoneta_art_iterator_remove(iter);
         MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_int"), "Contains key_int should be false", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_bool"))
      {
         MCTF_ASSERT((bool)pgmoneta_value_data(iter->value), "value bool mismatch", cleanup);
         pgmoneta_art_iterator_remove(iter);
         MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_bool"), "Contains key_bool should be false", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_float"))
      {
         float f_val = pgmoneta_value_to_float(pgmoneta_value_data(iter->value));
         MCTF_ASSERT(f_val == 2.5f, "value float mismatch", cleanup);
         pgmoneta_art_iterator_remove(iter);
         MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_float"), "Contains key_float should be false", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_double"))
      {
         double d_val = pgmoneta_value_to_double(pgmoneta_value_data(iter->value));
         MCTF_ASSERT(d_val == 2.5, "value double mismatch", cleanup);
         pgmoneta_art_iterator_remove(iter);
         MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_double"), "Contains key_double should be false", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_mem"))
      {
         pgmoneta_art_iterator_remove(iter);
         MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_mem"), "Contains key_mem should be false", cleanup);
      }
      else if (pgmoneta_compare_string(iter->key, "key_obj"))
      {
         MCTF_ASSERT_INT_EQ(((struct art_test_obj*)pgmoneta_value_data(iter->value))->idx, 1, "obj idx mismatch", cleanup);
         MCTF_ASSERT_STR_EQ(((struct art_test_obj*)pgmoneta_value_data(iter->value))->str, "obj1", "obj str mismatch", cleanup);
         pgmoneta_art_iterator_remove(iter);
         MCTF_ASSERT(!pgmoneta_art_contains_key(t, "key_obj"), "Contains key_obj should be false", cleanup);
      }
      else
      {
         MCTF_ASSERT(false, "found key not inserted", cleanup);
      }

      MCTF_ASSERT_INT_EQ(t->size, 7 - cnt, "size mismatch", cleanup);
      MCTF_ASSERT_PTR_NULL(iter->key, "key should be NULL", cleanup);
      MCTF_ASSERT_PTR_NULL(iter->value, "value should be NULL", cleanup);
   }
   MCTF_ASSERT_INT_EQ(cnt, 7, "count mismatch", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 0, "size should be 0", cleanup);
   MCTF_ASSERT(!pgmoneta_art_iterator_has_next(iter), "iterator should not have next", cleanup);

cleanup:
   if (iter)
   {
      pgmoneta_art_iterator_destroy(iter);
   }
   if (t)
   {
      pgmoneta_art_destroy(t);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_art_insert_search_extensive)
{
   struct art* t = NULL;
   char buf[512];
   FILE* f = NULL;
   uintptr_t line = 1;
   int len = 0;
   char* path = NULL;

   pgmoneta_test_setup();

   if (strlen(TEST_BASE_DIR) == 0)
   {
      char file_path[MAX_PATH];
      snprintf(file_path, sizeof(file_path), "%s", __FILE__);
      char* dir = dirname(file_path);
      path = pgmoneta_append(path, dir);
      path = pgmoneta_append(path, "/../resource/art_advanced_test/words.txt");
   }
   else
   {
      path = pgmoneta_append(path, TEST_BASE_DIR);
      path = pgmoneta_append(path, "/resource/art_advanced_test/words.txt");
   }

   f = fopen(path, "r");
   MCTF_ASSERT_PTR_NONNULL(f, "File open failed", cleanup);

   pgmoneta_art_create(&t);
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      MCTF_ASSERT(!pgmoneta_art_insert(t, buf, line, ValueInt32), "Insert failed", cleanup);
      line++;
   }

   // Seek back to the start
   fseek(f, 0, SEEK_SET);
   line = 1;
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      int val = (int)pgmoneta_art_search(t, buf);
      MCTF_ASSERT(val == line, "Search mismatch", cleanup);
      line++;
   }

cleanup:
   if (f)
   {
      fclose(f);
   }
   free(path);
   pgmoneta_art_destroy(t);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_art_insert_very_long)
{
   struct art* t = NULL;
   pgmoneta_test_setup();

   pgmoneta_art_create(&t);

   unsigned char key1[300] = {16, 1, 1, 1, 7, 11, 1, 1, 1, 2, 17, 11, 1, 1, 1, 121, 11, 1, 1, 1, 121, 11, 1,
                              1, 1, 216, 11, 1, 1, 1, 202, 11, 1, 1, 1, 194, 11, 1, 1, 1, 224, 11, 1, 1, 1,
                              231, 11, 1, 1, 1, 211, 11, 1, 1, 1, 206, 11, 1, 1, 1, 208, 11, 1, 1, 1, 232,
                              11, 1, 1, 1, 124, 11, 1, 1, 1, 124, 2, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 210, 95, 185, 89, 111, 118, 250, 173, 202, 199, 101, 1,
                              8, 18, 182, 92, 236, 147, 171, 101, 151, 195, 112, 185, 218, 108, 246,
                              139, 164, 234, 195, 58, 177, 1, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 211, 95, 185, 89, 111, 118, 250, 173, 202, 199, 101, 1,
                              8, 18, 181, 93, 46, 150, 9, 212, 191, 95, 102, 178, 217, 44, 178, 235,
                              29, 191, 218, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213, 251, 173, 202,
                              211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1, 8, 18, 181, 93,
                              46, 151, 9, 212, 191, 95, 102, 183, 219, 229, 214, 59, 125, 182, 71,
                              108, 181, 220, 238, 150, 91, 117, 151, 201, 84, 183, 128, 8, 16, 1, 1,
                              1, 2, 12, 185, 89, 44, 213, 251, 173, 202, 211, 95, 185, 89, 111, 118,
                              251, 173, 202, 199, 100, 1, 8, 18, 181, 93, 46, 151, 9, 212, 191, 95,
                              108, 176, 217, 47, 51, 219, 61, 134, 207, 97, 151, 88, 237, 246, 208,
                              8, 18, 255, 255, 255, 219, 191, 198, 134, 5, 223, 212, 72, 44, 208,
                              251, 181, 14, 1, 1, 1, 8, '\0'};
   unsigned char key2[303] = {16, 1, 1, 1, 7, 10, 1, 1, 1, 2, 17, 11, 1, 1, 1, 121, 11, 1, 1, 1, 121, 11, 1,
                              1, 1, 216, 11, 1, 1, 1, 202, 11, 1, 1, 1, 194, 11, 1, 1, 1, 224, 11, 1, 1, 1,
                              231, 11, 1, 1, 1, 211, 11, 1, 1, 1, 206, 11, 1, 1, 1, 208, 11, 1, 1, 1, 232,
                              11, 1, 1, 1, 124, 10, 1, 1, 1, 124, 2, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1,
                              8, 18, 182, 92, 236, 147, 171, 101, 150, 195, 112, 185, 218, 108, 246,
                              139, 164, 234, 195, 58, 177, 1, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213,
                              251, 173, 202, 211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1,
                              8, 18, 181, 93, 46, 151, 9, 212, 191, 95, 102, 178, 217, 44, 178, 235,
                              29, 191, 218, 8, 16, 1, 1, 1, 2, 12, 185, 89, 44, 213, 251, 173, 202,
                              211, 95, 185, 89, 111, 118, 251, 173, 202, 199, 101, 1, 8, 18, 181, 93,
                              46, 151, 9, 212, 191, 95, 102, 183, 219, 229, 214, 59, 125, 182, 71,
                              108, 181, 221, 238, 151, 91, 117, 151, 201, 84, 183, 128, 8, 16, 1, 1,
                              1, 3, 12, 185, 89, 44, 213, 250, 133, 178, 195, 105, 183, 87, 237, 151,
                              155, 165, 151, 229, 97, 182, 1, 8, 18, 161, 91, 239, 51, 11, 61, 151,
                              223, 114, 179, 217, 64, 8, 12, 186, 219, 172, 151, 91, 53, 166, 221,
                              101, 178, 1, 8, 18, 255, 255, 255, 219, 191, 198, 134, 5, 208, 212, 72,
                              44, 208, 251, 180, 14, 1, 1, 1, 8, '\0'};

   MCTF_ASSERT(!pgmoneta_art_insert(t, (char*)key1, (uintptr_t)key1, ValueRef), "Insert key1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, (char*)key2, (uintptr_t)key2, ValueRef), "Insert key2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, (char*)key2, (uintptr_t)key2, ValueRef), "Insert key2 copy failed", cleanup);
   MCTF_ASSERT_INT_EQ(t->size, 2, "Size mismatch", cleanup);

cleanup:
   pgmoneta_art_destroy(t);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_art_random_delete)
{
   struct art* t = NULL;
   char buf[512];
   FILE* f = NULL;
   uintptr_t line = 1;
   int len = 0;
   char* path = NULL;

   pgmoneta_test_setup();

   if (strlen(TEST_BASE_DIR) == 0)
   {
      char file_path[MAX_PATH];
      snprintf(file_path, sizeof(file_path), "%s", __FILE__);
      char* dir = dirname(file_path);
      path = pgmoneta_append(path, dir);
      path = pgmoneta_append(path, "/../resource/art_advanced_test/words.txt");
   }
   else
   {
      path = pgmoneta_append(path, TEST_BASE_DIR);
      path = pgmoneta_append(path, "/resource/art_advanced_test/words.txt");
   }

   f = fopen(path, "r");
   MCTF_ASSERT_PTR_NONNULL(f, "File open failed", cleanup);

   pgmoneta_art_create(&t);
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      MCTF_ASSERT(!pgmoneta_art_insert(t, buf, line, ValueInt32), "Insert failed", cleanup);
      line++;
   }

   // Seek back to the start
   fseek(f, 0, SEEK_SET);
   line = 1;
   while (fgets(buf, sizeof(buf), f))
   {
      len = strlen(buf);
      buf[len - 1] = '\0';
      int val = (int)pgmoneta_art_search(t, buf);
      MCTF_ASSERT(val == line, "Search mismatch", cleanup);
      line++;
   }

   MCTF_ASSERT(!pgmoneta_art_delete(t, "A"), "Delete A failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "A"), "Contains A should be false", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "yard"), "Delete yard failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "yard"), "Contains yard should be false", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "Xenarchi"), "Delete Xenarchi failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "Xenarchi"), "Contains Xenarchi should be false", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "F"), "Delete F failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "F"), "Contains F should be false", cleanup);

   MCTF_ASSERT(!pgmoneta_art_delete(t, "wirespun"), "Delete wirespun failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_contains_key(t, "wirespun"), "Contains wirespun should be false", cleanup);

cleanup:
   if (f)
   {
      fclose(f);
   }
   free(path);
   pgmoneta_art_destroy(t);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_art_insert_index_out_of_range)
{
   struct art* t = NULL;
   char* s1 = "abcdefghijklmnxyz";
   char* s2 = "abcdefghijklmnopqrstuvw";
   char* s3 = "abcdefghijk";

   pgmoneta_test_setup();

   pgmoneta_art_create(&t);
   MCTF_ASSERT(!pgmoneta_art_insert(t, s1, 1, ValueUInt8), "Insert s1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_art_insert(t, s2, 1, ValueUInt8), "Insert s2 failed", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_art_search(t, s3), 0, "Search s3 should be 0", cleanup);

cleanup:
   pgmoneta_art_destroy(t);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

static void
test_obj_create(int idx, struct art_test_obj** obj)
{
   struct art_test_obj* o = NULL;

   o = malloc(sizeof(struct art_test_obj));
   memset(o, 0, sizeof(struct art_test_obj));
   o->idx = idx;
   o->str = pgmoneta_append(o->str, "obj");
   o->str = pgmoneta_append_int(o->str, idx);

   *obj = o;
}
static void
test_obj_destroy(struct art_test_obj* obj)
{
   if (obj == NULL)
   {
      return;
   }
   free(obj->str);
   free(obj);
}

static void
test_obj_destroy_cb(uintptr_t obj)
{
   test_obj_destroy((struct art_test_obj*)obj);
}