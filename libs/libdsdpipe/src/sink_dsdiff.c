/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSDIFF sink implementation using libdsdiff
 * Supports per-track and edit master modes.
 * Standard headers first, before platform-specific ones
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "dsdpipe_internal.h"

#include <libdsdiff/dsdiff.h>
#include <libsautil/mem.h>
#include <libsautil/sa_path.h>
#include <libsautil/sastring.h>

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum filename length */
#define MAX_FILENAME_SIZE   1024

/** DSD frame size in samples per channel */
#define DSD_SAMPLES_PER_FRAME   (4704 * 8)

/*============================================================================
 * DSDIFF Sink Context
 *============================================================================*/

typedef struct dsdpipe_sink_dsdiff_ctx_s {
    /* Configuration */
    char *base_path;                    /**< Base output path */
    bool write_dst;                     /**< Keep DST compression */
    bool edit_master;                   /**< Create edit master */
    bool write_id3;                     /**< Write ID3 tag */
    uint8_t track_selection_count;      /**< Track count for edit master renumbering */
    dsdpipe_track_format_t track_filename_format; /**< Track filename format */

    /* Current state */
    dsdiff_t *dsdiff_handle;            /**< Current DSDIFF file handle */
    dsdpipe_format_t format;           /**< Audio format */
    uint8_t current_track;              /**< Current track number (1-based) */
    bool track_is_open;                 /**< Whether a track file is open */
    bool file_is_open;                  /**< Whether main file is open (edit master mode) */

    /* Sample position tracking (for edit master markers) */
    uint64_t current_sample;            /**< Current sample position */
    uint64_t track_start_sample;        /**< Track start sample position */

    /* Album metadata (cached for DIIN) */
    dsdpipe_metadata_t album_metadata; /**< Cached album metadata */
    bool have_album_metadata;           /**< Whether album metadata is set */

    /* Current track metadata */
    dsdpipe_metadata_t track_metadata; /**< Current track metadata */

    /* Statistics */
    uint64_t frames_written;            /**< Total frames written */
    uint64_t bytes_written;             /**< Total bytes written */
    uint64_t tracks_written;            /**< Total tracks written */
    uint64_t markers_added;             /**< Total markers added */

    /* State flags */
    bool is_open;                       /**< Whether sink is open */
} dsdpipe_sink_dsdiff_ctx_t;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Map channel count to DSDIFF loudspeaker configuration
 */
static dsdiff_loudspeaker_config_t get_loudspeaker_config(uint16_t channel_count)
{
    switch (channel_count) {
        case 2:  return DSDIFF_LS_CONFIG_STEREO;
        case 5:  return DSDIFF_LS_CONFIG_MULTI5;
        case 6:  return DSDIFF_LS_CONFIG_MULTI6;
        default: return DSDIFF_LS_CONFIG_INVALID;
    }
}

/**
 * @brief Convert sample position to DSDIFF timecode
 */
static void samples_to_timecode(uint64_t samples, uint32_t sample_rate,
                                dsdiff_timecode_t *tc)
{
    uint64_t total_seconds;
    uint32_t remaining_samples;

    if (sample_rate == 0) {
        memset(tc, 0, sizeof(dsdiff_timecode_t));
        return;
    }

    total_seconds = samples / sample_rate;
    remaining_samples = (uint32_t)(samples % sample_rate);

    tc->hours = (uint16_t)(total_seconds / 3600);
    tc->minutes = (uint8_t)((total_seconds % 3600) / 60);
    tc->seconds = (uint8_t)(total_seconds % 60);
    tc->samples = remaining_samples;
}

/**
 * @brief Get DSDIFF audio type from dsdpipe format
 */
static dsdiff_audio_type_t get_audio_type(const dsdpipe_format_t *format, bool write_dst)
{
    if (write_dst && format->type == DSDPIPE_FORMAT_DST) {
        return DSDIFF_AUDIO_DST;
    }
    return DSDIFF_AUDIO_DSD;
}

/**
 * @brief Generate output filename for a track (per-track mode)
 *
 * Uses dsdpipe_get_track_filename() for the name and sa_make_path()
 * to construct the filesystem path.
 *
 * @param base_path Base output directory
 * @param metadata Track metadata (may be NULL)
 * @param format Track filename format
 * @return Allocated filename string, or NULL on error
 */
