// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file network.c
 * @brief Android platform network adapter implementation
 *
 * Implements the AntChainTrustSDK network interface using Android Bionic POSIX
 * sockets. Timeout management is based on poll() with monotonic-clock EINTR
 * recovery. TCP connect uses non-blocking connect + poll + SO_ERROR pattern.
 *
 * @note DNS resolution uses a worker thread because getaddrinfo() has no
 *       portable per-call timeout control.
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

/* C standard */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Adapter */
#include "adapter/network.h"

/** @brief Convenience macro to build network-module errors */
#define NET_ERR(reason)                                                        \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_NETWORK, (reason))

#define NET_DNS_DEFAULT_TIMEOUT_MS (30000u)

/**
 * @brief Convert between actrust_net_t and file descriptor.
 *
 * Keep NULL reserved as the invalid public handle while still allowing POSIX
 * fd 0, which is a valid descriptor.
 */
#define ACTRUST_NET_TO_FD(net) ((int) ((intptr_t) (net) - 1))
#define FD_TO_ACTRUST_NET(fd)  ((actrust_net_t) (intptr_t) ((fd) + 1))

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    bool            lock_ready;
    bool            cond_ready;
    bool            completed;
    bool            detached;
    char           *host;
    char            ip[16];
    int             gai_rc;
    bool            inet_ok;
} net_dns_resolve_ctx_t;

static pthread_mutex_t g_dns_resolver_gate   = PTHREAD_MUTEX_INITIALIZER;
static bool            g_dns_resolver_active = false;

/* ========================================================================
 * Private Helpers
 * ======================================================================== */

static actrust_err_t net_dns_resolver_acquire(void)
{
    if (pthread_mutex_lock(&g_dns_resolver_gate) != 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    if (g_dns_resolver_active) {
        (void) pthread_mutex_unlock(&g_dns_resolver_gate);
        return NET_ERR(ACTRUST_ERR_BUSY);
    }

    g_dns_resolver_active = true;
    (void) pthread_mutex_unlock(&g_dns_resolver_gate);
    return ACTRUST_OK;
}

static void net_dns_resolver_release(void)
{
    if (pthread_mutex_lock(&g_dns_resolver_gate) == 0) {
        g_dns_resolver_active = false;
        (void) pthread_mutex_unlock(&g_dns_resolver_gate);
    }
}

static void net_dns_ctx_free(net_dns_resolve_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->cond_ready) {
        (void) pthread_cond_destroy(&ctx->cond);
    }
    if (ctx->lock_ready) {
        (void) pthread_mutex_destroy(&ctx->lock);
    }
    free(ctx->host);
    free(ctx);
}

