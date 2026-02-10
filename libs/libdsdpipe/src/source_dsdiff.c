/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSDIFF source implementation for libdsdpipe
 * DSDIFF files can contain either DSD or DST compressed data.
 * Edit Master files have markers that define track boundaries.
 * Non-edit-master files are treated as single-track files.
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

#include <libdsdiff/dsdiff.h>
#include <libsautil/mem.h>

#include <string.h>

/*============================================================================
 * Constants
 *============================================================================*/

/** Frame rate for SACD-compatible output (frames per second) */
#define DSDIFF_SOURCE_FRAME_RATE    75

/** Maximum metadata string buffer size */
#define DSDIFF_MAX_STRING_SIZE      1024

/** Maximum number of tracks from markers */
#define DSDIFF_MAX_TRACKS           255

/*============================================================================
 * Track Info Structure
 *============================================================================*/

/**
 * @brief Track boundary information from markers
 */
typedef struct dsdiff_track_info_s {
    uint64_t start_sample;      /**< Start sample offset */
    uint64_t end_sample;        /**< End sample offset (exclusive) */
    uint64_t sample_count;      /**< Number of samples in track */
    char *title;                /**< Track title from marker text (may be NULL) */
} dsdiff_track_info_t;

/*============================================================================
 * DSDIFF Source Context
 *============================================================================*/

typedef struct dsdpipe_source_dsdiff_ctx_s {
    /* Configuration */
    char *path;                         /**< Path to DSDIFF file */

    /* libdsdiff handle */
    dsdiff_t *dsdiff;                   /**< DSDIFF file handle */

    /* Cached format info */
    dsdpipe_format_t format;           /**< Audio format */
    dsdiff_audio_type_t audio_type;     /**< DSD or DST */
    uint32_t sample_rate;               /**< Sample rate */
    uint16_t channel_count;             /**< Number of channels */
    uint64_t total_samples;             /**< Total samples in file */
    uint64_t dsd_data_size;             /**< DSD data size in bytes */

    /* Track info */
    uint8_t track_count;                /**< Number of tracks */
    bool is_edit_master;                /**< Has markers (edit master) */
    dsdiff_track_info_t *tracks;        /**< Track info array */

    /* Derived info for DSD mode */
    uint64_t bytes_per_frame;           /**< Bytes per DSD frame */

    /* Playback state */
    uint8_t current_track;              /**< Current track (1-based) */
    uint64_t current_frame;             /**< Current frame number within track */
    uint64_t track_start_sample;        /**< Start sample of current track */
    uint64_t track_end_sample;          /**< End sample of current track */
    uint64_t current_sample;            /**< Current sample position */
    uint32_t dst_frame_index;           /**< Current DST frame index */

    /* State flags */
    bool is_open;                       /**< Whether source is open */
} dsdpipe_source_dsdiff_ctx_t;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Convert timecode to sample offset
 */
static uint64_t timecode_to_samples(const dsdiff_timecode_t *tc, uint32_t sample_rate)
{
    uint64_t samples = 0;
    samples += (uint64_t)tc->hours * 3600 * sample_rate;
    samples += (uint64_t)tc->minutes * 60 * sample_rate;
    samples += (uint64_t)tc->seconds * sample_rate;
    samples += tc->samples;
    return samples;
}

/**
 * @brief Calculate frame size for DSD data
 *
 * For SACD-compatible frame rate (75 fps), each frame contains:
 *   samples_per_frame = sample_rate / 75
 *   bytes_per_channel_per_frame = samples_per_frame / 8
 *   bytes_per_frame = bytes_per_channel_per_frame * channel_count
 */
static uint64_t calc_bytes_per_frame(uint32_t sample_rate, uint32_t channel_count)
{
    uint64_t samples_per_frame = sample_rate / DSDIFF_SOURCE_FRAME_RATE;
    uint64_t bytes_per_channel = samples_per_frame / 8;
    return bytes_per_channel * channel_count;
}

/**
 * @brief Parse track boundaries from markers
 *
 * Scans markers for TRACK_START and TRACK_STOP pairs to build track list.
 */
