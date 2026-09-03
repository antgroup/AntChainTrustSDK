// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Adapter */
#include "adapter/network.h"

void setUp(void)
{
}
void tearDown(void)
{
}

void test_tcp_lifecycle(void)
{
    actrust_net_t sock = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_open(&sock, ACTRUST_NET_TCP));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_close(sock));
}

void test_udp_lifecycle(void)
{
    actrust_net_t sock = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_open(&sock, ACTRUST_NET_UDP));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_close(sock));
}

void test_dns_resolve(void)
{
    char          ip[16] = { 0 };
    actrust_err_t err =
        actrust_net_dns_resolve("example.com", ip, sizeof(ip), 5000);
    if (!ACTRUST_IS_OK(err)) {
        TEST_IGNORE_MESSAGE("external DNS unavailable");
    }
    TEST_ASSERT_GREATER_THAN(0u, strlen(ip));
}

void test_tcp_echo(void)
{
    char          ip[16] = { 0 };
    actrust_err_t err =
        actrust_net_dns_resolve("example.com", ip, sizeof(ip), 5000);
    if (!ACTRUST_IS_OK(err)) {
        TEST_IGNORE_MESSAGE("external DNS unavailable, skipping tcp echo");
    }

    actrust_net_t sock = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_open(&sock, ACTRUST_NET_TCP));

    err = actrust_net_connect(sock, ip, 80, 5000);
    if (!ACTRUST_IS_OK(err)) {
        (void) actrust_net_close(sock);
        TEST_IGNORE_MESSAGE("external TCP endpoint unavailable");
    }

    const char *req     = "HEAD / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    size_t      req_len = strlen(req);
    size_t      sent    = 0;
    err = actrust_net_send(sock, (const uint8_t *) req, req_len, 5000, &sent);
    if (!ACTRUST_IS_OK(err)) {
        (void) actrust_net_close(sock);
        TEST_IGNORE_MESSAGE("external TCP send unavailable");
    }
    TEST_ASSERT_EQUAL(req_len, sent);

    uint8_t rx[256] = { 0 };
    size_t  got     = 0;
    err             = actrust_net_recv(sock, rx, sizeof(rx) - 1, 5000, &got);
    if (!ACTRUST_IS_OK(err)) {
        (void) actrust_net_close(sock);
        TEST_IGNORE_MESSAGE("external TCP receive unavailable");
    }
    TEST_ASSERT_GREATER_THAN(0u, got);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_close(sock));
}

void test_udp_sendrecv(void)
{
    char          ip[16] = { 0 };
    actrust_err_t err =
        actrust_net_dns_resolve("pool.ntp.org", ip, sizeof(ip), 5000);
    if (!ACTRUST_IS_OK(err)) {
        TEST_IGNORE_MESSAGE("external DNS unavailable, skipping udp sendrecv");
    }

    actrust_net_t sock = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_open(&sock, ACTRUST_NET_UDP));

    uint8_t ntp_req[48];
    memset(ntp_req, 0, sizeof(ntp_req));
    ntp_req[0] = 0x1B;

    size_t sent = 0;
    err = actrust_net_sendto(sock, ip, 123, ntp_req, sizeof(ntp_req), 5000,
                             &sent);
    if (!ACTRUST_IS_OK(err)) {
        (void) actrust_net_close(sock);
        TEST_IGNORE_MESSAGE("external UDP endpoint unavailable");
    }
    TEST_ASSERT_EQUAL(sizeof(ntp_req), sent);

    uint8_t  rx[64]      = { 0 };
    char     peer_ip[16] = { 0 };
    uint16_t peer_port   = 0;
    size_t   got         = 0;
    err = actrust_net_recvfrom(sock, peer_ip, sizeof(peer_ip), &peer_port, rx,
                               sizeof(rx), 5000, &got);
    if (!ACTRUST_IS_OK(err)) {
        (void) actrust_net_close(sock);
        TEST_IGNORE_MESSAGE("external UDP receive unavailable");
    }
    TEST_ASSERT_GREATER_OR_EQUAL(48u, got);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_close(sock));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tcp_lifecycle);
    RUN_TEST(test_udp_lifecycle);
    RUN_TEST(test_dns_resolve);
    RUN_TEST(test_tcp_echo);
    RUN_TEST(test_udp_sendrecv);
    return UNITY_END();
}
