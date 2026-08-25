// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/* C standard */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Third-party */
#ifndef MQTT_AGENT_DO_NOT_USE_CUSTOM_CONFIG
#define MQTT_AGENT_DO_NOT_USE_CUSTOM_CONFIG
#endif
#include "core_mqtt_agent.h"
#include "unity.h"

/* MQTT */
#include "mqtt/mqtt.h"

/* Adapter */
#include "adapter/network.h"
#include "adapter/system.h"

#define TEST_MQTT_TOPIC_MAX_LEN (128u)
#define TEST_CLIENT_ID          "mqtt-owned-client"
#define TEST_TCP_HOST           "mqtt.example.test"
#define TEST_TLS_HOST           "tls.example.test"

static actrust_mqtt_t mqtt;

#ifdef ACTRUST_TEST_WRAP_MQTT_OWNERSHIP
static char                      *s_caller_client_id;
static char                      *s_caller_host;
static const uint8_t             *s_caller_ca;
static const uint8_t             *s_caller_cert;
static const char                *s_seen_client_id;
static char                       s_seen_client_id_value[64];
static char                       s_seen_host[64];
static uint8_t                    s_seen_ca[64];
static size_t                     s_seen_ca_len;
static uint8_t                    s_seen_cert[64];
static size_t                     s_seen_cert_len;
static actrust_crypto_ctx_t       s_seen_crypto_ctx;
static actrust_crypto_key_t       s_seen_client_key;
static MQTTAgentCommandCallback_t s_connect_callback;
static MQTTAgentCommandContext_t *s_connect_callback_context;
static MQTTAgentCommandCallback_t s_subscribe_callback;
static MQTTAgentCommandContext_t *s_subscribe_callback_context;
static MQTTAgentCommandCallback_t s_unsubscribe_callback;
static MQTTAgentCommandContext_t *s_unsubscribe_callback_context;
static bool                       s_hold_command_loop;
static actrust_sem_t              s_command_loop_entered;
static actrust_sem_t              s_command_loop_release;
static bool                       s_hold_owned_allocation;
static actrust_sem_t              s_owned_allocation_entered;
static actrust_sem_t              s_owned_allocation_release;
static size_t                     s_alloc_call_count;
static size_t                     s_fail_alloc_call;
static size_t                     s_tracked_alloc_count;
static bool                       s_track_allocations;
static bool                       s_free_saw_nonzero;

typedef struct test_alloc_node {
    void                   *ptr;
    size_t                  len;
    struct test_alloc_node *next;
} test_alloc_node_t;

static test_alloc_node_t *s_allocations;

static void test_complete_connect(MQTTStatus_t status);
static void test_complete_subscribe(MQTTStatus_t status, uint8_t *suback);
static void test_complete_unsubscribe(MQTTStatus_t status);

extern void *__real_actrust_calloc(size_t nmemb, size_t size);
extern void  __real_actrust_free(void *ptr);

static test_alloc_node_t *test_find_allocation(void               *ptr,
                                               test_alloc_node_t **previous)
{
    test_alloc_node_t *prev = NULL;
    test_alloc_node_t *node = s_allocations;
    while (node != NULL && node->ptr != ptr) {
        prev = node;
        node = node->next;
    }
    if (previous != NULL) {
        *previous = prev;
    }
    return node;
}

void *__wrap_actrust_calloc(size_t nmemb, size_t size)
{
    if (s_hold_owned_allocation == true) {
        TEST_ASSERT_EQUAL(ACTRUST_OK,
                          actrust_sem_post(s_owned_allocation_entered));
        TEST_ASSERT_EQUAL(ACTRUST_OK,
                          actrust_sem_wait(s_owned_allocation_release, 1000u));
    }

    if (s_track_allocations == false) {
        return __real_actrust_calloc(nmemb, size);
    }

    s_alloc_call_count++;
    if (s_fail_alloc_call != 0u && s_alloc_call_count == s_fail_alloc_call) {
        return NULL;
    }

    void *ptr = __real_actrust_calloc(nmemb, size);
    if (ptr != NULL) {
        test_alloc_node_t *node = calloc(1u, sizeof(*node));
        TEST_ASSERT_NOT_NULL(node);
        node->ptr     = ptr;
        node->len     = nmemb * size;
        node->next    = s_allocations;
        s_allocations = node;
        s_tracked_alloc_count++;
    }
    return ptr;
}

