// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file queue.c
 * @brief Bounded FIFO queue backed by a mutex + semaphore pair, used by
 *        the cloud and core layers to hand off fixed-size messages between
 *        producer and consumer tasks.
 */

/* C standard */
#include <stdbool.h>
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

#define QUEUE_WAIT_SLICE_MS 10u

typedef enum {
    ACTRUST_QUEUE_STATE_OPEN = 0,
    ACTRUST_QUEUE_STATE_CLOSING,
    ACTRUST_QUEUE_STATE_CLOSED,
} actrust_queue_state_t;

struct actrust_queue {
    uint8_t              *buffer;
    size_t                capacity;
    size_t                item_size;
    size_t                head;
    size_t                tail;
    size_t                size;
    actrust_mutex_t       lock;
    actrust_mutex_t       state_lock;
    actrust_sem_t         empty_slots;
    actrust_sem_t         filled_slots;
    actrust_queue_state_t state;
    size_t                in_flight;
    size_t                waiting_push;
    size_t                waiting_pop;
};

static actrust_err_t queue_lock(actrust_queue_t queue)
{
    if (queue->lock == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_BAD_STATE);
    }
    return actrust_mutex_lock(queue->lock);
}

static actrust_err_t queue_state_lock(actrust_queue_t queue)
{
    if (queue->state_lock == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_BAD_STATE);
    }
    return actrust_mutex_lock(queue->state_lock);
}

static void queue_operation_leave_locked(actrust_queue_t queue)
{
    if (queue->in_flight > 0u) {
        queue->in_flight--;
    }
}

static uint32_t queue_wait_slice(uint32_t timeout_ms, uint64_t deadline_ms)
{
    if (timeout_ms == UINT32_MAX) {
        return QUEUE_WAIT_SLICE_MS;
    }

    uint64_t now = actrust_monotonic_ms();
    if (now >= deadline_ms) {
        return 0u;
    }

    uint64_t remaining = deadline_ms - now;
    return (remaining > QUEUE_WAIT_SLICE_MS) ? QUEUE_WAIT_SLICE_MS
                                             : (uint32_t) remaining;
}

static actrust_err_t queue_wait_resource(actrust_queue_t queue,
                                         actrust_sem_t   semaphore,
                                         uint32_t timeout_ms, bool push)
{
    uint64_t deadline_ms = 0u;
    if (timeout_ms == 0u) {
        actrust_err_t err      = actrust_sem_wait(semaphore, 0u);
        actrust_err_t lock_err = queue_state_lock(queue);
        if (lock_err != ACTRUST_OK) {
            if (err == ACTRUST_OK) {
                (void) actrust_sem_post(semaphore);
            }
            return lock_err;
        }
        if (push && queue->waiting_push > 0u) {
            queue->waiting_push--;
        } else if (!push && queue->waiting_pop > 0u) {
            queue->waiting_pop--;
        }
        if (queue->state != ACTRUST_QUEUE_STATE_OPEN) {
            if (err == ACTRUST_OK) {
                (void) actrust_sem_post(semaphore);
            }
            queue_operation_leave_locked(queue);
            (void) actrust_mutex_unlock(queue->state_lock);
            return QUEUE_ERR(ACTRUST_ERR_BAD_STATE);
        }
        (void) actrust_mutex_unlock(queue->state_lock);
        return err;
    }
    if (timeout_ms != UINT32_MAX) {
        uint64_t now = actrust_monotonic_ms();
        deadline_ms  = now + timeout_ms;
        if (deadline_ms < now) {
            deadline_ms = UINT64_MAX;
        }
    }

    for (;;) {
        uint32_t wait_ms = queue_wait_slice(timeout_ms, deadline_ms);
        if (timeout_ms != UINT32_MAX && timeout_ms != 0u && wait_ms == 0u) {
            actrust_err_t lock_err = queue_state_lock(queue);
            if (lock_err != ACTRUST_OK) {
                return lock_err;
            }
            if (push && queue->waiting_push > 0u) {
                queue->waiting_push--;
            } else if (!push && queue->waiting_pop > 0u) {
                queue->waiting_pop--;
            }
            (void) actrust_mutex_unlock(queue->state_lock);
            return ACTRUST_ERR(ACTRUST_ERR_MODULE_ADAPTER_SYSTEM,
                               ACTRUST_ERR_TIMEOUT);
        }

        actrust_err_t err = actrust_sem_wait(semaphore, wait_ms);

        actrust_err_t lock_err = queue_state_lock(queue);
        if (lock_err != ACTRUST_OK) {
            return lock_err;
        }

        if (queue->state != ACTRUST_QUEUE_STATE_OPEN) {
            if (push && queue->waiting_push > 0u) {
                queue->waiting_push--;
            } else if (!push && queue->waiting_pop > 0u) {
                queue->waiting_pop--;
            }
            queue_operation_leave_locked(queue);
            (void) actrust_mutex_unlock(queue->state_lock);
            return QUEUE_ERR(ACTRUST_ERR_BAD_STATE);
        }

        if (err == ACTRUST_OK || timeout_ms == 0u ||
            (ACTRUST_ERR_CODE(err) != ACTRUST_ERR_TIMEOUT &&
             ACTRUST_ERR_CODE(err) != ACTRUST_ERR_WOULD_BLOCK)) {
            if (push && queue->waiting_push > 0u) {
                queue->waiting_push--;
            } else if (!push && queue->waiting_pop > 0u) {
                queue->waiting_pop--;
            }
            (void) actrust_mutex_unlock(queue->state_lock);
            return err;
        }

        (void) actrust_mutex_unlock(queue->state_lock);
    }
}

