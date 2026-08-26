// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file cloud_aws.c
 * @brief AWS cloud provider implementation.
 */

/* C standard */
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Common */
#include "common/common.h"

/* Cloud */
#include "cloud_aws_cert.h"
#include "cloud_aws.h"

/* Component */
#include "crypto/crypto.h"
#include "json/json.h"
#include "kv/kv.h"
#include "log/log.h"

/* Adapter */
#include "adapter/device.h"
#include "adapter/system.h"

#define CLOUD_AWS_MQTT_PROCESS_JOIN_TIMEOUT_MS                                 \
    ((uint32_t) CONFIG_ACTRUST_MQTT_CONNECT_TIMEOUT_MS + 1000u)

/* ========================================================================
 * AWS private functions
 * ======================================================================== */

void actrust_cloud_aws_set_registration_phase(
    actrust_cloud_t cloud, actrust_cloud_aws_registration_phase_t phase)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return;
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;

    (void) actrust_mutex_lock(aws_ctx->mutex);
    aws_ctx->registration_phase = phase;
    (void) actrust_mutex_unlock(aws_ctx->mutex);

    (void) actrust_sem_post(aws_ctx->phase_sem);
}

void actrust_cloud_aws_get_registration_phase(
    actrust_cloud_t cloud, actrust_cloud_aws_registration_phase_t *phase)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return;
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;

    (void) actrust_mutex_lock(aws_ctx->mutex);
    *phase = aws_ctx->registration_phase;
    (void) actrust_mutex_unlock(aws_ctx->mutex);
}

static void actrust_cloud_aws_clear_runtime_identity(
    actrust_cloud_aws_ctx_t *aws_ctx)
{
    if (aws_ctx == NULL) {
        return;
    }

    if (aws_ctx->client_key != NULL) {
        (void) actrust_crypto_key_close(aws_ctx->crypto_ctx,
                                        &aws_ctx->client_key);
    }

    (void) actrust_crypto_key_destroy(aws_ctx->crypto_ctx,
                                      ACTRUST_CLOUD_RUNTIME_KEY_ID);
    (void) actrust_crypto_cert_delete(ACTRUST_CLOUD_RUNTIME_CERT_ID);

    actrust_kv_t kv = NULL;
    if (actrust_kv_open(CLOUD_AWS_KV_NAMESPACE, strlen(CLOUD_AWS_KV_NAMESPACE),
                        &kv) == ACTRUST_OK) {
        (void) actrust_kv_del(kv, CLOUD_AWS_THING_NAME_KV_KEY,
                              strlen(CLOUD_AWS_THING_NAME_KV_KEY));
        (void) actrust_kv_close(kv);
    }

    memset(aws_ctx->ownership_token, 0, sizeof(aws_ctx->ownership_token));
    aws_ctx->ownership_token_len = 0u;
    memset(aws_ctx->client_cert_pem, 0, sizeof(aws_ctx->client_cert_pem));
    aws_ctx->client_cert_len = 0u;
    memset(aws_ctx->thing_name, 0, sizeof(aws_ctx->thing_name));
    aws_ctx->thing_name_len = 0u;
}

