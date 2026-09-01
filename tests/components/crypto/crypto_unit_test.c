// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdint.h>
#include <string.h>

/* Third-party */
#include "unity.h"
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>

/* Crypto */
#include "crypto/crypto_internal.h"

static actrust_crypto_ctx_t ctx;

#define TEST_PRIVATE_DER_MAX 512u

static struct actrust_crypto_key fake_import_key;

static actrust_err_t fake_key_import(actrust_crypto_ctx_t ctx, uint32_t key_id,
                                     actrust_crypto_format_t format,
                                     const uint8_t *key, size_t key_len,
                                     actrust_crypto_key_t *out_key)
{
    (void) ctx;
    (void) key_id;
    (void) format;
    (void) key;
    (void) key_len;

    *out_key = &fake_import_key;
    return ACTRUST_OK;
}

static actrust_err_t fake_key_close_fails(actrust_crypto_ctx_t  ctx,
                                          actrust_crypto_key_t *key)
{
    (void) ctx;

    if (key != NULL) {
        *key = NULL;
    }
    return ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_CRYPTO,
                       ACTRUST_ERR_HW_FAILURE);
}

static const crypto_backend_ops_t fake_import_ops = {
    .key_close  = fake_key_close_fails,
    .key_import = fake_key_import,
};

static size_t test_make_p256_private_der(uint8_t *out, size_t out_cap)
{
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context       pk;
    uint8_t                  tmp[TEST_PRIVATE_DER_MAX];
    const char               pers[] = "actrust_key_import_test";

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_pk_init(&pk);

    TEST_ASSERT_EQUAL(0, mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func,
                                               &entropy, (const uint8_t *) pers,
                                               sizeof(pers) - 1u));
    TEST_ASSERT_EQUAL(
        0, mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)));
    TEST_ASSERT_EQUAL(
        0, mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk),
                               mbedtls_ctr_drbg_random, &ctr_drbg));

    int ret = mbedtls_pk_write_key_der(&pk, tmp, sizeof(tmp));
    TEST_ASSERT_GREATER_THAN(0, ret);

    size_t der_len = (size_t) ret;
    TEST_ASSERT_LESS_OR_EQUAL(out_cap, der_len);
    memcpy(out, tmp + sizeof(tmp) - der_len, der_len);

    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    return der_len;
}

void setUp(void)
{
    ctx = NULL;
    actrust_crypto_init(&ctx);
}

void tearDown(void)
{
    if (ctx != NULL) {
        actrust_crypto_key_destroy(ctx, ACTRUST_CRYPTO_KEY_ID_EC_0);
        (void) actrust_crypto_cert_delete(ACTRUST_CRYPTO_CERT_ID_X509_0);
        actrust_crypto_deinit(&ctx);
    }
}

/* --- init / deinit --- */

void test_init_null_ctx(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_crypto_init(NULL));
}

void test_deinit_null_ctx(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_crypto_deinit(NULL));
}

/* --- random --- */

void test_random_null_buf(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_crypto_random(ctx, NULL, 16));
}

void test_random_nonzero(void)
{
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_random(ctx, buf, sizeof(buf)));

    int nonzero = 0;
    for (size_t i = 0; i < sizeof(buf); i++) {
        if (buf[i] != 0) {
            nonzero = 1;
        }
    }
    TEST_ASSERT_TRUE(nonzero);
}

void test_random_large_request(void)
{
    uint8_t buf[2048];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_random(ctx, buf, sizeof(buf)));
}

/* --- identity object mapping --- */

void test_cert_id_maps_to_cert_slot(void)
{
    TEST_ASSERT_EQUAL_UINT32(ACTRUST_SEC_SLOT_CERT(0),
                             certid_to_slotid(ACTRUST_CRYPTO_CERT_ID_X509_0));
    TEST_ASSERT_EQUAL_UINT32(ACTRUST_SEC_SLOT_CERT(1),
                             certid_to_slotid(ACTRUST_CRYPTO_CERT_ID_X509_1));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, certid_to_slotid(0xFFFFFFFFu));
}

