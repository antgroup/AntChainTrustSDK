// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file crypto.h
 * @brief Cryptographic operations component.
 *
 * Provides cryptographic primitives backed by hardware security modules
 * or software (Mbed TLS), including random number generation,
 * key management, ECDSA signing/verification, and AES encryption.
 */

#ifndef ACTRUST_CRYPTO_H
#define ACTRUST_CRYPTO_H

/* C standard */
#include <stddef.h>
#include <stdint.h>

/* Project */
#include "actrust_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Enumerations
 * ======================================================================== */

/** @brief Cryptographic backend provider. */
typedef enum {
    ACTRUST_CRYPTO_BACKEND_HW = 1, /**< Hardware security module */
    ACTRUST_CRYPTO_BACKEND_SW = 2, /**< Software (Mbed TLS) */
} actrust_crypto_backend_t;

/** @brief Hash algorithm identifier. */
typedef enum {
    ACTRUST_CRYPTO_HASH_SHA256 = 1, /**< SHA-256 (32-byte digest) */
    ACTRUST_CRYPTO_HASH_SHA384 = 2, /**< SHA-384 (48-byte digest) */
    ACTRUST_CRYPTO_HASH_SHA512 = 3, /**< SHA-512 (64-byte digest) */
} actrust_crypto_hash_alg_t;

/** @brief Key type identifier. */
typedef enum {
    ACTRUST_CRYPTO_KEY_EC  = 1, /**< Elliptic curve key */
    ACTRUST_CRYPTO_KEY_AES = 2, /**< AES key */
} actrust_crypto_key_type_t;

/** @brief Elliptic curve identifier. */
typedef enum {
    ACTRUST_CRYPTO_EC_SECP256R1 = 1, /**< NIST P-256 (secp256r1) */
    ACTRUST_CRYPTO_EC_SECP256K1 = 2, /**< secp256k1 */
} actrust_crypto_ec_curve_t;

/** @brief Symmetric cipher algorithm. */
typedef enum {
    ACTRUST_CRYPTO_AES_GCM = 1, /**< AES-GCM authenticated encryption */
    ACTRUST_CRYPTO_AES_CBC = 2, /**< AES-CBC */
} actrust_crypto_sym_alg_t;

/** @brief Block cipher padding mode (applies to CBC; ignored for GCM). */
typedef enum {
    ACTRUST_CRYPTO_PAD_NONE =
        0, /**< No padding (input must be block-aligned) */
    ACTRUST_CRYPTO_PAD_PKCS7 = 1, /**< PKCS#7 padding */
    ACTRUST_CRYPTO_PAD_ZEROS = 2, /**< Zero padding */
} actrust_crypto_padding_t;

/** @brief ECDSA sign/verify input type. */
typedef enum {
    ACTRUST_CRYPTO_INPUT_MESSAGE = 0, /**< Raw message; hashed internally */
    ACTRUST_CRYPTO_INPUT_DIGEST  = 1, /**< Pre-computed digest */
} actrust_crypto_input_t;

/** @brief PEM/DER object type identifier. */
typedef enum {
    ACTRUST_CRYPTO_PEM_OBJECT_CSR =
        1, /**< PKCS#10 Certificate Signing Request */
    ACTRUST_CRYPTO_PEM_OBJECT_CERTIFICATE = 2, /**< X.509 certificate */
    ACTRUST_CRYPTO_PEM_OBJECT_PUBLIC_KEY =
        3, /**< SubjectPublicKeyInfo public key */
    ACTRUST_CRYPTO_PEM_OBJECT_PRIVATE_KEY = 4, /**< PKCS#8 private key */
} actrust_crypto_pem_object_t;

/** @brief Serialized crypto object format. */
typedef enum {
    ACTRUST_CRYPTO_FORMAT_PRIVATE_PEM = 1, /**< PKCS#8 private key PEM */
    ACTRUST_CRYPTO_FORMAT_PRIVATE_DER = 2, /**< PKCS#8 private key DER */
} actrust_crypto_format_t;

