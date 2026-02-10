/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Thread-safe frame queue for async reader→decoder communication
 * This queue implements a bounded SPSC (single-producer, single-consumer)
 * pattern for passing frames from the reader thread to the main decode thread.
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

#ifndef LIBDSDPIPE_FRAME_QUEUE_H
#define LIBDSDPIPE_FRAME_QUEUE_H

#include "dsdpipe_internal.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque frame queue type
 */
typedef struct dsdpipe_frame_queue_s dsdpipe_frame_queue_t;

/**
 * @brief Create a new frame queue
 *
 * @param capacity Maximum number of frames the queue can hold
 * @return New queue instance, or NULL on error
 */
dsdpipe_frame_queue_t *dsdpipe_frame_queue_create(size_t capacity);

/**
 * @brief Destroy a frame queue
 *
 * @param queue Queue to destroy (may be NULL)
 *
 * @note Any remaining frames in the queue will be unreferenced
 */
void dsdpipe_frame_queue_destroy(dsdpipe_frame_queue_t *queue);

/**
 * @brief Push a frame to the queue (producer/reader thread)
 *
 * Blocks if the queue is full until space is available or the queue is
 * cancelled.
 *
 * @param queue Frame queue
 * @param frame Frame to push (ownership transferred to queue)
 * @param is_last True if this is the last frame of the track
 * @return 0 on success, -1 on error or cancellation
 */
int dsdpipe_frame_queue_push(dsdpipe_frame_queue_t *queue,
                               dsdpipe_buffer_t *frame,
                               bool is_last);

/**
 * @brief Pop a batch of frames from the queue (consumer/main thread)
 *
 * Blocks until at least one frame is available or the queue is cancelled/EOF.
 *
 * @param queue Frame queue
 * @param frames Array to receive frame pointers (ownership transferred to caller)
 * @param max_count Maximum number of frames to pop
 * @param actual_count Receives actual number of frames popped
 * @param track_complete Receives true if this batch includes the last frame
 * @return 0 on success, -1 on error or cancellation
 */
int dsdpipe_frame_queue_pop_batch(dsdpipe_frame_queue_t *queue,
                                    dsdpipe_buffer_t **frames,
                                    size_t max_count,
                                    size_t *actual_count,
                                    bool *track_complete);

/**
 * @brief Signal end-of-file (producer thread)
 *
 * Call this after pushing the last frame to indicate no more frames will come.
 *
 * @param queue Frame queue
 */
void dsdpipe_frame_queue_signal_eof(dsdpipe_frame_queue_t *queue);

/**
 * @brief Check if EOF has been signaled
 *
 * @param queue Frame queue
 * @return true if EOF has been signaled
 */
bool dsdpipe_frame_queue_is_eof(dsdpipe_frame_queue_t *queue);

/**
 * @brief Cancel the queue (wake up blocked threads)
 *
 * After cancellation, all push/pop operations will return -1.
 *
 * @param queue Frame queue
 */
void dsdpipe_frame_queue_cancel(dsdpipe_frame_queue_t *queue);

/**
 * @brief Check if the queue has been cancelled
 *
 * @param queue Frame queue
 * @return true if cancelled
 */
bool dsdpipe_frame_queue_is_cancelled(dsdpipe_frame_queue_t *queue);

/**
 * @brief Reset the queue for reuse
 *
 * Clears EOF and cancellation flags, drains any remaining frames.
 *
 * @param queue Frame queue
 */
void dsdpipe_frame_queue_reset(dsdpipe_frame_queue_t *queue);

/**
 * @brief Get current number of frames in the queue
 *
 * @param queue Frame queue
 * @return Number of frames currently queued
 */
size_t dsdpipe_frame_queue_size(dsdpipe_frame_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDPIPE_FRAME_QUEUE_H */