void test_key_roles_use_distinct_slots(void)
{
    const actrust_crypto_key_desc_t *claim =
        actrust_crypto_key_lookup(ACTRUST_CRYPTO_KEY_ID_EC_0);
    const actrust_crypto_key_desc_t *runtime =
        actrust_crypto_key_lookup(ACTRUST_CRYPTO_KEY_ID_EC_1);
    const actrust_crypto_key_desc_t *signing =
        actrust_crypto_key_lookup(ACTRUST_CRYPTO_KEY_ID_EC_2);

    TEST_ASSERT_NOT_NULL(claim);
    TEST_ASSERT_NOT_NULL(runtime);
    TEST_ASSERT_NOT_NULL(signing);
    TEST_ASSERT_EQUAL_UINT8(0u, claim->slot_index);
    TEST_ASSERT_EQUAL_UINT8(1u, runtime->slot_index);
    TEST_ASSERT_EQUAL_UINT8(3u, signing->slot_index);
    TEST_ASSERT_NOT_EQUAL(claim->slot_index, runtime->slot_index);
    TEST_ASSERT_NOT_EQUAL(claim->slot_index, signing->slot_index);
    TEST_ASSERT_NOT_EQUAL(runtime->slot_index, signing->slot_index);
    TEST_ASSERT_EQUAL(ACTRUST_CRYPTO_BACKEND_SW, claim->backend);
    TEST_ASSERT_EQUAL(ACTRUST_CRYPTO_BACKEND_SW, runtime->backend);
    TEST_ASSERT_EQUAL(ACTRUST_CRYPTO_BACKEND_SW, signing->backend);
}

void test_security_capabilities_match_reference_adapter(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, actrust_sec_get_capabilities());
}

void test_cert_storage_roundtrip(void)
{
    const uint8_t cert[] = "-----BEGIN CERTIFICATE-----\nA\n";
    uint8_t       out[sizeof(cert)];
    size_t        out_len = 0u;

    (void) actrust_crypto_cert_delete(ACTRUST_CRYPTO_CERT_ID_X509_0);

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_cert_write(ACTRUST_CRYPTO_CERT_ID_X509_0,
                                                cert, sizeof(cert)));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_cert_read(ACTRUST_CRYPTO_CERT_ID_X509_0,
                                               out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL_UINT(sizeof(cert), out_len);
    TEST_ASSERT_EQUAL_MEMORY(cert, out, sizeof(cert));
    TEST_ASSERT_EQUAL(
        ACTRUST_OK, actrust_crypto_cert_delete(ACTRUST_CRYPTO_CERT_ID_X509_0));
}

void test_cert_storage_rejects_invalid_args(void)
{
    uint8_t out[8];
    size_t  out_len = 0u;

    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_crypto_cert_write(0xFFFFFFFFu, out, sizeof(out)));
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK,
        actrust_crypto_cert_write(ACTRUST_CRYPTO_CERT_ID_X509_0, NULL, 1u));
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK,
        actrust_crypto_cert_read(0xFFFFFFFFu, out, sizeof(out), &out_len));
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_crypto_cert_delete(0xFFFFFFFFu));
}

/* --- hash: all three algorithms --- */

void test_hash_sha256(void)
{
    const uint8_t msg[] = "abc";
    uint8_t       out[64];
    size_t        out_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_hash(ctx, ACTRUST_CRYPTO_HASH_SHA256, msg,
                                          3, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL(32u, out_len);
}

void test_hash_sha384(void)
{
    const uint8_t msg[] = "abc";
    uint8_t       out[64];
    size_t        out_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_hash(ctx, ACTRUST_CRYPTO_HASH_SHA384, msg,
                                          3, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL(48u, out_len);
}

void test_hash_sha512(void)
{
    const uint8_t msg[] = "abc";
    uint8_t       out[64];
    size_t        out_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_hash(ctx, ACTRUST_CRYPTO_HASH_SHA512, msg,
                                          3, out, sizeof(out), &out_len));
    TEST_ASSERT_EQUAL(64u, out_len);
}

void test_hash_null_input(void)
{
    uint8_t out[32];
    size_t  out_len = 0;
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_crypto_hash(ctx, ACTRUST_CRYPTO_HASH_SHA256, NULL,
                                        10, out, sizeof(out), &out_len));
}

void test_hash_buffer_too_small(void)
{
    const uint8_t msg[] = "abc";
    uint8_t       out[16];
    size_t        out_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL,
                      ACTRUST_ERR_CODE(actrust_crypto_hash(
                          ctx, ACTRUST_CRYPTO_HASH_SHA256, msg, 3, out,
                          sizeof(out), &out_len)));
}