/** @brief Registered key identifiers (type defined in the key descriptor
 * table). */
typedef enum {
    ACTRUST_CRYPTO_KEY_ID_EC_0  = 0x1000, /**< EC key slot 0 (P-256) */
    ACTRUST_CRYPTO_KEY_ID_EC_1  = 0x1001, /**< EC key slot 1 (P-256) */
    ACTRUST_CRYPTO_KEY_ID_AES_0 = 0x1002, /**< AES key slot 0 (256-bit) */
} actrust_crypto_key_id_t;

/** @brief Number of entries in the key descriptor table. */
#define ACTRUST_CRYPTO_KEY_ID_COUNT 3

/** @brief Registered certificate identifiers. */
typedef enum {
    ACTRUST_CRYPTO_CERT_ID_X509_0 = 0x3000, /**< X.509 cert slot 0 */
    ACTRUST_CRYPTO_CERT_ID_X509_1 = 0x3001, /**< X.509 cert slot 1 */
} actrust_crypto_cert_id_t;

/** @brief Number of entries in the certificate descriptor table. */
#define ACTRUST_CRYPTO_CERT_ID_COUNT 2

/* ========================================================================
 * Opaque Handles
 * ======================================================================== */

/** @brief Opaque crypto context handle. */
typedef struct actrust_crypto_ctx *actrust_crypto_ctx_t;

/** @brief Opaque key handle returned by key creation functions. */
typedef struct actrust_crypto_key *actrust_crypto_key_t;

/* ========================================================================
 * Context Lifecycle
 * ======================================================================== */

/**
 * @brief Initialize the crypto context.
 *
 * Initializes the software backend and hardware adapter backend. Initialization
 * fails if either backend cannot be initialized.  Subsequent operations are
 * routed by the configured capability or key descriptor.
 *
 * @param[out] ctx  Receives a valid context handle on success.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG if @p ctx is NULL.
 * @return @c ACTRUST_ERR_NO_MEM on allocation failure.
 */
actrust_err_t actrust_crypto_init(actrust_crypto_ctx_t *ctx);

/**
 * @brief Deinitialize the crypto context and release all resources.
 *
 * @warning All key handles obtained from this context must be closed
 *          before calling this function.
 *
 * @param[in,out] ctx  Context handle pointer; set to NULL on success.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG if @p ctx is NULL.
 */
actrust_err_t actrust_crypto_deinit(actrust_crypto_ctx_t *ctx);

/* ========================================================================
 * Random Number Generation
 * ======================================================================== */

/**
 * @brief Generate cryptographically secure random bytes.
 *
 * @param[in]  ctx      Crypto context.
 * @param[out] out      Buffer to receive random bytes.
 * @param[in]  out_len  Number of bytes to generate (> 0).
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return @c ACTRUST_ERR_HW_FAILURE if the entropy source fails.
 */
actrust_err_t actrust_crypto_random(actrust_crypto_ctx_t ctx, uint8_t *out,
                                    size_t out_len);

/* ========================================================================
 * Hash
 * ======================================================================== */

/**
 * @brief Compute a cryptographic hash digest.
 *
 * @param[in]  ctx        Crypto context.
 * @param[in]  alg        Hash algorithm to use.
 * @param[in]  input      Data to hash.
 * @param[in]  input_len  Length of @p input in bytes.
 * @param[out] out        Buffer to receive the digest.
 * @param[in]  out_cap    Capacity of @p out in bytes.
 * @param[out] out_len    Receives the actual digest length.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p out_cap < digest size.
 * @return @c ACTRUST_ERR_UNSUPPORTED for unsupported hash algorithms.
 * @return @c ACTRUST_ERR_HW_FAILURE if the hash engine fails.
 */
actrust_err_t actrust_crypto_hash(actrust_crypto_ctx_t      ctx,
                                  actrust_crypto_hash_alg_t alg,
                                  const uint8_t *input, size_t input_len,
                                  uint8_t *out, size_t out_cap,
                                  size_t *out_len);

