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

   MCTF_ASSERT(!pgmoneta_json_create(&obj), "json creation failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(obj, "json object is null", cleanup);
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, "json type should be JSONUnknown", cleanup);

cleanup:
   pgmoneta_json_destroy(obj);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_json_put_basic)
{
   struct json* obj = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_json_create(&obj), "json creation failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(obj, "json object is null", cleanup);
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, "json type should be JSONUnknown", cleanup);

   MCTF_ASSERT(!pgmoneta_json_put(obj, "key1", (uintptr_t)"value1", ValueString), "json put failed", cleanup);
   MCTF_ASSERT(pgmoneta_json_contains_key(obj, "key1"), "json should contain key1", cleanup);
   MCTF_ASSERT_STR_EQ((char*)pgmoneta_json_get(obj, "key1"), "value1", "value mismatch", cleanup);
   MCTF_ASSERT_INT_EQ(obj->type, JSONItem, "json type should be JSONItem", cleanup);

   // json only takes in certain types of value
   MCTF_ASSERT(pgmoneta_json_put(obj, "key2", (uintptr_t)"value1", ValueMem), "should fail for ValueMem", cleanup);
   MCTF_ASSERT(!pgmoneta_json_contains_key(obj, "key2"), "should not contain key2", cleanup);

   // item should not take entry input
   MCTF_ASSERT(pgmoneta_json_append(obj, (uintptr_t)"entry", ValueString), "item should not append", cleanup);

cleanup:
   pgmoneta_json_destroy(obj);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_json_append_basic)
{
   struct json* obj = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_json_create(&obj), "json creation failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(obj, "json object is null", cleanup);
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, "json type should be JSONUnknown", cleanup);

   MCTF_ASSERT(!pgmoneta_json_append(obj, (uintptr_t)"value1", ValueString), "json append failed", cleanup);
   MCTF_ASSERT_INT_EQ(obj->type, JSONArray, "json type should be JSONArray", cleanup);

   MCTF_ASSERT(pgmoneta_json_append(obj, (uintptr_t)"value2", ValueMem), "should fail for ValueMem", cleanup);
   MCTF_ASSERT(pgmoneta_json_put(obj, "key", (uintptr_t)"value", ValueString), "array should not put", cleanup);

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
   MCTF_ASSERT(!pgmoneta_json_parse_string(str_obj, &obj_parsed), "json parse failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(obj_parsed, "parsed object is null", cleanup);

   str_obj_parsed = pgmoneta_json_to_string(obj_parsed, FORMAT_JSON, NULL, 0);
   MCTF_ASSERT_STR_EQ(str_obj, str_obj_parsed, "json format mismatch", cleanup);

   free(str_obj);
   str_obj = NULL;
   free(str_obj_parsed);
   str_obj_parsed = NULL;

   str_obj = pgmoneta_json_to_string(obj, FORMAT_TEXT, NULL, 0);
   str_obj_parsed = pgmoneta_json_to_string(obj_parsed, FORMAT_TEXT, NULL, 0);
   MCTF_ASSERT_STR_EQ(str_obj, str_obj_parsed, "text format mismatch", cleanup);

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

   MCTF_ASSERT(pgmoneta_json_remove(array, "key1"), "should fail to remove from array", cleanup);
   MCTF_ASSERT(pgmoneta_json_remove(obj, ""), "should fail for empty key", cleanup);
   MCTF_ASSERT(pgmoneta_json_remove(obj, NULL), "should fail for null key", cleanup);
   MCTF_ASSERT(pgmoneta_json_remove(NULL, "key1"), "should fail for null object", cleanup);

   MCTF_ASSERT(pgmoneta_json_contains_key(obj, "key1"), "should contain key1", cleanup);
   MCTF_ASSERT(!pgmoneta_json_remove(obj, "key3"), "non-existent key should return 0", cleanup);
   MCTF_ASSERT(!pgmoneta_json_remove(obj, "key1"), "remove key1 should succeed", cleanup);
   MCTF_ASSERT(!pgmoneta_json_contains_key(obj, "key1"), "should not contain key1", cleanup);
   MCTF_ASSERT_INT_EQ(obj->type, JSONItem, "json type should still be JSONItem", cleanup);

   // double delete
   MCTF_ASSERT(!pgmoneta_json_remove(obj, "key1"), "double delete should return 0", cleanup);

   MCTF_ASSERT(pgmoneta_json_contains_key(obj, "key2"), "should contain key2", cleanup);
   MCTF_ASSERT(!pgmoneta_json_remove(obj, "key2"), "remove key2 should succeed", cleanup);
   MCTF_ASSERT(!pgmoneta_json_contains_key(obj, "key2"), "should not contain key2", cleanup);
   MCTF_ASSERT_INT_EQ(obj->type, JSONUnknown, "json type should be JSONUnknown", cleanup);

   // double delete
   MCTF_ASSERT(!pgmoneta_json_remove(obj, "key2"), "double delete should return 0", cleanup);

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

   MCTF_ASSERT(pgmoneta_json_iterator_create(NULL, &iiter), "should fail for null json", cleanup);
   MCTF_ASSERT(pgmoneta_json_iterator_create(item, &aiter), "should fail for unknown type", cleanup);

   pgmoneta_json_put(item, "1", 1, ValueInt32);
   pgmoneta_json_put(item, "2", 2, ValueInt32);
   pgmoneta_json_put(item, "3", 3, ValueInt32);

   pgmoneta_json_append(array, 1, ValueInt32);
   pgmoneta_json_append(array, 2, ValueInt32);
   pgmoneta_json_append(array, 3, ValueInt32);

   MCTF_ASSERT(!pgmoneta_json_iterator_create(item, &iiter), "iterator create failed", cleanup);
   MCTF_ASSERT(!pgmoneta_json_iterator_create(array, &aiter), "array iterator create failed", cleanup);
   MCTF_ASSERT(pgmoneta_json_iterator_has_next(iiter), "should have next (item)", cleanup);
   MCTF_ASSERT(pgmoneta_json_iterator_has_next(aiter), "should have next (array)", cleanup);

   while (pgmoneta_json_iterator_next(iiter))
   {
      cnt++;
      key[0] = '0' + cnt;
      MCTF_ASSERT_STR_EQ(iiter->key, key, "key mismatch", cleanup);
      MCTF_ASSERT_INT_EQ(iiter->value->data, cnt, "value mismatch", cleanup);
   }

   cnt = 0;

   while (pgmoneta_json_iterator_next(aiter))
   {
      cnt++;
      MCTF_ASSERT_INT_EQ(aiter->value->data, cnt, "array value mismatch", cleanup);
   }

   MCTF_ASSERT(!pgmoneta_json_iterator_has_next(iiter), "should not have next (item)", cleanup);
   MCTF_ASSERT(!pgmoneta_json_iterator_has_next(aiter), "should not have next (array)", cleanup);

cleanup:
   pgmoneta_json_iterator_destroy(iiter);
   pgmoneta_json_iterator_destroy(aiter);
   pgmoneta_json_destroy(item);
   pgmoneta_json_destroy(array);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}
