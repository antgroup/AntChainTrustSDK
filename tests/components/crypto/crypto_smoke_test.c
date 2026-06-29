// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdint.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Crypto */
#include "crypto/crypto.h"

static actrust_crypto_ctx_t ctx = NULL;

#define TEST_KEY_ID_EC  ACTRUST_CRYPTO_KEY_ID_EC_0
#define TEST_KEY_ID_EC2 ACTRUST_CRYPTO_KEY_ID_EC_1
#define TEST_KEY_ID_AES ACTRUST_CRYPTO_KEY_ID_AES_0

void setUp(void)
{
}
void tearDown(void)
{
}

void test_crypto_init_deinit(void)
{
    actrust_crypto_ctx_t tmp = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_init(&tmp));
    TEST_ASSERT_NOT_NULL(tmp);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_deinit(&tmp));
    TEST_ASSERT_NULL(tmp);
}

void test_crypto_random(void)
{
    uint8_t buf[32] = { 0 };
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_random(ctx, buf, sizeof(buf)));

    int all_zero = 1;
    for (size_t i = 0; i < sizeof(buf); ++i) {
        if (buf[i] != 0) {
            all_zero = 0;
            break;
        }
    }
    TEST_ASSERT_FALSE(all_zero);
}

void test_crypto_hash_sha256(void)
{
    static const uint8_t expected[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4,
        0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b,
        0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
    };

    uint8_t digest[64];
    size_t  digest_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_hash(ctx, ACTRUST_CRYPTO_HASH_SHA256,
                                          (const uint8_t *) "", 0, digest,
                                          sizeof(digest), &digest_len));
    TEST_ASSERT_EQUAL(32u, digest_len);
    TEST_ASSERT_EQUAL_MEMORY(expected, digest, 32);
}

void test_crypto_key_management(void)
{
    actrust_crypto_key_t key = NULL;
    (void) actrust_crypto_key_destroy(ctx, TEST_KEY_ID_EC);
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_generate(ctx, TEST_KEY_ID_EC, &key));

    uint8_t pub_orig[256];
    size_t  pub_orig_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_export_public(
                          ctx, key, pub_orig, sizeof(pub_orig), &pub_orig_len));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(ctx, &key));

    actrust_crypto_key_t loaded = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_open(ctx, TEST_KEY_ID_EC, &loaded));

    uint8_t pub_loaded[256];
    size_t  pub_loaded_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_export_public(
                                      ctx, loaded, pub_loaded,
                                      sizeof(pub_loaded), &pub_loaded_len));
    TEST_ASSERT_EQUAL(pub_orig_len, pub_loaded_len);
    TEST_ASSERT_EQUAL_MEMORY(pub_orig, pub_loaded, pub_orig_len);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(ctx, &loaded));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_destroy(ctx, TEST_KEY_ID_EC));

    actrust_crypto_key_t gone = NULL;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_crypto_key_open(ctx, TEST_KEY_ID_EC, &gone));
}

void test_crypto_ec_keygen(void)
{
    actrust_crypto_key_t key = NULL;
    (void) actrust_crypto_key_destroy(ctx, TEST_KEY_ID_EC);
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_generate(ctx, TEST_KEY_ID_EC, &key));
    TEST_ASSERT_NOT_NULL(key);

    uint8_t pub[256];
    size_t  pub_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_export_public(
                                      ctx, key, pub, sizeof(pub), &pub_len));
    TEST_ASSERT_GREATER_THAN(0u, pub_len);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(ctx, &key));
    TEST_ASSERT_NULL(key);
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_destroy(ctx, TEST_KEY_ID_EC));
}