actrust_err_t actrust_cloud_aws_validate_thing_name(
    const actrust_cloud_aws_ctx_t *aws_ctx)
{
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    char    expected_thing_name[CLOUD_AWS_THING_NAME_MAX_LEN];
    int32_t n =
        snprintf(expected_thing_name, sizeof(expected_thing_name), "%s-%s",
                 CONFIG_ACTRUST_CLOUD_AWS_PRODUCT_KEY, aws_ctx->device_name);
    if (n < 0 || (size_t) n >= sizeof(expected_thing_name)) {
        return CLOUD_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    size_t expected_thing_name_len = (size_t) n;
    if (expected_thing_name_len == 0u || aws_ctx->thing_name_len == 0u ||
        aws_ctx->thing_name_len != expected_thing_name_len ||
        memcmp(aws_ctx->thing_name, expected_thing_name,
               expected_thing_name_len) != 0) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    return ACTRUST_OK;
}

/*
 * Build the shadow topics.
 */
static actrust_err_t actrust_cloud_aws_build_shadow_topics(
    actrust_cloud_t cloud)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    /* Build the shadow update topic. */
    int32_t n = snprintf(aws_ctx->shadow_topics.shadow_update_topic,
                         sizeof(aws_ctx->shadow_topics.shadow_update_topic),
                         CLOUD_AWS_SHADOW_UPDATE_TOPIC, aws_ctx->thing_name);
    if (n < 0 ||
        (size_t) n >= sizeof(aws_ctx->shadow_topics.shadow_update_topic)) {
        return CLOUD_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    /* Build the shadow update accepted topic. */
    n = snprintf(aws_ctx->shadow_topics.shadow_update_accepted_topic,
                 sizeof(aws_ctx->shadow_topics.shadow_update_accepted_topic),
                 CLOUD_AWS_SHADOW_UPDATE_ACCEPTED_TOPIC, aws_ctx->thing_name);
    if (n < 0 ||
        (size_t) n >=
            sizeof(aws_ctx->shadow_topics.shadow_update_accepted_topic)) {
        return CLOUD_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    /* Build the shadow update rejected topic. */
    n = snprintf(aws_ctx->shadow_topics.shadow_update_rejected_topic,
                 sizeof(aws_ctx->shadow_topics.shadow_update_rejected_topic),
                 CLOUD_AWS_SHADOW_UPDATE_REJECTED_TOPIC, aws_ctx->thing_name);
    if (n < 0 ||
        (size_t) n >=
            sizeof(aws_ctx->shadow_topics.shadow_update_rejected_topic)) {
        return CLOUD_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    return ACTRUST_OK;
}

/*
 * Build the register topics.
 */
static actrust_err_t actrust_cloud_aws_build_register_topics(
    actrust_cloud_t cloud)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    int32_t n =
        snprintf(aws_ctx->register_topics.register_request_topic,
                 sizeof(aws_ctx->register_topics.register_request_topic),
                 CLOUD_AWS_REGISTER_REQUEST_TOPIC, aws_ctx->thing_name);
    if (n < 0 ||
        (size_t) n >= sizeof(aws_ctx->register_topics.register_request_topic)) {
        return CLOUD_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    n = snprintf(aws_ctx->register_topics.register_challenge_topic,
                 sizeof(aws_ctx->register_topics.register_challenge_topic),
                 CLOUD_AWS_REGISTER_CHALLENGE_TOPIC, aws_ctx->thing_name);
    if (n < 0 ||
        (size_t) n >=
            sizeof(aws_ctx->register_topics.register_challenge_topic)) {
        return CLOUD_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    n = snprintf(aws_ctx->register_topics.register_response_topic,
                 sizeof(aws_ctx->register_topics.register_response_topic),
                 CLOUD_AWS_REGISTER_RESPONSE_TOPIC, aws_ctx->thing_name);
    if (n < 0 || (size_t) n >=
                     sizeof(aws_ctx->register_topics.register_response_topic)) {
        return CLOUD_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    n = snprintf(aws_ctx->register_topics.register_result_topic,
                 sizeof(aws_ctx->register_topics.register_result_topic),
                 CLOUD_AWS_REGISTER_RESULT_TOPIC, aws_ctx->thing_name);
    if (n < 0 ||
        (size_t) n >= sizeof(aws_ctx->register_topics.register_result_topic)) {
        return CLOUD_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    return ACTRUST_OK;
}

/*
 * Subscribe to provision topics.
 */
static actrust_err_t actrust_cloud_aws_mqtt_subscribe_provision_topics(
    actrust_cloud_t cloud)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    actrust_err_t ret = actrust_mqtt_subscribe(
        aws_ctx->mqtt_client, CLOUD_AWS_CREATE_CERT_ACCEPTED_TOPIC);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to subscribe to %s topic",
                  CLOUD_AWS_CREATE_CERT_ACCEPTED_TOPIC);
        return ret;
    }

    ret = actrust_mqtt_subscribe(aws_ctx->mqtt_client,
                                 CLOUD_AWS_CREATE_CERT_REJECTED_TOPIC);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to subscribe to %s topic",
                  CLOUD_AWS_CREATE_CERT_REJECTED_TOPIC);
        return ret;
    }

    ret = actrust_mqtt_subscribe(aws_ctx->mqtt_client,
                                 CLOUD_AWS_FLEET_PROVISION_ACCEPTED_TOPIC);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to subscribe to %s topic",
                  CLOUD_AWS_FLEET_PROVISION_ACCEPTED_TOPIC);
        return ret;
    }

    ret = actrust_mqtt_subscribe(aws_ctx->mqtt_client,
                                 CLOUD_AWS_FLEET_PROVISION_REJECTED_TOPIC);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to subscribe to %s topic",
                  CLOUD_AWS_FLEET_PROVISION_REJECTED_TOPIC);
        return ret;
    }

    LOG_INFO("Subscribed to provision topics.");

    return ACTRUST_OK;
}

/*
 * Subscribe to shadow topics.
 */
static actrust_err_t actrust_cloud_aws_mqtt_subscribe_shadow_topics(
    actrust_cloud_t cloud)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    actrust_err_t ret = actrust_mqtt_subscribe(
        aws_ctx->mqtt_client,
        aws_ctx->shadow_topics.shadow_update_accepted_topic);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to subscribe to %s topic",
                  aws_ctx->shadow_topics.shadow_update_accepted_topic);
        return ret;
    }

    ret = actrust_mqtt_subscribe(
        aws_ctx->mqtt_client,
        aws_ctx->shadow_topics.shadow_update_rejected_topic);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to subscribe to %s topic",
                  aws_ctx->shadow_topics.shadow_update_rejected_topic);
        return ret;
    }

    LOG_INFO("Subscribed to shadow topics.");

    return ACTRUST_OK;
}

static actrust_err_t actrust_cloud_aws_mqtt_subscribe_register_topics(
    actrust_cloud_t cloud)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    actrust_err_t ret = actrust_mqtt_subscribe(
        aws_ctx->mqtt_client,
        aws_ctx->register_topics.register_challenge_topic);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to subscribe to %s topic",
                  aws_ctx->register_topics.register_challenge_topic);
        return ret;
    }

    ret = actrust_mqtt_subscribe(
        aws_ctx->mqtt_client, aws_ctx->register_topics.register_result_topic);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to subscribe to %s topic",
                  aws_ctx->register_topics.register_result_topic);
        return ret;
    }

    LOG_INFO("Subscribed to register command topics.");

    return ACTRUST_OK;
}

