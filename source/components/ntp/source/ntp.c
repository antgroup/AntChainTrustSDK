// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file ntp.c
 * @brief NTP time synchronisation component implementation
 *
 * Uses the coreSNTP serializer API (Sntp_SerializeRequest /
 * Sntp_DeserializeResponse) to build and parse SNTP packets, combined with the
 * AntChainTrustSDK network and system adapter layers for transport and
 * timekeeping.
 *
 * A single call to actrust_ntp_sync() performs the full NTP exchange:
 *
 *     DNS resolve → open UDP → send request → receive response → parse → close
 *
 * The resulting clock offset is stored in the opaque actrust_ntp_t instance;
 * the system clock is never modified.
 *
 * @note Only the low-level coreSNTP serializer is used — no callbacks, no
 *       global state.  This keeps the implementation deterministic and easy
 *       to unit-test.
 */

/* C standard */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Third-party */
#include "core_sntp_serializer.h"

/* Common */
#include "common/common.h"

/* Project */
#include "actrust_config.h"

/* NTP */
#include "ntp/ntp.h"

/* Component */
#include "log/log.h"

/* Adapter */
#include "adapter/network.h"
#include "adapter/system.h"

/* ========================================================================
 * Internal Macros
 * ======================================================================== */

/** @brief Convenience macro to build NTP-module errors */
#define NTP_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_NTP, (reason))

/** @brief IANA well-known port for NTP (RFC 5905). */
#define NTP_DEFAULT_PORT (123u)

#ifndef CONFIG_ACTRUST_NTP_MAX_OFFSET_MS
#define CONFIG_ACTRUST_NTP_MAX_OFFSET_MS (86400000)
#endif

/* ========================================================================
 * Private Types
 * ======================================================================== */

/**
 * @brief Internal NTP context — hidden behind the opaque @c actrust_ntp_t
 * handle.
 */
struct actrust_ntp_s {
    char     server[64]; /**< NTP server hostname or IP */
    uint16_t port;       /**< NTP server port */
    uint32_t timeout_ms; /**< Single sync timeout in ms */

    bool            synced; /**< true after at least one successful sync */
    int64_t         last_offset_ms; /**< corrected_utc = wall_time + offset */
    actrust_mutex_t lock;           /**< Mutex for thread safety */
};

/* ========================================================================
 * Private Helpers
 * ======================================================================== */

/**
 * @brief Convert UNIX wall-clock milliseconds to an SNTP timestamp
 *
 * SNTP epoch is 1 Jan 1900; UNIX epoch is 1 Jan 1970.
 * The 70-year offset is added via @c SNTP_TIME_AT_UNIX_EPOCH_SECS.
 * Uint32 overflow after Feb 2036 is intentional (enters SNTP era 1);
 * the coreSNTP library handles cross-era arithmetic internally.
 */
static SntpTimestamp_t wall_ms_to_sntp(uint64_t wall_ms)
{
    uint32_t unix_secs = (uint32_t) (wall_ms / 1000u);
    uint32_t frac_us   = (uint32_t) (wall_ms % 1000u) * 1000u;

    SntpTimestamp_t ts;
    ts.seconds   = unix_secs + SNTP_TIME_AT_UNIX_EPOCH_SECS;
    ts.fractions = frac_us * SNTP_FRACTION_VALUE_PER_MICROSECOND;
    return ts;
}

/**
 * @brief Produce a non-cryptographic random number for the SNTP request
 *
 * Combines monotonic and wall clocks with a fast integer hash
 * (splitmix32 finaliser).  Sufficient for the anti-replay "transmit
 * timestamp" randomisation required by RFC 4330 Section 3; not suitable for
 * security purposes.
 */
static uint32_t ntp_random(void)
{
    uint64_t m = actrust_monotonic_ms();
    uint64_t w = actrust_wall_time_ms();
    uint32_t r = (uint32_t) (m ^ (w >> 16) ^ (w << 16));

    r ^= r >> 16;
    r *= 0x45D9F3Bu;
    r ^= r >> 16;
    return r;
}