void __wrap_actrust_free(void *ptr)
{
    if (ptr != NULL && s_track_allocations == true) {
        test_alloc_node_t *previous = NULL;
        test_alloc_node_t *node     = test_find_allocation(ptr, &previous);
        if (node != NULL) {
            const uint8_t *bytes = ptr;
            for (size_t i = 0u; i < node->len; ++i) {
                if (bytes[i] != 0u) {
                    s_free_saw_nonzero = true;
                    break;
                }
            }
            if (previous == NULL) {
                s_allocations = node->next;
            } else {
                previous->next = node->next;
            }
            free(node);
            s_tracked_alloc_count--;
        }
    }
    __real_actrust_free(ptr);
}

actrust_err_t __wrap_actrust_tls_connect(actrust_tls_t              *out_tls,
                                         const actrust_tls_config_t *cfg,
                                         uint32_t                    timeout_ms)
{
    (void) timeout_ms;
    TEST_ASSERT_NOT_NULL(out_tls);
    TEST_ASSERT_NOT_NULL(cfg);
    TEST_ASSERT_NOT_EQUAL(s_caller_host, cfg->host);
    TEST_ASSERT_NOT_EQUAL(s_caller_ca, cfg->ca);
    TEST_ASSERT_NOT_EQUAL(s_caller_cert, cfg->client_cert);
    TEST_ASSERT_EQUAL_STRING(TEST_TLS_HOST, cfg->host);
    TEST_ASSERT_TRUE(cfg->ca_len <= sizeof(s_seen_ca));
    TEST_ASSERT_TRUE(cfg->client_cert_len <= sizeof(s_seen_cert));
    memcpy(s_seen_host, cfg->host, strlen(cfg->host) + 1u);
    memcpy(s_seen_ca, cfg->ca, cfg->ca_len);
    memcpy(s_seen_cert, cfg->client_cert, cfg->client_cert_len);
    s_seen_ca_len     = cfg->ca_len;
    s_seen_cert_len   = cfg->client_cert_len;
    s_seen_crypto_ctx = cfg->crypto_ctx;
    s_seen_client_key = cfg->client_key;
    *out_tls          = (actrust_tls_t) (uintptr_t) 1u;
    return ACTRUST_OK;
}

actrust_err_t __wrap_actrust_tls_close(actrust_tls_t *tls, uint32_t timeout_ms)
{
    (void) timeout_ms;
    if (tls != NULL) {
        *tls = NULL;
    }
    return ACTRUST_OK;
}

actrust_err_t __wrap_actrust_net_open(actrust_net_t     *out,
                                      actrust_net_type_t type)
{
    TEST_ASSERT_EQUAL(ACTRUST_NET_TCP, type);
    *out = (actrust_net_t) (uintptr_t) 1u;
    return ACTRUST_OK;
}

actrust_err_t __wrap_actrust_net_dns_resolve(const char *host, char *ip,
                                             size_t ip_len, uint32_t timeout_ms)
{
    (void) timeout_ms;
    TEST_ASSERT_NOT_EQUAL(s_caller_host, host);
    TEST_ASSERT_EQUAL_STRING(TEST_TCP_HOST, host);
    TEST_ASSERT_TRUE(ip_len >= sizeof("127.0.0.1"));
    memcpy(s_seen_host, host, strlen(host) + 1u);
    memcpy(ip, "127.0.0.1", sizeof("127.0.0.1"));
    return ACTRUST_OK;
}

actrust_err_t __wrap_actrust_net_connect(actrust_net_t net,
                                         const char *remote_ip, uint16_t port,
                                         uint32_t timeout_ms)
{
    (void) net;
    (void) timeout_ms;
    TEST_ASSERT_EQUAL_STRING("127.0.0.1", remote_ip);
    TEST_ASSERT_EQUAL_UINT16(1883u, port);
    return ACTRUST_OK;
}

