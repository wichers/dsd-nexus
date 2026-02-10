/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD ISO source implementation using libsacd
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

#include <libsacd/sacd.h>
#include <libsautil/mem.h>
#include <libsautil/sastring.h>

#include <string.h>
#include <stdio.h>

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum DST frame buffer size */
#define SACD_SOURCE_FRAME_BUFFER_SIZE   (SACD_MAX_DST_SIZE)

/** Text channel to use for metadata (1 = primary language) */
#define SACD_TEXT_CHANNEL               1

/*============================================================================
 * SACD Source Context
 *============================================================================*/

typedef struct dsdpipe_source_sacd_ctx_s {
    /* Configuration */
    dsdpipe_channel_type_t channel_type;   /**< Stereo or multichannel */
    char *path;                             /**< Path to ISO file */

    /* libsacd reader */
    sacd_t *sacd;                           /**< SACD reader context */

    /* Cached format info */
    dsdpipe_format_t format;               /**< Audio format */
    uint8_t track_count;                    /**< Number of tracks */
    frame_format_t frame_format;            /**< DST or DSD format */

    /* Playback state */
    uint8_t current_track;                  /**< Current track (1-based) */
    uint32_t current_frame;                 /**< Current frame within track */
    uint32_t track_index_start;             /**< Start frame of current track */
    uint32_t track_frame_length;            /**< Length of current track in frames */

    /* Frame buffer for reading */
    uint8_t *frame_buffer;                  /**< Buffer for reading frames */

    /* State flags */
    bool is_open;                           /**< Whether source is open */
} dsdpipe_source_sacd_ctx_t;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Maps libsacd channel_t to dsdpipe_channel_type_t
 */
static channel_t map_channel_type(dsdpipe_channel_type_t type)
{
    return (type == DSDPIPE_CHANNEL_MULTICHANNEL) ? MULTI_CHANNEL : TWO_CHANNEL;
}

/**
 * @brief Gets genre string from genre table and index
 */
static const char *get_genre_string(uint8_t genre_table, uint16_t genre_index)
{
    if (genre_table == CATEGORY_GENERAL && genre_index < 256) {
        return album_genre_general[genre_index];
    } else if (genre_table == CATEGORY_JAPANESE && genre_index < 256) {
        return album_genre_japanese[genre_index];
    }
    return NULL;
}

/**
 * @brief Formats ISRC code into a string
 */
