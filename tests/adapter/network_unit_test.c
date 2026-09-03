// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

/* C standard */
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Third-party */
#include "unity.h"

/* Adapter */
#include "adapter/network.h"

#ifdef ACTRUST_TEST_WRAP_CLOCK_GETTIME
extern int __real_clock_gettime(clockid_t clock_id, struct timespec *tp);

static bool s_saw_monotonic;
static bool s_saw_realtime;

int __wrap_clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    s_saw_monotonic = s_saw_monotonic || clock_id == CLOCK_MONOTONIC;
    s_saw_realtime  = s_saw_realtime || clock_id == CLOCK_REALTIME;
    return __real_clock_gettime(clock_id, tp);
}
#else
static bool s_saw_monotonic;
static bool s_saw_realtime;
#endif

typedef struct {
    actrust_net_t receiver;
    int           sender_fd;
    uint16_t      receiver_port;
    uint16_t      sender_port;
} udp_fixture_t;

static void udp_fixture_open(udp_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->sender_fd = -1;

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_net_open(&fixture->receiver, ACTRUST_NET_UDP));

    int probe_fd = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_TRUE(probe_fd >= 0);
    struct sockaddr_in receiver_addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        .sin_port        = 0,
    };
    TEST_ASSERT_EQUAL_INT(0, bind(probe_fd, (struct sockaddr *) &receiver_addr,
                                  sizeof(receiver_addr)));
    socklen_t receiver_len = sizeof(receiver_addr);
    TEST_ASSERT_EQUAL_INT(0, getsockname(probe_fd,
                                         (struct sockaddr *) &receiver_addr,
                                         &receiver_len));
    fixture->receiver_port = ntohs(receiver_addr.sin_port);
    TEST_ASSERT_EQUAL_INT(0, close(probe_fd));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_bind(fixture->receiver,
                                                   fixture->receiver_port));

    fixture->sender_fd = socket(AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT_TRUE(fixture->sender_fd >= 0);
    struct sockaddr_in sender_addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        .sin_port        = 0,
    };
    TEST_ASSERT_EQUAL_INT(0, bind(fixture->sender_fd,
                                  (struct sockaddr *) &sender_addr,
                                  sizeof(sender_addr)));
    socklen_t sender_len = sizeof(sender_addr);
    TEST_ASSERT_EQUAL_INT(0, getsockname(fixture->sender_fd,
                                         (struct sockaddr *) &sender_addr,
                                         &sender_len));
    fixture->sender_port = ntohs(sender_addr.sin_port);
}

static void udp_fixture_send(const udp_fixture_t *fixture, const void *data,
                             size_t len)
{
    struct sockaddr_in receiver_addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        .sin_port        = htons(fixture->receiver_port),
    };
    TEST_ASSERT_EQUAL_INT((int) len,
                          (int) sendto(fixture->sender_fd, data, len, 0,
                                       (struct sockaddr *) &receiver_addr,
                                       sizeof(receiver_addr)));
}

static void udp_fixture_close(udp_fixture_t *fixture)
{
    if (fixture->sender_fd >= 0) {
        TEST_ASSERT_EQUAL_INT(0, close(fixture->sender_fd));
        fixture->sender_fd = -1;
    }
    if (fixture->receiver != NULL) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_net_close(fixture->receiver));
        fixture->receiver = NULL;
    }
}

void setUp(void)
{
    s_saw_monotonic = false;
    s_saw_realtime  = false;
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
#ifdef ACTRUST_TEST_WRAP_CLOCK_GETTIME
    TEST_ASSERT_TRUE(s_saw_monotonic);
    TEST_ASSERT_FALSE(s_saw_realtime);
#endif
}

void test_dns_resolve_buffer_too_small(void)
{
    char ip[9];
    memset(ip, 'X', sizeof(ip));
    actrust_err_t err =
        actrust_net_dns_resolve("localhost", ip, sizeof(ip), 3000);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL, ACTRUST_ERR_CODE(err));
    for (size_t i = 0u; i < sizeof(ip); ++i) {
        TEST_ASSERT_EQUAL_CHAR('X', ip[i]);
    }
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

