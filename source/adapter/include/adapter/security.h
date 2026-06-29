// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file adapter/security.h
 * @brief Platform security abstraction layer interface
 *
 * Provides a uniform security API that platform adapters implement.
 * On Linux the "secure storage" group is backed by files; on hardware
 * platforms with a TEE / secure element the "secure key" and "secure
 * crypto" groups route operations into the secure world so that key
 * material never leaves the trusted environment.
 */

#ifndef ACTRUST_SECURITY_H
#define ACTRUST_SECURITY_H

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_config.h"
#include "actrust_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup adapter_security Security Services
 * @{
 */

/* ========================================================================
 * Types
 * ======================================================================== */

/** @brief Security slot identifier (used by both storage and key slots). */
typedef uint32_t actrust_sec_slot_t;

/* ---- slot addressing constants ---------------------------------------- */

/** @brief Base hardware slot ID for cryptographic key storage. */
#define ACTRUST_SEC_SLOT_KEY_BASE CONFIG_ACTRUST_SEC_SLOT_KEY_BASE
/** @brief Number of key slots. */
#define ACTRUST_SEC_SLOT_KEY_COUNT CONFIG_ACTRUST_SEC_SLOT_KEY_COUNT

/** @brief Base hardware slot ID for general-purpose secure data blobs. */
#define ACTRUST_SEC_SLOT_DATA_BASE CONFIG_ACTRUST_SEC_SLOT_DATA_BASE
/** @brief Number of data slots. */
#define ACTRUST_SEC_SLOT_DATA_COUNT CONFIG_ACTRUST_SEC_SLOT_DATA_COUNT

/** @brief Base hardware slot ID for X.509 certificate storage. */
#define ACTRUST_SEC_SLOT_CERT_BASE CONFIG_ACTRUST_SEC_SLOT_CERT_BASE
/** @brief Number of certificate slots. */
#define ACTRUST_SEC_SLOT_CERT_COUNT CONFIG_ACTRUST_SEC_SLOT_CERT_COUNT

/* ---- slot addressing helpers (indices are 0-based) --------------------- */

/** @brief Compute a key slot ID from a 0-based index.  */
#define ACTRUST_SEC_SLOT_KEY(n)                                                \
    ((actrust_sec_slot_t) (ACTRUST_SEC_SLOT_KEY_BASE + (n)))
/** @brief Compute a data slot ID from a 0-based index. */
#define ACTRUST_SEC_SLOT_DATA(n)                                               \
    ((actrust_sec_slot_t) (ACTRUST_SEC_SLOT_DATA_BASE + (n)))
/** @brief Compute a cert slot ID from a 0-based index. */
#define ACTRUST_SEC_SLOT_CERT(n)                                               \
    ((actrust_sec_slot_t) (ACTRUST_SEC_SLOT_CERT_BASE + (n)))

/** @brief Key type for secure-element key generation. */
typedef enum {
    ACTRUST_SEC_KEY_EC_P256      = 1, /**< NIST P-256 EC key pair */
    ACTRUST_SEC_KEY_EC_SECP256K1 = 2, /**< secp256k1 EC key pair */
    ACTRUST_SEC_KEY_AES_128      = 3, /**< 128-bit AES key */
    ACTRUST_SEC_KEY_AES_256      = 4, /**< 256-bit AES key */
} actrust_sec_key_type_t;

/** @brief Hash algorithm for TEE-resident hash computation. */
typedef enum {
    ACTRUST_SEC_HASH_SHA256 = 1, /**< SHA-256 (32-byte digest) */
    ACTRUST_SEC_HASH_SHA384 = 2, /**< SHA-384 (48-byte digest) */
    ACTRUST_SEC_HASH_SHA512 = 3, /**< SHA-512 (64-byte digest) */
} actrust_sec_hash_alg_t;

/** @brief Symmetric cipher algorithm for TEE-resident AES operations. */
typedef enum {
    ACTRUST_SEC_AES_GCM = 1, /**< AES-GCM authenticated encryption */
    ACTRUST_SEC_AES_CBC = 2, /**< AES-CBC */
} actrust_sec_sym_alg_t;

/** @brief Block cipher padding mode for TEE-resident AES operations. */
typedef enum {
    ACTRUST_SEC_PAD_NONE  = 0, /**< No padding (input must be block-aligned) */
    ACTRUST_SEC_PAD_PKCS7 = 1, /**< PKCS#7 padding */
    ACTRUST_SEC_PAD_ZEROS = 2, /**< Zero padding */
} actrust_sec_padding_t;

/* ========================================================================
 * Secure Storage (opaque blob read / write / delete)
 * ======================================================================== */

/**
 * @brief Write an opaque data blob to secure storage.
 *
 * @param[in] slot_id  Storage slot identifier.
 * @param[in] data     Data to write.
 * @param[in] len      Length of @p data in bytes.
 *
 * @return @c ACTRUST_OK on success.
 */
actrust_err_t actrust_sec_store_write(actrust_sec_slot_t slot_id,
                                      const uint8_t *data, size_t len);

/**
 * @brief Read an opaque data blob from secure storage.
 *
 * @param[in]  slot_id     Storage slot identifier.
 * @param[out] out         Buffer to receive the data.
 * @param[in]  out_len     Capacity of @p out in bytes.
 * @param[out] actual_len  Receives the actual stored length.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_NO_RESOURCE if the slot does not exist.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p out_len is insufficient.
 */
actrust_err_t actrust_sec_store_read(actrust_sec_slot_t slot_id, uint8_t *out,
                                     size_t out_len, size_t *actual_len);

/**
 * @brief Delete an opaque data blob from secure storage.
 *
 * @param[in] slot_id  Storage slot identifier.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_NO_RESOURCE if the slot does not exist.
 */
actrust_err_t actrust_sec_store_delete(actrust_sec_slot_t slot_id);

/* ========================================================================
 * Hardware RNG
 * ======================================================================== */

/**
 * @brief Obtain random bytes from the platform's hardware TRNG / TEE.
 *
 * @param[out] out  Buffer to receive random bytes.
 * @param[in]  len  Number of bytes to generate (> 0).
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_UNSUPPORTED if no hardware RNG is available.
 */
actrust_err_t actrust_sec_random(uint8_t *out, size_t len);

/* ========================================================================
 * Secure Key Management (key material stays inside TEE / SE)
 * ======================================================================== */

/**
 * @brief Generate a key inside the secure element.
 *
 * The key material is created and stored entirely within the
 * TEE / secure element; the normal world receives only the @p slot_id
 * for subsequent operations.
 *
 * @param[in] slot_id  Slot to store the generated key.
 * @param[in] type     Key type and parameters.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_UNSUPPORTED if the key type is not supported.
 */
actrust_err_t actrust_sec_key_generate(actrust_sec_slot_t     slot_id,
                                       actrust_sec_key_type_t type);
/**
 * @brief Permanently delete a key from the secure element.
 *
 * @param[in] slot_id  Slot identifier.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_NO_RESOURCE if the slot is empty.
 */
actrust_err_t actrust_sec_key_delete(actrust_sec_slot_t slot_id);

/**
 * @brief Export the public part of an EC key from the secure element.
 *
 * Only the public key leaves the secure world; the private key
 * remains inside the TEE.  Output is SubjectPublicKeyInfo DER.
 *
 * @param[in]  slot_id  Slot containing an EC key.
 * @param[out] out      Buffer to receive the DER-encoded public key.
 * @param[in]  out_cap  Capacity of @p out in bytes.
 * @param[out] out_len  Receives the actual output length.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_NO_RESOURCE if the slot is empty.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p out_cap is insufficient.
 */
actrust_err_t actrust_sec_key_get_public(actrust_sec_slot_t slot_id,
                                         uint8_t *out, size_t out_cap,
                                         size_t *out_len);

/* ========================================================================
 * Secure Hash (performed inside TEE / SE)
 * ======================================================================== */

/**
 * @brief Compute a hash digest inside the TEE / secure element.
 *
 * @param[in]  alg        Hash algorithm to use.
 * @param[in]  input      Data to hash.
 * @param[in]  input_len  Length of @p input in bytes.
 * @param[out] out        Buffer to receive the digest.
 * @param[in]  out_cap    Capacity of @p out in bytes.
 * @param[out] out_len    Receives the actual digest length.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_UNSUPPORTED if no hardware hash is available.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p out_cap < digest size.
 */
actrust_err_t actrust_sec_hash(actrust_sec_hash_alg_t alg, const uint8_t *input,
                               size_t input_len, uint8_t *out, size_t out_cap,
                               size_t *out_len);

/* ========================================================================
 * Secure AES Operations (key material stays inside TEE / SE)
 * ======================================================================== */

/**
 * @brief Encrypt data using a TEE-resident AES key.
 *
 * @param[in]  key_slot    Slot containing the AES key.
 * @param[in]  alg         Cipher algorithm.
 * @param[in]  padding     Padding scheme to apply.
 * @param[in]  iv          Initialization vector.
 * @param[in]  iv_len      Length of @p iv.
 * @param[in]  aad         Additional authenticated data (GCM only, may be
 * NULL).
 * @param[in]  aad_len     Length of @p aad.
 * @param[in]  input       Plaintext.
 * @param[in]  input_len   Length of @p input in bytes.
 * @param[out] output      Ciphertext buffer.
 * @param[in]  output_cap  Capacity of @p output in bytes.
 * @param[out] output_len  Receives the actual ciphertext length.
 * @param[out] tag         Authentication tag buffer (required for GCM).
 * @param[in,out] tag_len  In: requested tag length. Out: actual tag length.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_UNSUPPORTED if no hardware AES is available.
 */
actrust_err_t actrust_sec_aes_encrypt(
    actrust_sec_slot_t key_slot, actrust_sec_sym_alg_t alg,
    actrust_sec_padding_t padding, const uint8_t *iv, size_t iv_len,
    const uint8_t *aad, size_t aad_len, const uint8_t *input, size_t input_len,
    uint8_t *output, size_t output_cap, size_t *output_len, uint8_t *tag,
    size_t *tag_len);

/**
 * @brief Decrypt data using a TEE-resident AES key.
 *
 * @param[in]  key_slot    Slot containing the AES key.
 * @param[in]  alg         Cipher algorithm.
 * @param[in]  padding     Padding scheme that was applied during encryption.
 * @param[in]  iv          Initialization vector.
 * @param[in]  iv_len      Length of @p iv.
 * @param[in]  aad         Additional authenticated data (GCM only, may be
 * NULL).
 * @param[in]  aad_len     Length of @p aad.
 * @param[in]  input       Ciphertext.
 * @param[in]  input_len   Length of @p input in bytes.
 * @param[out] output      Plaintext buffer.
 * @param[in]  output_cap  Capacity of @p output in bytes.
 * @param[out] output_len  Receives the actual plaintext length.
 * @param[in]  tag         Authentication tag to verify (required for GCM).
 * @param[in]  tag_len     Length of @p tag in bytes.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_UNSUPPORTED if no hardware AES is available.
 */
actrust_err_t actrust_sec_aes_decrypt(
    actrust_sec_slot_t key_slot, actrust_sec_sym_alg_t alg,
    actrust_sec_padding_t padding, const uint8_t *iv, size_t iv_len,
    const uint8_t *aad, size_t aad_len, const uint8_t *input, size_t input_len,
    uint8_t *output, size_t output_cap, size_t *output_len, const uint8_t *tag,
    size_t tag_len);

/* ========================================================================
 * Secure Crypto Operations (performed inside TEE / SE)
 * ======================================================================== */

/**
 * @brief Sign a digest using a TEE-resident EC private key.
 *
 * The signing operation is performed entirely inside the secure world.
 * The caller provides a pre-computed hash digest.
 *
 * @param[in]  slot_id   Slot containing the EC private key.
 * @param[in]  digest    Hash digest to sign.
 * @param[in]  dig_len   Length of @p digest in bytes.
 * @param[out] sig       Buffer for the DER-encoded signature.
 * @param[in]  sig_cap   Capacity of @p sig in bytes.
 * @param[out] sig_len   Receives the actual signature length.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_NO_RESOURCE if the slot is empty.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p sig_cap is insufficient.
 */
actrust_err_t actrust_sec_ecdsa_sign(actrust_sec_slot_t slot_id,
                                     const uint8_t *digest, size_t dig_len,
                                     uint8_t *sig, size_t sig_cap,
                                     size_t *sig_len);

/**
 * @brief Verify a signature using a TEE-resident EC public key.
 *
 * @param[in] slot_id   Slot containing the EC key (public part used).
 * @param[in] digest    Hash digest that was signed.
 * @param[in] dig_len   Length of @p digest in bytes.
 * @param[in] sig       DER-encoded signature to verify.
 * @param[in] sig_len   Length of @p sig in bytes.
 *
 * @return @c ACTRUST_OK if the signature is valid.
 * @return @c ACTRUST_ERR_NO_RESOURCE if the slot is empty.
 * @return Module-specific error if verification fails.
 */
actrust_err_t actrust_sec_ecdsa_verify(actrust_sec_slot_t slot_id,
                                       const uint8_t *digest, size_t dig_len,
                                       const uint8_t *sig, size_t sig_len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_SECURITY_H */
