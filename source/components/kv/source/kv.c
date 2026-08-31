// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file kv.c
 * @brief KV storage component — implementation
 *
 * Storage layout (all offsets relative to the start of a namespace region):
 *
 *   Offset 0: two 64-byte v2 superblocks
 *   Offset 128: fixed-size canonical little-endian records
 *   Tail: two journal slots with PREPARED/COMMITTED state
 *
 * v1 records are detected and rejected without destructive migration.
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
#include "actrust_config.h"

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

/** @brief Magic number written to the KV v2 superblock ("ACTR"). */
#define KV_HEADER_MAGIC 0x41435452u
/** @brief Legacy v1 format version, detected but never migrated. */
#define KV_HEADER_VERSION_V1 0x0001u
/** @brief Current journaled KV format version. */
#define KV_HEADER_VERSION_V2 0x0002u
#define KV_LAYOUT_VERSION_V2 0x0001u

#define KV_SUPERBLOCK_SIZE          64u
#define KV_DATA_OFFSET              (KV_SUPERBLOCK_SIZE * 2u)
#define KV_JOURNAL_SLOTS            2u
#define KV_JOURNAL_MAGIC            0x4A4E4C32u
#define KV_JOURNAL_VERSION          1u
#define KV_JOURNAL_EMPTY            0u
#define KV_JOURNAL_PREPARED         1u
#define KV_JOURNAL_COMMITTED        2u
#define KV_JOURNAL_COMMIT_MAGIC     0x434F4D32u
#define KV_OP_SET                   1u
#define KV_OP_DELETE                2u
#define KV_OP_FORMAT                3u
#define KV_REGISTRY_SIZE            4u
#define KV_RECORD_HEAD_WIRE_SIZE    44u
#define KV_RECORD_WIRE_VALUE_OFFSET KV_RECORD_HEAD_WIRE_SIZE
#define KV_SUPERBLOCK_CRC_OFFSET    20u
#define KV_JOURNAL_RECORD_OFFSET    36u
#define KV_JOURNAL_COMMIT_OFFSET                                               \
    (KV_JOURNAL_RECORD_OFFSET + KV_RECORD_HEAD_WIRE_SIZE +                     \
     CONFIG_ACTRUST_KV_MAX_VALUE_LEN)
#define KV_JOURNAL_WIRE_SIZE (KV_JOURNAL_COMMIT_OFFSET + 16u)

/**
 * @brief Record metadata in the canonical on-disk record encoding.
 */
typedef struct {
    uint8_t  used;
    uint8_t  key_len;
    uint16_t reserved;
    uint32_t crc32;
    uint32_t value_len;
    uint8_t  key[CONFIG_ACTRUST_KV_MAX_KEY_LEN];
} kv_record_head_t;

/**
 * @brief Fixed-size KV record containing key metadata and value bytes.
 */
typedef struct {
    kv_record_head_t head;
    uint8_t          value[CONFIG_ACTRUST_KV_MAX_VALUE_LEN];
} kv_record_t;

/**
 * @brief Redundant v2 superblock persisted at the beginning of a namespace.
 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t layout;
    uint64_t generation;
    uint32_t record_count;
    uint32_t crc32;
    uint8_t  reserved[36];
} kv_superblock_t;

/**
 * @brief Journal descriptor for one recoverable KV transaction.
 */
typedef struct {
    uint32_t    magic;
    uint16_t    version;
    uint8_t     state;
    uint8_t     operation;
    uint64_t    generation;
    uint64_t    base_generation;
    uint32_t    record_index;
    uint32_t    record_count;
    uint32_t    crc32;
    kv_record_t record;
    uint32_t    commit_magic;
    uint64_t    commit_generation;
    uint32_t    commit_crc32;
} kv_journal_t;

typedef struct {
    bool                 used;
    actrust_storage_id_t id;
    actrust_mutex_t      lock;
    size_t               refs;
} kv_namespace_t;

struct actrust_kv {
    char                 ns[CONFIG_ACTRUST_KV_NAMESPACE_MAX_LEN];
    actrust_storage_id_t id;
    actrust_storage_t    st;
    kv_namespace_t      *shared;
};

typedef struct {
    const char          *ns;
    actrust_storage_id_t id;
} kv_ns_map_t;

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

/** @brief Number of entries actually compiled into @c kv_ns_map. */
#define KV_NS_MAP_SIZE    (sizeof(kv_ns_map) / sizeof(kv_ns_map[0]) - 1u)
#define KV_RECORD_SIZE    ((uint32_t) sizeof(kv_record_t))
#define KV_DATA_SIZE      (KV_RECORD_SIZE * (uint32_t) CONFIG_ACTRUST_KV_MAX_RECORDS)
#define KV_JOURNAL_SIZE   KV_JOURNAL_WIRE_SIZE
#define KV_JOURNAL_OFFSET (KV_DATA_OFFSET + KV_DATA_SIZE)
#define KV_REGION_SIZE    (KV_JOURNAL_OFFSET + KV_JOURNAL_SIZE * KV_JOURNAL_SLOTS)

static kv_namespace_t  g_namespaces[KV_REGISTRY_SIZE];
static volatile int    g_registry_init_state;
static actrust_mutex_t g_registry_lock;

/* ========================================================================
 * Private Helper Functions — CRC
 * ======================================================================== */

static uint32_t kv_crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0u; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc;
}

/**
 * @brief Compute CRC-32 (ISO 3309 / ITU-T V.42) over a byte buffer.
 *
 * Uses the standard polynomial @c 0xEDB88320 (bit-reversed representation),
 * the same algorithm used by zlib, PNG, and Ethernet FCS.
 *
 * @param[in] data Pointer to input bytes.
 * @param[in] len Number of bytes to process.
 * @return Computed CRC-32 value.
 */
static uint32_t kv_crc32(const uint8_t *data, size_t len)
{
    return ~kv_crc32_update(0xFFFFFFFFu, data, len);
}

