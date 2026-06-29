// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file queue.c
 * @brief Bounded FIFO queue backed by a mutex + semaphore pair, used by
 *        the cloud and core layers to hand off fixed-size messages between
 *        producer and consumer tasks.
 */

/* C standard */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Common */
#include "common/common.h"

/* Queue */
#include "queue/queue.h"

/* Adapter */
#include "adapter/system.h"

#define QUEUE_ERR(reason)                                                      \
    ACTRUST_ERR(ACTRUST_ERR_MODULE_COMPONENTS_QUEUE, (reason))

struct actrust_queue {
    uint8_t        *buffer;
    size_t          capacity;
    size_t          item_size;
    size_t          head;
    size_t          tail;
    size_t          size;
    actrust_mutex_t lock;
    actrust_sem_t   empty_slots;
    actrust_sem_t   filled_slots;
};

actrust_err_t actrust_queue_create(actrust_queue_t *out_queue, size_t capacity,
                                   size_t item_size)
{
    if (out_queue == NULL || capacity == 0u || item_size == 0u) {
        return QUEUE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    if (capacity > UINT32_MAX || capacity > SIZE_MAX / item_size) {
        return QUEUE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    size_t total_bytes = capacity * item_size;

    actrust_queue_t queue = (actrust_queue_t) ACTRUST_CALLOC(1, sizeof(*queue));
    if (queue == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_NO_MEM);
    }

    queue->buffer = (uint8_t *) ACTRUST_MALLOC(total_bytes);
    if (queue->buffer == NULL) {
        ACTRUST_FREE(queue);
        return QUEUE_ERR(ACTRUST_ERR_NO_MEM);
    }

    queue->capacity  = capacity;
    queue->item_size = item_size;

    actrust_err_t err = actrust_mutex_create(&queue->lock);
    if (err != ACTRUST_OK) {
        ACTRUST_FREE(queue->buffer);
        ACTRUST_FREE(queue);
        return err;
    }

    err = actrust_sem_create(&queue->empty_slots, (uint32_t) capacity);
    if (err != ACTRUST_OK) {
        actrust_mutex_destroy(queue->lock);
        ACTRUST_FREE(queue->buffer);
        ACTRUST_FREE(queue);
        return err;
    }

    err = actrust_sem_create(&queue->filled_slots, 0u);
    if (err != ACTRUST_OK) {
        actrust_sem_destroy(queue->empty_slots);
        actrust_mutex_destroy(queue->lock);
        ACTRUST_FREE(queue->buffer);
        ACTRUST_FREE(queue);
        return err;
    }

    *out_queue = queue;
    return ACTRUST_OK;
}

actrust_err_t actrust_queue_destroy(actrust_queue_t *queue)
{
    if (queue == NULL || *queue == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_queue_t q         = *queue;
    actrust_err_t   first_err = ACTRUST_OK;
    actrust_err_t   err;

    err = actrust_mutex_destroy(q->lock);
    if (first_err == ACTRUST_OK && err != ACTRUST_OK) {
        first_err = err;
    }

    err = actrust_sem_destroy(q->empty_slots);
    if (first_err == ACTRUST_OK && err != ACTRUST_OK) {
        first_err = err;
    }

    err = actrust_sem_destroy(q->filled_slots);
    if (first_err == ACTRUST_OK && err != ACTRUST_OK) {
        first_err = err;
    }

    ACTRUST_FREE(q->buffer);
    memset(q, 0, sizeof(*q));
    ACTRUST_FREE(q);
    *queue = NULL;

    return first_err;
}

actrust_err_t actrust_queue_push(actrust_queue_t queue, const void *item,
                                 uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = actrust_sem_wait(queue->empty_slots, timeout_ms);
    if (err != ACTRUST_OK) {
        return QUEUE_ERR(ACTRUST_ERR_QUEUE_FULL);
    }

    err = actrust_mutex_lock(queue->lock);
    if (err != ACTRUST_OK) {
        (void) actrust_sem_post(queue->empty_slots);
        return err;
    }

    memcpy(queue->buffer + (queue->tail * queue->item_size), item,
           queue->item_size);
    queue->tail = (queue->tail + 1u) % queue->capacity;
    queue->size++;

    (void) actrust_mutex_unlock(queue->lock);
    (void) actrust_sem_post(queue->filled_slots);

    return ACTRUST_OK;
}

actrust_err_t actrust_queue_pop(actrust_queue_t queue, void *out_item,
                                uint32_t timeout_ms)
{
    if (queue == NULL || out_item == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = actrust_sem_wait(queue->filled_slots, timeout_ms);
    if (err != ACTRUST_OK) {
        return QUEUE_ERR(ACTRUST_ERR_NO_RESOURCE);
    }

    err = actrust_mutex_lock(queue->lock);
    if (err != ACTRUST_OK) {
        (void) actrust_sem_post(queue->filled_slots);
        return err;
    }

    memcpy(out_item, queue->buffer + (queue->head * queue->item_size),
           queue->item_size);
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->size--;

    (void) actrust_mutex_unlock(queue->lock);
    (void) actrust_sem_post(queue->empty_slots);

    return ACTRUST_OK;
}

actrust_err_t actrust_queue_size(actrust_queue_t queue, size_t *out_size)
{
    if (queue == NULL || out_size == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = actrust_mutex_lock(queue->lock);
    if (err != ACTRUST_OK) {
        return err;
    }

    *out_size = queue->size;

    (void) actrust_mutex_unlock(queue->lock);

    return ACTRUST_OK;
}