void test_crypto_ecdsa(void)
{
    actrust_crypto_key_t key = NULL;
    (void) actrust_crypto_key_destroy(ctx, TEST_KEY_ID_EC2);
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_generate(ctx, TEST_KEY_ID_EC2, &key));

    static const uint8_t msg[] = "test message for ecdsa";
    uint8_t              sig[128];
    size_t               sig_len = 0;

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

    sig[0] ^= 0xFF;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_crypto_ecdsa_verify(
                                          ctx, key, ACTRUST_CRYPTO_HASH_SHA256,
                                          ACTRUST_CRYPTO_INPUT_MESSAGE, msg,
                                          sizeof(msg) - 1, sig, sig_len));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(ctx, &key));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_destroy(ctx, TEST_KEY_ID_EC2));
}

void test_crypto_aes_gcm(void)
{
    actrust_crypto_key_t key = NULL;
    (void) actrust_crypto_key_destroy(ctx, TEST_KEY_ID_AES);
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_generate(ctx, TEST_KEY_ID_AES, &key));

    static const uint8_t plaintext[] = "hello aes-gcm!";
    static const uint8_t aad[]       = "additional data";
    uint8_t              iv[12];
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_random(ctx, iv, sizeof(iv)));

    uint8_t ciphertext[64];
    size_t  ct_len = 0;
    uint8_t tag[16];
    size_t  tag_len = sizeof(tag);

    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_crypto_aes_encrypt(
            ctx, key, ACTRUST_CRYPTO_AES_GCM, ACTRUST_CRYPTO_PAD_NONE, iv,
            sizeof(iv), aad, sizeof(aad) - 1, plaintext, sizeof(plaintext) - 1,
            ciphertext, sizeof(ciphertext), &ct_len, tag, &tag_len));
    TEST_ASSERT_EQUAL(sizeof(plaintext) - 1, ct_len);

    uint8_t decrypted[64];
    size_t  pt_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_aes_decrypt(
                          ctx, key, ACTRUST_CRYPTO_AES_GCM,
                          ACTRUST_CRYPTO_PAD_NONE, iv, sizeof(iv), aad,
                          sizeof(aad) - 1, ciphertext, ct_len, decrypted,
                          sizeof(decrypted), &pt_len, tag, tag_len));
    TEST_ASSERT_EQUAL(sizeof(plaintext) - 1, pt_len);
    TEST_ASSERT_EQUAL_MEMORY(plaintext, decrypted, pt_len);

    tag[0] ^= 0xFF;
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_crypto_aes_decrypt(
                              ctx, key, ACTRUST_CRYPTO_AES_GCM,
                              ACTRUST_CRYPTO_PAD_NONE, iv, sizeof(iv), aad,
                              sizeof(aad) - 1, ciphertext, ct_len, decrypted,
                              sizeof(decrypted), &pt_len, tag, tag_len));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(ctx, &key));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_destroy(ctx, TEST_KEY_ID_AES));
}