static void kv_put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t) value;
    out[1] = (uint8_t) (value >> 8);
}

static void kv_put_u32(uint8_t *out, uint32_t value)
{
    for (size_t i = 0u; i < 4u; ++i) {
        out[i] = (uint8_t) (value >> (8u * i));
    }
}

static void kv_put_u64(uint8_t *out, uint64_t value)
{
    for (size_t i = 0u; i < 8u; ++i) {
        out[i] = (uint8_t) (value >> (8u * i));
    }
}

static uint16_t kv_get_u16(const uint8_t *in)
{
    return (uint16_t) in[0] | ((uint16_t) in[1] << 8);
}

static uint32_t kv_get_u32(const uint8_t *in)
{
    uint32_t value = 0u;
    for (size_t i = 0u; i < 4u; ++i) {
        value |= (uint32_t) in[i] << (8u * i);
    }
    return value;
}

static uint64_t kv_get_u64(const uint8_t *in)
{
    uint64_t value = 0u;
    for (size_t i = 0u; i < 8u; ++i) {
        value |= (uint64_t) in[i] << (8u * i);
    }
    return value;
}

static size_t kv_record_wire_size(void)
{
    return KV_RECORD_HEAD_WIRE_SIZE + CONFIG_ACTRUST_KV_MAX_VALUE_LEN;
}

static void kv_encode_record(const kv_record_t *record, uint8_t *out)
{
    memset(out, 0, kv_record_wire_size());
    out[0] = record->head.used;
    out[1] = record->head.key_len;
    kv_put_u16(out + 2u, record->head.reserved);
    kv_put_u32(out + 4u, record->head.crc32);
    kv_put_u32(out + 8u, record->head.value_len);
    memcpy(out + 12u, record->head.key, CONFIG_ACTRUST_KV_MAX_KEY_LEN);
    memcpy(out + KV_RECORD_WIRE_VALUE_OFFSET, record->value,
           CONFIG_ACTRUST_KV_MAX_VALUE_LEN);
}

static void kv_decode_record(const uint8_t *in, kv_record_t *record)
{
    memset(record, 0, sizeof(*record));
    record->head.used      = in[0];
    record->head.key_len   = in[1];
    record->head.reserved  = kv_get_u16(in + 2u);
    record->head.crc32     = kv_get_u32(in + 4u);
    record->head.value_len = kv_get_u32(in + 8u);
    memcpy(record->head.key, in + 12u, CONFIG_ACTRUST_KV_MAX_KEY_LEN);
    memcpy(record->value, in + KV_RECORD_WIRE_VALUE_OFFSET,
           CONFIG_ACTRUST_KV_MAX_VALUE_LEN);
}

static uint32_t kv_record_crc(const kv_record_t *record)
{
    uint8_t  bytes[KV_RECORD_HEAD_WIRE_SIZE + CONFIG_ACTRUST_KV_MAX_VALUE_LEN];
    uint32_t crc;

    kv_encode_record(record, bytes);
    memset(bytes + 4u, 0, sizeof(uint32_t));
    crc = kv_crc32(bytes, KV_RECORD_HEAD_WIRE_SIZE + record->head.value_len);
    return crc;
}

static uint32_t kv_superblock_crc(const kv_superblock_t *sb)
{
    uint8_t bytes[KV_SUPERBLOCK_SIZE];
    memset(bytes, 0, sizeof(bytes));
    kv_put_u32(bytes, sb->magic);
    kv_put_u16(bytes + 4u, sb->version);
    kv_put_u16(bytes + 6u, sb->layout);
    kv_put_u64(bytes + 8u, sb->generation);
    kv_put_u32(bytes + 16u, sb->record_count);
    return kv_crc32(bytes, KV_SUPERBLOCK_CRC_OFFSET);
}

static void kv_encode_superblock(const kv_superblock_t *sb, uint8_t *out)
{
    memset(out, 0, KV_SUPERBLOCK_SIZE);
    kv_put_u32(out, sb->magic);
    kv_put_u16(out + 4u, sb->version);
    kv_put_u16(out + 6u, sb->layout);
    kv_put_u64(out + 8u, sb->generation);
    kv_put_u32(out + 16u, sb->record_count);
    kv_put_u32(out + KV_SUPERBLOCK_CRC_OFFSET, sb->crc32);
}

static void kv_decode_superblock(const uint8_t *in, kv_superblock_t *sb)
{
    memset(sb, 0, sizeof(*sb));
    sb->magic        = kv_get_u32(in);
    sb->version      = kv_get_u16(in + 4u);
    sb->layout       = kv_get_u16(in + 6u);
    sb->generation   = kv_get_u64(in + 8u);
    sb->record_count = kv_get_u32(in + 16u);
    sb->crc32        = kv_get_u32(in + KV_SUPERBLOCK_CRC_OFFSET);
}

static uint32_t kv_journal_crc(const kv_journal_t *journal)
{
    uint8_t bytes[KV_JOURNAL_WIRE_SIZE];
    memset(bytes, 0, sizeof(bytes));
    kv_put_u32(bytes, journal->magic);
    kv_put_u16(bytes + 4u, journal->version);
    bytes[6] = journal->state;
    bytes[7] = journal->operation;
    kv_put_u64(bytes + 8u, journal->generation);
    kv_put_u64(bytes + 16u, journal->base_generation);
    kv_put_u32(bytes + 24u, journal->record_index);
    kv_put_u32(bytes + 28u, journal->record_count);
    kv_put_u32(bytes + 32u, 0u);
    uint8_t *record_bytes = bytes + KV_JOURNAL_RECORD_OFFSET;
    kv_encode_record(&journal->record, record_bytes);
    return kv_crc32(bytes, KV_JOURNAL_COMMIT_OFFSET);
}