/* ========================================================================
 * Key Management
 * ======================================================================== */

/**
 * @brief Open a persistent key by its storage identifier.
 *
 * @param[in]  ctx      Crypto context.
 * @param[in]  key_id   Persistent key identifier.
 * @param[out] out_key  Receives the key handle on success.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return @c ACTRUST_ERR_NO_RESOURCE if the key does not exist.
 */
actrust_err_t actrust_crypto_key_open(actrust_crypto_ctx_t ctx, uint32_t key_id,
                                      actrust_crypto_key_t *out_key);

/**
 * @brief Close a key handle and release associated memory.
 *
 * The in-memory key material is zeroized and the handle is freed.
 * For persistent keys the data remains in storage and can be
 * re-opened via @ref actrust_crypto_key_open.
 * On success @p *key is set to NULL.
 *
 * @param[in]     ctx  Crypto context.
 * @param[in,out] key  Pointer to key handle; set to NULL on success.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG if @p ctx or @p key is NULL.
 */
actrust_err_t actrust_crypto_key_close(actrust_crypto_ctx_t  ctx,
                                       actrust_crypto_key_t *key);

/**
 * @brief Generate and persist a new key.
 *
 * The key type, algorithm, and backend are determined by the key
 * descriptor table.  The generated key is persisted under @p key_id
 * and can be re-opened later via @ref actrust_crypto_key_open.
 *
 * @param[in]  ctx      Crypto context.
 * @param[in]  key_id   Persistent storage identifier (must be registered).
 * @param[out] out_key  Receives the key handle on success.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG if @p key_id is not registered.
 * @return @c ACTRUST_ERR_NO_MEM on allocation failure.
 * @return @c ACTRUST_ERR_UNSUPPORTED for unsupported type/size/curve or backend
 * limitations.
 */
actrust_err_t actrust_crypto_key_generate(actrust_crypto_ctx_t  ctx,
                                          uint32_t              key_id,
                                          actrust_crypto_key_t *out_key);

/**
 * @brief Import and persist key material.
 *
 * The logical key descriptor table determines the expected key type and
 * backend for @p key_id. On success the key is persisted under @p key_id and
 * can be re-opened with @ref actrust_crypto_key_open. When @p out_key is not
 * NULL, it receives an open key handle that must be closed with
 * @ref actrust_crypto_key_close.
 *
 * @param[in]  ctx      Crypto context.
 * @param[in]  key_id   Registered logical key ID.
 * @param[in]  format   Input key format.
 * @param[in]  key      Input key bytes.
 * @param[in]  key_len  Length of @p key in bytes.
 * @param[out] out_key  Optional imported key handle.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments or unknown key ID.
 * @return @c ACTRUST_ERR_UNSUPPORTED when the backend cannot import keys.
 * @return @c ACTRUST_ERR_HW_FAILURE if the backend reports success but yields
 * no key handle (internal consistency failure).
 * @return Backend-specific error if the backend import fails; propagated as-is.
 */
actrust_err_t actrust_crypto_key_import(actrust_crypto_ctx_t    ctx,
                                        uint32_t                key_id,
                                        actrust_crypto_format_t format,
                                        const uint8_t *key, size_t key_len,
                                        actrust_crypto_key_t *out_key);

/**
 * @brief Permanently erase key material from persistent storage.
 *
 * Removes the key data associated with @p key_id from non-volatile
 * storage.  Any open handles to this key remain valid in memory but
 * the key will no longer be recoverable via @ref actrust_crypto_key_open
 * after this call.
 *
 * @param[in] ctx     Crypto context.
 * @param[in] key_id  Persistent storage identifier of the key to erase.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG if @p ctx is NULL or @p key_id is unknown.
 * @return @c ACTRUST_ERR_NO_RESOURCE if @p key_id does not exist in storage.
 * @return Backend-specific error if the underlying storage erase fails.
 */
