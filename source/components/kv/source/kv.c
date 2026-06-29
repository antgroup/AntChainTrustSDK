// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file kv.c
 * @brief KV storage component — implementation
 *
 * Storage layout (all offsets relative to the start of a namespace region):
 *
 *   Offset 0
 *   +-----------------------+
 *   |     kv_header_t       |  magic · version · record_count
 *   +-----------------------+
 *   |   kv_record_t  [0]    |  used · crc32 · key_len · value_len · key · value
 *   +-----------------------+
 *   |   kv_record_t  [1]    |
 *   +-----------------------+
 *   |        ...             |
 *   +-----------------------+
 *   | kv_record_t  [N - 1]  |  N = CONFIG_ACTRUST_KV_MAX_RECORDS
 *   +-----------------------+
 *
 * All multi-byte fields use host byte order.  Cross-platform data exchange
 * requires an external serialisation layer.
 */

/* C standard */
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Common */
#include "common/common.h"

/* Project */
#include "actrust_errno.h"

/* KV */
#include "kv/kv.h"

/* Component */
#include "log/log.h"

/* Adapter */
#include "adapter/storage.h"
#include "adapter/system.h"

/* ========================================================================
 * Internal Macros
 * ======================================================================== */

/** @brief Build a KV-module-specific error code. */
#define KV_ERR(reason) ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_KV, (reason))

#if CONFIG_ACTRUST_KV_MAX_KEY_LEN > UINT8_MAX
#error "CONFIG_ACTRUST_KV_MAX_KEY_LEN must fit in kv_record_head_t.key_len"
#endif

#if CONFIG_ACTRUST_KV_MAX_VALUE_LEN > UINT32_MAX
#error "CONFIG_ACTRUST_KV_MAX_VALUE_LEN must fit in kv_record_head_t.value_len"
#endif

/* ========================================================================
 * Private Types and Constants
 * ======================================================================== */

/** @brief Marker: record slot is free. */
#define KV_RECORD_NOT_USED 0x00u

/** @brief Marker: record slot contains valid data. */
#define KV_RECORD_IN_USE 0x01u

/** @brief Magic number written to the KV on-disk header ("ACTR"). */
#define KV_HEADER_MAGIC 0x41435452u

/** @brief KV on-disk format version. */
#define KV_HEADER_VERSION 0x0001u

/**
 * @brief KV instance handle (opaque to callers, heap-allocated per open).
 */
struct actrust_kv {
    /** Namespace string, NUL-terminated. */
    char ns[CONFIG_ACTRUST_KV_NAMESPACE_MAX_LEN];
    /** Storage identifier for this namespace. */
    actrust_storage_id_t id;
    /** Opened storage handle. */
    actrust_storage_t st;
    /** Per-instance mutex for thread safety. */
    actrust_mutex_t lock;
};

/**
 * @brief Compile-time namespace-to-storage-ID mapping entry.
 */
typedef struct {
    const char          *ns; /**< Namespace string literal. */
    actrust_storage_id_t id; /**< Corresponding storage identifier. */
} kv_ns_map_t;

/**
 * @brief On-disk header stored at offset 0 of every KV storage region.
 *
 * @note All fields use fixed-width types to guarantee consistent layout
 *       across 32-bit and 64-bit targets compiled with the same toolchain.
 */
typedef struct {
    uint32_t magic;        /**< Magic number (@c KV_HEADER_MAGIC). */
    uint16_t version;      /**< Format version (@c KV_HEADER_VERSION). */
    uint16_t reserved;     /**< Reserved for alignment / future use. */
    uint32_t record_count; /**< Number of records currently in use. */
} kv_header_t;

/**
 * @brief On-disk record stored sequentially after the header.
 *
 * @note @c key_len is stored as @c uint8_t, therefore
 *       @c CONFIG_ACTRUST_KV_MAX_KEY_LEN must not exceed 255.
 */
/**
 * @brief Record header.
 */
