// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* NTP */
#include "ntp/ntp.h"

/* Adapter */
#include "adapter/network.h"
#include "adapter/system.h"

#define TEST_NET_ERR(reason)                                                   \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_NETWORK, (reason))

typedef enum {
    TEST_NTP_PEER_MATCH = 0,
    TEST_NTP_PEER_SPOOF_IP,
    TEST_NTP_PEER_SPOOF_PORT,
} test_ntp_peer_mode_t;

static const char           TEST_NTP_SERVER_IP[] = "203.0.113.10";
static const char           TEST_NTP_SPOOF_IP[]  = "203.0.113.11";
static test_ntp_peer_mode_t s_peer_mode;
static uint8_t              s_request_tx[8];
static uint16_t             s_server_port;
static bool                 s_have_request;
static uint32_t             s_dns_timeout;
static uint32_t             s_send_timeout;
static uint32_t             s_recv_timeout;
static uint32_t             s_dns_calls;
static uint32_t             s_send_calls;
static uint32_t             s_recv_calls;
static uint32_t             s_monotonic_delay_after_dns_ms;
static uint32_t             s_monotonic_delay_after_send_ms;
static bool                 s_block_dns;
static actrust_sem_t        s_dns_entered;
static actrust_sem_t        s_dns_release;
static actrust_ntp_t        ntp;

static void test_fill_sntp_response(uint8_t *buf, size_t len)
{
    memset(buf, 0, len);
    buf[0] = 0x24u;
    buf[1] = 1u;
    memcpy(buf + 24u, s_request_tx, sizeof(s_request_tx));
    memcpy(buf + 32u, s_request_tx, sizeof(s_request_tx));
    memcpy(buf + 40u, s_request_tx, sizeof(s_request_tx));
}

actrust_err_t actrust_net_dns_resolve(const char *host, char *ip, size_t ip_len,
                                      uint32_t timeout_ms)
{
    (void) host;

    s_dns_timeout = timeout_ms;
    s_dns_calls++;
    if (s_block_dns) {
        (void) actrust_sem_post(s_dns_entered);
        actrust_err_t wait_err = actrust_sem_wait(s_dns_release, UINT32_MAX);
        if (wait_err != ACTRUST_OK) {
            return wait_err;
        }
    }
    if (s_monotonic_delay_after_dns_ms > 0u) {
        actrust_sleep_ms(s_monotonic_delay_after_dns_ms);
    }

    if (ip == NULL || ip_len < sizeof(TEST_NTP_SERVER_IP)) {
        return TEST_NET_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    memcpy(ip, TEST_NTP_SERVER_IP, sizeof(TEST_NTP_SERVER_IP));
    return ACTRUST_OK;
}

actrust_err_t actrust_net_open(actrust_net_t *out, actrust_net_type_t type)
{
    if (out == NULL || type != ACTRUST_NET_UDP) {
        return TEST_NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    *out = (actrust_net_t) (uintptr_t) 1u;
    return ACTRUST_OK;
}

actrust_err_t actrust_net_close(actrust_net_t net)
{
    return (net == NULL) ? TEST_NET_ERR(ACTRUST_ERR_INVALID_ARG) : ACTRUST_OK;
}

actrust_err_t actrust_net_sendto(actrust_net_t net, const char *ip,
                                 uint16_t port, const uint8_t *buf, size_t len,
                                 uint32_t timeout_ms, size_t *sent_len)
{
    (void) ip;

    s_send_timeout = timeout_ms;
    s_send_calls++;
    if (s_monotonic_delay_after_send_ms > 0u) {
        actrust_sleep_ms(s_monotonic_delay_after_send_ms);
    }

    if (net == NULL || buf == NULL || sent_len == NULL || len < 48u) {
        return TEST_NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    memcpy(s_request_tx, buf + 40u, sizeof(s_request_tx));
    s_server_port  = port;
    s_have_request = true;
    *sent_len      = len;
    return ACTRUST_OK;
}

actrust_err_t actrust_net_recvfrom(actrust_net_t net, char *ip, size_t ip_len,
                                   uint16_t *port, uint8_t *buf, size_t len,
                                   uint32_t timeout_ms, size_t *recvd_len)
{
    s_recv_timeout = timeout_ms;
    s_recv_calls++;

    if (net == NULL || buf == NULL || recvd_len == NULL || len < 48u ||
        !s_have_request) {
        return TEST_NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const char *peer_ip = (s_peer_mode == TEST_NTP_PEER_SPOOF_IP)
                              ? TEST_NTP_SPOOF_IP
                              : TEST_NTP_SERVER_IP;
    if (ip != NULL && ip_len > 0u) {
        (void) snprintf(ip, ip_len, "%s", peer_ip);
    }

    if (port != NULL) {
        *port = (s_peer_mode == TEST_NTP_PEER_SPOOF_PORT)
                    ? (uint16_t) (s_server_port + 1u)
                    : s_server_port;
    }

    test_fill_sntp_response(buf, len);
    *recvd_len = 48u;
    return ACTRUST_OK;
}

void setUp(void)
{
    s_peer_mode                     = TEST_NTP_PEER_MATCH;
    s_server_port                   = 0u;
    s_have_request                  = false;
    s_dns_timeout                   = 0u;
    s_send_timeout                  = 0u;
    s_recv_timeout                  = 0u;
    s_dns_calls                     = 0u;
    s_send_calls                    = 0u;
    s_recv_calls                    = 0u;
    s_monotonic_delay_after_dns_ms  = 0u;
    s_monotonic_delay_after_send_ms = 0u;
    s_block_dns                     = false;
    s_dns_entered                   = NULL;
    s_dns_release                   = NULL;
    memset(s_request_tx, 0, sizeof(s_request_tx));
    ntp = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_ntp_init(&ntp));
}

void tearDown(void)
{
    if (ntp != NULL) {
        actrust_ntp_deinit(ntp);
        ntp = NULL;
    }
    if (s_dns_release != NULL) {
        (void) actrust_sem_destroy(s_dns_release);
        s_dns_release = NULL;
    }
    if (s_dns_entered != NULL) {
        (void) actrust_sem_destroy(s_dns_entered);
        s_dns_entered = NULL;
    }
}

void test_init_null_out(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_ntp_init(NULL));
}

void test_init_success(void)
{
    TEST_ASSERT_NOT_NULL(ntp);
}

void test_deinit_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_ntp_deinit(NULL));
}

