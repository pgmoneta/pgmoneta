/*
* Copyright (C) 2025 The pgmoneta community
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
#include <tssuite.h>
#include <utils.h>

START_TEST(test_json_create)
{
    struct json* obj = NULL;

    ck_assert(!pgmoneta_json_create(&obj));
    ck_assert_ptr_nonnull(obj);
    ck_assert_int_eq(obj->type, JSONUnknown);

    pgmoneta_json_destroy(obj);
}
END_TEST

START_TEST(test_json_put_basic)
{
    struct json* obj = NULL;

    ck_assert(!pgmoneta_json_create(&obj));
    ck_assert_ptr_nonnull(obj);
    ck_assert_int_eq(obj->type, JSONUnknown);

    ck_assert(!pgmoneta_json_put(obj, "key1", (uintptr_t)"value1", ValueString));
    ck_assert(pgmoneta_json_contains_key(obj, "key1"));
    ck_assert_str_eq((char*)pgmoneta_json_get(obj, "key1"), "value1");
    ck_assert_int_eq(obj->type, JSONItem);

    // json only takes in certain types of value
    ck_assert(pgmoneta_json_put(obj, "key2", (uintptr_t)"value1", ValueMem));
    ck_assert(!pgmoneta_json_contains_key(obj, "key2"));

    // item should not take entry input
    ck_assert(pgmoneta_json_append(obj, (uintptr_t)"entry", ValueString));

    pgmoneta_json_destroy(obj);
}
END_TEST

START_TEST(test_json_append_basic)
{
    struct json* obj = NULL;

    ck_assert(!pgmoneta_json_create(&obj));
    ck_assert_ptr_nonnull(obj);
    ck_assert_int_eq(obj->type, JSONUnknown);

    ck_assert(!pgmoneta_json_append(obj, (uintptr_t)"value1", ValueString));
    ck_assert_int_eq(obj->type, JSONArray);

    ck_assert(pgmoneta_json_append(obj, (uintptr_t)"value2", ValueMem));
    ck_assert(pgmoneta_json_put(obj, "key", (uintptr_t)"value", ValueString));

    pgmoneta_json_destroy(obj);
}
END_TEST

START_TEST(test_json_parse_to_string)
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
    pgmoneta_json_put(json_array_nested_item2, "3", (uintptr_t)"3", ValueString );
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
    pgmoneta_json_put(json_item_nested_item2, "3", (uintptr_t)"3", ValueString );
    pgmoneta_json_append(json_item_item_nested, (uintptr_t)json_item_nested_item1, ValueJSON);
    pgmoneta_json_append(json_item_array_nested, (uintptr_t)json_item_nested_item2, ValueJSON);

    str_obj = pgmoneta_json_to_string(obj, FORMAT_JSON, NULL, 0);
    ck_assert(!pgmoneta_json_parse_string(str_obj, &obj_parsed));
    ck_assert_ptr_nonnull(obj_parsed);

    str_obj_parsed = pgmoneta_json_to_string(obj_parsed, FORMAT_JSON, NULL, 0);
    ck_assert_str_eq(str_obj, str_obj_parsed);

    free(str_obj);
    str_obj = NULL;
    free(str_obj_parsed);
    str_obj_parsed = NULL;

    str_obj = pgmoneta_json_to_string(obj, FORMAT_TEXT, NULL, 0);
    str_obj_parsed = pgmoneta_json_to_string(obj_parsed, FORMAT_TEXT, NULL, 0);
    ck_assert_str_eq(str_obj, str_obj_parsed);

    free(str_obj);
    str_obj = NULL;
    free(str_obj_parsed);
    str_obj_parsed = NULL;

    pgmoneta_json_destroy(obj);
    pgmoneta_json_destroy(obj_parsed);
}
END_TEST

START_TEST(test_json_remove)
{
    struct json* obj = NULL;
    struct json* array = NULL;
    pgmoneta_json_create(&obj);
    pgmoneta_json_create(&array);

    pgmoneta_json_put(obj, "key1", (uintptr_t)"1", ValueString);
    pgmoneta_json_put(obj, "key2", 2, ValueInt32);
    pgmoneta_json_append(array, (uintptr_t)"key1", ValueString);
    ck_assert(pgmoneta_json_remove(array, "key1"));
    ck_assert(pgmoneta_json_remove(obj, ""));
    ck_assert(pgmoneta_json_remove(obj, NULL));
    ck_assert(pgmoneta_json_remove(NULL, "key1"));

    ck_assert(pgmoneta_json_contains_key(obj, "key1"));
    ck_assert(!pgmoneta_json_remove(obj, "key3"));
    ck_assert(!pgmoneta_json_remove(obj, "key1"));
    ck_assert(!pgmoneta_json_contains_key(obj, "key1"));
    ck_assert_int_eq(obj->type, JSONItem);

    // double delete
    ck_assert(!pgmoneta_json_remove(obj, "key1"));

    ck_assert(pgmoneta_json_contains_key(obj, "key2"));
    ck_assert(!pgmoneta_json_remove(obj, "key2"));
    ck_assert(!pgmoneta_json_contains_key(obj, "key2"));
    ck_assert_int_eq(obj->type, JSONUnknown);

    // double delete
    ck_assert(!pgmoneta_json_remove(obj, "key2"));

    pgmoneta_json_destroy(obj);
    pgmoneta_json_destroy(array);
}
END_TEST

START_TEST(test_json_iterator)
{
    struct json* item = NULL;
    struct json* array = NULL;
    struct json_iterator* iiter = NULL;
    struct json_iterator* aiter = NULL;
    char key[2] = {0};
    int cnt = 0;

    pgmoneta_json_create(&item);
    pgmoneta_json_create(&array);

    ck_assert(pgmoneta_json_iterator_create(NULL, &iiter));
    ck_assert_msg(pgmoneta_json_iterator_create(item, &aiter),
        "iterator creation should fail if json type is unknown");

    pgmoneta_json_put(item, "1", 1, ValueInt32);
    pgmoneta_json_put(item, "2", 2, ValueInt32);
    pgmoneta_json_put(item, "3", 3, ValueInt32);

    pgmoneta_json_append(array, 1, ValueInt32);
    pgmoneta_json_append(array, 2, ValueInt32);
    pgmoneta_json_append(array, 3, ValueInt32);

    ck_assert(!pgmoneta_json_iterator_create(item, &iiter));
    ck_assert(!pgmoneta_json_iterator_create(array, &aiter));
    ck_assert(pgmoneta_json_iterator_has_next(iiter));
    ck_assert(pgmoneta_json_iterator_has_next(aiter));

    while (pgmoneta_json_iterator_next(iiter))
    {
        cnt++;
        key[0] = '0' + cnt;
        ck_assert_str_eq(iiter->key, key);
        ck_assert_int_eq(iiter->value->data, cnt);
    }

    cnt = 0;

    while (pgmoneta_json_iterator_next(aiter))
    {
        cnt++;
        ck_assert_int_eq(aiter->value->data, cnt);
    }

    ck_assert(!pgmoneta_json_iterator_has_next(iiter));
    ck_assert(!pgmoneta_json_iterator_has_next(aiter));

    pgmoneta_json_iterator_destroy(iiter);
    pgmoneta_json_iterator_destroy(aiter);

    pgmoneta_json_destroy(item);
    pgmoneta_json_destroy(array);
}
END_TEST

Suite*
pgmoneta_test_json_suite()
{
    Suite* s;
    TCase* tc_json_basic;

    s = suite_create("pgmoneta_test_json");

    tc_json_basic = tcase_create("json_basic_test");
    tcase_set_timeout(tc_json_basic, 60);
    tcase_add_checked_fixture(tc_json_basic, pgmoneta_test_setup, pgmoneta_test_teardown);
    tcase_add_test(tc_json_basic, test_json_create);
    tcase_add_test(tc_json_basic, test_json_put_basic);
    tcase_add_test(tc_json_basic, test_json_append_basic);
    tcase_add_test(tc_json_basic, test_json_parse_to_string);
    tcase_add_test(tc_json_basic, test_json_remove);
    tcase_add_test(tc_json_basic, test_json_iterator);

    suite_add_tcase(s, tc_json_basic);

    return s;
}