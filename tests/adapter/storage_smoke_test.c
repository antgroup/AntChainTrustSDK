// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdint.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Adapter */
#include "adapter/storage.h"

void setUp(void)
{
}
void tearDown(void)
{
}

void test_storage_readwrite(void)
{
    actrust_storage_t st = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_open(&st, 0));

    uint8_t wbuf[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_write(st, 0, wbuf, sizeof(wbuf)));

    uint8_t rbuf[4] = { 0 };
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_read(st, 0, rbuf, sizeof(rbuf)));
    TEST_ASSERT_EQUAL_MEMORY(wbuf, rbuf, sizeof(wbuf));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_erase(st, 0, sizeof(wbuf)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_close(st));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_storage_readwrite);
    return UNITY_END();
}