void test_hash_rejects_unsupported_algorithm(void)
{
    const uint8_t msg[] = "abc";
    uint8_t       out[64];
    size_t        out_len = 0;

    TEST_ASSERT_EQUAL(ACTRUST_ERR_UNSUPPORTED,
                      ACTRUST_ERR_CODE(actrust_crypto_hash(
                          ctx, (actrust_crypto_hash_alg_t) 0xFFu, msg, 3, out,
                          sizeof(out), &out_len)));
}

/* --- key lifecycle --- */

void test_key_generate_and_open(void)
{
    actrust_crypto_key_t key = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_generate(
                                      ctx, ACTRUST_CRYPTO_KEY_ID_EC_0, &key));
    TEST_ASSERT_NOT_NULL(key);

    actrust_crypto_key_close(ctx, &key);
    TEST_ASSERT_NULL(key);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_open(
                                      ctx, ACTRUST_CRYPTO_KEY_ID_EC_0, &key));
    TEST_ASSERT_NOT_NULL(key);

    actrust_crypto_key_close(ctx, &key);
}

void test_key_import_private_der_and_open(void)
{
    uint8_t der[TEST_PRIVATE_DER_MAX];
    size_t  der_len = test_make_p256_private_der(der, sizeof(der));

    actrust_crypto_key_t key = NULL;
    TEST_ASSERT_EQUAL(
        ACTRUST_OK, actrust_crypto_key_import(ctx, ACTRUST_CRYPTO_KEY_ID_EC_0,
                                              ACTRUST_CRYPTO_FORMAT_PRIVATE_DER,
                                              der, der_len, &key));
    TEST_ASSERT_NOT_NULL(key);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(ctx, &key));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_open(
                                      ctx, ACTRUST_CRYPTO_KEY_ID_EC_0, &key));
    uint8_t public_der[128];
    size_t  public_der_len = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_export_public(
                                      ctx, key, public_der, sizeof(public_der),
                                      &public_der_len));
    TEST_ASSERT_GREATER_THAN(0u, public_der_len);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(ctx, &key));
}

void test_key_import_private_der_without_handle_persists(void)
{
    uint8_t der[TEST_PRIVATE_DER_MAX];
    size_t  der_len = test_make_p256_private_der(der, sizeof(der));

    TEST_ASSERT_EQUAL(
        ACTRUST_OK, actrust_crypto_key_import(ctx, ACTRUST_CRYPTO_KEY_ID_EC_0,
                                              ACTRUST_CRYPTO_FORMAT_PRIVATE_DER,
                                              der, der_len, NULL));

    actrust_crypto_key_t key = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_open(
                                      ctx, ACTRUST_CRYPTO_KEY_ID_EC_0, &key));
    TEST_ASSERT_NOT_NULL(key);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(ctx, &key));
}

void test_key_import_without_handle_ignores_close_failure(void)
{
    const crypto_backend_ops_t *saved_sw = ctx->sw;
    const uint8_t               key[]    = { 0x01u };

    ctx->sw           = &fake_import_ops;
    actrust_err_t err = actrust_crypto_key_import(
        ctx, ACTRUST_CRYPTO_KEY_ID_EC_0, ACTRUST_CRYPTO_FORMAT_PRIVATE_DER, key,
        sizeof(key), NULL);
    ctx->sw = saved_sw;

    TEST_ASSERT_EQUAL(ACTRUST_OK, err);
}

void test_key_import_rejects_invalid_args(void)
{
    uint8_t der[TEST_PRIVATE_DER_MAX];
    size_t  der_len = test_make_p256_private_der(der, sizeof(der));

    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_crypto_key_import(NULL, ACTRUST_CRYPTO_KEY_ID_EC_0,
                                              ACTRUST_CRYPTO_FORMAT_PRIVATE_DER,
                                              der, der_len, NULL));
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_crypto_key_import(ctx, 0xFFFFFFFFu,
                                              ACTRUST_CRYPTO_FORMAT_PRIVATE_DER,
                                              der, der_len, NULL));
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_crypto_key_import(ctx, ACTRUST_CRYPTO_KEY_ID_AES_0,
                                              ACTRUST_CRYPTO_FORMAT_PRIVATE_DER,
                                              der, der_len, NULL));
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_crypto_key_import(ctx, ACTRUST_CRYPTO_KEY_ID_EC_0,
                                              (actrust_crypto_format_t) 0u, der,
                                              der_len, NULL));
}

