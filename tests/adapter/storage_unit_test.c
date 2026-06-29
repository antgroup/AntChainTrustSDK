// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

/* C standard */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Third-party */
#include "unity.h"

/* Adapter */
#include "adapter/storage.h"

static actrust_storage_t st;
static char              test_home[256];

static void storage_test_remove_path(const char *suffix)
{
    char path[320];
    int  n = snprintf(path, sizeof(path), "%s/%s", test_home, suffix);
    if (n > 0 && (size_t) n < sizeof(path)) {
        (void) unlink(path);
        (void) rmdir(path);
    }
}

static void storage_test_cleanup_home(void)
{
    storage_test_remove_path(".actrust/storage/actrust_kv_aaaaaaaa.bin");
    storage_test_remove_path(".actrust/storage/actrust_kv_aaaaaaab.bin");
    storage_test_remove_path(".actrust/storage/actrust_kv_aaaaaaac.bin");
    storage_test_remove_path(".actrust/storage/actrust_kv_aaaaaaad.bin");
    storage_test_remove_path("target_storage");
    storage_test_remove_path(".actrust/storage");
    storage_test_remove_path(".actrust");
}

static void storage_test_prepare_home(void)
{
    int n = snprintf(test_home, sizeof(test_home), ".");
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(test_home));
}

void setUp(void)
{
    storage_test_prepare_home();
    st = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_open(&st, 0xAAAAAAAAu));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_erase(st, 0, 256));
}

void tearDown(void)
{
    if (st != NULL) {
        actrust_storage_erase(st, 0, 256);
        actrust_storage_close(st);
        st = NULL;
    }
    storage_test_cleanup_home();
    (void) rmdir(test_home);
}

void test_open_null_handle(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_storage_open(NULL, 1));
}

void test_close_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_storage_close(NULL));
}

void test_open_handles_fd_zero(void)
{
    int saved_stdin = dup(STDIN_FILENO);
    TEST_ASSERT_TRUE(saved_stdin >= 0);

    int               close_rc  = close(STDIN_FILENO);
    actrust_storage_t fd0_st    = NULL;
    actrust_err_t     open_err  = ACTRUST_OK;
    actrust_err_t     close_err = ACTRUST_OK;

    if (close_rc == 0) {
        open_err = actrust_storage_open(&fd0_st, 0xAAAAAAABu);
        if (fd0_st != NULL) {
            close_err = actrust_storage_close(fd0_st);
        }
    }

    int restore_rc = dup2(saved_stdin, STDIN_FILENO);
    (void) close(saved_stdin);

    TEST_ASSERT_EQUAL_INT(0, close_rc);
    TEST_ASSERT_EQUAL_INT(STDIN_FILENO, restore_rc);
    TEST_ASSERT_EQUAL(ACTRUST_OK, open_err);
    TEST_ASSERT_NOT_NULL(fd0_st);
    TEST_ASSERT_EQUAL(ACTRUST_OK, close_err);
}

void test_open_rejects_file_in_parent_path(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_close(st));
    st = NULL;
    storage_test_cleanup_home();

    char path[320];
    int  n = snprintf(path, sizeof(path), "%s/.actrust", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(path));

    FILE *fp = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_INT(0, fclose(fp));

    actrust_storage_t bad_st = NULL;
    actrust_err_t     err    = actrust_storage_open(&bad_st, 0xAAAAAAACu);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_NULL(bad_st);
}

void test_open_rejects_region_symlink(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_close(st));
    st = NULL;
    storage_test_cleanup_home();

    char actrust_dir[320];
    char storage_dir[320];
    char target[320];
    char link_path[320];

    int n =
        snprintf(actrust_dir, sizeof(actrust_dir), "%s/.actrust", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(actrust_dir));
    n = snprintf(storage_dir, sizeof(storage_dir), "%s/.actrust/storage",
                 test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(storage_dir));
    n = snprintf(target, sizeof(target), "%s/target_storage", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(target));
    n = snprintf(link_path, sizeof(link_path),
                 "%s/.actrust/storage/actrust_kv_aaaaaaad.bin", test_home);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(link_path));

    TEST_ASSERT_EQUAL_INT(0, mkdir(actrust_dir, 0700));
    TEST_ASSERT_EQUAL_INT(0, mkdir(storage_dir, 0700));

    FILE *fp = fopen(target, "w");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_INT(0, fclose(fp));
    TEST_ASSERT_EQUAL_INT(0, symlink(target, link_path));

    actrust_storage_t bad_st = NULL;
    actrust_err_t     err    = actrust_storage_open(&bad_st, 0xAAAAAAADu);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_NULL(bad_st);
}

void test_write_read_roundtrip(void)
{
    const uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_write(st, 0, data, sizeof(data)));

    uint8_t buf[4];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_read(st, 0, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_MEMORY(data, buf, sizeof(data));
}

void test_write_at_offset(void)
{
    const uint8_t data[] = { 0x11, 0x22 };
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_write(st, 100, data, sizeof(data)));

    uint8_t buf[2];
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_read(st, 100, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_MEMORY(data, buf, sizeof(data));
}

void test_erase_zeroes(void)
{
    const uint8_t data[] = { 0xFF, 0xFF, 0xFF, 0xFF };
    actrust_storage_write(st, 0, data, sizeof(data));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_storage_erase(st, 0, 4));

    uint8_t buf[4];
    actrust_storage_read(st, 0, buf, sizeof(buf));
    const uint8_t zeroes[4] = { 0 };
    TEST_ASSERT_EQUAL_MEMORY(zeroes, buf, 4);
}

void test_read_null_buf(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_storage_read(st, 0, NULL, 4));
}

void test_write_null_buf(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_storage_write(st, 0, NULL, 4));
}

void test_large_write_read(void)
{
    uint8_t big[128];
    for (size_t i = 0; i < sizeof(big); i++) {
        big[i] = (uint8_t) (i & 0xFF);
    }
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_write(st, 0, big, sizeof(big)));

    uint8_t out[128];
    memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_storage_read(st, 0, out, sizeof(out)));
    TEST_ASSERT_EQUAL_MEMORY(big, out, sizeof(big));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_null_handle);
    RUN_TEST(test_close_null);
    RUN_TEST(test_open_handles_fd_zero);
    RUN_TEST(test_open_rejects_file_in_parent_path);
    RUN_TEST(test_open_rejects_region_symlink);
    RUN_TEST(test_write_read_roundtrip);
    RUN_TEST(test_write_at_offset);
    RUN_TEST(test_erase_zeroes);
    RUN_TEST(test_read_null_buf);
    RUN_TEST(test_write_null_buf);
    RUN_TEST(test_large_write_read);
    return UNITY_END();
}