actrust_err_t actrust_crypto_key_destroy(actrust_crypto_ctx_t ctx,
                                         uint32_t             key_id);

/**
 * @brief Export the public key in SubjectPublicKeyInfo DER format.
 *
 * @param[in]  ctx      Crypto context.
 * @param[in]  key      EC key handle (private or public).
 * @param[out] out      Buffer to receive the DER-encoded output.
 * @param[in]  out_cap  Capacity of @p out in bytes.
 * @param[out] out_len  Receives the actual output length.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p out_cap is insufficient.
 */
actrust_err_t actrust_crypto_key_export_public(actrust_crypto_ctx_t ctx,
                                               actrust_crypto_key_t key,
                                               uint8_t *out, size_t out_cap,
                                               size_t *out_len);

/* ========================================================================
 * Certificate Storage
 * ======================================================================== */

/**
 * @brief Persist a certificate by logical certificate ID.
 *
 * @param[in] cert_id  Registered certificate identifier.
 * @param[in] cert     Certificate bytes.
 * @param[in] cert_len Length of @p cert in bytes.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments or unknown cert ID.
 * @return @c ACTRUST_ERR_IO if the underlying storage write fails.
 */
actrust_err_t actrust_crypto_cert_write(uint32_t cert_id, const uint8_t *cert,
                                        size_t cert_len);

/**
 * @brief Load a certificate by logical certificate ID.
 *
 * @param[in]  cert_id Registered certificate identifier.
 * @param[out] out     Buffer to receive certificate bytes.
 * @param[in]  out_cap Capacity of @p out in bytes.
 * @param[out] out_len Receives the actual certificate length.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments or unknown cert ID.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p out_cap is insufficient.
 * @return @c ACTRUST_ERR_NO_RESOURCE if the certificate does not exist.
 */
actrust_err_t actrust_crypto_cert_read(uint32_t cert_id, uint8_t *out,
                                       size_t out_cap, size_t *out_len);

/**
 * @brief Delete a certificate by logical certificate ID.
 *
 * @param[in] cert_id Registered certificate identifier.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for unknown cert ID.
 * @return @c ACTRUST_ERR_NO_RESOURCE if the certificate does not exist.
 */
actrust_err_t actrust_crypto_cert_delete(uint32_t cert_id);

/* ========================================================================
 * ECDSA
 * ======================================================================== */

/**
 * @brief Create an ECDSA signature.
 *
 * @param[in]  ctx         Crypto context.
 * @param[in]  priv_key    EC private key handle.
 * @param[in]  hash_alg    Hash algorithm for signing.
 * @param[in]  input_type  Whether @p msg is raw data or a pre-computed digest.
 * @param[in]  msg         Input data.
 * @param[in]  msg_len     Length of @p msg in bytes.
 * @param[out] sig         Buffer for the DER-encoded signature.
 * @param[in]  sig_cap     Capacity of @p sig in bytes.
 * @param[out] sig_len     Receives the actual signature length.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p sig_cap is insufficient.
 * @return @c ACTRUST_ERR_UNSUPPORTED for unsupported hash algorithms.
 */
actrust_err_t actrust_crypto_ecdsa_sign(actrust_crypto_ctx_t      ctx,
                                        actrust_crypto_key_t      priv_key,
                                        actrust_crypto_hash_alg_t hash_alg,
                                        actrust_crypto_input_t    input_type,
                                        const uint8_t *msg, size_t msg_len,
                                        uint8_t *sig, size_t sig_cap,
                                        size_t *sig_len);

/**
 * @brief Verify an ECDSA signature.
 *
 * @param[in] ctx         Crypto context.
 * @param[in] pub_key     EC public key handle.
 * @param[in] hash_alg    Hash algorithm used when signing.
 * @param[in] input_type  Whether @p msg is raw data or a pre-computed digest.
 * @param[in] msg         Input data.
 * @param[in] msg_len     Length of @p msg in bytes.
 * @param[in] sig         DER-encoded signature to verify.
 * @param[in] sig_len     Length of @p sig in bytes.
 *
 * @return @c ACTRUST_OK if the signature is valid.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return @c ACTRUST_ERR_UNSUPPORTED for unsupported hash algorithms.
 * @return Module-specific error if verification fails.
 */