static int parse_tracks_from_markers(dsdpipe_source_dsdiff_ctx_t *ctx)
{
    int marker_count = 0;
    int result;

    result = dsdiff_get_dsd_marker_count(ctx->dsdiff, &marker_count);
    if (result != DSDIFF_SUCCESS || marker_count == 0) {
        /* No markers - single track */
        ctx->is_edit_master = false;
        ctx->track_count = 1;

        ctx->tracks = (dsdiff_track_info_t *)sa_calloc(1, sizeof(dsdiff_track_info_t));
        if (!ctx->tracks) {
            return DSDPIPE_ERROR_OUT_OF_MEMORY;
        }

        ctx->tracks[0].start_sample = 0;
        ctx->tracks[0].end_sample = ctx->total_samples;
        ctx->tracks[0].sample_count = ctx->total_samples;
        ctx->tracks[0].title = NULL;

        return DSDPIPE_OK;
    }

    ctx->is_edit_master = true;

    /* Allocate temporary arrays for track starts/ends */
    uint64_t *track_starts = (uint64_t *)sa_calloc((size_t)marker_count, sizeof(uint64_t));
    uint64_t *track_ends = (uint64_t *)sa_calloc((size_t)marker_count, sizeof(uint64_t));
    char **track_titles = (char **)sa_calloc((size_t)marker_count, sizeof(char *));

    if (!track_starts || !track_ends || !track_titles) {
        sa_free(track_starts);
        sa_free(track_ends);
        sa_free(track_titles);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Scan markers for track boundaries */
    int num_tracks = 0;
    for (int i = 0; i < marker_count && num_tracks < DSDIFF_MAX_TRACKS; i++) {
        dsdiff_marker_t marker;
        memset(&marker, 0, sizeof(marker));

        result = dsdiff_get_dsd_marker(ctx->dsdiff, i, &marker);
        if (result != DSDIFF_SUCCESS) {
            continue;
        }

        uint64_t sample_pos = timecode_to_samples(&marker.time, ctx->sample_rate);
        sample_pos = (uint64_t)((int64_t)sample_pos + marker.offset);

        if (marker.mark_type == DSDIFF_MARK_TRACK_START) {
            track_starts[num_tracks] = sample_pos;
            if (marker.marker_text && marker.text_length > 0) {
                track_titles[num_tracks] = dsdpipe_strdup(marker.marker_text);
            }
            /* Look for matching TRACK_STOP or use next TRACK_START */
            track_ends[num_tracks] = ctx->total_samples;  /* Default to end */
            num_tracks++;
        }

        /* Free marker text allocated by libdsdiff */
        if (marker.marker_text) {
            sa_free(marker.marker_text);
        }
    }

    /* Find track end positions from TRACK_STOP markers */
    for (int i = 0; i < marker_count; i++) {
        dsdiff_marker_t marker;
        memset(&marker, 0, sizeof(marker));

        result = dsdiff_get_dsd_marker(ctx->dsdiff, i, &marker);
        if (result != DSDIFF_SUCCESS) {
            continue;
        }

        if (marker.mark_type == DSDIFF_MARK_TRACK_STOP) {
            uint64_t sample_pos = timecode_to_samples(&marker.time, ctx->sample_rate);
            sample_pos = (uint64_t)((int64_t)sample_pos + marker.offset);

            /* Find the track this stop belongs to */
            for (int t = 0; t < num_tracks; t++) {
                if (sample_pos > track_starts[t] &&
                    (t == num_tracks - 1 || sample_pos <= track_starts[t + 1])) {
                    track_ends[t] = sample_pos;
                    break;
                }
            }
        }

        if (marker.marker_text) {
            sa_free(marker.marker_text);
        }
    }

    /* Handle case where no TRACK_START markers but other markers exist */
    if (num_tracks == 0) {
        num_tracks = 1;
        track_starts[0] = 0;
        track_ends[0] = ctx->total_samples;
    }

    /* Build final track info array */
    ctx->track_count = (uint8_t)num_tracks;
    ctx->tracks = (dsdiff_track_info_t *)sa_calloc((size_t)num_tracks, sizeof(dsdiff_track_info_t));
    if (!ctx->tracks) {
        for (int i = 0; i < num_tracks; i++) {
            sa_free(track_titles[i]);
        }
        sa_free(track_starts);
        sa_free(track_ends);
        sa_free(track_titles);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    for (int i = 0; i < num_tracks; i++) {
        ctx->tracks[i].start_sample = track_starts[i];
        ctx->tracks[i].end_sample = track_ends[i];
        ctx->tracks[i].sample_count = track_ends[i] - track_starts[i];
        ctx->tracks[i].title = track_titles[i];  /* Transfer ownership */
    }

    sa_free(track_starts);
    sa_free(track_ends);
    sa_free(track_titles);

    return DSDPIPE_OK;
}

/**
 * @brief Free track info array
 */
static void free_tracks(dsdpipe_source_dsdiff_ctx_t *ctx)
{
    if (ctx->tracks) {
        for (int i = 0; i < ctx->track_count; i++) {
            sa_free(ctx->tracks[i].title);
        }
        sa_free(ctx->tracks);
        ctx->tracks = NULL;
    }
    ctx->track_count = 0;
}

/*============================================================================
 * Source Operations
 *============================================================================*/

static int dsdiff_source_open(void *ctx, const char *path)
{
    dsdpipe_source_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_source_dsdiff_ctx_t *)ctx;
    int result;

    if (!dsdiff_ctx || !path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store path */
    dsdiff_ctx->path = dsdpipe_strdup(path);
    if (!dsdiff_ctx->path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Allocate DSDIFF handle */
    result = dsdiff_new(&dsdiff_ctx->dsdiff);
    if (result != DSDIFF_SUCCESS) {
        sa_free(dsdiff_ctx->path);
        dsdiff_ctx->path = NULL;
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Open DSDIFF file for reading */
    result = dsdiff_open(dsdiff_ctx->dsdiff, path);
    if (result != DSDIFF_SUCCESS) {
        dsdiff_ctx->dsdiff = NULL;
        sa_free(dsdiff_ctx->path);
        dsdiff_ctx->path = NULL;
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    /* Get audio type (DSD or DST) */
    result = dsdiff_get_audio_type(dsdiff_ctx->dsdiff, &dsdiff_ctx->audio_type);
    if (result != DSDIFF_SUCCESS) {
        dsdiff_close(dsdiff_ctx->dsdiff);
        dsdiff_ctx->dsdiff = NULL;
        sa_free(dsdiff_ctx->path);
        dsdiff_ctx->path = NULL;
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    /* Get channel count */
    result = dsdiff_get_channel_count(dsdiff_ctx->dsdiff, &dsdiff_ctx->channel_count);
    if (result != DSDIFF_SUCCESS) {
        dsdiff_close(dsdiff_ctx->dsdiff);
        dsdiff_ctx->dsdiff = NULL;
        sa_free(dsdiff_ctx->path);
        dsdiff_ctx->path = NULL;
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    /* Get sample rate */
    result = dsdiff_get_sample_rate(dsdiff_ctx->dsdiff, &dsdiff_ctx->sample_rate);
    if (result != DSDIFF_SUCCESS) {
        dsdiff_close(dsdiff_ctx->dsdiff);
        dsdiff_ctx->dsdiff = NULL;
        sa_free(dsdiff_ctx->path);
        dsdiff_ctx->path = NULL;
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    /* Get total sample count
     *
     * For DSD: libdsdiff returns sample_frame_count in "sample frames" where
     * 1 sample frame = 1 byte per channel = 8 DSD samples.
     *
     * For DST: sample_frame_count is not set (remains 0), so we calculate
     * from dst_frame_count and frame rate: samples = frames * (sample_rate / frame_rate)
     */
    if (dsdiff_ctx->audio_type == DSDIFF_AUDIO_DST) {
        /* DST: calculate from DST frame count */
        uint32_t dst_frame_count = 0;
        result = dsdiff_get_dst_frame_count(dsdiff_ctx->dsdiff, &dst_frame_count);
        if (result != DSDIFF_SUCCESS || dst_frame_count == 0) {
            dsdiff_close(dsdiff_ctx->dsdiff);
            dsdiff_ctx->dsdiff = NULL;
            sa_free(dsdiff_ctx->path);
            dsdiff_ctx->path = NULL;
            return DSDPIPE_ERROR_SOURCE_OPEN;
        }
        /* Each DST frame contains sample_rate / 75 DSD samples */
        uint64_t samples_per_frame = dsdiff_ctx->sample_rate / DSDIFF_SOURCE_FRAME_RATE;
        dsdiff_ctx->total_samples = (uint64_t)dst_frame_count * samples_per_frame;
    } else {
        /* DSD: get sample frame count and convert to DSD samples */
        result = dsdiff_get_sample_frame_count(dsdiff_ctx->dsdiff, &dsdiff_ctx->total_samples);
        if (result != DSDIFF_SUCCESS) {
            dsdiff_close(dsdiff_ctx->dsdiff);
            dsdiff_ctx->dsdiff = NULL;
            sa_free(dsdiff_ctx->path);
            dsdiff_ctx->path = NULL;
            return DSDPIPE_ERROR_SOURCE_OPEN;
        }
        dsdiff_ctx->total_samples *= 8;  /* Convert sample frames (bytes) to DSD samples (bits) */
    }

    /* Get DSD data size */
    result = dsdiff_get_dsd_data_size(dsdiff_ctx->dsdiff, &dsdiff_ctx->dsd_data_size);
    if (result != DSDIFF_SUCCESS) {
        dsdiff_ctx->dsd_data_size = 0;  /* Not critical */
    }

    /* Fill in format structure */
    dsdiff_ctx->format.type = (dsdiff_ctx->audio_type == DSDIFF_AUDIO_DST)
                              ? DSDPIPE_FORMAT_DST
                              : DSDPIPE_FORMAT_DSD_RAW;
    dsdiff_ctx->format.sample_rate = dsdiff_ctx->sample_rate;
    dsdiff_ctx->format.channel_count = dsdiff_ctx->channel_count;
    dsdiff_ctx->format.bits_per_sample = 1;
    dsdiff_ctx->format.frame_rate = DSDIFF_SOURCE_FRAME_RATE;

    /* Calculate bytes per frame for DSD mode */
    dsdiff_ctx->bytes_per_frame = calc_bytes_per_frame(dsdiff_ctx->sample_rate,
                                                        dsdiff_ctx->channel_count);

    /* Parse track boundaries from markers */
    result = parse_tracks_from_markers(dsdiff_ctx);
    if (result != DSDPIPE_OK) {
        dsdiff_close(dsdiff_ctx->dsdiff);
        dsdiff_ctx->dsdiff = NULL;
        sa_free(dsdiff_ctx->path);
        dsdiff_ctx->path = NULL;
        return result;
    }

    /* Initialize playback state */
    dsdiff_ctx->current_track = 0;
    dsdiff_ctx->current_frame = 0;
    dsdiff_ctx->current_sample = 0;
    dsdiff_ctx->dst_frame_index = 0;
    dsdiff_ctx->is_open = true;

    return DSDPIPE_OK;
}

static void dsdiff_source_close(void *ctx)
{
    dsdpipe_source_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_source_dsdiff_ctx_t *)ctx;

    if (!dsdiff_ctx) {
        return;
    }

    free_tracks(dsdiff_ctx);

    if (dsdiff_ctx->dsdiff) {
        dsdiff_close(dsdiff_ctx->dsdiff);
        dsdiff_ctx->dsdiff = NULL;
    }

    if (dsdiff_ctx->path) {
        sa_free(dsdiff_ctx->path);
        dsdiff_ctx->path = NULL;
    }

    dsdiff_ctx->is_open = false;
}

static int dsdiff_source_get_track_count(void *ctx, uint8_t *count)
{
    dsdpipe_source_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_source_dsdiff_ctx_t *)ctx;

    if (!dsdiff_ctx || !count) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsdiff_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    *count = dsdiff_ctx->track_count;
    return DSDPIPE_OK;
}

static int dsdiff_source_get_format(void *ctx, dsdpipe_format_t *format)
{
    dsdpipe_source_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_source_dsdiff_ctx_t *)ctx;

    if (!dsdiff_ctx || !format) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsdiff_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    *format = dsdiff_ctx->format;
    return DSDPIPE_OK;
}

static int dsdiff_source_seek_track(void *ctx, uint8_t track_number)
{
    dsdpipe_source_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_source_dsdiff_ctx_t *)ctx;
    int result;

    if (!dsdiff_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsdiff_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (track_number == 0 || track_number > dsdiff_ctx->track_count) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    /* Get track boundaries */
    dsdiff_track_info_t *track = &dsdiff_ctx->tracks[track_number - 1];
    dsdiff_ctx->track_start_sample = track->start_sample;
    dsdiff_ctx->track_end_sample = track->end_sample;

    /* Seek to track start */
    if (dsdiff_ctx->audio_type == DSDIFF_AUDIO_DST) {
        /* For DST, calculate the frame index */
        uint32_t dst_frame_count = 0;
        dsdiff_get_dst_frame_count(dsdiff_ctx->dsdiff, &dst_frame_count);

        /* Calculate DST frame index from sample position */
        uint64_t samples_per_frame = dsdiff_ctx->sample_rate / DSDIFF_SOURCE_FRAME_RATE;
        dsdiff_ctx->dst_frame_index = (uint32_t)(track->start_sample / samples_per_frame);

        /* Check if file has DST index for seeking */
        int has_index = 0;
        dsdiff_has_dst_index(dsdiff_ctx->dsdiff, &has_index);
        if (has_index && dsdiff_ctx->dst_frame_index > 0) {
            result = dsdiff_seek_dst_frame(dsdiff_ctx->dsdiff, dsdiff_ctx->dst_frame_index);
            if (result != DSDIFF_SUCCESS) {
                return DSDPIPE_ERROR_READ;
            }
        }
    } else {
        /* For DSD, seek to byte position */
        int64_t frame_offset = (int64_t)(track->start_sample / 8);  /* Convert to sample frames */
        result = dsdiff_seek_dsd_data(dsdiff_ctx->dsdiff, frame_offset, DSDIFF_SEEK_SET);
        if (result != DSDIFF_SUCCESS) {
            return DSDPIPE_ERROR_READ;
        }
    }

    dsdiff_ctx->current_track = track_number;
    dsdiff_ctx->current_frame = 0;
    dsdiff_ctx->current_sample = track->start_sample;

    return DSDPIPE_OK;
}

static int dsdiff_source_read_frame(void *ctx, dsdpipe_buffer_t *buffer)
{
    dsdpipe_source_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_source_dsdiff_ctx_t *)ctx;
    int result;

    if (!dsdiff_ctx || !buffer) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsdiff_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (dsdiff_ctx->current_track == 0) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    /* Check for end of track */
    if (dsdiff_ctx->current_sample >= dsdiff_ctx->track_end_sample) {
        buffer->flags = DSDPIPE_BUF_FLAG_TRACK_END | DSDPIPE_BUF_FLAG_EOF;
        buffer->size = 0;
        return 1;  /* EOF indicator */
    }

    if (dsdiff_ctx->audio_type == DSDIFF_AUDIO_DST) {
        /* Read DST frame */
        uint32_t frame_size = 0;
        result = dsdiff_read_dst_frame(dsdiff_ctx->dsdiff,
                                       buffer->data,
                                       (uint32_t)buffer->capacity,
                                       &frame_size);

        if (result == DSDIFF_ERROR_END_OF_DATA || frame_size == 0) {
            buffer->flags = DSDPIPE_BUF_FLAG_TRACK_END | DSDPIPE_BUF_FLAG_EOF;
            buffer->size = 0;
            return 1;  /* EOF */
        }

        if (result != DSDIFF_SUCCESS) {
            return DSDPIPE_ERROR_READ;
        }

        buffer->size = frame_size;
        dsdiff_ctx->dst_frame_index++;

        /* Update sample position (one DST frame = sample_rate / 75 samples) */
        uint64_t samples_per_frame = dsdiff_ctx->sample_rate / DSDIFF_SOURCE_FRAME_RATE;
        dsdiff_ctx->current_sample += samples_per_frame;
    } else {
        /* Read DSD data */
        uint32_t bytes_to_read = (uint32_t)dsdiff_ctx->bytes_per_frame;
        if (bytes_to_read > buffer->capacity) {
            bytes_to_read = (uint32_t)buffer->capacity;
        }

        /* Don't read past track end */
        uint64_t samples_remaining = dsdiff_ctx->track_end_sample - dsdiff_ctx->current_sample;
        uint64_t bytes_remaining = (samples_remaining / 8) * dsdiff_ctx->channel_count;
        if (bytes_to_read > bytes_remaining) {
            bytes_to_read = (uint32_t)bytes_remaining;
        }

        uint32_t bytes_read = 0;
        result = dsdiff_read_dsd_data(dsdiff_ctx->dsdiff,
                                      buffer->data,
                                      bytes_to_read,
                                      &bytes_read);

        if (result == DSDIFF_ERROR_END_OF_DATA || bytes_read == 0) {
            buffer->flags = DSDPIPE_BUF_FLAG_TRACK_END | DSDPIPE_BUF_FLAG_EOF;
            buffer->size = 0;
            return 1;  /* EOF */
        }

        if (result != DSDIFF_SUCCESS) {
            return DSDPIPE_ERROR_READ;
        }

        buffer->size = bytes_read;

        /* Update sample position */
        uint64_t samples_read = (bytes_read / dsdiff_ctx->channel_count) * 8;
        dsdiff_ctx->current_sample += samples_read;
    }

    /* Fill in buffer metadata */
    buffer->format = dsdiff_ctx->format;
    buffer->track_number = dsdiff_ctx->current_track;
    buffer->frame_number = dsdiff_ctx->current_frame;
    buffer->flags = 0;

    /* Calculate sample offset within track */
    buffer->sample_offset = dsdiff_ctx->current_sample - dsdiff_ctx->track_start_sample;

    /* Set track start flag for first frame */
    if (dsdiff_ctx->current_frame == 0) {
        buffer->flags |= DSDPIPE_BUF_FLAG_TRACK_START;
    }

    dsdiff_ctx->current_frame++;

    /* Set track end flag for last frame */
    if (dsdiff_ctx->current_sample >= dsdiff_ctx->track_end_sample) {
        buffer->flags |= DSDPIPE_BUF_FLAG_TRACK_END;
    }

    return DSDPIPE_OK;
}

static int dsdiff_source_get_album_metadata(void *ctx, dsdpipe_metadata_t *metadata)
{
    dsdpipe_source_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_source_dsdiff_ctx_t *)ctx;
    char buffer[DSDIFF_MAX_STRING_SIZE];
    uint32_t length;
    int has_field;
    int result;

    if (!dsdiff_ctx || !metadata) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsdiff_ctx->is_open || !dsdiff_ctx->dsdiff) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* Initialize metadata structure */
    dsdpipe_metadata_init(metadata);

    /* Set basic info */
    metadata->track_total = dsdiff_ctx->track_count;
    metadata->disc_number = 1;
    metadata->disc_total = 1;

    /* Parse ID3 tag first (if present) - DIIN native metadata will override */
    uint8_t *id3_data = NULL;
    uint32_t id3_size = 0;
    result = dsdiff_get_id3_tag(dsdiff_ctx->dsdiff, &id3_data, &id3_size);
    if (result == DSDIFF_SUCCESS && id3_data && id3_size > 0) {
        /* Parse ID3v2 tag using id3dev library
         * Note: id3_data points to internal memory, do not free */
        (void)id3_parse_to_metadata(id3_data, (size_t)id3_size, metadata);
    }

    /* DIIN native metadata takes priority over ID3 */

    /* Disc title (overwrites ID3 album title if present) */
    result = dsdiff_has_disc_title(dsdiff_ctx->dsdiff, &has_field);
    if (result == DSDIFF_SUCCESS && has_field) {
        length = sizeof(buffer);
        result = dsdiff_get_disc_title(dsdiff_ctx->dsdiff, &length, buffer);
        if (result == DSDIFF_SUCCESS && length > 0) {
            buffer[length < sizeof(buffer) ? length : sizeof(buffer) - 1] = '\0';
            dsdpipe_metadata_set_string(&metadata->album_title, buffer);
            /* Also store in tags as "DITI" (DSDIFF Disc Title) */
            dsdpipe_metadata_set_tag(metadata, "DITI", buffer);
        }
    }

    /* Disc artist (overwrites ID3 album artist if present) */
    result = dsdiff_has_disc_artist(dsdiff_ctx->dsdiff, &has_field);
    if (result == DSDIFF_SUCCESS && has_field) {
        length = sizeof(buffer);
        result = dsdiff_get_disc_artist(dsdiff_ctx->dsdiff, &length, buffer);
        if (result == DSDIFF_SUCCESS && length > 0) {
            buffer[length < sizeof(buffer) ? length : sizeof(buffer) - 1] = '\0';
            dsdpipe_metadata_set_string(&metadata->album_artist, buffer);
            /* Also store in tags as "DIAR" (DSDIFF Disc Artist) */
            dsdpipe_metadata_set_tag(metadata, "DIAR", buffer);
        }
    }

    return DSDPIPE_OK;
}

static int dsdiff_source_get_track_metadata(void *ctx, uint8_t track_number,
                                            dsdpipe_metadata_t *metadata)
{
    dsdpipe_source_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_source_dsdiff_ctx_t *)ctx;
    int result;

    if (!dsdiff_ctx || !metadata) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsdiff_ctx->is_open || !dsdiff_ctx->dsdiff) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (track_number == 0 || track_number > dsdiff_ctx->track_count) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    /* Start with album metadata */
    result = dsdiff_source_get_album_metadata(ctx, metadata);
    if (result != DSDPIPE_OK) {
        return result;
    }

    /* Set track-specific fields */
    metadata->track_number = track_number;
    metadata->track_total = dsdiff_ctx->track_count;

    /* Get track info */
    dsdiff_track_info_t *track = &dsdiff_ctx->tracks[track_number - 1];

    /* Track title from marker text (as fallback) */
    if (track->title) {
        dsdpipe_metadata_set_string(&metadata->track_title, track->title);
    }

    /* Read per-track ID3 tag if present (Edit Master mode)
     * Per-track ID3 overrides marker-based fields */
    uint8_t *track_id3_data = NULL;
    uint32_t track_id3_size = 0;
    uint32_t track_index = (uint32_t)(track_number - 1);  /* 0-based index */

    result = dsdiff_get_track_id3_tag(dsdiff_ctx->dsdiff, track_index,
                                       &track_id3_data, &track_id3_size);
    if (result == DSDIFF_SUCCESS && track_id3_data && track_id3_size > 0) {
        /* Parse per-track ID3 directly into metadata.
         * id3_parse_to_metadata() only overwrites fields present in the tag,
         * so album-level fields and marker-derived title remain unless
         * the per-track ID3 has those frames. */
        (void)id3_parse_to_metadata(track_id3_data, (size_t)track_id3_size,
                                     metadata);

        /* Free the allocated ID3 data (dsdiff_get_track_id3_tag allocates) */
        sa_free(track_id3_data);
    }

    /* Calculate duration */
    double duration = (double)track->sample_count / (double)dsdiff_ctx->sample_rate;
    metadata->duration_seconds = duration;
    metadata->duration_frames = (uint32_t)(duration * DSDIFF_SOURCE_FRAME_RATE);

    return DSDPIPE_OK;
}

