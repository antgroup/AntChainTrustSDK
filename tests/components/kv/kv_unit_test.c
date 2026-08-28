// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Common */
#include "common/common.h"

/* KV */
#include "kv/kv.h"

/* Adapter */
#include "adapter/storage.h"

#define ACTRUST_TEST_KV_NAMESPACE     "testns"
#define ACTRUST_TEST_KV_NAMESPACE_LEN 6u
#define ACTRUST_TEST_KV_STORAGE_ID    0x00000001u
#define ACTRUST_TEST_KV_RECORD_IN_USE 0x01u

static actrust_kv_t kv;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t record_count;
} test_kv_header_t;

typedef struct {
    uint8_t  used;
    uint8_t  key_len;
    uint16_t reserved;
    uint32_t crc32;
    uint32_t value_len;
    uint8_t  key[CONFIG_ACTRUST_KV_MAX_KEY_LEN];
} test_kv_record_head_t;

typedef struct {
    test_kv_record_head_t head;
    uint8_t               value[CONFIG_ACTRUST_KV_MAX_VALUE_LEN];
} test_kv_record_t;

void setUp(void)
{
    kv = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_init());
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_open(ACTRUST_TEST_KV_NAMESPACE,
                                      ACTRUST_TEST_KV_NAMESPACE_LEN, &kv));
}

void tearDown(void)
{
    if (kv != NULL) {
        actrust_kv_del(kv, "k", 1);
        actrust_kv_del(kv, "key1", 4);
        actrust_kv_del(kv, "key2", 4);
        actrust_kv_del(kv, "big", 3);
        actrust_kv_del(kv, "over", 4);
        actrust_kv_close(kv);
        kv = NULL;
    }
}

void test_open_null_args(void)
{
    actrust_kv_t h = NULL;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_kv_open(NULL, 0, &h));
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_kv_open("x", 1, NULL));
}

void test_open_zero_len(void)
{
    actrust_kv_t h = NULL;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_kv_open(ACTRUST_TEST_KV_NAMESPACE, 0, &h));
}

void test_open_rejects_unformatted_storage(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_close(kv));
    kv = NULL;

    actrust_storage_t st = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_open(&st, ACTRUST_TEST_KV_STORAGE_ID));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_erase(st, 0, 64));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_close(st));

    actrust_kv_t  h   = NULL;
    actrust_err_t err = actrust_kv_open(ACTRUST_TEST_KV_NAMESPACE,
                                        ACTRUST_TEST_KV_NAMESPACE_LEN, &h);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_NULL(h);
}

void test_set_get_roundtrip(void)
{
    const char *val = "hello";
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_set(kv, "k", 1, val, 5));

    char   buf[32];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_get(kv, "k", 1, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL(5u, len);
    TEST_ASSERT_EQUAL_MEMORY(val, buf, 5);
}

void test_set_overwrite(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_set(kv, "k", 1, "aaa", 3));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_set(kv, "k", 1, "bbbbb", 5));

    char   buf[32];
    size_t len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_get(kv, "k", 1, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL(5u, len);
    TEST_ASSERT_EQUAL_MEMORY("bbbbb", buf, 5);
}

void test_get_nonexistent(void)
{
    char          buf[16];
    size_t        len = 0;
    actrust_err_t err = actrust_kv_get(kv, "nope", 4, buf, sizeof(buf), &len);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_NO_RESOURCE, ACTRUST_ERR_CODE(err));
}

void test_get_buffer_too_small(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_set(kv, "k", 1, "longvalue", 9));

    char          buf[4];
    size_t        len = 0;
    actrust_err_t err = actrust_kv_get(kv, "k", 1, buf, sizeof(buf), &len);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_EQUAL(9u, len);
}

void test_get_rejects_corrupt_record_value_len(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_set(kv, "k", 1, "safe", 4));

    actrust_storage_t st = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_open(&st, ACTRUST_TEST_KV_STORAGE_ID));

    bool     found      = false;
    uint32_t len_offset = 0u;
    for (size_t i = 0; i < CONFIG_ACTRUST_KV_MAX_RECORDS; ++i) {
        uint32_t         rec_offset = (uint32_t) (sizeof(test_kv_header_t) +
                                          sizeof(test_kv_record_t) * i);
        test_kv_record_t rec;

        TEST_ASSERT_EQUAL(ACTRUST_OK,
                          actrust_storage_read(st, rec_offset, (uint8_t *) &rec,
                                               sizeof(rec)));
        if (rec.head.used == ACTRUST_TEST_KV_RECORD_IN_USE &&
            rec.head.key_len == 1u && rec.head.key[0] == (uint8_t) 'k') {
            uint32_t bad_len = (uint32_t) CONFIG_ACTRUST_KV_MAX_VALUE_LEN + 1u;
            len_offset       = rec_offset +
                         (uint32_t) offsetof(test_kv_record_t, head) +
                         (uint32_t) offsetof(test_kv_record_head_t, value_len);

            TEST_ASSERT_EQUAL(ACTRUST_OK,
                              actrust_storage_write(st, len_offset,
                                                    (const uint8_t *) &bad_len,
                                                    sizeof(bad_len)));
            found = true;
            break;
        }
    }

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_close(st));
    TEST_ASSERT_TRUE(found);

    char          buf[16];
    size_t        len = 0;
    actrust_err_t err = actrust_kv_get(kv, "k", 1, buf, sizeof(buf), &len);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO, ACTRUST_ERR_CODE(err));

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_open(&st, ACTRUST_TEST_KV_STORAGE_ID));
    uint32_t original_len = 4u;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_write(st, len_offset,
                                            (const uint8_t *) &original_len,
                                            sizeof(original_len)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_close(st));
}

