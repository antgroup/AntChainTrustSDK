// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

/* C standard */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Third-party */
#include "unity.h"

/* Adapter */
#include "adapter/security.h"

#define TEST_SLOT ACTRUST_SEC_SLOT_DATA(7)

static char test_home[256];

static void security_test_remove_path(const char *suffix)
{
    char path[320];
    int  n = snprintf(path, sizeof(path), "%s/%s", test_home, suffix);
    if (n > 0 && (size_t) n < sizeof(path)) {
        (void) unlink(path);
        (void) rmdir(path);
    }
}

static void security_test_cleanup_temp_files(void)
{
    char command[384];
    int  n = snprintf(command, sizeof(command),
                      "find '%s/.actrust/security' -maxdepth 1 -type f "
                       "-name '.sec_*.tmp' -delete -o -name '.sec_*.old' "
                       "-delete 2>/dev/null",
                      test_home);
    if (n > 0 && (size_t) n < sizeof(command)) {
        (void) system(command);
    }
}

static void security_test_cleanup_home(void)
{
    security_test_remove_path(".actrust/security/sec_00002007.bin");
    security_test_remove_path(".actrust/security/sec_00002008.bin");
    security_test_remove_path(".actrust/security/sec_00002009.bin");
    security_test_cleanup_temp_files();
    security_test_remove_path("target_secret");
    security_test_remove_path(".actrust/security");
    security_test_remove_path(".actrust");
}

static void security_test_prepare_home(void)
{
    int n = snprintf(test_home, sizeof(test_home), ".");
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(test_home));
}

void setUp(void)
{
    security_test_prepare_home();
    actrust_sec_store_delete(TEST_SLOT);
}

void tearDown(void)
{
    actrust_sec_store_delete(TEST_SLOT);
    security_test_cleanup_home();
    (void) rmdir(test_home);
}

void test_store_write_read_roundtrip(void)
{
    const uint8_t data[] = { 0xCA, 0xFE, 0xBA, 0xBE };
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sec_store_write(TEST_SLOT, data, sizeof(data)));

    uint8_t buf[16];
    size_t  actual = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sec_store_read(TEST_SLOT, buf,
                                                         sizeof(buf), &actual));
    TEST_ASSERT_EQUAL(sizeof(data), actual);
    TEST_ASSERT_EQUAL_MEMORY(data, buf, sizeof(data));
}

void test_store_read_nonexistent(void)
{
    uint8_t       buf[16];
    size_t        actual = 0;
    actrust_err_t err =
        actrust_sec_store_read(TEST_SLOT, buf, sizeof(buf), &actual);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_NO_RESOURCE, ACTRUST_ERR_CODE(err));
}

void test_store_delete_nonexistent(void)
{
    actrust_err_t err = actrust_sec_store_delete(TEST_SLOT);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_NO_RESOURCE, ACTRUST_ERR_CODE(err));
}

void test_store_overwrite(void)
{
    actrust_sec_store_write(TEST_SLOT, (const uint8_t *) "aaa", 3);
    actrust_sec_store_write(TEST_SLOT, (const uint8_t *) "bbbbb", 5);

    uint8_t buf[16];
    size_t  actual = 0;
    actrust_sec_store_read(TEST_SLOT, buf, sizeof(buf), &actual);
    TEST_ASSERT_EQUAL(5u, actual);
    TEST_ASSERT_EQUAL_MEMORY("bbbbb", buf, 5);
}

void test_store_read_buffer_too_small(void)
{
    actrust_sec_store_write(TEST_SLOT, (const uint8_t *) "longdata", 8);

    uint8_t       buf[2];
    size_t        actual = 0;
    actrust_err_t err =
        actrust_sec_store_read(TEST_SLOT, buf, sizeof(buf), &actual);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_EQUAL(8u, actual);
}

void test_store_delete_then_read(void)
{
    actrust_sec_store_write(TEST_SLOT, (const uint8_t *) "x", 1);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sec_store_delete(TEST_SLOT));

    uint8_t       buf[8];
    size_t        actual = 0;
    actrust_err_t err =
        actrust_sec_store_read(TEST_SLOT, buf, sizeof(buf), &actual);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_NO_RESOURCE, ACTRUST_ERR_CODE(err));
}

