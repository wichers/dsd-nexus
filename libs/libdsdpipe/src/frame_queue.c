/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Thread-safe frame queue implementation
 * Uses a bounded circular buffer with mutex and condition variables
 * for synchronization between reader and decoder threads.
 *
 * DSD-Nexus is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * DSD-Nexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with DSD-Nexus; if not, see <https://www.gnu.org/licenses/>.
 */


#include "frame_queue.h"
#include <libsautil/mem.h>

#include <stdlib.h>
#include <string.h>
#include <threads.h>

/*============================================================================
 * Frame Queue Structure
 *============================================================================*/

struct dsdpipe_frame_queue_s {
    /* Circular buffer of frame pointers */
    dsdpipe_buffer_t **frames;
    size_t capacity;
    size_t head;          /**< Next slot to write (producer) */
    size_t tail;          /**< Next slot to read (consumer) */
    size_t count;         /**< Current number of frames */

    /* Track completion flags */
    bool *is_last_frame;  /**< Parallel array: true if frame is last of track */

    /* Synchronization */
    mtx_t mutex;
    cnd_t not_full;    /**< Signaled when queue is not full */
    cnd_t not_empty;   /**< Signaled when queue is not empty */

    /* State flags */
    bool eof;             /**< End-of-file signaled */
    bool cancelled;       /**< Queue cancelled */
};

/*============================================================================
 * Public API
 *============================================================================*/

dsdpipe_frame_queue_t *dsdpipe_frame_queue_create(size_t capacity)
{
    dsdpipe_frame_queue_t *queue;

    if (capacity == 0) {
        return NULL;
    }

    queue = (dsdpipe_frame_queue_t *)sa_calloc(1, sizeof(*queue));
    if (!queue) {
        return NULL;
    }

    queue->frames = (dsdpipe_buffer_t **)sa_calloc(capacity, sizeof(dsdpipe_buffer_t *));
    if (!queue->frames) {
        sa_free(queue);
        return NULL;
    }

    queue->is_last_frame = (bool *)sa_calloc(capacity, sizeof(bool));
    if (!queue->is_last_frame) {
        sa_free(queue->frames);
        sa_free(queue);
        return NULL;
    }

    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->eof = false;
    queue->cancelled = false;

    if (mtx_init(&queue->mutex, mtx_plain) != thrd_success) {
        sa_free(queue->is_last_frame);
        sa_free(queue->frames);
        sa_free(queue);
        return NULL;
    }

    if (cnd_init(&queue->not_full) != thrd_success) {
        mtx_destroy(&queue->mutex);
        sa_free(queue->is_last_frame);
        sa_free(queue->frames);
        sa_free(queue);
        return NULL;
    }

    if (cnd_init(&queue->not_empty) != thrd_success) {
        cnd_destroy(&queue->not_full);
        mtx_destroy(&queue->mutex);
        sa_free(queue->is_last_frame);
        sa_free(queue->frames);
        sa_free(queue);
        return NULL;
    }

    return queue;
}

void dsdpipe_frame_queue_destroy(dsdpipe_frame_queue_t *queue)
{
    if (!queue) {
        return;
    }

    /* Drain any remaining frames */
    mtx_lock(&queue->mutex);
    while (queue->count > 0) {
        dsdpipe_buffer_t *frame = queue->frames[queue->tail];
        queue->tail = (queue->tail + 1) % queue->capacity;
        queue->count--;
        if (frame) {
            dsdpipe_buffer_unref(frame);
        }
    }
    mtx_unlock(&queue->mutex);

    cnd_destroy(&queue->not_empty);
    cnd_destroy(&queue->not_full);
    mtx_destroy(&queue->mutex);
    sa_free(queue->is_last_frame);
    sa_free(queue->frames);
    sa_free(queue);
}

int dsdpipe_frame_queue_push(dsdpipe_frame_queue_t *queue,
                               dsdpipe_buffer_t *frame,
                               bool is_last)
{
    if (!queue || !frame) {
        return -1;
    }

    mtx_lock(&queue->mutex);

    /* Wait while queue is full and not cancelled */
    while (queue->count >= queue->capacity && !queue->cancelled) {
        cnd_wait(&queue->not_full, &queue->mutex);
    }

    if (queue->cancelled) {
        mtx_unlock(&queue->mutex);
        return -1;
    }

    /* Add frame to queue */
    queue->frames[queue->head] = frame;
    queue->is_last_frame[queue->head] = is_last;
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;

    /* Signal that queue is not empty */
    cnd_signal(&queue->not_empty);

    mtx_unlock(&queue->mutex);
    return 0;
}

