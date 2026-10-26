// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* Cloud */
#include "cloud/cloud.h"
#include "cloud/cloud_internal.h"
#include "cloud_aws.h"

/* Component */
#include "kv/kv.h"
#include "queue/queue.h"

/* Adapter */
#include "adapter/system.h"

#define TEST_SHADOW_UPDATE_TOPIC    "test/shadow/update"
#define TEST_REGISTER_REQUEST_TOPIC "actrust/things/test-thing/register/request"
#define TEST_REGISTER_CHALLENGE_TOPIC                                          \
    "actrust/things/test-thing/register/challenge"
#define TEST_REGISTER_RESPONSE_TOPIC                                           \
    "actrust/things/test-thing/register/response"
#define TEST_REGISTER_RESULT_TOPIC "actrust/things/test-thing/register/result"

static bool            s_fail_mqtt_deinit;
static bool            s_fail_queue_destroy;
static bool            s_fail_task_join;
static actrust_queue_t s_queue_destroy_target;
static size_t          s_mqtt_deinit_calls;
static size_t          s_queue_destroy_calls;
static size_t          s_task_join_calls;

#ifdef ACTRUST_TEST_WRAP_CLOUD_IDENTITY
typedef enum {
    TEST_ACCESS_NONE,
    TEST_ACCESS_KV_OPEN,
    TEST_ACCESS_KV_GET,
    TEST_ACCESS_KV_CLOSE,
    TEST_ACCESS_CERT_READ,
    TEST_ACCESS_KEY_OPEN,
} test_identity_access_t;

static bool                   s_identity_probe;
static test_identity_access_t s_identity_access;
static actrust_err_t          s_identity_error;
static size_t                 s_claim_cert_reads;

#define TEST_PROVISION_STOP_ERROR                                              \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_CLOUD, ACTRUST_ERR_BAD_STATE)
#define TEST_PUBLIC_KEY_MAX 128u

extern actrust_err_t __real_actrust_kv_open(const char *ns, size_t ns_len,
                                            actrust_kv_t *out_kv);
extern actrust_err_t __real_actrust_kv_get(actrust_kv_t kv, const char *key,
                                           size_t key_len, void *out_value,
                                           size_t value_cap, size_t *out_len);
extern actrust_err_t __real_actrust_kv_close(actrust_kv_t kv);
extern actrust_err_t __real_actrust_crypto_cert_read(uint32_t cert_id,
                                                     uint8_t *out,
                                                     size_t   out_cap,
                                                     size_t  *out_len);
extern actrust_err_t __real_actrust_crypto_key_open(
    actrust_crypto_ctx_t ctx, uint32_t key_id, actrust_crypto_key_t *out_key);

actrust_err_t __wrap_actrust_kv_open(const char *ns, size_t ns_len,
                                     actrust_kv_t *out_kv)
{
    if (s_identity_probe && s_identity_access == TEST_ACCESS_KV_OPEN) {
        return s_identity_error;
    }
    return __real_actrust_kv_open(ns, ns_len, out_kv);
}

actrust_err_t __wrap_actrust_kv_get(actrust_kv_t kv, const char *key,
                                    size_t key_len, void *out_value,
                                    size_t value_cap, size_t *out_len)
{
    if (s_identity_probe && s_identity_access == TEST_ACCESS_KV_GET) {
        return s_identity_error;
    }
    return __real_actrust_kv_get(kv, key, key_len, out_value, value_cap,
                                 out_len);
}

actrust_err_t __wrap_actrust_kv_close(actrust_kv_t kv)
{
    actrust_err_t err = __real_actrust_kv_close(kv);
    if (s_identity_probe && s_identity_access == TEST_ACCESS_KV_CLOSE &&
        err == ACTRUST_OK) {
        return s_identity_error;
    }
    return err;
}

actrust_err_t __wrap_actrust_crypto_cert_read(uint32_t cert_id, uint8_t *out,
                                              size_t out_cap, size_t *out_len)
{
    if (s_identity_probe && cert_id == ACTRUST_CLOUD_CLAIM_CERT_ID) {
        /* Detect provisioning and stop before any network activity. */
        s_claim_cert_reads++;
        return TEST_PROVISION_STOP_ERROR;
    }
    if (s_identity_probe && s_identity_access == TEST_ACCESS_CERT_READ &&
        cert_id == ACTRUST_CLOUD_RUNTIME_CERT_ID) {
        return s_identity_error;
    }
    return __real_actrust_crypto_cert_read(cert_id, out, out_cap, out_len);
}

