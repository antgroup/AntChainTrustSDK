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
#include "queue/queue.h"

#define TEST_SHADOW_UPDATE_TOPIC    "test/shadow/update"
#define TEST_REGISTER_REQUEST_TOPIC "actrust/things/test-thing/register/request"
#define TEST_REGISTER_CHALLENGE_TOPIC                                          \
    "actrust/things/test-thing/register/challenge"
#define TEST_REGISTER_RESPONSE_TOPIC                                           \
    "actrust/things/test-thing/register/response"
#define TEST_REGISTER_RESULT_TOPIC "actrust/things/test-thing/register/result"

void setUp(void)
{
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_null_cloud);
    RUN_TEST(test_init_null_queue);
    RUN_TEST(test_init_unsupported_provider);
    RUN_TEST(test_init_deinit_aws);
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
    return UNITY_END();
}
