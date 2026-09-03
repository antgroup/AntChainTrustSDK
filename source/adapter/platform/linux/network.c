// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file network.c
 * @brief Linux platform network adapter implementation
 *
 * Implements the AntChainTrustSDK network interface using POSIX sockets.
 * Timeout management is based on poll() with monotonic-clock EINTR recovery.
 * TCP connect uses non-blocking connect + poll + SO_ERROR pattern.
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
    struct timespec expires;
    bool            immediate;
} net_deadline_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    bool            lock_ready;
    bool            cond_ready;
    bool            completed;
    bool            detached;
    char           *host;
    char            ip[INET_ADDRSTRLEN];
    int             gai_rc;
    bool            inet_ok;
} net_dns_resolve_ctx_t;

static pthread_mutex_t g_dns_resolver_gate   = PTHREAD_MUTEX_INITIALIZER;
static bool            g_dns_resolver_active = false;

/* ========================================================================
 * Private Helpers
 * ======================================================================== */

static actrust_err_t net_deadline_start(uint32_t        timeout_ms,
                                        net_deadline_t *deadline)
{
    if (deadline == NULL) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    deadline->immediate = timeout_ms == 0u;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline->expires) != 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    uint64_t nsec =
        (uint64_t) deadline->expires.tv_nsec + (uint64_t) timeout_ms * 1000000u;
    deadline->expires.tv_sec += (time_t) (nsec / 1000000000u);
    deadline->expires.tv_nsec = (long) (nsec % 1000000000u);
    return ACTRUST_OK;
}

static actrust_err_t net_deadline_remaining(const net_deadline_t *deadline,
                                            int                  *remaining_ms)
{
    if (deadline == NULL || remaining_ms == NULL) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (deadline->immediate) {
        *remaining_ms = 0;
        return ACTRUST_OK;
    }

    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    int64_t sec  = (int64_t) deadline->expires.tv_sec - (int64_t) now.tv_sec;
    int64_t nsec = (int64_t) deadline->expires.tv_nsec - (int64_t) now.tv_nsec;
    int64_t left_ns = sec * 1000000000ll + nsec;
    if (left_ns <= 0) {
        return NET_ERR(ACTRUST_ERR_TIMEOUT);
    }

    int64_t left_ms = left_ns / 1000000ll;
    *remaining_ms   = left_ms > INT_MAX ? INT_MAX : (int) left_ms;
    return ACTRUST_OK;
}

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
        ctx->inet_ok             = inet_ntop(AF_INET, &addr->sin_addr, ctx->ip,
                                             (socklen_t) sizeof(ctx->ip)) != NULL;
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

