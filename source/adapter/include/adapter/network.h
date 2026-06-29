// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file adapter/network.h
 * @brief Platform network abstraction layer interface
 *
 * Provides a uniform TCP/UDP socket API that platform adapters implement.
 * All I/O operations support configurable timeouts and partial transfer
 * reporting.  DNS resolution is a standalone utility.
 *
 * Typical TCP usage:
 * @code{.c}
 *   actrust_net_t sock;
 *   actrust_net_open(&sock, ACTRUST_NET_TCP);
 *   actrust_net_connect(sock, "192.168.1.1", 8883, 5000);
 *   actrust_net_send(sock, data, len, 3000, &sent);
 *   actrust_net_recv(sock, buf, sizeof(buf), 3000, &got);
 *   actrust_net_close(sock);
 * @endcode
 *
 * Typical UDP usage:
 * @code{.c}
 *   actrust_net_t sock;
 *   actrust_net_open(&sock, ACTRUST_NET_UDP);
 *   actrust_net_sendto(sock, "1.2.3.4", 123, pkt, pkt_len, 3000, &sent);
 *   actrust_net_recvfrom(sock, ip, sizeof(ip), &port, buf, sizeof(buf), 3000,
 * &got); actrust_net_close(sock);
 * @endcode
 */

#ifndef ACTRUST_NETWORK_H
#define ACTRUST_NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif

/* C standard */
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_errno.h"

/** @brief Network socket handle (opaque pointer) */
typedef void *actrust_net_t;

/** @brief Socket protocol type */
typedef enum { ACTRUST_NET_TCP = 1, ACTRUST_NET_UDP = 2 } actrust_net_type_t;

/**
 * @defgroup adapter_net_lifecycle Lifecycle
 * @{
 */

/**
 * @brief Create a network socket
 *
 * @param[out] out   Receives socket handle on success
 * @param[in]  type  Protocol type (TCP or UDP)
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p out is NULL or @p type
 *         is unknown
 * @return @c ACTRUST_ERR_NO_MEM if insufficient memory
 * @return @c ACTRUST_ERR_IO if the underlying socket cannot be created
 */
actrust_err_t actrust_net_open(actrust_net_t *out, actrust_net_type_t type);

/**
 * @brief Close a socket and release all associated resources
 *
 * @param[in] net  Socket handle
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p net is NULL
 */
actrust_err_t actrust_net_close(actrust_net_t net);

/** @} */

/**
 * @defgroup adapter_net_tcp TCP
 * @{
 */

/**
 * @brief Connect a TCP socket to a remote host
 *
 * @param[in] net         Socket handle (must be TCP)
 * @param[in] remote_ip   Remote IPv4 address string (e.g. "192.168.1.1").
 *                        Must be a resolved IP, not a hostname; use
 *                        @ref actrust_net_dns_resolve first if needed.
 * @param[in] port        Remote port number
 * @param[in] timeout_ms  Connection timeout in milliseconds;
 *                        0 = non-blocking attempt
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if any argument is invalid
 * @return @c ACTRUST_ERR_TIMEOUT if the connection times out
 * @return @c ACTRUST_ERR_IO on network failure
 */
actrust_err_t actrust_net_connect(actrust_net_t net, const char *remote_ip,
                                  uint16_t port, uint32_t timeout_ms);

/**
 * @brief Send data over a connected TCP socket
 *
 * Performs a single send operation.  On success @p *sent_len contains
 * the number of bytes actually sent, which may be less than @p len
 * (partial write).  The caller is responsible for retrying the remainder.
 *
 * @param[in]  net         Socket handle (must be connected TCP)
 * @param[in]  buf         Data to send (must not be NULL)
 * @param[in]  len         Number of bytes to send
 * @param[in]  timeout_ms  Send timeout in milliseconds; 0 = non-blocking
 * @param[out] sent_len    Receives the number of bytes actually sent
 *
 * @return @c ACTRUST_OK on success (even if partial)
 * @return @c ACTRUST_ERR_INVALID_ARG if any argument is invalid
 * @return @c ACTRUST_ERR_TIMEOUT if the operation times out
 * @return @c ACTRUST_ERR_IO on network failure
 */
actrust_err_t actrust_net_send(actrust_net_t net, const uint8_t *buf,
                               size_t len, uint32_t timeout_ms,
                               size_t *sent_len);