void test_key_open_nonexistent(void)
{
    actrust_crypto_key_destroy(ctx, ACTRUST_CRYPTO_KEY_ID_EC_0);

    actrust_crypto_key_t key = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_NO_RESOURCE,
                      ACTRUST_ERR_CODE(actrust_crypto_key_open(
                          ctx, ACTRUST_CRYPTO_KEY_ID_EC_0, &key)));
    TEST_ASSERT_NULL(key);
}

void test_key_invalid_id_clears_output(void)
{
    actrust_crypto_key_t key = (actrust_crypto_key_t) (uintptr_t) 1u;

    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_INVALID_ARG,
        ACTRUST_ERR_CODE(actrust_crypto_key_open(ctx, 0xFFFFFFFFu, &key)));
    TEST_ASSERT_NULL(key);

    key = (actrust_crypto_key_t) (uintptr_t) 1u;
    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_INVALID_ARG,
        ACTRUST_ERR_CODE(actrust_crypto_key_generate(ctx, 0xFFFFFFFFu, &key)));
    TEST_ASSERT_NULL(key);
}

void test_key_destroy_nonexistent(void)
{
    actrust_crypto_key_destroy(ctx, ACTRUST_CRYPTO_KEY_ID_EC_0);

    TEST_ASSERT_EQUAL(ACTRUST_ERR_NO_RESOURCE,
                      ACTRUST_ERR_CODE(actrust_crypto_key_destroy(
                          ctx, ACTRUST_CRYPTO_KEY_ID_EC_0)));
}

void test_key_export_public(void)
{
    actrust_crypto_key_t key = NULL;
    actrust_crypto_key_generate(ctx, ACTRUST_CRYPTO_KEY_ID_EC_0, &key);

    uint8_t pub[128];
    size_t  pub_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_export_public(
                                      ctx, key, pub, sizeof(pub), &pub_len));
    TEST_ASSERT_GREATER_THAN(0u, pub_len);

    actrust_crypto_key_close(ctx, &key);
}

void test_key_export_buffer_too_small(void)
{
    actrust_crypto_key_t key = NULL;
    actrust_crypto_key_generate(ctx, ACTRUST_CRYPTO_KEY_ID_EC_0, &key);

    uint8_t pub[4];
    size_t  pub_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BUF_TOO_SMALL,
                      ACTRUST_ERR_CODE(actrust_crypto_key_export_public(
                          ctx, key, pub, sizeof(pub), &pub_len)));
    actrust_crypto_key_close(ctx, &key);
}

/* --- ECDSA sign / verify --- */

void test_ecdsa_sign_verify(void)
{
    actrust_crypto_key_t key = NULL;
    actrust_crypto_key_generate(ctx, ACTRUST_CRYPTO_KEY_ID_EC_0, &key);

    const uint8_t msg[] = "sign me";
    uint8_t       sig[128];
    size_t        sig_len = 0;
    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_crypto_ecdsa_sign(ctx, key, ACTRUST_CRYPTO_HASH_SHA256,
                                  ACTRUST_CRYPTO_INPUT_MESSAGE, msg,
                                  sizeof(msg) - 1, sig, sizeof(sig), &sig_len));
    TEST_ASSERT_GREATER_THAN(0u, sig_len);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_ecdsa_verify(
                                      ctx, key, ACTRUST_CRYPTO_HASH_SHA256,
                                      ACTRUST_CRYPTO_INPUT_MESSAGE, msg,
                                      sizeof(msg) - 1, sig, sig_len));

    actrust_crypto_key_close(ctx, &key);
}

void test_ecdsa_verify_tampered(void)
{
    actrust_crypto_key_t key = NULL;
    actrust_crypto_key_generate(ctx, ACTRUST_CRYPTO_KEY_ID_EC_0, &key);

    const uint8_t msg[] = "data";
    uint8_t       sig[128];
    size_t        sig_len = 0;
    actrust_crypto_ecdsa_sign(ctx, key, ACTRUST_CRYPTO_HASH_SHA256,
                              ACTRUST_CRYPTO_INPUT_MESSAGE, msg, 4, sig,
                              sizeof(sig), &sig_len);

    sig[sig_len / 2] ^= 0xFF;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_crypto_ecdsa_verify(
                                          ctx, key, ACTRUST_CRYPTO_HASH_SHA256,
                                          ACTRUST_CRYPTO_INPUT_MESSAGE, msg, 4,
                                          sig, sig_len));
    actrust_crypto_key_close(ctx, &key);
}

/* --- AES-GCM roundtrip --- */