static void actrust_cloud_aws_mqtt_process_entry(void *arg)
{
    actrust_mqtt_t mqtt = (actrust_mqtt_t) arg;
    if (mqtt == NULL) {
        return;
    }

    actrust_err_t ret = actrust_mqtt_process(mqtt);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("MQTT process exited: 0x%08" PRIx32, ret);
    }
}

static actrust_err_t actrust_cloud_aws_start_mqtt_process(actrust_cloud_t cloud)
{
    if (cloud == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    actrust_err_t ret =
        actrust_task_create(&aws_ctx->mqtt_process_task, "mqtt_process",
                            actrust_cloud_aws_mqtt_process_entry,
                            (void *) aws_ctx->mqtt_client, 0u, 0u);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to create MQTT process task.");
        return ret;
    }

    return ACTRUST_OK;
}

static actrust_err_t actrust_cloud_aws_join_mqtt_process(
    actrust_cloud_aws_ctx_t *aws_ctx, bool terminate_first)
{
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (aws_ctx->mqtt_process_task == NULL) {
        return ACTRUST_OK;
    }

    actrust_err_t first_err = ACTRUST_OK;
    if (terminate_first && aws_ctx->mqtt_client != NULL) {
        first_err = actrust_mqtt_stop_process(aws_ctx->mqtt_client);
        if (first_err != ACTRUST_OK) {
            LOG_WARN("Failed to queue MQTT process termination: 0x%08" PRIx32,
                     first_err);
        }
    }

    actrust_err_t join_err = actrust_task_join(
        aws_ctx->mqtt_process_task, CLOUD_AWS_MQTT_PROCESS_JOIN_TIMEOUT_MS);
    if (join_err == ACTRUST_OK) {
        aws_ctx->mqtt_process_task = NULL;
    } else {
        LOG_WARN("Failed to join MQTT process task: 0x%08" PRIx32, join_err);
        if (first_err == ACTRUST_OK) {
            first_err = join_err;
        }
    }

    return first_err;
}

/*
 * Load a certificate and key from persistent storage.
 */
static actrust_err_t load_cert_and_key(actrust_cloud_aws_ctx_t *aws_ctx,
                                       uint32_t cert_id, uint32_t key_id,
                                       uint8_t *cert_buf, size_t cert_buf_size,
                                       size_t               *cert_len,
                                       actrust_crypto_key_t *key_handle)
{
    actrust_err_t ret =
        actrust_crypto_cert_read(cert_id, cert_buf, cert_buf_size, cert_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to load certificate %" PRIu32 ".", cert_id);
        return ret;
    }

    actrust_crypto_key_t opened_key = NULL;
    ret = actrust_crypto_key_open(aws_ctx->crypto_ctx, key_id, &opened_key);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to open key %" PRIu32 ".", key_id);
        return ret;
    }

    if (*key_handle != NULL) {
        ret = actrust_crypto_key_close(aws_ctx->crypto_ctx, key_handle);
        if (ret != ACTRUST_OK) {
            (void) actrust_crypto_key_close(aws_ctx->crypto_ctx, &opened_key);
            return ret;
        }
    }

    *key_handle = opened_key;

    return ACTRUST_OK;
}

/*
 * Request a certificate from AWS.
 * example request:
 * {"certificateSigningRequest": "<CSR>"}
 */
static actrust_err_t actrust_cloud_aws_request_cert(actrust_cloud_t cloud)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    /* Generate a new runtime key. */
    actrust_crypto_key_t runtime_key = NULL;
    actrust_err_t        ret         = actrust_crypto_key_generate(
        aws_ctx->crypto_ctx, ACTRUST_CLOUD_RUNTIME_KEY_ID, &runtime_key);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to generate runtime key.");
        return ret;
    }

    /* Generate a CSR. */
    uint8_t csr_der[CLOUD_AWS_CSR_DER_MAX_LEN];
    size_t  csr_der_len = 0u;
    char    csr_subject[CLOUD_AWS_THING_NAME_MAX_LEN];
    (void) snprintf(csr_subject, sizeof(csr_subject),
                    "C=CN,O=AntChainTrustSDK,CN=%s", aws_ctx->device_name);

    ret = actrust_crypto_csr_generate(aws_ctx->crypto_ctx, runtime_key,
                                      ACTRUST_CRYPTO_HASH_SHA256, csr_subject,
                                      csr_der, sizeof(csr_der), &csr_der_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to generate CSR.");
        (void) actrust_crypto_key_close(aws_ctx->crypto_ctx, &runtime_key);
        return ret;
    }

    char   csr_pem[CLOUD_AWS_CSR_PEM_MAX_LEN];
    size_t csr_pem_len = 0u;
    ret = actrust_crypto_der_to_pem(ACTRUST_CRYPTO_PEM_OBJECT_CSR, csr_der,
                                    csr_der_len, csr_pem, sizeof(csr_pem),
                                    &csr_pem_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to convert CSR to PEM.");
        (void) actrust_crypto_key_close(aws_ctx->crypto_ctx, &runtime_key);
        return ret;
    }

    ret = actrust_crypto_key_close(aws_ctx->crypto_ctx, &runtime_key);
    if (ret != ACTRUST_OK) {
        return ret;
    }

    /* Build a JSON payload for the certificate request. */
    char json_payload[CLOUD_AWS_JSON_BUF_MAX_LEN];
    memset(json_payload, 0, sizeof(json_payload));
    actrust_json_builder_t builder;
    ret =
        actrust_json_builder_init(&builder, json_payload, sizeof(json_payload));
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to initialize JSON builder.");
        return ret;
    }

    ret = actrust_json_builder_add_string(
        &builder, CLOUD_AWS_JSON_KEY_CERT_SIGNING_REQUEST, csr_pem,
        csr_pem_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to add CSR PEM to JSON builder.");
        return ret;
    }

    char  *payload_ptr = NULL;
    size_t payload_len = 0u;
    ret = actrust_json_builder_finish(&builder, &payload_ptr, &payload_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to finish JSON builder.");
        return ret;
    }

    /* Publish the certificate request. */

    actrust_mqtt_message_t message = {
        .topic       = CLOUD_AWS_CREATE_CERT_TOPIC,
        .topic_len   = strlen(CLOUD_AWS_CREATE_CERT_TOPIC),
        .payload     = (uint8_t *) payload_ptr,
        .payload_len = payload_len,
        .qos         = ACTRUST_MQTT_QOS0,
    };
    ret = actrust_mqtt_publish(aws_ctx->mqtt_client, &message);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to publish certificate request.");
        return ret;
    }
    LOG_INFO("Published certificate request: payload_len=%zu", payload_len);

    return ACTRUST_OK;
}

