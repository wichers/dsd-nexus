/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSF sink implementation using libdsf
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

#include <libdsf/dsf.h>
#include <libsautil/mem.h>
#include <libsautil/sa_path.h>
#include <libsautil/sastring.h>

#include <string.h>
#include <stdio.h>

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum ID3 tag buffer size */
#define DSF_ID3_BUFFER_SIZE     8192

/*============================================================================
 * ID3v2.4 Tag Generation
 *============================================================================*/

/**
 * @brief Write ID3v2.4 syncsafe integer (28 bits spread across 4 bytes)
 */
static void write_syncsafe_int(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)((value >> 21) & 0x7F);
    buf[1] = (uint8_t)((value >> 14) & 0x7F);
    buf[2] = (uint8_t)((value >> 7) & 0x7F);
    buf[3] = (uint8_t)(value & 0x7F);
}

/**
 * @brief Write ID3v2.4 frame header and UTF-8 text content
 * @return Number of bytes written, or 0 on error
 */
static size_t write_id3_text_frame(uint8_t *buf, size_t buf_size,
                                   const char *frame_id, const char *text)
{
    size_t text_len;
    size_t frame_size;
    size_t total_size;

    if (!text || !text[0]) {
        return 0;
    }

    text_len = strlen(text);
    frame_size = 1 + text_len;  /* encoding byte + text */
    total_size = 10 + frame_size;  /* header + content */

    if (total_size > buf_size) {
        return 0;
    }

    /* Frame ID (4 bytes) */
    buf[0] = (uint8_t)frame_id[0];
    buf[1] = (uint8_t)frame_id[1];
    buf[2] = (uint8_t)frame_id[2];
    buf[3] = (uint8_t)frame_id[3];

    /* Frame size (syncsafe) */
    write_syncsafe_int(buf + 4, (uint32_t)frame_size);

    /* Flags (2 bytes) */
    buf[8] = 0;
    buf[9] = 0;

    /* Encoding (UTF-8 = 3) */
    buf[10] = 3;

    /* Text content */
#ifdef _MSC_VER
    memcpy_s(buf + 11, buf_size - 11, text, text_len);
#else
    memcpy(buf + 11, text, text_len);
#endif

    return total_size;
}

/**
 * @brief Write ID3v2.4 TRCK frame (track number)
 */
static size_t write_id3_track_frame(uint8_t *buf, size_t buf_size,
                                    uint8_t track_num, uint8_t track_total)
{
    char track_str[16];

    if (track_total > 0) {
#ifdef _MSC_VER
        sprintf_s(track_str, sizeof(track_str), "%d/%d", track_num, track_total);
#else
        snprintf(track_str, sizeof(track_str), "%d/%d", track_num, track_total);
#endif
    } else {
#ifdef _MSC_VER
        sprintf_s(track_str, sizeof(track_str), "%d", track_num);
#else
        snprintf(track_str, sizeof(track_str), "%d", track_num);
#endif
    }

    return write_id3_text_frame(buf, buf_size, "TRCK", track_str);
}

/**
 * @brief Write ID3v2.4 TYER frame (year)
 */
static size_t write_id3_year_frame(uint8_t *buf, size_t buf_size, uint16_t year)
{
    char year_str[8];

    if (year == 0) {
        return 0;
    }

#ifdef _MSC_VER
    sprintf_s(year_str, sizeof(year_str), "%d", year);
#else
    snprintf(year_str, sizeof(year_str), "%d", year);
#endif

    return write_id3_text_frame(buf, buf_size, "TYER", year_str);
}

/**
 * @brief Build complete ID3v2.4 tag from metadata
 * @return Size of ID3 tag in bytes, or 0 on error
 */
