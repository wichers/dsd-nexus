/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSF source implementation for libdsdpipe
 * DSF files contain a single track. The libdsf library handles the
 * block-interleaved to byte-interleaved conversion internally,
 * providing data in DSDIFF-compatible format (MSB-first, byte-interleaved).
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


#include "dsdpipe_internal.h"
#include "id3_parser.h"

#include <libdsf/dsf.h>
#include <libsautil/mem.h>

#include <string.h>

/*============================================================================
 * Constants
 *============================================================================*/

/** Frame rate for SACD-compatible output (frames per second) */
#define DSF_SOURCE_FRAME_RATE       75

/*============================================================================
 * DSF Source Context
 *============================================================================*/

typedef struct dsdpipe_source_dsf_ctx_s {
    /* Configuration */
    char *path;                     /**< Path to DSF file */

    /* libdsf handle */
    dsf_t *dsf;                     /**< DSF file handle */

    /* Cached format info */
    dsdpipe_format_t format;       /**< Audio format */
    uint64_t sample_count;          /**< Total samples per channel */
    uint64_t audio_data_size;       /**< Total audio data size in bytes */

    /* Derived info */
    uint64_t bytes_per_frame;       /**< Bytes per DSD frame */
    uint64_t total_frames;          /**< Total frames in file */

    /* Playback state */
    uint8_t current_track;          /**< Current track (always 1 for DSF) */
    uint64_t current_frame;         /**< Current frame number within track */
    uint64_t audio_position;        /**< Current position in audio data (bytes) */

    /* State flags */
    bool is_open;                   /**< Whether source is open */
} dsdpipe_source_dsf_ctx_t;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Calculate frame size for DSD data
 *
 * For SACD-compatible frame rate (75 fps), each frame contains:
 *   samples_per_frame = sample_rate / 75
 *   bytes_per_channel_per_frame = samples_per_frame / 8
 *   bytes_per_frame = bytes_per_channel_per_frame * channel_count
 *
 * For DSD64 stereo: (2822400 / 75) / 8 * 2 = 9408 bytes/frame
 */
static uint64_t calc_bytes_per_frame(uint32_t sample_rate, uint32_t channel_count)
{
    uint64_t samples_per_frame = sample_rate / DSF_SOURCE_FRAME_RATE;
    uint64_t bytes_per_channel = samples_per_frame / 8;
    return bytes_per_channel * channel_count;
}

/*============================================================================
 * Source Operations
 *============================================================================*/