actrust_err_t __wrap_actrust_net_close(actrust_net_t net)
{
    (void) net;
    return ACTRUST_OK;
}

MQTTStatus_t __wrap_MQTTAgent_Connect(
    const MQTTAgentContext_t *agent, MQTTAgentConnectArgs_t *args,
    const MQTTAgentCommandInfo_t *command_info)
{
    (void) agent;
    TEST_ASSERT_NOT_NULL(args);
    TEST_ASSERT_NOT_NULL(args->pConnectInfo);
    TEST_ASSERT_NOT_NULL(command_info);
    s_seen_client_id = args->pConnectInfo->pClientIdentifier;
    TEST_ASSERT_NOT_EQUAL(s_caller_client_id, s_seen_client_id);
    TEST_ASSERT_TRUE(args->pConnectInfo->clientIdentifierLength <
                     sizeof(s_seen_client_id_value));
    memcpy(s_seen_client_id_value, s_seen_client_id,
           args->pConnectInfo->clientIdentifierLength);
    s_seen_client_id_value[args->pConnectInfo->clientIdentifierLength] = '\0';
    s_connect_callback         = command_info->cmdCompleteCallback;
    s_connect_callback_context = command_info->pCmdCompleteCallbackContext;
    test_complete_connect(MQTTSuccess);
    return MQTTSuccess;
}

MQTTStatus_t __wrap_MQTTAgent_Disconnect(
    const MQTTAgentContext_t *agent, const MQTTAgentCommandInfo_t *command_info)
{
    MQTTAgentReturnInfo_t return_info = {
        .returnCode = MQTTSuccess,
    };

    (void) agent;
    TEST_ASSERT_NOT_NULL(command_info);
    TEST_ASSERT_NOT_NULL(command_info->cmdCompleteCallback);
    command_info->cmdCompleteCallback(command_info->pCmdCompleteCallbackContext,
                                      &return_info);
    return MQTTSuccess;
}

MQTTStatus_t __wrap_MQTTAgent_Subscribe(
    const MQTTAgentContext_t *agent, MQTTAgentSubscribeArgs_t *args,
    const MQTTAgentCommandInfo_t *command_info)
{
    (void) agent;
    TEST_ASSERT_NOT_NULL(args);
    TEST_ASSERT_NOT_NULL(command_info);
    s_subscribe_callback         = command_info->cmdCompleteCallback;
    s_subscribe_callback_context = command_info->pCmdCompleteCallbackContext;
    return MQTTSuccess;
}

MQTTStatus_t __wrap_MQTTAgent_Unsubscribe(
    const MQTTAgentContext_t *agent, MQTTAgentSubscribeArgs_t *args,
    const MQTTAgentCommandInfo_t *command_info)
{
    (void) agent;
    TEST_ASSERT_NOT_NULL(args);
    TEST_ASSERT_NOT_NULL(command_info);
    s_unsubscribe_callback         = command_info->cmdCompleteCallback;
    s_unsubscribe_callback_context = command_info->pCmdCompleteCallbackContext;
    return MQTTSuccess;
}

MQTTStatus_t __wrap_MQTTAgent_CommandLoop(MQTTAgentContext_t *agent)
{
    (void) agent;
    if (s_hold_command_loop == true) {
        TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_post(s_command_loop_entered));
        TEST_ASSERT_EQUAL(ACTRUST_OK,
                          actrust_sem_wait(s_command_loop_release, 1000u));
    }
    return MQTTSuccess;
}

static void test_complete_connect(MQTTStatus_t status)
{
    MQTTAgentReturnInfo_t return_info = {
        .returnCode = status,
    };
    TEST_ASSERT_NOT_NULL(s_connect_callback);
    s_connect_callback(s_connect_callback_context, &return_info);
    s_connect_callback         = NULL;
    s_connect_callback_context = NULL;
}