actrust_err_t __wrap_actrust_crypto_key_open(actrust_crypto_ctx_t  ctx,
                                             uint32_t              key_id,
                                             actrust_crypto_key_t *out_key)
{
    if (s_identity_probe && s_identity_access == TEST_ACCESS_KEY_OPEN &&
        key_id == ACTRUST_CLOUD_RUNTIME_KEY_ID) {
        return s_identity_error;
    }
    return __real_actrust_crypto_key_open(ctx, key_id, out_key);
}
#endif

#ifdef ACTRUST_TEST_WRAP_CLOUD_TEARDOWN
extern actrust_err_t __real_actrust_mqtt_deinit(actrust_mqtt_t mqtt);
extern actrust_err_t __real_actrust_queue_destroy(actrust_queue_t *queue);
extern actrust_err_t __real_actrust_task_join(actrust_task_t task,
                                              uint32_t       timeout_ms);

actrust_err_t __wrap_actrust_mqtt_deinit(actrust_mqtt_t mqtt)
{
    s_mqtt_deinit_calls++;
    if (s_fail_mqtt_deinit == true) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_MQTT,
                           ACTRUST_ERR_BAD_STATE);
    }
    return __real_actrust_mqtt_deinit(mqtt);
}

actrust_err_t __wrap_actrust_queue_destroy(actrust_queue_t *queue)
{
    s_queue_destroy_calls++;
    if (s_fail_queue_destroy == true && *queue == s_queue_destroy_target) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_QUEUE,
                           ACTRUST_ERR_HW_FAILURE);
    }
    return __real_actrust_queue_destroy(queue);
}

actrust_err_t __wrap_actrust_task_join(actrust_task_t task, uint32_t timeout_ms)
{
    s_task_join_calls++;
    if (s_fail_task_join == true) {
        return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                           ACTRUST_ERR_TIMEOUT);
    }
    return __real_actrust_task_join(task, timeout_ms);
}
#endif

void setUp(void)
{
#ifdef ACTRUST_TEST_WRAP_CLOUD_IDENTITY
    s_identity_probe   = false;
    s_identity_access  = TEST_ACCESS_NONE;
    s_identity_error   = ACTRUST_OK;
    s_claim_cert_reads = 0u;
#endif
#ifdef ACTRUST_TEST_WRAP_CLOUD_TEARDOWN
    s_fail_mqtt_deinit     = false;
    s_fail_queue_destroy   = false;
    s_fail_task_join       = false;
    s_mqtt_deinit_calls    = 0u;
    s_queue_destroy_calls  = 0u;
    s_task_join_calls      = 0u;
    s_queue_destroy_target = NULL;
#endif
}

void tearDown(void)
{
}

static void test_set_string(char *out, size_t out_len, const char *value)
{
    size_t value_len = strlen(value);

    TEST_ASSERT_LESS_THAN(out_len, value_len);
    memcpy(out, value, value_len + 1u);
}

static actrust_cloud_aws_ctx_t *test_get_aws_ctx(actrust_cloud_t cloud)
{
    TEST_ASSERT_NOT_NULL(cloud);
    TEST_ASSERT_NOT_NULL(cloud->provider_ctx);
    return (actrust_cloud_aws_ctx_t *) cloud->provider_ctx;
}

static void test_configure_runtime_topics(actrust_cloud_t cloud)
{
    actrust_cloud_aws_ctx_t *aws_ctx = test_get_aws_ctx(cloud);

    test_set_string(aws_ctx->shadow_topics.shadow_update_topic,
                    sizeof(aws_ctx->shadow_topics.shadow_update_topic),
                    TEST_SHADOW_UPDATE_TOPIC);
    test_set_string(aws_ctx->register_topics.register_request_topic,
                    sizeof(aws_ctx->register_topics.register_request_topic),
                    TEST_REGISTER_REQUEST_TOPIC);
    test_set_string(aws_ctx->register_topics.register_challenge_topic,
                    sizeof(aws_ctx->register_topics.register_challenge_topic),
                    TEST_REGISTER_CHALLENGE_TOPIC);
    test_set_string(aws_ctx->register_topics.register_response_topic,
                    sizeof(aws_ctx->register_topics.register_response_topic),
                    TEST_REGISTER_RESPONSE_TOPIC);
    test_set_string(aws_ctx->register_topics.register_result_topic,
                    sizeof(aws_ctx->register_topics.register_result_topic),
                    TEST_REGISTER_RESULT_TOPIC);
}