int dsdpipe_frame_queue_pop_batch(dsdpipe_frame_queue_t *queue,
                                    dsdpipe_buffer_t **frames,
                                    size_t max_count,
                                    size_t *actual_count,
                                    bool *track_complete)
{
    size_t popped = 0;
    bool got_last = false;

    if (!queue || !frames || !actual_count || !track_complete || max_count == 0) {
        return -1;
    }

    *actual_count = 0;
    *track_complete = false;

    mtx_lock(&queue->mutex);

    /* Wait while queue is empty and not cancelled/eof */
    while (queue->count == 0 && !queue->cancelled && !queue->eof) {
        cnd_wait(&queue->not_empty, &queue->mutex);
    }

    if (queue->cancelled) {
        mtx_unlock(&queue->mutex);
        return -1;
    }

    /* Pop as many frames as available up to max_count */
    while (popped < max_count && queue->count > 0) {
        frames[popped] = queue->frames[queue->tail];
        if (queue->is_last_frame[queue->tail]) {
            got_last = true;
        }
        queue->frames[queue->tail] = NULL;
        queue->is_last_frame[queue->tail] = false;
        queue->tail = (queue->tail + 1) % queue->capacity;
        queue->count--;
        popped++;

        /* Stop after receiving the last frame */
        if (got_last) {
            break;
        }
    }

    /* Signal that queue is not full */
    if (popped > 0) {
        cnd_signal(&queue->not_full);
    }

    mtx_unlock(&queue->mutex);

    *actual_count = popped;
    *track_complete = got_last;

    return 0;
}

void dsdpipe_frame_queue_signal_eof(dsdpipe_frame_queue_t *queue)
{
    if (!queue) {
        return;
    }

    mtx_lock(&queue->mutex);
    queue->eof = true;
    cnd_broadcast(&queue->not_empty);
    mtx_unlock(&queue->mutex);
}

bool dsdpipe_frame_queue_is_eof(dsdpipe_frame_queue_t *queue)
{
    bool eof;

    if (!queue) {
        return true;
    }

    mtx_lock(&queue->mutex);
    eof = queue->eof;
    mtx_unlock(&queue->mutex);

    return eof;
}

void dsdpipe_frame_queue_cancel(dsdpipe_frame_queue_t *queue)
{
    if (!queue) {
        return;
    }

    mtx_lock(&queue->mutex);
    queue->cancelled = true;
    cnd_broadcast(&queue->not_full);
    cnd_broadcast(&queue->not_empty);
    mtx_unlock(&queue->mutex);
}

bool dsdpipe_frame_queue_is_cancelled(dsdpipe_frame_queue_t *queue)
{
    bool cancelled;

    if (!queue) {
        return true;
    }

    mtx_lock(&queue->mutex);
    cancelled = queue->cancelled;
    mtx_unlock(&queue->mutex);

    return cancelled;
}

void dsdpipe_frame_queue_reset(dsdpipe_frame_queue_t *queue)
{
    if (!queue) {
        return;
    }

    mtx_lock(&queue->mutex);

    /* Drain any remaining frames */
    while (queue->count > 0) {
        dsdpipe_buffer_t *frame = queue->frames[queue->tail];
        queue->frames[queue->tail] = NULL;
        queue->is_last_frame[queue->tail] = false;
        queue->tail = (queue->tail + 1) % queue->capacity;
        queue->count--;
        if (frame) {
            dsdpipe_buffer_unref(frame);
        }
    }

    /* Reset state */
    queue->head = 0;
    queue->tail = 0;
    queue->eof = false;
    queue->cancelled = false;

    mtx_unlock(&queue->mutex);
}

size_t dsdpipe_frame_queue_size(dsdpipe_frame_queue_t *queue)
{
    size_t count;

    if (!queue) {
        return 0;
    }

    mtx_lock(&queue->mutex);
    count = queue->count;
    mtx_unlock(&queue->mutex);

    return count;
}
