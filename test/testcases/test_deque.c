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
#include <deque.h>
#include <tscommon.h>
#include <mctf.h>
#include <utils.h>
#include <value.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct deque_test_obj
{
   char* str;
   int idx;
};

static void test_obj_create(int idx, struct deque_test_obj** obj);
static void test_obj_destroy(struct deque_test_obj* obj);
static void test_obj_destroy_cb(uintptr_t obj);

MCTF_TEST(test_deque_create)
{
   struct deque* dq = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_deque_create(false, &dq), "deque creation failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(dq, "deque is null", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 0, "deque size should be 0", cleanup);

cleanup:
   pgmoneta_deque_destroy(dq);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_deque_add_poll)
{
   struct deque* dq = NULL;
   char* value1 = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_deque_create(false, &dq), "deque creation failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, NULL, (uintptr_t)-1, ValueInt32), "add int failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, NULL, (uintptr_t)true, ValueBool), "add bool failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, NULL, (uintptr_t)"value1", ValueString), "add string failed", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 3, "deque size should be 3", cleanup);

   MCTF_ASSERT_INT_EQ((int)pgmoneta_deque_peek(dq, NULL), -1, "peek failed", cleanup);
   MCTF_ASSERT_INT_EQ((int)pgmoneta_deque_poll(dq, NULL), -1, "poll int failed", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 2, "deque size should be 2", cleanup);

   MCTF_ASSERT((bool)pgmoneta_deque_poll(dq, NULL), "poll bool failed", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 1, "deque size should be 1", cleanup);

   value1 = (char*)pgmoneta_deque_poll(dq, NULL);
   MCTF_ASSERT_PTR_NONNULL(value1, "poll string returned null", cleanup);
   MCTF_ASSERT_STR_EQ(value1, "value1", "poll string value mismatch", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 0, "deque size should be 0", cleanup);

   MCTF_ASSERT_INT_EQ(pgmoneta_deque_poll(dq, NULL), 0, "poll empty should return 0", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 0, "deque size should still be 0", cleanup);

cleanup:
   if (value1)
   {
      free(value1);
   }
   if (dq)
   {
      pgmoneta_deque_destroy(dq);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_deque_add_poll_last)
{
   struct deque* dq = NULL;
   char* value1 = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_deque_create(false, &dq), "deque creation failed", cleanup);
   pgmoneta_deque_add(dq, NULL, 0, ValueNone);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, NULL, (uintptr_t)"value1", ValueString), "add string failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, NULL, (uintptr_t)true, ValueBool), "add bool failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, NULL, (uintptr_t)-1, ValueInt32), "add int failed", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 3, "deque size should be 3", cleanup);

   MCTF_ASSERT_INT_EQ((int)pgmoneta_deque_peek_last(dq, NULL), -1, "peek_last failed", cleanup);
   MCTF_ASSERT_INT_EQ((int)pgmoneta_deque_poll_last(dq, NULL), -1, "poll_last int failed", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 2, "deque size should be 2", cleanup);

   MCTF_ASSERT((bool)pgmoneta_deque_poll_last(dq, NULL), "poll_last bool failed", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 1, "deque size should be 1", cleanup);

   value1 = (char*)pgmoneta_deque_poll_last(dq, NULL);
   MCTF_ASSERT_PTR_NONNULL(value1, "poll_last string returned null", cleanup);
   MCTF_ASSERT_STR_EQ(value1, "value1", "poll_last string value mismatch", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 0, "deque size should be 0", cleanup);

   MCTF_ASSERT_INT_EQ(pgmoneta_deque_poll_last(dq, NULL), 0, "poll_last empty should return 0", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 0, "deque size should still be 0", cleanup);

cleanup:
   if (value1)
   {
      free(value1);
   }
   if (dq)
   {
      pgmoneta_deque_destroy(dq);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_deque_clear)
{
   struct deque* dq = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_deque_create(false, &dq), "deque creation failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, NULL, (uintptr_t)"value1", ValueString), "add string failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, NULL, (uintptr_t)true, ValueBool), "add bool failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, NULL, (uintptr_t)-1, ValueInt32), "add int failed", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 3, "deque size should be 3", cleanup);

   pgmoneta_deque_clear(dq);
   MCTF_ASSERT_INT_EQ(dq->size, 0, "deque size should be 0 after clear", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_poll(dq, NULL), 0, "poll after clear should return 0", cleanup);