void test_store_write_rejects_file_in_parent_path(void)
{
    security_test_cleanup_home();

    char path[320];
    int  n = snprintf(path, sizeof(path), "%s/.actrust", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(path));

    FILE *fp = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_INT(0, fclose(fp));

    const uint8_t data[] = { 0x01u };
    actrust_err_t err =
        actrust_sec_store_write(ACTRUST_SEC_SLOT_DATA(8), data, sizeof(data));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO, ACTRUST_ERR_CODE(err));
}

void test_store_write_rejects_slot_symlink(void)
{
    security_test_cleanup_home();

    char actrust_dir[320];
    char sec_dir[320];
    char target[320];
    char link_path[320];

    int n =
        snprintf(actrust_dir, sizeof(actrust_dir), "%s/.actrust", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(actrust_dir));
    n = snprintf(sec_dir, sizeof(sec_dir), "%s/.actrust/security", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(sec_dir));
    n = snprintf(target, sizeof(target), "%s/target_secret", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(target));
    n = snprintf(link_path, sizeof(link_path),
                 "%s/.actrust/security/sec_00002009.bin", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(link_path));

    TEST_ASSERT_EQUAL_INT(0, mkdir(actrust_dir, 0700));
    TEST_ASSERT_EQUAL_INT(0, mkdir(sec_dir, 0700));

    FILE *fp = fopen(target, "w");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_INT(0, fclose(fp));
    TEST_ASSERT_EQUAL_INT(0, symlink(target, link_path));

    const uint8_t data[] = { 0x02u };
    actrust_err_t err =
        actrust_sec_store_write(ACTRUST_SEC_SLOT_DATA(9), data, sizeof(data));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO, ACTRUST_ERR_CODE(err));
}

void test_store_failed_update_preserves_previous_value(void)
{
    const uint8_t old_data[] = { 0x11u, 0x22u };
    const uint8_t new_data[] = { 0x33u, 0x44u, 0x55u };
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sec_store_write(TEST_SLOT, old_data,
                                                          sizeof(old_data)));

    char path[320];
    int  n = snprintf(path, sizeof(path),
                      "%s/.actrust/security/sec_00002007.bin", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(path));
    TEST_ASSERT_EQUAL_INT(0, chmod(path, 0400));

    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO,
                      ACTRUST_ERR_CODE(actrust_sec_store_write(
                          TEST_SLOT, new_data, sizeof(new_data))));

    TEST_ASSERT_EQUAL_INT(0, chmod(path, 0600));
    uint8_t buf[8] = { 0 };
    size_t  actual = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sec_store_read(TEST_SLOT, buf,
                                                         sizeof(buf), &actual));
    TEST_ASSERT_EQUAL(sizeof(old_data), actual);
    TEST_ASSERT_EQUAL_MEMORY(old_data, buf, sizeof(old_data));
}

void test_store_rejects_nonregular_slot(void)
{
    security_test_cleanup_home();
    const uint8_t data[] = { 0x01u };
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sec_store_write(TEST_SLOT, data, sizeof(data)));

    char path[320];
    int  n = snprintf(path, sizeof(path),
                      "%s/.actrust/security/sec_00002007.bin", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(path));
    TEST_ASSERT_EQUAL_INT(0, unlink(path));
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));

    uint8_t buf[8];
    size_t  actual = 0;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO,
                      ACTRUST_ERR_CODE(actrust_sec_store_read(
                          TEST_SLOT, buf, sizeof(buf), &actual)));
}

void test_store_delete_rejects_nonregular_slot(void)
{
    security_test_cleanup_home();
    const uint8_t data[] = { 0x01u };
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sec_store_write(TEST_SLOT, data, sizeof(data)));

    char path[320];
    int  n = snprintf(path, sizeof(path),
                      "%s/.actrust/security/sec_00002007.bin", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(path));
    TEST_ASSERT_EQUAL_INT(0, unlink(path));
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0700));

    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO,
                      ACTRUST_ERR_CODE(actrust_sec_store_delete(TEST_SLOT)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_store_write_read_roundtrip);
    RUN_TEST(test_store_read_nonexistent);
    RUN_TEST(test_store_delete_nonexistent);
    RUN_TEST(test_store_overwrite);
    RUN_TEST(test_store_read_buffer_too_small);
    RUN_TEST(test_store_delete_then_read);
    RUN_TEST(test_store_write_rejects_file_in_parent_path);
    RUN_TEST(test_store_write_rejects_slot_symlink);
    RUN_TEST(test_store_failed_update_preserves_previous_value);
    RUN_TEST(test_store_rejects_nonregular_slot);
    RUN_TEST(test_store_delete_rejects_nonregular_slot);
    return UNITY_END();
}