void test_deinit_success(void)
{
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_ntp_deinit(ntp));
    ntp = NULL;
}

void test_offset_before_sync(void)
{
    int64_t       offset = 0;
    actrust_err_t err    = actrust_ntp_get_last_offset_ms(ntp, &offset);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_NOT_READY, ACTRUST_ERR_CODE(err));
}

void test_now_before_sync(void)
{
    uint64_t      now = 0;
    actrust_err_t err = actrust_ntp_now_ms(ntp, &now);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_NOT_READY, ACTRUST_ERR_CODE(err));
}

void test_offset_null_args(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_ntp_get_last_offset_ms(NULL, NULL));
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_ntp_get_last_offset_ms(ntp, NULL));
}

void test_now_null_args(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_ntp_now_ms(NULL, NULL));
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_ntp_now_ms(ntp, NULL));
}

void test_sync_accepts_matching_sender(void)
{
    int64_t offset = 0;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_ntp_sync(ntp));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_ntp_get_last_offset_ms(ntp, &offset));
}

void test_sync_uses_one_timeout_budget(void)
{
    s_monotonic_delay_after_dns_ms  = 20u;
    s_monotonic_delay_after_send_ms = 20u;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_ntp_sync(ntp));
    TEST_ASSERT_EQUAL_UINT32(1u, s_dns_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, s_send_calls);
    TEST_ASSERT_EQUAL_UINT32(1u, s_recv_calls);
    TEST_ASSERT_GREATER_THAN(s_send_timeout, s_dns_timeout);
    TEST_ASSERT_GREATER_THAN(s_recv_timeout, s_send_timeout);
}

typedef struct {
    actrust_ntp_t ntp;
    actrust_err_t result;
    actrust_sem_t returned;
} ntp_worker_t;

static void sync_worker(void *arg)
{
    ntp_worker_t *worker = (ntp_worker_t *) arg;
    worker->result       = actrust_ntp_sync(worker->ntp);
    (void) actrust_sem_post(worker->returned);
}

static void deinit_worker(void *arg)
{
    ntp_worker_t *worker = (ntp_worker_t *) arg;
    worker->result       = actrust_ntp_deinit(worker->ntp);
    (void) actrust_sem_post(worker->returned);
}

