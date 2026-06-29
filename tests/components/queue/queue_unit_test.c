// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Queue */
#include "queue/queue.h"

static actrust_queue_t queue;

void setUp(void)
{
    queue = NULL;
}

void tearDown(void)
{
    if (queue != NULL) {
        actrust_queue_destroy(&queue);
    }
}

void test_create_null_handle(void)
{
    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_CODE(actrust_queue_create(NULL, 4u, sizeof(int))),
        ACTRUST_ERR_INVALID_ARG);
}

void test_create_zero_capacity(void)
{
    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_CODE(actrust_queue_create(&queue, 0u, sizeof(int))),
        ACTRUST_ERR_INVALID_ARG);
}

void test_create_zero_item_size(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_create(&queue, 4u, 0u)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_create_success(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));
    TEST_ASSERT_NOT_NULL(queue);
}

void test_destroy_null_handle(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_destroy(NULL)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_destroy_null_contents(void)
{
    actrust_queue_t q = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_destroy(&q)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_destroy_sets_null(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 2u, sizeof(int)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_destroy(&queue));
    TEST_ASSERT_NULL(queue);
}

void test_push_null_queue(void)
{
    int v = 1;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_push(NULL, &v, 0u)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_push_null_item(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_push(queue, NULL, 0u)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_pop_null_queue(void)
{
    int out;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_pop(NULL, &out, 0u)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_pop_null_item(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_pop(queue, NULL, 0u)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_pop_empty_nonblocking(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));
    int out = 0;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_pop(queue, &out, 0u)),
                      ACTRUST_ERR_NO_RESOURCE);
}

void test_push_full_nonblocking(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 1u, sizeof(int)));
    int v = 42;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(queue, &v, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_push(queue, &v, 0u)),
                      ACTRUST_ERR_QUEUE_FULL);
}

void test_fifo_order(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 8u, sizeof(int)));

    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(queue, &i, 0u));
    }

    for (int i = 0; i < 5; ++i) {
        int out = -1;
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_pop(queue, &out, 0u));
        TEST_ASSERT_EQUAL_INT(i, out);
    }
}

void test_wraparound(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 3u, sizeof(int)));

    for (int round = 0; round < 4; ++round) {
        for (int i = 0; i < 3; ++i) {
            int v = round * 10 + i;
            TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(queue, &v, 0u));
        }
        for (int i = 0; i < 3; ++i) {
            int out = -1;
            TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_pop(queue, &out, 0u));
            TEST_ASSERT_EQUAL_INT(round * 10 + i, out);
        }
    }
}

void test_size_tracks_push_pop(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&queue, 4u, sizeof(int)));

    size_t sz = 99u;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_size(queue, &sz));
    TEST_ASSERT_EQUAL(0u, sz);

    int v = 1;
    actrust_queue_push(queue, &v, 0u);
    actrust_queue_push(queue, &v, 0u);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_size(queue, &sz));
    TEST_ASSERT_EQUAL(2u, sz);

    actrust_queue_pop(queue, &v, 0u);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_size(queue, &sz));
    TEST_ASSERT_EQUAL(1u, sz);
}

void test_size_null_args(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CODE(actrust_queue_size(NULL, NULL)),
                      ACTRUST_ERR_INVALID_ARG);
}

void test_large_item_struct(void)
{
    typedef struct {
        uint32_t id;
        char     name[32];
        float    value;
    } big_item_t;

    actrust_queue_t q = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_queue_create(&q, 4u, sizeof(big_item_t)));

    big_item_t in = { .id = 0xDEADBEEF, .name = "hello", .value = 3.14f };
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_push(q, &in, 0u));

    big_item_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_pop(q, &out, 0u));
    TEST_ASSERT_EQUAL_UINT32(in.id, out.id);
    TEST_ASSERT_EQUAL_STRING(in.name, out.name);
    TEST_ASSERT_EQUAL_FLOAT(in.value, out.value);

    actrust_queue_destroy(&q);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_create_null_handle);
    RUN_TEST(test_create_zero_capacity);
    RUN_TEST(test_create_zero_item_size);
    RUN_TEST(test_create_success);
    RUN_TEST(test_destroy_null_handle);
    RUN_TEST(test_destroy_null_contents);
    RUN_TEST(test_destroy_sets_null);
    RUN_TEST(test_push_null_queue);
    RUN_TEST(test_push_null_item);
    RUN_TEST(test_pop_null_queue);
    RUN_TEST(test_pop_null_item);
    RUN_TEST(test_pop_empty_nonblocking);
    RUN_TEST(test_push_full_nonblocking);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_wraparound);
    RUN_TEST(test_size_tracks_push_pop);
    RUN_TEST(test_size_null_args);
    RUN_TEST(test_large_item_struct);
    return UNITY_END();
}