void test_aes_gcm_encrypt_decrypt(void)
{
    actrust_crypto_key_t key = NULL;
    actrust_crypto_key_generate(ctx, ACTRUST_CRYPTO_KEY_ID_AES_0, &key);

    const uint8_t iv[12] = { 0 };
    const uint8_t pt[]   = "secret plaintext";
    uint8_t       ct[64], pt2[64], tag[16];
    size_t        ct_len = 0, pt2_len = 0, tag_len = 16;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_aes_encrypt(
                                      ctx, key, ACTRUST_CRYPTO_AES_GCM,
                                      ACTRUST_CRYPTO_PAD_NONE, iv, sizeof(iv),
                                      NULL, 0, pt, sizeof(pt) - 1, ct,
                                      sizeof(ct), &ct_len, tag, &tag_len));

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_aes_decrypt(
                          ctx, key, ACTRUST_CRYPTO_AES_GCM,
                          ACTRUST_CRYPTO_PAD_NONE, iv, sizeof(iv), NULL, 0, ct,
                          ct_len, pt2, sizeof(pt2), &pt2_len, tag, tag_len));

    TEST_ASSERT_EQUAL(sizeof(pt) - 1, pt2_len);
    TEST_ASSERT_EQUAL_MEMORY(pt, pt2, pt2_len);

    actrust_crypto_key_close(ctx, &key);
    actrust_crypto_key_destroy(ctx, ACTRUST_CRYPTO_KEY_ID_AES_0);
}

void test_aes_gcm_tampered_tag(void)
{
    actrust_crypto_key_t key = NULL;
    actrust_crypto_key_generate(ctx, ACTRUST_CRYPTO_KEY_ID_AES_0, &key);

    const uint8_t iv[12] = { 0 };
    const uint8_t pt[]   = "data";
    uint8_t       ct[32], pt2[32], tag[16];
    size_t        ct_len = 0, pt2_len = 0, tag_len = 16;

    actrust_crypto_aes_encrypt(ctx, key, ACTRUST_CRYPTO_AES_GCM,
                               ACTRUST_CRYPTO_PAD_NONE, iv, sizeof(iv), NULL, 0,
                               pt, 4, ct, sizeof(ct), &ct_len, tag, &tag_len);

    tag[0] ^= 0xFF;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_crypto_aes_decrypt(
                                          ctx, key, ACTRUST_CRYPTO_AES_GCM,
                                          ACTRUST_CRYPTO_PAD_NONE, iv,
                                          sizeof(iv), NULL, 0, ct, ct_len, pt2,
                                          sizeof(pt2), &pt2_len, tag, tag_len));
    actrust_crypto_key_close(ctx, &key);
    actrust_crypto_key_destroy(ctx, ACTRUST_CRYPTO_KEY_ID_AES_0);
}

void test_aes_gcm_rejects_invalid_tag_length(void)
{
    actrust_crypto_key_t key = NULL;
    actrust_crypto_key_generate(ctx, ACTRUST_CRYPTO_KEY_ID_AES_0, &key);

    const uint8_t iv[12] = { 0 };
    const uint8_t pt[]   = "data";
    uint8_t       ct[32], tag[16];
    size_t        ct_len = 0, tag_len = 3;

    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_INVALID_ARG,
        ACTRUST_ERR_CODE(actrust_crypto_aes_encrypt(
            ctx, key, ACTRUST_CRYPTO_AES_GCM, ACTRUST_CRYPTO_PAD_NONE, iv,
            sizeof(iv), NULL, 0, pt, sizeof(pt) - 1u, ct, sizeof(ct), &ct_len,
            tag, &tag_len)));

    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG,
                      ACTRUST_ERR_CODE(actrust_crypto_aes_decrypt(
                          ctx, key, ACTRUST_CRYPTO_AES_GCM,
                          ACTRUST_CRYPTO_PAD_NONE, iv, sizeof(iv), NULL, 0, ct,
                          sizeof(pt) - 1u, ct, sizeof(ct), &ct_len, tag, 17u)));

    actrust_crypto_key_close(ctx, &key);
    actrust_crypto_key_destroy(ctx, ACTRUST_CRYPTO_KEY_ID_AES_0);
}

