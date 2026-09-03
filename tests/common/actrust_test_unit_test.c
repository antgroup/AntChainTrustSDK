// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdio.h>

/* Third-party */
#include "unity.h"

/* Test */
#include "actrust_test.h"

#define TEST_FILE "actrust_test_file"

static void write_file(const char *data, size_t len)
{
    FILE *file = fopen(TEST_FILE, "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL(len, fwrite(data, 1u, len, file));
    TEST_ASSERT_EQUAL(0, fclose(file));
}

void setUp(void)
{
    (void) remove(TEST_FILE);
}

void tearDown(void)
{
    (void) remove(TEST_FILE);
}

void test_load_text_exact_capacity(void)
{
    const char data[]            = "test";
    char       buf[sizeof(data)] = { 0 };
    size_t     len               = 0u;

    write_file(data, sizeof(data) - 1u);
    TEST_ASSERT_EQUAL(
        0, actrust_test_load_file(TEST_FILE, buf, sizeof(buf), &len, true));
    TEST_ASSERT_EQUAL(sizeof(data) - 1u, len);
    TEST_ASSERT_EQUAL_STRING(data, buf);
}

void test_load_text_rejects_truncation(void)
{
    const char data[]                 = "test";
    char       buf[sizeof(data) - 1u] = { 0 };
    size_t     len                    = 99u;

    write_file(data, sizeof(data) - 1u);
    TEST_ASSERT_NOT_EQUAL(
        0, actrust_test_load_file(TEST_FILE, buf, sizeof(buf), &len, true));
    TEST_ASSERT_EQUAL(0u, len);
    TEST_ASSERT_EACH_EQUAL_CHAR(0, buf, sizeof(buf));
}

void test_load_binary_rejects_truncation(void)
{
    const unsigned char data[] = { 0x01u, 0x02u, 0x03u };
    unsigned char       buf[2] = { 0 };
    size_t              len    = 99u;

    write_file((const char *) data, sizeof(data));
    TEST_ASSERT_NOT_EQUAL(
        0, actrust_test_load_file(TEST_FILE, buf, sizeof(buf), &len, false));
    TEST_ASSERT_EQUAL(0u, len);
    TEST_ASSERT_EACH_EQUAL_CHAR(0, buf, sizeof(buf));
}

void test_load_binary_exact_capacity(void)
{
    const unsigned char data[]            = { 0x01u, 0x02u, 0x03u };
    unsigned char       buf[sizeof(data)] = { 0 };
    size_t              len               = 0u;

    write_file((const char *) data, sizeof(data));
    TEST_ASSERT_EQUAL(
        0, actrust_test_load_file(TEST_FILE, buf, sizeof(buf), &len, false));
    TEST_ASSERT_EQUAL(sizeof(data), len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, buf, sizeof(data));
}

void test_load_missing_file(void)
{
    char   buf[4] = { 0 };
    size_t len    = 99u;

    TEST_ASSERT_NOT_EQUAL(
        0, actrust_test_load_file("missing", buf, sizeof(buf), &len, false));
    TEST_ASSERT_EQUAL(0u, len);
    TEST_ASSERT_EACH_EQUAL_CHAR(0, buf, sizeof(buf));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_load_text_exact_capacity);
    RUN_TEST(test_load_text_rejects_truncation);
    RUN_TEST(test_load_binary_rejects_truncation);
    RUN_TEST(test_load_binary_exact_capacity);
    RUN_TEST(test_load_missing_file);
    return UNITY_END();
}
