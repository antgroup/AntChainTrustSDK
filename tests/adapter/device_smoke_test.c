// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <string.h>

/* Third-party */
#include "unity.h"

/* Adapter */
#include "adapter/device.h"

void setUp(void)
{
}
void tearDown(void)
{
}

void test_hw_id(void)
{
    char buf[128] = { 0 };
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_get_hw_id(buf, sizeof(buf)));
    TEST_ASSERT_GREATER_THAN(0u, strlen(buf));
}

void test_hw_model(void)
{
    char buf[64] = { 0 };
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_get_hw_model(buf, sizeof(buf)));
    TEST_ASSERT_GREATER_THAN(0u, strlen(buf));
}

void test_fw_version(void)
{
    char buf[128] = { 0 };
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_get_fw_version(buf, sizeof(buf)));
    TEST_ASSERT_GREATER_THAN(0u, strlen(buf));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hw_id);
    RUN_TEST(test_hw_model);
    RUN_TEST(test_fw_version);
    return UNITY_END();
}