static void kv_encode_journal(const kv_journal_t *journal, uint8_t *out)
{
    memset(out, 0, KV_JOURNAL_WIRE_SIZE);
    kv_put_u32(out, journal->magic);
    kv_put_u16(out + 4u, journal->version);
    out[6] = journal->state;
    out[7] = journal->operation;
    kv_put_u64(out + 8u, journal->generation);
    kv_put_u64(out + 16u, journal->base_generation);
    kv_put_u32(out + 24u, journal->record_index);
    kv_put_u32(out + 28u, journal->record_count);
    kv_put_u32(out + 32u, journal->crc32);
    kv_encode_record(&journal->record, out + KV_JOURNAL_RECORD_OFFSET);
    kv_put_u32(out + KV_JOURNAL_COMMIT_OFFSET, journal->commit_magic);
    kv_put_u64(out + KV_JOURNAL_COMMIT_OFFSET + 4u, journal->commit_generation);
    kv_put_u32(out + KV_JOURNAL_COMMIT_OFFSET + 12u, journal->commit_crc32);
}

static void kv_decode_journal(const uint8_t *in, kv_journal_t *journal)
{
    memset(journal, 0, sizeof(*journal));
    journal->magic           = kv_get_u32(in);
    journal->version         = kv_get_u16(in + 4u);
    journal->state           = in[6];
    journal->operation       = in[7];
    journal->generation      = kv_get_u64(in + 8u);
    journal->base_generation = kv_get_u64(in + 16u);
    journal->record_index    = kv_get_u32(in + 24u);
    journal->record_count    = kv_get_u32(in + 28u);
    journal->crc32           = kv_get_u32(in + 32u);
    kv_decode_record(in + KV_JOURNAL_RECORD_OFFSET, &journal->record);
    journal->commit_magic      = kv_get_u32(in + KV_JOURNAL_COMMIT_OFFSET);
    journal->commit_generation = kv_get_u64(in + KV_JOURNAL_COMMIT_OFFSET + 4u);
    journal->commit_crc32 = kv_get_u32(in + KV_JOURNAL_COMMIT_OFFSET + 12u);
}

static uint32_t kv_commit_crc(const kv_journal_t *journal)
{
    uint8_t bytes[12];
    kv_put_u32(bytes, journal->commit_magic);
    kv_put_u64(bytes + 4u, journal->commit_generation);
    return kv_crc32(bytes, sizeof(bytes));
}

/* ========================================================================
 * Private Helper Functions — Namespace Lookup
 * ======================================================================== */

/**
 * @brief Resolve a namespace string to its storage identifier.
 *
 * Searches the compile-time @ref kv_ns_map for a matching entry.
 *
 * @param[in] ns Namespace string, not necessarily NUL-terminated.
 * @param[in] len Length of @p ns in bytes.
 * @param[out] out_id Receives the storage identifier on success.
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
    for (size_t i = 0u; i < count; ++i) {
        if (kv_ns_map[i].ns != NULL && strlen(kv_ns_map[i].ns) == len &&
            memcmp(kv_ns_map[i].ns, ns, len) == 0) {
            *out_id = kv_ns_map[i].id;
            return true;
        }
    }
    return false;
}

static kv_namespace_t *kv_namespace_find(actrust_storage_id_t id)
{
    for (size_t i = 0u; i < KV_REGISTRY_SIZE; ++i) {
        if (g_namespaces[i].used && g_namespaces[i].id == id) {
            return &g_namespaces[i];
        }
    }
    return NULL;
}

static actrust_err_t kv_namespace_prepare(void)
{
    actrust_err_t err;

    for (;;) {
        int state = __sync_val_compare_and_swap(&g_registry_init_state, 0, 0);
        if (state == 2) {
            break;
        }
        if (state == 0 &&
            __sync_bool_compare_and_swap(&g_registry_init_state, 0, 1)) {
            err = actrust_mutex_create(&g_registry_lock);
            if (ACTRUST_IS_ERR(err)) {
                __sync_bool_compare_and_swap(&g_registry_init_state, 1, 0);
                return err;
            }
            __sync_synchronize();
            __sync_lock_test_and_set(&g_registry_init_state, 2);
            break;
        }
        actrust_sleep_ms(1u);
    }

    err = actrust_mutex_lock(g_registry_lock);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    size_t count = (size_t) CONFIG_ACTRUST_KV_NS_MAP_COUNT;
    if (count > KV_NS_MAP_SIZE) {
        count = KV_NS_MAP_SIZE;
    }
    if (count > KV_REGISTRY_SIZE) {
        (void) actrust_mutex_unlock(g_registry_lock);
        return KV_ERR(ACTRUST_ERR_NO_RESOURCE);
    }

    for (size_t i = 0u; i < count; ++i) {
        actrust_storage_id_t id = kv_ns_map[i].id;
        if (kv_namespace_find(id) != NULL) {
            continue;
        }
        kv_namespace_t *entry = NULL;
        for (size_t j = 0u; j < KV_REGISTRY_SIZE; ++j) {
            if (!g_namespaces[j].used) {
                entry = &g_namespaces[j];
                break;
            }
        }
        if (entry == NULL) {
            (void) actrust_mutex_unlock(g_registry_lock);
            return KV_ERR(ACTRUST_ERR_NO_RESOURCE);
        }
        actrust_err_t entry_err = actrust_mutex_create(&entry->lock);
        if (ACTRUST_IS_ERR(entry_err)) {
            (void) actrust_mutex_unlock(g_registry_lock);
            return entry_err;
        }
        entry->used = true;
        entry->id   = id;
        entry->refs = 0u;
    }
    err = actrust_mutex_unlock(g_registry_lock);
    return err;
}

static actrust_err_t kv_lock(kv_namespace_t *shared)
{
    if (shared == NULL || shared->lock == NULL) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return actrust_mutex_lock(shared->lock);
}

static actrust_err_t kv_unlock(kv_namespace_t *shared)
{
    if (shared == NULL || shared->lock == NULL) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return actrust_mutex_unlock(shared->lock);
}

/* ========================================================================
 * Private Helper Functions — Storage I/O Wrappers
 * ======================================================================== */