static char *generate_track_filename(const char *base_path,
                                     const dsdpipe_metadata_t *metadata,
                                     dsdpipe_track_format_t format)
{
    if (!base_path) {
        return NULL;
    }

    char *track_name = dsdpipe_get_track_filename(metadata, format);
    if (!track_name) {
        uint8_t track_num = metadata ? metadata->track_number : 0;
        track_name = sa_asprintf("%02u", track_num);
        if (!track_name) {
            return NULL;
        }
    }

    char *full_path = sa_make_path(base_path, NULL, track_name, "dff");
    sa_free(track_name);
    return full_path;
}

/**
 * @brief Generate output filename for edit master
 *
 * Uses dsdpipe_get_album_dir() to produce "Artist - Album" format
 * (matching CUE/XML naming), with "edit_master" as fallback.
 */
static char *generate_edit_master_filename(const char *base_path,
                                           const dsdpipe_metadata_t *metadata)
{
    if (!base_path) {
        return NULL;
    }

    const char *name = "edit_master";
    char *album_name = NULL;

    if (metadata) {
        album_name = dsdpipe_get_album_dir(metadata,
                                            DSDPIPE_ALBUM_ARTIST_TITLE);
    }

    char *result = sa_make_path(base_path, NULL,
                                album_name ? album_name : name, "dff");
    sa_free(album_name);
    return result;
}

/**
 * @brief Set DIIN metadata (disc artist, title) on DSDIFF file
 */
static void set_diin_metadata(dsdiff_t *handle, const dsdpipe_metadata_t *metadata)
{
    if (!handle || !metadata) {
        return;
    }

    if (metadata->album_artist && metadata->album_artist[0]) {
        dsdiff_set_disc_artist(handle, metadata->album_artist);
    }

    if (metadata->album_title && metadata->album_title[0]) {
        dsdiff_set_disc_title(handle, metadata->album_title);
    }
}

/**
 * @brief Add standard comments to DSDIFF file
 */
static void add_standard_comments(dsdiff_t *handle, const dsdpipe_metadata_t *metadata)
{
    dsdiff_comment_t comment = {0};
    char comment_text[512];

    if (!handle) {
        return;
    }

    /* Comment 1: Source information */
    /* Use metadata date if available, otherwise use zeros */
    if (metadata) {
        comment.year = metadata->year;
        comment.month = metadata->month;
        comment.day = metadata->day;
    }
    comment.hour = 0;
    comment.minute = 0;
    comment.comment_type = DSDIFF_COMMENT_TYPE_GENERAL;
    comment.comment_ref = 0;

#ifdef _MSC_VER
    sprintf_s(comment_text, sizeof(comment_text),
              "Source: %s",
              (metadata && metadata->album_title) ? metadata->album_title : "Unknown");
#else
    snprintf(comment_text, sizeof(comment_text),
             "Source: %s",
             (metadata && metadata->album_title) ? metadata->album_title : "Unknown");
#endif
    comment.text_length = (uint32_t)strlen(comment_text);
    comment.text = comment_text;
    dsdiff_add_comment(handle, &comment);

    /* Comment 2: Generator information */
    comment.comment_type = DSDIFF_COMMENT_TYPE_FILE_HISTORY;
    comment.comment_ref = DSDIFF_HISTORY_CREATE_MACHINE;

#ifdef _MSC_VER
    sprintf_s(comment_text, sizeof(comment_text),
              "Created by libdsdpipe");
#else
    snprintf(comment_text, sizeof(comment_text),
             "Created by libdsdpipe");
#endif
    comment.text_length = (uint32_t)strlen(comment_text);
    comment.text = comment_text;
    dsdiff_add_comment(handle, &comment);
}

/**
 * @brief Add PROGRAM_START marker at position 0 (edit master mode)
 */
static void add_program_start_marker(dsdiff_t *handle)
{
    dsdiff_marker_t marker = {0};

    if (!handle) {
        return;
    }

    marker.time.hours = 0;
    marker.time.minutes = 0;
    marker.time.seconds = 0;
    marker.time.samples = 0;
    marker.mark_type = DSDIFF_MARK_PROGRAM_START;
    marker.mark_channel = DSDIFF_MARK_CHANNEL_ALL;
    marker.track_flags = DSDIFF_TRACK_FLAG_NONE;
    marker.offset = 0;
    marker.text_length = 0;
    marker.marker_text = NULL;

    dsdiff_add_dsd_marker(handle, &marker);
}

/**
 * @brief Add TRACK_START marker (edit master mode)
 */
