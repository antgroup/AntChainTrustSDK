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
    (void) timeout_ms;

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
    (void) timeout_ms;

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
    (void) timeout_ms;

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
    s_peer_mode    = TEST_NTP_PEER_MATCH;
    s_server_port  = 0u;
    s_have_request = false;
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
    RUN_TEST(test_sync_rejects_unexpected_sender_ip);
    RUN_TEST(test_sync_rejects_unexpected_sender_port);
    return UNITY_END();
}