static int dsdiff_source_get_track_frames(void *ctx, uint8_t track_number,
                                          uint64_t *frames)
{
    dsdpipe_source_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_source_dsdiff_ctx_t *)ctx;

    if (!dsdiff_ctx || !frames) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsdiff_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (track_number == 0 || track_number > dsdiff_ctx->track_count) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    /* Calculate frames from sample count */
    dsdiff_track_info_t *track = &dsdiff_ctx->tracks[track_number - 1];
    uint64_t samples_per_frame = dsdiff_ctx->sample_rate / DSDIFF_SOURCE_FRAME_RATE;
    *frames = track->sample_count / samples_per_frame;

    return DSDPIPE_OK;
}

static void dsdiff_source_destroy(void *ctx)
{
    dsdpipe_source_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_source_dsdiff_ctx_t *)ctx;

    if (!dsdiff_ctx) {
        return;
    }

    dsdiff_source_close(ctx);
    sa_free(dsdiff_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_source_ops_t s_dsdiff_source_ops = {
    .open = dsdiff_source_open,
    .close = dsdiff_source_close,
    .get_track_count = dsdiff_source_get_track_count,
    .get_format = dsdiff_source_get_format,
    .seek_track = dsdiff_source_seek_track,
    .read_frame = dsdiff_source_read_frame,
    .get_album_metadata = dsdiff_source_get_album_metadata,
    .get_track_metadata = dsdiff_source_get_track_metadata,
    .get_track_frames = dsdiff_source_get_track_frames,
    .destroy = dsdiff_source_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_source_dsdiff_create(dsdpipe_source_t *source)
{
    dsdpipe_source_dsdiff_ctx_t *ctx;

    if (!source) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    ctx = (dsdpipe_source_dsdiff_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    source->type = DSDPIPE_SOURCE_DSDIFF;
    source->ops = &s_dsdiff_source_ops;
    source->ctx = ctx;
    source->is_open = false;

    return DSDPIPE_OK;
}