/*
 * Publish a register request to AWS.
 * example request:
 * {
 *     "certificateOwnershipToken": "<ownership token>",
 *     "parameters": {
 *        "productKey": "<product key>",
 *        "deviceName": "<device name>"
 *    }
 * }
 */
static actrust_err_t actrust_cloud_aws_request_register(actrust_cloud_t cloud)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    /* Build registration JSON in a single buffer. */
    char                   json_buf[CLOUD_AWS_JSON_BUF_MAX_LEN];
    actrust_json_builder_t builder;
    actrust_err_t          ret =
        actrust_json_builder_init(&builder, json_buf, sizeof(json_buf));
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to initialize JSON builder.");
        return ret;
    }

    ret = actrust_json_builder_add_string(
        &builder, CLOUD_AWS_JSON_KEY_CERT_OWNERSHIP_TOKEN,
        aws_ctx->ownership_token, aws_ctx->ownership_token_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to add ownership token.");
        return ret;
    }

    ret = actrust_json_builder_open_object(&builder,
                                           CLOUD_AWS_JSON_KEY_PARAMETERS);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to open parameters object.");
        return ret;
    }

    ret = actrust_json_builder_add_string(
        &builder, CLOUD_AWS_JSON_KEY_PRODUCT_KEY,
        CONFIG_ACTRUST_CLOUD_AWS_PRODUCT_KEY,
        strlen(CONFIG_ACTRUST_CLOUD_AWS_PRODUCT_KEY));
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to add product key.");
        return ret;
    }

    ret = actrust_json_builder_add_string(
        &builder, CLOUD_AWS_JSON_KEY_DEVICE_NAME, aws_ctx->device_name,
        strlen(aws_ctx->device_name));
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to add device name.");
        return ret;
    }

    ret = actrust_json_builder_close_object(&builder);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to close parameters object.");
        return ret;
    }

    char  *payload_ptr = NULL;
    size_t payload_len = 0u;
    ret = actrust_json_builder_finish(&builder, &payload_ptr, &payload_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to finish JSON builder.");
        return ret;
    }

    /* Publish the registration request. */
    actrust_mqtt_message_t message = {
        .topic       = CLOUD_AWS_FLEET_PROVISION_TOPIC,
        .topic_len   = strlen(CLOUD_AWS_FLEET_PROVISION_TOPIC),
        .payload     = (uint8_t *) payload_ptr,
        .payload_len = payload_len,
        .qos         = ACTRUST_MQTT_QOS0,
    };
    ret = actrust_mqtt_publish(aws_ctx->mqtt_client, &message);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to publish registration request.");
        return ret;
    }

    return ACTRUST_OK;
}

static actrust_err_t actrust_cloud_aws_wait_registration_phase(
    actrust_cloud_t cloud, actrust_cloud_aws_registration_phase_t wait_phase,
    actrust_cloud_aws_registration_phase_t accepted_phase,
    actrust_cloud_aws_registration_phase_t rejected_phase)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    uint32_t timeout_ms = (uint32_t) CLOUD_AWS_REGISTER_TIMEOUT_MS;
    uint64_t deadline   = actrust_monotonic_ms() + (uint64_t) timeout_ms;

    actrust_cloud_aws_set_registration_phase(cloud, wait_phase);

    for (;;) {
        actrust_cloud_aws_registration_phase_t phase = wait_phase;
        actrust_cloud_aws_get_registration_phase(cloud, &phase);

        if (phase == accepted_phase) {
            return ACTRUST_OK;
        }

        if (phase == rejected_phase) {
            return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
        }

        uint64_t now = actrust_monotonic_ms();
        if (now >= deadline) {
            return CLOUD_ERR(ACTRUST_ERR_TIMEOUT);
        }

        uint64_t      remaining = deadline - now;
        uint32_t      wait_ms   = (remaining > (uint64_t) UINT32_MAX)
                                      ? UINT32_MAX
                                      : (uint32_t) remaining;
        actrust_err_t sem_ret   = actrust_sem_wait(aws_ctx->phase_sem, wait_ms);
        if (sem_ret != ACTRUST_OK && sem_ret != ACTRUST_ERR_TIMEOUT) {
            /* A genuine semaphore / hardware error — propagate it distinctly
             * rather than masking it as a timeout. */
            return CLOUD_ERR(sem_ret);
        }
        continue;
    }
}