actrust_err_t actrust_crypto_ecdsa_verify(actrust_crypto_ctx_t      ctx,
                                          actrust_crypto_key_t      pub_key,
                                          actrust_crypto_hash_alg_t hash_alg,
                                          actrust_crypto_input_t    input_type,
                                          const uint8_t *msg, size_t msg_len,
                                          const uint8_t *sig, size_t sig_len);

/* ========================================================================
 * Symmetric Encryption (AES)
 * ======================================================================== */

/**
 * @brief Encrypt data using AES.
 *
 * For @c ACTRUST_CRYPTO_AES_GCM, provides authenticated encryption;
 * @p aad and @p tag must be provided. Tag length must be 4..16 bytes.
 * For @c ACTRUST_CRYPTO_AES_CBC, @p aad and @p tag are ignored; @p padding
 * selects whether the backend applies no padding, PKCS#7 padding, or zero
 * padding.
 *
 * @param[in]  ctx        Crypto context.
 * @param[in]  key        AES key handle.
 * @param[in]  alg        Cipher algorithm.
 * @param[in]  padding    Padding mode (applies to CBC; ignored for GCM).
 * @param[in]  iv         Initialization vector.
 * @param[in]  iv_len     Length of @p iv (12 bytes recommended for GCM).
 * @param[in]  aad        Additional authenticated data (GCM only, may be NULL).
 * @param[in]  aad_len    Length of @p aad (0 when @p aad is NULL).
 * @param[in]  input      Plaintext.
 * @param[in]  input_len  Length of @p input in bytes.
 * @param[out] output     Ciphertext buffer.
 * @param[in]  output_cap Capacity of @p output in bytes.
 * @param[out] output_len Receives the actual ciphertext length.
 * @param[out] tag        Authentication tag buffer (required for GCM).
 * @param[out] tag_len    In: requested tag length (4..16). Out: actual tag
 * length.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p output_cap is insufficient.
 * @return @c ACTRUST_ERR_UNSUPPORTED for unsupported algorithm.
 */
actrust_err_t actrust_crypto_aes_encrypt(
    actrust_crypto_ctx_t ctx, actrust_crypto_key_t key,
    actrust_crypto_sym_alg_t alg, actrust_crypto_padding_t padding,
    const uint8_t *iv, size_t iv_len, const uint8_t *aad, size_t aad_len,
    const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap,
    size_t *output_len, uint8_t *tag, size_t *tag_len);

/**
 * @brief Decrypt data using AES.
 *
 * For @c ACTRUST_CRYPTO_AES_GCM, the authentication tag is verified before
 * returning plaintext.  If verification fails the output buffer is zeroized.
 * For @c ACTRUST_CRYPTO_AES_CBC, @p aad and @p tag are ignored.
 *
 * @param[in]  ctx        Crypto context.
 * @param[in]  key        AES key handle.
 * @param[in]  alg        Cipher algorithm.
 * @param[in]  padding    Padding mode (applies to CBC; ignored for GCM).
 * @param[in]  iv         Initialization vector used during encryption.
 * @param[in]  iv_len     Length of @p iv.
 * @param[in]  aad        Additional authenticated data (GCM only, may be NULL).
 * @param[in]  aad_len    Length of @p aad (0 when @p aad is NULL).
 * @param[in]  input      Ciphertext.
 * @param[in]  input_len  Length of @p input in bytes.
 * @param[out] output     Plaintext buffer.
 * @param[in]  output_cap Capacity of @p output in bytes.
 * @param[out] output_len Receives the actual plaintext length.
 * @param[in]  tag        Authentication tag to verify (required for GCM).
 * @param[in]  tag_len    Length of @p tag in bytes (4..16 for GCM).
 *
 * @return @c ACTRUST_OK on success (and tag verified for GCM).
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p output_cap is insufficient.
 * @return Module-specific error if GCM authentication fails.
 */
