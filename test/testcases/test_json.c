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
#include <json.h>
#include <tscommon.h>
#include <mctf.h>
#include <utils.h>
#include <stdio.h>

MCTF_TEST(test_json_create)
{
   struct json* obj = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_json_create(&obj), cleanup, "json creation failed");
   MCTF_ASSERT_PTR_NONNULL(obj, cleanup, "json object is null");
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, cleanup, "json type should be JSONUnknown");

cleanup:
   pgmoneta_json_destroy(obj);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_json_put_basic)
{
   struct json* obj = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_json_create(&obj), cleanup, "json creation failed");
   MCTF_ASSERT_PTR_NONNULL(obj, cleanup, "json object is null");
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, cleanup, "json type should be JSONUnknown");

   MCTF_ASSERT(!pgmoneta_json_put(obj, "key1", (uintptr_t)"value1", ValueString), cleanup, "json put failed");
   MCTF_ASSERT(pgmoneta_json_contains_key(obj, "key1"), cleanup, "json should contain key1");
   MCTF_ASSERT_STR_EQ((char*)pgmoneta_json_get(obj, "key1"), "value1", cleanup, "value mismatch");
   MCTF_ASSERT_INT_EQ(obj->type, JSONItem, cleanup, "json type should be JSONItem");

   // json only takes in certain types of value
   MCTF_ASSERT(pgmoneta_json_put(obj, "key2", (uintptr_t)"value1", ValueMem), cleanup, "should fail for ValueMem");
   MCTF_ASSERT(!pgmoneta_json_contains_key(obj, "key2"), cleanup, "should not contain key2");

   // item should not take entry input
   MCTF_ASSERT(pgmoneta_json_append(obj, (uintptr_t)"entry", ValueString), cleanup, "item should not append");

cleanup:
   pgmoneta_json_destroy(obj);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_json_append_basic)
{
   struct json* obj = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_json_create(&obj), cleanup, "json creation failed");
   MCTF_ASSERT_PTR_NONNULL(obj, cleanup, "json object is null");
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, cleanup, "json type should be JSONUnknown");

   MCTF_ASSERT(!pgmoneta_json_append(obj, (uintptr_t)"value1", ValueString), cleanup, "json append failed");
   MCTF_ASSERT_INT_EQ(obj->type, JSONArray, cleanup, "json type should be JSONArray");

   MCTF_ASSERT(pgmoneta_json_append(obj, (uintptr_t)"value2", ValueMem), cleanup, "should fail for ValueMem");
   MCTF_ASSERT(pgmoneta_json_put(obj, "key", (uintptr_t)"value", ValueString), cleanup, "array should not put");