static actrust_err_t queue_wait_quiescent(actrust_queue_t queue)
{
    for (;;) {
        actrust_err_t err = queue_state_lock(queue);
        if (err != ACTRUST_OK) {
            return err;
        }

        bool quiescent = queue->in_flight == 0u && queue->waiting_push == 0u &&
                         queue->waiting_pop == 0u;
        (void) actrust_mutex_unlock(queue->state_lock);

        if (quiescent) {
            return ACTRUST_OK;
        }
        actrust_sleep_ms(QUEUE_WAIT_SLICE_MS);
    }
}

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
    queue->state     = ACTRUST_QUEUE_STATE_OPEN;

    actrust_err_t err = actrust_mutex_create(&queue->lock);
    if (err != ACTRUST_OK) {
        ACTRUST_FREE(queue->buffer);
        ACTRUST_FREE(queue);
        return err;
    }

    err = actrust_mutex_create(&queue->state_lock);
    if (err != ACTRUST_OK) {
        (void) actrust_mutex_destroy(queue->lock);
        ACTRUST_FREE(queue->buffer);
        ACTRUST_FREE(queue);
        return err;
    }

    err = actrust_sem_create(&queue->empty_slots, (uint32_t) capacity);
    if (err != ACTRUST_OK) {
        (void) actrust_mutex_destroy(queue->state_lock);
        (void) actrust_mutex_destroy(queue->lock);
        ACTRUST_FREE(queue->buffer);
        ACTRUST_FREE(queue);
        return err;
    }

    err = actrust_sem_create(&queue->filled_slots, 0u);
    if (err != ACTRUST_OK) {
        (void) actrust_sem_destroy(queue->empty_slots);
        (void) actrust_mutex_destroy(queue->state_lock);
        (void) actrust_mutex_destroy(queue->lock);
        ACTRUST_FREE(queue->buffer);
        ACTRUST_FREE(queue);
        return err;
    }

    *out_queue = queue;
    return ACTRUST_OK;
}