static void test_complete_subscribe(MQTTStatus_t status, uint8_t *suback)
{
    MQTTAgentReturnInfo_t return_info = {
        .returnCode   = status,
        .pSubackCodes = suback,
    };
    TEST_ASSERT_NOT_NULL(s_subscribe_callback);
    MQTTAgentCommandCallback_t callback = s_subscribe_callback;
    MQTTAgentCommandContext_t *context  = s_subscribe_callback_context;
    s_subscribe_callback                = NULL;
    s_subscribe_callback_context        = NULL;
    callback(context, &return_info);
}

static void test_complete_unsubscribe(MQTTStatus_t status)
{
    MQTTAgentReturnInfo_t return_info = { .returnCode = status };
    TEST_ASSERT_NOT_NULL(s_unsubscribe_callback);
    MQTTAgentCommandCallback_t callback = s_unsubscribe_callback;
    MQTTAgentCommandContext_t *context  = s_unsubscribe_callback_context;
    s_unsubscribe_callback              = NULL;
    s_unsubscribe_callback_context      = NULL;
    callback(context, &return_info);
}

static void test_reset_wrapped_state(void)
{
    s_caller_client_id = NULL;
    s_caller_host      = NULL;
    s_caller_ca        = NULL;
    s_caller_cert      = NULL;
    s_seen_client_id   = NULL;
    memset(s_seen_client_id_value, 0, sizeof(s_seen_client_id_value));
    memset(s_seen_host, 0, sizeof(s_seen_host));
    memset(s_seen_ca, 0, sizeof(s_seen_ca));
    memset(s_seen_cert, 0, sizeof(s_seen_cert));
    s_seen_ca_len                  = 0u;
    s_seen_cert_len                = 0u;
    s_seen_crypto_ctx              = NULL;
    s_seen_client_key              = NULL;
    s_connect_callback             = NULL;
    s_connect_callback_context     = NULL;
    s_subscribe_callback           = NULL;
    s_subscribe_callback_context   = NULL;
    s_unsubscribe_callback         = NULL;
    s_unsubscribe_callback_context = NULL;
    s_hold_command_loop            = false;
    s_hold_owned_allocation        = false;
    s_owned_allocation_entered     = NULL;
    s_owned_allocation_release     = NULL;
    s_alloc_call_count             = 0u;
    s_fail_alloc_call              = 0u;
    s_track_allocations            = false;
    s_free_saw_nonzero             = false;
    TEST_ASSERT_EQUAL_size_t(0u, s_tracked_alloc_count);
    TEST_ASSERT_NULL(s_allocations);
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sem_create(&s_command_loop_entered, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sem_create(&s_command_loop_release, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sem_create(&s_owned_allocation_entered, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sem_create(&s_owned_allocation_release, 0u));
}
#endif

typedef struct {
    actrust_mqtt_t mqtt;
    actrust_sem_t  returned_sem;
} mqtt_unit_process_arg_t;

typedef struct {
    actrust_mqtt_t               mqtt;
    const actrust_mqtt_config_t *config;
    actrust_sem_t                returned_sem;
    actrust_err_t                result;
} mqtt_unit_connect_arg_t;

static void mqtt_unit_connect_task(void *arg)
{
    mqtt_unit_connect_arg_t *connect_arg = (mqtt_unit_connect_arg_t *) arg;

    connect_arg->result =
        actrust_mqtt_connect(connect_arg->mqtt, connect_arg->config);
    (void) actrust_sem_post(connect_arg->returned_sem);
}

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
#ifdef ACTRUST_TEST_WRAP_MQTT_OWNERSHIP
    test_reset_wrapped_state();
#endif
}

void tearDown(void)
{
    if (mqtt != NULL) {
        actrust_mqtt_deinit(mqtt);
        mqtt = NULL;
    }
#ifdef ACTRUST_TEST_WRAP_MQTT_OWNERSHIP
    s_track_allocations = false;
    if (s_command_loop_entered != NULL) {
        (void) actrust_sem_destroy(s_command_loop_entered);
        s_command_loop_entered = NULL;
    }
    if (s_command_loop_release != NULL) {
        (void) actrust_sem_destroy(s_command_loop_release);
        s_command_loop_release = NULL;
    }
    if (s_owned_allocation_entered != NULL) {
        (void) actrust_sem_destroy(s_owned_allocation_entered);
        s_owned_allocation_entered = NULL;
    }
    if (s_owned_allocation_release != NULL) {
        (void) actrust_sem_destroy(s_owned_allocation_release);
        s_owned_allocation_release = NULL;
    }
#endif
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

#ifdef ACTRUST_TEST_WRAP_MQTT_OWNERSHIP
static actrust_mqtt_config_t test_tcp_config(char *client_id, char *host)
{
    actrust_mqtt_config_t config     = { 0 };
    config.client_id                 = client_id;
    config.transport.type            = ACTRUST_MQTT_TRANSPORT_TCP;
    config.transport.config.tcp.host = host;
    config.transport.config.tcp.port = 1883u;
    return config;
}

static actrust_mqtt_config_t test_tls_config(char *client_id, char *host,
                                             uint8_t *ca, size_t ca_len,
                                             uint8_t *cert, size_t cert_len,
                                             actrust_crypto_ctx_t crypto_ctx,
                                             actrust_crypto_key_t client_key)
{
    actrust_mqtt_config_t config                = { 0 };
    config.client_id                            = client_id;
    config.transport.type                       = ACTRUST_MQTT_TRANSPORT_TLS;
    config.transport.config.tls.host            = host;
    config.transport.config.tls.port            = 8883u;
    config.transport.config.tls.crypto_ctx      = crypto_ctx;
    config.transport.config.tls.ca              = ca;
    config.transport.config.tls.ca_len          = ca_len;
    config.transport.config.tls.ca_format       = ACTRUST_TLS_CERT_FORMAT_DER;
    config.transport.config.tls.client_cert     = cert;
    config.transport.config.tls.client_cert_len = cert_len;
    config.transport.config.tls.client_cert_format =
        ACTRUST_TLS_CERT_FORMAT_DER;
    config.transport.config.tls.client_key = client_key;
    return config;
}

static void test_connect_tcp(void)
{
    static char           client_id[] = TEST_CLIENT_ID;
    static char           host[]      = TEST_TCP_HOST;
    actrust_mqtt_config_t config      = test_tcp_config(client_id, host);
    s_caller_client_id                = client_id;
    s_caller_host                     = host;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_connect(mqtt, &config));
}

void test_subscription_commits_on_ack(void)
{
    uint8_t suback = MQTTSubAckSuccessQos0;
    test_connect_tcp();
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_subscribe(mqtt, "topic/a"));
    TEST_ASSERT_EQUAL(
        ACTRUST_ERR_ALREADY,
        ACTRUST_ERR_CODE(actrust_mqtt_subscribe(mqtt, "topic/a")));
    test_complete_subscribe(MQTTSuccess, &suback);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_unsubscribe(mqtt, "topic/a"));
    test_complete_unsubscribe(MQTTSuccess);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_disconnect(mqtt));
}