/**
 * @brief Compute the byte offset of a record by its index.
 * @param[in] index Zero-based record index.
 * @return Byte offset from the beginning of the storage region.
 */
static uint32_t kv_record_offset(size_t index)
{
    return KV_DATA_OFFSET + KV_RECORD_SIZE * (uint32_t) index;
}

static uint32_t kv_journal_offset(size_t index)
{
    return KV_JOURNAL_OFFSET + KV_JOURNAL_SIZE * (uint32_t) index;
}

/**
 * @brief Read a single record from storage.
 * @param[in] st Opened storage handle.
 * @param[in] index Record index (0-based).
 * @param[out] record Receives the record on success.
 * @return ACTRUST_OK on success, or a storage-layer error code.
 */
static actrust_err_t kv_read_record(actrust_storage_t st, size_t index,
                                    kv_record_t *record)
{
    uint8_t bytes[KV_RECORD_HEAD_WIRE_SIZE + CONFIG_ACTRUST_KV_MAX_VALUE_LEN];
    actrust_err_t err =
        actrust_storage_read(st, kv_record_offset(index), bytes, sizeof(bytes));
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    kv_decode_record(bytes, record);
    return ACTRUST_OK;
}

/**
 * @brief Write a single record to storage.
 * @param[in] st Opened storage handle.
 * @param[in] index Record index (0-based).
 * @param[in] record Record to persist.
 * @return ACTRUST_OK on success, or a storage-layer error code.
 */
static actrust_err_t kv_write_record(actrust_storage_t st, size_t index,
                                     const kv_record_t *record)
{
    uint8_t bytes[KV_RECORD_HEAD_WIRE_SIZE + CONFIG_ACTRUST_KV_MAX_VALUE_LEN];
    kv_encode_record(record, bytes);
    return actrust_storage_write(st, kv_record_offset(index), bytes,
                                 sizeof(bytes));
}

static actrust_err_t kv_read_superblock(actrust_storage_t st, size_t index,
                                        kv_superblock_t *sb)
{
    uint8_t       bytes[KV_SUPERBLOCK_SIZE];
    actrust_err_t err = actrust_storage_read(
        st, (uint32_t) index * KV_SUPERBLOCK_SIZE, bytes, sizeof(bytes));
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    kv_decode_superblock(bytes, sb);
    return ACTRUST_OK;
}

static actrust_err_t kv_write_superblock(actrust_storage_t st, size_t index,
                                         const kv_superblock_t *sb)
{
    uint8_t bytes[KV_SUPERBLOCK_SIZE];
    kv_encode_superblock(sb, bytes);
    return actrust_storage_write(st, (uint32_t) index * KV_SUPERBLOCK_SIZE,
                                 bytes, sizeof(bytes));
}

static actrust_err_t kv_read_journal(actrust_storage_t st, size_t index,
                                     kv_journal_t *journal)
{
    uint8_t       bytes[sizeof(*journal)];
    actrust_err_t err = actrust_storage_read(st, kv_journal_offset(index),
                                             bytes, sizeof(bytes));
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    kv_decode_journal(bytes, journal);
    return ACTRUST_OK;
}

static actrust_err_t kv_write_journal(actrust_storage_t st, size_t index,
                                      const kv_journal_t *journal)
{
    uint8_t bytes[KV_JOURNAL_WIRE_SIZE];
    kv_encode_journal(journal, bytes);
    return actrust_storage_write(st, kv_journal_offset(index), bytes,
                                 KV_JOURNAL_WIRE_SIZE);
}

static actrust_err_t kv_clear_journal(actrust_storage_t st, size_t index)
{
    return actrust_storage_erase(st, kv_journal_offset(index), KV_JOURNAL_SIZE);
}

/* ========================================================================
 * Private Helper Functions — Validation and Recovery
 * ======================================================================== */

/**
 * @brief Validate persistent record metadata before using bounded arrays.
 */
static bool kv_record_is_valid(const kv_record_t *record)
{
    if (record == NULL) {
        return false;
    }
    if (record->head.used == KV_RECORD_NOT_USED) {
        return true;
    }
    if (record->head.used != KV_RECORD_IN_USE || record->head.key_len == 0u ||
        record->head.key_len > CONFIG_ACTRUST_KV_MAX_KEY_LEN ||
        record->head.value_len > CONFIG_ACTRUST_KV_MAX_VALUE_LEN) {
        return false;
    }
    return record->head.crc32 == kv_record_crc(record);
}

static bool kv_superblock_is_valid(const kv_superblock_t *sb)
{
    return sb != NULL && sb->magic == KV_HEADER_MAGIC &&
           sb->version == KV_HEADER_VERSION_V2 &&
           sb->layout == KV_LAYOUT_VERSION_V2 &&
           sb->record_count <= CONFIG_ACTRUST_KV_MAX_RECORDS &&
           sb->crc32 == kv_superblock_crc(sb);
}

