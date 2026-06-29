// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file cloud_aws_callback.c
 * @brief AWS MQTT callback and ACK parsing.
 */

/* C standard */
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Cloud */
#include "cloud/cloud_internal.h"
#include "cloud_aws.h"

/* Component */
#include "crypto/crypto.h"
#include "json/json.h"
#include "kv/kv.h"
#include "log/log.h"
#include "mqtt/mqtt.h"

/* Adapter */
#include "adapter/system.h"

static bool actrust_cloud_aws_topic_match(const actrust_mqtt_message_t *msg,
                                          const char                   *topic)
{
    if (msg == NULL || msg->topic == NULL || topic == NULL) {
        return false;
    }

    size_t topic_len = strlen(topic);
    if (msg->topic_len != topic_len) {
        return false;
    }

    if (strncmp(msg->topic, topic, topic_len) != 0) {
        return false;
    }

    return true;
}

static void actrust_cloud_aws_queue_register_command(
    actrust_cloud_t cloud, const actrust_mqtt_message_t *msg)
{
    if (cloud == NULL || msg == NULL) {
        return;
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return;
    }

    if (msg->payload_len > CONFIG_ACTRUST_MQTT_PAYLOAD_MAX_LEN) {
        LOG_ERROR("Register command payload too large");
        return;
    }

    actrust_cloud_msg_t downlink;
    memset(&downlink, 0, sizeof(downlink));
    memcpy(downlink.payload, msg->payload, msg->payload_len);
    downlink.payload_len  = msg->payload_len;
    downlink.recv_time_ms = actrust_wall_time_ms();

    actrust_err_t ret =
        actrust_queue_push(cloud->downlink_queue, &downlink, 0u);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to add register message to downlink queue");
    }
}

/*
 * Expected payload shape (see AWS IoT CreateCertificateFromCsr response):
 *   { "certificatePem": "<PEM>",
 *     "certificateId": "<hex>",
 *     "certificateOwnershipToken": "<token>" }
 */
static void actrust_cloud_aws_handle_csr_accepted(
    actrust_cloud_t cloud, const actrust_mqtt_message_t *msg)
{
    if (cloud == NULL || msg == NULL) {
        return;
    }

    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        LOG_ERROR("Invalid AWS context");
        return;
    }

    /* Parse the JSON payload in place (actrust_json_init takes const input). */
    actrust_json_view_t json_view;
    actrust_err_t       ret = actrust_json_init(
        &json_view, (const char *) msg->payload, msg->payload_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to initialize JSON view");
        return;
    }

    /* Get the certificate PEM. */
    actrust_json_value_t value;
    ret = actrust_json_query(&json_view, CLOUD_AWS_JSON_KEY_CERTIFICATE_PEM,
                             &value);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to query certificatePem");
        return;
    }

    char   client_cert_pem[CLOUD_AWS_CERT_PEM_MAX_LEN];
    size_t client_cert_pem_len = sizeof(client_cert_pem);
    ret = actrust_json_unescape_string(value.value, value.value_len,
                                       client_cert_pem, sizeof(client_cert_pem),
                                       &client_cert_pem_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to unescape certificate PEM");
        return;
    }

    /* Write the certificate PEM to the security store. */
    ret = actrust_crypto_cert_write(ACTRUST_CLOUD_RUNTIME_CERT_ID,
                                    (const uint8_t *) client_cert_pem,
                                    client_cert_pem_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to set client certificate");
        return;
    }

    /* Get the ownership token. */
    ret = actrust_json_query(&json_view,
                             CLOUD_AWS_JSON_KEY_CERT_OWNERSHIP_TOKEN, &value);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to query certificateOwnershipToken");
        return;
    }

    ret = actrust_json_get_string(&value, aws_ctx->ownership_token,
                                  sizeof(aws_ctx->ownership_token),
                                  &aws_ctx->ownership_token_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to get ownership token");
        return;
    }

    actrust_cloud_aws_set_registration_phase(cloud, CLOUD_AWS_CSR_ACCEPTED);
}

static void actrust_cloud_aws_handle_csr_rejected(
    actrust_cloud_t cloud, const actrust_mqtt_message_t *msg)
{
    if (cloud == NULL || msg == NULL) {
        return;
    }

    actrust_cloud_aws_set_registration_phase(cloud, CLOUD_AWS_CSR_REJECTED);

    LOG_ERROR("CSR rejected: payload_len=%zu payload=%.*s", msg->payload_len,
              (int) msg->payload_len, (const char *) msg->payload);
}

/*
 * Expected payload shape (see AWS IoT RegisterThing response):
 *   { "deviceConfiguration": {}, "thingName": "<thing>" }
 */