static int dsf_source_open(void *ctx, const char *path)
{
    dsdpipe_source_dsf_ctx_t *dsf_ctx = (dsdpipe_source_dsf_ctx_t *)ctx;
    dsf_file_info_t info;
    int result;

    if (!dsf_ctx || !path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store path */
    dsf_ctx->path = dsdpipe_strdup(path);
    if (!dsf_ctx->path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Allocate DSF handle */
    result = dsf_alloc(&dsf_ctx->dsf);
    if (result != DSF_SUCCESS) {
        sa_free(dsf_ctx->path);
        dsf_ctx->path = NULL;
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Open DSF file for reading */
    result = dsf_open(dsf_ctx->dsf, path);
    if (result != DSF_SUCCESS) {
        dsf_free(dsf_ctx->dsf);
        dsf_ctx->dsf = NULL;
        sa_free(dsf_ctx->path);
        dsf_ctx->path = NULL;
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    /* Get file info */
    result = dsf_get_file_info(dsf_ctx->dsf, &info);
    if (result != DSF_SUCCESS) {
        dsf_close(dsf_ctx->dsf);
        dsf_free(dsf_ctx->dsf);
        dsf_ctx->dsf = NULL;
        sa_free(dsf_ctx->path);
        dsf_ctx->path = NULL;
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    /* Validate format - DSF only supports DSD raw */
    if (info.format_id != DSF_FORMAT_DSD_RAW) {
        dsf_close(dsf_ctx->dsf);
        dsf_free(dsf_ctx->dsf);
        dsf_ctx->dsf = NULL;
        sa_free(dsf_ctx->path);
        dsf_ctx->path = NULL;
        return DSDPIPE_ERROR_UNSUPPORTED;
    }

    /* Fill in format structure */
    dsf_ctx->format.type = DSDPIPE_FORMAT_DSD_RAW;
    dsf_ctx->format.sample_rate = info.sampling_frequency;
    dsf_ctx->format.channel_count = (uint16_t)info.channel_count;
    dsf_ctx->format.bits_per_sample = (uint16_t)info.bits_per_sample;
    dsf_ctx->format.frame_rate = DSF_SOURCE_FRAME_RATE;

    /* Store file info */
    dsf_ctx->sample_count = info.sample_count;
    dsf_ctx->audio_data_size = info.audio_data_size;

    /* Calculate frame info */
    dsf_ctx->bytes_per_frame = calc_bytes_per_frame(info.sampling_frequency,
                                                     info.channel_count);

    /* Calculate total frames */
    if (dsf_ctx->bytes_per_frame > 0) {
        dsf_ctx->total_frames = dsf_ctx->audio_data_size / dsf_ctx->bytes_per_frame;
    } else {
        dsf_ctx->total_frames = 0;
    }

    /* Initialize playback state */
    dsf_ctx->current_track = 0;
    dsf_ctx->current_frame = 0;
    dsf_ctx->audio_position = 0;
    dsf_ctx->is_open = true;

    return DSDPIPE_OK;
}

static void dsf_source_close(void *ctx)
{
    dsdpipe_source_dsf_ctx_t *dsf_ctx = (dsdpipe_source_dsf_ctx_t *)ctx;

    if (!dsf_ctx) {
        return;
    }

    if (dsf_ctx->dsf) {
        dsf_close(dsf_ctx->dsf);
        dsf_free(dsf_ctx->dsf);
        dsf_ctx->dsf = NULL;
    }

    if (dsf_ctx->path) {
        sa_free(dsf_ctx->path);
        dsf_ctx->path = NULL;
    }

    dsf_ctx->is_open = false;
}

static int dsf_source_get_track_count(void *ctx, uint8_t *count)
{
    dsdpipe_source_dsf_ctx_t *dsf_ctx = (dsdpipe_source_dsf_ctx_t *)ctx;

    if (!dsf_ctx || !count) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsf_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* DSF files contain exactly one track */
    *count = 1;
    return DSDPIPE_OK;
}

static int dsf_source_get_format(void *ctx, dsdpipe_format_t *format)
{
    dsdpipe_source_dsf_ctx_t *dsf_ctx = (dsdpipe_source_dsf_ctx_t *)ctx;

    if (!dsf_ctx || !format) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsf_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    *format = dsf_ctx->format;
    return DSDPIPE_OK;
}

static int dsf_source_seek_track(void *ctx, uint8_t track_number)
{
    dsdpipe_source_dsf_ctx_t *dsf_ctx = (dsdpipe_source_dsf_ctx_t *)ctx;
    int result;

    if (!dsf_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsf_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* DSF only has one track */
    if (track_number != 1) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    /* Seek to beginning of audio data */
    result = dsf_seek_to_audio_start(dsf_ctx->dsf);
    if (result != DSF_SUCCESS) {
        return DSDPIPE_ERROR_READ;
    }

    dsf_ctx->current_track = track_number;
    dsf_ctx->current_frame = 0;
    dsf_ctx->audio_position = 0;

    return DSDPIPE_OK;
}

static int dsf_source_read_frame(void *ctx, dsdpipe_buffer_t *buffer)
{
    dsdpipe_source_dsf_ctx_t *dsf_ctx = (dsdpipe_source_dsf_ctx_t *)ctx;
    size_t bytes_to_read;
    size_t bytes_read = 0;
    uint64_t remaining;
    int result;

    if (!dsf_ctx || !buffer) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsf_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (dsf_ctx->current_track == 0) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    /* Check for end of track */
    if (dsf_ctx->audio_position >= dsf_ctx->audio_data_size) {
        buffer->flags = DSDPIPE_BUF_FLAG_TRACK_END | DSDPIPE_BUF_FLAG_EOF;
        buffer->size = 0;
        return 1;  /* EOF indicator */
    }

    /* Calculate how many bytes to read this frame */
    bytes_to_read = (size_t)dsf_ctx->bytes_per_frame;
    if (bytes_to_read > buffer->capacity) {
        bytes_to_read = buffer->capacity;
    }

    /* Don't read past end of audio data */
    remaining = dsf_ctx->audio_data_size - dsf_ctx->audio_position;
    if (bytes_to_read > remaining) {
        bytes_to_read = (size_t)remaining;
    }

    /* Read audio data - dsf_read_audio_data returns byte-interleaved DSDIFF format */
    result = dsf_read_audio_data(dsf_ctx->dsf, buffer->data, bytes_to_read, &bytes_read);
    if (result != DSF_SUCCESS && result != DSF_ERROR_END_OF_DATA) {
        return DSDPIPE_ERROR_READ;
    }

    if (bytes_read == 0) {
        buffer->flags = DSDPIPE_BUF_FLAG_TRACK_END | DSDPIPE_BUF_FLAG_EOF;
        buffer->size = 0;
        return 1;  /* EOF indicator */
    }

    /* Fill in buffer metadata */
    buffer->size = bytes_read;
    buffer->format = dsf_ctx->format;
    buffer->track_number = dsf_ctx->current_track;
    buffer->frame_number = dsf_ctx->current_frame;
    buffer->flags = 0;

    /* Calculate sample offset */
    buffer->sample_offset = dsf_ctx->current_frame *
                            (dsf_ctx->format.sample_rate / DSF_SOURCE_FRAME_RATE);

    /* Set track start flag for first frame */
    if (dsf_ctx->current_frame == 0) {
        buffer->flags |= DSDPIPE_BUF_FLAG_TRACK_START;
    }

    /* Update position tracking */
    dsf_ctx->audio_position += bytes_read;
    dsf_ctx->current_frame++;

    /* Set track end flag for last frame */
    if (dsf_ctx->audio_position >= dsf_ctx->audio_data_size ||
        result == DSF_ERROR_END_OF_DATA) {
        buffer->flags |= DSDPIPE_BUF_FLAG_TRACK_END;
    }

    return DSDPIPE_OK;
}

static int dsf_source_get_album_metadata(void *ctx, dsdpipe_metadata_t *metadata)
{
    dsdpipe_source_dsf_ctx_t *dsf_ctx = (dsdpipe_source_dsf_ctx_t *)ctx;
    int has_metadata = 0;
    int result;

    if (!dsf_ctx || !metadata) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsf_ctx->is_open || !dsf_ctx->dsf) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* Initialize metadata structure */
    dsdpipe_metadata_init(metadata);

    /* Set basic track info (DSF has only one track) */
    metadata->track_total = 1;
    metadata->disc_number = 1;
    metadata->disc_total = 1;

    /* Check if file has metadata (ID3v2 tag) */
    result = dsf_has_metadata(dsf_ctx->dsf, &has_metadata);
    if (result != DSF_SUCCESS || !has_metadata) {
        /* No metadata available - return empty but valid metadata */
        return DSDPIPE_OK;
    }

    /* Read ID3v2 metadata */
    uint8_t *id3_data = NULL;
    uint64_t id3_size = 0;
    result = dsf_read_metadata(dsf_ctx->dsf, &id3_data, &id3_size);
    if (result != DSF_SUCCESS || !id3_data || id3_size == 0) {
        return DSDPIPE_OK;  /* No metadata, not an error */
    }

    /* Parse ID3v2 tag using id3dev library */
    int parse_result = id3_parse_to_metadata(id3_data, (size_t)id3_size, metadata);

    /* Free the ID3 data buffer */
    sa_free(id3_data);

    /* ID3 parsing failure is not fatal - we still return success */
    (void)parse_result;

    return DSDPIPE_OK;
}

static int dsf_source_get_track_metadata(void *ctx, uint8_t track_number,
                                         dsdpipe_metadata_t *metadata)
{
    dsdpipe_source_dsf_ctx_t *dsf_ctx = (dsdpipe_source_dsf_ctx_t *)ctx;
    double duration = 0.0;
    int result;

    if (!dsf_ctx || !metadata) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsf_ctx->is_open || !dsf_ctx->dsf) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* DSF only has one track */
    if (track_number != 1) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    /* For DSF, track metadata is the same as album metadata */
    result = dsf_source_get_album_metadata(ctx, metadata);
    if (result != DSDPIPE_OK) {
        return result;
    }

    /* Set track-specific fields */
    metadata->track_number = 1;
    metadata->track_total = 1;

    /* Get duration */
    result = dsf_get_duration(dsf_ctx->dsf, &duration);
    if (result == DSF_SUCCESS) {
        metadata->duration_seconds = duration;
        metadata->duration_frames = (uint32_t)(duration * DSF_SOURCE_FRAME_RATE);
    }

    return DSDPIPE_OK;
}

static int dsf_source_get_track_frames(void *ctx, uint8_t track_number,
                                       uint64_t *frames)
{
    dsdpipe_source_dsf_ctx_t *dsf_ctx = (dsdpipe_source_dsf_ctx_t *)ctx;

    if (!dsf_ctx || !frames) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsf_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* DSF only has one track */
    if (track_number != 1) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    *frames = dsf_ctx->total_frames;
    return DSDPIPE_OK;
}

static void dsf_source_destroy(void *ctx)
{
    dsdpipe_source_dsf_ctx_t *dsf_ctx = (dsdpipe_source_dsf_ctx_t *)ctx;

    if (!dsf_ctx) {
        return;
    }

    dsf_source_close(ctx);
    sa_free(dsf_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_source_ops_t s_dsf_source_ops = {
    .open = dsf_source_open,
    .close = dsf_source_close,
    .get_track_count = dsf_source_get_track_count,
    .get_format = dsf_source_get_format,
    .seek_track = dsf_source_seek_track,
    .read_frame = dsf_source_read_frame,
    .get_album_metadata = dsf_source_get_album_metadata,
    .get_track_metadata = dsf_source_get_track_metadata,
    .get_track_frames = dsf_source_get_track_frames,
    .destroy = dsf_source_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_source_dsf_create(dsdpipe_source_t *source)
{
    dsdpipe_source_dsf_ctx_t *ctx;

    if (!source) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    ctx = (dsdpipe_source_dsf_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    source->type = DSDPIPE_SOURCE_DSF;
    source->ops = &s_dsf_source_ops;
    source->ctx = ctx;
    source->is_open = false;

    return DSDPIPE_OK;
}