static void test_dispatch_runtime_message(actrust_cloud_t cloud,
                                          const char     *topic,
                                          const char     *payload)
{
    actrust_mqtt_message_t msg = {
        .topic       = (char *) topic,
        .topic_len   = (uint16_t) strlen(topic),
        .payload     = (uint8_t *) payload,
        .payload_len = strlen(payload),
        .qos         = ACTRUST_MQTT_QOS0,
    };

    actrust_cloud_aws_runtime_callback(cloud, &msg);
}

static size_t test_queue_size(actrust_queue_t q)
{
    size_t size = 0u;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_size(q, &size));
    return size;
}

static void test_completed_task_entry(void *arg)
{
    (void) arg;
}

void test_init_null_cloud(void)
{
    actrust_queue_t q = NULL;
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS, NULL, &q));
}

void test_init_null_queue(void)
{
    actrust_cloud_t c = NULL;
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS, &c, NULL));
}

void test_init_unsupported_provider(void)
{
    actrust_cloud_t c = NULL;
    actrust_queue_t q = NULL;
    TEST_ASSERT_NOT_EQUAL(
        ACTRUST_OK, actrust_cloud_init((actrust_cloud_provider_t) 99, &c, &q));
}

void test_init_deinit_aws(void)
{
    actrust_cloud_t c = NULL;
    actrust_queue_t q = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS, &c, &q));
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_deinit(c));
}

void test_deinit_preserves_provider_after_join_failure(void)
{
    actrust_cloud_t cloud = NULL;
    actrust_queue_t queue = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS,
                                                     &cloud, &queue));
    actrust_cloud_aws_ctx_t *aws_ctx = test_get_aws_ctx(cloud);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_create(
                                      &aws_ctx->mqtt_process_task, "cloud_join",
                                      test_completed_task_entry, NULL, 0u, 0u));

    s_fail_task_join = true;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CLOUD_DEINIT_FAILED,
                      ACTRUST_ERR_CODE(actrust_cloud_deinit(cloud)));
    TEST_ASSERT_NOT_NULL(cloud->provider_ctx);
    TEST_ASSERT_NOT_NULL(aws_ctx->mqtt_process_task);
    TEST_ASSERT_EQUAL_UINT(1u, s_task_join_calls);
    TEST_ASSERT_EQUAL_UINT(0u, s_mqtt_deinit_calls);

    s_fail_task_join = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_deinit(cloud));
    TEST_ASSERT_EQUAL_UINT(2u, s_task_join_calls);
    TEST_ASSERT_EQUAL_UINT(1u, s_mqtt_deinit_calls);
}

void test_deinit_propagates_mqtt_failure(void)
{
    actrust_cloud_t cloud = NULL;
    actrust_queue_t queue = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS,
                                                     &cloud, &queue));
    s_fail_mqtt_deinit = true;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CLOUD_DEINIT_FAILED,
                      ACTRUST_ERR_CODE(actrust_cloud_deinit(cloud)));
    TEST_ASSERT_NOT_NULL(cloud);
    TEST_ASSERT_NOT_NULL(cloud->provider_ctx);
    TEST_ASSERT_EQUAL_UINT(1u, s_mqtt_deinit_calls);

    s_fail_mqtt_deinit = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_deinit(cloud));
    TEST_ASSERT_EQUAL_UINT(2u, s_mqtt_deinit_calls);
}