static actrust_err_t actrust_cloud_aws_first_connect(actrust_cloud_t cloud)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    /* Set the MQTT callbacks. */
    actrust_mqtt_callbacks_t callbacks = {
        .on_message = actrust_cloud_aws_registration_callback,
        .user_ctx   = cloud,
    };

    actrust_err_t ret =
        actrust_mqtt_set_callbacks(aws_ctx->mqtt_client, &callbacks);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to set MQTT callbacks.");
        actrust_cloud_aws_clear_runtime_identity(aws_ctx);
        return ret;
    }

    ret = load_cert_and_key(
        aws_ctx, ACTRUST_CLOUD_CLAIM_CERT_ID, ACTRUST_CLOUD_CLAIM_KEY_ID,
        (uint8_t *) aws_ctx->client_cert_pem, sizeof(aws_ctx->client_cert_pem),
        &aws_ctx->client_cert_len, &aws_ctx->client_key);
    if (ret != ACTRUST_OK) {
        actrust_cloud_aws_clear_runtime_identity(aws_ctx);
        return ret;
    }

    actrust_mqtt_config_t mqtt_config = {
        .client_id                 = aws_ctx->device_name,
        .transport.type            = ACTRUST_MQTT_TRANSPORT_TLS,
        .transport.config.tls.host = CONFIG_ACTRUST_CLOUD_AWS_ENDPOINT,
        .transport.config.tls.port = (uint16_t) CONFIG_ACTRUST_CLOUD_AWS_PORT,
        .transport.config.tls.crypto_ctx = aws_ctx->crypto_ctx,
        .transport.config.tls.ca = (const uint8_t *) CLOUD_AWS_ROOT_CA_CERT_PEM,
        .transport.config.tls.ca_len    = sizeof(CLOUD_AWS_ROOT_CA_CERT_PEM),
        .transport.config.tls.ca_format = ACTRUST_TLS_CERT_FORMAT_PEM,
        .transport.config.tls.client_cert =
            (const uint8_t *) aws_ctx->client_cert_pem,
        .transport.config.tls.client_cert_len    = aws_ctx->client_cert_len,
        .transport.config.tls.client_cert_format = ACTRUST_TLS_CERT_FORMAT_PEM,
        .transport.config.tls.client_key         = aws_ctx->client_key,
    };

    ret = actrust_cloud_aws_start_mqtt_process(cloud);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to start MQTT process.");
        actrust_cloud_aws_clear_runtime_identity(aws_ctx);
        return ret;
    }

    ret = actrust_mqtt_connect(aws_ctx->mqtt_client, &mqtt_config);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to connect to MQTT broker.");
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, true);
        actrust_cloud_aws_clear_runtime_identity(aws_ctx);
        return ret;
    }

    /* Subscribe to provision topics. */
    ret = actrust_cloud_aws_mqtt_subscribe_provision_topics(cloud);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to subscribe to provision topics.");
        (void) actrust_mqtt_disconnect(aws_ctx->mqtt_client);
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, false);
        actrust_cloud_aws_clear_runtime_identity(aws_ctx);
        return ret;
    }

    actrust_cloud_aws_set_registration_phase(cloud, CLOUD_AWS_REQUEST_CERT);

    /* Request a certificate from AWS. */
    ret = actrust_cloud_aws_request_cert(cloud);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to request certificate.");
        (void) actrust_mqtt_disconnect(aws_ctx->mqtt_client);
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, false);
        actrust_cloud_aws_clear_runtime_identity(aws_ctx);
        return ret;
    }
    LOG_INFO("Requested certificate.");

    ret = actrust_cloud_aws_wait_registration_phase(
        cloud, CLOUD_AWS_WAIT_CSR_RESPONSE, CLOUD_AWS_CSR_ACCEPTED,
        CLOUD_AWS_CSR_REJECTED);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed while waiting CSR response.");
        (void) actrust_mqtt_disconnect(aws_ctx->mqtt_client);
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, false);
        actrust_cloud_aws_clear_runtime_identity(aws_ctx);
        return ret;
    }

    actrust_cloud_aws_set_registration_phase(cloud, CLOUD_AWS_REQUEST_REGISTER);

    ret = actrust_cloud_aws_request_register(cloud);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to request registration.");
        (void) actrust_mqtt_disconnect(aws_ctx->mqtt_client);
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, false);
        actrust_cloud_aws_clear_runtime_identity(aws_ctx);
        return ret;
    }
    LOG_INFO("Requested registration.");

    ret = actrust_cloud_aws_wait_registration_phase(
        cloud, CLOUD_AWS_WAIT_REGISTER_RESPONSE, CLOUD_AWS_REGISTER_ACCEPTED,
        CLOUD_AWS_REGISTER_REJECTED);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed while waiting register response.");
        (void) actrust_mqtt_disconnect(aws_ctx->mqtt_client);
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, false);
        actrust_cloud_aws_clear_runtime_identity(aws_ctx);
        return ret;
    }

    actrust_cloud_aws_set_registration_phase(cloud, CLOUD_AWS_FINISH);

    /* close the MQTT connection. */
    ret = actrust_mqtt_disconnect(aws_ctx->mqtt_client);
    if (ret != ACTRUST_OK) {
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, true);
        return ret;
    }

    ret = actrust_cloud_aws_join_mqtt_process(aws_ctx, false);
    if (ret != ACTRUST_OK) {
        return ret;
    }

    LOG_INFO("First connection successful.");
    return ACTRUST_OK;
}