static test_kv_record_t s_original_record;
static uint32_t         s_original_record_offset;

static void corrupt_record_value(void)
{
    actrust_storage_t st = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_open(&st, ACTRUST_TEST_KV_STORAGE_ID));

    test_kv_record_t rec;
    uint32_t         rec_offset = (uint32_t) sizeof(test_kv_header_t);
    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_storage_read(st, rec_offset, (uint8_t *) &rec, sizeof(rec)));
    TEST_ASSERT_EQUAL(ACTRUST_TEST_KV_RECORD_IN_USE, rec.head.used);
    s_original_record        = rec;
    s_original_record_offset = rec_offset;
    rec.value[0] ^= 0xFFu;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_write(st, rec_offset,
                                                        (const uint8_t *) &rec,
                                                        sizeof(rec)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_close(st));
}

static void restore_original_record(void)
{
    actrust_storage_t st = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_open(&st, ACTRUST_TEST_KV_STORAGE_ID));
    TEST_ASSERT_EQUAL(
        ACTRUST_OK, actrust_storage_write(st, s_original_record_offset,
                                          (const uint8_t *) &s_original_record,
                                          sizeof(s_original_record)));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_close(st));
}

void test_corrupt_record_is_rejected_by_search_operations(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_set(kv, "k", 1, "safe", 4));
    corrupt_record_value();

    bool          exists = true;
    actrust_err_t err    = actrust_kv_exists(kv, "k", 1, &exists);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO, ACTRUST_ERR_CODE(err));

    err = actrust_kv_set(kv, "k", 1, "new", 3);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO, ACTRUST_ERR_CODE(err));

    err = actrust_kv_del(kv, "k", 1);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO, ACTRUST_ERR_CODE(err));
    restore_original_record();
}

void test_del_and_exists(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_set(kv, "k", 1, "v", 1));

    bool exists = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_exists(kv, "k", 1, &exists));
    TEST_ASSERT_TRUE(exists);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_del(kv, "k", 1));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_exists(kv, "k", 1, &exists));
    TEST_ASSERT_FALSE(exists);
}

void test_del_nonexistent(void)
{
    actrust_err_t err = actrust_kv_del(kv, "nope", 4);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_NO_RESOURCE, ACTRUST_ERR_CODE(err));
}

void test_exists_null_args(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_kv_exists(kv, NULL, 0, NULL));
}

void test_set_null_key(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_kv_set(kv, NULL, 0, "v", 1));
}

void test_set_key_too_long(void)
{
    char big_key[CONFIG_ACTRUST_KV_MAX_KEY_LEN + 2];
    memset(big_key, 'K', sizeof(big_key));

    actrust_err_t err = actrust_kv_set(kv, big_key, sizeof(big_key), "v", 1);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
}

void test_set_value_too_long(void)
{
    size_t bad_len = CONFIG_ACTRUST_KV_MAX_VALUE_LEN + 1;
    char  *big_val = ACTRUST_CALLOC(1, bad_len);
    TEST_ASSERT_NOT_NULL(big_val);
    memset(big_val, 'V', bad_len);

    actrust_err_t err = actrust_kv_set(kv, "over", 4, big_val, bad_len);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
    ACTRUST_FREE(big_val);
}

void test_multiple_keys_independent(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_set(kv, "key1", 4, "AAA", 3));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_set(kv, "key2", 4, "BBB", 3));

    char   buf[16];
    size_t len = 0;

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_get(kv, "key1", 4, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL(3u, len);
    TEST_ASSERT_EQUAL_MEMORY("AAA", buf, 3);

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_get(kv, "key2", 4, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL(3u, len);
    TEST_ASSERT_EQUAL_MEMORY("BBB", buf, 3);
}

void test_set_zero_length_value(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_set(kv, "k", 1, NULL, 0));

    char   buf[16];
    size_t len = 99;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_get(kv, "k", 1, buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL(0u, len);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_null_args);
    RUN_TEST(test_open_zero_len);
    RUN_TEST(test_open_rejects_unformatted_storage);
    RUN_TEST(test_set_get_roundtrip);
    RUN_TEST(test_set_overwrite);
    RUN_TEST(test_get_nonexistent);
    RUN_TEST(test_get_buffer_too_small);
    RUN_TEST(test_get_rejects_corrupt_record_value_len);
    RUN_TEST(test_corrupt_record_is_rejected_by_search_operations);
    RUN_TEST(test_del_and_exists);
    RUN_TEST(test_del_nonexistent);
    RUN_TEST(test_exists_null_args);
    RUN_TEST(test_set_null_key);
    RUN_TEST(test_set_key_too_long);
    RUN_TEST(test_set_value_too_long);
    RUN_TEST(test_multiple_keys_independent);
    RUN_TEST(test_set_zero_length_value);
    return UNITY_END();
}