actrust_err_t actrust_queue_close(actrust_queue_t queue)
{
    if (queue == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = queue_state_lock(queue);
    if (err != ACTRUST_OK) {
        return err;
    }

    if (queue->state == ACTRUST_QUEUE_STATE_OPEN) {
        queue->state = ACTRUST_QUEUE_STATE_CLOSING;
    } else if (queue->state == ACTRUST_QUEUE_STATE_CLOSED) {
        (void) actrust_mutex_unlock(queue->state_lock);
        return ACTRUST_OK;
    }

    (void) actrust_mutex_unlock(queue->state_lock);
    return queue_wait_quiescent(queue);
}

actrust_err_t actrust_queue_destroy(actrust_queue_t *queue)
{
    if (queue == NULL || *queue == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_queue_t q         = *queue;
    actrust_err_t   first_err = ACTRUST_OK;

    if (q->state_lock != NULL) {
        first_err = actrust_queue_close(q);
        if (first_err != ACTRUST_OK) {
            return first_err;
        }
    }

    if (q->empty_slots != NULL) {
        actrust_err_t err = actrust_sem_destroy(q->empty_slots);
        if (err != ACTRUST_OK) {
            first_err = err;
        } else {
            q->empty_slots = NULL;
        }
    }

    if (q->filled_slots != NULL) {
        actrust_err_t err = actrust_sem_destroy(q->filled_slots);
        if (err != ACTRUST_OK) {
            if (first_err == ACTRUST_OK) {
                first_err = err;
            }
        } else {
            q->filled_slots = NULL;
        }
    }

    if (q->state_lock != NULL) {
        actrust_err_t err = actrust_mutex_lock(q->state_lock);
        if (err == ACTRUST_OK) {
            q->state = ACTRUST_QUEUE_STATE_CLOSED;
            (void) actrust_mutex_unlock(q->state_lock);
            err = actrust_mutex_destroy(q->state_lock);
        }
        if (err != ACTRUST_OK) {
            if (first_err == ACTRUST_OK) {
                first_err = err;
            }
        } else {
            q->state_lock = NULL;
        }
    }

    if (q->lock != NULL) {
        actrust_err_t err = actrust_mutex_destroy(q->lock);
        if (err != ACTRUST_OK) {
            if (first_err == ACTRUST_OK) {
                first_err = err;
            }
        } else {
            q->lock = NULL;
        }
    }

    if (first_err != ACTRUST_OK || q->empty_slots != NULL ||
        q->filled_slots != NULL || q->state_lock != NULL || q->lock != NULL) {
        return first_err == ACTRUST_OK ? QUEUE_ERR(ACTRUST_ERR_BUSY)
                                       : first_err;
    }

    ACTRUST_FREE(q->buffer);
    memset(q, 0, sizeof(*q));
    ACTRUST_FREE(q);
    *queue = NULL;

    return ACTRUST_OK;
}

actrust_err_t actrust_queue_push(actrust_queue_t queue, const void *item,
                                 uint32_t timeout_ms)
{
    if (queue == NULL || item == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = queue_state_lock(queue);
    if (err != ACTRUST_OK) {
        return err;
    }
    if (queue->state != ACTRUST_QUEUE_STATE_OPEN) {
        (void) actrust_mutex_unlock(queue->state_lock);
        return QUEUE_ERR(ACTRUST_ERR_BAD_STATE);
    }
    queue->in_flight++;
    queue->waiting_push++;
    (void) actrust_mutex_unlock(queue->state_lock);

    actrust_err_t lock_err;
    err = queue_wait_resource(queue, queue->empty_slots, timeout_ms, true);
    if (err != ACTRUST_OK) {
        if (ACTRUST_ERR_MODULE(err) == ACTRUST_ERR_MODULE_COMPONENTS_QUEUE &&
            ACTRUST_ERR_CODE(err) == ACTRUST_ERR_BAD_STATE) {
            return err;
        }

        lock_err = queue_state_lock(queue);
        if (lock_err != ACTRUST_OK) {
            return lock_err;
        }
        queue_operation_leave_locked(queue);
        (void) actrust_mutex_unlock(queue->state_lock);
        if (ACTRUST_ERR_MODULE(err) == ACTRUST_ERR_MODULE_ADAPTER_SYSTEM &&
            ACTRUST_ERR_CODE(err) == ACTRUST_ERR_WOULD_BLOCK) {
            return QUEUE_ERR(ACTRUST_ERR_QUEUE_FULL);
        }
        return err;
    }

    lock_err = queue_lock(queue);
    if (lock_err != ACTRUST_OK) {
        actrust_err_t state_err = queue_state_lock(queue);
        if (state_err == ACTRUST_OK) {
            queue_operation_leave_locked(queue);
            (void) actrust_mutex_unlock(queue->state_lock);
        }
        (void) actrust_sem_post(queue->empty_slots);
        return lock_err;
    }
    lock_err = queue_state_lock(queue);
    if (lock_err != ACTRUST_OK) {
        (void) actrust_sem_post(queue->empty_slots);
        (void) actrust_mutex_unlock(queue->lock);
        return lock_err;
    }
    if (queue->state != ACTRUST_QUEUE_STATE_OPEN) {
        (void) actrust_sem_post(queue->empty_slots);
        queue_operation_leave_locked(queue);
        (void) actrust_mutex_unlock(queue->state_lock);
        (void) actrust_mutex_unlock(queue->lock);
        return QUEUE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    /* Publish while holding the lock so post failure remains rollback-safe. */
    err = actrust_sem_post(queue->filled_slots);
    if (err != ACTRUST_OK) {
        (void) actrust_sem_post(queue->empty_slots);
        queue_operation_leave_locked(queue);
        (void) actrust_mutex_unlock(queue->state_lock);
        (void) actrust_mutex_unlock(queue->lock);
        return err;
    }

    memcpy(queue->buffer + (queue->tail * queue->item_size), item,
           queue->item_size);
    queue->tail = (queue->tail + 1u) % queue->capacity;
    queue->size++;
    queue_operation_leave_locked(queue);
    (void) actrust_mutex_unlock(queue->state_lock);

    (void) actrust_mutex_unlock(queue->lock);
    return ACTRUST_OK;
}

actrust_err_t actrust_queue_pop(actrust_queue_t queue, void *out_item,
                                uint32_t timeout_ms)
{
    if (queue == NULL || out_item == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = queue_state_lock(queue);
    if (err != ACTRUST_OK) {
        return err;
    }
    if (queue->state != ACTRUST_QUEUE_STATE_OPEN) {
        (void) actrust_mutex_unlock(queue->state_lock);
        return QUEUE_ERR(ACTRUST_ERR_BAD_STATE);
    }
    queue->in_flight++;
    queue->waiting_pop++;
    (void) actrust_mutex_unlock(queue->state_lock);

    actrust_err_t lock_err;
    err = queue_wait_resource(queue, queue->filled_slots, timeout_ms, false);
    if (err != ACTRUST_OK) {
        if (ACTRUST_ERR_MODULE(err) == ACTRUST_ERR_MODULE_COMPONENTS_QUEUE &&
            ACTRUST_ERR_CODE(err) == ACTRUST_ERR_BAD_STATE) {
            return err;
        }

        lock_err = queue_state_lock(queue);
        if (lock_err != ACTRUST_OK) {
            return lock_err;
        }
        queue_operation_leave_locked(queue);
        (void) actrust_mutex_unlock(queue->state_lock);
        if (ACTRUST_ERR_MODULE(err) == ACTRUST_ERR_MODULE_ADAPTER_SYSTEM &&
            ACTRUST_ERR_CODE(err) == ACTRUST_ERR_WOULD_BLOCK) {
            return QUEUE_ERR(ACTRUST_ERR_NO_RESOURCE);
        }
        return err;
    }

    lock_err = queue_lock(queue);
    if (lock_err != ACTRUST_OK) {
        actrust_err_t state_err = queue_state_lock(queue);
        if (state_err == ACTRUST_OK) {
            queue_operation_leave_locked(queue);
            (void) actrust_mutex_unlock(queue->state_lock);
        }
        (void) actrust_sem_post(queue->filled_slots);
        return lock_err;
    }
    lock_err = queue_state_lock(queue);
    if (lock_err != ACTRUST_OK) {
        (void) actrust_sem_post(queue->filled_slots);
        (void) actrust_mutex_unlock(queue->lock);
        return lock_err;
    }
    if (queue->state != ACTRUST_QUEUE_STATE_OPEN) {
        (void) actrust_sem_post(queue->filled_slots);
        queue_operation_leave_locked(queue);
        (void) actrust_mutex_unlock(queue->state_lock);
        (void) actrust_mutex_unlock(queue->lock);
        return QUEUE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    /* Reserve capacity while holding the lock so post failure is recoverable.
     */
    err = actrust_sem_post(queue->empty_slots);
    if (err != ACTRUST_OK) {
        (void) actrust_sem_post(queue->filled_slots);
        queue_operation_leave_locked(queue);
        (void) actrust_mutex_unlock(queue->state_lock);
        (void) actrust_mutex_unlock(queue->lock);
        return err;
    }

    memcpy(out_item, queue->buffer + (queue->head * queue->item_size),
           queue->item_size);
    queue->head = (queue->head + 1u) % queue->capacity;
    queue->size--;
    queue_operation_leave_locked(queue);
    (void) actrust_mutex_unlock(queue->state_lock);

    (void) actrust_mutex_unlock(queue->lock);
    return ACTRUST_OK;
}

actrust_err_t actrust_queue_size(actrust_queue_t queue, size_t *out_size)
{
    if (queue == NULL || out_size == NULL) {
        return QUEUE_ERR(ACTRUST_ERR_INVALID_ARG);
    }

    actrust_err_t err = queue_state_lock(queue);
    if (err != ACTRUST_OK) {
        return err;
    }
    if (queue->state != ACTRUST_QUEUE_STATE_OPEN) {
        (void) actrust_mutex_unlock(queue->state_lock);
        return QUEUE_ERR(ACTRUST_ERR_BAD_STATE);
    }

    queue->in_flight++;
    *out_size = queue->size;
    queue_operation_leave_locked(queue);

    return actrust_mutex_unlock(queue->state_lock);
}