static void add_track_start_marker(dsdiff_t *handle, uint64_t sample_pos,
                                   uint32_t sample_rate, const char *track_title)
{
    dsdiff_marker_t marker = {0};

    if (!handle) {
        return;
    }

    samples_to_timecode(sample_pos, sample_rate, &marker.time);
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    marker.mark_channel = DSDIFF_MARK_CHANNEL_ALL;
    marker.track_flags = DSDIFF_TRACK_FLAG_NONE;
    marker.offset = 0;

    if (track_title && track_title[0]) {
        marker.text_length = (uint32_t)strlen(track_title);
        marker.marker_text = (char *)track_title;
    } else {
        marker.text_length = 0;
        marker.marker_text = NULL;
    }

    dsdiff_add_dsd_marker(handle, &marker);
}

/**
 * @brief Add TRACK_STOP marker (edit master mode)
 */
static void add_track_stop_marker(dsdiff_t *handle, uint64_t sample_pos,
                                  uint32_t sample_rate)
{
    dsdiff_marker_t marker = {0};

    if (!handle) {
        return;
    }

    samples_to_timecode(sample_pos, sample_rate, &marker.time);
    marker.mark_type = DSDIFF_MARK_TRACK_STOP;
    marker.mark_channel = DSDIFF_MARK_CHANNEL_ALL;
    marker.track_flags = DSDIFF_TRACK_FLAG_NONE;
    marker.offset = 0;
    marker.text_length = 0;
    marker.marker_text = NULL;

    dsdiff_add_dsd_marker(handle, &marker);
}

/**
 * @brief Close the current track file (per-track mode)
 */
static void close_current_track(dsdpipe_sink_dsdiff_ctx_t *ctx)
{
    if (!ctx->track_is_open || !ctx->dsdiff_handle || ctx->edit_master) {
        return;
    }

    /* Finalize and close DSDIFF file */
    dsdiff_finalize(ctx->dsdiff_handle);
    dsdiff_close(ctx->dsdiff_handle);

    ctx->dsdiff_handle = NULL;
    ctx->track_is_open = false;

    /* Clear track metadata */
    dsdpipe_metadata_free(&ctx->track_metadata);
    dsdpipe_metadata_init(&ctx->track_metadata);
}

/**
 * @brief Close the edit master file
 */
static void close_edit_master(dsdpipe_sink_dsdiff_ctx_t *ctx)
{
    if (!ctx->file_is_open || !ctx->dsdiff_handle || !ctx->edit_master) {
        return;
    }

    /* Finalize and close DSDIFF file */
    dsdiff_finalize(ctx->dsdiff_handle);
    dsdiff_close(ctx->dsdiff_handle);

    ctx->dsdiff_handle = NULL;
    ctx->file_is_open = false;
    ctx->track_is_open = false;
}

/**
 * @brief Create and open a new DSDIFF file
 * @param ctx Sink context
 * @param filename Output filename
 * @param metadata Metadata to write
 * @param track_number Track number for ID3 tag (0 for album-only)
 */
static int create_dsdiff_file(dsdpipe_sink_dsdiff_ctx_t *ctx, const char *filename,
                              const dsdpipe_metadata_t *metadata, uint8_t track_number)
{
    dsdiff_audio_type_t audio_type;
    int result;

    /* Allocate DSDIFF handle */
    result = dsdiff_new(&ctx->dsdiff_handle);
    if (result != DSDIFF_SUCCESS) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Determine audio type */
    audio_type = get_audio_type(&ctx->format, ctx->write_dst);

    /* Create DSDIFF file */
    result = dsdiff_create(ctx->dsdiff_handle, filename, audio_type,
                           ctx->format.channel_count, 1, ctx->format.sample_rate);
    if (result != DSDIFF_SUCCESS) {
        dsdiff_close(ctx->dsdiff_handle);
        ctx->dsdiff_handle = NULL;
        return DSDPIPE_ERROR_SINK_OPEN;
    }

    /* Set loudspeaker configuration */
    dsdiff_set_loudspeaker_config(ctx->dsdiff_handle,
                                  get_loudspeaker_config(ctx->format.channel_count));

    /* Set DST frame rate if using DST */
    if (audio_type == DSDIFF_AUDIO_DST) {
        dsdiff_set_dst_frame_rate(ctx->dsdiff_handle, (uint16_t)ctx->format.frame_rate);
    }

    /* Set DIIN metadata */
    if (metadata) {
        set_diin_metadata(ctx->dsdiff_handle, metadata);
    } else if (ctx->have_album_metadata) {
        set_diin_metadata(ctx->dsdiff_handle, &ctx->album_metadata);
    }

    /* Add standard comments */
    add_standard_comments(ctx->dsdiff_handle,
                          ctx->have_album_metadata ? &ctx->album_metadata : metadata);

    /* Set ID3 tag if enabled */
    if (ctx->write_id3) {
        const dsdpipe_metadata_t *meta = metadata;
        if (!meta && ctx->have_album_metadata) {
            meta = &ctx->album_metadata;
        }
        if (meta) {
            uint8_t *id3_data = NULL;
            size_t id3_size = 0;
            uint8_t tn = track_number > 0 ? track_number : 1;
            if (dsdpipe_id3_render(meta, tn, &id3_data, &id3_size) == DSDPIPE_OK) {
                dsdiff_set_id3_tag(ctx->dsdiff_handle, id3_data, (uint32_t)id3_size);
                free(id3_data);
            }
        }
    }

    return DSDPIPE_OK;
}

