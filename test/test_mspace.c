//
//  Copyright (C) 2022  Nick Gasson
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "test_util.h"
#include "rt/mspace.h"

#include <stdlib.h>

START_TEST(test_sanity)
{
   mspace_t *m = mspace_new(1024);

   int *ptr1 = mspace_alloc(m, sizeof(int));
   ck_assert_ptr_nonnull(ptr1);
   *ptr1 = 42;

   int *ptr2 = mspace_alloc(m, sizeof(int));
   ck_assert_ptr_nonnull(ptr2);
   ck_assert_ptr_ne(ptr1, ptr2);
   *ptr2 = 55;

   ck_assert_ptr_null(mspace_alloc(m, 0));

   int *array = mspace_alloc(m, 50 * sizeof(int));
   array[49] = 1234;
   array = NULL;

   // Do enough allocations to trigger a GC
   for (int i = 0; i < 1000; i++) {
      int *garbage = mspace_alloc(m, sizeof(int));
      ck_assert_ptr_nonnull(garbage);
      ck_assert_ptr_ne(garbage, ptr1);
      ck_assert_ptr_ne(garbage, ptr2);
      *garbage = 0xdead;
   }

   ck_assert_int_eq(*ptr1, 42);
   ck_assert_int_eq(*ptr2, 55);

   mspace_destroy(m);
}
END_TEST

__attribute__((noinline))
static int **get_indirect(mspace_t *m)
{
   int *ptr1 = mspace_alloc(m, sizeof(int));
   ck_assert_ptr_nonnull(ptr1);
   *ptr1 = 42;

   int **ptr2 = mspace_alloc(m, sizeof(int *));
   ck_assert_ptr_nonnull(ptr2);
   ck_assert_ptr_ne(ptr1, ptr2);
   *ptr2 = ptr1;

   return ptr2;
}

__attribute__((noinline))
static void generate_garbage(mspace_t *m, int count, size_t size)
{
   for (int i = 0; i < count; i++) {
      int *garbage = mspace_alloc(m, size);
      ck_assert_ptr_nonnull(garbage);
      *garbage = 0xdead;
   }
}

START_TEST(test_indirect)
{
   mspace_t *m = mspace_new(1024);

   // Do a few dummy allocations to ensure the pointer is not in the
   // first line which is often kept alive by stack pointers to m->space
   generate_garbage(m, 5, sizeof(int));

   int *volatile* ptr2 = get_indirect(m);

   // Do enough allocations to trigger a GC
   generate_garbage(m, 1000, sizeof(int));

   ck_assert_int_eq(**ptr2, 42);

   mspace_destroy(m);
}
END_TEST

START_TEST(test_mptr)
{
   mspace_t *m = mspace_new(1024);

   // Do a few dummy allocations to ensure the pointer is not in the
   // first line which is often kept alive by stack pointers to m->space
   generate_garbage(m, 5, sizeof(int));

   mptr_t p = mptr_new(m, "test");
   *mptr_get(p) = mspace_alloc(m, sizeof(int));
   *(int *)*mptr_get(p) = 42;

   // Do enough allocations to trigger a GC
   generate_garbage(m, 1000, sizeof(int));

   ck_assert_int_eq(*(int *)*mptr_get(p), 42);

   mptr_free(m, &p);
   ck_assert_ptr_eq(p, MPTR_INVALID);

   mspace_destroy(m);
}
END_TEST

START_TEST(test_large)
{
   mspace_t *m = mspace_new(5 * 1024);

   // Do a few dummy allocations to ensure the pointer is not in the
   // first line which is often kept alive by stack pointers to m->space
   generate_garbage(m, 50, sizeof(int));

   int *volatile* parray = get_indirect(m);

   *parray = mspace_alloc(m, 100 * sizeof(int));
   (*parray)[99] = 1234;

   // Do enough allocations to trigger a GC
   generate_garbage(m, 1000, 5 * sizeof(int));

   ck_assert_int_eq((*parray)[99], 1234);

   mspace_destroy(m);
}
END_TEST

START_TEST(test_interior)
{
   mspace_t *m = mspace_new(5 * 1024);

   // Do a few dummy allocations to ensure the pointer is not in the
   // first line which is often kept alive by stack pointers to m->space
   generate_garbage(m, 10, sizeof(int));

   int *volatile* parray = get_indirect(m);

   // Interior pointers keep the whole allocation alive
   *parray = mspace_alloc(m, 100 * sizeof(int)) + (13 * sizeof(int));
   (*parray)[49] = 1234;

   // Do enough allocations to trigger a GC
   generate_garbage(m, 1000, 5 * sizeof(int));

   ck_assert_int_eq((*parray)[49], 1234);

   mspace_destroy(m);
}
END_TEST