static bool kv_journal_is_zero(const kv_journal_t *journal)
{
    const uint8_t *bytes = (const uint8_t *) journal;
    for (size_t i = 0u; i < sizeof(*journal); ++i) {
        if (bytes[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool kv_journal_is_valid(const kv_journal_t *journal)
{
    if (journal == NULL || journal->magic != KV_JOURNAL_MAGIC ||
        journal->version != KV_JOURNAL_VERSION ||
        (journal->state != KV_JOURNAL_PREPARED &&
         journal->state != KV_JOURNAL_COMMITTED) ||
        journal->operation < KV_OP_SET || journal->operation > KV_OP_FORMAT ||
        journal->generation == 0u ||
        journal->crc32 != kv_journal_crc(journal)) {
        return false;
    }
    if (journal->operation != KV_OP_FORMAT &&
        journal->record_index >= CONFIG_ACTRUST_KV_MAX_RECORDS) {
        return false;
    }
    if (journal->operation != KV_OP_FORMAT &&
        !kv_record_is_valid(&journal->record)) {
        return false;
    }
    if (journal->state == KV_JOURNAL_COMMITTED &&
        (journal->commit_magic != KV_JOURNAL_COMMIT_MAGIC ||
         journal->commit_generation != journal->generation ||
         journal->commit_crc32 != kv_commit_crc(journal))) {
        return false;
    }
    return true;
}

static void kv_make_superblock(kv_superblock_t *sb, uint64_t generation,
                               uint32_t record_count)
{
    memset(sb, 0, sizeof(*sb));
    sb->magic        = KV_HEADER_MAGIC;
    sb->version      = KV_HEADER_VERSION_V2;
    sb->layout       = KV_LAYOUT_VERSION_V2;
    sb->generation   = generation;
    sb->record_count = record_count;
    sb->crc32        = kv_superblock_crc(sb);
}

static actrust_err_t kv_select_superblock(actrust_storage_t st,
                                          kv_superblock_t  *out_sb,
                                          size_t           *out_index)
{
    kv_superblock_t sb[2];
    actrust_err_t   err = kv_read_superblock(st, 0u, &sb[0]);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    err = kv_read_superblock(st, 1u, &sb[1]);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    bool valid0 = kv_superblock_is_valid(&sb[0]);
    bool valid1 = kv_superblock_is_valid(&sb[1]);
    if (!valid0 && !valid1) {
        return KV_ERR(ACTRUST_ERR_NO_RESOURCE);
    }
    size_t selected =
        valid1 && (!valid0 || sb[1].generation > sb[0].generation) ? 1u : 0u;
    *out_sb    = sb[selected];
    *out_index = selected;
    return ACTRUST_OK;
}

static actrust_err_t kv_validate_records(actrust_storage_t st,
                                         uint32_t          expected_count)
{
    uint32_t count = 0u;
    for (size_t i = 0u; i < CONFIG_ACTRUST_KV_MAX_RECORDS; ++i) {
        kv_record_t   record;
        actrust_err_t err = kv_read_record(st, i, &record);
        if (ACTRUST_IS_ERR(err)) {
            return KV_ERR(ACTRUST_ERR_IO);
        }
        if (!kv_record_is_valid(&record)) {
            return KV_ERR(ACTRUST_ERR_IO);
        }
        if (record.head.used == KV_RECORD_IN_USE) {
            count++;
        }
    }
    return count == expected_count ? ACTRUST_OK : KV_ERR(ACTRUST_ERR_IO);
}

static actrust_err_t kv_complete_format(actrust_storage_t   st,
                                        const kv_journal_t *journal)
{
    if (journal->base_generation != 0u || journal->generation == 0u) {
        return KV_ERR(ACTRUST_ERR_IO);
    }
    actrust_err_t err = actrust_storage_erase(st, 0u, KV_JOURNAL_OFFSET);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    kv_superblock_t sb;
    kv_make_superblock(&sb, journal->generation, 0u);
    err = kv_write_superblock(st, 0u, &sb);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    err = kv_write_superblock(st, 1u, &sb);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    return actrust_storage_sync(st);
}

static actrust_err_t kv_recover(actrust_storage_t st)
{
    kv_superblock_t current;
    size_t          current_index = 0u;
    actrust_err_t   select_err =
        kv_select_superblock(st, &current, &current_index);
    bool no_superblock =
        ACTRUST_IS_ERR(select_err) &&
        ACTRUST_ERR_CODE(select_err) == ACTRUST_ERR_NO_RESOURCE;
    if (ACTRUST_IS_ERR(select_err) && !no_superblock) {
        return select_err;
    }

    kv_journal_t journals[KV_JOURNAL_SLOTS];
    bool         valid[KV_JOURNAL_SLOTS] = { false, false };
    for (size_t i = 0u; i < KV_JOURNAL_SLOTS; ++i) {
        actrust_err_t err = kv_read_journal(st, i, &journals[i]);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
        if (kv_journal_is_zero(&journals[i])) {
            continue;
        }
        if (!kv_journal_is_valid(&journals[i])) {
            if (no_superblock) {
                return KV_ERR(ACTRUST_ERR_IO);
            }
            continue;
        }
        valid[i] = true;
    }

    size_t selected_journal = KV_JOURNAL_SLOTS;
    for (size_t i = 0u; i < KV_JOURNAL_SLOTS; ++i) {
        if (valid[i] &&
            (selected_journal == KV_JOURNAL_SLOTS ||
             journals[i].generation > journals[selected_journal].generation)) {
            selected_journal = i;
        }
    }

    if (no_superblock) {
        if (selected_journal == KV_JOURNAL_SLOTS) {
            return KV_ERR(ACTRUST_ERR_NO_RESOURCE);
        }
        if (journals[selected_journal].operation != KV_OP_FORMAT) {
            return KV_ERR(ACTRUST_ERR_IO);
        }
        actrust_err_t err = kv_complete_format(st, &journals[selected_journal]);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
        return kv_clear_journal(st, selected_journal);
    }

    if (selected_journal == KV_JOURNAL_SLOTS) {
        return kv_validate_records(st, current.record_count);
    }
    kv_journal_t *journal = &journals[selected_journal];
    if (journal->generation <= current.generation) {
        return kv_clear_journal(st, selected_journal);
    }
    if (journal->base_generation != current.generation ||
        journal->operation == KV_OP_FORMAT) {
        return KV_ERR(ACTRUST_ERR_IO);
    }

    actrust_err_t err =
        kv_write_record(st, journal->record_index, &journal->record);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    kv_superblock_t next;
    kv_make_superblock(&next, journal->generation, journal->record_count);
    err = kv_write_superblock(st, (current_index + 1u) % 2u, &next);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    err = actrust_storage_sync(st);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    return kv_clear_journal(st, selected_journal);
}

static bool kv_region_is_zero(const uint8_t *bytes, size_t len)
{
    for (size_t i = 0u; i < len; ++i) {
        if (bytes[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool kv_storage_is_zero(actrust_storage_t st, uint32_t len)
{
    uint8_t zeros[256];
    uint8_t bytes[256];
    memset(zeros, 0, sizeof(zeros));

    uint32_t offset = 0u;
    while (offset < len) {
        uint32_t chunk = len - offset;
        if (chunk > (uint32_t) sizeof(bytes)) {
            chunk = (uint32_t) sizeof(bytes);
        }
        if (ACTRUST_IS_ERR(actrust_storage_read(st, offset, bytes, chunk)) ||
            memcmp(bytes, zeros, chunk) != 0) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

static actrust_err_t kv_prepare_storage(actrust_storage_t st)
{
    uint32_t      capacity = 0u;
    actrust_err_t err      = actrust_storage_get_capacity(st, &capacity);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    if (capacity < KV_REGION_SIZE) {
        return KV_ERR(ACTRUST_ERR_UNSUPPORTED);
    }

    uint8_t prefix[12];
    err = actrust_storage_read(st, 0u, prefix, sizeof(prefix));
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    uint32_t magic   = kv_get_u32(prefix);
    uint16_t version = kv_get_u16(prefix + sizeof(magic));

    if (magic == KV_HEADER_MAGIC && version == KV_HEADER_VERSION_V1) {
        return KV_ERR(ACTRUST_ERR_UNSUPPORTED);
    }
    if (magic == KV_HEADER_MAGIC && version == KV_HEADER_VERSION_V2) {
        return kv_recover(st);
    }
    if (!kv_region_is_zero(prefix, sizeof(prefix))) {
        return KV_ERR(ACTRUST_ERR_IO);
    }

    err = kv_recover(st);
    if (ACTRUST_IS_OK(err)) {
        return ACTRUST_OK;
    }
    if (ACTRUST_ERR_CODE(err) != ACTRUST_ERR_NO_RESOURCE) {
        return err;
    }

    if (!kv_storage_is_zero(st, KV_REGION_SIZE)) {
        return KV_ERR(ACTRUST_ERR_IO);
    }

    kv_journal_t journal;
    memset(&journal, 0, sizeof(journal));
    journal.magic           = KV_JOURNAL_MAGIC;
    journal.version         = KV_JOURNAL_VERSION;
    journal.state           = KV_JOURNAL_PREPARED;
    journal.operation       = KV_OP_FORMAT;
    journal.generation      = 1u;
    journal.base_generation = 0u;
    journal.crc32           = kv_journal_crc(&journal);
    err                     = kv_write_journal(st, 0u, &journal);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    err = actrust_storage_sync(st);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    err = kv_complete_format(st, &journal);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    journal.state             = KV_JOURNAL_COMMITTED;
    journal.commit_magic      = KV_JOURNAL_COMMIT_MAGIC;
    journal.commit_generation = journal.generation;
    journal.commit_crc32      = kv_commit_crc(&journal);
    journal.crc32             = kv_journal_crc(&journal);
    err                       = kv_write_journal(st, 0u, &journal);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    err = actrust_storage_sync(st);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    return kv_clear_journal(st, 0u);
}

static actrust_err_t kv_load_current(actrust_storage_t st, kv_superblock_t *sb,
                                     size_t *sb_index)
{
    actrust_err_t err = kv_recover(st);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    return kv_select_superblock(st, sb, sb_index);
}

/* ========================================================================
 * Private Helper Functions — Record Search
 * ======================================================================== */

/**
 * @brief Search for a record matching a key.
 * @note The caller must hold the namespace lock.
 */
static actrust_err_t kv_find_record(actrust_storage_t st, const char *key,
                                    size_t key_len, size_t *out_index,
                                    uint32_t *out_count)
{
    size_t   found = CONFIG_ACTRUST_KV_MAX_RECORDS;
    uint32_t count = 0u;
    for (size_t i = 0u; i < CONFIG_ACTRUST_KV_MAX_RECORDS; ++i) {
        kv_record_t   record;
        actrust_err_t err = kv_read_record(st, i, &record);
        if (ACTRUST_IS_ERR(err) || !kv_record_is_valid(&record)) {
            return KV_ERR(ACTRUST_ERR_IO);
        }
        if (record.head.used != KV_RECORD_IN_USE) {
            continue;
        }
        count++;
        if (record.head.key_len == key_len &&
            memcmp(record.head.key, key, key_len) == 0) {
            found = i;
        }
    }
    if (out_index != NULL) {
        *out_index = found;
    }
    if (out_count != NULL) {
        *out_count = count;
    }
    return found == CONFIG_ACTRUST_KV_MAX_RECORDS
               ? KV_ERR(ACTRUST_ERR_NO_RESOURCE)
               : ACTRUST_OK;
}

/**
 * @brief Locate the first unused record slot.
 * @param[in] st Opened storage handle.
 * @return The free record index, or CONFIG_ACTRUST_KV_MAX_RECORDS on failure.
 */
static size_t kv_find_free_record(actrust_storage_t st)
{
    for (size_t i = 0u; i < CONFIG_ACTRUST_KV_MAX_RECORDS; ++i) {
        kv_record_t record;
        if (ACTRUST_IS_ERR(kv_read_record(st, i, &record))) {
            return CONFIG_ACTRUST_KV_MAX_RECORDS;
        }
        if (!kv_record_is_valid(&record)) {
            return CONFIG_ACTRUST_KV_MAX_RECORDS;
        }
        if (record.head.used == KV_RECORD_NOT_USED) {
            return i;
        }
    }
    return CONFIG_ACTRUST_KV_MAX_RECORDS;
}

/**
 * @brief Complete a journaled transaction and publish its superblock.
 * @note The caller must hold the namespace lock.
 */
static actrust_err_t kv_transaction(actrust_storage_t      st,
                                    const kv_superblock_t *current,
                                    size_t current_index, uint8_t operation,
                                    size_t record_index, uint32_t record_count,
                                    const kv_record_t *record)
{
    kv_journal_t journal;
    memset(&journal, 0, sizeof(journal));
    journal.magic           = KV_JOURNAL_MAGIC;
    journal.version         = KV_JOURNAL_VERSION;
    journal.state           = KV_JOURNAL_PREPARED;
    journal.operation       = operation;
    journal.generation      = current->generation + 1u;
    journal.base_generation = current->generation;
    journal.record_index    = (uint32_t) record_index;
    journal.record_count    = record_count;
    if (record != NULL) {
        journal.record = *record;
    }
    journal.crc32 = kv_journal_crc(&journal);

    size_t        journal_index = current->generation % KV_JOURNAL_SLOTS;
    actrust_err_t err           = kv_write_journal(st, journal_index, &journal);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    err = actrust_storage_sync(st);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    if (operation == KV_OP_FORMAT) {
        err = kv_complete_format(st, &journal);
    } else {
        err = kv_write_record(st, record_index, record);
    }
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    kv_superblock_t next;
    kv_make_superblock(&next, journal.generation, record_count);
    err = kv_write_superblock(st, (current_index + 1u) % 2u, &next);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    err = actrust_storage_sync(st);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    journal.state             = KV_JOURNAL_COMMITTED;
    journal.commit_magic      = KV_JOURNAL_COMMIT_MAGIC;
    journal.commit_generation = journal.generation;
    journal.commit_crc32      = kv_commit_crc(&journal);
    journal.crc32             = kv_journal_crc(&journal);
    err                       = kv_write_journal(st, journal_index, &journal);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    err = actrust_storage_sync(st);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    return kv_clear_journal(st, journal_index);
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

actrust_err_t actrust_kv_init(void)
{
    actrust_err_t err = kv_namespace_prepare();
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }

    size_t count = (size_t) CONFIG_ACTRUST_KV_NS_MAP_COUNT;
    if (count > KV_NS_MAP_SIZE) {
        count = KV_NS_MAP_SIZE;
    }
    for (size_t i = 0u; i < count; ++i) {
        actrust_storage_t st = NULL;
        err                  = actrust_storage_open(&st, kv_ns_map[i].id);
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
        kv_namespace_t *shared = kv_namespace_find(kv_ns_map[i].id);
        err                    = kv_lock(shared);
        if (ACTRUST_IS_OK(err)) {
            err                      = kv_prepare_storage(st);
            actrust_err_t unlock_err = kv_unlock(shared);
            if (ACTRUST_IS_OK(err) && ACTRUST_IS_ERR(unlock_err)) {
                err = unlock_err;
            }
        }
        actrust_err_t close_err = actrust_storage_close(st);
        if (ACTRUST_IS_OK(err) && ACTRUST_IS_ERR(close_err)) {
            err = close_err;
        }
        if (ACTRUST_IS_ERR(err)) {
            return err;
        }
    }
    return ACTRUST_OK;
}

actrust_err_t actrust_kv_open(const char *ns, size_t ns_len,
                              actrust_kv_t *out_kv)
{
    if (out_kv == NULL || ns == NULL || ns_len == 0u ||
        ns_len >= CONFIG_ACTRUST_KV_NAMESPACE_MAX_LEN) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    *out_kv = NULL;

    actrust_storage_id_t id;
    if (!kv_lookup_storage_id(ns, ns_len, &id)) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    actrust_err_t err = kv_namespace_prepare();
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    kv_namespace_t *shared = kv_namespace_find(id);
    actrust_kv_t    kv     = (actrust_kv_t) ACTRUST_CALLOC(1u, sizeof(*kv));
    if (kv == NULL) {
        return KV_ERR(ACTRUST_ERR_NO_MEM);
    }
    memcpy(kv->ns, ns, ns_len);
    kv->ns[ns_len] = '\0';
    kv->id         = id;
    kv->shared     = shared;

    err = actrust_storage_open(&kv->st, id);
    if (ACTRUST_IS_ERR(err)) {
        ACTRUST_FREE(kv);
        return err;
    }
    err = kv_lock(shared);
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_storage_close(kv->st);
        ACTRUST_FREE(kv);
        return err;
    }
    err = kv_prepare_storage(kv->st);
    if (ACTRUST_IS_OK(err)) {
        shared->refs++;
    }
    actrust_err_t unlock_err = kv_unlock(shared);
    if (ACTRUST_IS_OK(err) && ACTRUST_IS_ERR(unlock_err)) {
        err = unlock_err;
    }
    if (ACTRUST_IS_ERR(err)) {
        (void) actrust_storage_close(kv->st);
        ACTRUST_FREE(kv);
        return err;
    }
    *out_kv = kv;
    return ACTRUST_OK;
}

actrust_err_t actrust_kv_close(actrust_kv_t kv)
{
    if (kv == NULL || kv->shared == NULL) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    actrust_err_t err = kv_lock(kv->shared);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    actrust_err_t close_err = actrust_storage_close(kv->st);
    if (kv->shared->refs > 0u) {
        kv->shared->refs--;
    }
    actrust_err_t unlock_err = kv_unlock(kv->shared);
    if (ACTRUST_IS_OK(close_err) && ACTRUST_IS_ERR(unlock_err)) {
        close_err = unlock_err;
    }
    memset(kv, 0, sizeof(*kv));
    ACTRUST_FREE(kv);
    return close_err;
}

actrust_err_t actrust_kv_set(actrust_kv_t kv, const char *key, size_t key_len,
                             const void *value, size_t value_len)
{
    if (kv == NULL || key == NULL || key_len == 0u ||
        key_len > CONFIG_ACTRUST_KV_MAX_KEY_LEN ||
        (value == NULL && value_len > 0u) ||
        value_len > CONFIG_ACTRUST_KV_MAX_VALUE_LEN) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    actrust_err_t err = kv_lock(kv->shared);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    kv_superblock_t sb;
    size_t          sb_index;
    err = kv_load_current(kv->st, &sb, &sb_index);
    if (ACTRUST_IS_OK(err)) {
        size_t   index;
        uint32_t count;
        err         = kv_find_record(kv->st, key, key_len, &index, &count);
        bool is_new = ACTRUST_IS_ERR(err) &&
                      ACTRUST_ERR_CODE(err) == ACTRUST_ERR_NO_RESOURCE;
        if (is_new) {
            index = kv_find_free_record(kv->st);
            if (index == CONFIG_ACTRUST_KV_MAX_RECORDS) {
                err = KV_ERR(ACTRUST_ERR_NO_RESOURCE);
            } else {
                err = ACTRUST_OK;
            }
        }
        if (ACTRUST_IS_OK(err)) {
            kv_record_t record;
            memset(&record, 0, sizeof(record));
            record.head.used      = KV_RECORD_IN_USE;
            record.head.key_len   = (uint8_t) key_len;
            record.head.value_len = (uint32_t) value_len;
            memcpy(record.head.key, key, key_len);
            if (value_len > 0u) {
                memcpy(record.value, value, value_len);
            }
            record.head.crc32 = kv_record_crc(&record);
            err = kv_transaction(kv->st, &sb, sb_index, KV_OP_SET, index,
                                 is_new ? count + 1u : count, &record);
        }
    }
    actrust_err_t unlock_err = kv_unlock(kv->shared);
    if (ACTRUST_IS_OK(err) && ACTRUST_IS_ERR(unlock_err)) {
        err = unlock_err;
    }
    return err;
}

actrust_err_t actrust_kv_get(actrust_kv_t kv, const char *key, size_t key_len,
                             void *out_value, size_t out_cap, size_t *out_len)
{
    if (kv == NULL || key == NULL || key_len == 0u ||
        key_len > CONFIG_ACTRUST_KV_MAX_KEY_LEN ||
        (out_value == NULL && out_cap > 0u) || out_len == NULL) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    actrust_err_t err = kv_lock(kv->shared);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    kv_superblock_t sb;
    size_t          sb_index;
    err = kv_load_current(kv->st, &sb, &sb_index);
    (void) sb_index;
    if (ACTRUST_IS_OK(err)) {
        size_t index;
        err = kv_find_record(kv->st, key, key_len, &index, NULL);
        if (ACTRUST_IS_OK(err)) {
            kv_record_t record;
            err = kv_read_record(kv->st, index, &record);
            if (ACTRUST_IS_OK(err) && !kv_record_is_valid(&record)) {
                err = KV_ERR(ACTRUST_ERR_IO);
            }
            if (ACTRUST_IS_OK(err)) {
                *out_len = record.head.value_len;
                if (record.head.value_len > out_cap) {
                    err = KV_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
                } else {
                    memcpy(out_value, record.value, record.head.value_len);
                }
            }
        }
    }
    actrust_err_t unlock_err = kv_unlock(kv->shared);
    if (ACTRUST_IS_OK(err) && ACTRUST_IS_ERR(unlock_err)) {
        err = unlock_err;
    }
    return err;
}

actrust_err_t actrust_kv_del(actrust_kv_t kv, const char *key, size_t key_len)
{
    if (kv == NULL || key == NULL || key_len == 0u ||
        key_len > CONFIG_ACTRUST_KV_MAX_KEY_LEN) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    actrust_err_t err = kv_lock(kv->shared);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    kv_superblock_t sb;
    size_t          sb_index;
    err = kv_load_current(kv->st, &sb, &sb_index);
    if (ACTRUST_IS_OK(err)) {
        size_t   index;
        uint32_t count;
        err = kv_find_record(kv->st, key, key_len, &index, &count);
        if (ACTRUST_IS_OK(err)) {
            kv_record_t empty;
            memset(&empty, 0, sizeof(empty));
            err = kv_transaction(kv->st, &sb, sb_index, KV_OP_DELETE, index,
                                 count - 1u, &empty);
        }
    }
    actrust_err_t unlock_err = kv_unlock(kv->shared);
    if (ACTRUST_IS_OK(err) && ACTRUST_IS_ERR(unlock_err)) {
        err = unlock_err;
    }
    return err;
}

actrust_err_t actrust_kv_exists(actrust_kv_t kv, const char *key,
                                size_t key_len, bool *out_exist)
{
    if (kv == NULL || key == NULL || key_len == 0u ||
        key_len > CONFIG_ACTRUST_KV_MAX_KEY_LEN || out_exist == NULL) {
        return KV_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    actrust_err_t err = kv_lock(kv->shared);
    if (ACTRUST_IS_ERR(err)) {
        return err;
    }
    kv_superblock_t sb;
    size_t          sb_index;
    err = kv_load_current(kv->st, &sb, &sb_index);
    (void) sb_index;
    if (ACTRUST_IS_OK(err)) {
        size_t index;
        err = kv_find_record(kv->st, key, key_len, &index, NULL);
        if (ACTRUST_ERR_CODE(err) == ACTRUST_ERR_NO_RESOURCE) {
            *out_exist = false;
            err        = ACTRUST_OK;
        } else if (ACTRUST_IS_OK(err)) {
            *out_exist = true;
        }
    }
    actrust_err_t unlock_err = kv_unlock(kv->shared);
    if (ACTRUST_IS_OK(err) && ACTRUST_IS_ERR(unlock_err)) {
        err = unlock_err;
    }
    return err;
}