/*============================================================================
 * Sink Operations
 *============================================================================*/

static int dsdiff_sink_open(void *ctx, const char *path,
                            const dsdpipe_format_t *format,
                            const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_sink_dsdiff_ctx_t *)ctx;

    if (!dsdiff_ctx || !path || !format) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store base path */
    dsdiff_ctx->base_path = dsdpipe_strdup(path);
    if (!dsdiff_ctx->base_path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Ensure output directory exists */
    if (sa_mkdir_p(path, NULL, 0755) != 0) {
        sa_free(dsdiff_ctx->base_path);
        dsdiff_ctx->base_path = NULL;
        return DSDPIPE_ERROR_FILE_CREATE;
    }

    /* Store format */
    dsdiff_ctx->format = *format;

    /* Initialize statistics */
    dsdiff_ctx->frames_written = 0;
    dsdiff_ctx->bytes_written = 0;
    dsdiff_ctx->tracks_written = 0;
    dsdiff_ctx->markers_added = 0;
    dsdiff_ctx->current_sample = 0;
    dsdiff_ctx->track_start_sample = 0;

    /* Cache album metadata */
    if (metadata) {
        int result = dsdpipe_metadata_copy(&dsdiff_ctx->album_metadata, metadata);
        if (result == DSDPIPE_OK) {
            dsdiff_ctx->have_album_metadata = true;
        }
    }

    /* In edit master mode, create the single output file now */
    if (dsdiff_ctx->edit_master) {
        char *filename = generate_edit_master_filename(path,
            dsdiff_ctx->have_album_metadata ? &dsdiff_ctx->album_metadata : metadata);
        if (!filename) {
            return DSDPIPE_ERROR_OUT_OF_MEMORY;
        }

        int result = create_dsdiff_file(dsdiff_ctx, filename, metadata, 0);
        sa_free(filename);

        if (result != DSDPIPE_OK) {
            return result;
        }

        /* Add PROGRAM_START marker at position 0 */
        add_program_start_marker(dsdiff_ctx->dsdiff_handle);
        dsdiff_ctx->markers_added++;

        dsdiff_ctx->file_is_open = true;
    }

    dsdiff_ctx->is_open = true;
    return DSDPIPE_OK;
}

static void dsdiff_sink_close(void *ctx)
{
    dsdpipe_sink_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_sink_dsdiff_ctx_t *)ctx;

    if (!dsdiff_ctx) {
        return;
    }

    /* Close any open track (per-track mode) */
    if (!dsdiff_ctx->edit_master) {
        close_current_track(dsdiff_ctx);
    } else {
        close_edit_master(dsdiff_ctx);
    }

    /* Free album metadata */
    if (dsdiff_ctx->have_album_metadata) {
        dsdpipe_metadata_free(&dsdiff_ctx->album_metadata);
        dsdiff_ctx->have_album_metadata = false;
    }

    /* Free base path */
    if (dsdiff_ctx->base_path) {
        sa_free(dsdiff_ctx->base_path);
        dsdiff_ctx->base_path = NULL;
    }

    dsdiff_ctx->is_open = false;
}

