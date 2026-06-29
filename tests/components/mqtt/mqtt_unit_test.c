// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Third-party */
#include "unity.h"

/* MQTT */
#include "mqtt/mqtt.h"

/* Adapter */
#include "adapter/system.h"

#define TEST_MQTT_TOPIC_MAX_LEN (128u)

static actrust_mqtt_t mqtt;

typedef struct {
    actrust_mqtt_t mqtt;
    actrust_sem_t  returned_sem;
} mqtt_unit_process_arg_t;

static void mqtt_unit_process_task(void *arg)
{
    mqtt_unit_process_arg_t *process_arg = (mqtt_unit_process_arg_t *) arg;

    (void) actrust_mqtt_process(process_arg->mqtt);
    (void) actrust_sem_post(process_arg->returned_sem);
}

void setUp(void)
{
    mqtt = NULL;
    actrust_mqtt_init(&mqtt);
}

void tearDown(void)
{
    if (mqtt != NULL) {
        actrust_mqtt_deinit(mqtt);
        mqtt = NULL;
    }
}

void test_init_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_init(NULL));
}

void test_init_success(void)
{
    TEST_ASSERT_NOT_NULL(mqtt);
}

void test_deinit_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_deinit(NULL));
}

void test_deinit_stops_running_process(void)
{
    actrust_task_t          task         = NULL;
    actrust_sem_t           returned_sem = NULL;
    mqtt_unit_process_arg_t arg;
    actrust_err_t           deinit_err;
    actrust_err_t           returned_err;
    actrust_err_t           join_err;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&returned_sem, 0u));
    arg.mqtt         = mqtt;
    arg.returned_sem = returned_sem;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_create(&task, "mqtt_unit",
                                                      mqtt_unit_process_task,
                                                      (void *) &arg, 0u, 0u));

    actrust_sleep_ms(100u);

    deinit_err = actrust_mqtt_deinit(mqtt);
    if (deinit_err == ACTRUST_OK) {
        mqtt = NULL;
    }

    returned_err = actrust_sem_wait(returned_sem, 500u);
    join_err     = actrust_task_join(task, 1000u);
    (void) actrust_sem_destroy(returned_sem);

    TEST_ASSERT_EQUAL(ACTRUST_OK, deinit_err);
    TEST_ASSERT_EQUAL(ACTRUST_OK, returned_err);
    TEST_ASSERT_EQUAL(ACTRUST_OK, join_err);
}

void test_set_callbacks_null_handle(void)
{
    actrust_mqtt_callbacks_t cbs = { 0 };
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_set_callbacks(NULL, &cbs));
}

void test_set_callbacks_success(void)
{
    actrust_mqtt_callbacks_t cbs = { .on_message = NULL, .user_ctx = NULL };
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_set_callbacks(mqtt, &cbs));
}

void test_connect_null_handle(void)
{
    actrust_mqtt_config_t cfg = { .client_id = "id",
                                  .transport.type =
                                      ACTRUST_MQTT_TRANSPORT_TCP };
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_connect(NULL, &cfg));
}

void test_disconnect_null_handle(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_disconnect(NULL));
}

void test_publish_null_handle(void)
{
    actrust_mqtt_message_t msg = {
        .topic       = "t",
        .topic_len   = 1,
        .payload     = (uint8_t *) "p",
        .payload_len = 1,
        .qos         = ACTRUST_MQTT_QOS0,
    };
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_publish(NULL, &msg));
}

void test_publish_before_connect(void)
{
    actrust_mqtt_message_t msg = {
        .topic       = "t",
        .topic_len   = 1,
        .payload     = (uint8_t *) "p",
        .payload_len = 1,
        .qos         = ACTRUST_MQTT_QOS0,
    };
    actrust_err_t err = actrust_mqtt_publish(mqtt, &msg);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE, ACTRUST_ERR_CODE(err));
}

void test_publish_rejects_null_payload_with_nonzero_len(void)
{
    actrust_mqtt_message_t msg = {
        .topic       = "t",
        .topic_len   = 1,
        .payload     = NULL,
        .payload_len = 1,
        .qos         = ACTRUST_MQTT_QOS0,
    };
    actrust_err_t err = actrust_mqtt_publish(mqtt, &msg);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
}

void test_subscribe_null_handle(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_subscribe(NULL, "topic"));
}

void test_subscribe_null_topic(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_subscribe(mqtt, NULL));
}

void test_unsubscribe_null_handle(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_unsubscribe(NULL, "topic"));
}

void test_unsubscribe_null_topic(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_unsubscribe(mqtt, NULL));
}

void test_unsubscribe_rejects_oversized_topic(void)
{
    char topic[TEST_MQTT_TOPIC_MAX_LEN + 1u];
    memset(topic, 'a', sizeof(topic));
    topic[sizeof(topic) - 1u] = '\0';

    actrust_err_t err = actrust_mqtt_unsubscribe(mqtt, topic);
    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG, ACTRUST_ERR_CODE(err));
}

void test_process_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_process(NULL));
}

void test_stop_process_null(void)
{
    TEST_ASSERT_NOT_EQUAL(ACTRUST_OK, actrust_mqtt_stop_process(NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_null);
    RUN_TEST(test_init_success);
    RUN_TEST(test_deinit_null);
    RUN_TEST(test_deinit_stops_running_process);
    RUN_TEST(test_set_callbacks_null_handle);
    RUN_TEST(test_set_callbacks_success);
    RUN_TEST(test_connect_null_handle);
    RUN_TEST(test_disconnect_null_handle);
    RUN_TEST(test_publish_null_handle);
    RUN_TEST(test_publish_before_connect);
    RUN_TEST(test_publish_rejects_null_payload_with_nonzero_len);
    RUN_TEST(test_subscribe_null_handle);
    RUN_TEST(test_subscribe_null_topic);
    RUN_TEST(test_unsubscribe_null_handle);
    RUN_TEST(test_unsubscribe_null_topic);
    RUN_TEST(test_unsubscribe_rejects_oversized_topic);
    RUN_TEST(test_process_null);
    RUN_TEST(test_stop_process_null);
    return UNITY_END();
}