static void *net_dns_resolve_worker(void *arg)
{
    net_dns_resolve_ctx_t *ctx = (net_dns_resolve_ctx_t *) arg;
    if (ctx == NULL) {
        return NULL;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    ctx->gai_rc          = getaddrinfo(ctx->host, NULL, &hints, &res);
    if (ctx->gai_rc == 0 && res != NULL && res->ai_addr != NULL) {
        struct sockaddr_in *addr = (struct sockaddr_in *) res->ai_addr;
        const char         *ip   = inet_ntop(AF_INET, &addr->sin_addr, ctx->ip,
                                             (socklen_t) sizeof(ctx->ip));
        ctx->inet_ok             = ip != NULL;
    }

    if (res != NULL) {
        freeaddrinfo(res);
    }

    bool should_free = false;
    if (pthread_mutex_lock(&ctx->lock) == 0) {
        ctx->completed = true;
        should_free    = ctx->detached;
        (void) pthread_cond_broadcast(&ctx->cond);
        (void) pthread_mutex_unlock(&ctx->lock);
    }

    if (should_free) {
        net_dns_ctx_free(ctx);
    }

    net_dns_resolver_release();

    return NULL;
}

static bool net_dns_detach_if_running(pthread_t              thread,
                                      net_dns_resolve_ctx_t *ctx)
{
    bool running = false;

    if (pthread_mutex_lock(&ctx->lock) == 0) {
        running = !ctx->completed;
        if (running) {
            ctx->detached = true;
        }
        (void) pthread_mutex_unlock(&ctx->lock);
    }

    if (running) {
        (void) pthread_detach(thread);
    }

    return running;
}

static actrust_err_t net_dns_wait_complete(net_dns_resolve_ctx_t *ctx,
                                           const struct timespec *deadline)
{
    int rc = 0;

    if (pthread_mutex_lock(&ctx->lock) != 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    while (!ctx->completed && rc == 0) {
        rc = pthread_cond_timedwait(&ctx->cond, &ctx->lock, deadline);
    }

    bool completed = ctx->completed;
    (void) pthread_mutex_unlock(&ctx->lock);

    if (completed) {
        return ACTRUST_OK;
    }
    if (rc == ETIMEDOUT) {
        return NET_ERR(ACTRUST_ERR_TIMEOUT);
    }
    return NET_ERR(ACTRUST_ERR_IO);
}

static actrust_err_t net_dns_copy_result(const net_dns_resolve_ctx_t *ctx,
                                         char *ip, size_t ip_len)
{
    if (ctx->gai_rc != 0 || !ctx->inet_ok) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    size_t resolved_len = strlen(ctx->ip);
    if (resolved_len + 1u > ip_len) {
        return NET_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    memcpy(ip, ctx->ip, resolved_len + 1u);
    return ACTRUST_OK;
}

/**
 * @brief Interpret poll(2) revents after a successful return (rc > 0)
 *
 * Error events (POLLNVAL, POLLERR) always indicate failure.  For reads,
 * POLLHUP (peer closed) is treated as "ready" so the caller's recv()
 * can discover EOF normally (returning 0 bytes).
 *
 * @param revents  The revents field from struct pollfd
 * @param is_read  True for read-readiness, false for write-readiness
 * @return @c ACTRUST_OK if the descriptor is ready
 * @return @c ACTRUST_ERR_IO on error condition
 */
static actrust_err_t net_poll_check_revents(short revents, bool is_read)
{
    /* POLLNVAL (bad fd) and POLLERR are always fatal */
    if (revents & (POLLNVAL | POLLERR)) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    const int ready = is_read ? (POLLIN | POLLHUP) : POLLOUT;
    return (revents & ready) ? ACTRUST_OK : NET_ERR(ACTRUST_ERR_IO);
}

/**
 * @brief Wait for a file descriptor to become ready using poll()
 *
 * Handles EINTR by recalculating remaining time with monotonic clock to
 * guarantee the total wait never exceeds @p timeout_ms.
 *
 * @param fd         File descriptor
 * @param is_read    True to wait for read-readiness, false for write-readiness
 * @param timeout_ms Timeout in milliseconds; 0 = non-blocking check
 * @return @c ACTRUST_OK if the descriptor is ready
 * @return @c ACTRUST_ERR_TIMEOUT on timeout (including non-blocking)
 * @return @c ACTRUST_ERR_IO on poll error or error condition on the descriptor
 */
static actrust_err_t actrust_net_poll_wait(int fd, bool is_read,
                                           uint32_t timeout_ms)
{
    struct pollfd pfd = {
        .fd     = fd,
        .events = is_read ? POLLIN : POLLOUT,
    };

    /* Non-blocking check */
    if (timeout_ms == 0) {
        int rc = poll(&pfd, 1, 0);
        if (rc > 0) {
            return net_poll_check_revents(pfd.revents, is_read);
        }
        return (rc == 0 || errno == EINTR) ? NET_ERR(ACTRUST_ERR_TIMEOUT)
                                           : NET_ERR(ACTRUST_ERR_IO);
    }

    /* Timed wait with EINTR recovery */
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int remaining =
        (timeout_ms > (uint32_t) INT_MAX) ? INT_MAX : (int) timeout_ms;

    for (;;) {
        int rc = poll(&pfd, 1, remaining);

        if (rc > 0) {
            return net_poll_check_revents(pfd.revents, is_read);
        }
        if (rc == 0) {
            return NET_ERR(ACTRUST_ERR_TIMEOUT);
        }

        /* rc < 0 */
        if (errno != EINTR) {
            return NET_ERR(ACTRUST_ERR_IO);
        }

        /* EINTR: recalculate remaining time */
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t elapsed_ms = (int64_t) (now.tv_sec - start.tv_sec) * 1000 +
                             (int64_t) (now.tv_nsec - start.tv_nsec) / 1000000;

        if (elapsed_ms >= (int64_t) timeout_ms) {
            return NET_ERR(ACTRUST_ERR_TIMEOUT);
        }

        int64_t left = (int64_t) timeout_ms - elapsed_ms;
        remaining    = (left > (int64_t) INT_MAX) ? INT_MAX : (int) left;
    }
}

/**
 * @brief Set or clear O_NONBLOCK on a file descriptor
 *
 * @param fd         File descriptor
 * @param enable     True to set O_NONBLOCK, false to clear
 *
 * @return @c true on success
 * @return @c false on failure
 */
static bool net_set_nonblock(int fd, bool enable)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }

    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    return fcntl(fd, F_SETFL, flags) == 0;
}

/* ========================================================================
 * Public API – Lifecycle
 * ======================================================================== */

actrust_err_t actrust_net_open(actrust_net_t *out, actrust_net_type_t type)
{
    if (out == NULL) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    int sock_type;
    switch (type) {
        case ACTRUST_NET_TCP:
            sock_type = SOCK_STREAM;
            break;
        case ACTRUST_NET_UDP:
            sock_type = SOCK_DGRAM;
            break;
        default:
            return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    int fd = socket(AF_INET, sock_type, 0);
    if (fd < 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    *out = FD_TO_ACTRUST_NET(fd);
    return ACTRUST_OK;
}

actrust_err_t actrust_net_close(actrust_net_t net)
{
    if (net == NULL) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    (void) close(ACTRUST_NET_TO_FD(net));
    return ACTRUST_OK;
}

/* ========================================================================
 * Public API – TCP
 * ======================================================================== */

actrust_err_t actrust_net_connect(actrust_net_t net, const char *remote_ip,
                                  uint16_t port, uint32_t timeout_ms)
{
    if (net == NULL || remote_ip == NULL) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    int fd = ACTRUST_NET_TO_FD(net);

    /* Build destination address */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, remote_ip, &addr.sin_addr) != 1) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    /* Set non-blocking for timeout-controlled connect */
    if (!net_set_nonblock(fd, true)) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    int rc = connect(fd, (struct sockaddr *) &addr, sizeof(addr));

    if (rc == 0) {
        (void) net_set_nonblock(fd, false);
        return ACTRUST_OK;
    }

    if (errno != EINPROGRESS) {
        (void) net_set_nonblock(fd, false);
        return NET_ERR(ACTRUST_ERR_IO);
    }

    /* Wait for connection to complete or time out */
    actrust_err_t err = actrust_net_poll_wait(fd, false, timeout_ms);

    /* Restore blocking mode regardless of outcome */
    (void) net_set_nonblock(fd, false);

    if (err != ACTRUST_OK) {
        return err;
    }

    /* Verify the connection actually succeeded */
    int       so_err = 0;
    socklen_t errlen = sizeof(so_err);

    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &errlen) != 0 ||
        so_err != 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    return ACTRUST_OK;
}

