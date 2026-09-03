// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file mqtt.c
 * @brief MQTT client component implementation based on coreMQTT-Agent.
 */

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
#include "backoff_algorithm.h"
#include "core_mqtt_agent.h"

/* Common */
#include "common/common.h"

/* Project */
#include "actrust_config.h"

/* MQTT */
#include "mqtt/mqtt.h"

/* Component */
#include "crypto/crypto.h"
#include "log/log.h"
#include "queue/queue.h"

/* Adapter */
#include "adapter/network.h"
#include "adapter/system.h"

#define MQTT_ERR(reason)                                                       \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_MQTT, (reason))

#define ACTRUST_MQTT_CMD_BLOCK_TIME_MS  (0u)
#define ACTRUST_MQTT_WAIT_FOR_RESULT_MS (100u)

#define ACTRUST_MQTT_QUEUE_CAPACITY           (16u)
#define ACTRUST_MQTT_TOPIC_MAX_LEN            (128u)
#define ACTRUST_MQTT_NET_BUF_SIZE             (16384u)
#define ACTRUST_MQTT_SUBSCRIPTION_MAX         (16u)
#define ACTRUST_MQTT_IO_TIMEOUT_MS            (500u)
#define ACTRUST_MQTT_PROCESS_STOP_TIMEOUT_MS  (30000u)
#define ACTRUST_MQTT_CONNECT_RESULT_MARGIN_MS (1000u)
#define ACTRUST_MQTT_CONNECT_WAIT_TIMEOUT_MS                                   \
    ((uint32_t) CONFIG_ACTRUST_MQTT_CONNECT_TIMEOUT_MS +                       \
     ACTRUST_MQTT_CONNECT_RESULT_MARGIN_MS)
#define ACTRUST_MQTT_CONNECT_RETRY_BASE_MS  (1000u)
#define ACTRUST_MQTT_CONNECT_RETRY_MAX_MS   (10000u)
#define ACTRUST_MQTT_CONNECT_RETRY_ATTEMPTS (10u)

/* coreMQTT declares an incomplete NetworkContext type; define the concrete type
 * here. */
struct NetworkContext {
    actrust_mqtt_transport_t type;
    union {
        actrust_tls_t tls;
        actrust_net_t tcp;
    } handle;
    uint32_t io_timeout_ms;
};

/** @brief MQTT client connection state. */
typedef enum {
    ACTRUST_MQTT_STATE_INITIALIZED = 0,
    ACTRUST_MQTT_STATE_CONNECTED,
    ACTRUST_MQTT_STATE_CONNECTING,
    ACTRUST_MQTT_STATE_DISCONNECTED,
    ACTRUST_MQTT_STATE_DISCONNECTING,
    ACTRUST_MQTT_STATE_WAITING_RECONNECT,
} actrust_mqtt_state_t;

typedef enum {
    MQTT_LIFECYCLE_OPEN = 0,
    MQTT_LIFECYCLE_CLOSING,
} mqtt_lifecycle_state_t;

typedef enum {
    MQTT_SUBSCRIPTION_EMPTY = 0,
    MQTT_SUBSCRIPTION_PENDING,
    MQTT_SUBSCRIPTION_ACTIVE,
    MQTT_UNSUBSCRIPTION_PENDING,
} mqtt_subscription_state_t;

/** @brief MQTT subscription topic. */
typedef struct {
    mqtt_subscription_state_t state;
    uint32_t                  token;
    char                      topic[ACTRUST_MQTT_TOPIC_MAX_LEN];
} mqtt_subscription_topic_t;

/** @brief Agent message context consumed by coreMQTT-Agent interface hooks. */
struct MQTTAgentMessageContext {
    actrust_queue_t command_queue;
    actrust_mqtt_t  mqtt;
};

typedef enum {
    MQTT_OPERATION_CONNECT = 0,
    MQTT_OPERATION_DISCONNECT,
    MQTT_OPERATION_PUBLISH,
    MQTT_OPERATION_SUBSCRIBE,
    MQTT_OPERATION_UNSUBSCRIBE,
} mqtt_operation_type_t;

typedef struct mqtt_operation_base {
    mqtt_operation_type_t type;
    actrust_mqtt_t        mqtt;
    uint32_t              generation;
    size_t                subscription_index;
    uint32_t              subscription_token;
} mqtt_operation_base_t;

typedef struct {
    mqtt_operation_base_t  base;
    MQTTAgentConnectArgs_t connect_args;
    MQTTConnectInfo_t      connect_info;
    size_t                 alloc_len;
    char                   client_id[];
} mqtt_connect_operation_t;

typedef struct {
    mqtt_operation_base_t base;
    MQTTPublishInfo_t     publish_info;
    char                  topic[ACTRUST_MQTT_TOPIC_MAX_LEN];
    uint8_t               payload[CONFIG_ACTRUST_MQTT_PAYLOAD_MAX_LEN];
} mqtt_publish_operation_t;

typedef struct {
    mqtt_operation_base_t    base;
    MQTTAgentSubscribeArgs_t subscribe_args;
    MQTTSubscribeInfo_t      subscribe_info;
    char                     topic[ACTRUST_MQTT_TOPIC_MAX_LEN];
} mqtt_subscribe_operation_t;

typedef struct {
    mqtt_operation_base_t base;
} mqtt_simple_operation_t;

/** @brief MQTT client context. */
struct actrust_mqtt_ctx {
    MQTTAgentContext_t          agent_ctx;
    MQTTAgentMessageContext_t   message_ctx;
    MQTTAgentMessageInterface_t message_interface;
    TransportInterface_t        transport;
    NetworkContext_t            net_ctx;
    MQTTFixedBuffer_t           network_buffer;

    actrust_mqtt_config_t     config;
    actrust_mqtt_callbacks_t  callbacks;
    actrust_mqtt_state_t      state;
    mqtt_lifecycle_state_t    lifecycle;
    uint32_t                  connect_generation;
    uint32_t                  next_subscription_token;
    uint32_t                  api_inflight;
    uint32_t                  callback_inflight;
    uint32_t                  operation_inflight;
    actrust_err_t             connect_result;
    actrust_err_t             disconnect_result;
    bool                      connect_result_valid;
    bool                      disconnect_result_valid;
    bool                      command_active;
    BackoffAlgorithmContext_t backoff_ctx;

    actrust_mutex_t            lock;
    actrust_queue_t            command_queue;
    mqtt_subscription_topic_t *subscriptions;
    actrust_crypto_ctx_t       crypto_ctx;
    actrust_sem_t              process_done_sem;
    actrust_sem_t              command_done_sem;
    actrust_sem_t              lifecycle_done_sem;
    bool                       process_running;
    bool                       process_stop_requested;
    bool                       agent_cancelled;
    bool                       deinit_inflight;
};

/* ========================================================================
 * Internal Function Declarations
 * ======================================================================== */

static actrust_err_t mqtt_enqueue_connect_command(actrust_mqtt_t mqtt,
                                                  bool           clean_session,
                                                  uint32_t connect_generation,
                                                  uint32_t block_time_ms);
static actrust_err_t mqtt_enqueue_disconnect_command(actrust_mqtt_t mqtt,
                                                     uint32_t       generation,
                                                     uint32_t block_time_ms);
static actrust_err_t mqtt_enqueue_publish_command(
    actrust_mqtt_t mqtt, const actrust_mqtt_message_t *message,
    uint32_t generation, uint32_t block_time_ms);
static actrust_err_t mqtt_enqueue_subscribe_command(
    actrust_mqtt_t mqtt, const char *topic, bool is_unsubscribe,
    uint32_t generation, size_t subscription_index, uint32_t subscription_token,
    uint32_t block_time_ms);
static actrust_err_t mqtt_enqueue_status_to_actrust_err(MQTTStatus_t status);
static actrust_err_t mqtt_status_to_actrust_err(MQTTStatus_t status);
static uint32_t      mqtt_next_connect_generation_locked(actrust_mqtt_t mqtt);

/* ========================================================================
 * Generic Helpers
 * ======================================================================== */

static size_t mqtt_owned_string_len(const char *value)
{
    return value == NULL ? 0u : strlen(value) + 1u;
}

static void mqtt_owned_config_release(actrust_mqtt_config_t *config)
{
    if (config == NULL) {
        return;
    }

    if (config->transport.type == ACTRUST_MQTT_TRANSPORT_TLS) {
        actrust_tls_config_t *tls = &config->transport.config.tls;
        actrust_secure_free((void *) tls->client_cert, tls->client_cert_len);
        actrust_secure_free((void *) tls->ca, tls->ca_len);
        actrust_secure_free((void *) tls->host,
                            mqtt_owned_string_len(tls->host));
    } else if (config->transport.type == ACTRUST_MQTT_TRANSPORT_TCP) {
        const char *host = config->transport.config.tcp.host;
        actrust_secure_free((void *) host, mqtt_owned_string_len(host));
    }
    actrust_secure_free((void *) config->client_id,
                        mqtt_owned_string_len(config->client_id));
    actrust_secure_zeroize(config, sizeof(*config));
}

