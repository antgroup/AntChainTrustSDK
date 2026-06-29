// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Project */
#include "actrust_config.h"

/* Test */
#include "actrust_test.h"

/* MQTT */
#include "mqtt/mqtt.h"

/* Component */
#include "crypto/crypto.h"

/* Adapter */
#include "adapter/system.h"

#ifndef TEST_MQTT_PKI_DIR
#define TEST_MQTT_PKI_DIR "pki"
#endif
#define TEST_MQTT_CA_PATH          TEST_MQTT_PKI_DIR "/AmazonRootCA.pem"
#define TEST_MQTT_CLIENT_CERT_PATH TEST_MQTT_PKI_DIR "/client.crt"
#define TEST_MQTT_CLIENT_KEY_PATH  TEST_MQTT_PKI_DIR "/client.key"

#define TEST_MQTT_CLIENT_KEY_ID ACTRUST_CRYPTO_KEY_ID_EC_0

#define TEST_MQTT_PORT              8883
#define TEST_MQTT_CREATE_CERT_TOPIC "$aws/certificates/create-from-csr/json"
#define TEST_MQTT_CREATE_CERT_ACCEPTED_TOPIC                                   \
    "$aws/certificates/create-from-csr/json/accepted"
#define TEST_MQTT_CREATE_CERT_REJECTED_TOPIC                                   \
    "$aws/certificates/create-from-csr/json/rejected"
#define TEST_MQTT_PAYLOAD                                                      \
    "{\"certificateSigningRequest\":\"-----BEGIN CERTIFICATE "                 \
    "REQUEST-----"                                                             \
    "\\nMIHzMIGZAgEAMDcxGTAXBgNVBAMMEGlib3RfcGxhY2Vob2xkZXIxDTALBgNVBAoM\\nBG" \
    "lCb3QxCzAJBgNVBAYTAlVTMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEZ9jp\\nfn3IICu" \
    "okbXDkWqJzhv9SQOaGX1AH9JKmjlhsAKvnrxoPmR2RWQ86ZlmVgFR4q/S\\nZs/"          \
    "0EU7xctzQn65U9aAAMAoGCCqGSM49BAMCA0kAMEYCIQCxw3H51KGk2rvHOqMo\\nGpNJEP/"  \
    "dyWb8IYtOgvvkWG35PQIhAImyYKOE1FZixJ/"                                     \
    "r1F+cQmHd8BmyZuvcqJeb\\nlVSqirIl\\n-----END CERTIFICATE "                 \
    "REQUEST-----\\n\"}"

static uint32_t g_mqtt_test_rx_accepted = 0;
static uint32_t g_mqtt_test_rx_rejected = 0;

static void mqtt_test_on_message(void                         *user_ctx,
                                 const actrust_mqtt_message_t *message)
{
    (void) user_ctx;
    if (message == NULL) {
        return;
    }

    size_t accepted_topic_len = strlen(TEST_MQTT_CREATE_CERT_ACCEPTED_TOPIC);
    if (message->topic_len == accepted_topic_len &&
        memcmp(message->topic, TEST_MQTT_CREATE_CERT_ACCEPTED_TOPIC,
               accepted_topic_len) == 0) {
        g_mqtt_test_rx_accepted++;
    }

    size_t rejected_topic_len = strlen(TEST_MQTT_CREATE_CERT_REJECTED_TOPIC);
    if (message->topic_len == rejected_topic_len &&
        memcmp(message->topic, TEST_MQTT_CREATE_CERT_REJECTED_TOPIC,
               rejected_topic_len) == 0) {
        g_mqtt_test_rx_rejected++;
    }
}

static void mqtt_test_process_task(void *arg)
{
    actrust_mqtt_t mqtt = (actrust_mqtt_t) arg;
    if (mqtt == NULL) {
        return;
    }
    (void) actrust_mqtt_process(mqtt);
}

static int mqtt_test_load_client_key_pem(actrust_crypto_ctx_t  crypto,
                                         actrust_crypto_key_t *client_key)
{
    if (crypto == NULL || client_key == NULL) {
        return 1;
    }

    char   client_key_pem[4096];
    size_t client_key_pem_len = 0u;
    if (actrust_test_load_file(TEST_MQTT_CLIENT_KEY_PATH, client_key_pem,
                               sizeof(client_key_pem), &client_key_pem_len,
                               true) != 0) {
        return 1;
    }

    actrust_err_t err = actrust_crypto_key_import(
        crypto, TEST_MQTT_CLIENT_KEY_ID, ACTRUST_CRYPTO_FORMAT_PRIVATE_PEM,
        (const uint8_t *) client_key_pem, client_key_pem_len, client_key);
    if (err != ACTRUST_OK) {
        return 1;
    }

    return 0;
}