static bool ntp_offset_is_acceptable(int64_t offset_ms)
{
    if (CONFIG_ACTRUST_NTP_MAX_OFFSET_MS == 0) {
        return true;
    }

    const int64_t max_offset_ms = (int64_t) CONFIG_ACTRUST_NTP_MAX_OFFSET_MS;
    return offset_ms >= -max_offset_ms && offset_ms <= max_offset_ms;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

actrust_err_t actrust_ntp_init(actrust_ntp_t *out)
{
    if (out == NULL) {
        return NTP_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_ntp_t ntp = ACTRUST_CALLOC(1, sizeof(*ntp));
    if (ntp == NULL) {
        return NTP_ERR(ACTRUST_ERR_NO_MEM);
    }
    snprintf(ntp->server, sizeof(ntp->server), "%s", CONFIG_ACTRUST_NTP_SERVER);
    ntp->port       = (uint16_t) NTP_DEFAULT_PORT;
    ntp->timeout_ms = (uint32_t) CONFIG_ACTRUST_NTP_TIMEOUT_MS;
    if (actrust_mutex_create(&ntp->lock) != ACTRUST_OK) {
        LOG_ERROR("ntp mutex create failed");
        ACTRUST_FREE(ntp);
        return NTP_ERR(ACTRUST_ERR_HW_FAILURE);
    }
    *out = ntp;
    LOG_INFO("ntp initialized: server=%s port=%u timeout_ms=%lu", ntp->server,
             (unsigned int) ntp->port, (unsigned long) ntp->timeout_ms);
    return ACTRUST_OK;
}

actrust_err_t actrust_ntp_deinit(actrust_ntp_t handle)
{
    if (handle == NULL) {
        return NTP_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (actrust_mutex_destroy(handle->lock) != ACTRUST_OK) {
        return NTP_ERR(ACTRUST_ERR_HW_FAILURE);
    }
    LOG_INFO("ntp deinitialized: server=%s", handle->server);
    ACTRUST_FREE(handle);
    return ACTRUST_OK;
}

actrust_err_t actrust_ntp_sync(actrust_ntp_t handle)
{
    if (handle == NULL) {
        return NTP_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    /*
     * ---- Step 1: Resolve server hostname to IPv4 address ----
     */
    LOG_INFO("ntp sync started: server=%s port=%u", handle->server,
             (unsigned int) handle->port);

    char          ip[16];
    actrust_net_t sock = NULL;

    actrust_err_t err = actrust_net_dns_resolve(handle->server, ip, sizeof(ip),
                                                handle->timeout_ms);
    if (err != ACTRUST_OK) {
        LOG_WARN("ntp dns resolve failed: server=%s err=0x%08" PRIx32,
                 handle->server, err);
        goto out;
    }

    LOG_DEBUG("ntp resolved: server=%s ip=%s", handle->server, ip);

    /*
     * ---- Step 2: Open a UDP socket ----
     */
    err = actrust_net_open(&sock, ACTRUST_NET_UDP);
    if (err != ACTRUST_OK) {
        LOG_WARN("ntp socket open failed: err=0x%08" PRIx32, err);
        goto out;
    }

    /*
     * ---- Step 3: Build SNTP request ----
     */
    uint8_t         pkt[SNTP_PACKET_BASE_SIZE];
    SntpTimestamp_t t1 = wall_ms_to_sntp(actrust_wall_time_ms());

    if (Sntp_SerializeRequest(&t1, ntp_random(), pkt, sizeof(pkt)) !=
        SntpSuccess) {
        err = NTP_ERR(ACTRUST_ERR_IO);
        LOG_WARN("ntp request serialization failed");
        goto out;
    }

    /*
     * ---- Step 4: Send request to NTP server ----
     */
    size_t io_len = 0;
    err           = actrust_net_sendto(sock, ip, handle->port, pkt, sizeof(pkt),
                                       handle->timeout_ms, &io_len);
    if (err != ACTRUST_OK) {
        LOG_WARN("ntp send failed: ip=%s err=0x%08" PRIx32, ip, err);
        goto out;
    }

    /*
     * ---- Step 5: Receive server response ----
     */
    char     sender_ip[16];
    uint16_t sender_port = 0u;
    err = actrust_net_recvfrom(sock, sender_ip, sizeof(sender_ip), &sender_port,
                               pkt, sizeof(pkt), handle->timeout_ms, &io_len);
    if (err != ACTRUST_OK) {
        LOG_WARN("ntp receive failed: err=0x%08" PRIx32, err);
        goto out;
    }
    if (strcmp(sender_ip, ip) != 0 || sender_port != handle->port) {
        err = NTP_ERR(ACTRUST_ERR_IO);
        LOG_WARN("ntp response rejected: expected=%s:%u got=%s:%u", ip,
                 (unsigned int) handle->port, sender_ip,
                 (unsigned int) sender_port);
        goto out;
    }

    /*
     * ---- Step 6: Deserialize response and calculate clock offset ----
     */
    SntpTimestamp_t    t4   = wall_ms_to_sntp(actrust_wall_time_ms());
    SntpResponseData_t resp = { 0 };

    if (Sntp_DeserializeResponse(&t1, &t4, pkt, io_len, &resp) != SntpSuccess) {
        err = NTP_ERR(ACTRUST_ERR_IO);
        LOG_WARN("ntp response parse failed: bytes=%zu", io_len);
        goto out;
    }

    if (resp.leapSecondType == AlarmServerNotSynchronized) {
        err = NTP_ERR(ACTRUST_ERR_BAD_STATE);
        LOG_WARN("ntp server reports unsynchronized time");
        goto out;
    }

    if (!ntp_offset_is_acceptable(resp.clockOffsetMs)) {
        err = NTP_ERR(ACTRUST_ERR_BAD_STATE);
        LOG_WARN("ntp offset rejected: offset_ms=%" PRId64 " max_ms=%d",
                 resp.clockOffsetMs, CONFIG_ACTRUST_NTP_MAX_OFFSET_MS);
        goto out;
    }

    /*
     * ---- Step 7: Commit results to instance ----
     */
    if (actrust_mutex_lock(handle->lock) != ACTRUST_OK) {
        err = NTP_ERR(ACTRUST_ERR_HW_FAILURE);
        goto out;
    }
    handle->synced         = true;
    handle->last_offset_ms = resp.clockOffsetMs;
    if (actrust_mutex_unlock(handle->lock) != ACTRUST_OK) {
        err = NTP_ERR(ACTRUST_ERR_HW_FAILURE);
        goto out;
    }
    err = ACTRUST_OK;
    LOG_INFO("ntp sync complete: offset_ms=%" PRId64, resp.clockOffsetMs);

out:
    if (sock != NULL) {
        (void) actrust_net_close(sock);
    }
    return err;
}

actrust_err_t actrust_ntp_get_last_offset_ms(actrust_ntp_t handle,
                                             int64_t      *out_offset_ms)
{
    if (handle == NULL || out_offset_ms == NULL) {
        return NTP_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (actrust_mutex_lock(handle->lock) != ACTRUST_OK) {
        return NTP_ERR(ACTRUST_ERR_HW_FAILURE);
    }
    if (!handle->synced) {
        (void) actrust_mutex_unlock(handle->lock);
        return NTP_ERR(ACTRUST_ERR_NOT_READY);
    }
    *out_offset_ms = handle->last_offset_ms;
    if (actrust_mutex_unlock(handle->lock) != ACTRUST_OK) {
        return NTP_ERR(ACTRUST_ERR_HW_FAILURE);
    }
    return ACTRUST_OK;
}

actrust_err_t actrust_ntp_now_ms(actrust_ntp_t handle, uint64_t *out_now_ms)
{
    if (handle == NULL || out_now_ms == NULL) {
        return NTP_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (actrust_mutex_lock(handle->lock) != ACTRUST_OK) {
        return NTP_ERR(ACTRUST_ERR_HW_FAILURE);
    }
    if (!handle->synced) {
        (void) actrust_mutex_unlock(handle->lock);
        return NTP_ERR(ACTRUST_ERR_NOT_READY);
    }
    *out_now_ms =
        (uint64_t) ((int64_t) actrust_wall_time_ms() + handle->last_offset_ms);
    if (actrust_mutex_unlock(handle->lock) != ACTRUST_OK) {
        return NTP_ERR(ACTRUST_ERR_HW_FAILURE);
    }
    return ACTRUST_OK;
}