static actrust_err_t mqtt_owned_string_copy(char **out, const char *input)
{
    size_t len;

    if (out == NULL || input == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    len = strlen(input);
    if (len == SIZE_MAX) {
        return MQTT_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }
    *out = (char *) ACTRUST_CALLOC(1u, len + 1u);
    if (*out == NULL) {
        return MQTT_ERR(ACTRUST_ERR_NO_MEM);
    }
    memcpy(*out, input, len);
    return ACTRUST_OK;
}

static actrust_err_t mqtt_owned_bytes_copy(uint8_t **out, const uint8_t *input,
                                           size_t len)
{
    if (out == NULL || (input == NULL && len > 0u)) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (len == 0u) {
        return ACTRUST_OK;
    }

    *out = (uint8_t *) ACTRUST_CALLOC(1u, len);
    if (*out == NULL) {
        return MQTT_ERR(ACTRUST_ERR_NO_MEM);
    }
    memcpy(*out, input, len);
    return ACTRUST_OK;
}

static actrust_err_t mqtt_config_validate(const actrust_mqtt_config_t *config)
{
    if (config == NULL || config->client_id == NULL ||
        config->client_id[0] == '\0' ||
        strlen(config->client_id) > UINT16_MAX) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (config->transport.type == ACTRUST_MQTT_TRANSPORT_TCP) {
        const actrust_mqtt_tcp_config_t *tcp = &config->transport.config.tcp;
        return (tcp->host != NULL && tcp->host[0] != '\0' && tcp->port != 0u)
                   ? ACTRUST_OK
                   : MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (config->transport.type != ACTRUST_MQTT_TRANSPORT_TLS) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    const actrust_tls_config_t *tls = &config->transport.config.tls;
    if (tls->host == NULL || tls->host[0] == '\0' || tls->port == 0u ||
        tls->crypto_ctx == NULL || (tls->ca == NULL && tls->ca_len > 0u) ||
        (tls->ca != NULL && tls->ca_len == 0u) ||
        (tls->client_cert == NULL && tls->client_cert_len > 0u) ||
        (tls->client_cert != NULL && tls->client_cert_len == 0u) ||
        ((tls->client_cert == NULL) != (tls->client_key == NULL)) ||
        (tls->ca == NULL && tls->insecure == false)) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (tls->ca != NULL && tls->ca_format != ACTRUST_TLS_CERT_FORMAT_PEM &&
        tls->ca_format != ACTRUST_TLS_CERT_FORMAT_DER) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (tls->client_cert != NULL &&
        tls->client_cert_format != ACTRUST_TLS_CERT_FORMAT_PEM &&
        tls->client_cert_format != ACTRUST_TLS_CERT_FORMAT_DER) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    return ACTRUST_OK;
}

static actrust_err_t mqtt_owned_config_clone(actrust_mqtt_config_t       *out,
                                             const actrust_mqtt_config_t *input)
{
    actrust_err_t err;

    if (out == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    err = mqtt_config_validate(input);
    if (err != ACTRUST_OK) {
        return err;
    }

    actrust_secure_zeroize(out, sizeof(*out));
    out->transport.type = input->transport.type;

    err = mqtt_owned_string_copy((char **) &out->client_id, input->client_id);
    if (err != ACTRUST_OK) {
        goto fail;
    }

    switch (input->transport.type) {
        case ACTRUST_MQTT_TRANSPORT_TCP: {
            const actrust_mqtt_tcp_config_t *src = &input->transport.config.tcp;
            actrust_mqtt_tcp_config_t       *dst = &out->transport.config.tcp;
            err = mqtt_owned_string_copy((char **) &dst->host, src->host);
            if (err != ACTRUST_OK) {
                goto fail;
            }
            dst->port = src->port;
            break;
        }
        case ACTRUST_MQTT_TRANSPORT_TLS: {
            const actrust_tls_config_t *src = &input->transport.config.tls;
            actrust_tls_config_t       *dst = &out->transport.config.tls;
            err = mqtt_owned_string_copy((char **) &dst->host, src->host);
            if (err != ACTRUST_OK) {
                goto fail;
            }
            dst->ca_len          = src->ca_len;
            dst->client_cert_len = src->client_cert_len;
            err = mqtt_owned_bytes_copy((uint8_t **) &dst->ca, src->ca,
                                        src->ca_len);
            if (err != ACTRUST_OK) {
                goto fail;
            }
            err = mqtt_owned_bytes_copy((uint8_t **) &dst->client_cert,
                                        src->client_cert, src->client_cert_len);
            if (err != ACTRUST_OK) {
                goto fail;
            }
            dst->port               = src->port;
            dst->ca_len             = src->ca_len;
            dst->ca_format          = src->ca_format;
            dst->insecure           = src->insecure;
            dst->client_cert_len    = src->client_cert_len;
            dst->client_cert_format = src->client_cert_format;
            dst->client_key         = src->client_key;
            dst->crypto_ctx         = src->crypto_ctx;
            break;
        }
        default:
            err = MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
            goto fail;
    }

    return ACTRUST_OK;

fail:
    mqtt_owned_config_release(out);
    return err;
}

static void mqtt_lock(actrust_mqtt_t mqtt)
{
    if (mqtt != NULL && mqtt->lock != NULL) {
        (void) actrust_mutex_lock(mqtt->lock);
    }
}

static void mqtt_unlock(actrust_mqtt_t mqtt)
{
    if (mqtt != NULL && mqtt->lock != NULL) {
        (void) actrust_mutex_unlock(mqtt->lock);
    }
}

static void mqtt_set_state(actrust_mqtt_t mqtt, actrust_mqtt_state_t state)
{
    if (mqtt == NULL) {
        return;
    }

    mqtt_lock(mqtt);
    mqtt->state = state;
    mqtt_unlock(mqtt);
}

static bool mqtt_lifecycle_idle_locked(actrust_mqtt_t mqtt)
{
    return mqtt->api_inflight == 0u && mqtt->callback_inflight == 0u &&
           mqtt->operation_inflight == 0u && mqtt->command_active == false &&
           mqtt->process_running == false;
}

static void mqtt_signal_lifecycle_idle(actrust_mqtt_t mqtt)
{
    if (mqtt->lifecycle_done_sem != NULL) {
        (void) actrust_sem_post(mqtt->lifecycle_done_sem);
    }
}

static actrust_err_t mqtt_api_enter(actrust_mqtt_t mqtt)
{
    mqtt_lock(mqtt);
    if (mqtt->lifecycle != MQTT_LIFECYCLE_OPEN) {
        mqtt_unlock(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }
    mqtt->api_inflight++;
    mqtt_unlock(mqtt);
    return ACTRUST_OK;
}

static void mqtt_api_leave(actrust_mqtt_t mqtt)
{
    bool idle;
    mqtt_lock(mqtt);
    if (mqtt->api_inflight > 0u) {
        mqtt->api_inflight--;
    }
    idle = mqtt->lifecycle == MQTT_LIFECYCLE_CLOSING &&
           mqtt_lifecycle_idle_locked(mqtt);
    mqtt_unlock(mqtt);
    if (idle) {
        mqtt_signal_lifecycle_idle(mqtt);
    }
}

static void mqtt_operation_enter(actrust_mqtt_t mqtt)
{
    mqtt_lock(mqtt);
    mqtt->operation_inflight++;
    mqtt_unlock(mqtt);
}

static void mqtt_operation_leave(actrust_mqtt_t mqtt)
{
    bool idle;
    mqtt_lock(mqtt);
    if (mqtt->operation_inflight > 0u) {
        mqtt->operation_inflight--;
    }
    idle = mqtt->lifecycle == MQTT_LIFECYCLE_CLOSING &&
           mqtt_lifecycle_idle_locked(mqtt);
    mqtt_unlock(mqtt);
    if (idle) {
        mqtt_signal_lifecycle_idle(mqtt);
    }
}

static actrust_err_t mqtt_wait_for_lifecycle_idle(actrust_mqtt_t mqtt,
                                                  uint32_t       timeout_ms)
{
    uint64_t start_ms = actrust_monotonic_ms();

    for (;;) {
        mqtt_lock(mqtt);
        bool idle = mqtt_lifecycle_idle_locked(mqtt);
        mqtt_unlock(mqtt);
        if (idle) {
            return ACTRUST_OK;
        }
        if ((actrust_monotonic_ms() - start_ms) >= (uint64_t) timeout_ms) {
            return MQTT_ERR(ACTRUST_ERR_TIMEOUT);
        }
        (void) actrust_sem_wait(mqtt->lifecycle_done_sem,
                                ACTRUST_MQTT_WAIT_FOR_RESULT_MS);
    }
}

static void mqtt_get_state(actrust_mqtt_t mqtt, actrust_mqtt_state_t *out_state)
{
    mqtt_lock(mqtt);
    *out_state = mqtt->state;
    mqtt_unlock(mqtt);
}

static void mqtt_release_command_claim(actrust_mqtt_t mqtt)
{
    mqtt_lock(mqtt);
    bool active          = mqtt->command_active;
    mqtt->command_active = false;
    mqtt_unlock(mqtt);
    if (active && mqtt->command_done_sem != NULL) {
        (void) actrust_sem_post(mqtt->command_done_sem);
    }
}

static actrust_err_t mqtt_wait_for_command_claim(actrust_mqtt_t mqtt,
                                                 uint32_t       timeout_ms)
{
    uint64_t start_ms = actrust_monotonic_ms();

    for (;;) {
        mqtt_lock(mqtt);
        bool active = mqtt->command_active;
        mqtt_unlock(mqtt);
        if (active == false) {
            return ACTRUST_OK;
        }
        if ((actrust_monotonic_ms() - start_ms) >= (uint64_t) timeout_ms) {
            return MQTT_ERR(ACTRUST_ERR_TIMEOUT);
        }
        (void) actrust_sem_wait(mqtt->command_done_sem,
                                ACTRUST_MQTT_WAIT_FOR_RESULT_MS);
    }
}

static uint32_t mqtt_next_subscription_token_locked(actrust_mqtt_t mqtt)
{
    mqtt->next_subscription_token++;
    if (mqtt->next_subscription_token == 0u) {
        mqtt->next_subscription_token = 1u;
    }
    return mqtt->next_subscription_token;
}

static actrust_err_t mqtt_subscription_reserve_locked(actrust_mqtt_t mqtt,
                                                      const char    *topic,
                                                      bool      unsubscribe,
                                                      size_t   *index,
                                                      uint32_t *token)
{
    size_t found      = ACTRUST_MQTT_SUBSCRIPTION_MAX;
    size_t free_index = ACTRUST_MQTT_SUBSCRIPTION_MAX;

    for (size_t i = 0u; i < ACTRUST_MQTT_SUBSCRIPTION_MAX; ++i) {
        mqtt_subscription_topic_t *slot = &mqtt->subscriptions[i];
        if (slot->state != MQTT_SUBSCRIPTION_EMPTY &&
            strcmp(slot->topic, topic) == 0) {
            found = i;
            break;
        }
        if (free_index == ACTRUST_MQTT_SUBSCRIPTION_MAX &&
            slot->state == MQTT_SUBSCRIPTION_EMPTY) {
            free_index = i;
        }
    }

    if (unsubscribe) {
        if (found == ACTRUST_MQTT_SUBSCRIPTION_MAX ||
            mqtt->subscriptions[found].state != MQTT_SUBSCRIPTION_ACTIVE) {
            return MQTT_ERR(ACTRUST_ERR_MQTT_UNSUBSCRIBE_FAILED);
        }
        *index                           = found;
        mqtt->subscriptions[found].state = MQTT_UNSUBSCRIPTION_PENDING;
    } else {
        if (found != ACTRUST_MQTT_SUBSCRIPTION_MAX) {
            return MQTT_ERR(ACTRUST_ERR_ALREADY);
        }
        if (free_index == ACTRUST_MQTT_SUBSCRIPTION_MAX) {
            return MQTT_ERR(ACTRUST_ERR_NO_MEM);
        }
        *index                          = free_index;
        mqtt_subscription_topic_t *slot = &mqtt->subscriptions[free_index];
        slot->state                     = MQTT_SUBSCRIPTION_PENDING;
        memcpy(slot->topic, topic, strlen(topic) + 1u);
    }

    *token = mqtt_next_subscription_token_locked(mqtt);
    mqtt->subscriptions[*index].token = *token;
    return ACTRUST_OK;
}

static void mqtt_subscription_clear_locked(actrust_mqtt_t mqtt, size_t index,
                                           uint32_t token)
{
    if (index >= ACTRUST_MQTT_SUBSCRIPTION_MAX ||
        mqtt->subscriptions[index].token != token) {
        return;
    }
    mqtt->subscriptions[index].state = MQTT_SUBSCRIPTION_EMPTY;
    mqtt->subscriptions[index].token = 0u;
    memset(mqtt->subscriptions[index].topic, 0,
           sizeof(mqtt->subscriptions[index].topic));
}
static uint32_t mqtt_next_connect_generation_locked(actrust_mqtt_t mqtt)
{
    mqtt->connect_generation++;
    if (mqtt->connect_generation == 0u) {
        mqtt->connect_generation = 1u;
    }

    return mqtt->connect_generation;
}

static actrust_err_t mqtt_begin_connect(actrust_mqtt_t        mqtt,
                                        actrust_mqtt_state_t *previous_state,
                                        uint32_t             *generation)
{
    if (mqtt == NULL || previous_state == NULL || generation == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    mqtt_lock(mqtt);
    if (mqtt->state != ACTRUST_MQTT_STATE_INITIALIZED &&
        (mqtt->state != ACTRUST_MQTT_STATE_DISCONNECTED ||
         mqtt->process_running == true)) {
        mqtt_unlock(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }

    *previous_state            = mqtt->state;
    *generation                = mqtt_next_connect_generation_locked(mqtt);
    mqtt->state                = ACTRUST_MQTT_STATE_CONNECTING;
    mqtt->connect_result_valid = false;
    mqtt_unlock(mqtt);
    return ACTRUST_OK;
}

static void mqtt_release_connect_reservation(actrust_mqtt_t       mqtt,
                                             uint32_t             generation,
                                             actrust_mqtt_state_t state)
{
    mqtt_lock(mqtt);
    if (mqtt->connect_generation == generation &&
        mqtt->state == ACTRUST_MQTT_STATE_CONNECTING) {
        mqtt->state = state;
    }
    mqtt_unlock(mqtt);
}

static actrust_err_t mqtt_install_config(actrust_mqtt_t         mqtt,
                                         actrust_mqtt_config_t *candidate,
                                         actrust_mqtt_config_t *replaced,
                                         uint32_t               generation)
{
    if (mqtt == NULL || candidate == NULL || replaced == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    mqtt_lock(mqtt);
    if (mqtt->connect_generation != generation ||
        mqtt->state != ACTRUST_MQTT_STATE_CONNECTING) {
        mqtt_unlock(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }

    *replaced    = mqtt->config;
    mqtt->config = *candidate;
    actrust_secure_zeroize(candidate, sizeof(*candidate));
    mqtt_unlock(mqtt);
    return ACTRUST_OK;
}

static bool mqtt_complete_connect_attempt(actrust_mqtt_t mqtt,
                                          uint32_t       generation,
                                          MQTTStatus_t   status)
{
    bool accepted = false;

    mqtt_lock(mqtt);
    if (mqtt->connect_generation == generation &&
        mqtt->state == ACTRUST_MQTT_STATE_CONNECTING) {
        mqtt->connect_result       = mqtt_status_to_actrust_err(status);
        mqtt->connect_result_valid = true;
        mqtt->state = status == MQTTSuccess ? ACTRUST_MQTT_STATE_CONNECTED
                                            : ACTRUST_MQTT_STATE_DISCONNECTED;
        accepted    = true;
    }
    mqtt_unlock(mqtt);

    return accepted;
}

static void mqtt_cancel_connect_attempt(actrust_mqtt_t mqtt,
                                        uint32_t       generation)
{
    mqtt_lock(mqtt);
    if (mqtt->connect_generation == generation &&
        mqtt->connect_result_valid == false) {
        (void) mqtt_next_connect_generation_locked(mqtt);
        mqtt->connect_result_valid = false;
        mqtt->state                = ACTRUST_MQTT_STATE_DISCONNECTED;
    }
    mqtt_unlock(mqtt);
}

static void mqtt_abort_disconnect(actrust_mqtt_t mqtt, uint32_t generation)
{
    mqtt_lock(mqtt);
    if (mqtt->connect_generation == generation &&
        mqtt->state == ACTRUST_MQTT_STATE_DISCONNECTING &&
        mqtt->disconnect_result_valid == false) {
        (void) mqtt_next_connect_generation_locked(mqtt);
        mqtt->disconnect_result_valid = false;
        mqtt->state                   = ACTRUST_MQTT_STATE_DISCONNECTED;
    }
    mqtt_unlock(mqtt);
}

static void mqtt_drain_process_done(actrust_mqtt_t mqtt)
{
    if (mqtt == NULL || mqtt->process_done_sem == NULL) {
        return;
    }

    while (actrust_sem_wait(mqtt->process_done_sem, 0u) == ACTRUST_OK) {
    }
}

static bool mqtt_is_process_running(actrust_mqtt_t mqtt)
{
    bool running = false;

    mqtt_lock(mqtt);
    running = mqtt->process_running;
    mqtt_unlock(mqtt);

    return running;
}

static bool mqtt_is_process_stop_requested(actrust_mqtt_t mqtt)
{
    bool stop_requested = false;

    mqtt_lock(mqtt);
    stop_requested = mqtt->process_stop_requested;
    mqtt_unlock(mqtt);

    return stop_requested;
}

static actrust_err_t mqtt_mark_process_started(actrust_mqtt_t mqtt)
{
    mqtt_drain_process_done(mqtt);

    mqtt_lock(mqtt);
    if (mqtt->lifecycle != MQTT_LIFECYCLE_OPEN) {
        mqtt_unlock(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }
    if (mqtt->process_running == true) {
        mqtt_unlock(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BUSY);
    }

    mqtt->process_running = true;
    mqtt_unlock(mqtt);

    return ACTRUST_OK;
}

static void mqtt_mark_process_stopped(actrust_mqtt_t mqtt)
{
    mqtt_lock(mqtt);
    mqtt->process_running        = false;
    mqtt->process_stop_requested = false;
    mqtt_unlock(mqtt);

    if (mqtt->process_done_sem != NULL) {
        (void) actrust_sem_post(mqtt->process_done_sem);
    }
}

static actrust_err_t mqtt_signal_process_stop_internal(actrust_mqtt_t mqtt)
{
    bool running = false;

    mqtt_lock(mqtt);
    mqtt->process_stop_requested = true;
    running                      = mqtt->process_running;
    mqtt_unlock(mqtt);

    if (running == false) {
        return ACTRUST_OK;
    }

    MQTTAgentCommandInfo_t cmd_info = {
        .cmdCompleteCallback         = NULL,
        .pCmdCompleteCallbackContext = NULL,
        .blockTimeMs                 = ACTRUST_MQTT_CMD_BLOCK_TIME_MS,
    };

    MQTTStatus_t status = MQTTAgent_Terminate(&mqtt->agent_ctx, &cmd_info);
    return mqtt_enqueue_status_to_actrust_err(status);
}

static actrust_err_t mqtt_signal_process_stop(actrust_mqtt_t mqtt)
{
    mqtt_lock(mqtt);
    if (mqtt->lifecycle != MQTT_LIFECYCLE_OPEN) {
        mqtt_unlock(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }
    mqtt_unlock(mqtt);
    return mqtt_signal_process_stop_internal(mqtt);
}

static actrust_err_t mqtt_wait_for_process_stop(actrust_mqtt_t mqtt,
                                                uint32_t       timeout_ms)
{
    if (mqtt_is_process_running(mqtt) == false) {
        return ACTRUST_OK;
    }

    actrust_err_t err = actrust_sem_wait(mqtt->process_done_sem, timeout_ms);
    if (err != ACTRUST_OK) {
        return MQTT_ERR(ACTRUST_ERR_TIMEOUT);
    }

    if (mqtt_is_process_running(mqtt) == true) {
        return MQTT_ERR(ACTRUST_ERR_BUSY);
    }

    return ACTRUST_OK;
}

static bool mqtt_sleep_stopped(actrust_mqtt_t mqtt, uint32_t timeout_ms)
{
    uint32_t remaining_ms = timeout_ms;

    while (remaining_ms > 0u) {
        uint32_t sleep_ms = remaining_ms;

        if (mqtt_is_process_stop_requested(mqtt) == true) {
            return true;
        }

        if (sleep_ms > ACTRUST_MQTT_WAIT_FOR_RESULT_MS) {
            sleep_ms = ACTRUST_MQTT_WAIT_FOR_RESULT_MS;
        }

        actrust_sleep_ms(sleep_ms);
        remaining_ms -= sleep_ms;
    }

    return mqtt_is_process_stop_requested(mqtt);
}

static actrust_err_t mqtt_wait_for_connect_result(actrust_mqtt_t mqtt,
                                                  uint32_t       generation,
                                                  uint32_t       timeout_ms)
{
    uint64_t start_ms = actrust_monotonic_ms();

    for (;;) {
        mqtt_lock(mqtt);
        if (mqtt->connect_result_valid) {
            actrust_err_t result = mqtt->connect_result;
            mqtt_unlock(mqtt);
            return result;
        }
        if (mqtt->connect_generation != generation) {
            mqtt_unlock(mqtt);
            return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
        }
        mqtt_unlock(mqtt);

        if ((actrust_monotonic_ms() - start_ms) >= (uint64_t) timeout_ms) {
            return MQTT_ERR(ACTRUST_ERR_TIMEOUT);
        }
        actrust_sleep_ms(ACTRUST_MQTT_WAIT_FOR_RESULT_MS);
    }
}

static actrust_err_t mqtt_wait_for_disconnect_result(actrust_mqtt_t mqtt,
                                                     uint32_t       generation,
                                                     uint32_t       timeout_ms)
{
    uint64_t start_ms = actrust_monotonic_ms();

    for (;;) {
        mqtt_lock(mqtt);
        if (mqtt->disconnect_result_valid) {
            actrust_err_t result = mqtt->disconnect_result;
            mqtt_unlock(mqtt);
            return result;
        }
        if (mqtt->connect_generation != generation) {
            mqtt_unlock(mqtt);
            return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
        }
        mqtt_unlock(mqtt);

        if ((actrust_monotonic_ms() - start_ms) >= (uint64_t) timeout_ms) {
            return MQTT_ERR(ACTRUST_ERR_TIMEOUT);
        }
        actrust_sleep_ms(ACTRUST_MQTT_WAIT_FOR_RESULT_MS);
    }
}

static MQTTQoS_t actrust_mqtt_qos_to_coremqtt(actrust_mqtt_qos_t qos)
{
    return (qos == ACTRUST_MQTT_QOS1) ? MQTTQoS1 : MQTTQoS0;
}

static actrust_mqtt_qos_t coremqtt_qos_to_actrust_mqtt(MQTTQoS_t qos)
{
    return (qos == MQTTQoS1) ? ACTRUST_MQTT_QOS1 : ACTRUST_MQTT_QOS0;
}

static actrust_err_t mqtt_status_to_actrust_err(MQTTStatus_t status)
{
    switch (status) {
        case MQTTSuccess:
            return ACTRUST_OK;
        case MQTTBadParameter:
            return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
        case MQTTNoMemory:
            return MQTT_ERR(ACTRUST_ERR_NO_MEM);
        case MQTTSendFailed:
        case MQTTRecvFailed:
            return MQTT_ERR(ACTRUST_ERR_IO);
        case MQTTBadResponse:
        case MQTTServerRefused:
        case MQTTIllegalState:
        case MQTTStateCollision:
            return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
        case MQTTKeepAliveTimeout:
        case MQTTNoDataAvailable:
            return MQTT_ERR(ACTRUST_ERR_TIMEOUT);
        case MQTTNeedMoreBytes:
            return ACTRUST_OK;
        default:
            return MQTT_ERR(ACTRUST_ERR_IO);
    }
}

static actrust_err_t mqtt_enqueue_status_to_actrust_err(MQTTStatus_t status)
{
    switch (status) {
        case MQTTSuccess:
            return ACTRUST_OK;
        case MQTTNoMemory:
            return MQTT_ERR(ACTRUST_ERR_NO_MEM);
        case MQTTSendFailed:
            return MQTT_ERR(ACTRUST_ERR_QUEUE_FULL);
        case MQTTBadParameter:
            return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
        default:
            return mqtt_status_to_actrust_err(status);
    }
}

static uint32_t mqtt_get_time_ms(void)
{
    return (uint32_t) (actrust_monotonic_ms() & 0xFFFFFFFFu);
}

static uint32_t mqtt_random_u32(actrust_mqtt_t mqtt)
{
    uint32_t random = 0u;

    if (mqtt == NULL) {
        return 1u;
    }

    if (actrust_crypto_random(mqtt->crypto_ctx, (uint8_t *) &random,
                              sizeof(random)) != ACTRUST_OK) {
        return 1u;
    }

    return random;
}

/* ========================================================================
 * Transport Interface Functions
 * ======================================================================== */

static int32_t mqtt_transport_send(NetworkContext_t *pNetworkContext,
                                   const void *pBuffer, size_t bytesToSend)
{
    if (pNetworkContext == NULL || pBuffer == NULL || bytesToSend == 0u) {
        return -1;
    }

    size_t        sent = 0u;
    actrust_err_t err  = ACTRUST_OK;

    switch (pNetworkContext->type) {
        case ACTRUST_MQTT_TRANSPORT_TLS:
            err = actrust_tls_write(pNetworkContext->handle.tls,
                                    (const uint8_t *) pBuffer, bytesToSend,
                                    pNetworkContext->io_timeout_ms, &sent);
            break;
        case ACTRUST_MQTT_TRANSPORT_TCP:
            err = actrust_net_send(pNetworkContext->handle.tcp,
                                   (const uint8_t *) pBuffer, bytesToSend,
                                   pNetworkContext->io_timeout_ms, &sent);
            break;
        default:
            return -1;
    }

    switch (ACTRUST_ERR_CODE(err)) {
        case ACTRUST_OK:
            return (int32_t) sent;
        case ACTRUST_ERR_TIMEOUT:
        case ACTRUST_ERR_WOULD_BLOCK:
        case ACTRUST_ERR_BUSY:
            return 0;
        default:
            return -1;
    }
}

static int32_t mqtt_transport_recv(NetworkContext_t *pNetworkContext,
                                   void *pBuffer, size_t bytesToRecv)
{
    if (pNetworkContext == NULL || pBuffer == NULL || bytesToRecv == 0u) {
        return -1;
    }

    size_t        recvd = 0u;
    actrust_err_t err   = ACTRUST_OK;

    switch (pNetworkContext->type) {
        case ACTRUST_MQTT_TRANSPORT_TLS:
            err = actrust_tls_read(pNetworkContext->handle.tls,
                                   (uint8_t *) pBuffer, bytesToRecv,
                                   pNetworkContext->io_timeout_ms, &recvd);
            break;
        case ACTRUST_MQTT_TRANSPORT_TCP:
            err = actrust_net_recv(pNetworkContext->handle.tcp,
                                   (uint8_t *) pBuffer, bytesToRecv,
                                   pNetworkContext->io_timeout_ms, &recvd);
            break;
        default:
            return -1;
    }

    switch (ACTRUST_ERR_CODE(err)) {
        case ACTRUST_OK:
            return recvd == 0u ? -1 : (int32_t) recvd;
        case ACTRUST_ERR_TIMEOUT:
        case ACTRUST_ERR_WOULD_BLOCK:
        case ACTRUST_ERR_BUSY:
            return 0;
        default:
            return -1;
    }
}

static void mqtt_transport_close(actrust_mqtt_t mqtt)
{
    if (mqtt == NULL) {
        return;
    }

    mqtt_lock(mqtt);
    switch (mqtt->net_ctx.type) {
        case ACTRUST_MQTT_TRANSPORT_TLS:
            if (mqtt->net_ctx.handle.tls != NULL) {
                (void) actrust_tls_close(&mqtt->net_ctx.handle.tls,
                                         ACTRUST_MQTT_IO_TIMEOUT_MS);
                mqtt->net_ctx.handle.tls = NULL;
            }
            break;
        case ACTRUST_MQTT_TRANSPORT_TCP:
            if (mqtt->net_ctx.handle.tcp != NULL) {
                (void) actrust_net_close(mqtt->net_ctx.handle.tcp);
                mqtt->net_ctx.handle.tcp = NULL;
            }
            break;
        default:
            break;
    }
    mqtt_unlock(mqtt);
}

static actrust_err_t mqtt_tcp_open(actrust_mqtt_t mqtt)
{
    const actrust_mqtt_tcp_config_t *tcp_cfg =
        &mqtt->config.transport.config.tcp;
    if (tcp_cfg->host == NULL || tcp_cfg->port == 0u) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err =
        actrust_net_open(&mqtt->net_ctx.handle.tcp, ACTRUST_NET_TCP);
    if (err != ACTRUST_OK) {
        return err;
    }

    char remote_ip[16] = { 0 };
    err = actrust_net_dns_resolve(tcp_cfg->host, remote_ip, sizeof(remote_ip),
                                  CONFIG_ACTRUST_MQTT_CONNECT_TIMEOUT_MS);
    if (err != ACTRUST_OK) {
        goto fail;
    }

    err =
        actrust_net_connect(mqtt->net_ctx.handle.tcp, remote_ip, tcp_cfg->port,
                            CONFIG_ACTRUST_MQTT_CONNECT_TIMEOUT_MS);
    if (err != ACTRUST_OK) {
        goto fail;
    }

    return ACTRUST_OK;

fail:
    (void) actrust_net_close(mqtt->net_ctx.handle.tcp);
    mqtt->net_ctx.handle.tcp = NULL;
    return err;
}

static actrust_err_t mqtt_transport_open(actrust_mqtt_t mqtt)
{
    if (mqtt == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    mqtt_transport_close(mqtt);
    mqtt->net_ctx.type          = mqtt->config.transport.type;
    mqtt->net_ctx.io_timeout_ms = ACTRUST_MQTT_IO_TIMEOUT_MS;

    switch (mqtt->net_ctx.type) {
        case ACTRUST_MQTT_TRANSPORT_TLS:
            return actrust_tls_connect(&mqtt->net_ctx.handle.tls,
                                       &mqtt->config.transport.config.tls,
                                       CONFIG_ACTRUST_MQTT_CONNECT_TIMEOUT_MS);
        case ACTRUST_MQTT_TRANSPORT_TCP:
            return mqtt_tcp_open(mqtt);
        default:
            return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }
}

/* ========================================================================
 * Subscription Helpers
 * ======================================================================== */

static void mqtt_subscription_rollback_locked(actrust_mqtt_t mqtt, size_t index,
                                              uint32_t                  token,
                                              mqtt_subscription_state_t state)
{
    if (index >= ACTRUST_MQTT_SUBSCRIPTION_MAX ||
        mqtt->subscriptions[index].token != token) {
        return;
    }
    if (state == MQTT_SUBSCRIPTION_EMPTY) {
        mqtt_subscription_clear_locked(mqtt, index, token);
    } else {
        mqtt->subscriptions[index].state = state;
    }
}

/* ========================================================================
 * Agent Message Interface Functions
 * ======================================================================== */

static bool mqtt_agent_send(MQTTAgentMessageContext_t *pMsgCtx,
                            MQTTAgentCommand_t *const *pCommandToSend,
                            uint32_t                   blockTimeMs)
{
    if (pMsgCtx == NULL || pMsgCtx->command_queue == NULL ||
        pCommandToSend == NULL) {
        return false;
    }

    if (actrust_queue_push(pMsgCtx->command_queue, pCommandToSend,
                           blockTimeMs) == ACTRUST_OK) {
        return true; // Command sent to queue successfully.
    }

    return false;
}

static bool mqtt_agent_release_command(MQTTAgentCommand_t *pCommandToRelease);

static bool mqtt_agent_recv(MQTTAgentMessageContext_t *pMsgCtx,
                            MQTTAgentCommand_t       **pReceivedCommand,
                            uint32_t                   blockTimeMs)
{
    if (pMsgCtx == NULL || pMsgCtx->command_queue == NULL ||
        pMsgCtx->mqtt == NULL || pReceivedCommand == NULL) {
        return false;
    }

    mqtt_release_command_claim(pMsgCtx->mqtt);

    for (;;) {
        if (actrust_queue_pop(pMsgCtx->command_queue, pReceivedCommand,
                              blockTimeMs) != ACTRUST_OK) {
            mqtt_lock(pMsgCtx->mqtt);
            pMsgCtx->mqtt->command_active = true;
            mqtt_unlock(pMsgCtx->mqtt);
            return false;
        }

        MQTTAgentCommand_t *command = *pReceivedCommand;
        if (command == NULL || command->commandType == NONE ||
            command->commandType == PROCESSLOOP ||
            command->commandType == PING || command->commandType == TERMINATE) {
            mqtt_lock(pMsgCtx->mqtt);
            pMsgCtx->mqtt->command_active = true;
            mqtt_unlock(pMsgCtx->mqtt);
            return true;
        }

        mqtt_operation_base_t *base =
            (mqtt_operation_base_t *) command->pCmdContext;
        mqtt_lock(pMsgCtx->mqtt);
        bool valid =
            base != NULL &&
            base->generation == pMsgCtx->mqtt->connect_generation &&
            ((base->type == MQTT_OPERATION_CONNECT &&
              pMsgCtx->mqtt->state == ACTRUST_MQTT_STATE_CONNECTING) ||
             (base->type == MQTT_OPERATION_DISCONNECT &&
              pMsgCtx->mqtt->state == ACTRUST_MQTT_STATE_DISCONNECTING) ||
             ((base->type == MQTT_OPERATION_PUBLISH ||
               base->type == MQTT_OPERATION_SUBSCRIBE ||
               base->type == MQTT_OPERATION_UNSUBSCRIBE) &&
              pMsgCtx->mqtt->state == ACTRUST_MQTT_STATE_CONNECTED));
        if (valid) {
            pMsgCtx->mqtt->command_active = true;
        }
        mqtt_unlock(pMsgCtx->mqtt);
        if (valid) {
            return true;
        }

        if (command->pCommandCompleteCallback != NULL) {
            MQTTAgentReturnInfo_t info = { .returnCode = MQTTRecvFailed };
            command->pCommandCompleteCallback(command->pCmdContext, &info);
        }
        mqtt_agent_release_command(command);
        *pReceivedCommand = NULL;
    }
}

static MQTTAgentCommand_t *mqtt_agent_get_command(uint32_t blockTimeMs)
{
    (void) blockTimeMs;
    return (MQTTAgentCommand_t *) ACTRUST_CALLOC(1, sizeof(MQTTAgentCommand_t));
}

static bool mqtt_agent_release_command(MQTTAgentCommand_t *pCommandToRelease)
{
    if (pCommandToRelease == NULL) {
        return false;
    }

    ACTRUST_FREE(pCommandToRelease);
    return true;
}

/* ========================================================================
 * Agent Callbacks
 * ======================================================================== */

static void mqtt_incoming_publish_callback(
    MQTTAgentContext_t *pMqttAgentContext, uint16_t packetId,
    MQTTPublishInfo_t *pPublishInfo)
{
    (void) packetId;

    if (pMqttAgentContext == NULL || pPublishInfo == NULL) {
        return;
    }

    actrust_mqtt_t mqtt =
        (actrust_mqtt_t) pMqttAgentContext->pIncomingCallbackContext;
    if (mqtt == NULL) {
        return;
    }

    actrust_mqtt_callbacks_t callbacks;
    mqtt_lock(mqtt);
    if (mqtt->lifecycle != MQTT_LIFECYCLE_OPEN ||
        mqtt->callbacks.on_message == NULL) {
        mqtt_unlock(mqtt);
        return;
    }
    callbacks = mqtt->callbacks;
    mqtt->callback_inflight++;
    mqtt_unlock(mqtt);

    actrust_mqtt_message_t message;
    memset(&message, 0, sizeof(message));
    message.topic       = (char *) pPublishInfo->pTopicName;
    message.topic_len   = (uint16_t) pPublishInfo->topicNameLength;
    message.payload     = (uint8_t *) pPublishInfo->pPayload;
    message.payload_len = pPublishInfo->payloadLength;
    message.qos         = coremqtt_qos_to_actrust_mqtt(pPublishInfo->qos);

    LOG_INFO("MQTT inbound message: topic_len=%u payload_len=%zu qos=%d",
             (unsigned int) message.topic_len, message.payload_len,
             (int) message.qos);
    LOG_DEBUG("MQTT inbound topic: %.*s", (int) message.topic_len,
              message.topic);

    callbacks.on_message(callbacks.user_ctx, &message);
    mqtt_lock(mqtt);
    if (mqtt->callback_inflight > 0u) {
        mqtt->callback_inflight--;
    }
    bool idle = mqtt->lifecycle == MQTT_LIFECYCLE_CLOSING &&
                mqtt_lifecycle_idle_locked(mqtt);
    mqtt_unlock(mqtt);
    if (idle) {
        mqtt_signal_lifecycle_idle(mqtt);
    }
}

static void mqtt_replay_subscriptions(actrust_mqtt_t mqtt)
{
    if (mqtt == NULL) {
        return;
    }

    for (size_t i = 0u; i < ACTRUST_MQTT_SUBSCRIPTION_MAX; ++i) {
        char     topic[ACTRUST_MQTT_TOPIC_MAX_LEN];
        uint32_t generation;
        uint32_t token;

        mqtt_lock(mqtt);
        mqtt_subscription_topic_t *slot = &mqtt->subscriptions[i];
        if (slot->state != MQTT_SUBSCRIPTION_ACTIVE) {
            mqtt_unlock(mqtt);
            continue;
        }
        generation  = mqtt->connect_generation;
        token       = mqtt_next_subscription_token_locked(mqtt);
        slot->state = MQTT_SUBSCRIPTION_PENDING;
        slot->token = token;
        memcpy(topic, slot->topic, strlen(slot->topic) + 1u);
        mqtt_unlock(mqtt);

        actrust_err_t err = mqtt_enqueue_subscribe_command(
            mqtt, topic, false, generation, i, token,
            ACTRUST_MQTT_CMD_BLOCK_TIME_MS);
        if (err != ACTRUST_OK) {
            mqtt_lock(mqtt);
            mqtt_subscription_rollback_locked(mqtt, i, token,
                                              MQTT_SUBSCRIPTION_ACTIVE);
            mqtt_unlock(mqtt);
        }
    }
}

static void mqtt_agent_command_complete(
    MQTTAgentCommandContext_t *pCmdCallbackContext,
    MQTTAgentReturnInfo_t     *pReturnInfo)
{
    if (pCmdCallbackContext == NULL) {
        return;
    }

    MQTTAgentReturnInfo_t fallback_info = { .returnCode = MQTTRecvFailed };
    if (pReturnInfo == NULL) {
        pReturnInfo = &fallback_info;
    }

    mqtt_operation_base_t *base = (mqtt_operation_base_t *) pCmdCallbackContext;
    MQTTStatus_t           status = pReturnInfo->returnCode;

    if (base->type == MQTT_OPERATION_PUBLISH) {
        LOG_DEBUG("MQTT publish complete: status=%d topic=%s", (int) status,
                  ((mqtt_publish_operation_t *) base)->topic);
    } else if (base->type == MQTT_OPERATION_SUBSCRIBE ||
               base->type == MQTT_OPERATION_UNSUBSCRIBE) {
        LOG_DEBUG("MQTT %s complete: status=%d suback=%d topic=%s",
                  base->type == MQTT_OPERATION_SUBSCRIBE ? "subscribe"
                                                         : "unsubscribe",
                  (int) status,
                  pReturnInfo->pSubackCodes != NULL
                      ? (int) pReturnInfo->pSubackCodes[0]
                      : -1,
                  ((mqtt_subscribe_operation_t *) base)->topic);
    } else {
        LOG_DEBUG("MQTT command complete: type=%d status=%d", (int) base->type,
                  (int) status);
    }

    switch (base->type) {
        case MQTT_OPERATION_CONNECT: {
            mqtt_connect_operation_t *connect_op =
                (mqtt_connect_operation_t *) base;
            bool accepted = mqtt_complete_connect_attempt(
                base->mqtt, base->generation, status);
            if (accepted && status == MQTTSuccess &&
                connect_op->connect_args.sessionPresent == false) {
                mqtt_replay_subscriptions(base->mqtt);
            }
            break;
        }
        case MQTT_OPERATION_DISCONNECT:
            mqtt_lock(base->mqtt);
            if (base->generation == base->mqtt->connect_generation &&
                base->mqtt->state == ACTRUST_MQTT_STATE_DISCONNECTING) {
                base->mqtt->disconnect_result =
                    mqtt_status_to_actrust_err(status);
                base->mqtt->disconnect_result_valid = true;
                if (status == MQTTSuccess) {
                    base->mqtt->state = ACTRUST_MQTT_STATE_DISCONNECTED;
                } else {
                    base->mqtt->state = ACTRUST_MQTT_STATE_CONNECTED;
                }
            }
            mqtt_unlock(base->mqtt);
            break;
        case MQTT_OPERATION_SUBSCRIBE:
        case MQTT_OPERATION_UNSUBSCRIBE: {
            mqtt_lock(base->mqtt);
            bool current =
                base->generation == base->mqtt->connect_generation &&
                base->subscription_index < ACTRUST_MQTT_SUBSCRIPTION_MAX &&
                base->mqtt->subscriptions[base->subscription_index].token ==
                    base->subscription_token;
            if (current) {
                bool accepted = status == MQTTSuccess;
                if (base->type == MQTT_OPERATION_SUBSCRIBE) {
                    accepted =
                        accepted && pReturnInfo->pSubackCodes != NULL &&
                        pReturnInfo->pSubackCodes[0] != MQTTSubAckFailure;
                    if (accepted) {
                        base->mqtt->subscriptions[base->subscription_index]
                            .state = MQTT_SUBSCRIPTION_ACTIVE;
                    } else {
                        mqtt_subscription_clear_locked(
                            base->mqtt, base->subscription_index,
                            base->subscription_token);
                    }
                } else if (accepted) {
                    mqtt_subscription_clear_locked(base->mqtt,
                                                   base->subscription_index,
                                                   base->subscription_token);
                } else {
                    base->mqtt->subscriptions[base->subscription_index].state =
                        MQTT_SUBSCRIPTION_ACTIVE;
                }
            }
            mqtt_unlock(base->mqtt);
            break;
        }
        default:
            if (status == MQTTSendFailed || status == MQTTRecvFailed ||
                status == MQTTKeepAliveTimeout) {
                mqtt_lock(base->mqtt);
                if (base->generation == base->mqtt->connect_generation &&
                    base->mqtt->state == ACTRUST_MQTT_STATE_CONNECTED) {
                    base->mqtt->state = ACTRUST_MQTT_STATE_WAITING_RECONNECT;
                }
                mqtt_unlock(base->mqtt);
            }
            break;
    }

    actrust_mqtt_t mqtt = base->mqtt;
    if (base->type == MQTT_OPERATION_CONNECT) {
        mqtt_connect_operation_t *connect_op =
            (mqtt_connect_operation_t *) base;
        actrust_secure_free(connect_op, connect_op->alloc_len);
    } else {
        ACTRUST_FREE(base);
    }
    mqtt_operation_leave(mqtt);
}

/* ========================================================================
 * Enqueue Helpers
 * ======================================================================== */

static actrust_err_t mqtt_enqueue_connect_command(actrust_mqtt_t mqtt,
                                                  bool           clean_session,
                                                  uint32_t connect_generation,
                                                  uint32_t block_time_ms)
{
    size_t                    client_id_len;
    size_t                    alloc_len;
    mqtt_connect_operation_t *op;
    MQTTStatus_t              status;

    if (mqtt == NULL || mqtt->config.client_id == NULL) {
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }
    client_id_len = strlen(mqtt->config.client_id);
    if (client_id_len > UINT16_MAX || client_id_len == SIZE_MAX ||
        client_id_len + 1u > SIZE_MAX - sizeof(*op)) {
        return MQTT_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }
    alloc_len = sizeof(*op) + client_id_len + 1u;
    op        = (mqtt_connect_operation_t *) ACTRUST_CALLOC(1u, alloc_len);
    if (op == NULL) {
        return MQTT_ERR(ACTRUST_ERR_NO_MEM);
    }

    op->base.type       = MQTT_OPERATION_CONNECT;
    op->base.mqtt       = mqtt;
    op->base.generation = connect_generation;
    op->alloc_len       = alloc_len;
    memcpy(op->client_id, mqtt->config.client_id, client_id_len + 1u);

    op->connect_info.cleanSession           = clean_session;
    op->connect_info.keepAliveSeconds       = CONFIG_ACTRUST_MQTT_KEEPALIVE_SEC;
    op->connect_info.pClientIdentifier      = op->client_id;
    op->connect_info.clientIdentifierLength = client_id_len;

    op->connect_args.pConnectInfo = &op->connect_info;
    op->connect_args.pWillInfo    = NULL;
    op->connect_args.timeoutMs    = CONFIG_ACTRUST_MQTT_CONNECT_TIMEOUT_MS;

    MQTTAgentCommandInfo_t cmd_info = {
        .cmdCompleteCallback         = mqtt_agent_command_complete,
        .pCmdCompleteCallbackContext = (MQTTAgentCommandContext_t *) op,
        .blockTimeMs                 = block_time_ms,
    };

    mqtt_operation_enter(mqtt);
    status = MQTTAgent_Connect(&mqtt->agent_ctx, &op->connect_args, &cmd_info);
    if (status != MQTTSuccess) {
        mqtt_operation_leave(mqtt);
        actrust_secure_free(op, alloc_len);
    }

    return mqtt_enqueue_status_to_actrust_err(status);
}

static actrust_err_t mqtt_enqueue_disconnect_command(actrust_mqtt_t mqtt,
                                                     uint32_t       generation,
                                                     uint32_t block_time_ms)
{
    mqtt_simple_operation_t *op =
        (mqtt_simple_operation_t *) ACTRUST_CALLOC(1, sizeof(*op));
    if (op == NULL) {
        return MQTT_ERR(ACTRUST_ERR_NO_MEM);
    }

    op->base.type       = MQTT_OPERATION_DISCONNECT;
    op->base.mqtt       = mqtt;
    op->base.generation = generation;

    MQTTAgentCommandInfo_t cmd_info = {
        .cmdCompleteCallback         = mqtt_agent_command_complete,
        .pCmdCompleteCallbackContext = (MQTTAgentCommandContext_t *) op,
        .blockTimeMs                 = block_time_ms,
    };

    mqtt_operation_enter(mqtt);
    MQTTStatus_t status = MQTTAgent_Disconnect(&mqtt->agent_ctx, &cmd_info);
    if (status != MQTTSuccess) {
        mqtt_operation_leave(mqtt);
        ACTRUST_FREE(op);
    }

    return mqtt_enqueue_status_to_actrust_err(status);
}

static actrust_err_t mqtt_enqueue_publish_command(
    actrust_mqtt_t mqtt, const actrust_mqtt_message_t *message,
    uint32_t generation, uint32_t block_time_ms)
{
    mqtt_publish_operation_t *op =
        (mqtt_publish_operation_t *) ACTRUST_CALLOC(1, sizeof(*op));
    if (op == NULL) {
        return MQTT_ERR(ACTRUST_ERR_NO_MEM);
    }

    op->base.type       = MQTT_OPERATION_PUBLISH;
    op->base.mqtt       = mqtt;
    op->base.generation = generation;

    memcpy(op->topic, message->topic, message->topic_len);
    op->topic[message->topic_len] = '\0';
    if (message->payload_len > 0u) {
        memcpy(op->payload, message->payload, message->payload_len);
    }

    op->publish_info.qos        = actrust_mqtt_qos_to_coremqtt(message->qos);
    op->publish_info.retain     = false;
    op->publish_info.dup        = false;
    op->publish_info.pTopicName = op->topic;
    op->publish_info.topicNameLength = message->topic_len;
    op->publish_info.pPayload        = op->payload;
    op->publish_info.payloadLength   = message->payload_len;

    MQTTAgentCommandInfo_t cmd_info = {
        .cmdCompleteCallback         = mqtt_agent_command_complete,
        .pCmdCompleteCallbackContext = (MQTTAgentCommandContext_t *) op,
        .blockTimeMs                 = block_time_ms,
    };

    mqtt_operation_enter(mqtt);
    MQTTStatus_t status =
        MQTTAgent_Publish(&mqtt->agent_ctx, &op->publish_info, &cmd_info);
    if (status != MQTTSuccess) {
        mqtt_operation_leave(mqtt);
        ACTRUST_FREE(op);
    } else {
        LOG_INFO("MQTT outbound message: topic_len=%u payload_len=%zu qos=%d",
                 (unsigned int) message->topic_len, message->payload_len,
                 (int) message->qos);
        LOG_DEBUG("MQTT outbound topic: %.*s", (int) message->topic_len,
                  message->topic);
    }

    return mqtt_enqueue_status_to_actrust_err(status);
}

static actrust_err_t mqtt_enqueue_subscribe_command(
    actrust_mqtt_t mqtt, const char *topic, bool is_unsubscribe,
    uint32_t generation, size_t subscription_index, uint32_t subscription_token,
    uint32_t block_time_ms)
{
    mqtt_subscribe_operation_t *op =
        (mqtt_subscribe_operation_t *) ACTRUST_CALLOC(1, sizeof(*op));
    if (op == NULL) {
        return MQTT_ERR(ACTRUST_ERR_NO_MEM);
    }

    op->base.type =
        is_unsubscribe ? MQTT_OPERATION_UNSUBSCRIBE : MQTT_OPERATION_SUBSCRIBE;
    op->base.mqtt               = mqtt;
    op->base.generation         = generation;
    op->base.subscription_index = subscription_index;
    op->base.subscription_token = subscription_token;
    size_t topic_len            = strlen(topic);

    memcpy(op->topic, topic, topic_len);
    op->topic[topic_len] = '\0';

    op->subscribe_info.qos               = MQTTQoS0;
    op->subscribe_info.pTopicFilter      = op->topic;
    op->subscribe_info.topicFilterLength = topic_len;
    op->subscribe_args.pSubscribeInfo    = &op->subscribe_info;
    op->subscribe_args.numSubscriptions  = 1u;

    MQTTAgentCommandInfo_t cmd_info = {
        .cmdCompleteCallback         = mqtt_agent_command_complete,
        .pCmdCompleteCallbackContext = (MQTTAgentCommandContext_t *) op,
        .blockTimeMs                 = block_time_ms,
    };

    mqtt_operation_enter(mqtt);
    MQTTStatus_t status = MQTTSuccess;
    if (is_unsubscribe == true) {
        status = MQTTAgent_Unsubscribe(&mqtt->agent_ctx, &op->subscribe_args,
                                       &cmd_info);
    } else {
        status = MQTTAgent_Subscribe(&mqtt->agent_ctx, &op->subscribe_args,
                                     &cmd_info);
    }

    if (status != MQTTSuccess) {
        mqtt_operation_leave(mqtt);
        ACTRUST_FREE(op);
    }

    return mqtt_enqueue_status_to_actrust_err(status);
}

static actrust_err_t mqtt_try_schedule_connect(actrust_mqtt_t mqtt)
{
    if (mqtt == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    uint16_t                 backoff_ms     = 0u;
    BackoffAlgorithmStatus_t backoff_status = BackoffAlgorithm_GetNextBackoff(
        &mqtt->backoff_ctx, mqtt_random_u32(mqtt), &backoff_ms);
    if (backoff_status == BackoffAlgorithmRetriesExhausted) {
        return MQTT_ERR(
            ACTRUST_ERR_MQTT_RECONNECT_FAILED); // Failed to get next backoff.
    }

    if (mqtt_sleep_stopped(mqtt, (uint32_t) backoff_ms) == true) {
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }

    if (mqtt_is_process_stop_requested(mqtt) == true) {
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }

    mqtt_lock(mqtt);
    uint32_t generation = mqtt_next_connect_generation_locked(mqtt);
    mqtt->state         = ACTRUST_MQTT_STATE_CONNECTING;
    for (size_t i = 0u; i < ACTRUST_MQTT_SUBSCRIPTION_MAX; ++i) {
        mqtt_subscription_topic_t *slot = &mqtt->subscriptions[i];
        if (slot->state == MQTT_SUBSCRIPTION_PENDING) {
            mqtt_subscription_clear_locked(mqtt, i, slot->token);
        } else if (slot->state == MQTT_UNSUBSCRIPTION_PENDING) {
            slot->state = MQTT_SUBSCRIPTION_ACTIVE;
        }
    }
    mqtt_unlock(mqtt);

    actrust_err_t ret = mqtt_transport_open(mqtt);
    if (ret != ACTRUST_OK) {
        mqtt_lock(mqtt);
        if (mqtt->connect_generation == generation) {
            mqtt->state = ACTRUST_MQTT_STATE_WAITING_RECONNECT;
        }
        mqtt_unlock(mqtt);
        return ret;
    }

    /* The agent command loop has returned; reconnect synchronously before
     * entering the loop again so CONNECT is not queued to a stopped loop. */
    MQTTConnectInfo_t connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.cleanSession           = false;
    connect_info.keepAliveSeconds       = CONFIG_ACTRUST_MQTT_KEEPALIVE_SEC;
    connect_info.pClientIdentifier      = mqtt->config.client_id;
    connect_info.clientIdentifierLength = strlen(mqtt->config.client_id);

    bool         session_present = false;
    MQTTStatus_t status =
        MQTT_Connect(&mqtt->agent_ctx.mqttContext, &connect_info, NULL,
                     CONFIG_ACTRUST_MQTT_CONNECT_TIMEOUT_MS, &session_present);
    if (status != MQTTSuccess) {
        mqtt_transport_close(mqtt);
        return mqtt_status_to_actrust_err(status);
    }

    status = MQTTAgent_ResumeSession(&mqtt->agent_ctx, session_present);
    if (status != MQTTSuccess) {
        mqtt_transport_close(mqtt);
        return mqtt_status_to_actrust_err(status);
    }

    mqtt_lock(mqtt);
    if (mqtt->connect_generation == generation &&
        mqtt->state == ACTRUST_MQTT_STATE_CONNECTING) {
        mqtt->state = ACTRUST_MQTT_STATE_CONNECTED;
    }
    mqtt_unlock(mqtt);
    if (session_present == false) {
        mqtt_replay_subscriptions(mqtt);
    }
    return ACTRUST_OK;
}

static actrust_err_t mqtt_reconnect_with_backoff(actrust_mqtt_t mqtt)
{
    if (mqtt == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    BackoffAlgorithm_InitializeParams(
        &mqtt->backoff_ctx, ACTRUST_MQTT_CONNECT_RETRY_BASE_MS,
        ACTRUST_MQTT_CONNECT_RETRY_MAX_MS, ACTRUST_MQTT_CONNECT_RETRY_ATTEMPTS);

    for (;;) {
        if (mqtt_is_process_stop_requested(mqtt) == true) {
            return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
        }

        actrust_err_t ret = mqtt_try_schedule_connect(mqtt);
        if (ret == ACTRUST_OK ||
            ret == MQTT_ERR(ACTRUST_ERR_MQTT_RECONNECT_FAILED)) {
            return ret;
        }

        mqtt_set_state(mqtt, ACTRUST_MQTT_STATE_WAITING_RECONNECT);
    }
}

/* ========================================================================
 * Public Functions
 * ======================================================================== */

actrust_err_t actrust_mqtt_init(actrust_mqtt_t *out_mqtt)
{
    if (out_mqtt == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_mqtt_t mqtt =
        (actrust_mqtt_t) ACTRUST_CALLOC(1, sizeof(struct actrust_mqtt_ctx));
    if (mqtt == NULL) {
        return MQTT_ERR(ACTRUST_ERR_NO_MEM);
    }

    actrust_err_t ret = actrust_mutex_create(&mqtt->lock);
    if (ret != ACTRUST_OK) {
        ret = MQTT_ERR(ACTRUST_ERR_HW_FAILURE);
        goto fail;
    }

    ret = actrust_sem_create(&mqtt->process_done_sem, 0u);
    if (ret != ACTRUST_OK) {
        ret = MQTT_ERR(ACTRUST_ERR_HW_FAILURE);
        goto fail;
    }

    ret = actrust_sem_create(&mqtt->command_done_sem, 0u);
    if (ret != ACTRUST_OK) {
        ret = MQTT_ERR(ACTRUST_ERR_HW_FAILURE);
        goto fail;
    }

    ret = actrust_sem_create(&mqtt->lifecycle_done_sem, 0u);
    if (ret != ACTRUST_OK) {
        ret = MQTT_ERR(ACTRUST_ERR_HW_FAILURE);
        goto fail;
    }

    mqtt->network_buffer.pBuffer =
        (uint8_t *) ACTRUST_CALLOC(1, (size_t) ACTRUST_MQTT_NET_BUF_SIZE);
    if (mqtt->network_buffer.pBuffer == NULL) {
        ret = MQTT_ERR(ACTRUST_ERR_NO_MEM);
        goto fail;
    }
    mqtt->network_buffer.size = (size_t) ACTRUST_MQTT_NET_BUF_SIZE;

    ret = actrust_queue_create(&mqtt->command_queue,
                               (size_t) ACTRUST_MQTT_QUEUE_CAPACITY,
                               sizeof(MQTTAgentCommand_t *));
    if (ret != ACTRUST_OK) {
        goto fail;
    }

    mqtt->subscriptions = (mqtt_subscription_topic_t *) ACTRUST_CALLOC(
        ACTRUST_MQTT_SUBSCRIPTION_MAX, sizeof(mqtt_subscription_topic_t));
    if (mqtt->subscriptions == NULL) {
        ret = MQTT_ERR(ACTRUST_ERR_NO_MEM);
        goto fail;
    }

    ret = actrust_crypto_init(&mqtt->crypto_ctx);
    if (ret != ACTRUST_OK) {
        goto fail;
    }

    mqtt->transport.send            = mqtt_transport_send;
    mqtt->transport.recv            = mqtt_transport_recv;
    mqtt->transport.writev          = NULL;
    mqtt->transport.pNetworkContext = &mqtt->net_ctx;

    mqtt->message_ctx.command_queue        = mqtt->command_queue;
    mqtt->message_ctx.mqtt                 = mqtt;
    mqtt->message_interface.pMsgCtx        = &mqtt->message_ctx;
    mqtt->message_interface.send           = mqtt_agent_send;
    mqtt->message_interface.recv           = mqtt_agent_recv;
    mqtt->message_interface.getCommand     = mqtt_agent_get_command;
    mqtt->message_interface.releaseCommand = mqtt_agent_release_command;

    MQTTStatus_t status =
        MQTTAgent_Init(&mqtt->agent_ctx, &mqtt->message_interface,
                       &mqtt->network_buffer, &mqtt->transport,
                       mqtt_get_time_ms, mqtt_incoming_publish_callback, mqtt);
    if (status != MQTTSuccess) {
        ret = mqtt_status_to_actrust_err(status);
        goto fail;
    }

    *out_mqtt = mqtt;
    mqtt_set_state(mqtt, ACTRUST_MQTT_STATE_INITIALIZED);

    return ACTRUST_OK;

fail:
    (void) actrust_crypto_deinit(&mqtt->crypto_ctx);
    (void) actrust_queue_destroy(&mqtt->command_queue);
    ACTRUST_FREE(mqtt->subscriptions);
    ACTRUST_FREE(mqtt->network_buffer.pBuffer);
    (void) actrust_sem_destroy(mqtt->lifecycle_done_sem);
    (void) actrust_sem_destroy(mqtt->command_done_sem);
    (void) actrust_sem_destroy(mqtt->process_done_sem);
    (void) actrust_mutex_destroy(mqtt->lock);
    ACTRUST_FREE(mqtt);
    return ret;
}

static void mqtt_deinit_abort(actrust_mqtt_t mqtt)
{
    mqtt_lock(mqtt);
    mqtt->deinit_inflight = false;
    mqtt_unlock(mqtt);
}

actrust_err_t actrust_mqtt_deinit(actrust_mqtt_t mqtt)
{
    if (mqtt == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    mqtt_lock(mqtt);
    if (mqtt->callback_inflight > 0u || mqtt->deinit_inflight == true) {
        mqtt_unlock(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }
    if (mqtt->lifecycle == MQTT_LIFECYCLE_OPEN &&
        (mqtt->state == ACTRUST_MQTT_STATE_CONNECTED ||
         mqtt->state == ACTRUST_MQTT_STATE_CONNECTING)) {
        mqtt_unlock(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }
    if (mqtt->lifecycle == MQTT_LIFECYCLE_OPEN) {
        mqtt->lifecycle = MQTT_LIFECYCLE_CLOSING;
    }
    mqtt->deinit_inflight        = true;
    mqtt->process_stop_requested = true;
    mqtt_unlock(mqtt);

    if (mqtt_is_process_running(mqtt) == true) {
        actrust_err_t err = mqtt_signal_process_stop_internal(mqtt);
        if (err != ACTRUST_OK) {
            mqtt_lock(mqtt);
            mqtt->deinit_inflight = false;
            mqtt_unlock(mqtt);
            return err;
        }
        err = mqtt_wait_for_process_stop(mqtt,
                                         ACTRUST_MQTT_PROCESS_STOP_TIMEOUT_MS);
        if (err != ACTRUST_OK) {
            mqtt_lock(mqtt);
            mqtt->deinit_inflight = false;
            mqtt_unlock(mqtt);
            return err;
        }
    }

    actrust_err_t err =
        mqtt_wait_for_command_claim(mqtt, ACTRUST_MQTT_PROCESS_STOP_TIMEOUT_MS);
    if (err != ACTRUST_OK) {
        mqtt_deinit_abort(mqtt);
        return err;
    }
    MQTTStatus_t status = MQTTSuccess;
    if (mqtt->agent_cancelled == false) {
        status = MQTTAgent_CancelAll(&mqtt->agent_ctx);
        if (status != MQTTSuccess) {
            mqtt_lock(mqtt);
            mqtt->deinit_inflight = false;
            mqtt_unlock(mqtt);
            return mqtt_status_to_actrust_err(status);
        }
        mqtt->agent_cancelled = true;
    }
    mqtt_release_command_claim(mqtt);

    err = mqtt_wait_for_lifecycle_idle(mqtt,
                                       ACTRUST_MQTT_PROCESS_STOP_TIMEOUT_MS);
    if (err != ACTRUST_OK) {
        mqtt_lock(mqtt);
        mqtt->deinit_inflight = false;
        mqtt_unlock(mqtt);
        return err;
    }

    mqtt_transport_close(mqtt);
    mqtt_owned_config_release(&mqtt->config);
    if (mqtt->crypto_ctx != NULL) {
        err = actrust_crypto_deinit(&mqtt->crypto_ctx);
        if (err != ACTRUST_OK) {
            mqtt_deinit_abort(mqtt);
            return err;
        }
    }
    if (mqtt->command_queue != NULL) {
        err = actrust_queue_destroy(&mqtt->command_queue);
        if (err != ACTRUST_OK) {
            mqtt_deinit_abort(mqtt);
            return err;
        }
    }
    ACTRUST_FREE(mqtt->subscriptions);
    mqtt->subscriptions = NULL;
    ACTRUST_FREE(mqtt->network_buffer.pBuffer);
    mqtt->network_buffer.pBuffer = NULL;
    if (mqtt->lifecycle_done_sem != NULL) {
        err = actrust_sem_destroy(mqtt->lifecycle_done_sem);
        if (err != ACTRUST_OK) {
            mqtt_deinit_abort(mqtt);
            return err;
        }
        mqtt->lifecycle_done_sem = NULL;
    }
    if (mqtt->command_done_sem != NULL) {
        err = actrust_sem_destroy(mqtt->command_done_sem);
        if (err != ACTRUST_OK) {
            mqtt_deinit_abort(mqtt);
            return err;
        }
        mqtt->command_done_sem = NULL;
    }
    if (mqtt->process_done_sem != NULL) {
        err = actrust_sem_destroy(mqtt->process_done_sem);
        if (err != ACTRUST_OK) {
            mqtt_deinit_abort(mqtt);
            return err;
        }
        mqtt->process_done_sem = NULL;
    }
    if (mqtt->lock != NULL) {
        err = actrust_mutex_destroy(mqtt->lock);
        if (err != ACTRUST_OK) {
            mqtt_deinit_abort(mqtt);
            return err;
        }
        mqtt->lock = NULL;
    }
    ACTRUST_FREE(mqtt);
    return ACTRUST_OK;
}

actrust_err_t actrust_mqtt_set_callbacks(
    actrust_mqtt_t mqtt, const actrust_mqtt_callbacks_t *callbacks)
{
    if (mqtt == NULL || callbacks == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    actrust_err_t err = mqtt_api_enter(mqtt);
    if (err != ACTRUST_OK) {
        return err;
    }
    mqtt_lock(mqtt);
    if (mqtt->state == ACTRUST_MQTT_STATE_CONNECTED ||
        mqtt->state == ACTRUST_MQTT_STATE_CONNECTING) {
        mqtt_unlock(mqtt);
        mqtt_api_leave(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }
    mqtt->callbacks = *callbacks;
    mqtt_unlock(mqtt);
    mqtt_api_leave(mqtt);
    return ACTRUST_OK;
}

actrust_err_t actrust_mqtt_connect(actrust_mqtt_t               mqtt,
                                   const actrust_mqtt_config_t *config)
{
    actrust_mqtt_config_t candidate;
    actrust_mqtt_config_t replaced;
    actrust_mqtt_state_t  previous_state;
    uint32_t              connect_generation;
    actrust_err_t         err;

    if (mqtt == NULL || config == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    err = mqtt_api_enter(mqtt);
    if (err != ACTRUST_OK) {
        return err;
    }

    err = mqtt_begin_connect(mqtt, &previous_state, &connect_generation);
    if (err != ACTRUST_OK) {
        mqtt_api_leave(mqtt);
        return err;
    }

    actrust_secure_zeroize(&candidate, sizeof(candidate));
    actrust_secure_zeroize(&replaced, sizeof(replaced));
    err = mqtt_owned_config_clone(&candidate, config);
    if (err != ACTRUST_OK) {
        mqtt_release_connect_reservation(mqtt, connect_generation,
                                         previous_state);
        mqtt_api_leave(mqtt);
        return err;
    }

    err = mqtt_install_config(mqtt, &candidate, &replaced, connect_generation);
    if (err != ACTRUST_OK) {
        mqtt_owned_config_release(&candidate);
        mqtt_release_connect_reservation(mqtt, connect_generation,
                                         previous_state);
        mqtt_api_leave(mqtt);
        return err;
    }
    mqtt_owned_config_release(&replaced);

    err = mqtt_transport_open(mqtt);
    if (err != ACTRUST_OK) {
        mqtt_set_state(mqtt, ACTRUST_MQTT_STATE_DISCONNECTED);
        mqtt_api_leave(mqtt);
        return err;
    }

    err = mqtt_enqueue_connect_command(mqtt, true, connect_generation,
                                       ACTRUST_MQTT_CMD_BLOCK_TIME_MS);
    if (err != ACTRUST_OK) {
        mqtt_transport_close(mqtt);
        mqtt_set_state(mqtt, ACTRUST_MQTT_STATE_DISCONNECTED);
        mqtt_api_leave(mqtt);
        return err;
    }

    err = mqtt_wait_for_connect_result(mqtt, connect_generation,
                                       ACTRUST_MQTT_CONNECT_WAIT_TIMEOUT_MS);
    if (err != ACTRUST_OK) {
        mqtt_cancel_connect_attempt(mqtt, connect_generation);
        actrust_err_t claim_err = mqtt_wait_for_command_claim(
            mqtt, ACTRUST_MQTT_CONNECT_WAIT_TIMEOUT_MS);
        if (claim_err == ACTRUST_OK) {
            mqtt_transport_close(mqtt);
        }
        mqtt_api_leave(mqtt);
        return err;
    }

    mqtt_api_leave(mqtt);
    return ACTRUST_OK;
}

actrust_err_t actrust_mqtt_disconnect(actrust_mqtt_t mqtt)
{
    if (mqtt == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = mqtt_api_enter(mqtt);
    if (err != ACTRUST_OK) {
        return err;
    }
    mqtt_lock(mqtt);
    if (mqtt->state != ACTRUST_MQTT_STATE_CONNECTED) {
        mqtt_unlock(mqtt);
        mqtt_api_leave(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }
    uint32_t generation           = mqtt->connect_generation;
    mqtt->state                   = ACTRUST_MQTT_STATE_DISCONNECTING;
    mqtt->disconnect_result_valid = false;
    err = mqtt_enqueue_disconnect_command(mqtt, generation,
                                          ACTRUST_MQTT_CMD_BLOCK_TIME_MS);
    if (err != ACTRUST_OK) {
        mqtt->state = ACTRUST_MQTT_STATE_CONNECTED;
    }
    mqtt_unlock(mqtt);
    if (err != ACTRUST_OK) {
        mqtt_api_leave(mqtt);
        return err;
    }
    err = mqtt_wait_for_disconnect_result(
        mqtt, generation, CONFIG_ACTRUST_MQTT_CONNECT_TIMEOUT_MS);
    if (err != ACTRUST_OK) {
        mqtt_abort_disconnect(mqtt, generation);
        actrust_err_t claim_err = mqtt_wait_for_command_claim(
            mqtt, ACTRUST_MQTT_CONNECT_WAIT_TIMEOUT_MS);
        if (claim_err == ACTRUST_OK) {
            mqtt_transport_close(mqtt);
        }
    }
    mqtt_api_leave(mqtt);
    return err;
}

actrust_err_t actrust_mqtt_publish(actrust_mqtt_t                mqtt,
                                   const actrust_mqtt_message_t *message)
{
    if (mqtt == NULL || message == NULL || message->topic == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (message->topic_len == 0u) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }
    if (message->topic_len >= ACTRUST_MQTT_TOPIC_MAX_LEN ||
        message->payload_len > CONFIG_ACTRUST_MQTT_PAYLOAD_MAX_LEN) {
        return MQTT_ERR(ACTRUST_ERR_BUF_TOO_SMALL);
    }

    if (message->payload == NULL && message->payload_len > 0u) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = mqtt_api_enter(mqtt);
    if (err != ACTRUST_OK) {
        return err;
    }
    mqtt_lock(mqtt);
    if (mqtt->state != ACTRUST_MQTT_STATE_CONNECTED) {
        mqtt_unlock(mqtt);
        mqtt_api_leave(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }
    uint32_t generation = mqtt->connect_generation;
    err = mqtt_enqueue_publish_command(mqtt, message, generation,
                                       ACTRUST_MQTT_CMD_BLOCK_TIME_MS);
    mqtt_unlock(mqtt);
    mqtt_api_leave(mqtt);
    return err;
}

actrust_err_t actrust_mqtt_subscribe(actrust_mqtt_t mqtt, const char *topic)
{
    if (mqtt == NULL || topic == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    size_t topic_len = strlen(topic);
    if (topic_len == 0u || topic_len >= ACTRUST_MQTT_TOPIC_MAX_LEN) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = mqtt_api_enter(mqtt);
    if (err != ACTRUST_OK) {
        return err;
    }
    mqtt_lock(mqtt);
    if (mqtt->state != ACTRUST_MQTT_STATE_CONNECTED) {
        mqtt_unlock(mqtt);
        mqtt_api_leave(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }

    uint32_t      generation = mqtt->connect_generation;
    size_t        index;
    uint32_t      token;
    actrust_err_t ret =
        mqtt_subscription_reserve_locked(mqtt, topic, false, &index, &token);
    if (ret == ACTRUST_OK) {
        ret = mqtt_enqueue_subscribe_command(mqtt, topic, false, generation,
                                             index, token,
                                             ACTRUST_MQTT_CMD_BLOCK_TIME_MS);
        if (ret != ACTRUST_OK) {
            mqtt_subscription_rollback_locked(mqtt, index, token,
                                              MQTT_SUBSCRIPTION_EMPTY);
        }
    }
    mqtt_unlock(mqtt);
    mqtt_api_leave(mqtt);
    return ret;
}

actrust_err_t actrust_mqtt_unsubscribe(actrust_mqtt_t mqtt, const char *topic)
{
    if (mqtt == NULL || topic == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    size_t topic_len = strlen(topic);
    if (topic_len == 0u || topic_len >= ACTRUST_MQTT_TOPIC_MAX_LEN) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = mqtt_api_enter(mqtt);
    if (err != ACTRUST_OK) {
        return err;
    }
    mqtt_lock(mqtt);
    if (mqtt->state != ACTRUST_MQTT_STATE_CONNECTED) {
        mqtt_unlock(mqtt);
        mqtt_api_leave(mqtt);
        return MQTT_ERR(ACTRUST_ERR_BAD_STATE);
    }

    uint32_t      generation = mqtt->connect_generation;
    size_t        index;
    uint32_t      token;
    actrust_err_t ret =
        mqtt_subscription_reserve_locked(mqtt, topic, true, &index, &token);
    if (ret == ACTRUST_OK) {
        ret = mqtt_enqueue_subscribe_command(mqtt, topic, true, generation,
                                             index, token,
                                             ACTRUST_MQTT_CMD_BLOCK_TIME_MS);
        if (ret != ACTRUST_OK) {
            mqtt_subscription_rollback_locked(mqtt, index, token,
                                              MQTT_SUBSCRIPTION_ACTIVE);
        }
    }
    mqtt_unlock(mqtt);
    mqtt_api_leave(mqtt);
    return ret;
}

actrust_err_t actrust_mqtt_process(actrust_mqtt_t mqtt)
{
    if (mqtt == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t ret = mqtt_api_enter(mqtt);
    if (ret != ACTRUST_OK) {
        return ret;
    }

    ret = mqtt_mark_process_started(mqtt);
    if (ret != ACTRUST_OK) {
        mqtt_api_leave(mqtt);
        return ret;
    }

    for (;;) {
        if (mqtt_is_process_stop_requested(mqtt) == true) {
            if (mqtt->agent_ctx.mqttContext.connectStatus == MQTTConnected) {
                (void) MQTT_Disconnect(&mqtt->agent_ctx.mqttContext);
            }
            mqtt_transport_close(mqtt);
            ret = ACTRUST_OK;
            break;
        }

        MQTTStatus_t         status = MQTTAgent_CommandLoop(&mqtt->agent_ctx);
        actrust_mqtt_state_t state;
        bool                 should_reconnect = false;
        bool                 stop_requested   = false;

        mqtt_release_command_claim(mqtt);
        mqtt_get_state(mqtt, &state);
        stop_requested = mqtt_is_process_stop_requested(mqtt);

        if (status != MQTTSuccess && stop_requested == false) {
            if (state == ACTRUST_MQTT_STATE_WAITING_RECONNECT) {
                should_reconnect = true;
            } else if (status == MQTTSendFailed || status == MQTTRecvFailed ||
                       status == MQTTKeepAliveTimeout) {
                should_reconnect = true;
            }
        }

        if (mqtt->agent_ctx.mqttContext.connectStatus == MQTTConnected) {
            (void) MQTT_Disconnect(&mqtt->agent_ctx.mqttContext);
        }
        mqtt_transport_close(mqtt);

        if (status == MQTTSuccess || stop_requested == true) {
            ret = ACTRUST_OK;
            break;
        }

        if (should_reconnect) {
            mqtt_lock(mqtt);
            bool closing = mqtt->lifecycle == MQTT_LIFECYCLE_CLOSING;
            mqtt_unlock(mqtt);
            if (closing) {
                ret = ACTRUST_OK;
                break;
            }
            mqtt_set_state(mqtt, ACTRUST_MQTT_STATE_WAITING_RECONNECT);
        } else {
            ret = mqtt_status_to_actrust_err(status);
            break; // Not waiting to reconnect.
        }

        ret = mqtt_reconnect_with_backoff(mqtt);

        if (ret != ACTRUST_OK) {
            if (mqtt_is_process_stop_requested(mqtt) == true) {
                ret = ACTRUST_OK;
            }
            break;
        }
    }

    mqtt_lock(mqtt);
    bool closing = mqtt->lifecycle == MQTT_LIFECYCLE_CLOSING;
    mqtt_unlock(mqtt);
    if (!closing) {
        mqtt_set_state(mqtt, ACTRUST_MQTT_STATE_DISCONNECTED);
    }
    mqtt_mark_process_stopped(mqtt);
    mqtt_api_leave(mqtt);

    return ret;
}

actrust_err_t actrust_mqtt_stop_process(actrust_mqtt_t mqtt)
{
    if (mqtt == NULL) {
        return MQTT_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = mqtt_api_enter(mqtt);
    if (err != ACTRUST_OK) {
        return err;
    }
    err = mqtt_signal_process_stop(mqtt);
    mqtt_api_leave(mqtt);
    return err;
}