static int dsdiff_sink_track_start(void *ctx, uint8_t track_number,
                                   const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_sink_dsdiff_ctx_t *)ctx;
    int result;

    if (!dsdiff_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsdiff_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    dsdiff_ctx->current_track = track_number;

    /* Store track metadata */
    if (metadata) {
        dsdpipe_metadata_copy(&dsdiff_ctx->track_metadata, metadata);
    }

    if (dsdiff_ctx->edit_master) {
        /* Edit master mode: add TRACK_START marker */
        dsdiff_ctx->track_start_sample = dsdiff_ctx->current_sample;

        const char *title = metadata ? metadata->track_title : NULL;
        add_track_start_marker(dsdiff_ctx->dsdiff_handle,
                               dsdiff_ctx->track_start_sample,
                               dsdiff_ctx->format.sample_rate,
                               title);
        dsdiff_ctx->markers_added++;
        dsdiff_ctx->track_is_open = true;
    } else {
        /* Per-track mode: create new file */
        close_current_track(dsdiff_ctx);

        char *filename = generate_track_filename(dsdiff_ctx->base_path, metadata,
                                                 dsdiff_ctx->track_filename_format);
        if (!filename) {
            return DSDPIPE_ERROR_OUT_OF_MEMORY;
        }

        result = create_dsdiff_file(dsdiff_ctx, filename, metadata, track_number);
        sa_free(filename);

        if (result != DSDPIPE_OK) {
            return result;
        }

        dsdiff_ctx->track_is_open = true;
    }

    return DSDPIPE_OK;
}

static int dsdiff_sink_track_end(void *ctx, uint8_t track_number)
{
    dsdpipe_sink_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_sink_dsdiff_ctx_t *)ctx;

    if (!dsdiff_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsdiff_ctx->track_is_open || !dsdiff_ctx->dsdiff_handle) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    (void)track_number;  /* Unused, we track current_track internally */

    if (dsdiff_ctx->edit_master) {
        /* Edit master mode: add TRACK_STOP marker */
        add_track_stop_marker(dsdiff_ctx->dsdiff_handle,
                              dsdiff_ctx->current_sample,
                              dsdiff_ctx->format.sample_rate);
        dsdiff_ctx->markers_added++;
        dsdiff_ctx->track_is_open = false;

        /* Write per-track ID3 tag if enabled */
        if (dsdiff_ctx->write_id3) {
            uint8_t *id3_data = NULL;
            size_t id3_size = 0;

            /* Override track numbering for edit master mode
             * Selection "1,2,3,5" becomes sequential "1/4, 2/4, 3/4, 4/4"
             * tracks_written is 0-indexed at this point (before increment) */
            uint8_t sequential_track = (uint8_t)(dsdiff_ctx->tracks_written + 1);
            dsdiff_ctx->track_metadata.track_number = sequential_track;
            if (dsdiff_ctx->track_selection_count > 0) {
                dsdiff_ctx->track_metadata.track_total = dsdiff_ctx->track_selection_count;
            }

            /* Generate ID3 tag from track metadata
             * track_metadata contains both album and track info */
            if (dsdpipe_id3_render(&dsdiff_ctx->track_metadata,
                                   sequential_track,
                                   &id3_data, &id3_size) == DSDPIPE_OK) {
                /* Track index is 0-based for libdsdiff API */
                uint32_t track_index = (uint32_t)dsdiff_ctx->tracks_written;
                dsdiff_set_track_id3_tag(dsdiff_ctx->dsdiff_handle,
                                         track_index, id3_data, (uint32_t)id3_size);
                free(id3_data);
            }
        }
    } else {
        /* Per-track mode: finalize and close file */
        close_current_track(dsdiff_ctx);
    }

    dsdiff_ctx->tracks_written++;

    /* Clear track metadata */
    dsdpipe_metadata_free(&dsdiff_ctx->track_metadata);
    dsdpipe_metadata_init(&dsdiff_ctx->track_metadata);

    return DSDPIPE_OK;
}

static int dsdiff_sink_write_frame(void *ctx, const dsdpipe_buffer_t *buffer)
{
    dsdpipe_sink_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_sink_dsdiff_ctx_t *)ctx;
    int result;

    if (!dsdiff_ctx || !buffer) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsdiff_ctx->dsdiff_handle) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (!dsdiff_ctx->edit_master && !dsdiff_ctx->track_is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* Write audio data based on format */
    if (buffer->format.type == DSDPIPE_FORMAT_DST && dsdiff_ctx->write_dst) {
        /* Write DST frame directly (passthrough) */
        result = dsdiff_write_dst_frame(dsdiff_ctx->dsdiff_handle,
                                        buffer->data,
                                        (uint32_t)buffer->size);
        if (result != DSDIFF_SUCCESS) {
            return DSDPIPE_ERROR_WRITE;
        }

        /* Update sample counter: DST frame = DSD_SAMPLES_PER_FRAME samples per channel */
        dsdiff_ctx->current_sample += DSD_SAMPLES_PER_FRAME;
    } else {
        /* Write DSD data */
        uint32_t written = 0;
        result = dsdiff_write_dsd_data(dsdiff_ctx->dsdiff_handle,
                                       buffer->data,
                                       (uint32_t)buffer->size,
                                       &written);
        if (result != DSDIFF_SUCCESS) {
            return DSDPIPE_ERROR_WRITE;
        }

        /* Update sample counter: 8 samples per byte per channel */
        dsdiff_ctx->current_sample += (uint64_t)(buffer->size * 8) / dsdiff_ctx->format.channel_count;
        dsdiff_ctx->bytes_written += written;
    }

    dsdiff_ctx->frames_written++;

    return DSDPIPE_OK;
}