typedef struct {
    uint8_t  used;     /**< @ref KV_RECORD_NOT_USED or @ref KV_RECORD_IN_USE. */
    uint8_t  key_len;  /**< Actual key length in bytes (1 .. 255). */
    uint16_t reserved; /**< Reserved for alignment / future use. */
    uint32_t crc32;    /**< CRC-32 of value payload (0 when CRC is disabled). */
    uint32_t value_len; /**< Actual value length in bytes. */
    uint8_t  key[CONFIG_ACTRUST_KV_MAX_KEY_LEN]; /**< Key data (NOT
                                                    NUL-terminated). */
} kv_record_head_t;

/**
 * @brief On-disk record stored sequentially after the header.
 */
typedef struct {
    kv_record_head_t head;                          /**< Key and metadata. */
    uint8_t value[CONFIG_ACTRUST_KV_MAX_VALUE_LEN]; /**< Value payload. */
} kv_record_t;

/** @brief Static namespace map populated via Kconfig. */
static const kv_ns_map_t kv_ns_map[] = {
#if CONFIG_ACTRUST_KV_NS_MAP_COUNT > 0
    { CONFIG_ACTRUST_KV_NAMESPACE_0,
      (actrust_storage_id_t) CONFIG_ACTRUST_KV_NAMESPACE_STORAGE_ID_0 },
#endif
#if CONFIG_ACTRUST_KV_NS_MAP_COUNT > 1
    { CONFIG_ACTRUST_KV_NAMESPACE_1,
      (actrust_storage_id_t) CONFIG_ACTRUST_KV_NAMESPACE_STORAGE_ID_1 },
#endif
#if CONFIG_ACTRUST_KV_NS_MAP_COUNT > 2
    { CONFIG_ACTRUST_KV_NAMESPACE_2,
      (actrust_storage_id_t) CONFIG_ACTRUST_KV_NAMESPACE_STORAGE_ID_2 },
#endif
#if CONFIG_ACTRUST_KV_NS_MAP_COUNT > 3
    { CONFIG_ACTRUST_KV_NAMESPACE_3,
      (actrust_storage_id_t) CONFIG_ACTRUST_KV_NAMESPACE_STORAGE_ID_3 },
#endif
    { "", 0 },
};

/** @brief Number of entries actually compiled into @c kv_ns_map (excludes
 * sentinel). */
#define KV_NS_MAP_SIZE (sizeof(kv_ns_map) / sizeof(kv_ns_map[0]) - 1u)

/* ========================================================================
 * Private Helper Functions — Storage I/O Wrappers
 * ======================================================================== */

/**
 * @brief Compute the byte offset of a record by its index.
 *
 * @param[in] index  Zero-based record index.
 * @return Byte offset from the beginning of the storage region.
 */
static uint32_t kv_record_offset(size_t index)
{
    return (uint32_t) (sizeof(kv_header_t) + sizeof(kv_record_t) * index);
}

/**
 * @brief Read the on-disk header.
 *
 * @param[in]  st   Opened storage handle.
 * @param[out] hdr  Receives the header on success.
 * @return ACTRUST_OK on success, or a storage-layer error code.
 */
static actrust_err_t kv_read_header(actrust_storage_t st, kv_header_t *hdr)
{
    return actrust_storage_read(st, 0u, (uint8_t *) hdr, sizeof(*hdr));
}

/**
 * @brief Write the on-disk header.
 *
 * @param[in] st   Opened storage handle.
 * @param[in] hdr  Header to persist.
 * @return ACTRUST_OK on success, or a storage-layer error code.
 */
static actrust_err_t kv_write_header(actrust_storage_t  st,
                                     const kv_header_t *hdr)
{
    return actrust_storage_write(st, 0u, (const uint8_t *) hdr, sizeof(*hdr));
}

/**
 * @brief Read a single record from storage.
 *
 * @param[in]  st     Opened storage handle.
 * @param[in]  index  Record index (0-based).
 * @param[out] rec    Receives the record on success.
 * @return ACTRUST_OK on success, or a storage-layer error code.
 */