static void actrust_cloud_aws_handle_register_accepted(
    actrust_cloud_t cloud, const actrust_mqtt_message_t *msg)
{
    if (cloud == NULL || msg == NULL) {
        return;
    }

    /* Parse the JSON payload. */
    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        LOG_ERROR("Invalid AWS context");
        return;
    }

    actrust_json_view_t json_view;
    actrust_err_t       ret = actrust_json_init(
        &json_view, (const char *) msg->payload, msg->payload_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to initialize JSON view");
        return;
    }

    /* Get the thing name. */
    actrust_json_value_t value;
    ret = actrust_json_query(&json_view, CLOUD_AWS_JSON_KEY_THING_NAME, &value);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to query thingName");
        return;
    }

    ret = actrust_json_get_string(&value, aws_ctx->thing_name,
                                  sizeof(aws_ctx->thing_name),
                                  &aws_ctx->thing_name_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to get thingName");
        return;
    }

    ret = actrust_cloud_aws_validate_thing_name(aws_ctx);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Registration thingName does not match device identity.");
        actrust_cloud_aws_set_registration_phase(cloud,
                                                 CLOUD_AWS_REGISTER_REJECTED);
        return;
    }

    /* Store the thing name in the KV namespace. */
    actrust_kv_t kv;
    ret = actrust_kv_open(CLOUD_AWS_KV_NAMESPACE,
                          strlen(CLOUD_AWS_KV_NAMESPACE), &kv);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to open KV namespace");
        return;
    }

    ret = actrust_kv_set(kv, CLOUD_AWS_THING_NAME_KV_KEY,
                         strlen(CLOUD_AWS_THING_NAME_KV_KEY),
                         aws_ctx->thing_name, aws_ctx->thing_name_len);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to set thingName");
        (void) actrust_kv_close(kv);
        return;
    }

    ret = actrust_kv_close(kv);
    if (ret != ACTRUST_OK) {
        LOG_ERROR("Failed to close KV namespace");
        return;
    }

    actrust_cloud_aws_set_registration_phase(cloud,
                                             CLOUD_AWS_REGISTER_ACCEPTED);
}

static void actrust_cloud_aws_handle_register_rejected(
    actrust_cloud_t cloud, const actrust_mqtt_message_t *msg)
{
    if (cloud == NULL || msg == NULL) {
        return;
    }

    actrust_cloud_aws_set_registration_phase(cloud,
                                             CLOUD_AWS_REGISTER_REJECTED);

    LOG_ERROR("Register rejected: payload_len=%zu payload=%.*s",
              msg->payload_len, (int) msg->payload_len,
              (const char *) msg->payload);
}

static void actrust_cloud_aws_handle_shadow_update_accepted(
    actrust_cloud_t cloud, const actrust_mqtt_message_t *msg)
{
    if (cloud == NULL || msg == NULL) {
        return;
    }

    LOG_INFO("Shadow update accepted: payload_len=%zu", msg->payload_len);
}

static void actrust_cloud_aws_handle_shadow_update_rejected(
    actrust_cloud_t cloud, const actrust_mqtt_message_t *msg)
{
    if (cloud == NULL || msg == NULL) {
        return;
    }

    LOG_WARN("Shadow update rejected: payload_len=%zu", msg->payload_len);
}

void actrust_cloud_aws_registration_callback(
    void *user_ctx, const actrust_mqtt_message_t *message)
{
    if (user_ctx == NULL || message == NULL) {
        return;
    }

    actrust_cloud_t cloud = (actrust_cloud_t) user_ctx;

    /* Handle CSR response. */
    if (actrust_cloud_aws_topic_match(message,
                                      CLOUD_AWS_CREATE_CERT_ACCEPTED_TOPIC)) {
        actrust_cloud_aws_handle_csr_accepted(cloud, message);
    }
    /* Handle CSR rejection. */
    else if (actrust_cloud_aws_topic_match(
                 message, CLOUD_AWS_CREATE_CERT_REJECTED_TOPIC)) {
        actrust_cloud_aws_handle_csr_rejected(cloud, message);
    }
    /* Handle register accepted response. */
    else if (actrust_cloud_aws_topic_match(
                 message, CLOUD_AWS_FLEET_PROVISION_ACCEPTED_TOPIC)) {
        actrust_cloud_aws_handle_register_accepted(cloud, message);
    }
    /* Handle register rejected response. */
    else if (actrust_cloud_aws_topic_match(
                 message, CLOUD_AWS_FLEET_PROVISION_REJECTED_TOPIC)) {
        actrust_cloud_aws_handle_register_rejected(cloud, message);
    }
    /* Handle unknown topic. */
    else {
        LOG_WARN("Received message on unknown topic: %.*s",
                 (int) ((message)->topic_len),
                 (const char *) ((message)->topic));
        return;
    }
}

void actrust_cloud_aws_runtime_callback(void                         *user_ctx,
                                        const actrust_mqtt_message_t *message)
{
    if (user_ctx == NULL || message == NULL) {
        return;
    }

    actrust_cloud_t          cloud = (actrust_cloud_t) user_ctx;
    actrust_cloud_aws_ctx_t *aws_ctx =
        (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
    if (aws_ctx == NULL) {
        return;
    }

    /* Handle register challenge command. */
    if (actrust_cloud_aws_topic_match(
            message, aws_ctx->register_topics.register_challenge_topic)) {
        actrust_cloud_aws_queue_register_command(cloud, message);
    }
    /* Handle register result command. */
    else if (actrust_cloud_aws_topic_match(
                 message, aws_ctx->register_topics.register_result_topic)) {
        actrust_cloud_aws_queue_register_command(cloud, message);
    }
    /* Handle shadow update accepted. */
    else if (actrust_cloud_aws_topic_match(
                 message,
                 aws_ctx->shadow_topics.shadow_update_accepted_topic)) {
        actrust_cloud_aws_handle_shadow_update_accepted(cloud, message);
    }
    /* Handle shadow update rejected. */
    else if (actrust_cloud_aws_topic_match(
                 message,
                 aws_ctx->shadow_topics.shadow_update_rejected_topic)) {
        actrust_cloud_aws_handle_shadow_update_rejected(cloud, message);
    }
    /* Handle unknown topic. */
    else {
        LOG_WARN("Received message on unknown topic: %.*s",
                 (int) ((message)->topic_len),
                 (const char *) ((message)->topic));
        return;
    }
}