void test_subscription_rolls_back_on_rejection(void)
{
    uint8_t suback = MQTTSubAckFailure;
    test_connect_tcp();
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_subscribe(mqtt, "topic/b"));
    test_complete_subscribe(MQTTSuccess, &suback);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_subscribe(mqtt, "topic/b"));
    test_complete_subscribe(MQTTSuccess, NULL);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_subscribe(mqtt, "topic/b"));
    suback = MQTTSubAckSuccessQos0;
    test_complete_subscribe(MQTTSuccess, &suback);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_unsubscribe(mqtt, "topic/b"));
    test_complete_unsubscribe(MQTTRecvFailed);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_unsubscribe(mqtt, "topic/b"));
    test_complete_unsubscribe(MQTTSuccess);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_disconnect(mqtt));
}

void test_connect_owns_tcp_strings(void)
{
    char                  client_id[] = TEST_CLIENT_ID;
    char                  host[]      = TEST_TCP_HOST;
    actrust_mqtt_config_t config      = test_tcp_config(client_id, host);
    s_caller_client_id                = client_id;
    s_caller_host                     = host;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_connect(mqtt, &config));
    TEST_ASSERT_EQUAL_STRING(TEST_CLIENT_ID, s_seen_client_id_value);
    TEST_ASSERT_EQUAL_STRING(TEST_TCP_HOST, s_seen_host);

    memset(client_id, 'x', sizeof(client_id) - 1u);
    memset(host, 'y', sizeof(host) - 1u);
    TEST_ASSERT_EQUAL_STRING(TEST_CLIENT_ID, s_seen_client_id_value);
    TEST_ASSERT_EQUAL_STRING(TEST_TCP_HOST, s_seen_host);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_disconnect(mqtt));
}

