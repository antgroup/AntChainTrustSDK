// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file cloud_aws.h
 * @brief Internal shared definitions for AWS cloud provider.
 */

#ifndef ACTRUST_CLOUD_AWS_H
#define ACTRUST_CLOUD_AWS_H

/* Cloud */
#include "cloud/cloud_internal.h"

/* Component */
#include "crypto/crypto.h"
#include "mqtt/mqtt.h"

/* Adapter */
#include "adapter/system.h"

#define CLOUD_ERR(reason)                                                      \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_CLOUD, (reason))

/* ========================================================================
 * AWS MQTT topics
 * ======================================================================== */

#define CLOUD_AWS_CREATE_CERT_TOPIC "$aws/certificates/create-from-csr/json"
#define CLOUD_AWS_CREATE_CERT_ACCEPTED_TOPIC                                   \
    "$aws/certificates/create-from-csr/json/accepted"
#define CLOUD_AWS_CREATE_CERT_REJECTED_TOPIC                                   \
    "$aws/certificates/create-from-csr/json/rejected"

#define CLOUD_AWS_FLEET_PROVISION_TOPIC                                        \
    "$aws/provisioning-templates/" CONFIG_ACTRUST_CLOUD_AWS_TEMPLATE_NAME      \
    "/provision/json"
#define CLOUD_AWS_FLEET_PROVISION_ACCEPTED_TOPIC                               \
    "$aws/provisioning-templates/" CONFIG_ACTRUST_CLOUD_AWS_TEMPLATE_NAME      \
    "/provision/json/accepted"
#define CLOUD_AWS_FLEET_PROVISION_REJECTED_TOPIC                               \
    "$aws/provisioning-templates/" CONFIG_ACTRUST_CLOUD_AWS_TEMPLATE_NAME      \
    "/provision/json/rejected"

#define CLOUD_AWS_SHADOW_UPDATE_TOPIC "$aws/things/%s/shadow/update"
#define CLOUD_AWS_SHADOW_UPDATE_ACCEPTED_TOPIC                                 \
    "$aws/things/%s/shadow/update/accepted"
#define CLOUD_AWS_SHADOW_UPDATE_REJECTED_TOPIC                                 \
    "$aws/things/%s/shadow/update/rejected"

#define CLOUD_AWS_REGISTER_REQUEST_TOPIC "actrust/things/%s/register/request"
#define CLOUD_AWS_REGISTER_CHALLENGE_TOPIC                                     \
    "actrust/things/%s/register/challenge"
#define CLOUD_AWS_REGISTER_RESPONSE_TOPIC "actrust/things/%s/register/response"
#define CLOUD_AWS_REGISTER_RESULT_TOPIC   "actrust/things/%s/register/result"

/* ========================================================================
 * AWS JSON keys
 * ======================================================================== */

#define CLOUD_AWS_JSON_KEY_CERT_SIGNING_REQUEST "certificateSigningRequest"
#define CLOUD_AWS_JSON_KEY_PRODUCT_KEY          "productKey"
#define CLOUD_AWS_JSON_KEY_DEVICE_NAME          "deviceName"
#define CLOUD_AWS_JSON_KEY_CERT_OWNERSHIP_TOKEN "certificateOwnershipToken"
#define CLOUD_AWS_JSON_KEY_PARAMETERS           "parameters"
#define CLOUD_AWS_JSON_KEY_CERTIFICATE_PEM      "certificatePem"
#define CLOUD_AWS_JSON_KEY_THING_NAME           "thingName"
#define CLOUD_AWS_JSON_KEY_SHADOW_STATE         "state"
#define CLOUD_AWS_JSON_KEY_SHADOW_REPORTED      "reported"

#define CLOUD_AWS_DEVICE_NAME_MAX_LEN (64u)
#define CLOUD_AWS_THING_NAME_KV_KEY   "thing_name"
#define CLOUD_AWS_KV_NAMESPACE        "cloud_aws"

/* ========================================================================
 * AWS Constants
 * ======================================================================== */