static actrust_err_t kv_read_record(actrust_storage_t st, size_t index,
                                    kv_record_t *rec)
{
    return actrust_storage_read(st, kv_record_offset(index), (uint8_t *) rec,
                                sizeof(*rec));
}

/**
 * @brief Read only the header portion of a record (no value payload).
 *
 * @param[in]  st     Opened storage handle.
 * @param[in]  index  Record index (0-based).
 * @param[out] head   Receives the record header on success.
 * @return ACTRUST_OK on success, or a storage-layer error code.
 */
static actrust_err_t kv_read_record_head(actrust_storage_t st, size_t index,
                                         kv_record_head_t *head)
{
    return actrust_storage_read(st, kv_record_offset(index), (uint8_t *) head,
                                sizeof(*head));
}

/**
 * @brief Write a single record to storage.
 *
 * @param[in] st     Opened storage handle.
 * @param[in] index  Record index (0-based).
 * @param[in] rec    Record to persist.
 * @return ACTRUST_OK on success, or a storage-layer error code.
 */
static actrust_err_t kv_write_record(actrust_storage_t st, size_t index,
                                     const kv_record_t *rec)
{
    return actrust_storage_write(st, kv_record_offset(index),
                                 (const uint8_t *) rec, sizeof(*rec));
}

/* ========================================================================
 * Private Helper Functions — Locking
 * ======================================================================== */

/**
 * @brief Acquire the per-instance mutex.
 *
 * @param[in] kv  KV handle (must not be NULL and must own a valid lock).
 * @return ACTRUST_OK on success, or a KV error code.
 */
static actrust_err_t kv_lock(actrust_kv_t kv)
{
    if (kv == NULL || kv->lock == NULL) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return actrust_mutex_lock(kv->lock);
}

/**
 * @brief Release the per-instance mutex.
 *
 * @param[in] kv  KV handle (must not be NULL and must own a valid lock).
 * @return ACTRUST_OK on success, or a KV error code.
 */
static actrust_err_t kv_unlock(actrust_kv_t kv)
{
    if (kv == NULL || kv->lock == NULL) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return actrust_mutex_unlock(kv->lock);
}

/* ========================================================================
 * Private Helper Functions — CRC
 * ======================================================================== */

/**
 * @brief Compute CRC-32 (ISO 3309 / ITU-T V.42) over a byte buffer.
 *
 * Uses the standard polynomial @c 0xEDB88320 (bit-reversed representation),
 * the same algorithm used by zlib, PNG, and Ethernet FCS.
 *
 * @param[in] data  Pointer to input bytes (may be NULL when @p len is 0).
 * @param[in] len   Number of bytes to process.
 * @return Computed CRC-32 value.
 */
static uint32_t kv_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }

    return ~crc;
}

/**
 * @brief Validate persistent record metadata before using bounded arrays.
 */
static bool kv_record_head_is_valid(const kv_record_head_t *head)
{
    if (head == NULL) {
        return false;
    }

    if (head->used != KV_RECORD_IN_USE) {
        return head->used == KV_RECORD_NOT_USED;
    }

    return head->key_len > 0u &&
           head->key_len <= (uint8_t) CONFIG_ACTRUST_KV_MAX_KEY_LEN &&
           head->value_len <= (uint32_t) CONFIG_ACTRUST_KV_MAX_VALUE_LEN;
}

/* ========================================================================
 * Private Helper Functions — Namespace Lookup
 * ======================================================================== */

/**
 * @brief Resolve a namespace string to its storage identifier.
 *
 * Searches the compile-time @ref kv_ns_map for a matching entry.
 *
 * @param[in]  ns      Namespace string (not necessarily NUL-terminated).
 * @param[in]  len     Length of @p ns in bytes.
 * @param[out] out_id  Receives the storage identifier on success.
 * @return @c true if a match was found, @c false otherwise.
 */