cleanup:
   if (dq)
   {
      pgmoneta_deque_destroy(dq);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_deque_remove)
{
   struct deque* dq = NULL;
   char* value1 = NULL;
   char* tag = NULL;

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_deque_create(false, &dq), "deque creation failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, "tag1", (uintptr_t)"value1", ValueString), "add string failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, "tag2", (uintptr_t)true, ValueBool), "add bool failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, "tag2", (uintptr_t)-1, ValueInt32), "add int failed", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 3, "deque size should be 3", cleanup);

   MCTF_ASSERT_INT_EQ(pgmoneta_deque_remove(dq, NULL), 0, "remove with NULL tag should return 0", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_remove(NULL, "tag2"), 0, "remove with NULL deque should return 0", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_remove(dq, "tag3"), 0, "remove non-existent tag should return 0", cleanup);

   MCTF_ASSERT_INT_EQ(pgmoneta_deque_remove(dq, "tag2"), 2, "remove tag2 should return 2", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 1, "deque size should be 1", cleanup);

   value1 = (char*)pgmoneta_deque_peek(dq, &tag);
   MCTF_ASSERT_PTR_NONNULL(value1, "peek returned null", cleanup);
   MCTF_ASSERT_STR_EQ(value1, "value1", "peek value mismatch", cleanup);
   MCTF_ASSERT_PTR_NONNULL(tag, "peek tag is null", cleanup);
   MCTF_ASSERT_STR_EQ(tag, "tag1", "peek tag mismatch", cleanup);

cleanup:
   if (dq)
   {
      pgmoneta_deque_destroy(dq);
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_deque_add_with_config_and_get)
{
   struct deque* dq = NULL;
   struct value_config test_obj_config = {.destroy_data = test_obj_destroy_cb, .to_string = NULL};
   struct deque_test_obj* obj1 = NULL;
   struct deque_test_obj* obj2 = NULL;
   struct deque_test_obj* obj3 = NULL;

   pgmoneta_test_setup();

   test_obj_create(1, &obj1);
   test_obj_create(2, &obj2);
   test_obj_create(3, &obj3);

   MCTF_ASSERT(!pgmoneta_deque_create(false, &dq), "deque creation failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add_with_config(dq, "tag1", (uintptr_t)obj1, &test_obj_config), "add obj1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add_with_config(dq, "tag2", (uintptr_t)obj2, &test_obj_config), "add obj2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add_with_config(dq, "tag3", (uintptr_t)obj3, &test_obj_config), "add obj3 failed", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 3, "deque size should be 3", cleanup);

   {
      struct deque_test_obj* got = (struct deque_test_obj*)pgmoneta_deque_get(dq, "tag1");
      MCTF_ASSERT_PTR_NONNULL(got, "get tag1 returned null", cleanup);
      MCTF_ASSERT_INT_EQ(got->idx, 1, "obj1 idx mismatch", cleanup);
      MCTF_ASSERT_STR_EQ(got->str, "obj1", "obj1 str mismatch", cleanup);
   }

   {
      struct deque_test_obj* got = (struct deque_test_obj*)pgmoneta_deque_get(dq, "tag2");
      MCTF_ASSERT_PTR_NONNULL(got, "get tag2 returned null", cleanup);
      MCTF_ASSERT_INT_EQ(got->idx, 2, "obj2 idx mismatch", cleanup);
      MCTF_ASSERT_STR_EQ(got->str, "obj2", "obj2 str mismatch", cleanup);
   }

   {
      struct deque_test_obj* got = (struct deque_test_obj*)pgmoneta_deque_get(dq, "tag3");
      MCTF_ASSERT_PTR_NONNULL(got, "get tag3 returned null", cleanup);
      MCTF_ASSERT_INT_EQ(got->idx, 3, "obj3 idx mismatch", cleanup);
      MCTF_ASSERT_STR_EQ(got->str, "obj3", "obj3 str mismatch", cleanup);
   }

cleanup:
   if (dq)
   {
      pgmoneta_deque_destroy(dq);
      obj1 = NULL;
      obj2 = NULL;
      obj3 = NULL;
   }
   else
   {
      if (obj1)
      {
         test_obj_destroy(obj1);
      }
      if (obj2)
      {
         test_obj_destroy(obj2);
      }
      if (obj3)
      {
         test_obj_destroy(obj3);
      }
   }
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_deque_iterator_read)
{
   struct deque* dq = NULL;
   struct deque_iterator* iter = NULL;
   int cnt = 0;
   char tag[2] = {0};

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_deque_create(false, &dq), "deque creation failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, "1", 1, ValueInt32), "add 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, "2", 2, ValueInt32), "add 2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, "3", 3, ValueInt32), "add 3 failed", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 3, "deque size should be 3", cleanup);

   MCTF_ASSERT(pgmoneta_deque_iterator_create(NULL, &iter), "iterator create with NULL should fail", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_iterator_create(dq, &iter), "iterator creation failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(iter, "iterator is null", cleanup);
   MCTF_ASSERT(pgmoneta_deque_iterator_has_next(iter), "iterator should have next", cleanup);

   while (pgmoneta_deque_iterator_next(iter))
   {
      cnt++;
      MCTF_ASSERT_INT_EQ(pgmoneta_value_data(iter->value), cnt, "iterator value mismatch", cleanup);
      tag[0] = '0' + cnt;
      MCTF_ASSERT_STR_EQ(iter->tag, tag, "iterator tag mismatch", cleanup);
   }
   MCTF_ASSERT_INT_EQ(cnt, 3, "iterator count should be 3", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_iterator_has_next(iter), "iterator should not have next", cleanup);

cleanup:
   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(dq);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_deque_iterator_remove)
{
   struct deque* dq = NULL;
   struct deque_iterator* iter = NULL;
   int cnt = 0;
   char tag[2] = {0};

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_deque_create(false, &dq), "deque creation failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, "1", 1, ValueInt32), "add 1 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, "2", 2, ValueInt32), "add 2 failed", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_add(dq, "3", 3, ValueInt32), "add 3 failed", cleanup);
   MCTF_ASSERT_INT_EQ(dq->size, 3, "deque size should be 3", cleanup);

   MCTF_ASSERT(pgmoneta_deque_iterator_create(NULL, &iter), "iterator create with NULL should fail", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_iterator_create(dq, &iter), "iterator creation failed", cleanup);
   MCTF_ASSERT_PTR_NONNULL(iter, "iterator is null", cleanup);
   MCTF_ASSERT(pgmoneta_deque_iterator_has_next(iter), "iterator should have next", cleanup);

   while (pgmoneta_deque_iterator_next(iter))
   {
      cnt++;
      MCTF_ASSERT_INT_EQ(pgmoneta_value_data(iter->value), cnt, "iterator value mismatch", cleanup);
      tag[0] = '0' + cnt;
      MCTF_ASSERT_STR_EQ(iter->tag, tag, "iterator tag mismatch", cleanup);

      if (cnt == 2 || cnt == 3)
      {
         pgmoneta_deque_iterator_remove(iter);
      }
   }

   /* should be no-op */
   pgmoneta_deque_iterator_remove(iter);

   MCTF_ASSERT_INT_EQ(dq->size, 1, "deque size should be 1", cleanup);
   MCTF_ASSERT(!pgmoneta_deque_iterator_has_next(iter), "iterator should not have next", cleanup);
   MCTF_ASSERT_INT_EQ(pgmoneta_deque_peek(dq, NULL), 1, "peek should return 1", cleanup);