static size_t build_id3_tag(uint8_t *buf, size_t buf_size,
                            const dsdpipe_metadata_t *album_meta,
                            const dsdpipe_metadata_t *track_meta)
{
    size_t offset = 10;  /* Skip header, fill in later */
    size_t written;

    if (buf_size < 128) {
        return 0;
    }

    /* Track title (TIT2) */
    if (track_meta && track_meta->track_title) {
        written = write_id3_text_frame(buf + offset, buf_size - offset,
                                       "TIT2", track_meta->track_title);
        offset += written;
    }

    /* Track performer (TPE1) - fall back to album artist */
    if (track_meta && track_meta->track_performer) {
        written = write_id3_text_frame(buf + offset, buf_size - offset,
                                       "TPE1", track_meta->track_performer);
        offset += written;
    } else if (album_meta && album_meta->album_artist) {
        written = write_id3_text_frame(buf + offset, buf_size - offset,
                                       "TPE1", album_meta->album_artist);
        offset += written;
    }

    /* Album title (TALB) */
    if (album_meta && album_meta->album_title) {
        written = write_id3_text_frame(buf + offset, buf_size - offset,
                                       "TALB", album_meta->album_title);
        offset += written;
    }

    /* Album artist (TPE2) */
    if (album_meta && album_meta->album_artist) {
        written = write_id3_text_frame(buf + offset, buf_size - offset,
                                       "TPE2", album_meta->album_artist);
        offset += written;
    }

    /* Track number (TRCK) */
    if (track_meta && track_meta->track_number > 0) {
        written = write_id3_track_frame(buf + offset, buf_size - offset,
                                        track_meta->track_number,
                                        track_meta->track_total);
        offset += written;
    }

    /* Year (TYER) */
    if (album_meta && album_meta->year > 0) {
        written = write_id3_year_frame(buf + offset, buf_size - offset,
                                       album_meta->year);
        offset += written;
    }

    /* Genre (TCON) */
    if (track_meta && track_meta->genre) {
        written = write_id3_text_frame(buf + offset, buf_size - offset,
                                       "TCON", track_meta->genre);
        offset += written;
    } else if (album_meta && album_meta->genre) {
        written = write_id3_text_frame(buf + offset, buf_size - offset,
                                       "TCON", album_meta->genre);
        offset += written;
    }

    /* Composer (TCOM) */
    if (track_meta && track_meta->track_composer) {
        written = write_id3_text_frame(buf + offset, buf_size - offset,
                                       "TCOM", track_meta->track_composer);
        offset += written;
    }

    /* ISRC (TSRC) */
    if (track_meta && track_meta->isrc[0]) {
        written = write_id3_text_frame(buf + offset, buf_size - offset,
                                       "TSRC", track_meta->isrc);
        offset += written;
    }

    /* Publisher (TPUB) */
    if (album_meta && album_meta->album_publisher) {
        written = write_id3_text_frame(buf + offset, buf_size - offset,
                                       "TPUB", album_meta->album_publisher);
        offset += written;
    }

    /* If no frames were written, don't create a tag */
    if (offset == 10) {
        return 0;
    }

    /* Write ID3v2.4 header */
    buf[0] = 'I';
    buf[1] = 'D';
    buf[2] = '3';
    buf[3] = 4;  /* Version 2.4 */
    buf[4] = 0;  /* Revision */
    buf[5] = 0;  /* Flags */

    /* Tag size (excluding header) */
    write_syncsafe_int(buf + 6, (uint32_t)(offset - 10));

    return offset;
}

/*============================================================================
 * DSF Sink Context
 *============================================================================*/

typedef struct dsdpipe_sink_dsf_ctx_s {
    /* Configuration */
    char *base_path;                    /**< Base output path */
    bool write_id3;                     /**< Whether to write ID3 tags */
    dsdpipe_track_format_t track_filename_format; /**< Track filename format */

    /* Current state */
    dsf_t *dsf_handle;                  /**< Current DSF file handle */
    dsdpipe_format_t format;           /**< Audio format */
    uint8_t current_track;              /**< Current track number */
    bool track_is_open;                 /**< Whether a track file is open */

    /* Album metadata (cached for ID3 generation) */
    dsdpipe_metadata_t album_metadata; /**< Cached album metadata */
    bool have_album_metadata;           /**< Whether album metadata is set */

    /* Current track metadata */
    dsdpipe_metadata_t track_metadata; /**< Current track metadata */

    /* Statistics */
    uint64_t frames_written;            /**< Total frames written */
    uint64_t bytes_written;             /**< Total bytes written */
    uint64_t tracks_written;            /**< Total tracks written */

    /* State flags */
    bool is_open;                       /**< Whether sink is open */
} dsdpipe_sink_dsf_ctx_t;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Map channel count to DSF channel type
 */
static uint32_t get_dsf_channel_type(uint16_t channel_count)
{
    switch (channel_count) {
        case 1:  return DSF_CHANNEL_TYPE_MONO;
        case 2:  return DSF_CHANNEL_TYPE_STEREO;
        case 3:  return DSF_CHANNEL_TYPE_3_CHANNELS;
        case 4:  return DSF_CHANNEL_TYPE_QUAD;
        case 5:  return DSF_CHANNEL_TYPE_5_CHANNELS;
        case 6:  return DSF_CHANNEL_TYPE_5_1_CHANNELS;
        default: return channel_count;
    }
}

/**
 * @brief Generate output filename for a track
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

    char *full_path = sa_make_path(base_path, NULL, track_name, "dsf");
    sa_free(track_name);
    return full_path;
}

/**
 * @brief Close the current track file if open
 */