static bool kv_lookup_storage_id(const char *ns, size_t len,
                                 actrust_storage_id_t *out_id)
{
    if (ns == NULL || len == 0u || out_id == NULL) {
        return false;
    }

    size_t count = (size_t) CONFIG_ACTRUST_KV_NS_MAP_COUNT;
    if (count > KV_NS_MAP_SIZE) {
        count = KV_NS_MAP_SIZE;
    }

    for (size_t i = 0; i < count; ++i) {
        const char *mapped = kv_ns_map[i].ns;
        if (mapped == NULL || mapped[0] == '\0') {
            continue;
        }
        if (strlen(mapped) == len && memcmp(mapped, ns, len) == 0) {
            *out_id = kv_ns_map[i].id;
            return true;
        }
    }

    return false;
}

/* ========================================================================
 * Private Helper Functions — Record Search
 * ======================================================================== */

/**
 * @brief Search for an existing record that matches @p key.
 *
 * Performs a linear scan over all record slots.  The caller must hold the
 * per-instance lock.
 *
 * @param[in]  kv         KV handle (must hold lock).
 * @param[in]  key        Key bytes to search for.
 * @param[in]  key_len    Key length in bytes.
 * @param[out] out_index  Receives the matching record index.  May be NULL.
 * @param[out] out_rec    Receives the matching record contents.  May be NULL.
 * @return @c true if a matching record was found, @c false otherwise.
 */
static bool kv_find_record(actrust_kv_t kv, const char *key, size_t key_len,
                           size_t *out_index, kv_record_t *out_rec)
{
    for (size_t i = 0; i < CONFIG_ACTRUST_KV_MAX_RECORDS; ++i) {
        kv_record_head_t head;

        if (kv_read_record_head(kv->st, i, &head) != ACTRUST_OK) {
            continue;
        }
        if (!kv_record_head_is_valid(&head)) {
            continue;
        }
        if (head.used != KV_RECORD_IN_USE) {
            continue;
        }
        if (head.key_len == (uint8_t) key_len &&
            memcmp(head.key, key, key_len) == 0) {
            if (out_index != NULL) {
                *out_index = i;
            }
            if (out_rec != NULL) {
                if (kv_read_record(kv->st, i, out_rec) != ACTRUST_OK) {
                    return false;
                }
            }
            return true;
        }
    }

    return false;
}

/**
 * @brief Locate the first unused record slot.
 *
 * @param[in]  kv         KV handle (must hold lock).
 * @param[out] out_index  Receives the index of the free slot.
 * @return @c true if a free slot was found, @c false if storage is full.
 */
static bool kv_find_empty_slot(actrust_kv_t kv, size_t *out_index)
{
    for (size_t i = 0; i < CONFIG_ACTRUST_KV_MAX_RECORDS; ++i) {
        kv_record_head_t head;

        if (kv_read_record_head(kv->st, i, &head) != ACTRUST_OK) {
            continue;
        }
        if (head.used == KV_RECORD_NOT_USED) {
            *out_index = i;
            return true;
        }
    }

    return false;
}

/* ========================================================================
 * Private Helper Functions — Storage Formatting
 * ======================================================================== */

/**
 * @brief Check whether a storage region contains a valid KV header.
 *
 * @param[in] st  Opened storage handle.
 * @return @c true if the header magic and version match the compiled-in
 *         configuration, @c false otherwise.
 */
static bool kv_storage_is_formatted(actrust_storage_t st)
{
    kv_header_t hdr;

    if (kv_read_header(st, &hdr) != ACTRUST_OK) {
        return false;
    }

    return hdr.magic == (uint32_t) KV_HEADER_MAGIC &&
           hdr.version == (uint16_t) KV_HEADER_VERSION;
}

/**
 * @brief Format a storage region with an empty KV layout.
 *
 * Writes a fresh header followed by @c CONFIG_ACTRUST_KV_MAX_RECORDS empty
 * record slots.  Any existing data in the region is overwritten.
 *
 * @param[in] st  Opened storage handle.
 * @return ACTRUST_OK on success, or a storage-layer error code.
 */