void test_deinit_does_not_repeat_provider_after_queue_failure(void)
{
    actrust_cloud_t cloud = NULL;
    actrust_queue_t queue = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS,
                                                     &cloud, &queue));
    s_fail_queue_destroy   = true;
    s_queue_destroy_target = queue;
    TEST_ASSERT_EQUAL(ACTRUST_ERR_CLOUD_DEINIT_FAILED,
                      ACTRUST_ERR_CODE(actrust_cloud_deinit(cloud)));
    TEST_ASSERT_EQUAL_UINT(1u, s_mqtt_deinit_calls);
    TEST_ASSERT_NOT_NULL(cloud);

    s_fail_queue_destroy = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_deinit(cloud));
    TEST_ASSERT_EQUAL_UINT(1u, s_mqtt_deinit_calls);
}

void test_deinit_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_cloud_deinit(NULL));
}

void test_connect_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_cloud_connect(NULL));
}

void test_disconnect_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_cloud_disconnect(NULL));
}

void test_send_data_null_cloud(void)
{
    const uint8_t payload[] = "x";
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_cloud_send_data(NULL, payload, 1));
}

void test_send_data_null_payload(void)
{
    actrust_cloud_t c = NULL;
    actrust_queue_t q = NULL;
    actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS, &c, &q);

    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_cloud_send_data(c, NULL, 1));

    actrust_cloud_deinit(c);
}

void test_send_data_before_connect(void)
{
    actrust_cloud_t c = NULL;
    actrust_queue_t q = NULL;
    actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS, &c, &q);

    const uint8_t payload[] = "data";
    actrust_err_t err       = actrust_cloud_send_data(c, payload, 4);
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, err);

    actrust_cloud_deinit(c);
}

void test_send_register_null_cloud(void)
{
    const uint8_t payload[] = "{}";
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_cloud_send_register(
                                          NULL, ACTRUST_CLOUD_REGISTER_REQUEST,
                                          payload, sizeof(payload) - 1u));
}

void test_send_register_null_payload(void)
{
    actrust_cloud_t c = NULL;
    actrust_queue_t q = NULL;
    actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS, &c, &q);

    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK,
                          actrust_cloud_send_register(
                              c, ACTRUST_CLOUD_REGISTER_REQUEST, NULL, 1u));

    actrust_cloud_deinit(c);
}

void test_send_register_invalid_type(void)
{
    actrust_cloud_t c = NULL;
    actrust_queue_t q = NULL;
    actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS, &c, &q);

    const uint8_t payload[] = "{}";
    actrust_err_t err =
        actrust_cloud_send_register(c, (actrust_cloud_register_msg_type_t) 99u,
                                    payload, sizeof(payload) - 1u);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));

    actrust_cloud_deinit(c);
}

void test_register_topic_downlinks_message(void)
{
    static const char payload[] =
        "{\"action\":4097,\"session_id\":\"abc\",\"nonce\":\"n\"}";

    actrust_cloud_t c = NULL;
    actrust_queue_t q = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS, &c, &q));
    test_configure_runtime_topics(c);

    test_dispatch_runtime_message(c, TEST_REGISTER_CHALLENGE_TOPIC, payload);

    TEST_ASSERT_EQUAL_UINT(1u, test_queue_size(q));

    actrust_cloud_msg_t downlink;
    memset(&downlink, 0, sizeof(downlink));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_queue_pop(q, &downlink, 0u));
    TEST_ASSERT_EQUAL_UINT(strlen(payload), downlink.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, downlink.payload, strlen(payload));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_deinit(c));
}

void test_thing_name_validation_accepts_bound_identity(void)
{
    actrust_cloud_t c = NULL;
    actrust_queue_t q = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS, &c, &q));

    actrust_cloud_aws_ctx_t *aws_ctx = test_get_aws_ctx(c);
    test_set_string(aws_ctx->device_name, sizeof(aws_ctx->device_name),
                    "device123");

    char expected_thing_name[CLOUD_AWS_THING_NAME_MAX_LEN];
    int  n =
        snprintf(expected_thing_name, sizeof(expected_thing_name), "%s-%s",
                 CONFIG_ACTRUST_CLOUD_AWS_PRODUCT_KEY, aws_ctx->device_name);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(expected_thing_name));
    test_set_string(aws_ctx->thing_name, sizeof(aws_ctx->thing_name),
                    expected_thing_name);
    aws_ctx->thing_name_len = strlen(aws_ctx->thing_name);

    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_cloud_aws_validate_thing_name(aws_ctx));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_deinit(c));
}