void test_sync_calls_are_serialized(void)
{
    actrust_task_t first_task  = NULL;
    actrust_task_t second_task = NULL;
    actrust_sem_t  returned    = NULL;
    ntp_worker_t   first       = { .ntp = ntp, .result = ACTRUST_OK };
    ntp_worker_t   second      = { .ntp = ntp, .result = ACTRUST_OK };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&s_dns_entered, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&s_dns_release, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&returned, 0u));
    first.returned  = returned;
    second.returned = returned;
    s_block_dns     = true;

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&first_task, "ntp_sync", sync_worker,
                                          &first, 0u, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(s_dns_entered, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&second_task, "ntp_sync", sync_worker,
                                          &second, 0u, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_WOULD_BLOCK,
                      ACTRUST_ERR_CODE(actrust_sem_wait(s_dns_entered, 0u)));

    s_block_dns = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_post(s_dns_release));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(returned, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(returned, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(first_task, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(second_task, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, first.result);
    TEST_ASSERT_EQUAL(ACTRUST_OK, second.result);
    TEST_ASSERT_EQUAL_UINT32(2u, s_dns_calls);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(returned));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(s_dns_release));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(s_dns_entered));
    s_dns_release = NULL;
    s_dns_entered = NULL;
}

void test_deinit_waits_for_sync(void)
{
    actrust_task_t sync_task   = NULL;
    actrust_task_t deinit_task = NULL;
    actrust_sem_t  returned    = NULL;
    ntp_worker_t   sync        = { .ntp = ntp, .result = ACTRUST_OK };
    ntp_worker_t   deinit      = { .ntp = ntp, .result = ACTRUST_OK };

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&s_dns_entered, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&s_dns_release, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&returned, 0u));
    sync.returned   = returned;
    deinit.returned = returned;
    s_block_dns     = true;

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&sync_task, "ntp_sync", sync_worker,
                                          &sync, 0u, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(s_dns_entered, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&deinit_task, "ntp_deinit",
                                          deinit_worker, &deinit, 0u, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_WOULD_BLOCK,
                      ACTRUST_ERR_CODE(actrust_sem_wait(returned, 0u)));

    s_block_dns = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_post(s_dns_release));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(returned, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(returned, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(sync_task, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(deinit_task, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, sync.result);
    TEST_ASSERT_EQUAL(ACTRUST_OK, deinit.result);
    ntp = NULL;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(returned));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(s_dns_release));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(s_dns_entered));
    s_dns_release = NULL;
    s_dns_entered = NULL;
}

void test_sync_rejects_unexpected_sender_ip(void)
{
    int64_t offset = 0;

    s_peer_mode       = TEST_NTP_PEER_SPOOF_IP;
    actrust_err_t err = actrust_ntp_sync(ntp);

    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO, ACTRUST_ERR_CODE(err));
    err = actrust_ntp_get_last_offset_ms(ntp, &offset);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_NOT_READY, ACTRUST_ERR_CODE(err));
}

void test_sync_rejects_unexpected_sender_port(void)
{
    int64_t offset = 0;

    s_peer_mode       = TEST_NTP_PEER_SPOOF_PORT;
    actrust_err_t err = actrust_ntp_sync(ntp);

    TEST_ASSERT_EQUAL(ACTRUST_ERR_IO, ACTRUST_ERR_CODE(err));
    err = actrust_ntp_get_last_offset_ms(ntp, &offset);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_NOT_READY, ACTRUST_ERR_CODE(err));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_null_out);
    RUN_TEST(test_init_success);
    RUN_TEST(test_deinit_null);
    RUN_TEST(test_deinit_success);
    RUN_TEST(test_offset_before_sync);
    RUN_TEST(test_now_before_sync);
    RUN_TEST(test_offset_null_args);
    RUN_TEST(test_now_null_args);
    RUN_TEST(test_sync_accepts_matching_sender);
    RUN_TEST(test_sync_uses_one_timeout_budget);
    RUN_TEST(test_sync_calls_are_serialized);
    RUN_TEST(test_deinit_waits_for_sync);
    RUN_TEST(test_sync_rejects_unexpected_sender_ip);
    RUN_TEST(test_sync_rejects_unexpected_sender_port);
    return UNITY_END();
}