actrust_err_t actrust_net_send(actrust_net_t net, const uint8_t *buf,
                               size_t len, uint32_t timeout_ms,
                               size_t *sent_len)
{
    if (net == NULL || buf == NULL || sent_len == NULL || len == 0) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    *sent_len = 0;
    int fd    = ACTRUST_NET_TO_FD(net);

    /* Wait until the socket is writable */
    actrust_err_t err = actrust_net_poll_wait(fd, false, timeout_ms);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    size_t  io_len = (len > (size_t) SSIZE_MAX) ? (size_t) SSIZE_MAX : len;
    ssize_t n;
    do {
        n = send(fd, buf, io_len, MSG_NOSIGNAL | MSG_DONTWAIT);
    } while (n < 0 && errno == EINTR);
    int send_errno = errno;

    if (n < 0) {
        if (send_errno == EAGAIN || send_errno == EWOULDBLOCK) {
            return NET_ERR(ACTRUST_ERR_TIMEOUT);
        }
        return NET_ERR(ACTRUST_ERR_IO);
    }

    *sent_len = (size_t) n;
    return ACTRUST_OK;
}

actrust_err_t actrust_net_recv(actrust_net_t net, uint8_t *buf, size_t len,
                               uint32_t timeout_ms, size_t *recvd_len)
{
    if (net == NULL || buf == NULL || recvd_len == NULL || len == 0) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    *recvd_len = 0;
    int fd     = ACTRUST_NET_TO_FD(net);

    /* Wait until data is available */
    actrust_err_t err = actrust_net_poll_wait(fd, true, timeout_ms);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    size_t  io_len = (len > (size_t) SSIZE_MAX) ? (size_t) SSIZE_MAX : len;
    ssize_t n;
    do {
        n = recv(fd, buf, io_len, 0);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    /* n == 0 means peer closed connection; ACTRUST_OK with recvd_len 0 */
    *recvd_len = (size_t) n;
    return ACTRUST_OK;
}

/* ========================================================================
 * Public API – UDP
 * ======================================================================== */

actrust_err_t actrust_net_bind(actrust_net_t net, uint16_t port)
{
    if (net == NULL) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    int fd = ACTRUST_NET_TO_FD(net);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    return ACTRUST_OK;
}

actrust_err_t actrust_net_sendto(actrust_net_t net, const char *ip,
                                 uint16_t port, const uint8_t *buf, size_t len,
                                 uint32_t timeout_ms, size_t *sent_len)
{
    if (net == NULL || ip == NULL || buf == NULL || sent_len == NULL ||
        len == 0) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    *sent_len = 0;
    int fd    = ACTRUST_NET_TO_FD(net);

    /* Build destination address */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    /* Wait until the socket is writable */
    actrust_err_t err = actrust_net_poll_wait(fd, false, timeout_ms);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    size_t  io_len = (len > (size_t) SSIZE_MAX) ? (size_t) SSIZE_MAX : len;
    ssize_t n;
    do {
        n = sendto(fd, buf, io_len, MSG_NOSIGNAL, (struct sockaddr *) &addr,
                   sizeof(addr));
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    *sent_len = (size_t) n;
    return ACTRUST_OK;
}

actrust_err_t actrust_net_recvfrom(actrust_net_t net, char *ip, size_t ip_len,
                                   uint16_t *port, uint8_t *buf, size_t len,
                                   uint32_t timeout_ms, size_t *recvd_len)
{
    if (net == NULL || buf == NULL || recvd_len == NULL || len == 0) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    *recvd_len = 0;
    int fd     = ACTRUST_NET_TO_FD(net);

    /* Wait until a datagram is available */
    actrust_err_t err = actrust_net_poll_wait(fd, true, timeout_ms);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    size_t io_len = (len > (size_t) SSIZE_MAX) ? (size_t) SSIZE_MAX : len;
    struct sockaddr_in addr;
    socklen_t          addrlen = sizeof(addr);
    memset(&addr, 0, sizeof(addr));

    ssize_t n;
    do {
        n = recvfrom(fd, buf, io_len, 0, (struct sockaddr *) &addr, &addrlen);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    /* Populate sender address if caller provided buffers */
    if (ip != NULL && ip_len > 0) {
        if (inet_ntop(AF_INET, &addr.sin_addr, ip, (socklen_t) ip_len) ==
            NULL) {
            ip[0] = '\0';
        }
    }

    if (port != NULL) {
        *port = ntohs(addr.sin_port);
    }

    *recvd_len = (size_t) n;
    return ACTRUST_OK;
}

/* ========================================================================
 * Public API – DNS
 * ======================================================================== */

actrust_err_t actrust_net_dns_resolve(const char *host, char *ip, size_t ip_len,
                                      uint32_t timeout_ms)
{
    if (host == NULL || ip == NULL || ip_len == 0) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = net_dns_resolver_acquire();
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    uint32_t effective_timeout = timeout_ms;
    if (effective_timeout == 0u) {
        effective_timeout = NET_DNS_DEFAULT_TIMEOUT_MS;
    }

    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        net_dns_resolver_release();
        return NET_ERR(ACTRUST_ERR_IO);
    }

    uint64_t nsec =
        (uint64_t) deadline.tv_nsec + (uint64_t) effective_timeout * 1000000u;
    deadline.tv_sec += (time_t) (nsec / 1000000000u);
    deadline.tv_nsec = (long) (nsec % 1000000000u);

    net_dns_resolve_ctx_t *ctx =
        (net_dns_resolve_ctx_t *) calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        net_dns_resolver_release();
        return NET_ERR(ACTRUST_ERR_NO_MEM);
    }

    size_t host_len = strlen(host);
    ctx->host       = (char *) malloc(host_len + 1u);
    if (ctx->host == NULL) {
        net_dns_resolver_release();
        net_dns_ctx_free(ctx);
        return NET_ERR(ACTRUST_ERR_NO_MEM);
    }
    memcpy(ctx->host, host, host_len + 1u);

    if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
        net_dns_resolver_release();
        net_dns_ctx_free(ctx);
        return NET_ERR(ACTRUST_ERR_IO);
    }
    ctx->lock_ready = true;

    if (pthread_cond_init(&ctx->cond, NULL) != 0) {
        net_dns_resolver_release();
        net_dns_ctx_free(ctx);
        return NET_ERR(ACTRUST_ERR_IO);
    }
    ctx->cond_ready = true;

    pthread_t thread;
    int       rc = pthread_create(&thread, NULL, net_dns_resolve_worker, ctx);
    if (rc != 0) {
        net_dns_resolver_release();
        net_dns_ctx_free(ctx);
        return NET_ERR(ACTRUST_ERR_IO);
    }

    err = net_dns_wait_complete(ctx, &deadline);
    if (ACTRUST_ERR_CODE(err) == ACTRUST_ERR_TIMEOUT) {
        if (net_dns_detach_if_running(thread, ctx)) {
            return err;
        }
        err = ACTRUST_OK;
    }

    if (err != ACTRUST_OK) {
        if (net_dns_detach_if_running(thread, ctx)) {
            return err;
        }
        (void) pthread_join(thread, NULL);
        net_dns_ctx_free(ctx);
        return err;
    }

    rc = pthread_join(thread, NULL);
    if (rc != 0) {
        net_dns_ctx_free(ctx);
        return NET_ERR(ACTRUST_ERR_IO);
    }

    err = net_dns_copy_result(ctx, ip, ip_len);
    net_dns_ctx_free(ctx);

    return err;
}