void test_crypto_aes_cbc(void)
{
    actrust_crypto_key_t key = NULL;
    (void) actrust_crypto_key_destroy(ctx, TEST_KEY_ID_AES);
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_generate(ctx, TEST_KEY_ID_AES, &key));

    static const uint8_t plaintext[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    uint8_t iv[16];
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_random(ctx, iv, sizeof(iv)));

    uint8_t ciphertext[16];
    size_t  ct_len = 0;

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_aes_encrypt(
                          ctx, key, ACTRUST_CRYPTO_AES_CBC,
                          ACTRUST_CRYPTO_PAD_NONE, iv, sizeof(iv), NULL, 0,
                          plaintext, sizeof(plaintext), ciphertext,
                          sizeof(ciphertext), &ct_len, NULL, NULL));
    TEST_ASSERT_EQUAL(sizeof(plaintext), ct_len);

    uint8_t decrypted[16];
    size_t  pt_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_aes_decrypt(
                                      ctx, key, ACTRUST_CRYPTO_AES_CBC,
                                      ACTRUST_CRYPTO_PAD_NONE, iv, sizeof(iv),
                                      NULL, 0, ciphertext, ct_len, decrypted,
                                      sizeof(decrypted), &pt_len, NULL, 0));
    TEST_ASSERT_EQUAL(sizeof(plaintext), pt_len);
    TEST_ASSERT_EQUAL_MEMORY(plaintext, decrypted, pt_len);

    static const uint8_t pkcs7_pt[] = "hello pkcs7!!";
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_random(ctx, iv, sizeof(iv)));

    uint8_t pkcs7_ct[32];
    size_t  pkcs7_ct_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_aes_encrypt(
                          ctx, key, ACTRUST_CRYPTO_AES_CBC,
                          ACTRUST_CRYPTO_PAD_PKCS7, iv, sizeof(iv), NULL, 0,
                          pkcs7_pt, sizeof(pkcs7_pt) - 1, pkcs7_ct,
                          sizeof(pkcs7_ct), &pkcs7_ct_len, NULL, NULL));
    TEST_ASSERT_EQUAL(16u, pkcs7_ct_len);

    uint8_t pkcs7_dec[32];
    size_t  pkcs7_pt_len = 0;
    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_crypto_aes_decrypt(ctx, key, ACTRUST_CRYPTO_AES_CBC,
                                   ACTRUST_CRYPTO_PAD_PKCS7, iv, sizeof(iv),
                                   NULL, 0, pkcs7_ct, pkcs7_ct_len, pkcs7_dec,
                                   sizeof(pkcs7_dec), &pkcs7_pt_len, NULL, 0));
    TEST_ASSERT_EQUAL(sizeof(pkcs7_pt) - 1, pkcs7_pt_len);
    TEST_ASSERT_EQUAL_MEMORY(pkcs7_pt, pkcs7_dec, pkcs7_pt_len);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(ctx, &key));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_destroy(ctx, TEST_KEY_ID_AES));
}

void test_crypto_csr(void)
{
    actrust_crypto_key_t key = NULL;
    (void) actrust_crypto_key_destroy(ctx, TEST_KEY_ID_EC);
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_generate(ctx, TEST_KEY_ID_EC, &key));

    uint8_t csr[1024];
    size_t  csr_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_csr_generate(
                                      ctx, key, ACTRUST_CRYPTO_HASH_SHA256,
                                      "CN=test,O=AntChainTrustSDK,C=CN", csr,
                                      sizeof(csr), &csr_len));
    TEST_ASSERT_GREATER_THAN(0u, csr_len);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(ctx, &key));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_destroy(ctx, TEST_KEY_ID_EC));
}

static void pem_roundtrip(actrust_crypto_pem_object_t object,
                          const uint8_t *der_in, size_t der_in_len,
                          const char *header_marker)
{
    char    pem[2048];
    size_t  pem_len = 0;
    uint8_t der_out[2048];
    size_t  der_out_len = 0;

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_der_to_pem(object, der_in, der_in_len, pem,
                                                sizeof(pem), &pem_len));
    TEST_ASSERT_GREATER_THAN(0u, pem_len);
    TEST_ASSERT_EQUAL('\0', pem[pem_len]);
    TEST_ASSERT_NOT_NULL(strstr(pem, header_marker));

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_pem_to_der(object, pem, pem_len, der_out,
                                                sizeof(der_out), &der_out_len));
    TEST_ASSERT_EQUAL(der_in_len, der_out_len);
    TEST_ASSERT_EQUAL_MEMORY(der_in, der_out, der_in_len);
}

static void pem_fixture_roundtrip(actrust_crypto_pem_object_t object,
                                  const char                 *pem_fixture,
                                  const char                 *header_marker)
{
    uint8_t der_in[4096];
    size_t  der_in_len      = 0u;
    size_t  pem_fixture_len = strlen(pem_fixture);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_pem_to_der(
                                      object, pem_fixture, pem_fixture_len,
                                      der_in, sizeof(der_in), &der_in_len));
    TEST_ASSERT_GREATER_THAN(0u, der_in_len);
    pem_roundtrip(object, der_in, der_in_len, header_marker);
}

