// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdint.h>

/* Third-party */
#include "unity.h"

/* Project */
#include "actrust_config.h"

/* NTP */
#include "ntp/ntp.h"

/* Adapter */
#include "adapter/system.h"

#define TEST_NTP_SYNC_WAIT_MS                                                  \
    ((uint32_t) CONFIG_ACTRUST_NTP_TIMEOUT_MS * 2u + 1000u)

typedef struct {
    actrust_ntp_t ntp;
    actrust_err_t sync_err;
} ntp_sync_ctx_t;

static void ntp_sync_task(void *arg)
{
    ntp_sync_ctx_t *ctx = (ntp_sync_ctx_t *) arg;
    if (ctx != NULL) {
        ctx->sync_err = actrust_ntp_sync(ctx->ntp);
    }
}

void setUp(void)
{
}
void tearDown(void)
{
}

void test_ntp_sync(void)
{
    actrust_ntp_t ntp = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_ntp_init(&ntp));
    TEST_ASSERT_NOT_NULL(ntp);

    ntp_sync_ctx_t ctx = {
        .ntp      = ntp,
        .sync_err = ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_NTP,
                                ACTRUST_ERR_NOT_READY),
    };
    actrust_task_t task = NULL;
    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_task_create(&task, "ntp_sync", ntp_sync_task, &ctx, 0, 0));

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_join(task, TEST_NTP_SYNC_WAIT_MS));
    TEST_ASSERT_EQUAL(ACTRUST_OK, ctx.sync_err);

    int64_t offset = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_ntp_get_last_offset_ms(ntp, &offset));

    uint64_t now = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_ntp_now_ms(ntp, &now));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_ntp_deinit(ntp));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ntp_sync);
    return UNITY_END();
}
