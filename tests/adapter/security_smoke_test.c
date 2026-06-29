// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Adapter */
#include "adapter/security.h"

void setUp(void)
{
}
void tearDown(void)
{
}

void test_secure_storage(void)
{
    uint8_t            data[] = "hello_sec";
    actrust_sec_slot_t slot   = ACTRUST_SEC_SLOT_DATA(0);

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sec_store_write(slot, data, sizeof(data)));

    uint8_t buf[32] = { 0 };
    size_t  len     = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sec_store_read(slot, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL(sizeof(data), len);
    TEST_ASSERT_EQUAL_MEMORY(data, buf, len);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sec_store_delete(slot));
}

void test_random(void)
{
    uint8_t       buf[8] = { 0 };
    actrust_err_t err    = actrust_sec_random(buf, sizeof(buf));
    if (ACTRUST_ERR_CODE(err) == ACTRUST_ERR_UNSUPPORTED) {
        TEST_IGNORE_MESSAGE("actrust_sec_random unsupported on this platform");
    }
    TEST_ASSERT_EQUAL(ACTRUST_OK, err);

    int all_zero = 1;
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    TEST_ASSERT_FALSE(all_zero);
}

void test_key_generate(void)
{
    actrust_sec_slot_t slot = ACTRUST_SEC_SLOT_KEY(0);
    actrust_err_t err = actrust_sec_key_generate(slot, ACTRUST_SEC_KEY_EC_P256);
    if (ACTRUST_ERR_CODE(err) == ACTRUST_ERR_UNSUPPORTED) {
        TEST_IGNORE_MESSAGE(
            "actrust_sec_key_generate unsupported on this platform");
    }
    TEST_ASSERT_EQUAL(ACTRUST_OK, err);
}

void test_key_get_public(void)
{
    actrust_sec_slot_t slot     = ACTRUST_SEC_SLOT_KEY(0);
    uint8_t            buf[128] = { 0 };
    size_t             len      = 0;
    actrust_err_t      err =
        actrust_sec_key_get_public(slot, buf, sizeof(buf), &len);
    if (ACTRUST_ERR_CODE(err) == ACTRUST_ERR_UNSUPPORTED) {
        TEST_IGNORE_MESSAGE(
            "actrust_sec_key_get_public unsupported on this platform");
    }
    TEST_ASSERT_EQUAL(ACTRUST_OK, err);
    TEST_ASSERT_GREATER_THAN(0u, len);
}

void test_key_delete(void)
{
    actrust_sec_slot_t slot = ACTRUST_SEC_SLOT_KEY(0);
    actrust_err_t      err  = actrust_sec_key_delete(slot);
    if (ACTRUST_ERR_CODE(err) == ACTRUST_ERR_UNSUPPORTED) {
        TEST_IGNORE_MESSAGE(
            "actrust_sec_key_delete unsupported on this platform");
    }
    TEST_ASSERT_EQUAL(ACTRUST_OK, err);
}

void test_sign_verify(void)
{
    actrust_sec_slot_t slot       = ACTRUST_SEC_SLOT_KEY(0);
    uint8_t            digest[32] = { 0x01, 0x02, 0x03 };
    uint8_t            sig[128]   = { 0 };
    size_t             sig_len    = 0;

    actrust_err_t err = actrust_sec_ecdsa_sign(slot, digest, sizeof(digest),
                                               sig, sizeof(sig), &sig_len);
    if (ACTRUST_ERR_CODE(err) == ACTRUST_ERR_UNSUPPORTED) {
        TEST_IGNORE_MESSAGE(
            "actrust_sec_ecdsa_sign unsupported on this platform");
    }
    TEST_ASSERT_EQUAL(ACTRUST_OK, err);
    TEST_ASSERT_GREATER_THAN(0u, sig_len);

    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_sec_ecdsa_verify(slot, digest, sizeof(digest), sig, sig_len));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_secure_storage);
    RUN_TEST(test_random);
    RUN_TEST(test_key_generate);
    RUN_TEST(test_key_get_public);
    RUN_TEST(test_key_delete);
    RUN_TEST(test_sign_verify);
    return UNITY_END();
}