static actrust_err_t kv_storage_format(actrust_storage_t st)
{
    actrust_err_t err;

    /* Write header. */
    kv_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic        = (uint32_t) KV_HEADER_MAGIC;
    hdr.version      = (uint16_t) KV_HEADER_VERSION;
    hdr.record_count = 0u;

    err = kv_write_header(st, &hdr);
    if (err != ACTRUST_OK) {
        return err;
    }

    /* Write empty record slots. */
    kv_record_t rec;
    memset(&rec, 0, sizeof(rec));

    for (size_t i = 0; i < CONFIG_ACTRUST_KV_MAX_RECORDS; ++i) {
        err = kv_write_record(st, i, &rec);
        if (err != ACTRUST_OK) {
            return err;
        }
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

actrust_err_t actrust_kv_init(void)
{
    LOG_INFO("kv init requested: namespaces=%u",
             (unsigned int) CONFIG_ACTRUST_KV_NS_MAP_COUNT);

    size_t count = (size_t) CONFIG_ACTRUST_KV_NS_MAP_COUNT;
    if (count > KV_NS_MAP_SIZE) {
        count = KV_NS_MAP_SIZE;
    }

    for (size_t i = 0; i < count; ++i) {
        const char *mapped = kv_ns_map[i].ns;
        if (mapped == NULL || mapped[0] == '\0') {
            continue;
        }

        actrust_storage_t    st = NULL;
        actrust_storage_id_t id = kv_ns_map[i].id;

        actrust_err_t err = actrust_storage_open(&st, id);
        if (err != ACTRUST_OK) {
            LOG_ERROR("kv storage open failed: id=0x%08" PRIx32
                      " err=0x%08" PRIx32,
                      id, err);
            return err;
        }

        if (!kv_storage_is_formatted(st)) {
            LOG_WARN("kv storage not formatted, formatting: id=0x%08" PRIx32,
                     id);
            err = kv_storage_format(st);
            if (err != ACTRUST_OK) {
                LOG_ERROR("kv storage format failed: id=0x%08" PRIx32
                          " err=0x%08" PRIx32,
                          id, err);
                (void) actrust_storage_close(st);
                return err;
            }
        }

        err = actrust_storage_close(st);
        if (err != ACTRUST_OK) {
            LOG_ERROR("kv storage close failed: id=0x%08" PRIx32
                      " err=0x%08" PRIx32,
                      id, err);
            return err;
        }
    }

    LOG_INFO("kv initialized");
    return ACTRUST_OK;
}

actrust_err_t actrust_kv_open(const char *ns, size_t ns_len,
                              actrust_kv_t *out_kv)
{
    if (out_kv == NULL || ns == NULL || ns_len == 0u ||
        ns_len >= CONFIG_ACTRUST_KV_NAMESPACE_MAX_LEN) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_storage_id_t id;
    if (!kv_lookup_storage_id(ns, ns_len, &id)) {
        LOG_WARN("kv namespace not mapped: ns=%.*s", (int) ns_len, ns);
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_kv_t kv = (actrust_kv_t) ACTRUST_CALLOC(1, sizeof(*kv));
    if (kv == NULL) {
        LOG_ERROR("kv handle allocation failed: ns=%.*s", (int) ns_len, ns);
        return KV_ERR(ACTRUST_ERR_NO_MEM);
    }

    memcpy(kv->ns, ns, ns_len);
    kv->ns[ns_len] = '\0';
    kv->id         = id;

    actrust_err_t err = actrust_mutex_create(&kv->lock);
    if (err != ACTRUST_OK) {
        goto fail;
    }

    err = actrust_storage_open(&kv->st, id);
    if (err != ACTRUST_OK) {
        goto fail;
    }

    if (!kv_storage_is_formatted(kv->st)) {
        err = KV_ERR(ACTRUST_ERR_BAD_STATE);
        goto fail;
    }

    *out_kv = kv;
    LOG_DEBUG("kv opened: ns=%s id=0x%08" PRIx32, kv->ns, kv->id);
    return ACTRUST_OK;

fail:
    if (kv->st != NULL) {
        (void) actrust_storage_close(kv->st);
    }
    if (kv->lock != NULL) {
        (void) actrust_mutex_destroy(kv->lock);
    }
    LOG_ERROR("kv open failed: ns=%.*s err=0x%08" PRIx32, (int) ns_len, ns,
              err);
    ACTRUST_FREE(kv);
    return err;
}

actrust_err_t actrust_kv_close(actrust_kv_t kv)
{
    if (kv == NULL) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t first_err = ACTRUST_OK;
    actrust_err_t err;

    /* Acquire the lock before tearing down. */
    err = kv_lock(kv);
    if (first_err == ACTRUST_OK && err != ACTRUST_OK) {
        first_err = err;
    }

    err = actrust_storage_close(kv->st);
    if (first_err == ACTRUST_OK && err != ACTRUST_OK) {
        first_err = err;
    }

    /* Unlock + destroy. */
    (void) kv_unlock(kv);
    (void) actrust_mutex_destroy(kv->lock);

    LOG_DEBUG("kv closed: ns=%s err=0x%08" PRIx32, kv->ns, first_err);

    /* Scrub the handle before freeing to avoid use-after-free data leaks. */
    memset(kv, 0, sizeof(*kv));
    ACTRUST_FREE(kv);

    return first_err;
}

actrust_err_t actrust_kv_set(actrust_kv_t kv, const char *key, size_t key_len,
                             const void *value, size_t value_len)
{
    if (kv == NULL || kv->st == NULL || key == NULL || key_len == 0u ||
        key_len > CONFIG_ACTRUST_KV_MAX_KEY_LEN ||
        (value == NULL && value_len > 0u) ||
        value_len > CONFIG_ACTRUST_KV_MAX_VALUE_LEN) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = kv_lock(kv);
    if (err != ACTRUST_OK) {
        return err;
    }

    size_t index  = 0u;
    bool   is_new = false;

    /* Try to locate an existing record with the same key. */
    if (!kv_find_record(kv, key, key_len, &index, NULL)) {
        /* Key does not exist - need a free slot. */
        if (!kv_find_empty_slot(kv, &index)) {
            err = KV_ERR(ACTRUST_ERR_NO_RESOURCE);
            goto out;
        }
        is_new = true;
    }

    /* Build the new record. */
    kv_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.head.used      = KV_RECORD_IN_USE;
    rec.head.key_len   = (uint8_t) key_len;
    rec.head.value_len = (uint32_t) value_len;
    memcpy(rec.head.key, key, key_len);
    if (value_len > 0u && value != NULL) {
        memcpy(rec.value, value, value_len);
    }

#if CONFIG_ACTRUST_KV_ENABLE_CRC
    rec.head.crc32 = kv_crc32(rec.value, rec.head.value_len);
#else
    rec.head.crc32 = 0u;
#endif

    /* Step 1: persist the record data. */
    err = kv_write_record(kv->st, index, &rec);
    if (err != ACTRUST_OK) {
        goto out;
    }

    /* Step 2: update the header record count (new keys only). */
    if (is_new) {
        kv_header_t hdr;
        err = kv_read_header(kv->st, &hdr);
        if (err != ACTRUST_OK) {
            goto out;
        }
        if (hdr.record_count < (uint32_t) CONFIG_ACTRUST_KV_MAX_RECORDS) {
            hdr.record_count++;
        }
        err = kv_write_header(kv->st, &hdr);
    }

out: {
    actrust_err_t unlock_err = kv_unlock(kv);
    if (err == ACTRUST_OK && unlock_err != ACTRUST_OK) {
        err = unlock_err;
    }
}
    if (err != ACTRUST_OK) {
        LOG_WARN(
            "kv set failed: ns=%s key_len=%zu value_len=%zu err=0x%08" PRIx32,
            kv->ns, key_len, value_len, err);
    } else {
        LOG_DEBUG("kv set: ns=%s key_len=%zu value_len=%zu new=%d", kv->ns,
                  key_len, value_len, is_new ? 1 : 0);
    }
    return err;
}

actrust_err_t actrust_kv_get(actrust_kv_t kv, const char *key, size_t key_len,
                             void *out_value, size_t out_cap, size_t *out_len)
{
    if (kv == NULL || kv->st == NULL || key == NULL || key_len == 0u ||
        key_len > CONFIG_ACTRUST_KV_MAX_KEY_LEN ||
        (out_value == NULL && out_cap > 0u) || out_len == NULL) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = kv_lock(kv);
    if (err != ACTRUST_OK) {
        return err;
    }

    kv_record_t rec;
    if (!kv_find_record(kv, key, key_len, NULL, &rec)) {
        err = KV_ERR(ACTRUST_ERR_NO_RESOURCE);
        goto out;
    }
    if (!kv_record_head_is_valid(&rec.head)) {
        err = KV_ERR(ACTRUST_ERR_IO);
        goto out;
    }

#if CONFIG_ACTRUST_KV_ENABLE_CRC
    if (rec.head.crc32 != kv_crc32(rec.value, rec.head.value_len)) {
        err = KV_ERR(ACTRUST_ERR_IO);
        goto out;
    }
#endif

    *out_len = (size_t) rec.head.value_len;

    if ((size_t) rec.head.value_len > out_cap) {
        err = KV_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
        goto out;
    }

    if (rec.head.value_len > 0u && out_value != NULL) {
        memcpy(out_value, rec.value, rec.head.value_len);
    }

    err = ACTRUST_OK;

out: {
    actrust_err_t unlock_err = kv_unlock(kv);
    if (err == ACTRUST_OK && unlock_err != ACTRUST_OK) {
        err = unlock_err;
    }
}
    if (err != ACTRUST_OK && ACTRUST_ERR_CODE(err) != ACTRUST_ERR_NO_RESOURCE) {
        LOG_WARN("kv get failed: ns=%s key_len=%zu err=0x%08" PRIx32, kv->ns,
                 key_len, err);
    }
    return err;
}

actrust_err_t actrust_kv_del(actrust_kv_t kv, const char *key, size_t key_len)
{
    if (kv == NULL || kv->st == NULL || key == NULL || key_len == 0u ||
        key_len > CONFIG_ACTRUST_KV_MAX_KEY_LEN) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = kv_lock(kv);
    if (err != ACTRUST_OK) {
        return err;
    }

    size_t index = 0u;
    if (!kv_find_record(kv, key, key_len, &index, NULL)) {
        err = KV_ERR(ACTRUST_ERR_NO_RESOURCE);
        goto out;
    }

    /* Step 1: clear the record. */
    kv_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.head.used = KV_RECORD_NOT_USED;
    err           = kv_write_record(kv->st, index, &rec);
    if (err != ACTRUST_OK) {
        goto out;
    }

    /* Step 2: decrement the header record count. */
    kv_header_t hdr;
    err = kv_read_header(kv->st, &hdr);
    if (err != ACTRUST_OK) {
        goto out;
    }
    if (hdr.record_count > 0u) {
        hdr.record_count--;
    }
    err = kv_write_header(kv->st, &hdr);

out: {
    actrust_err_t unlock_err = kv_unlock(kv);
    if (err == ACTRUST_OK && unlock_err != ACTRUST_OK) {
        err = unlock_err;
    }
}
    if (err != ACTRUST_OK && ACTRUST_ERR_CODE(err) != ACTRUST_ERR_NO_RESOURCE) {
        LOG_WARN("kv del failed: ns=%s key_len=%zu err=0x%08" PRIx32, kv->ns,
                 key_len, err);
    } else if (err == ACTRUST_OK) {
        LOG_DEBUG("kv deleted: ns=%s key_len=%zu", kv->ns, key_len);
    }
    return err;
}

actrust_err_t actrust_kv_exists(actrust_kv_t kv, const char *key,
                                size_t key_len, bool *out_exist)
{
    if (kv == NULL || kv->st == NULL || key == NULL || key_len == 0u ||
        key_len > CONFIG_ACTRUST_KV_MAX_KEY_LEN || out_exist == NULL) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = kv_lock(kv);
    if (err != ACTRUST_OK) {
        return err;
    }

    *out_exist = kv_find_record(kv, key, key_len, NULL, NULL);
    return kv_unlock(kv);
}