void test_recvfrom_loopback_outputs(void)
{
    udp_fixture_t fixture;
    udp_fixture_open(&fixture);

    const uint8_t payload[] = "network-c13";
    udp_fixture_send(&fixture, payload, sizeof(payload) - 1u);

    char     ip[16]       = { 0 };
    uint16_t port         = 0u;
    uint8_t  received[32] = { 0 };
    size_t   received_len = SIZE_MAX;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_net_recvfrom(fixture.receiver, ip, sizeof(ip),
                                           &port, received, sizeof(received),
                                           1000u, &received_len));
    TEST_ASSERT_EQUAL_STRING("127.0.0.1", ip);
    TEST_ASSERT_EQUAL(fixture.sender_port, port);
    TEST_ASSERT_EQUAL(sizeof(payload) - 1u, received_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, received, received_len);

    udp_fixture_close(&fixture);
}

void test_recvfrom_small_ip_does_not_consume(void)
{
    udp_fixture_t fixture;
    udp_fixture_open(&fixture);

    const uint8_t payload[] = "preserve-datagram";
    udp_fixture_send(&fixture, payload, sizeof(payload) - 1u);

    char ip[8];
    memset(ip, 'I', sizeof(ip));
    uint16_t      port         = 0xBEEFu;
    uint8_t       received[32] = { 0 };
    size_t        received_len = SIZE_MAX;
    actrust_err_t err =
        actrust_net_recvfrom(fixture.receiver, ip, sizeof(ip), &port, received,
                             sizeof(received), 1000u, &received_len);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_EQUAL(0u, received_len);
    TEST_ASSERT_EQUAL_HEX16(0xBEEFu, port);
    for (size_t i = 0u; i < sizeof(ip); ++i) {
        TEST_ASSERT_EQUAL_CHAR('I', ip[i]);
    }

    char full_ip[16] = { 0 };
    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_net_recvfrom(fixture.receiver, full_ip, sizeof(full_ip), &port,
                             received, sizeof(received), 1000u, &received_len));
    TEST_ASSERT_EQUAL_STRING("127.0.0.1", full_ip);
    TEST_ASSERT_EQUAL(sizeof(payload) - 1u, received_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, received, received_len);

    udp_fixture_close(&fixture);
}

void test_recvfrom_small_payload_does_not_consume(void)
{
    udp_fixture_t fixture;
    udp_fixture_open(&fixture);

    const uint8_t payload[] = "payload-too-large";
    udp_fixture_send(&fixture, payload, sizeof(payload) - 1u);

    uint8_t       received[4]  = { 0 };
    size_t        received_len = SIZE_MAX;
    actrust_err_t err =
        actrust_net_recvfrom(fixture.receiver, NULL, 0u, NULL, received,
                             sizeof(received), 1000u, &received_len);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL, ACTRUST_ERR_CODE(err));
    TEST_ASSERT_EQUAL(0u, received_len);

    uint8_t full[32] = { 0 };
    TEST_ASSERT_EQUAL(
        ACTRUST_OK, actrust_net_recvfrom(fixture.receiver, NULL, 0u, NULL, full,
                                         sizeof(full), 1000u, &received_len));
    TEST_ASSERT_EQUAL(sizeof(payload) - 1u, received_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, full, received_len);

    udp_fixture_close(&fixture);
}

void test_recvfrom_timeout_outputs_zero_length(void)
{
    udp_fixture_t fixture;
    udp_fixture_open(&fixture);

    uint8_t received[8]  = { 0 };
    size_t  received_len = SIZE_MAX;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_TIMEOUT,
                      ACTRUST_ERR_CODE(actrust_net_recvfrom(
                          fixture.receiver, NULL, 0u, NULL, received,
                          sizeof(received), 0u, &received_len)));
    TEST_ASSERT_EQUAL(0u, received_len);

    udp_fixture_close(&fixture);
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
    RUN_TEST(test_dns_resolve_buffer_too_small);
    RUN_TEST(test_sendto_null_handle);
    RUN_TEST(test_recvfrom_loopback_outputs);
    RUN_TEST(test_recvfrom_small_ip_does_not_consume);
    RUN_TEST(test_recvfrom_small_payload_does_not_consume);
    RUN_TEST(test_recvfrom_timeout_outputs_zero_length);
    return UNITY_END();
}