static int dsdiff_sink_finalize(void *ctx)
{
    dsdpipe_sink_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_sink_dsdiff_ctx_t *)ctx;

    if (!dsdiff_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Close any open file */
    if (dsdiff_ctx->edit_master) {
        close_edit_master(dsdiff_ctx);
    } else {
        close_current_track(dsdiff_ctx);
    }

    return DSDPIPE_OK;
}

static uint32_t dsdiff_sink_get_capabilities(void *ctx)
{
    dsdpipe_sink_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_sink_dsdiff_ctx_t *)ctx;
    uint32_t caps = DSDPIPE_SINK_CAP_DSD | DSDPIPE_SINK_CAP_METADATA;

    if (dsdiff_ctx && dsdiff_ctx->write_dst) {
        caps |= DSDPIPE_SINK_CAP_DST;
    }
    if (dsdiff_ctx && dsdiff_ctx->edit_master) {
        caps |= DSDPIPE_SINK_CAP_MARKERS | DSDPIPE_SINK_CAP_MULTI_TRACK;
    }

    return caps;
}

static void dsdiff_sink_destroy(void *ctx)
{
    dsdpipe_sink_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_sink_dsdiff_ctx_t *)ctx;

    if (!dsdiff_ctx) {
        return;
    }

    dsdiff_sink_close(ctx);
    sa_free(dsdiff_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_sink_ops_t s_dsdiff_sink_ops = {
    .open = dsdiff_sink_open,
    .close = dsdiff_sink_close,
    .track_start = dsdiff_sink_track_start,
    .track_end = dsdiff_sink_track_end,
    .write_frame = dsdiff_sink_write_frame,
    .finalize = dsdiff_sink_finalize,
    .get_capabilities = dsdiff_sink_get_capabilities,
    .destroy = dsdiff_sink_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_sink_dsdiff_create(dsdpipe_sink_t **sink,
                                const dsdpipe_sink_config_t *config)
{
    dsdpipe_sink_t *new_sink;
    dsdpipe_sink_dsdiff_ctx_t *ctx;

    if (!sink || !config) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    new_sink = (dsdpipe_sink_t *)sa_calloc(1, sizeof(*new_sink));
    if (!new_sink) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    ctx = (dsdpipe_sink_dsdiff_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        sa_free(new_sink);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Store configuration */
    ctx->write_dst = config->opts.dsdiff.write_dst;
    ctx->edit_master = config->opts.dsdiff.edit_master;
    ctx->write_id3 = config->opts.dsdiff.write_id3;
    ctx->track_filename_format = config->track_filename_format;

    /* Initialize metadata structures */
    dsdpipe_metadata_init(&ctx->album_metadata);
    dsdpipe_metadata_init(&ctx->track_metadata);

    new_sink->type = config->opts.dsdiff.edit_master
                     ? DSDPIPE_SINK_DSDIFF_EDIT : DSDPIPE_SINK_DSDIFF;
    new_sink->ops = &s_dsdiff_sink_ops;
    new_sink->ctx = ctx;
    new_sink->config = *config;
    new_sink->config.path = dsdpipe_strdup(config->path);
    new_sink->caps = dsdiff_sink_get_capabilities(ctx);
    new_sink->is_open = false;

    *sink = new_sink;
    return DSDPIPE_OK;
}

void dsdpipe_sink_dsdiff_set_track_count(void *ctx, uint8_t track_count)
{
    dsdpipe_sink_dsdiff_ctx_t *dsdiff_ctx = (dsdpipe_sink_dsdiff_ctx_t *)ctx;
    if (dsdiff_ctx) {
        dsdiff_ctx->track_selection_count = track_count;
    }
}