static actrust_err_t net_dns_transfer_to_worker(pthread_t              thread,
                                                net_dns_resolve_ctx_t *ctx)
{
    if (pthread_mutex_lock(&ctx->lock) != 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    bool running = !ctx->completed;
    if (running) {
        int rc = pthread_detach(thread);
        if (rc == 0) {
            ctx->detached = true;
        }
        (void) pthread_mutex_unlock(&ctx->lock);
        return rc == 0 ? ACTRUST_OK : NET_ERR(ACTRUST_ERR_IO);
    }

    (void) pthread_mutex_unlock(&ctx->lock);
    return NET_ERR(ACTRUST_ERR_BAD_STATE);
}

static actrust_err_t net_dns_wait_complete(net_dns_resolve_ctx_t *ctx,
                                           const struct timespec *deadline)
{
    if (pthread_mutex_lock(&ctx->lock) != 0) {
        return NET_ERR(ACTRUST_ERR_IO);
    }

    int rc = 0;
    while (!ctx->completed && rc == 0) {
        rc = pthread_cond_timedwait(&ctx->cond, &ctx->lock, deadline);
    }

    bool completed = ctx->completed;
    (void) pthread_mutex_unlock(&ctx->lock);

    if (completed && rc == 0) {
        int            remaining     = 0;
        net_deadline_t wait_deadline = {
            .expires   = *deadline,
            .immediate = false,
        };
        return net_deadline_remaining(&wait_deadline, &remaining);
    }
    return rc == ETIMEDOUT ? NET_ERR(ACTRUST_ERR_TIMEOUT)
                           : NET_ERR(ACTRUST_ERR_IO);
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
                                           const net_deadline_t *deadline)
{
    struct pollfd pfd = {
        .fd     = fd,
        .events = is_read ? POLLIN : POLLOUT,
    };

    for (;;) {
        int           remaining = 0;
        actrust_err_t err       = net_deadline_remaining(deadline, &remaining);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }

        int rc = poll(&pfd, 1, remaining);
        if (rc > 0) {
            if (!deadline->immediate) {
                err = net_deadline_remaining(deadline, &remaining);
                if (ACTRUST_IS_ERR(err)) {
                    return err;
                }
            }
            return net_poll_check_revents(pfd.revents, is_read);
        }
        if (rc == 0) {
            if (deadline->immediate) {
                return NET_ERR(ACTRUST_ERR_TIMEOUT);
            }
            continue;
        }
        if (errno != EINTR) {
            return NET_ERR(ACTRUST_ERR_IO);
        }
        if (deadline->immediate) {
            return NET_ERR(ACTRUST_ERR_TIMEOUT);
        }
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

    net_deadline_t deadline;
    actrust_err_t  err = net_deadline_start(timeout_ms, &deadline);
    if (ACTRUST_IS_ERR(err)) {
        return err;
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

    if (errno != EINPROGRESS && errno != EINTR) {
        (void) net_set_nonblock(fd, false);
        return NET_ERR(ACTRUST_ERR_IO);
    }

    err = actrust_net_poll_wait(fd, false, &deadline);

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

    net_deadline_t deadline;
    actrust_err_t  err = net_deadline_start(timeout_ms, &deadline);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    size_t  io_len = (len > (size_t) SSIZE_MAX) ? (size_t) SSIZE_MAX : len;
    ssize_t n;
    for (;;) {
        err = actrust_net_poll_wait(fd, false, &deadline);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
        n = send(fd, buf, io_len, MSG_NOSIGNAL | MSG_DONTWAIT);

        if (n >= 0) {
            *sent_len = (size_t) n;
            return ACTRUST_OK;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            if (deadline.immediate) {
                return NET_ERR(ACTRUST_ERR_TIMEOUT);
            }
            continue;
        }
        return NET_ERR(ACTRUST_ERR_IO);
    }
}

actrust_err_t actrust_net_recv(actrust_net_t net, uint8_t *buf, size_t len,
                               uint32_t timeout_ms, size_t *recvd_len)
{
    if (net == NULL || buf == NULL || recvd_len == NULL || len == 0) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    *recvd_len = 0;
    int fd     = ACTRUST_NET_TO_FD(net);

    net_deadline_t deadline;
    actrust_err_t  err = net_deadline_start(timeout_ms, &deadline);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    size_t  io_len = (len > (size_t) SSIZE_MAX) ? (size_t) SSIZE_MAX : len;
    ssize_t n;
    for (;;) {
        err = actrust_net_poll_wait(fd, true, &deadline);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
        n = recv(fd, buf, io_len, MSG_DONTWAIT);

        if (n >= 0) {
            *recvd_len = (size_t) n;
            return ACTRUST_OK;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            if (deadline.immediate) {
                return NET_ERR(ACTRUST_ERR_TIMEOUT);
            }
            continue;
        }
        return NET_ERR(ACTRUST_ERR_IO);
    }
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

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    net_deadline_t deadline;
    actrust_err_t  err = net_deadline_start(timeout_ms, &deadline);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    size_t  io_len = (len > (size_t) SSIZE_MAX) ? (size_t) SSIZE_MAX : len;
    ssize_t n;
    for (;;) {
        err = actrust_net_poll_wait(fd, false, &deadline);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
        n = sendto(fd, buf, io_len, MSG_NOSIGNAL | MSG_DONTWAIT,
                   (struct sockaddr *) &addr, sizeof(addr));

        if (n >= 0) {
            *sent_len = (size_t) n;
            return ACTRUST_OK;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            if (deadline.immediate) {
                return NET_ERR(ACTRUST_ERR_TIMEOUT);
            }
            continue;
        }
        return NET_ERR(ACTRUST_ERR_IO);
    }
}

actrust_err_t actrust_net_recvfrom(actrust_net_t net, char *ip, size_t ip_len,
                                   uint16_t *port, uint8_t *buf, size_t len,
                                   uint32_t timeout_ms, size_t *recvd_len)
{
    if (net == NULL || buf == NULL || recvd_len == NULL || len == 0) {
        return NET_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    *recvd_len = 0;
    if (ip != NULL && ip_len < INET_ADDRSTRLEN) {
        return NET_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    int            fd = ACTRUST_NET_TO_FD(net);
    net_deadline_t deadline;
    actrust_err_t  err = net_deadline_start(timeout_ms, &deadline);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    for (;;) {
        err = actrust_net_poll_wait(fd, true, &deadline);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }

        uint8_t            peek_byte;
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        struct iovec peek_iov = {
            .iov_base = &peek_byte,
            .iov_len  = sizeof(peek_byte),
        };
        struct msghdr peek_msg = {
            .msg_name       = &addr,
            .msg_namelen    = sizeof(addr),
            .msg_iov        = &peek_iov,
            .msg_iovlen     = 1u,
            .msg_control    = NULL,
            .msg_controllen = 0u,
            .msg_flags      = 0,
        };

        ssize_t datagram_len;
        datagram_len =
            recvmsg(fd, &peek_msg, MSG_PEEK | MSG_TRUNC | MSG_DONTWAIT);

        if (datagram_len >= 0) {
            if ((size_t) datagram_len > len) {
                return NET_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
            }

            socklen_t addrlen = sizeof(addr);
            ssize_t   copied  = recvfrom(fd, buf, len, MSG_DONTWAIT,
                                         (struct sockaddr *) &addr, &addrlen);
            if (copied < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                    if (deadline.immediate) {
                        return NET_ERR(ACTRUST_ERR_TIMEOUT);
                    }
                    continue;
                }
                return NET_ERR(ACTRUST_ERR_IO);
            }

            char local_ip[INET_ADDRSTRLEN];
            if (ip != NULL && inet_ntop(AF_INET, &addr.sin_addr, local_ip,
                                        sizeof(local_ip)) == NULL) {
                return NET_ERR(ACTRUST_ERR_IO);
            }
            if (ip != NULL) {
                memcpy(ip, local_ip, strlen(local_ip) + 1u);
            }
            if (port != NULL) {
                *port = ntohs(addr.sin_port);
            }
            *recvd_len = (size_t) copied;
            return ACTRUST_OK;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            if (deadline.immediate) {
                return NET_ERR(ACTRUST_ERR_TIMEOUT);
            }
            continue;
        }
        return NET_ERR(ACTRUST_ERR_IO);
    }
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

    net_deadline_t deadline;
    err = net_deadline_start(effective_timeout, &deadline);
    if (ACTRUST_IS_ERR(err)) {
        net_dns_resolver_release();
        return err;
    }

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

    pthread_condattr_t cond_attr;
    if (pthread_condattr_init(&cond_attr) != 0) {
        net_dns_resolver_release();
        net_dns_ctx_free(ctx);
        return NET_ERR(ACTRUST_ERR_IO);
    }
    int rc = pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
    if (rc == 0) {
        rc = pthread_cond_init(&ctx->cond, &cond_attr);
    }
    (void) pthread_condattr_destroy(&cond_attr);
    if (rc != 0) {
        net_dns_resolver_release();
        net_dns_ctx_free(ctx);
        return NET_ERR(ACTRUST_ERR_IO);
    }
    ctx->cond_ready = true;

    pthread_t thread;
    rc = pthread_create(&thread, NULL, net_dns_resolve_worker, ctx);
    if (rc != 0) {
        net_dns_resolver_release();
        net_dns_ctx_free(ctx);
        return NET_ERR(ACTRUST_ERR_IO);
    }

    err = net_dns_wait_complete(ctx, &deadline.expires);
    if (err != ACTRUST_OK) {
        actrust_err_t transfer_err = net_dns_transfer_to_worker(thread, ctx);
        if (transfer_err == ACTRUST_OK) {
            return err;
        }
        rc = pthread_join(thread, NULL);
        if (rc != 0) {
            return NET_ERR(ACTRUST_ERR_IO);
        }
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
