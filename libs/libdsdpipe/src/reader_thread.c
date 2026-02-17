/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Async reader thread implementation
 * Runs in a separate thread, pre-fetching frames from the source
 * to overlap I/O with decode operations.
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


#include "reader_thread.h"
#include <libsautil/mem.h>

#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <libsautil/c11threads.h>
#else
#include <threads.h>
#endif

/*============================================================================
 * Reader Thread Structure
 *============================================================================*/

struct dsdpipe_reader_thread_s {
    /* Pipeline reference */
    dsdpipe_t *pipe;

    /* Output queue */
    dsdpipe_frame_queue_t *output_queue;

    /* Thread management */
    thrd_t thread;
    bool thread_running;

    /* Track state */
    uint8_t current_track;
    bool track_started;

    /* Synchronization for track start/finish */
    mtx_t state_mutex;
    cnd_t track_start_cond;
    cnd_t track_done_cond;
    bool track_pending;       /**< Track is waiting to start */
    bool track_finished;      /**< Track reading is complete */

    /* Error state */
    int last_error;
    bool has_error;

    /* Cancellation flag */
    volatile bool cancelled;
    volatile bool shutdown;
};

/*============================================================================
 * Reader Thread Function
 *============================================================================*/

/**
 * @brief Main reader thread function
 */
static int reader_thread_func(void *arg)
{
    dsdpipe_reader_thread_t *reader = (dsdpipe_reader_thread_t *)arg;
    dsdpipe_t *pipe = reader->pipe;

    while (!reader->shutdown) {
        uint8_t track_number;
        int result;

        /* Wait for a track to be assigned */
        mtx_lock(&reader->state_mutex);
        while (!reader->track_pending && !reader->shutdown) {
            cnd_wait(&reader->track_start_cond, &reader->state_mutex);
        }

        if (reader->shutdown) {
            mtx_unlock(&reader->state_mutex);
            break;
        }

        track_number = reader->current_track;
        reader->track_pending = false;
        reader->track_finished = false;
        reader->has_error = false;
        reader->last_error = DSDPIPE_OK;
        mtx_unlock(&reader->state_mutex);

        /* Reset queue for new track */
        dsdpipe_frame_queue_reset(reader->output_queue);

        /* Seek to track start */
        result = pipe->source.ops->seek_track(pipe->source.ctx, track_number);
        if (result != DSDPIPE_OK) {
            mtx_lock(&reader->state_mutex);
            reader->has_error = true;
            reader->last_error = result;
            reader->track_finished = true;
            cnd_signal(&reader->track_done_cond);
            mtx_unlock(&reader->state_mutex);
            dsdpipe_frame_queue_signal_eof(reader->output_queue);
            continue;
        }

        /* Read frames until end-of-track, cancellation, or error */
        while (!reader->cancelled && !reader->shutdown) {
            dsdpipe_buffer_t *buffer;
            bool is_last_frame;

            /* Allocate buffer from pool */
            buffer = dsdpipe_buffer_alloc_dsd(pipe);
            if (!buffer) {
                /* Buffer pool exhausted - wait and retry */
                /* This shouldn't happen if pool limit is set correctly */
                mtx_lock(&reader->state_mutex);
                reader->has_error = true;
                reader->last_error = DSDPIPE_ERROR_OUT_OF_MEMORY;
                reader->track_finished = true;
                cnd_signal(&reader->track_done_cond);
                mtx_unlock(&reader->state_mutex);
                dsdpipe_frame_queue_signal_eof(reader->output_queue);
                break;
            }

            /* Read frame from source */
            result = pipe->source.ops->read_frame(pipe->source.ctx, buffer);

            if (result != DSDPIPE_OK && result != 1) {
                /* Read error */
                dsdpipe_buffer_unref(buffer);
                mtx_lock(&reader->state_mutex);
                reader->has_error = true;
                reader->last_error = (result < 0) ? result : DSDPIPE_ERROR_READ;
                reader->track_finished = true;
                cnd_signal(&reader->track_done_cond);
                mtx_unlock(&reader->state_mutex);
                dsdpipe_frame_queue_signal_eof(reader->output_queue);
                break;
            }

            /* Check for end-of-track */
            is_last_frame = (result == 1) ||
                            (buffer->flags & DSDPIPE_BUF_FLAG_TRACK_END);

            /* Push to queue (blocks if queue is full) */
            if (dsdpipe_frame_queue_push(reader->output_queue, buffer, is_last_frame) != 0) {
                /* Queue cancelled or error */
                dsdpipe_buffer_unref(buffer);
                mtx_lock(&reader->state_mutex);
                reader->track_finished = true;
                cnd_signal(&reader->track_done_cond);
                mtx_unlock(&reader->state_mutex);
                break;
            }

            if (is_last_frame) {
                /* Track complete */
                mtx_lock(&reader->state_mutex);
                reader->track_finished = true;
                cnd_signal(&reader->track_done_cond);
                mtx_unlock(&reader->state_mutex);
                dsdpipe_frame_queue_signal_eof(reader->output_queue);
                break;
            }
        }

        /* Handle cancellation */
        if (reader->cancelled || reader->shutdown) {
            mtx_lock(&reader->state_mutex);
            reader->track_finished = true;
            cnd_signal(&reader->track_done_cond);
            mtx_unlock(&reader->state_mutex);
            dsdpipe_frame_queue_cancel(reader->output_queue);
        }
    }

    return 0;
}