static actrust_err_t actrust_cloud_aws_runtime_connect(actrust_cloud_t cloud)
{
    if (cloud == NULL || cloud->provider_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    /* Get the thing name from KV. */
    actrust_kv_t  kv;
    actrust_err_t ret = actrust_kv_open(CLOUD_AWS_KV_NAMESPACE,
                                        strlen(CLOUD_AWS_KV_NAMESPACE), &kv);
    if (ret != ACTRUST_OK) {
        LOG_INFO("Runtime identity namespace not found, fallback to first "
                 "connection.");
        return CLOUD_ERR(ACTRUST_ERR_NOT_READY);
    }

    ret = actrust_kv_get(kv, CLOUD_AWS_THING_NAME_KV_KEY,
                         strlen(CLOUD_AWS_THING_NAME_KV_KEY),
                         aws_ctx->thing_name, sizeof(aws_ctx->thing_name) - 1u,
                         &aws_ctx->thing_name_len);
    if (ret != ACTRUST_OK) {
        (void) actrust_kv_close(kv);
        LOG_INFO("Runtime thing name not found, fallback to first connection.");
        return CLOUD_ERR(ACTRUST_ERR_NOT_READY);
    }
    aws_ctx->thing_name[aws_ctx->thing_name_len] = '\0';

    ret = actrust_kv_close(kv);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to close KV namespace: %s", CLOUD_AWS_KV_NAMESPACE);
        return ret;
    }

    ret = actrust_cloud_aws_validate_thing_name(aws_ctx);
    if (ret != ACTRUST_OK) {
        LOG_WARN("Runtime thing name does not match device identity, clearing "
                 "cached identity.");
        actrust_cloud_aws_clear_runtime_identity(aws_ctx);
        return CLOUD_ERR(ACTRUST_ERR_NOT_READY);
    }

    LOG_INFO("Runtime thing name: %s", aws_ctx->thing_name);

    /* Load the runtime certificate. */
    ret = load_cert_and_key(
        aws_ctx, ACTRUST_CLOUD_RUNTIME_CERT_ID, ACTRUST_CLOUD_RUNTIME_KEY_ID,
        (uint8_t *) aws_ctx->client_cert_pem, sizeof(aws_ctx->client_cert_pem),
        &aws_ctx->client_cert_len, &aws_ctx->client_key);
    if (ret != ACTRUST_OK) {
        LOG_WARN("Runtime identity is incomplete, clearing cached identity.");
        actrust_cloud_aws_clear_runtime_identity(aws_ctx);
        return CLOUD_ERR(ACTRUST_ERR_NOT_READY);
    }

    actrust_mqtt_config_t mqtt_config = {
        .client_id                 = aws_ctx->thing_name,
        .transport.type            = ACTRUST_MQTT_TRANSPORT_TLS,
        .transport.config.tls.host = CONFIG_ACTRUST_CLOUD_AWS_ENDPOINT,
        .transport.config.tls.port = (uint16_t) CONFIG_ACTRUST_CLOUD_AWS_PORT,
        .transport.config.tls.crypto_ctx = aws_ctx->crypto_ctx,
        .transport.config.tls.ca = (const uint8_t *) CLOUD_AWS_ROOT_CA_CERT_PEM,
        .transport.config.tls.ca_len    = sizeof(CLOUD_AWS_ROOT_CA_CERT_PEM),
        .transport.config.tls.ca_format = ACTRUST_TLS_CERT_FORMAT_PEM,
        .transport.config.tls.client_cert =
            (const uint8_t *) aws_ctx->client_cert_pem,
        .transport.config.tls.client_cert_len    = aws_ctx->client_cert_len,
        .transport.config.tls.client_cert_format = ACTRUST_TLS_CERT_FORMAT_PEM,
        .transport.config.tls.client_key         = aws_ctx->client_key,
    };

    /* Set the MQTT callbacks. */
    actrust_mqtt_callbacks_t callbacks = {
        .on_message = actrust_cloud_aws_runtime_callback,
        .user_ctx   = cloud,
    };

    ret = actrust_mqtt_set_callbacks(aws_ctx->mqtt_client, &callbacks);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to set MQTT callbacks.");
        return ret;
    }

    ret = actrust_cloud_aws_start_mqtt_process(cloud);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to start MQTT process.");
        return ret;
    }

    /* Open the MQTT connection. */
    ret = actrust_mqtt_connect(aws_ctx->mqtt_client, &mqtt_config);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to connect to MQTT broker.");
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, true);
        LOG_WARN("Runtime MQTT connect failed, keeping cached identity.");
        return ret;
    }

    /* Build runtime topics. */
    ret = actrust_cloud_aws_build_shadow_topics(cloud);
    if (ret != ACTRUST_OK) {
        (void) actrust_mqtt_disconnect(aws_ctx->mqtt_client);
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, false);
        return ret;
    }

    ret = actrust_cloud_aws_build_register_topics(cloud);
    if (ret != ACTRUST_OK) {
        (void) actrust_mqtt_disconnect(aws_ctx->mqtt_client);
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, false);
        return ret;
    }

    /* Subscribe to runtime topics. */
    ret = actrust_cloud_aws_mqtt_subscribe_shadow_topics(cloud);
    if (ret != ACTRUST_OK) {
        (void) actrust_mqtt_disconnect(aws_ctx->mqtt_client);
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, false);
        return ret;
    }

    ret = actrust_cloud_aws_mqtt_subscribe_register_topics(cloud);
    if (ret != ACTRUST_OK) {
        (void) actrust_mqtt_disconnect(aws_ctx->mqtt_client);
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, false);
        return ret;
    }

    return ACTRUST_OK;
}