cleanup:
   pgmoneta_json_destroy(obj);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_json_parse_to_string)
{
   struct json* obj = NULL;
   struct json* obj_parsed = NULL;
   char* str_obj = NULL;
   char* str_obj_parsed = NULL;

   struct json* int_array = NULL;
   struct json* str_array = NULL;
   struct json* json_item_shallow = NULL;

   struct json* json_array_nested_item1 = NULL;
   struct json* json_array_nested_item2 = NULL;
   struct json* json_array_item_nested = NULL;

   struct json* json_array_nested_array1 = NULL;
   struct json* json_array_nested_array2 = NULL;
   struct json* json_array_array_nested = NULL;

   struct json* json_item_nested_array1 = NULL;
   struct json* json_item_nested_array2 = NULL;
   struct json* json_item_array_nested = NULL;

   struct json* json_item_nested_item1 = NULL;
   struct json* json_item_nested_item2 = NULL;
   struct json* json_item_item_nested = NULL;

   pgmoneta_test_setup();

   pgmoneta_json_create(&obj);
   pgmoneta_json_create(&int_array);
   pgmoneta_json_create(&str_array);
   pgmoneta_json_create(&json_item_shallow);

   pgmoneta_json_create(&json_array_nested_item1);
   pgmoneta_json_create(&json_array_nested_item2);
   pgmoneta_json_create(&json_array_item_nested);

   pgmoneta_json_create(&json_array_nested_array1);
   pgmoneta_json_create(&json_array_nested_array2);
   pgmoneta_json_create(&json_array_array_nested);

   pgmoneta_json_create(&json_item_nested_array1);
   pgmoneta_json_create(&json_item_nested_array2);
   pgmoneta_json_create(&json_item_array_nested);

   pgmoneta_json_create(&json_item_nested_item1);
   pgmoneta_json_create(&json_item_nested_item2);
   pgmoneta_json_create(&json_item_item_nested);

   pgmoneta_json_put(obj, "int_array", (uintptr_t)int_array, ValueJSON);
   pgmoneta_json_put(obj, "str_array", (uintptr_t)str_array, ValueJSON);
   pgmoneta_json_put(obj, "json_item_shallow", (uintptr_t)json_item_shallow, ValueJSON);
   pgmoneta_json_put(obj, "json_array_item_nested", (uintptr_t)json_array_item_nested, ValueJSON);
   pgmoneta_json_put(obj, "json_array_array_nested", (uintptr_t)json_array_array_nested, ValueJSON);
   pgmoneta_json_put(obj, "json_item_array_nested", (uintptr_t)json_item_array_nested, ValueJSON);
   pgmoneta_json_put(obj, "json_item_item_nested", (uintptr_t)json_item_item_nested, ValueJSON);
   pgmoneta_json_put(obj, "empty_value", (uintptr_t)"", ValueString);
   pgmoneta_json_put(obj, "null_value", (uintptr_t)NULL, ValueString);

   pgmoneta_json_append(int_array, 1, ValueInt32);
   pgmoneta_json_append(int_array, 2, ValueInt32);
   pgmoneta_json_append(int_array, 3, ValueInt32);

   pgmoneta_json_append(str_array, (uintptr_t)"str1", ValueString);
   pgmoneta_json_append(str_array, (uintptr_t)"str2", ValueString);
   pgmoneta_json_append(str_array, (uintptr_t)"str3", ValueString);

   pgmoneta_json_put(json_item_shallow, "int", (uintptr_t)-1, ValueInt32);
   pgmoneta_json_put(json_item_shallow, "float", pgmoneta_value_from_float(-2.5), ValueFloat);
   pgmoneta_json_put(json_item_shallow, "double", pgmoneta_value_from_double(2.5), ValueDouble);
   pgmoneta_json_put(json_item_shallow, "bool_true", true, ValueBool);
   pgmoneta_json_put(json_item_shallow, "bool_false", false, ValueBool);
   pgmoneta_json_put(json_item_shallow, "string", (uintptr_t)"str", ValueString);

   pgmoneta_json_put(json_array_nested_item1, "1", 1, ValueInt32);
   pgmoneta_json_put(json_array_nested_item1, "2", 2, ValueInt32);
   pgmoneta_json_put(json_array_nested_item1, "3", 3, ValueInt32);
   pgmoneta_json_put(json_array_nested_item2, "1", (uintptr_t)"1", ValueString);
   pgmoneta_json_put(json_array_nested_item2, "2", (uintptr_t)"2", ValueString);
   pgmoneta_json_put(json_array_nested_item2, "3", (uintptr_t)"3", ValueString);
   pgmoneta_json_append(json_array_item_nested, (uintptr_t)json_array_nested_item1, ValueJSON);
   pgmoneta_json_append(json_array_item_nested, (uintptr_t)json_array_nested_item2, ValueJSON);

   pgmoneta_json_append(json_array_nested_array1, (uintptr_t)"1", ValueString);
   pgmoneta_json_append(json_array_nested_array1, (uintptr_t)"2", ValueString);
   pgmoneta_json_append(json_array_nested_array1, (uintptr_t)"3", ValueString);
   pgmoneta_json_append(json_array_nested_array2, true, ValueBool);
   pgmoneta_json_append(json_array_nested_array2, false, ValueBool);
   pgmoneta_json_append(json_array_nested_array2, false, ValueBool);
   pgmoneta_json_append(json_array_array_nested, (uintptr_t)json_array_nested_array1, ValueJSON);
   pgmoneta_json_append(json_array_array_nested, (uintptr_t)json_array_nested_array2, ValueJSON);

   pgmoneta_json_append(json_item_nested_array1, (uintptr_t)"1", ValueString);
   pgmoneta_json_append(json_item_nested_array1, (uintptr_t)"2", ValueString);
   pgmoneta_json_append(json_item_nested_array1, (uintptr_t)"3", ValueString);
   pgmoneta_json_append(json_item_nested_array2, true, ValueBool);
   pgmoneta_json_append(json_item_nested_array2, false, ValueBool);
   pgmoneta_json_append(json_item_nested_array2, true, ValueBool);
   pgmoneta_json_append(json_item_array_nested, (uintptr_t)json_item_nested_array1, ValueJSON);
   pgmoneta_json_append(json_item_array_nested, (uintptr_t)json_item_nested_array2, ValueJSON);

   pgmoneta_json_put(json_item_nested_item1, "1", 1, ValueInt32);
   pgmoneta_json_put(json_item_nested_item1, "2", 2, ValueInt32);
   pgmoneta_json_put(json_item_nested_item1, "3", 3, ValueInt32);
   pgmoneta_json_put(json_item_nested_item2, "1", (uintptr_t)"1", ValueString);
   pgmoneta_json_put(json_item_nested_item2, "2", (uintptr_t)"2", ValueString);
   pgmoneta_json_put(json_item_nested_item2, "3", (uintptr_t)"3", ValueString);
   pgmoneta_json_append(json_item_item_nested, (uintptr_t)json_item_nested_item1, ValueJSON);
   pgmoneta_json_append(json_item_array_nested, (uintptr_t)json_item_nested_item2, ValueJSON);

   str_obj = pgmoneta_json_to_string(obj, FORMAT_JSON, NULL, 0);
   MCTF_ASSERT(!pgmoneta_json_parse_string(str_obj, &obj_parsed), cleanup, "json parse failed");
   MCTF_ASSERT_PTR_NONNULL(obj_parsed, cleanup, "parsed object is null");

   str_obj_parsed = pgmoneta_json_to_string(obj_parsed, FORMAT_JSON, NULL, 0);
   MCTF_ASSERT_STR_EQ(str_obj, str_obj_parsed, cleanup, "json format mismatch");

   free(str_obj);
   str_obj = NULL;
   free(str_obj_parsed);
   str_obj_parsed = NULL;

   str_obj = pgmoneta_json_to_string(obj, FORMAT_TEXT, NULL, 0);
   str_obj_parsed = pgmoneta_json_to_string(obj_parsed, FORMAT_TEXT, NULL, 0);
   MCTF_ASSERT_STR_EQ(str_obj, str_obj_parsed, cleanup, "text format mismatch");

cleanup:
   if (str_obj)
   {
      free(str_obj);
   }
   if (str_obj_parsed)
   {
      free(str_obj_parsed);
   }
   pgmoneta_json_destroy(obj);
   pgmoneta_json_destroy(obj_parsed);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_json_remove)
{
   struct json* obj = NULL;
   struct json* array = NULL;

   pgmoneta_test_setup();

   pgmoneta_json_create(&obj);
   pgmoneta_json_create(&array);

   pgmoneta_json_put(obj, "key1", (uintptr_t)"1", ValueString);
   pgmoneta_json_put(obj, "key2", 2, ValueInt32);
   pgmoneta_json_append(array, (uintptr_t)"key1", ValueString);

   MCTF_ASSERT(pgmoneta_json_remove(array, "key1"), cleanup, "should fail to remove from array");
   MCTF_ASSERT(pgmoneta_json_remove(obj, ""), cleanup, "should fail for empty key");
   MCTF_ASSERT(pgmoneta_json_remove(obj, NULL), cleanup, "should fail for null key");
   MCTF_ASSERT(pgmoneta_json_remove(NULL, "key1"), cleanup, "should fail for null object");

   MCTF_ASSERT(pgmoneta_json_contains_key(obj, "key1"), cleanup, "should contain key1");
   MCTF_ASSERT(!pgmoneta_json_remove(obj, "key3"), cleanup, "non-existent key should return 0");
   MCTF_ASSERT(!pgmoneta_json_remove(obj, "key1"), cleanup, "remove key1 should succeed");
   MCTF_ASSERT(!pgmoneta_json_contains_key(obj, "key1"), cleanup, "should not contain key1");
   MCTF_ASSERT_INT_EQ(obj->type, JSONItem, cleanup, "json type should still be JSONItem");

   // double delete
   MCTF_ASSERT(!pgmoneta_json_remove(obj, "key1"), cleanup, "double delete should return 0");

   MCTF_ASSERT(pgmoneta_json_contains_key(obj, "key2"), cleanup, "should contain key2");
   MCTF_ASSERT(!pgmoneta_json_remove(obj, "key2"), cleanup, "remove key2 should succeed");
   MCTF_ASSERT(!pgmoneta_json_contains_key(obj, "key2"), cleanup, "should not contain key2");
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, cleanup, "json type should be JSONUnknown");

   // double delete
   MCTF_ASSERT(!pgmoneta_json_remove(obj, "key2"), cleanup, "double delete should return 0");