cleanup:
   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(dq);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

MCTF_TEST(test_deque_sort)
{
   struct deque* dq = NULL;
   struct deque_iterator* iter = NULL;
   int cnt = 0;
   char tag[2] = {0};
   int index[6] = {2, 1, 3, 5, 4, 0};

   pgmoneta_test_setup();

   MCTF_ASSERT(!pgmoneta_deque_create(false, &dq), "deque creation failed", cleanup);
   for (int i = 0; i < 6; i++)
   {
      tag[0] = '0' + index[i];
      MCTF_ASSERT(!pgmoneta_deque_add(dq, tag, index[i], ValueInt32), "add failed", cleanup);
   }

   pgmoneta_deque_sort(dq);

   MCTF_ASSERT(!pgmoneta_deque_iterator_create(dq, &iter), "iterator creation failed", cleanup);

   while (pgmoneta_deque_iterator_next(iter))
   {
      MCTF_ASSERT_INT_EQ(pgmoneta_value_data(iter->value), cnt, "sorted value mismatch", cleanup);
      tag[0] = '0' + cnt;
      MCTF_ASSERT_STR_EQ(iter->tag, tag, "sorted tag mismatch", cleanup);
      cnt++;
   }

cleanup:
   pgmoneta_deque_iterator_destroy(iter);
   pgmoneta_deque_destroy(dq);
   pgmoneta_test_teardown();
   MCTF_FINISH();
}

static void
test_obj_create(int idx, struct deque_test_obj** obj)
{
   struct deque_test_obj* o = NULL;

   o = malloc(sizeof(struct deque_test_obj));
   memset(o, 0, sizeof(struct deque_test_obj));
   o->idx = idx;
   o->str = pgmoneta_append(o->str, "obj");
   o->str = pgmoneta_append_int(o->str, idx);

   *obj = o;
}

static void
test_obj_destroy(struct deque_test_obj* obj)
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
   test_obj_destroy((struct deque_test_obj*)obj);
}