static void format_isrc(const area_isrc_t *isrc, char *out, size_t out_size)
{
    if (!isrc || !out || out_size < 13) {
        if (out && out_size > 0) {
            out[0] = '\0';
        }
        return;
    }

    /* Check if ISRC is all zeros (not available) */
    bool is_empty = true;
    for (int i = 0; i < 2 && is_empty; i++) {
        if (isrc->country_code[i] != '\0') is_empty = false;
    }
    for (int i = 0; i < 3 && is_empty; i++) {
        if (isrc->owner_code[i] != '\0') is_empty = false;
    }

    if (is_empty) {
        out[0] = '\0';
        return;
    }

    /* Format: CC-OOO-YY-NNNNN (12 chars total without dashes) */
#ifdef _MSC_VER
    sprintf_s(out, out_size, "%c%c%c%c%c%c%c%c%c%c%c%c",
#else
    snprintf(out, out_size, "%c%c%c%c%c%c%c%c%c%c%c%c",
#endif
             isrc->country_code[0], isrc->country_code[1],
             isrc->owner_code[0], isrc->owner_code[1], isrc->owner_code[2],
             isrc->recording_year[0], isrc->recording_year[1],
             isrc->designation_code[0], isrc->designation_code[1],
             isrc->designation_code[2], isrc->designation_code[3],
             isrc->designation_code[4]);
}

/*============================================================================
 * Source Operations
 *============================================================================*/

static int sacd_source_open(void *ctx, const char *path)
{
    dsdpipe_source_sacd_ctx_t *sacd_ctx = (dsdpipe_source_sacd_ctx_t *)ctx;
    int result;
    channel_t ch_type;
    uint16_t channel_count;
    uint32_t sample_rate;

    if (!sacd_ctx || !path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store path */
    sacd_ctx->path = dsdpipe_strdup(path);
    if (!sacd_ctx->path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Create SACD reader */
    sacd_ctx->sacd = sacd_create();
    if (!sacd_ctx->sacd) {
        sa_free(sacd_ctx->path);
        sacd_ctx->path = NULL;
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Initialize reader - open the ISO file */
    result = sacd_init(sacd_ctx->sacd, path, 1, 1);
    if (result != SACD_OK) {
        sacd_destroy(sacd_ctx->sacd);
        sacd_ctx->sacd = NULL;
        sa_free(sacd_ctx->path);
        sacd_ctx->path = NULL;
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    /* Select the requested channel type (stereo or multichannel) */
    ch_type = map_channel_type(sacd_ctx->channel_type);
    result = sacd_select_channel_type(sacd_ctx->sacd, ch_type);
    if (result != SACD_OK) {
        /* Requested area not available on disc */
        sacd_close(sacd_ctx->sacd);
        sacd_destroy(sacd_ctx->sacd);
        sacd_ctx->sacd = NULL;
        sa_free(sacd_ctx->path);
        sacd_ctx->path = NULL;
        return DSDPIPE_ERROR_UNSUPPORTED;
    }

    /* Get track count */
    result = sacd_get_track_count(sacd_ctx->sacd, &sacd_ctx->track_count);
    if (result != SACD_OK) {
        sacd_close(sacd_ctx->sacd);
        sacd_destroy(sacd_ctx->sacd);
        sacd_ctx->sacd = NULL;
        sa_free(sacd_ctx->path);
        sacd_ctx->path = NULL;
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    /* Get audio format information */
    result = sacd_get_area_channel_count(sacd_ctx->sacd, &channel_count);
    if (result != SACD_OK) {
        sacd_close(sacd_ctx->sacd);
        sacd_destroy(sacd_ctx->sacd);
        sacd_ctx->sacd = NULL;
        sa_free(sacd_ctx->path);
        sacd_ctx->path = NULL;
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    result = sacd_get_area_sample_frequency(sacd_ctx->sacd, &sample_rate);
    if (result != SACD_OK) {
        sacd_close(sacd_ctx->sacd);
        sacd_destroy(sacd_ctx->sacd);
        sacd_ctx->sacd = NULL;
        sa_free(sacd_ctx->path);
        sacd_ctx->path = NULL;
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    result = sacd_get_area_frame_format_enum(sacd_ctx->sacd, &sacd_ctx->frame_format);
    if (result != SACD_OK) {
        sacd_close(sacd_ctx->sacd);
        sacd_destroy(sacd_ctx->sacd);
        sacd_ctx->sacd = NULL;
        sa_free(sacd_ctx->path);
        sacd_ctx->path = NULL;
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    /* Fill in format structure */
    sacd_ctx->format.type = (sacd_ctx->frame_format == FRAME_FORMAT_DST)
                            ? DSDPIPE_FORMAT_DST
                            : DSDPIPE_FORMAT_DSD_RAW;
    sacd_ctx->format.sample_rate = sample_rate;
    sacd_ctx->format.channel_count = channel_count;
    sacd_ctx->format.bits_per_sample = 1;  /* DSD is 1-bit */
    sacd_ctx->format.frame_rate = SACD_FRAMES_PER_SEC;

    /* Allocate frame buffer */
    sacd_ctx->frame_buffer = sa_malloc(SACD_SOURCE_FRAME_BUFFER_SIZE);
    if (!sacd_ctx->frame_buffer) {
        sacd_close(sacd_ctx->sacd);
        sacd_destroy(sacd_ctx->sacd);
        sacd_ctx->sacd = NULL;
        sa_free(sacd_ctx->path);
        sacd_ctx->path = NULL;
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    sacd_ctx->is_open = true;
    sacd_ctx->current_track = 0;
    sacd_ctx->current_frame = 0;

    return DSDPIPE_OK;
}

static void sacd_source_close(void *ctx)
{
    dsdpipe_source_sacd_ctx_t *sacd_ctx = (dsdpipe_source_sacd_ctx_t *)ctx;

    if (!sacd_ctx) {
        return;
    }

    if (sacd_ctx->frame_buffer) {
        sa_free(sacd_ctx->frame_buffer);
        sacd_ctx->frame_buffer = NULL;
    }

    if (sacd_ctx->sacd) {
        sacd_close(sacd_ctx->sacd);
        sacd_destroy(sacd_ctx->sacd);
        sacd_ctx->sacd = NULL;
    }

    if (sacd_ctx->path) {
        sa_free(sacd_ctx->path);
        sacd_ctx->path = NULL;
    }

    sacd_ctx->is_open = false;
}

static int sacd_source_get_track_count(void *ctx, uint8_t *count)
{
    dsdpipe_source_sacd_ctx_t *sacd_ctx = (dsdpipe_source_sacd_ctx_t *)ctx;

    if (!sacd_ctx || !count) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!sacd_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    *count = sacd_ctx->track_count;
    return DSDPIPE_OK;
}

static int sacd_source_get_format(void *ctx, dsdpipe_format_t *format)
{
    dsdpipe_source_sacd_ctx_t *sacd_ctx = (dsdpipe_source_sacd_ctx_t *)ctx;

    if (!sacd_ctx || !format) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!sacd_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    *format = sacd_ctx->format;
    return DSDPIPE_OK;
}

static int sacd_source_seek_track(void *ctx, uint8_t track_number)
{
    dsdpipe_source_sacd_ctx_t *sacd_ctx = (dsdpipe_source_sacd_ctx_t *)ctx;
    int result;

    if (!sacd_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!sacd_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (track_number == 0 || track_number > sacd_ctx->track_count) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    /* Get track timing information */
    result = sacd_get_track_index_start(sacd_ctx->sacd, track_number, 1,
                                         &sacd_ctx->track_index_start);
    if (result != SACD_OK) {
        return DSDPIPE_ERROR_READ;
    }

    result = sacd_get_track_frame_length(sacd_ctx->sacd, track_number,
                                                &sacd_ctx->track_frame_length);
    if (result != SACD_OK) {
        return DSDPIPE_ERROR_READ;
    }

    sacd_ctx->current_track = track_number;
    sacd_ctx->current_frame = 0;

    return DSDPIPE_OK;
}

static int sacd_source_read_frame(void *ctx, dsdpipe_buffer_t *buffer)
{
    dsdpipe_source_sacd_ctx_t *sacd_ctx = (dsdpipe_source_sacd_ctx_t *)ctx;
    int result;
    uint32_t frames_to_read = 1;
    uint16_t frame_size = 0;
    uint32_t absolute_frame;

    if (!sacd_ctx || !buffer) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!sacd_ctx->is_open) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (sacd_ctx->current_track == 0) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    /* Check for end of track */
    if (sacd_ctx->current_frame >= sacd_ctx->track_frame_length) {
        buffer->flags = DSDPIPE_BUF_FLAG_TRACK_END | DSDPIPE_BUF_FLAG_EOF;
        buffer->size = 0;
        return 1;  /* EOF indicator */
    }

    /* Calculate absolute frame number */
    absolute_frame = sacd_ctx->track_index_start + sacd_ctx->current_frame;

    /* Read one frame of audio data */
    result = sacd_get_sound_data(sacd_ctx->sacd,
                                        sacd_ctx->frame_buffer,
                                        absolute_frame,
                                        &frames_to_read,
                                        &frame_size);

    if (result != SACD_OK || frames_to_read == 0) {
        return DSDPIPE_ERROR_READ;
    }

    /* Ensure frame fits in output buffer */
    if (frame_size > buffer->capacity) {
        return DSDPIPE_ERROR_INTERNAL;
    }

    /* Copy frame data to output buffer */
#ifdef _MSC_VER
    memcpy_s(buffer->data, buffer->capacity, sacd_ctx->frame_buffer, frame_size);
#else
    memcpy(buffer->data, sacd_ctx->frame_buffer, frame_size);
#endif

    buffer->size = frame_size;
    buffer->format = sacd_ctx->format;
    buffer->track_number = sacd_ctx->current_track;
    buffer->frame_number = sacd_ctx->current_frame;
    buffer->flags = 0;

    /* Set track start flag for first frame */
    if (sacd_ctx->current_frame == 0) {
        buffer->flags |= DSDPIPE_BUF_FLAG_TRACK_START;
    }

    /* Set track end flag for last frame */
    if (sacd_ctx->current_frame == sacd_ctx->track_frame_length - 1) {
        buffer->flags |= DSDPIPE_BUF_FLAG_TRACK_END;
    }

    sacd_ctx->current_frame++;

    return DSDPIPE_OK;
}

static int sacd_source_get_album_metadata(void *ctx, dsdpipe_metadata_t *metadata)
{
    dsdpipe_source_sacd_ctx_t *sacd_ctx = (dsdpipe_source_sacd_ctx_t *)ctx;
    const char *text = NULL;
    uint16_t year;
    uint8_t month, day;
    uint16_t disc_count, disc_seq;
    uint8_t genre_table;
    uint16_t genre_index;
    int result;

    if (!sacd_ctx || !metadata) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!sacd_ctx->is_open || !sacd_ctx->sacd) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* Initialize metadata structure */
    dsdpipe_metadata_init(metadata);

    /* Album title */
    result = sacd_get_album_text(sacd_ctx->sacd, SACD_TEXT_CHANNEL,
                                        ALBUM_TEXT_TYPE_TITLE, &text);
    if (result == SACD_OK && text && text[0]) {
        dsdpipe_metadata_set_string(&metadata->album_title, text);
    }

    /* Album artist */
    result = sacd_get_album_text(sacd_ctx->sacd, SACD_TEXT_CHANNEL,
                                        ALBUM_TEXT_TYPE_ARTIST, &text);
    if (result == SACD_OK && text && text[0]) {
        dsdpipe_metadata_set_string(&metadata->album_artist, text);
    }

    /* Publisher */
    result = sacd_get_album_text(sacd_ctx->sacd, SACD_TEXT_CHANNEL,
                                        ALBUM_TEXT_TYPE_PUBLISHER, &text);
    if (result == SACD_OK && text && text[0]) {
        dsdpipe_metadata_set_string(&metadata->album_publisher, text);
    }

    /* Copyright */
    result = sacd_get_album_text(sacd_ctx->sacd, SACD_TEXT_CHANNEL,
                                        ALBUM_TEXT_TYPE_COPYRIGHT, &text);
    if (result == SACD_OK && text && text[0]) {
        dsdpipe_metadata_set_string(&metadata->album_copyright, text);
    }

    /* Catalog number */
    result = sacd_get_album_catalog_num(sacd_ctx->sacd, &text);
    if (result == SACD_OK && text && text[0]) {
        dsdpipe_metadata_set_string(&metadata->catalog_number, text);
    }

    /* Date */
    result = sacd_get_disc_date(sacd_ctx->sacd, &year, &month, &day);
    if (result == SACD_OK) {
        metadata->year = year;
        metadata->month = month;
        metadata->day = day;
    }

    /* Genre */
    result = sacd_get_disc_genre(sacd_ctx->sacd, 1, &genre_table, &genre_index);
    if (result == SACD_OK) {
        const char *genre_str = get_genre_string(genre_table, genre_index);
        if (genre_str) {
            dsdpipe_metadata_set_string(&metadata->genre, genre_str);
        }
    }

    /* Track total */
    metadata->track_total = sacd_ctx->track_count;

    /* Disc information */
    result = sacd_get_album_disc_count(sacd_ctx->sacd, &disc_count);
    if (result == SACD_OK) {
        metadata->disc_total = disc_count;
    }

    result = sacd_get_disc_sequence_num(sacd_ctx->sacd, &disc_seq);
    if (result == SACD_OK) {
        metadata->disc_number = disc_seq;
    }

    return DSDPIPE_OK;
}

static int sacd_source_get_track_metadata(void *ctx, uint8_t track_number,
                                          dsdpipe_metadata_t *metadata)
{
    dsdpipe_source_sacd_ctx_t *sacd_ctx = (dsdpipe_source_sacd_ctx_t *)ctx;
    const char *text = NULL;
    area_isrc_t isrc;
    uint32_t frame_length;
    uint8_t genre_table;
    uint16_t genre_index;
    int result;

    if (!sacd_ctx || !metadata) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!sacd_ctx->is_open || !sacd_ctx->sacd) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (track_number == 0 || track_number > sacd_ctx->track_count) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    /* Start with album metadata (album_title, album_artist, year, etc.) */
    result = sacd_source_get_album_metadata(ctx, metadata);
    if (result != DSDPIPE_OK) {
        return result;
    }

    /* Track title */
    result = sacd_get_track_text(sacd_ctx->sacd, track_number,
                                        SACD_TEXT_CHANNEL, TRACK_TYPE_TITLE, &text);
    if (result == SACD_OK && text && text[0]) {
        dsdpipe_metadata_set_string(&metadata->track_title, text);
    }

    /* Track performer */
    result = sacd_get_track_text(sacd_ctx->sacd, track_number,
                                        SACD_TEXT_CHANNEL, TRACK_TYPE_PERFORMER, &text);
    if (result == SACD_OK && text && text[0]) {
        dsdpipe_metadata_set_string(&metadata->track_performer, text);
    }

    /* Track composer */
    result = sacd_get_track_text(sacd_ctx->sacd, track_number,
                                        SACD_TEXT_CHANNEL, TRACK_TYPE_COMPOSER, &text);
    if (result == SACD_OK && text && text[0]) {
        dsdpipe_metadata_set_string(&metadata->track_composer, text);
    }

    /* Track arranger */
    result = sacd_get_track_text(sacd_ctx->sacd, track_number,
                                        SACD_TEXT_CHANNEL, TRACK_TYPE_ARRANGER, &text);
    if (result == SACD_OK && text && text[0]) {
        dsdpipe_metadata_set_string(&metadata->track_arranger, text);
    }

    /* Track songwriter */
    result = sacd_get_track_text(sacd_ctx->sacd, track_number,
                                        SACD_TEXT_CHANNEL, TRACK_TYPE_SONGWRITER, &text);
    if (result == SACD_OK && text && text[0]) {
        dsdpipe_metadata_set_string(&metadata->track_songwriter, text);
    }

    /* Track message */
    result = sacd_get_track_text(sacd_ctx->sacd, track_number,
                                        SACD_TEXT_CHANNEL, TRACK_TYPE_MESSAGE, &text);
    if (result == SACD_OK && text && text[0]) {
        dsdpipe_metadata_set_string(&metadata->track_message, text);
    }

    /* ISRC */
    result = sacd_get_track_isrc_num(sacd_ctx->sacd, track_number, &isrc);
    if (result == SACD_OK) {
        format_isrc(&isrc, metadata->isrc, sizeof(metadata->isrc));
    }

    /* Track number and total */
    metadata->track_number = track_number;
    metadata->track_total = sacd_ctx->track_count;

    /* Track start position */
    {
        uint32_t index_start;
        result = sacd_get_track_index_start(sacd_ctx->sacd, track_number, 1, &index_start);
        if (result == SACD_OK) {
            metadata->start_frame = index_start;
        }
    }

    /* Track duration */
    result = sacd_get_track_frame_length(sacd_ctx->sacd, track_number, &frame_length);
    if (result == SACD_OK) {
        metadata->duration_frames = frame_length;
        metadata->duration_seconds = (double)frame_length / SACD_FRAMES_PER_SEC;
    }

    /* Track genre */
    result = sacd_get_track_genre(sacd_ctx->sacd, track_number,
                                         &genre_table, &genre_index);
    if (result == SACD_OK) {
        const char *genre_str = get_genre_string(genre_table, genre_index);
        if (genre_str) {
            dsdpipe_metadata_set_string(&metadata->genre, genre_str);
        }
    }

    /* Copy album-level info that applies to tracks */
    result = sacd_get_album_disc_count(sacd_ctx->sacd, &metadata->disc_total);
    (void)result;  /* Ignore error */
    result = sacd_get_disc_sequence_num(sacd_ctx->sacd, &metadata->disc_number);
    (void)result;  /* Ignore error */

    return DSDPIPE_OK;
}

static int sacd_source_get_track_frames(void *ctx, uint8_t track_number,
                                        uint64_t *frames)
{
    dsdpipe_source_sacd_ctx_t *sacd_ctx = (dsdpipe_source_sacd_ctx_t *)ctx;
    uint32_t frame_length;
    int result;

    if (!sacd_ctx || !frames) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!sacd_ctx->is_open || !sacd_ctx->sacd) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (track_number == 0 || track_number > sacd_ctx->track_count) {
        return DSDPIPE_ERROR_TRACK_NOT_FOUND;
    }

    result = sacd_get_track_frame_length(sacd_ctx->sacd, track_number, &frame_length);
    if (result != SACD_OK) {
        return DSDPIPE_ERROR_READ;
    }

    *frames = frame_length;
    return DSDPIPE_OK;
}

static void sacd_source_destroy(void *ctx)
{
    dsdpipe_source_sacd_ctx_t *sacd_ctx = (dsdpipe_source_sacd_ctx_t *)ctx;

    if (!sacd_ctx) {
        return;
    }

    sacd_source_close(ctx);
    sa_free(sacd_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_source_ops_t s_sacd_source_ops = {
    .open = sacd_source_open,
    .close = sacd_source_close,
    .get_track_count = sacd_source_get_track_count,
    .get_format = sacd_source_get_format,
    .seek_track = sacd_source_seek_track,
    .read_frame = sacd_source_read_frame,
    .get_album_metadata = sacd_source_get_album_metadata,
    .get_track_metadata = sacd_source_get_track_metadata,
    .get_track_frames = sacd_source_get_track_frames,
    .destroy = sacd_source_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_source_sacd_create(dsdpipe_source_t *source,
                                dsdpipe_channel_type_t channel_type)
{
    dsdpipe_source_sacd_ctx_t *ctx;

    if (!source) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    ctx = (dsdpipe_source_sacd_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    ctx->channel_type = channel_type;

    source->type = DSDPIPE_SOURCE_SACD;
    source->ops = &s_sacd_source_ops;
    source->ctx = ctx;
    source->is_open = false;

    return DSDPIPE_OK;
}
