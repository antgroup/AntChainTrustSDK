// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdint.h>
#include <string.h>
#include <unistd.h>

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

/* --- open / close --- */

void test_open_null_handle(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_net_open(NULL, ACTRUST_NET_TCP));
}

void test_open_tcp_close(void)
{
    actrust_net_t s = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_open(&s, ACTRUST_NET_TCP));
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_close(s));
}

void test_open_udp_close(void)
{
    actrust_net_t s = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_open(&s, ACTRUST_NET_UDP));
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_close(s));
}

void test_open_handles_fd_zero(void)
{
    int saved_stdin = dup(STDIN_FILENO);
    TEST_ASSERT_TRUE(saved_stdin >= 0);

    int           close_rc  = close(STDIN_FILENO);
    actrust_net_t s         = NULL;
    actrust_err_t open_err  = ACTRUST_OK;
    actrust_err_t close_err = ACTRUST_OK;

    if (close_rc == 0) {
        open_err = actrust_net_open(&s, ACTRUST_NET_UDP);
        if (s != NULL) {
            close_err = actrust_net_close(s);
        }
    }

    int restore_rc = dup2(saved_stdin, STDIN_FILENO);
    (void) close(saved_stdin);

    TEST_ASSERT_EQUAL_INT(0, close_rc);
    TEST_ASSERT_EQUAL_INT(STDIN_FILENO, restore_rc);
    TEST_ASSERT_EQUAL(ACTRUST_OK, open_err);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL(ACTRUST_OK, close_err);
}

void test_close_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_net_close(NULL));
}

/* --- connect --- */

void test_connect_null_handle(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_net_connect(NULL, "127.0.0.1", 80, 100));
}

void test_connect_null_ip(void)
{
    actrust_net_t s = NULL;
    actrust_net_open(&s, ACTRUST_NET_TCP);
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_net_connect(s, NULL, 80, 100));
    actrust_net_close(s);
}

/* --- send / recv --- */

void test_send_null_handle(void)
{
    uint8_t buf[] = "x";
    size_t  sent  = 0;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_net_send(NULL, buf, 1, 100, &sent));
}

void test_recv_null_handle(void)
{
    uint8_t buf[16];
    size_t  got = 0;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_net_recv(NULL, buf, sizeof(buf), 100, &got));
}

/* --- DNS --- */

void test_dns_null_host(void)
{
    char ip[16];
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_net_dns_resolve(NULL, ip, sizeof(ip), 1000));
}

void test_dns_null_ip_buf(void)
{
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_net_dns_resolve("example.com", NULL, 0, 1000));
}

void test_dns_resolve_localhost(void)
{
    char ip[16];
    memset(ip, 0, sizeof(ip));
    TEST_ASSERT_EQUAL(
        ACTRUST_OK, actrust_net_dns_resolve("localhost", ip, sizeof(ip), 3000));
    TEST_ASSERT_EQUAL_STRING("127.0.0.1", ip);
}

/* --- UDP sendto/recvfrom --- */

void test_sendto_null_handle(void)
{
    uint8_t buf[] = "x";
    size_t  sent  = 0;
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK,
        actrust_net_sendto(NULL, "127.0.0.1", 9999, buf, 1, 100, &sent));
}

void test_recvfrom_null_handle(void)
{
    char     ip[16];
    uint16_t port = 0;
    uint8_t  buf[16];
    size_t   got = 0;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_net_recvfrom(NULL, ip, sizeof(ip), &port, buf,
                                               sizeof(buf), 100, &got));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_null_handle);
    RUN_TEST(test_open_tcp_close);
    RUN_TEST(test_open_udp_close);
    RUN_TEST(test_open_handles_fd_zero);
    RUN_TEST(test_close_null);
    RUN_TEST(test_connect_null_handle);
    RUN_TEST(test_connect_null_ip);
    RUN_TEST(test_send_null_handle);
    RUN_TEST(test_recv_null_handle);
    RUN_TEST(test_dns_null_host);
    RUN_TEST(test_dns_null_ip_buf);
    RUN_TEST(test_dns_resolve_localhost);
    RUN_TEST(test_sendto_null_handle);
    RUN_TEST(test_recvfrom_null_handle);
    return UNITY_END();
}
