// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stddef.h>
#include <stdint.h>

/* Third-party */
#include "unity.h"

/* TLS */
#include "tls/tls.h"

/* Adapter */
#include "adapter/network.h"
#include "adapter/system.h"

struct actrust_tls_ctx {
    actrust_net_t net;
    void         *backend_ctx;
};

void setUp(void)
{
}
void tearDown(void)
{
}

void test_connect_null_handle(void)
{
    actrust_tls_config_t cfg = { .host     = "example.com",
                                 .port     = 443,
                                 .insecure = true };
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_tls_connect(NULL, &cfg, 1000));
}

void test_connect_null_config(void)
{
    actrust_tls_t tls = NULL;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_tls_connect(&tls, NULL, 1000));
}

void test_close_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_tls_close(NULL, 1000));
}

void test_close_null_contents(void)
{
    actrust_tls_t tls = NULL;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_tls_close(&tls, 1000));
}

void test_close_releases_handle_when_network_close_fails(void)
{
    actrust_tls_t tls = actrust_calloc(1u, sizeof(*tls));
    TEST_ASSERT_NOT_NULL(tls);

    actrust_err_t err = actrust_tls_close(&tls, 1000u);

    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_NULL(tls);
}

void test_write_null_handle(void)
{
    uint8_t buf[]   = "hi";
    size_t  written = 0;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_tls_write(NULL, buf, 2, 1000, &written));
}

void test_zero_timeout_invalid_handles_return(void)
{
    uint8_t buf[1] = { 0u };
    size_t  count  = 0u;

    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_tls_write(NULL, buf, sizeof(buf), 0u, &count));
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_tls_read(NULL, buf, sizeof(buf), 0u, &count));
}

void test_read_null_handle(void)
{
    uint8_t buf[16];
    size_t  rd = 0;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_tls_read(NULL, buf, sizeof(buf), 1000, &rd));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_connect_null_handle);
    RUN_TEST(test_connect_null_config);
    RUN_TEST(test_close_null);
    RUN_TEST(test_close_null_contents);
    RUN_TEST(test_close_releases_handle_when_network_close_fails);
    RUN_TEST(test_write_null_handle);
    RUN_TEST(test_zero_timeout_invalid_handles_return);
    RUN_TEST(test_read_null_handle);
    return UNITY_END();
}
