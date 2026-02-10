/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Async reader thread for pre-fetching audio frames
 * The reader thread reads frames from the source in the background,
 * allowing the main thread to decode without waiting for I/O.
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

#ifndef LIBDSDPIPE_READER_THREAD_H
#define LIBDSDPIPE_READER_THREAD_H

#include "dsdpipe_internal.h"
#include "frame_queue.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque reader thread type
 */
typedef struct dsdpipe_reader_thread_s dsdpipe_reader_thread_t;

/**
 * @brief Create a reader thread
 *
 * The reader thread will read frames from the pipe's source and push them
 * to the output queue. It starts in a stopped state.
 *
 * @param pipe Pipeline to read from
 * @param output_queue Queue to push frames to
 * @return New reader thread, or NULL on error
 */
dsdpipe_reader_thread_t *dsdpipe_reader_thread_create(
    dsdpipe_t *pipe,
    dsdpipe_frame_queue_t *output_queue);

/**
 * @brief Start reading a track
 *
 * Seeks to the beginning of the specified track and starts reading frames.
 * The thread will read until end-of-track, cancellation, or error.
 *
 * @param reader Reader thread
 * @param track_number Track number to read (1-based)
 * @return 0 on success, -1 on error
 */
int dsdpipe_reader_thread_start_track(dsdpipe_reader_thread_t *reader,
                                        uint8_t track_number);

/**
 * @brief Wait for the reader to finish the current track
 *
 * Blocks until the reader has finished reading all frames for the track
 * or an error/cancellation occurs.
 *
 * @param reader Reader thread
 */
void dsdpipe_reader_thread_wait(dsdpipe_reader_thread_t *reader);

/**
 * @brief Cancel reading (non-blocking)
 *
 * Signals the reader to stop and wake up from any blocking operations.
 *
 * @param reader Reader thread
 */
void dsdpipe_reader_thread_cancel(dsdpipe_reader_thread_t *reader);

/**
 * @brief Check if the reader encountered an error
 *
 * @param reader Reader thread
 * @return true if an error occurred
 */
bool dsdpipe_reader_thread_has_error(dsdpipe_reader_thread_t *reader);

/**
 * @brief Get the last error from the reader
 *
 * @param reader Reader thread
 * @return Error code (DSDPIPE_OK if no error)
 */
int dsdpipe_reader_thread_get_error(dsdpipe_reader_thread_t *reader);

/**
 * @brief Stop and destroy the reader thread
 *
 * Cancels any pending read and waits for the thread to exit.
 *
 * @param reader Reader thread to destroy (may be NULL)
 */
void dsdpipe_reader_thread_destroy(dsdpipe_reader_thread_t *reader);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDPIPE_READER_THREAD_H */
