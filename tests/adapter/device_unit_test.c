// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#if defined(ACTRUST_LINUX_DEVICE_TEST_FIXTURE)
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#endif
#include <string.h>

/* Third-party */
#include "unity.h"

/* Adapter */
#include "adapter/device.h"

#if defined(ACTRUST_LINUX_DEVICE_TEST_FIXTURE)
static void make_dir(const char *path)
{
    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        TEST_FAIL_MESSAGE("mkdir failed");
    }
}

static void write_file(const char *path, const void *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_UINT(len, fwrite(data, 1u, len, fp));
    TEST_ASSERT_EQUAL(0, fclose(fp));
}

static void prepare_linux_device_fixture(void)
{
    static const char machine_id[] = "0123456789abcdef0123456789abcdef\n";
    static const char dmi_model[]  = "Linux DMI Board\n";
    static const char dt_model[]   = "ARM Device Tree Board";
    static const char os_release[] = "NAME=Actrust\n"
                                     "PRETTY_NAME=\"Actrust Test OS\"\n";

    make_dir("fixtures");

    write_file(ACTRUST_LINUX_HW_ID_PATH, machine_id, strlen(machine_id));
    write_file(ACTRUST_LINUX_DMI_BOARD_NAME_PATH, dmi_model, strlen(dmi_model));
    write_file(ACTRUST_LINUX_DEVICE_TREE_MODEL_PATH, dt_model,
               sizeof(dt_model));
    write_file(ACTRUST_LINUX_OS_RELEASE_PATH, os_release, strlen(os_release));
}
#endif

void setUp(void)
{
#if defined(ACTRUST_LINUX_DEVICE_TEST_FIXTURE)
    prepare_linux_device_fixture();
#endif
}

void tearDown(void)
{
}

void test_hw_id_null_buf(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_get_hw_id(NULL, 64));
}

void test_hw_id_zero_len(void)
{
    char buf[64];
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_get_hw_id(buf, 0));
}

void test_hw_id_buffer_too_small(void)
{
    char          buf[1] = { 0 };
    actrust_err_t err    = actrust_get_hw_id(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL, ACTRUST_ERR_CODE(err));
}

void test_hw_id_success(void)
{
    char buf[128];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_get_hw_id(buf, sizeof(buf)));
    TEST_ASSERT_GREATER_THAN(0u, strlen(buf));
#if defined(ACTRUST_LINUX_DEVICE_TEST_FIXTURE)
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", buf);
#endif
}

void test_hw_model_null_buf(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_get_hw_model(NULL, 64));
}

void test_hw_model_success(void)
{
    char buf[128];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_get_hw_model(buf, sizeof(buf)));
    TEST_ASSERT_GREATER_THAN(0u, strlen(buf));
#if defined(ACTRUST_LINUX_DEVICE_TEST_FIXTURE)
    TEST_ASSERT_EQUAL_STRING("Linux DMI Board", buf);
#endif
}

#if defined(ACTRUST_LINUX_DEVICE_TEST_FIXTURE)
void test_hw_model_device_tree_fallback(void)
{
    char buf[128];
    memset(buf, 0, sizeof(buf));

    TEST_ASSERT_EQUAL(0, remove(ACTRUST_LINUX_DMI_BOARD_NAME_PATH));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_get_hw_model(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("ARM Device Tree Board", buf);
}

void test_hw_model_buffer_too_small(void)
{
    char          buf[4] = { 0 };
    actrust_err_t err    = actrust_get_hw_model(buf, sizeof(buf));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL, ACTRUST_ERR_CODE(err));
}
#endif

void test_fw_version_null_buf(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_get_fw_version(NULL, 64));
}

void test_fw_version_success(void)
{
    char buf[128];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_get_fw_version(buf, sizeof(buf)));
    TEST_ASSERT_GREATER_THAN(0u, strlen(buf));
#if defined(ACTRUST_LINUX_DEVICE_TEST_FIXTURE)
    TEST_ASSERT_EQUAL_STRING("Actrust Test OS", buf);
#endif
}

void test_hw_id_consistency(void)
{
    char buf1[128], buf2[128];
    actrust_get_hw_id(buf1, sizeof(buf1));
    actrust_get_hw_id(buf2, sizeof(buf2));
    TEST_ASSERT_EQUAL_STRING(buf1, buf2);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_hw_id_null_buf);
    RUN_TEST(test_hw_id_zero_len);
    RUN_TEST(test_hw_id_buffer_too_small);
    RUN_TEST(test_hw_id_success);
    RUN_TEST(test_hw_model_null_buf);
    RUN_TEST(test_hw_model_success);
#if defined(ACTRUST_LINUX_DEVICE_TEST_FIXTURE)
    RUN_TEST(test_hw_model_device_tree_fallback);
    RUN_TEST(test_hw_model_buffer_too_small);
#endif
    RUN_TEST(test_fw_version_null_buf);
    RUN_TEST(test_fw_version_success);
    RUN_TEST(test_hw_id_consistency);
    return UNITY_END();
}