static void close_current_track(dsdpipe_sink_dsf_ctx_t *dsf_ctx)
{
    if (!dsf_ctx->track_is_open || !dsf_ctx->dsf_handle) {
        return;
    }

    /* Finalize and close DSF file */
    dsf_finalize(dsf_ctx->dsf_handle);
    dsf_close(dsf_ctx->dsf_handle);
    dsf_free(dsf_ctx->dsf_handle);

    dsf_ctx->dsf_handle = NULL;
    dsf_ctx->track_is_open = false;

    /* Clear track metadata */
    dsdpipe_metadata_free(&dsf_ctx->track_metadata);
    dsdpipe_metadata_init(&dsf_ctx->track_metadata);
}

/*============================================================================
 * Sink Operations
 *============================================================================*/

static int dsf_sink_open(void *ctx, const char *path,
                         const dsdpipe_format_t *format,
                         const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_dsf_ctx_t *dsf_ctx = (dsdpipe_sink_dsf_ctx_t *)ctx;

    if (!dsf_ctx || !path || !format) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store base path */
    dsf_ctx->base_path = dsdpipe_strdup(path);
    if (!dsf_ctx->base_path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Ensure output directory exists */
    if (sa_mkdir_p(path, NULL, 0755) != 0) {
        sa_free(dsf_ctx->base_path);
        dsf_ctx->base_path = NULL;
        return DSDPIPE_ERROR_FILE_CREATE;
    }

    /* Store format */
    dsf_ctx->format = *format;

    /* Initialize statistics */
    dsf_ctx->frames_written = 0;
    dsf_ctx->bytes_written = 0;
    dsf_ctx->tracks_written = 0;

    /* Cache album metadata for ID3 generation */
    if (metadata) {
        int result = dsdpipe_metadata_copy(&dsf_ctx->album_metadata, metadata);
        if (result == DSDPIPE_OK) {
            dsf_ctx->have_album_metadata = true;
        }
    }

    dsf_ctx->is_open = true;
    return DSDPIPE_OK;
}

static void dsf_sink_close(void *ctx)
{
    dsdpipe_sink_dsf_ctx_t *dsf_ctx = (dsdpipe_sink_dsf_ctx_t *)ctx;

    if (!dsf_ctx) {
        return;
    }

    /* Close any open track */
    close_current_track(dsf_ctx);

    /* Free album metadata */
    if (dsf_ctx->have_album_metadata) {
        dsdpipe_metadata_free(&dsf_ctx->album_metadata);
        dsf_ctx->have_album_metadata = false;
    }

    /* Free base path */
    if (dsf_ctx->base_path) {
        sa_free(dsf_ctx->base_path);
        dsf_ctx->base_path = NULL;
    }

    dsf_ctx->is_open = false;
}

static int dsf_sink_track_start(void *ctx, uint8_t track_number,
                                const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_dsf_ctx_t *dsf_ctx = (dsdpipe_sink_dsf_ctx_t *)ctx;
    char *filename = NULL;
    uint32_t dsf_channel_type;
    int result;

    if (!dsf_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsf_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* Close any previously open track */
    close_current_track(dsf_ctx);

    /* Store track metadata for ID3 generation */
    if (metadata) {
        dsdpipe_metadata_copy(&dsf_ctx->track_metadata, metadata);
    }

    dsf_ctx->current_track = track_number;

    /* Generate output filename */
    filename = generate_track_filename(dsf_ctx->base_path, metadata,
                                       dsf_ctx->track_filename_format);
    if (!filename) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Allocate DSF handle */
    result = dsf_alloc(&dsf_ctx->dsf_handle);
    if (result != DSF_SUCCESS) {
        sa_free(filename);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Map channel count to DSF channel type */
    dsf_channel_type = get_dsf_channel_type(dsf_ctx->format.channel_count);

    /* Create DSF file */
    result = dsf_create(dsf_ctx->dsf_handle,
                        filename,
                        dsf_ctx->format.sample_rate,
                        dsf_channel_type,
                        dsf_ctx->format.channel_count,
                        1);  /* bits_per_sample = 1 for DSD */

    sa_free(filename);

    if (result != DSF_SUCCESS) {
        dsf_free(dsf_ctx->dsf_handle);
        dsf_ctx->dsf_handle = NULL;
        return DSDPIPE_ERROR_SINK_OPEN;
    }

    dsf_ctx->track_is_open = true;
    return DSDPIPE_OK;
}

static int dsf_sink_track_end(void *ctx, uint8_t track_number)
{
    dsdpipe_sink_dsf_ctx_t *dsf_ctx = (dsdpipe_sink_dsf_ctx_t *)ctx;
    uint8_t id3_buffer[DSF_ID3_BUFFER_SIZE];
    size_t id3_size;

    if (!dsf_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsf_ctx->track_is_open || !dsf_ctx->dsf_handle) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    (void)track_number;  /* Unused, we track current_track internally */

    /* Write ID3 metadata if enabled */
    if (dsf_ctx->write_id3) {
        id3_size = build_id3_tag(id3_buffer, sizeof(id3_buffer),
                                 dsf_ctx->have_album_metadata ? &dsf_ctx->album_metadata : NULL,
                                 &dsf_ctx->track_metadata);
        if (id3_size > 0) {
            dsf_write_metadata(dsf_ctx->dsf_handle, id3_buffer, id3_size);
        }
    }

    /* Close the track file */
    close_current_track(dsf_ctx);

    dsf_ctx->tracks_written++;
    return DSDPIPE_OK;
}

static int dsf_sink_write_frame(void *ctx, const dsdpipe_buffer_t *buffer)
{
    dsdpipe_sink_dsf_ctx_t *dsf_ctx = (dsdpipe_sink_dsf_ctx_t *)ctx;
    size_t written = 0;
    int result;

    if (!dsf_ctx || !buffer) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsf_ctx->track_is_open || !dsf_ctx->dsf_handle) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* DSF only accepts DSD data, not DST */
    if (buffer->format.type == DSDPIPE_FORMAT_DST) {
        return DSDPIPE_ERROR_UNSUPPORTED;
    }

    /* Write audio data to DSF file */
    /* Note: dsf_write_audio_data expects DSDIFF byte-interleaved format,
     * which is what libsacd/source_sacd provides after DST decoding */
    result = dsf_write_audio_data(dsf_ctx->dsf_handle,
                                  buffer->data,
                                  buffer->size,
                                  &written);

    if (result != DSF_SUCCESS) {
        return DSDPIPE_ERROR_WRITE;
    }

    dsf_ctx->frames_written++;
    dsf_ctx->bytes_written += written;

    return DSDPIPE_OK;
}

static int dsf_sink_finalize(void *ctx)
{
    dsdpipe_sink_dsf_ctx_t *dsf_ctx = (dsdpipe_sink_dsf_ctx_t *)ctx;

    if (!dsf_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Close any open track */
    close_current_track(dsf_ctx);

    return DSDPIPE_OK;
}

static uint32_t dsf_sink_get_capabilities(void *ctx)
{
    (void)ctx;
    /* DSF accepts DSD data and supports metadata (ID3) */
    return DSDPIPE_SINK_CAP_DSD | DSDPIPE_SINK_CAP_METADATA;
}

static void dsf_sink_destroy(void *ctx)
{
    dsdpipe_sink_dsf_ctx_t *dsf_ctx = (dsdpipe_sink_dsf_ctx_t *)ctx;

    if (!dsf_ctx) {
        return;
    }

    dsf_sink_close(ctx);
    sa_free(dsf_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_sink_ops_t s_dsf_sink_ops = {
    .open = dsf_sink_open,
    .close = dsf_sink_close,
    .track_start = dsf_sink_track_start,
    .track_end = dsf_sink_track_end,
    .write_frame = dsf_sink_write_frame,
    .finalize = dsf_sink_finalize,
    .get_capabilities = dsf_sink_get_capabilities,
    .destroy = dsf_sink_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_sink_dsf_create(dsdpipe_sink_t **sink,
                             const dsdpipe_sink_config_t *config)
{
    dsdpipe_sink_t *new_sink;
    dsdpipe_sink_dsf_ctx_t *ctx;

    if (!sink || !config) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    new_sink = (dsdpipe_sink_t *)sa_calloc(1, sizeof(*new_sink));
    if (!new_sink) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    ctx = (dsdpipe_sink_dsf_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        sa_free(new_sink);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Store configuration */
    ctx->write_id3 = config->opts.dsf.write_id3;
    ctx->track_filename_format = config->track_filename_format;

    /* Initialize metadata structures */
    dsdpipe_metadata_init(&ctx->album_metadata);
    dsdpipe_metadata_init(&ctx->track_metadata);

    new_sink->type = DSDPIPE_SINK_DSF;
    new_sink->ops = &s_dsf_sink_ops;
    new_sink->ctx = ctx;
    new_sink->config = *config;
    new_sink->config.path = dsdpipe_strdup(config->path);
    new_sink->caps = dsf_sink_get_capabilities(ctx);
    new_sink->is_open = false;

    *sink = new_sink;
    return DSDPIPE_OK;
}