cleanup:
   pgmoneta_json_destroy(obj);
   pgmoneta_json_destroy(array);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_json_iterator)
{
   struct json* item = NULL;
   struct json* array = NULL;
   struct json_iterator* iiter = NULL;
   struct json_iterator* aiter = NULL;
   char key[2] = {0};
   int cnt = 0;

   pgmoneta_test_setup();

   pgmoneta_json_create(&item);
   pgmoneta_json_create(&array);

   MCTF_ASSERT(pgmoneta_json_iterator_create(NULL, &iiter), cleanup, "should fail for null json");
   MCTF_ASSERT(pgmoneta_json_iterator_create(item, &aiter), cleanup, "should fail for unknown type");

   pgmoneta_json_put(item, "1", 1, ValueInt32);
   pgmoneta_json_put(item, "2", 2, ValueInt32);
   pgmoneta_json_put(item, "3", 3, ValueInt32);

   pgmoneta_json_append(array, 1, ValueInt32);
   pgmoneta_json_append(array, 2, ValueInt32);
   pgmoneta_json_append(array, 3, ValueInt32);

   MCTF_ASSERT(!pgmoneta_json_iterator_create(item, &iiter), cleanup, "iterator create failed");
   MCTF_ASSERT(!pgmoneta_json_iterator_create(array, &aiter), cleanup, "array iterator create failed");
   MCTF_ASSERT(pgmoneta_json_iterator_has_next(iiter), cleanup, "should have next (item)");
   MCTF_ASSERT(pgmoneta_json_iterator_has_next(aiter), cleanup, "should have next (array)");

   while (pgmoneta_json_iterator_next(iiter))
   {
      cnt++;
      key[0] = '0' + cnt;
      MCTF_ASSERT_STR_EQ(iiter->key, key, cleanup, "key mismatch");
      MCTF_ASSERT_INT_EQ(iiter->value->data, cnt, cleanup, "value mismatch");
   }

   cnt = 0;

   while (pgmoneta_json_iterator_next(aiter))
   {
      cnt++;
      MCTF_ASSERT_INT_EQ(aiter->value->data, cnt, cleanup, "array value mismatch");
   }

   MCTF_ASSERT(!pgmoneta_json_iterator_has_next(iiter), cleanup, "should not have next (item)");
   MCTF_ASSERT(!pgmoneta_json_iterator_has_next(aiter), cleanup, "should not have next (array)");

cleanup:
   pgmoneta_json_iterator_destroy(iiter);
   pgmoneta_json_iterator_destroy(aiter);
   pgmoneta_json_destroy(item);
   pgmoneta_json_destroy(array);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}