static size_t oom_sz = 0;

static void test_oom_cb(mspace_t *m, size_t size)
{
   oom_sz = size;
}

START_TEST(test_oom)
{
   mspace_t *m = mspace_new(1024);
   mspace_set_oom_handler(m, test_oom_cb);

   void *p = mspace_alloc(m, 1000000);
   ck_assert_ptr_null(p);
   ck_assert_int_eq(oom_sz, 1000000);

   mspace_destroy(m);
}
END_TEST

START_TEST(test_linked_list)
{
   struct list {
      struct list *next;
      int value;
   };

   mspace_t *m = mspace_new(4096);

   struct list *head = NULL, **tailp = &head;

   for (int i = 0; i < 40; i++) {
      struct list *l = mspace_alloc(m, sizeof(struct list));
      l->value = i;
      l->next = NULL;

      *tailp = l;
      tailp = &(l->next);

      // Do some dummy allocations to make sure the nodes are at random
      // locations in the heap
      for (int i = 0; i < 10; i++) {
         int *garbage = mspace_alloc(m, (1 + rand() % 10) * sizeof(int));
         ck_assert_ptr_nonnull(garbage);
         *garbage = 0xdead;
      }
   }

   struct list *it = head;
   for (int i = 0; i < 40; i++, it = it->next)
      ck_assert_int_eq(it->value, i);

   mspace_destroy(m);
}
END_TEST

__attribute__((noinline))
static void put_value(mspace_t *m, int **ptr, int value)
{
   int *st = mspace_alloc(m, sizeof(int));
   *st = value;
   *ptr = st;
}

START_TEST(test_tlab)
{
   mspace_t *m = mspace_new(2 * TLAB_SIZE);

   tlab_t *t = tlab_acquire(m);

   int *precious = tlab_alloc(t, sizeof(int));
   *precious = 0xbeef;

   int **array = tlab_alloc(t, sizeof(int *) * 10);
   for (int i = 0; i < 10; i++)
      put_value(m, array + i, i);

   int **p1 = tlab_alloc(t, sizeof(int *));
   *p1 = precious;

   for (int i = 0; i < 1000; i++) {
      int *array = tlab_alloc(t, 50 * sizeof(int));
      array[0] = 123;

      for (int i = 0; i < 10; i++) {
         int *garbage = mspace_alloc(m, sizeof(int));
         ck_assert_ptr_nonnull(garbage);
         *garbage = 0xdead;
      }
   }

   ck_assert_int_eq(*precious, 0xbeef);

   for (int i = 0; i < 10; i++)
      ck_assert_int_eq(*array[i], i);

   tlab_release(t);
   mspace_destroy(m);
}
END_TEST

START_TEST(test_end_ptr)
{
   mspace_t *m = mspace_new(5 * 1024);

   // Do a few dummy allocations to ensure the pointer is not in the
   // first line which is often kept alive by stack pointers to m->space
   generate_garbage(m, 50, sizeof(int));

   // Size is a multiple of LINE_SIZE
   static const int SIZE = 64;
   int *volatile array = mspace_alloc(m, SIZE * sizeof(int));
   for (int i = 0; i < SIZE; i++)
      *array++ = i;
   // Now array points one past the end of the of the allocation

   // Do enough allocations to trigger a GC
   generate_garbage(m, 1000, 5 * sizeof(int));

   ck_assert_int_eq(array[-SIZE], 0);
   ck_assert_int_eq(array[-1], SIZE - 1);

   mspace_destroy(m);
}
END_TEST

Suite *get_mspace_tests(void)
{
   Suite *s = suite_create("mspace");

   TCase *tc = nvc_unit_test();
   tcase_add_test(tc, test_sanity);
   tcase_add_test(tc, test_indirect);
   tcase_add_test(tc, test_mptr);
   tcase_add_test(tc, test_large);
   tcase_add_test(tc, test_interior);
   tcase_add_test(tc, test_oom);
   tcase_add_test(tc, test_linked_list);
   tcase_add_test(tc, test_tlab);
   tcase_add_test(tc, test_end_ptr);
   suite_add_tcase(s, tc);

   return s;
}