void test_thing_name_validation_rejects_mismatched_identity(void)
{
    actrust_cloud_t c = NULL;
    actrust_queue_t q = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS, &c, &q));

    actrust_cloud_aws_ctx_t *aws_ctx = test_get_aws_ctx(c);
    test_set_string(aws_ctx->device_name, sizeof(aws_ctx->device_name),
                    "device123");
    test_set_string(aws_ctx->thing_name, sizeof(aws_ctx->thing_name),
                    "other-thing");
    aws_ctx->thing_name_len = strlen(aws_ctx->thing_name);

    actrust_err_t err = actrust_cloud_aws_validate_thing_name(aws_ctx);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE, ACTRUST_ERR_CODE(err));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_deinit(c));
}

#ifdef ACTRUST_TEST_WRAP_CLOUD_IDENTITY
static void check_runtime_identity_access(test_identity_access_t access,
                                          actrust_err_t          error,
                                          test_identity_access_t missing)
{
    static const uint8_t cert[] = "test runtime certificate";
    actrust_cloud_t      cloud  = NULL;
    actrust_queue_t      queue  = NULL;
    actrust_kv_t         kv     = NULL;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_init(ACTRUST_CLOUD_PROVIDER_AWS,
                                                     &cloud, &queue));
    actrust_cloud_aws_ctx_t *aws_ctx = test_get_aws_ctx(cloud);
    char                     thing_name[CLOUD_AWS_THING_NAME_MAX_LEN];
    int                      n =
        snprintf(thing_name, sizeof(thing_name), "%s-%s",
                 CONFIG_ACTRUST_CLOUD_AWS_PRODUCT_KEY, aws_ctx->device_name);
    TEST_ASSERT_TRUE(n > 0 && (size_t) n < sizeof(thing_name));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_init());
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_open(CLOUD_AWS_KV_NAMESPACE,
                                      strlen(CLOUD_AWS_KV_NAMESPACE), &kv));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_set(kv, CLOUD_AWS_THING_NAME_KV_KEY,
                                     strlen(CLOUD_AWS_THING_NAME_KV_KEY),
                                     thing_name, (size_t) n));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_cert_write(ACTRUST_CLOUD_RUNTIME_CERT_ID,
                                                cert, sizeof(cert) - 1u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_generate(aws_ctx->crypto_ctx,
                                                  ACTRUST_CLOUD_RUNTIME_KEY_ID,
                                                  &aws_ctx->client_key));
    actrust_crypto_key_t original_key = aws_ctx->client_key;
    uint8_t              expected_public[TEST_PUBLIC_KEY_MAX];
    size_t               expected_len = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_crypto_key_export_public(
                          aws_ctx->crypto_ctx, original_key, expected_public,
                          sizeof(expected_public), &expected_len));

    if (missing == TEST_ACCESS_KV_GET) {
        TEST_ASSERT_EQUAL(ACTRUST_OK,
                          actrust_kv_del(kv, CLOUD_AWS_THING_NAME_KV_KEY,
                                         strlen(CLOUD_AWS_THING_NAME_KV_KEY)));
    } else if (missing == TEST_ACCESS_CERT_READ) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_cert_delete(
                                          ACTRUST_CLOUD_RUNTIME_CERT_ID));
    } else if (missing == TEST_ACCESS_KEY_OPEN) {
        TEST_ASSERT_EQUAL(
            ACTRUST_OK, actrust_crypto_key_destroy(
                            aws_ctx->crypto_ctx, ACTRUST_CLOUD_RUNTIME_KEY_ID));
    }
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_close(kv));

    s_identity_access         = access;
    s_identity_error          = error;
    s_claim_cert_reads        = 0u;
    s_identity_probe          = true;
    actrust_err_t result      = cloud->provider_ops->connect(cloud);
    s_identity_probe          = false;
    bool key_handle_preserved = aws_ctx->client_key == original_key;

    /* Read back the stored identity before cleaning up this fixture. */
    char   actual_name[sizeof(thing_name)];
    size_t name_len = 0u;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_kv_open(CLOUD_AWS_KV_NAMESPACE,
                                      strlen(CLOUD_AWS_KV_NAMESPACE), &kv));
    actrust_err_t name_result = actrust_kv_get(
        kv, CLOUD_AWS_THING_NAME_KV_KEY, strlen(CLOUD_AWS_THING_NAME_KV_KEY),
        actual_name, sizeof(actual_name), &name_len);
    uint8_t       actual_cert[sizeof(cert)];
    size_t        cert_len = 0u;
    actrust_err_t cert_result =
        actrust_crypto_cert_read(ACTRUST_CLOUD_RUNTIME_CERT_ID, actual_cert,
                                 sizeof(actual_cert), &cert_len);
    uint8_t              actual_public[TEST_PUBLIC_KEY_MAX];
    size_t               actual_len = 0u;
    actrust_crypto_key_t stored_key = NULL;
    actrust_err_t        key_result = actrust_crypto_key_open(
        aws_ctx->crypto_ctx, ACTRUST_CLOUD_RUNTIME_KEY_ID, &stored_key);
    if (key_result == ACTRUST_OK) {
        key_result = actrust_crypto_key_export_public(
            aws_ctx->crypto_ctx, stored_key, actual_public,
            sizeof(actual_public), &actual_len);
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_crypto_key_close(
                                          aws_ctx->crypto_ctx, &stored_key));
    }
    (void) actrust_kv_del(kv, CLOUD_AWS_THING_NAME_KV_KEY,
                          strlen(CLOUD_AWS_THING_NAME_KV_KEY));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_kv_close(kv));
    (void) actrust_crypto_cert_delete(ACTRUST_CLOUD_RUNTIME_CERT_ID);
    (void) actrust_crypto_key_destroy(aws_ctx->crypto_ctx,
                                      ACTRUST_CLOUD_RUNTIME_KEY_ID);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_cloud_deinit(cloud));

    if (missing != TEST_ACCESS_NONE && access == TEST_ACCESS_NONE) {
        TEST_ASSERT_EQUAL(TEST_PROVISION_STOP_ERROR, result);
        TEST_ASSERT_EQUAL_UINT(1u, s_claim_cert_reads);
        return;
    }
    TEST_ASSERT_EQUAL(error, result);
    TEST_ASSERT_EQUAL_UINT(0u, s_claim_cert_reads);
    TEST_ASSERT_TRUE(key_handle_preserved);
    if (missing == TEST_ACCESS_KV_GET) {
        TEST_ASSERT_EQUAL(ACTRUST_ERR_NO_RESOURCE,
                          ACTRUST_ERR_CODE(name_result));
    } else {
        TEST_ASSERT_EQUAL(ACTRUST_OK, name_result);
        TEST_ASSERT_EQUAL_UINT((size_t) n, name_len);
        TEST_ASSERT_EQUAL_MEMORY(thing_name, actual_name, name_len);
    }
    TEST_ASSERT_EQUAL(ACTRUST_OK, cert_result);
    TEST_ASSERT_EQUAL_UINT(sizeof(cert) - 1u, cert_len);
    TEST_ASSERT_EQUAL_MEMORY(cert, actual_cert, cert_len);
    TEST_ASSERT_EQUAL(ACTRUST_OK, key_result);
    TEST_ASSERT_EQUAL_UINT(expected_len, actual_len);
    TEST_ASSERT_EQUAL_MEMORY(expected_public, actual_public, expected_len);
}