/* ========================================================================
 * AWS public functions
 * ======================================================================== */

static actrust_err_t actrust_cloud_aws_init(actrust_cloud_t cloud)
{
    if (cloud == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) ACTRUST_CALLOC(1, sizeof(*aws_ctx));
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_NO_MEM);
    }

    cloud->provider_ctx = aws_ctx;

    actrust_err_t ret =
        actrust_get_hw_id(aws_ctx->device_name, sizeof(aws_ctx->device_name));
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to get hardware ID: 0x%08" PRIx32, ret);
        goto fail;
    }

    ret = actrust_crypto_init(&aws_ctx->crypto_ctx);
    if (ret != ACTRUST_OK) {
        goto fail;
    }

    ret = actrust_mutex_create(&aws_ctx->mutex);
    if (ret != ACTRUST_OK) {
        goto fail;
    }

    ret = actrust_sem_create(&aws_ctx->phase_sem, 0u);
    if (ret != ACTRUST_OK) {
        goto fail;
    }

    ret = actrust_mqtt_init(&aws_ctx->mqtt_client);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to initialize MQTT client: 0x%08" PRIx32, ret);
        goto fail;
    }

    return ACTRUST_OK;

fail:
    if (aws_ctx != NULL) {
        if (aws_ctx->crypto_ctx != NULL) {
            (void) actrust_crypto_deinit(&aws_ctx->crypto_ctx);
        }
        if (aws_ctx->phase_sem != NULL) {
            (void) actrust_sem_destroy(aws_ctx->phase_sem);
        }
        if (aws_ctx->mutex != NULL) {
            (void) actrust_mutex_destroy(aws_ctx->mutex);
        }
        if (aws_ctx->mqtt_client != NULL) {
            (void) actrust_mqtt_deinit(aws_ctx->mqtt_client);
        }
        ACTRUST_FREE(aws_ctx);
        cloud->provider_ctx = NULL;
    }
    return ret;
}

static actrust_err_t actrust_cloud_aws_deinit(actrust_cloud_t cloud)
{
    if (cloud == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return ACTRUST_OK;
    }

    actrust_err_t first_err = ACTRUST_OK;
    if (aws_ctx->mqtt_process_task != NULL) {
        actrust_err_t ret = actrust_cloud_aws_join_mqtt_process(aws_ctx, true);
        if (ret != ACTRUST_OK) {
            first_err = ret;
        }
        if (aws_ctx->mqtt_process_task != NULL) {
            return first_err;
        }
    }

    if (aws_ctx->mqtt_client != NULL) {
        actrust_err_t ret = actrust_mqtt_deinit(aws_ctx->mqtt_client);
        if (ret != ACTRUST_OK) {
            return ret;
        }
        aws_ctx->mqtt_client = NULL;
    }

    if (aws_ctx->client_key != NULL) {
        actrust_err_t ret =
            actrust_crypto_key_close(aws_ctx->crypto_ctx, &aws_ctx->client_key);
        if (ret != ACTRUST_OK) {
            if (first_err == ACTRUST_OK) {
                first_err = ret;
            }
        }
    }

    if (aws_ctx->mutex != NULL) {
        actrust_err_t ret = actrust_mutex_destroy(aws_ctx->mutex);
        if (ret == ACTRUST_OK) {
            aws_ctx->mutex = NULL;
        } else if (first_err == ACTRUST_OK) {
            first_err = ret;
        }
    }

    if (aws_ctx->phase_sem != NULL) {
        actrust_err_t ret = actrust_sem_destroy(aws_ctx->phase_sem);
        if (ret == ACTRUST_OK) {
            aws_ctx->phase_sem = NULL;
        } else if (first_err == ACTRUST_OK) {
            first_err = ret;
        }
    }

    if (aws_ctx->client_key == NULL && aws_ctx->crypto_ctx != NULL) {
        actrust_err_t ret = actrust_crypto_deinit(&aws_ctx->crypto_ctx);
        if (ret != ACTRUST_OK) {
            if (first_err == ACTRUST_OK) {
                first_err = ret;
            }
        }
    }

    if (aws_ctx->mqtt_process_task != NULL || aws_ctx->mqtt_client != NULL ||
        aws_ctx->client_key != NULL || aws_ctx->crypto_ctx != NULL ||
        aws_ctx->mutex != NULL || aws_ctx->phase_sem != NULL) {
        return first_err == ACTRUST_OK ? CLOUD_ERR(ACTRUST_ERR_BUSY)
                                       : first_err;
    }

    actrust_secure_zeroize(aws_ctx, sizeof(*aws_ctx));
    ACTRUST_FREE(aws_ctx);
    cloud->provider_ctx = NULL;
    return first_err;
}