#define CLOUD_AWS_CERT_PEM_MAX_LEN        (4096u)
#define CLOUD_AWS_CSR_DER_MAX_LEN         (512u)
#define CLOUD_AWS_CSR_PEM_MAX_LEN         (1024u)
#define CLOUD_AWS_JSON_BUF_MAX_LEN        CONFIG_ACTRUST_MQTT_PAYLOAD_MAX_LEN
#define CLOUD_AWS_OWNERSHIP_TOKEN_MAX_LEN (2048u)
#define CLOUD_AWS_TOPIC_MAX_LEN           (128u)
#define CLOUD_AWS_THING_NAME_MAX_LEN      (128u)
#define CLOUD_AWS_REGISTER_TIMEOUT_MS     30000

typedef enum {
    CLOUD_AWS_REQUEST_CERT = 0,
    CLOUD_AWS_WAIT_CSR_RESPONSE,
    CLOUD_AWS_CSR_ACCEPTED,
    CLOUD_AWS_CSR_REJECTED,
    CLOUD_AWS_REQUEST_REGISTER,
    CLOUD_AWS_WAIT_REGISTER_RESPONSE,
    CLOUD_AWS_REGISTER_ACCEPTED,
    CLOUD_AWS_REGISTER_REJECTED,
    CLOUD_AWS_FINISH,
} actrust_cloud_aws_registration_phase_t;

typedef struct {
    char shadow_update_topic[CLOUD_AWS_TOPIC_MAX_LEN];
    char shadow_update_accepted_topic[CLOUD_AWS_TOPIC_MAX_LEN];
    char shadow_update_rejected_topic[CLOUD_AWS_TOPIC_MAX_LEN];
} actrust_cloud_aws_shadow_topics_t;

typedef struct {
    char register_request_topic[CLOUD_AWS_TOPIC_MAX_LEN];
    char register_challenge_topic[CLOUD_AWS_TOPIC_MAX_LEN];
    char register_response_topic[CLOUD_AWS_TOPIC_MAX_LEN];
    char register_result_topic[CLOUD_AWS_TOPIC_MAX_LEN];
} actrust_cloud_aws_register_topics_t;

typedef struct {
    actrust_mqtt_t       mqtt_client;
    actrust_crypto_ctx_t crypto_ctx;

    actrust_crypto_key_t client_key;
    char                 client_cert_pem[CLOUD_AWS_CERT_PEM_MAX_LEN];
    size_t               client_cert_len;

    actrust_cloud_aws_registration_phase_t registration_phase;
    actrust_mutex_t                        mutex;
    actrust_sem_t                          phase_sem;

    char   ownership_token[CLOUD_AWS_OWNERSHIP_TOKEN_MAX_LEN];
    size_t ownership_token_len;
    char   thing_name[CLOUD_AWS_THING_NAME_MAX_LEN];
    size_t thing_name_len;

    char device_name[CLOUD_AWS_DEVICE_NAME_MAX_LEN];

    actrust_cloud_aws_shadow_topics_t   shadow_topics;
    actrust_cloud_aws_register_topics_t register_topics;
    actrust_task_t mqtt_process_task; /**< MQTT process task. */
} actrust_cloud_aws_ctx_t;

void actrust_cloud_aws_set_registration_phase(
    actrust_cloud_t cloud, actrust_cloud_aws_registration_phase_t phase);

void actrust_cloud_aws_get_registration_phase(
    actrust_cloud_t cloud, actrust_cloud_aws_registration_phase_t *phase);

actrust_err_t actrust_cloud_aws_validate_thing_name(
    const actrust_cloud_aws_ctx_t *aws_ctx);

void actrust_cloud_aws_registration_callback(
    void *user_ctx, const actrust_mqtt_message_t *message);

void actrust_cloud_aws_runtime_callback(void                         *user_ctx,
                                        const actrust_mqtt_message_t *message);

#endif /* ACTRUST_CLOUD_AWS_H */