static void check_runtime_access_errors(test_identity_access_t access)
{
    static const uint16_t reasons[] = {
        ACTRUST_ERR_IO,
        ACTRUST_ERR_NO_MEM,
        ACTRUST_ERR_BUF_TOO_SMALL,
    };
    uint16_t module = access <= TEST_ACCESS_KV_CLOSE
                          ? ACTRUST_ERR_MODULE_COMPONENTS_KV
                          : ACTRUST_ERR_MODULE_ADAPTER_SECURITY;
    for (size_t i = 0u; i < sizeof(reasons) / sizeof(reasons[0]); ++i) {
        check_runtime_identity_access(access, ACTRUST_ERR(module, reasons[i]),
                                      TEST_ACCESS_NONE);
    }
}
#endif

void test_runtime_kv_errors_preserve_identity(void)
{
#ifdef ACTRUST_TEST_WRAP_CLOUD_IDENTITY
    check_runtime_access_errors(TEST_ACCESS_KV_OPEN);
    check_runtime_access_errors(TEST_ACCESS_KV_GET);
    check_runtime_access_errors(TEST_ACCESS_KV_CLOSE);
#else
    TEST_IGNORE_MESSAGE("Identity linker wrapping is unavailable");
#endif
}

void test_runtime_certificate_errors_preserve_identity(void)
{
#ifdef ACTRUST_TEST_WRAP_CLOUD_IDENTITY
    check_runtime_access_errors(TEST_ACCESS_CERT_READ);
#else
    TEST_IGNORE_MESSAGE("Identity linker wrapping is unavailable");
#endif
}