static actrust_err_t actrust_cloud_aws_connect(actrust_cloud_t cloud)
{
    if (cloud == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    if (aws_ctx->mqtt_client == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    actrust_err_t ret = actrust_cloud_aws_runtime_connect(cloud);
    if (ret != ACTRUST_OK) {
        if (ret != CLOUD_ERR(ACTRUST_ERR_NOT_READY)) {
            return ret;
        }

        ret = actrust_cloud_aws_first_connect(cloud);
        if (ret != ACTRUST_OK) {
            return ret;
        }
        LOG_INFO("First connection successful.");

        LOG_INFO("Re-connecting to AWS.");
        ret = actrust_cloud_aws_runtime_connect(cloud);
        if (ret != ACTRUST_OK) {
            LOG_ERROR("Failed to re-connect to AWS.");
            return ret;
        }
    }

    return ACTRUST_OK;
}

static actrust_err_t actrust_cloud_aws_disconnect(actrust_cloud_t cloud)
{
    if (cloud == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    if (aws_ctx->mqtt_client == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    actrust_err_t ret = actrust_mqtt_disconnect(aws_ctx->mqtt_client);
    if (ret != ACTRUST_OK) {
        (void) actrust_cloud_aws_join_mqtt_process(aws_ctx, true);
        return ret;
    }

    return actrust_cloud_aws_join_mqtt_process(aws_ctx, false);
}

static actrust_err_t actrust_cloud_aws_send_register(
    actrust_cloud_t cloud, actrust_cloud_register_msg_type_t type,
    const uint8_t *payload, size_t payload_len)
{
    if (cloud == NULL || payload == NULL || payload_len == 0u) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL || aws_ctx->mqtt_client == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    if (payload_len > CONFIG_ACTRUST_MQTT_PAYLOAD_MAX_LEN ||
        memchr(payload, '\0', payload_len) != NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const char *topic = NULL;
    switch (type) {
        case ACTRUST_CLOUD_REGISTER_REQUEST:
            topic = aws_ctx->register_topics.register_request_topic;
            break;
        case ACTRUST_CLOUD_REGISTER_RESPONSE:
            topic = aws_ctx->register_topics.register_response_topic;
            break;
        default:
            return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (topic[0] == '\0') {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    actrust_mqtt_message_t message = {
        .topic       = (char *) topic,
        .topic_len   = strlen(topic),
        .payload     = (uint8_t *) payload,
        .payload_len = payload_len,
        .qos         = ACTRUST_MQTT_QOS0,
    };

    return actrust_mqtt_publish(aws_ctx->mqtt_client, &message);
}

/*
 * Send data to AWS.
 * example payload:
 * {
 *  "state":
 *  {
 *      "reported":
 *      {
 *          <payload>
 *      }
 *   }
 * }
 */
static actrust_err_t actrust_cloud_aws_send_data(actrust_cloud_t cloud,
                                                 const uint8_t  *payload,
                                                 size_t          payload_len)
{
    if (cloud == NULL || payload == NULL || payload_len == 0u) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL || aws_ctx->mqtt_client == NULL) {
        return CLOUD_ERR(ACTRUST_ERR_BAD_STATE);
    }

    if (payload_len > CONFIG_ACTRUST_MQTT_PAYLOAD_MAX_LEN ||
        memchr(payload, '\0', payload_len) != NULL) {
        return CLOUD_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    char payload_json[CONFIG_ACTRUST_MQTT_PAYLOAD_MAX_LEN + 1u];
    memcpy(payload_json, payload, payload_len);
    payload_json[payload_len] = '\0';

    /* Build {"state":{"reported":<payload>}} in a single buffer. */
    char                   json_buf[CLOUD_AWS_JSON_BUF_MAX_LEN];
    actrust_json_builder_t builder;
    actrust_err_t          ret =
        actrust_json_builder_init(&builder, json_buf, sizeof(json_buf));
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to initialize JSON builder.");
        return ret;
    }

    ret = actrust_json_builder_open_object(&builder,
                                           CLOUD_AWS_JSON_KEY_SHADOW_STATE);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to open shadow state object.");
        return ret;
    }

    ret = actrust_json_builder_add_object(
        &builder, CLOUD_AWS_JSON_KEY_SHADOW_REPORTED, payload_json);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to add reported payload.");
        return ret;
    }

    ret = actrust_json_builder_close_object(&builder);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to close shadow state object.");
        return ret;
    }

    char  *json_ptr = NULL;
    size_t json_len = 0u;
    ret = actrust_json_builder_finish(&builder, &json_ptr, &json_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to finish JSON builder.");
        return ret;
    }

    /* Publish the shadow update. */
    actrust_mqtt_message_t message = {
        .topic       = aws_ctx->shadow_topics.shadow_update_topic,
        .topic_len   = strlen(aws_ctx->shadow_topics.shadow_update_topic),
        .payload     = (uint8_t *) json_ptr,
        .payload_len = json_len,
        .qos         = ACTRUST_MQTT_QOS0,
    };
    ret = actrust_mqtt_publish(aws_ctx->mqtt_client, &message);
    if (ret != ACTRUST_OK) {
        return ret;
    }

    return ACTRUST_OK;
}

const actrust_cloud_provider_ops_t actrust_cloud_provider_aws_ops = {
    .init          = actrust_cloud_aws_init,
    .deinit        = actrust_cloud_aws_deinit,
    .connect       = actrust_cloud_aws_connect,
    .disconnect    = actrust_cloud_aws_disconnect,
    .send_data     = actrust_cloud_aws_send_data,
    .send_register = actrust_cloud_aws_send_register,
};