void test_connect_owns_tls_buffers_and_borrows_handles(void)
{
    char                  client_id[] = TEST_CLIENT_ID;
    char                  host[]      = TEST_TLS_HOST;
    uint8_t               ca[]        = { 1u, 2u, 3u, 4u };
    uint8_t               cert[]      = { 5u, 6u, 7u, 8u };
    actrust_crypto_ctx_t  crypto_ctx = (actrust_crypto_ctx_t) (uintptr_t) 0x11u;
    actrust_crypto_key_t  client_key = (actrust_crypto_key_t) (uintptr_t) 0x22u;
    actrust_mqtt_config_t config =
        test_tls_config(client_id, host, ca, sizeof(ca), cert, sizeof(cert),
                        crypto_ctx, client_key);
    s_caller_client_id = client_id;
    s_caller_host      = host;
    s_caller_ca        = ca;
    s_caller_cert      = cert;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_connect(mqtt, &config));
    TEST_ASSERT_EQUAL_MEMORY(ca, s_seen_ca, sizeof(ca));
    TEST_ASSERT_EQUAL_MEMORY(cert, s_seen_cert, sizeof(cert));
    TEST_ASSERT_EQUAL(crypto_ctx, s_seen_crypto_ctx);
    TEST_ASSERT_EQUAL(client_key, s_seen_client_key);

    memset(client_id, 'x', sizeof(client_id) - 1u);
    memset(host, 'y', sizeof(host) - 1u);
    memset(ca, 0xAA, sizeof(ca));
    memset(cert, 0xBB, sizeof(cert));
    TEST_ASSERT_EQUAL_STRING(TEST_CLIENT_ID, s_seen_client_id_value);
    TEST_ASSERT_EQUAL_STRING(TEST_TLS_HOST, s_seen_host);
    TEST_ASSERT_EQUAL_UINT8(1u, s_seen_ca[0]);
    TEST_ASSERT_EQUAL_UINT8(5u, s_seen_cert[0]);

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_disconnect(mqtt));
}

void test_connect_rejects_invalid_config(void)
{
    char                  client_id[] = TEST_CLIENT_ID;
    char                  host[]      = TEST_TCP_HOST;
    actrust_mqtt_config_t config      = test_tcp_config(client_id, host);
    config.transport.config.tcp.port  = 0u;
    s_track_allocations               = true;

    TEST_ASSERT_EQUAL(ACTRUST_ERR_INVALID_ARG,
                      ACTRUST_ERR_CODE(actrust_mqtt_connect(mqtt, &config)));
    TEST_ASSERT_EQUAL_size_t(0u, s_alloc_call_count);
    TEST_ASSERT_EQUAL_size_t(0u, s_tracked_alloc_count);
}

void test_connect_rejects_replacement_while_process_runs(void)
{
    char                    client_id[]  = TEST_CLIENT_ID;
    char                    host[]       = TEST_TCP_HOST;
    actrust_mqtt_config_t   config       = test_tcp_config(client_id, host);
    actrust_task_t          task         = NULL;
    actrust_sem_t           returned_sem = NULL;
    mqtt_unit_process_arg_t arg;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&returned_sem, 0u));
    arg.mqtt            = mqtt;
    arg.returned_sem    = returned_sem;
    s_hold_command_loop = true;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_create(&task, "mqtt_replace",
                                                      mqtt_unit_process_task,
                                                      (void *) &arg, 0u, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sem_wait(s_command_loop_entered, 1000u));

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_connect(mqtt, &config));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_disconnect(mqtt));
    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE,
                      ACTRUST_ERR_CODE(actrust_mqtt_connect(mqtt, &config)));

    s_hold_command_loop = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_post(s_command_loop_release));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(returned_sem, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(task, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(returned_sem));
}