/*============================================================================
 * Public API
 *============================================================================*/

dsdpipe_reader_thread_t *dsdpipe_reader_thread_create(
    dsdpipe_t *pipe,
    dsdpipe_frame_queue_t *output_queue)
{
    dsdpipe_reader_thread_t *reader;

    if (!pipe || !output_queue) {
        return NULL;
    }

    reader = (dsdpipe_reader_thread_t *)sa_calloc(1, sizeof(*reader));
    if (!reader) {
        return NULL;
    }

    reader->pipe = pipe;
    reader->output_queue = output_queue;
    reader->thread_running = false;
    reader->current_track = 0;
    reader->track_started = false;
    reader->track_pending = false;
    reader->track_finished = false;
    reader->last_error = DSDPIPE_OK;
    reader->has_error = false;
    reader->cancelled = false;
    reader->shutdown = false;

    if (mtx_init(&reader->state_mutex, mtx_plain) != thrd_success) {
        sa_free(reader);
        return NULL;
    }

    if (cnd_init(&reader->track_start_cond) != thrd_success) {
        mtx_destroy(&reader->state_mutex);
        sa_free(reader);
        return NULL;
    }

    if (cnd_init(&reader->track_done_cond) != thrd_success) {
        cnd_destroy(&reader->track_start_cond);
        mtx_destroy(&reader->state_mutex);
        sa_free(reader);
        return NULL;
    }

    /* Start the reader thread */
    if (thrd_create(&reader->thread, reader_thread_func, reader) != thrd_success) {
        cnd_destroy(&reader->track_done_cond);
        cnd_destroy(&reader->track_start_cond);
        mtx_destroy(&reader->state_mutex);
        sa_free(reader);
        return NULL;
    }

    reader->thread_running = true;

    return reader;
}

int dsdpipe_reader_thread_start_track(dsdpipe_reader_thread_t *reader,
                                        uint8_t track_number)
{
    if (!reader) {
        return -1;
    }

    mtx_lock(&reader->state_mutex);

    /* Wait for any previous track to finish */
    while (!reader->track_finished && reader->track_started && !reader->shutdown) {
        cnd_wait(&reader->track_done_cond, &reader->state_mutex);
    }

    if (reader->shutdown) {
        mtx_unlock(&reader->state_mutex);
        return -1;
    }

    /* Reset state */
    reader->cancelled = false;
    reader->current_track = track_number;
    reader->track_pending = true;
    reader->track_started = true;
    reader->track_finished = false;

    /* Signal thread to start */
    cnd_signal(&reader->track_start_cond);

    mtx_unlock(&reader->state_mutex);

    return 0;
}

void dsdpipe_reader_thread_wait(dsdpipe_reader_thread_t *reader)
{
    if (!reader) {
        return;
    }

    mtx_lock(&reader->state_mutex);
    while (!reader->track_finished && reader->track_started && !reader->shutdown) {
        cnd_wait(&reader->track_done_cond, &reader->state_mutex);
    }
    mtx_unlock(&reader->state_mutex);
}

void dsdpipe_reader_thread_cancel(dsdpipe_reader_thread_t *reader)
{
    if (!reader) {
        return;
    }

    reader->cancelled = true;
    dsdpipe_frame_queue_cancel(reader->output_queue);
}

bool dsdpipe_reader_thread_has_error(dsdpipe_reader_thread_t *reader)
{
    bool has_error;

    if (!reader) {
        return false;
    }

    mtx_lock(&reader->state_mutex);
    has_error = reader->has_error;
    mtx_unlock(&reader->state_mutex);

    return has_error;
}

int dsdpipe_reader_thread_get_error(dsdpipe_reader_thread_t *reader)
{
    int error;

    if (!reader) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    mtx_lock(&reader->state_mutex);
    error = reader->last_error;
    mtx_unlock(&reader->state_mutex);

    return error;
}

void dsdpipe_reader_thread_destroy(dsdpipe_reader_thread_t *reader)
{
    if (!reader) {
        return;
    }

    /* Signal shutdown */
    mtx_lock(&reader->state_mutex);
    reader->shutdown = true;
    reader->cancelled = true;
    cnd_broadcast(&reader->track_start_cond);
    cnd_broadcast(&reader->track_done_cond);
    mtx_unlock(&reader->state_mutex);

    /* Cancel queue to unblock any push */
    dsdpipe_frame_queue_cancel(reader->output_queue);

    /* Wait for thread to exit */
    if (reader->thread_running) {
        int result;
        thrd_join(reader->thread, &result);
        reader->thread_running = false;
    }

    cnd_destroy(&reader->track_done_cond);
    cnd_destroy(&reader->track_start_cond);
    mtx_destroy(&reader->state_mutex);
    sa_free(reader);
}