/**
 * @brief Receive data from a connected TCP socket
 *
 * Performs a single receive operation.  On success @p *recvd_len
 * contains the number of bytes actually received, which may be less
 * than @p len.  A return of ACTRUST_OK with @p *recvd_len == 0 indicates
 * the peer has closed the connection.
 *
 * @param[in]  net         Socket handle (must be connected TCP)
 * @param[out] buf         Destination buffer
 * @param[in]  len         Buffer capacity in bytes
 * @param[in]  timeout_ms  Receive timeout in milliseconds; 0 = non-blocking
 * @param[out] recvd_len   Receives the number of bytes actually read
 *
 * @return @c ACTRUST_OK on success (even if partial)
 * @return @c ACTRUST_ERR_INVALID_ARG if any argument is invalid
 * @return @c ACTRUST_ERR_TIMEOUT if the operation times out
 * @return @c ACTRUST_ERR_IO on network failure
 */
actrust_err_t actrust_net_recv(actrust_net_t net, uint8_t *buf, size_t len,
                               uint32_t timeout_ms, size_t *recvd_len);

/** @} */

/**
 * @defgroup adapter_net_udp UDP
 * @{
 */

/**
 * @brief Bind a UDP socket to a local port
 *
 * Optional on platforms where @ref actrust_net_sendto automatically assigns
 * an ephemeral port.  Required on some embedded RTOS stacks (e.g. lwIP
 * raw mode) for receiving replies.
 *
 * @param[in] net   Socket handle (must be UDP)
 * @param[in] port  Local port number; 0 = let the OS assign
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if @p net is NULL
 * @return @c ACTRUST_ERR_IO on failure
 */
actrust_err_t actrust_net_bind(actrust_net_t net, uint16_t port);

/**
 * @brief Send a UDP datagram to a specified destination
 *
 * @param[in]  net         Socket handle (must be UDP)
 * @param[in]  ip          Destination IPv4 address string
 * @param[in]  port        Destination port
 * @param[in]  buf         Data to send (must not be NULL)
 * @param[in]  len         Number of bytes to send
 * @param[in]  timeout_ms  Send timeout in milliseconds; 0 = non-blocking
 * @param[out] sent_len    Receives the number of bytes actually sent
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if any argument is invalid
 * @return @c ACTRUST_ERR_TIMEOUT if the operation times out
 * @return @c ACTRUST_ERR_IO on network failure
 */
actrust_err_t actrust_net_sendto(actrust_net_t net, const char *ip,
                                 uint16_t port, const uint8_t *buf, size_t len,
                                 uint32_t timeout_ms, size_t *sent_len);

/**
 * @brief Receive a UDP datagram and record the sender address
 *
 * @param[in]  net         Socket handle (must be UDP)
 * @param[out] ip          Buffer to receive sender IPv4 address string
 *                         (NUL-terminated).  Recommend at least 16 bytes.
 * @param[in]  ip_len      Capacity of @p ip buffer in bytes
 * @param[out] port        Receives sender port number
 * @param[out] buf         Destination buffer for datagram payload
 * @param[in]  len         Capacity of @p buf in bytes
 * @param[in]  timeout_ms  Receive timeout in milliseconds; 0 = non-blocking
 * @param[out] recvd_len   Receives the number of payload bytes actually read
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if any argument is invalid
 * @return @c ACTRUST_ERR_TIMEOUT if the operation times out
 * @return @c ACTRUST_ERR_IO on network failure
 */
actrust_err_t actrust_net_recvfrom(actrust_net_t net, char *ip, size_t ip_len,
                                   uint16_t *port, uint8_t *buf, size_t len,
                                   uint32_t timeout_ms, size_t *recvd_len);

/** @} */

/**
 * @defgroup adapter_net_dns DNS
 * @{
 */

/**
 * @brief Resolve a hostname to an IPv4 address string
 *
 * @param[in]  host        Hostname to resolve (NUL-terminated)
 * @param[out] ip          Buffer to receive the resolved IPv4 address
 *                         (NUL-terminated).  Recommend at least 16 bytes.
 * @param[in]  ip_len      Capacity of @p ip buffer in bytes
 * @param[in]  timeout_ms  Resolution timeout in milliseconds; 0 = default
 *
 * @return @c ACTRUST_OK on success
 * @return @c ACTRUST_ERR_INVALID_ARG if any argument is invalid
 * @return @c ACTRUST_ERR_BUSY if another resolver worker is still active
 * @return @c ACTRUST_ERR_TIMEOUT if resolution times out
 * @return @c ACTRUST_ERR_IO if resolution fails
 */
actrust_err_t actrust_net_dns_resolve(const char *host, char *ip, size_t ip_len,
                                      uint32_t timeout_ms);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_NETWORK_H */