void test_crypto_pem_convert(void)
{
    /* Throwaway P-256 certificate fixture generated solely to exercise the
     * PEM<->DER round-trip. */
    static const char cert_pem_fixture[] =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIB3zCCAYWgAwIBAgIUeFCvkVMO9w9x7RGwBvCEdQ4cNrMwCgYIKoZIzj0EAwIw\n"
        "SzELMAkGA1UEBhMCQ04xDTALBgNVBAoMBGlCb3QxETAPBgNVBAsMCFRMUyBUZXN0\n"
        "MRowGAYDVQQDDBFpQm90IFRlc3QgUm9vdCBDQTAeFw0yNjAzMDIwODM2MDBaFw0y\n"
        "ODA2MDQwODM2MDBaMEUxCzAJBgNVBAYTAkNOMQ0wCwYDVQQKDARpQm90MREwDwYD\n"
        "VQQLDAhUTFMgVGVzdDEUMBIGA1UEAwwLaWJvdC1jbGllbnQwWTATBgcqhkjOPQIB\n"
        "BggqhkjOPQMBBwNCAAQ2Svz2TteC9H/yaSlPK5jkWHlmJEtuJsmRJP5OqQ9nMs0e\n"
        "b4CODmWGvTXWUZHX6unyJ2UUNP5sk+gyaVn3p/jTo00wSzAMBgNVHRMBAf8EAjAA\n"
        "MA4GA1UdDwEB/wQEAwIHgDATBgNVHSUEDDAKBggrBgEFBQcDAjAWBgNVHREEDzAN\n"
        "ggtpYm90LWNsaWVudDAKBggqhkjOPQQDAgNIADBFAiEAsZTpZUNN44QZuO7/BT0T\n"
        "Fde+2Jr7rKBZ1y6I+/3Il58CIAnaSQTvRUo/nx3pSg8lFr/KlMlENU2tLoCnMTd/\n"
        "rpVm\n"
        "-----END CERTIFICATE-----\n";

    pem_fixture_roundtrip(ACTRUST_CRYPTO_PEM_OBJECT_CERTIFICATE,
                          cert_pem_fixture, "BEGIN CERTIFICATE");

    actrust_crypto_key_t key = NULL;
    (void) actrust_crypto_key_destroy(ctx, TEST_KEY_ID_EC2);
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_generate(ctx, TEST_KEY_ID_EC2, &key));

    uint8_t pub_der[256];
    size_t  pub_der_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_export_public(
                          ctx, key, pub_der, sizeof(pub_der), &pub_der_len));
    pem_roundtrip(ACTRUST_CRYPTO_PEM_OBJECT_PUBLIC_KEY, pub_der, pub_der_len,
                  "BEGIN PUBLIC KEY");

    uint8_t csr_der[1024];
    size_t  csr_der_len = 0;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_csr_generate(
                                      ctx, key, ACTRUST_CRYPTO_HASH_SHA256,
                                      "CN=pem-test,O=AntChainTrustSDK,C=CN",
                                      csr_der, sizeof(csr_der), &csr_der_len));
    pem_roundtrip(ACTRUST_CRYPTO_PEM_OBJECT_CSR, csr_der, csr_der_len,
                  "BEGIN CERTIFICATE REQUEST");

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(ctx, &key));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_destroy(ctx, TEST_KEY_ID_EC2));
}

int main(void)
{
    if (actrust_crypto_init(&ctx) != ACTRUST_OK) {
        return 1;
    }

    UNITY_BEGIN();
    RUN_TEST(test_crypto_init_deinit);
    RUN_TEST(test_crypto_random);
    RUN_TEST(test_crypto_hash_sha256);
    RUN_TEST(test_crypto_ec_keygen);
    RUN_TEST(test_crypto_ecdsa);
    RUN_TEST(test_crypto_aes_gcm);
    RUN_TEST(test_crypto_aes_cbc);
    RUN_TEST(test_crypto_key_management);
    RUN_TEST(test_crypto_csr);
    RUN_TEST(test_crypto_pem_convert);
    int rc = UNITY_END();

    actrust_crypto_deinit(&ctx);
    return rc;
}