actrust_err_t actrust_crypto_aes_decrypt(
    actrust_crypto_ctx_t ctx, actrust_crypto_key_t key,
    actrust_crypto_sym_alg_t alg, actrust_crypto_padding_t padding,
    const uint8_t *iv, size_t iv_len, const uint8_t *aad, size_t aad_len,
    const uint8_t *input, size_t input_len, uint8_t *output, size_t output_cap,
    size_t *output_len, const uint8_t *tag, size_t tag_len);

/* ========================================================================
 * ASN.1 DER / PEM Conversion
 * ======================================================================== */

/**
 * @brief Convert ASN.1 DER bytes to PEM text.
 *
 * Writes a NUL-terminated PEM string to @p pem_out.
 *
 * @param[in]  object   DER/PEM object type.
 * @param[in]  der      DER input bytes.
 * @param[in]  der_len  DER input length in bytes.
 * @param[out] pem_out  Buffer to receive PEM text.
 * @param[in]  pem_cap  Capacity of @p pem_out in bytes.
 * @param[out] pem_len  Receives PEM text length in bytes (excluding trailing
 * NUL).
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments or an unknown
 * @p object type.
 * @return @c ACTRUST_ERR_IO if PEM encoding fails, including when @p pem_cap
 * is insufficient.
 */
actrust_err_t actrust_crypto_der_to_pem(actrust_crypto_pem_object_t object,
                                        const uint8_t *der, size_t der_len,
                                        char *pem_out, size_t pem_cap,
                                        size_t *pem_len);

/**
 * @brief Convert PEM text to ASN.1 DER bytes.
 *
 * @param[in]  object   DER/PEM object type.
 * @param[in]  pem      PEM text input (NUL-termination not required).
 * @param[in]  pem_len  PEM text length in bytes.
 * @param[out] der_out  Buffer to receive DER bytes.
 * @param[in]  der_cap  Capacity of @p der_out in bytes.
 * @param[out] der_len  Receives DER length in bytes.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments or malformed PEM
 * text.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p der_cap is insufficient.
 * @return @c ACTRUST_ERR_NO_MEM on allocation failure.
 * @return Module-specific error on decoding failure.
 */
actrust_err_t actrust_crypto_pem_to_der(actrust_crypto_pem_object_t object,
                                        const char *pem, size_t pem_len,
                                        uint8_t *der_out, size_t der_cap,
                                        size_t *der_len);

/* ========================================================================
 * Certificate Signing Request (CSR)
 * ======================================================================== */

/**
 * @brief Generate a PKCS#10 Certificate Signing Request.
 *
 * @param[in]  ctx       Crypto context.
 * @param[in]  key       EC private key handle (used for signing the CSR).
 * @param[in]  hash_alg  Hash algorithm for the CSR signature.
 * @param[in]  subject   Subject distinguished name, comma-separated
 *                        (e.g. "CN=device01,O=AntChainTrustSDK,C=CN").
 * @param[out] out       Buffer to receive the DER-encoded CSR.
 * @param[in]  out_cap   Capacity of @p out in bytes.
 * @param[out] out_len   Receives the actual CSR length.
 *
 * @return @c ACTRUST_OK on success.
 * @return @c ACTRUST_ERR_INVALID_ARG for invalid arguments or malformed
 * subject.
 * @return @c ACTRUST_ERR_BUF_TOO_SMALL if @p out_cap is insufficient.
 * @return @c ACTRUST_ERR_NO_MEM on allocation failure.
 */
actrust_err_t actrust_crypto_csr_generate(actrust_crypto_ctx_t      ctx,
                                          actrust_crypto_key_t      key,
                                          actrust_crypto_hash_alg_t hash_alg,
                                          const char *subject, uint8_t *out,
                                          size_t out_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* ACTRUST_CRYPTO_H */