void test_runtime_key_errors_preserve_identity(void)
{
#ifdef ACTRUST_TEST_WRAP_CLOUD_IDENTITY
    check_runtime_access_errors(TEST_ACCESS_KEY_OPEN);
#else
    TEST_IGNORE_MESSAGE("Identity linker wrapping is unavailable");
#endif
}

void test_runtime_no_resource_requires_confirmed_missing_identity(void)
{
#ifdef ACTRUST_TEST_WRAP_CLOUD_IDENTITY
    actrust_err_t error =
        ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_KV, ACTRUST_ERR_NO_RESOURCE);
    check_runtime_identity_access(TEST_ACCESS_KV_OPEN, error, TEST_ACCESS_NONE);
    check_runtime_identity_access(TEST_ACCESS_KV_GET, error, TEST_ACCESS_NONE);
#else
    TEST_IGNORE_MESSAGE("Identity linker wrapping is unavailable");
#endif
}

void test_runtime_missing_identity_uses_provisioning(void)
{
#ifdef ACTRUST_TEST_WRAP_CLOUD_IDENTITY
    check_runtime_identity_access(TEST_ACCESS_NONE, ACTRUST_OK,
                                  TEST_ACCESS_KV_GET);
    check_runtime_identity_access(TEST_ACCESS_NONE, ACTRUST_OK,
                                  TEST_ACCESS_CERT_READ);
    check_runtime_identity_access(TEST_ACCESS_NONE, ACTRUST_OK,
                                  TEST_ACCESS_KEY_OPEN);
#else
    TEST_IGNORE_MESSAGE("Identity linker wrapping is unavailable");
#endif
}

void test_runtime_missing_thing_name_preserves_close_error(void)
{
#ifdef ACTRUST_TEST_WRAP_CLOUD_IDENTITY
    check_runtime_identity_access(
        TEST_ACCESS_KV_CLOSE,
        ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_KV, ACTRUST_ERR_IO),
        TEST_ACCESS_KV_GET);
#else
    TEST_IGNORE_MESSAGE("Identity linker wrapping is unavailable");
#endif
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_null_cloud);
    RUN_TEST(test_init_null_queue);
    RUN_TEST(test_init_unsupported_provider);
    RUN_TEST(test_init_deinit_aws);
    RUN_TEST(test_deinit_preserves_provider_after_join_failure);
    RUN_TEST(test_deinit_propagates_mqtt_failure);
    RUN_TEST(test_deinit_does_not_repeat_provider_after_queue_failure);
    RUN_TEST(test_deinit_null);
    RUN_TEST(test_connect_null);
    RUN_TEST(test_disconnect_null);
    RUN_TEST(test_send_data_null_cloud);
    RUN_TEST(test_send_data_null_payload);
    RUN_TEST(test_send_data_before_connect);
    RUN_TEST(test_send_register_null_cloud);
    RUN_TEST(test_send_register_null_payload);
    RUN_TEST(test_send_register_invalid_type);
    RUN_TEST(test_register_topic_downlinks_message);
    RUN_TEST(test_thing_name_validation_accepts_bound_identity);
    RUN_TEST(test_thing_name_validation_rejects_mismatched_identity);
    RUN_TEST(test_runtime_kv_errors_preserve_identity);
    RUN_TEST(test_runtime_certificate_errors_preserve_identity);
    RUN_TEST(test_runtime_key_errors_preserve_identity);
    RUN_TEST(test_runtime_no_resource_requires_confirmed_missing_identity);
    RUN_TEST(test_runtime_missing_identity_uses_provisioning);
    RUN_TEST(test_runtime_missing_thing_name_preserves_close_error);
    return UNITY_END();
}