void setUp(void)
{
    g_mqtt_test_rx_accepted = 0;
    g_mqtt_test_rx_rejected = 0;
}

void tearDown(void)
{
}

void test_mqtt_mtls_pub_sub(void)
{
    actrust_crypto_ctx_t crypto = NULL;
    uint8_t              ca_pem[4096];
    size_t               ca_len = 0u;
    TEST_ASSERT_EQUAL(0,
                      actrust_test_load_file(TEST_MQTT_CA_PATH, ca_pem,
                                             sizeof(ca_pem), &ca_len, false));

    uint8_t client_cert_pem[4096];
    size_t  client_cert_pem_len = 0u;
    TEST_ASSERT_EQUAL(0, actrust_test_load_file(TEST_MQTT_CLIENT_CERT_PATH,
                                                client_cert_pem,
                                                sizeof(client_cert_pem),
                                                &client_cert_pem_len, false));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_init(&crypto));

    actrust_crypto_key_t client_key = NULL;
    TEST_ASSERT_EQUAL(0, mqtt_test_load_client_key_pem(crypto, &client_key));

    actrust_mqtt_config_t config;
    memset(&config, 0, sizeof(config));
    config.client_id                        = "actrust-mqtt-test";
    config.transport.type                   = ACTRUST_MQTT_TRANSPORT_TLS;
    config.transport.config.tls.host        = CONFIG_ACTRUST_CLOUD_AWS_ENDPOINT;
    config.transport.config.tls.port        = TEST_MQTT_PORT;
    config.transport.config.tls.crypto_ctx  = crypto;
    config.transport.config.tls.ca          = ca_pem;
    config.transport.config.tls.ca_len      = ca_len;
    config.transport.config.tls.ca_format   = ACTRUST_TLS_CERT_FORMAT_PEM;
    config.transport.config.tls.client_cert = client_cert_pem;
    config.transport.config.tls.client_cert_len = client_cert_pem_len;
    config.transport.config.tls.client_cert_format =
        ACTRUST_TLS_CERT_FORMAT_PEM;
    config.transport.config.tls.client_key = client_key;

    actrust_mqtt_callbacks_t callbacks = {
        .on_message = mqtt_test_on_message,
        .user_ctx   = NULL,
    };

    actrust_mqtt_message_t publish_message = {
        .topic       = TEST_MQTT_CREATE_CERT_TOPIC,
        .topic_len   = strlen(TEST_MQTT_CREATE_CERT_TOPIC),
        .payload     = (uint8_t *) TEST_MQTT_PAYLOAD,
        .payload_len = strlen(TEST_MQTT_PAYLOAD),
        .qos         = ACTRUST_MQTT_QOS0,
    };

    actrust_mqtt_t mqtt = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_init(&mqtt));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_set_callbacks(mqtt, &callbacks));

    actrust_task_t task = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_create(&task, "mqtt_proc",
                                                      mqtt_test_process_task,
                                                      (void *) mqtt, 0u, 0u));
    (void) task;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_connect(mqtt, &config));
    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_mqtt_subscribe(mqtt, TEST_MQTT_CREATE_CERT_ACCEPTED_TOPIC));
    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_mqtt_subscribe(mqtt, TEST_MQTT_CREATE_CERT_REJECTED_TOPIC));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_publish(mqtt, &publish_message));

    while (g_mqtt_test_rx_accepted == 0u && g_mqtt_test_rx_rejected == 0u) {
        actrust_sleep_ms(1000u);
    }

    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_mqtt_unsubscribe(mqtt, TEST_MQTT_CREATE_CERT_ACCEPTED_TOPIC));
    TEST_ASSERT_EQUAL(
        ACTRUST_OK,
        actrust_mqtt_unsubscribe(mqtt, TEST_MQTT_CREATE_CERT_REJECTED_TOPIC));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_disconnect(mqtt));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_deinit(mqtt));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_close(crypto, &client_key));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_deinit(&crypto));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mqtt_mtls_pub_sub);
    return UNITY_END();
}