void test_aes_rejects_unsupported_algorithm_before_buffer_size(void)
{
    actrust_crypto_key_t key = NULL;
    actrust_crypto_key_generate(ctx, ACTRUST_CRYPTO_KEY_ID_AES_0, &key);

    const uint8_t iv[12] = { 0 };
    const uint8_t ct[4]  = { 0 };
    uint8_t       out[1];
    size_t        out_len = 0;

    TEST_ASSERT_EQUAL(ACTRUST_ERR_UNSUPPORTED,
                      ACTRUST_ERR_CODE(actrust_crypto_aes_decrypt(
                          ctx, key, (actrust_crypto_sym_alg_t) 0xFFu,
                          ACTRUST_CRYPTO_PAD_NONE, iv, sizeof(iv), NULL, 0, ct,
                          sizeof(ct), out, 0u, &out_len, NULL, 0u)));

    actrust_crypto_key_close(ctx, &key);
    actrust_crypto_key_destroy(ctx, ACTRUST_CRYPTO_KEY_ID_AES_0);
}

void test_sw_aes_gcm_decrypt_rejects_null_tag_before_buffer_size(void)
{
    actrust_crypto_key_t key = NULL;
    actrust_crypto_key_generate(ctx, ACTRUST_CRYPTO_KEY_ID_AES_0, &key);

    const uint8_t iv[12] = { 0 };
    const uint8_t ct[4]  = { 0 };
    uint8_t       out[1];
    size_t        out_len = 0;

    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_INVALID_ARG,
        ACTRUST_ERR_CODE(key->ops->aes_decrypt(
            ctx, key, ACTRUST_CRYPTO_AES_GCM, ACTRUST_CRYPTO_PAD_NONE, iv,
            sizeof(iv), NULL, 0, ct, sizeof(ct), out, 0u, &out_len, NULL, 0u)));

    actrust_crypto_key_close(ctx, &key);
    actrust_crypto_key_destroy(ctx, ACTRUST_CRYPTO_KEY_ID_AES_0);
}

/* --- DER / PEM --- */

void test_der_to_pem_null_args(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_crypto_der_to_pem(
                                          ACTRUST_CRYPTO_PEM_OBJECT_CERTIFICATE,
                                          NULL, 0, NULL, 0, NULL));
}

void test_pem_to_der_null_args(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_crypto_pem_to_der(
                                          ACTRUST_CRYPTO_PEM_OBJECT_CERTIFICATE,
                                          NULL, 0, NULL, 0, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_null_ctx);
    RUN_TEST(test_deinit_null_ctx);
    RUN_TEST(test_random_null_buf);
    RUN_TEST(test_random_nonzero);
    RUN_TEST(test_random_large_request);
    RUN_TEST(test_cert_id_maps_to_cert_slot);
    RUN_TEST(test_key_roles_use_distinct_slots);
    RUN_TEST(test_security_capabilities_match_reference_adapter);
    RUN_TEST(test_cert_storage_roundtrip);
    RUN_TEST(test_cert_storage_rejects_invalid_args);
    RUN_TEST(test_hash_sha256);
    RUN_TEST(test_hash_sha384);
    RUN_TEST(test_hash_sha512);
    RUN_TEST(test_hash_null_input);
    RUN_TEST(test_hash_buffer_too_small);
    RUN_TEST(test_hash_rejects_unsupported_algorithm);
    RUN_TEST(test_key_generate_and_open);
    RUN_TEST(test_key_import_private_der_and_open);
    RUN_TEST(test_key_import_private_der_without_handle_persists);
    RUN_TEST(test_key_import_without_handle_ignores_close_failure);
    RUN_TEST(test_key_import_rejects_invalid_args);
    RUN_TEST(test_key_open_nonexistent);
    RUN_TEST(test_key_invalid_id_clears_output);
    RUN_TEST(test_key_destroy_nonexistent);
    RUN_TEST(test_key_export_public);
    RUN_TEST(test_key_export_buffer_too_small);
    RUN_TEST(test_ecdsa_sign_verify);
    RUN_TEST(test_ecdsa_verify_tampered);
    RUN_TEST(test_aes_gcm_encrypt_decrypt);
    RUN_TEST(test_aes_gcm_tampered_tag);
    RUN_TEST(test_aes_gcm_rejects_invalid_tag_length);
    RUN_TEST(test_aes_rejects_unsupported_algorithm_before_buffer_size);
    RUN_TEST(test_sw_aes_gcm_decrypt_rejects_null_tag_before_buffer_size);
    RUN_TEST(test_der_to_pem_null_args);
    RUN_TEST(test_pem_to_der_null_args);
    return UNITY_END();
}