void test_connect_reserves_lifecycle_against_deinit(void)
{
    char                    client_id[]  = TEST_CLIENT_ID;
    char                    host[]       = TEST_TCP_HOST;
    actrust_mqtt_config_t   config       = test_tcp_config(client_id, host);
    actrust_task_t          connect_task = NULL;
    actrust_sem_t           connect_done = NULL;
    mqtt_unit_connect_arg_t connect_arg;

    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_create(&connect_done, 0u));
    connect_arg.mqtt         = mqtt;
    connect_arg.config       = &config;
    connect_arg.returned_sem = connect_done;
    connect_arg.result       = ACTRUST_ERR_INVALID_ARG;
    s_hold_owned_allocation  = true;
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_task_create(&connect_task, "mqtt_connect",
                                          mqtt_unit_connect_task,
                                          (void *) &connect_arg, 0u, 0u));
    TEST_ASSERT_EQUAL(ACTRUST_OK,
                      actrust_sem_wait(s_owned_allocation_entered, 1000u));

    TEST_ASSERT_EQUAL(ACTRUST_ERR_BAD_STATE,
                      ACTRUST_ERR_CODE(actrust_mqtt_deinit(mqtt)));

    s_hold_owned_allocation = false;
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_post(s_owned_allocation_release));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_wait(connect_done, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_task_join(connect_task, 1000u));
    TEST_ASSERT_EQUAL(ACTRUST_OK, connect_arg.result);
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_mqtt_disconnect(mqtt));
    TEST_ASSERT_EQUAL(ACTRUST_OK, actrust_sem_destroy(connect_done));
}
void test_connect_rolls_back_owned_allocations(void)
{
    char                  client_id[] = TEST_CLIENT_ID;
    char                  host[]      = TEST_TLS_HOST;
    uint8_t               ca[]        = { 1u, 2u, 3u, 4u };
    uint8_t               cert[]      = { 5u, 6u, 7u, 8u };
    actrust_mqtt_config_t config =
        test_tls_config(client_id, host, ca, sizeof(ca), cert, sizeof(cert),
                        (actrust_crypto_ctx_t) (uintptr_t) 0x11u,
                        (actrust_crypto_key_t) (uintptr_t) 0x22u);
    s_caller_client_id  = client_id;
    s_caller_host       = host;
    s_caller_ca         = ca;
    s_caller_cert       = cert;
    s_track_allocations = true;

    for (size_t fail_call = 1u; fail_call <= 4u; ++fail_call) {
        s_alloc_call_count = 0u;
        s_fail_alloc_call  = fail_call;
        s_free_saw_nonzero = false;
        TEST_ASSERT_EQUAL(
            ACTRUST_ERR_NO_MEM,
            ACTRUST_ERR_CODE(actrust_mqtt_connect(mqtt, &config)));
        TEST_ASSERT_EQUAL_size_t(0u, s_tracked_alloc_count);
        TEST_ASSERT_NULL(s_allocations);
        TEST_ASSERT_FALSE(s_free_saw_nonzero);
    }
}
#endif

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
#ifdef ACTRUST_TEST_WRAP_MQTT_OWNERSHIP
    RUN_TEST(test_connect_owns_tcp_strings);
    RUN_TEST(test_connect_owns_tls_buffers_and_borrows_handles);
    RUN_TEST(test_subscription_commits_on_ack);
    RUN_TEST(test_subscription_rolls_back_on_rejection);
    RUN_TEST(test_connect_rejects_invalid_config);
    RUN_TEST(test_connect_rejects_replacement_while_process_runs);
    RUN_TEST(test_connect_reserves_lifecycle_against_deinit);
    RUN_TEST(test_connect_rolls_back_owned_allocations);
#endif
    return UNITY_END();
}
