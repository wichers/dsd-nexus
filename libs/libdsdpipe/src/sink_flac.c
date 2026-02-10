/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief FLAC sink implementation using libFLAC
 * This sink converts PCM data to FLAC files using the libFLAC library.
 * One FLAC file is created per track with optional Vorbis comment metadata.
 * Supported input formats:
 *   - DSDPIPE_FORMAT_PCM_INT16
 *   - DSDPIPE_FORMAT_PCM_INT24
 *   - DSDPIPE_FORMAT_PCM_INT32
 *   - DSDPIPE_FORMAT_PCM_FLOAT32
 *   - DSDPIPE_FORMAT_PCM_FLOAT64
 * Output formats:
 *   - 16-bit FLAC (bits_per_sample=16)
 *   - 24-bit FLAC (bits_per_sample=24)
 * NOTE: This sink requires PCM data. The pipeline should have a DSD-to-PCM
 *       transform inserted when the source provides DSD/DST data.
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

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <libsautil/mem.h>
#include <libsautil/sa_path.h>
#include <libsautil/sastring.h>

#ifdef HAVE_LIBFLAC
#include <FLAC/stream_encoder.h>
#include <FLAC/metadata.h>
#endif

/*============================================================================
 * Constants
 *============================================================================*/

#define FLAC_SINK_MAX_CHANNELS       8
#define FLAC_SINK_SAMPLE_BUFFER_SIZE 8192

/*============================================================================
 * FLAC Sink Context
 *============================================================================*/

typedef struct dsdpipe_sink_flac_ctx_s {
    /* Configuration */
    char *base_path;            /**< Base output path (without extension) */
    int bit_depth;              /**< Requested output bit depth (16, 24) */
    int compression;            /**< FLAC compression level (0-8) */
    int sample_rate;            /**< Output sample rate (derived from format) */
    dsdpipe_track_format_t track_filename_format; /**< Track filename format */

    /* Source format */
    dsdpipe_format_t format;   /**< Source audio format */

    /* Track state */
    uint8_t current_track;      /**< Current track number */
    bool encoder_active;        /**< Whether encoder is currently active */

#ifdef HAVE_LIBFLAC
    /* FLAC encoder instance */
    FLAC__StreamEncoder *encoder;

    /* Metadata for current track */
    FLAC__StreamMetadata *vorbis_comment;
#endif

    /* Conversion buffer (for converting input PCM to FLAC__int32) */
    int32_t *conv_buffer;       /**< Conversion buffer */
    size_t conv_buffer_size;    /**< Conversion buffer size (samples) */

    /* Statistics */
    uint64_t frames_written;    /**< Total frames written (all tracks) */
    uint64_t samples_written;   /**< Total samples written (all tracks) */
    uint64_t tracks_written;    /**< Number of tracks completed */
    uint64_t track_samples;     /**< Samples written for current track */
} dsdpipe_sink_flac_ctx_t;

#ifdef HAVE_LIBFLAC

/*============================================================================
 * Helper: Convert PCM data to FLAC__int32
 *
 * libFLAC expects samples as signed 32-bit integers, with the actual sample
 * value in the lower bits according to bits_per_sample.
 *============================================================================*/

/**
 * @brief Convert int16 samples to FLAC__int32
 */
static void convert_int16_to_int32(const int16_t *src, int32_t *dst, size_t samples)
{
    for (size_t i = 0; i < samples; i++) {
        dst[i] = (int32_t)src[i];
    }
}

/**
 * @brief Convert int24 samples (packed, 3 bytes each) to FLAC__int32
 *
 * Assumes little-endian byte order and packed 24-bit samples.
 */
static void convert_int24_to_int32(const uint8_t *src, int32_t *dst, size_t samples)
{
    for (size_t i = 0; i < samples; i++) {
        /* Read 3 bytes as little-endian signed 24-bit */
        int32_t val = (int32_t)(src[0] | (src[1] << 8) | (src[2] << 16));
        /* Sign extend from 24-bit to 32-bit */
        if (val & 0x800000) {
            val |= (int32_t)0xFF000000;
        }
        dst[i] = val;
        src += 3;
    }
}

/**
 * @brief Convert int32 samples to FLAC__int32 (scale to 24-bit)
 */
static void convert_int32_to_int24(const int32_t *src, int32_t *dst, size_t samples)
{
    for (size_t i = 0; i < samples; i++) {
        /* Scale from 32-bit to 24-bit by shifting right 8 bits */
        dst[i] = src[i] >> 8;
    }
}

/**
 * @brief Convert float32 samples to FLAC__int32
 */
static void convert_float32_to_int32(const float *src, int32_t *dst,
                                     size_t samples, int bit_depth)
{
    if (bit_depth == 16) {
        const float scale = 32767.0f;
        for (size_t i = 0; i < samples; i++) {
            float val = src[i] * scale;
            if (val > 32767.0f) val = 32767.0f;
            if (val < -32768.0f) val = -32768.0f;
            dst[i] = (int32_t)val;
        }
    } else {
        /* 24-bit */
        const float scale = 8388607.0f;
        for (size_t i = 0; i < samples; i++) {
            float val = src[i] * scale;
            if (val > 8388607.0f) val = 8388607.0f;
            if (val < -8388608.0f) val = -8388608.0f;
            dst[i] = (int32_t)val;
        }
    }
}

/**
 * @brief Convert float64 samples to FLAC__int32
 */
static void convert_float64_to_int32(const double *src, int32_t *dst,
                                     size_t samples, int bit_depth)
{
    if (bit_depth == 16) {
        const double scale = 32767.0;
        for (size_t i = 0; i < samples; i++) {
            double val = src[i] * scale;
            if (val > 32767.0) val = 32767.0;
            if (val < -32768.0) val = -32768.0;
            dst[i] = (int32_t)val;
        }
    } else {
        /* 24-bit */
        const double scale = 8388607.0;
        for (size_t i = 0; i < samples; i++) {
            double val = src[i] * scale;
            if (val > 8388607.0) val = 8388607.0;
            if (val < -8388608.0) val = -8388608.0;
            dst[i] = (int32_t)val;
        }
    }
}

/*============================================================================
 * Helper: Get bytes per sample for a PCM format
 *============================================================================*/

static size_t get_bytes_per_sample(dsdpipe_audio_format_t type)
{
    switch (type) {
        case DSDPIPE_FORMAT_PCM_INT16:   return 2;
        case DSDPIPE_FORMAT_PCM_INT24:   return 3;
        case DSDPIPE_FORMAT_PCM_INT32:   return 4;
        case DSDPIPE_FORMAT_PCM_FLOAT32: return 4;
        case DSDPIPE_FORMAT_PCM_FLOAT64: return 8;
        default: return 0;
    }
}

/*============================================================================
 * Helper: Generate unique track filename
 *============================================================================*/

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

    char *full_path = sa_make_path(base_path, NULL, track_name, "flac");
    sa_free(track_name);
    return full_path;
}

/*============================================================================
 * Helper: Ensure conversion buffer is large enough
 *============================================================================*/

static int ensure_conv_buffer(dsdpipe_sink_flac_ctx_t *ctx, size_t samples)
{
    if (ctx->conv_buffer_size >= samples) {
        return DSDPIPE_OK;
    }

    /* Allocate with some extra room to avoid frequent reallocations */
    size_t new_size = samples + (samples / 4);
    if (new_size < FLAC_SINK_SAMPLE_BUFFER_SIZE) {
        new_size = FLAC_SINK_SAMPLE_BUFFER_SIZE;
    }

    int32_t *new_buffer = (int32_t *)sa_realloc(ctx->conv_buffer,
                                                 new_size * sizeof(int32_t));
    if (!new_buffer) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    ctx->conv_buffer = new_buffer;
    ctx->conv_buffer_size = new_size;
    return DSDPIPE_OK;
}

/*============================================================================
 * Helper: Add Vorbis comment entry
 *============================================================================*/

static int add_vorbis_comment(FLAC__StreamMetadata *vc, const char *name,
                               const char *value)
{
    if (!value || value[0] == '\0') {
        return DSDPIPE_OK;  /* Skip empty values */
    }

    FLAC__StreamMetadata_VorbisComment_Entry entry;
    if (!FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(
            &entry, name, value)) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    if (!FLAC__metadata_object_vorbiscomment_append_comment(vc, entry, false)) {
        /* entry was not copied, so we don't need to free it on failure */
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    return DSDPIPE_OK;
}

/*============================================================================
 * Helper: Build Vorbis comment metadata from dsdpipe_metadata_t
 *============================================================================*/

static FLAC__StreamMetadata *build_vorbis_comments(
    const dsdpipe_metadata_t *metadata,
    uint8_t track_number)
{
    FLAC__StreamMetadata *vc = FLAC__metadata_object_new(
        FLAC__METADATA_TYPE_VORBIS_COMMENT);
    if (!vc) {
        return NULL;
    }

    if (!metadata) {
        return vc;
    }

    /* Album-level tags */
    add_vorbis_comment(vc, "ALBUM", metadata->album_title);
    add_vorbis_comment(vc, "ALBUMARTIST", metadata->album_artist);
    add_vorbis_comment(vc, "GENRE", metadata->genre);
    add_vorbis_comment(vc, "PUBLISHER", metadata->album_publisher);
    add_vorbis_comment(vc, "COPYRIGHT", metadata->album_copyright);

    /* Year */
    if (metadata->year > 0) {
        char year_str[8];
        snprintf(year_str, sizeof(year_str), "%u", metadata->year);
        add_vorbis_comment(vc, "DATE", year_str);
    }

    /* Track-level tags */
    add_vorbis_comment(vc, "TITLE", metadata->track_title);
    add_vorbis_comment(vc, "ARTIST", metadata->track_performer);
    add_vorbis_comment(vc, "COMPOSER", metadata->track_composer);
    add_vorbis_comment(vc, "ARRANGER", metadata->track_arranger);
    add_vorbis_comment(vc, "LYRICIST", metadata->track_songwriter);
    add_vorbis_comment(vc, "COMMENT", metadata->track_message);

    /* ISRC */
    if (metadata->isrc[0] != '\0') {
        add_vorbis_comment(vc, "ISRC", metadata->isrc);
    }

    /* Track number */
    if (track_number > 0 || metadata->track_number > 0) {
        char track_str[16];
        uint8_t tn = (track_number > 0) ? track_number : metadata->track_number;
        if (metadata->track_total > 0) {
            snprintf(track_str, sizeof(track_str), "%u/%u", tn, metadata->track_total);
        } else {
            snprintf(track_str, sizeof(track_str), "%u", tn);
        }
        add_vorbis_comment(vc, "TRACKNUMBER", track_str);
    }

    /* Disc number */
    if (metadata->disc_number > 0) {
        char disc_str[16];
        if (metadata->disc_total > 0) {
            snprintf(disc_str, sizeof(disc_str), "%u/%u",
                     metadata->disc_number, metadata->disc_total);
        } else {
            snprintf(disc_str, sizeof(disc_str), "%u", metadata->disc_number);
        }
        add_vorbis_comment(vc, "DISCNUMBER", disc_str);
    }

    return vc;
}

/*============================================================================
 * Helper: Close encoder if active
 *============================================================================*/

static void close_encoder(dsdpipe_sink_flac_ctx_t *ctx)
{
    if (ctx->encoder) {
        if (ctx->encoder_active) {
            FLAC__stream_encoder_finish(ctx->encoder);
            ctx->encoder_active = false;
        }
        FLAC__stream_encoder_delete(ctx->encoder);
        ctx->encoder = NULL;
    }

    if (ctx->vorbis_comment) {
        FLAC__metadata_object_delete(ctx->vorbis_comment);
        ctx->vorbis_comment = NULL;
    }
}

/*============================================================================
 * Sink Operations
 *============================================================================*/

static int flac_sink_open(void *ctx, const char *path,
                          const dsdpipe_format_t *format,
                          const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_flac_ctx_t *flac_ctx = (dsdpipe_sink_flac_ctx_t *)ctx;

    if (!flac_ctx || !path || !format) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store base path */
    flac_ctx->base_path = dsdpipe_strdup(path);
    if (!flac_ctx->base_path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Ensure output directory exists */
    if (sa_mkdir_p(path, NULL, 0755) != 0) {
        sa_freep(&flac_ctx->base_path);
        return DSDPIPE_ERROR_FILE_CREATE;
    }

    flac_ctx->format = *format;
    flac_ctx->frames_written = 0;
    flac_ctx->samples_written = 0;
    flac_ctx->tracks_written = 0;
    flac_ctx->encoder_active = false;
    flac_ctx->encoder = NULL;
    flac_ctx->vorbis_comment = NULL;

    /* Determine output sample rate */
    if (format->sample_rate > 100000) {
        /* Likely DSD rate, convert to typical PCM rate */
        flac_ctx->sample_rate = (int)(format->sample_rate / 32);
    } else {
        flac_ctx->sample_rate = (int)format->sample_rate;
    }

    /* Validate bit depth - FLAC supports up to 24-bit */
    if (flac_ctx->bit_depth != 16 && flac_ctx->bit_depth != 24) {
        flac_ctx->bit_depth = 24;  /* Default to 24-bit */
    }

    /* Validate compression level (0-8) */
    if (flac_ctx->compression < 0 || flac_ctx->compression > 8) {
        flac_ctx->compression = 5;  /* Default compression */
    }

    /* Allocate initial conversion buffer */
    int ret = ensure_conv_buffer(flac_ctx, FLAC_SINK_SAMPLE_BUFFER_SIZE);
    if (ret != DSDPIPE_OK) {
        sa_freep(&flac_ctx->base_path);
        return ret;
    }

    (void)metadata;  /* Album metadata stored for per-track use */

    return DSDPIPE_OK;
}

static void flac_sink_close(void *ctx)
{
    dsdpipe_sink_flac_ctx_t *flac_ctx = (dsdpipe_sink_flac_ctx_t *)ctx;

    if (!flac_ctx) {
        return;
    }

    /* Close any active encoder */
    close_encoder(flac_ctx);

    /* Free resources */
    sa_freep(&flac_ctx->base_path);

    if (flac_ctx->conv_buffer) {
        sa_freep(&flac_ctx->conv_buffer);
        flac_ctx->conv_buffer_size = 0;
    }
}

static int flac_sink_track_start(void *ctx, uint8_t track_number,
                                  const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_flac_ctx_t *flac_ctx = (dsdpipe_sink_flac_ctx_t *)ctx;

    if (!flac_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Close previous encoder if still active */
    close_encoder(flac_ctx);

    flac_ctx->current_track = track_number;
    flac_ctx->track_samples = 0;

    /* Generate unique output filename for this track */
    char *output_path = generate_track_filename(flac_ctx->base_path, metadata,
                                                flac_ctx->track_filename_format);
    if (!output_path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Create FLAC encoder */
    flac_ctx->encoder = FLAC__stream_encoder_new();
    if (!flac_ctx->encoder) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Configure encoder */
    FLAC__stream_encoder_set_channels(flac_ctx->encoder,
                                       flac_ctx->format.channel_count);
    FLAC__stream_encoder_set_bits_per_sample(flac_ctx->encoder,
                                              (unsigned)flac_ctx->bit_depth);
    FLAC__stream_encoder_set_sample_rate(flac_ctx->encoder,
                                          (unsigned)flac_ctx->sample_rate);
    FLAC__stream_encoder_set_compression_level(flac_ctx->encoder,
                                                (unsigned)flac_ctx->compression);

    /* Enable verify for debugging (can be disabled for production) */
    FLAC__stream_encoder_set_verify(flac_ctx->encoder, false);

    /* Set total samples if known (enables seeking) */
    FLAC__stream_encoder_set_total_samples_estimate(flac_ctx->encoder, 0);

    /* Build and set Vorbis comment metadata */
    flac_ctx->vorbis_comment = build_vorbis_comments(metadata, track_number);
    if (flac_ctx->vorbis_comment) {
        FLAC__StreamMetadata *metadata_array[1] = { flac_ctx->vorbis_comment };
        FLAC__stream_encoder_set_metadata(flac_ctx->encoder, metadata_array, 1);
    }

    /* Initialize encoder with file output */
    FLAC__StreamEncoderInitStatus init_status;
    init_status = FLAC__stream_encoder_init_file(flac_ctx->encoder,
                                                  output_path,
                                                  NULL,  /* progress callback */
                                                  NULL); /* client data */
    sa_free(output_path);

    if (init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        close_encoder(flac_ctx);
        return DSDPIPE_ERROR_FILE_CREATE;
    }

    flac_ctx->encoder_active = true;

    return DSDPIPE_OK;
}

static int flac_sink_track_end(void *ctx, uint8_t track_number)
{
    dsdpipe_sink_flac_ctx_t *flac_ctx = (dsdpipe_sink_flac_ctx_t *)ctx;

    if (!flac_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    (void)track_number;

    /* Finalize and close the encoder for this track */
    if (flac_ctx->encoder && flac_ctx->encoder_active) {
        FLAC__bool ok = FLAC__stream_encoder_finish(flac_ctx->encoder);
        flac_ctx->encoder_active = false;

        if (!ok) {
            /* Check encoder state for error details */
            close_encoder(flac_ctx);
            return DSDPIPE_ERROR_FILE_WRITE;
        }

        flac_ctx->tracks_written++;
    }

    /* Clean up encoder resources */
    close_encoder(flac_ctx);

    return DSDPIPE_OK;
}

static int flac_sink_write_frame(void *ctx, const dsdpipe_buffer_t *buffer)
{
    dsdpipe_sink_flac_ctx_t *flac_ctx = (dsdpipe_sink_flac_ctx_t *)ctx;

    if (!flac_ctx || !buffer) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!flac_ctx->encoder || !flac_ctx->encoder_active) {
        return DSDPIPE_ERROR_INVALID_STATE;
    }

    /* Validate that we received PCM data */
    dsdpipe_audio_format_t type = buffer->format.type;
    if (type != DSDPIPE_FORMAT_PCM_INT16 &&
        type != DSDPIPE_FORMAT_PCM_INT24 &&
        type != DSDPIPE_FORMAT_PCM_INT32 &&
        type != DSDPIPE_FORMAT_PCM_FLOAT32 &&
        type != DSDPIPE_FORMAT_PCM_FLOAT64) {
        /* Non-PCM data received - pipeline should have inserted DSD2PCM transform */
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Calculate number of samples (total, all channels interleaved) */
    size_t bytes_per_sample = get_bytes_per_sample(type);
    if (bytes_per_sample == 0) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    size_t total_samples = buffer->size / bytes_per_sample;
    if (total_samples == 0) {
        return DSDPIPE_OK;  /* Nothing to write */
    }

    /* Calculate number of frames (samples per channel) */
    int channels = buffer->format.channel_count;
    if (channels <= 0 || channels > FLAC_SINK_MAX_CHANNELS) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    size_t frames = total_samples / (size_t)channels;
    if (frames == 0) {
        return DSDPIPE_OK;  /* Nothing to write */
    }

    /* Ensure conversion buffer is large enough */
    int ret = ensure_conv_buffer(flac_ctx, total_samples);
    if (ret != DSDPIPE_OK) {
        return ret;
    }

    /*
     * Convert input PCM data to FLAC__int32.
     * libFLAC expects signed integers with the sample value in the lower bits
     * according to bits_per_sample.
     */
    switch (type) {
        case DSDPIPE_FORMAT_PCM_INT16:
            if (flac_ctx->bit_depth == 16) {
                /* Direct conversion */
                convert_int16_to_int32((const int16_t *)buffer->data,
                                       flac_ctx->conv_buffer, total_samples);
            } else {
                /* Scale up to 24-bit (shift left 8 bits) */
                const int16_t *src = (const int16_t *)buffer->data;
                for (size_t i = 0; i < total_samples; i++) {
                    flac_ctx->conv_buffer[i] = ((int32_t)src[i]) << 8;
                }
            }
            break;

        case DSDPIPE_FORMAT_PCM_INT24:
            convert_int24_to_int32(buffer->data,
                                   flac_ctx->conv_buffer, total_samples);
            if (flac_ctx->bit_depth == 16) {
                /* Scale down to 16-bit */
                for (size_t i = 0; i < total_samples; i++) {
                    flac_ctx->conv_buffer[i] >>= 8;
                }
            }
            break;

        case DSDPIPE_FORMAT_PCM_INT32:
            if (flac_ctx->bit_depth == 24) {
                convert_int32_to_int24((const int32_t *)buffer->data,
                                       flac_ctx->conv_buffer, total_samples);
            } else {
                /* Scale down to 16-bit */
                const int32_t *src = (const int32_t *)buffer->data;
                for (size_t i = 0; i < total_samples; i++) {
                    flac_ctx->conv_buffer[i] = src[i] >> 16;
                }
            }
            break;

        case DSDPIPE_FORMAT_PCM_FLOAT32:
            convert_float32_to_int32((const float *)buffer->data,
                                     flac_ctx->conv_buffer, total_samples,
                                     flac_ctx->bit_depth);
            break;

        case DSDPIPE_FORMAT_PCM_FLOAT64:
            convert_float64_to_int32((const double *)buffer->data,
                                     flac_ctx->conv_buffer, total_samples,
                                     flac_ctx->bit_depth);
            break;

        default:
            return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Write samples to FLAC encoder */
    FLAC__bool ok = FLAC__stream_encoder_process_interleaved(
        flac_ctx->encoder,
        flac_ctx->conv_buffer,
        (unsigned)frames);

    if (!ok) {
        return DSDPIPE_ERROR_FILE_WRITE;
    }

    /* Update statistics */
    flac_ctx->frames_written++;
    flac_ctx->samples_written += frames;
    flac_ctx->track_samples += frames;

    return DSDPIPE_OK;
}

static int flac_sink_finalize(void *ctx)
{
    dsdpipe_sink_flac_ctx_t *flac_ctx = (dsdpipe_sink_flac_ctx_t *)ctx;

    if (!flac_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Close any remaining active encoder */
    close_encoder(flac_ctx);

    return DSDPIPE_OK;
}

static uint32_t flac_sink_get_capabilities(void *ctx)
{
    (void)ctx;
    /* FLAC sink accepts PCM data and supports metadata */
    return DSDPIPE_SINK_CAP_PCM | DSDPIPE_SINK_CAP_METADATA;
}

static void flac_sink_destroy(void *ctx)
{
    dsdpipe_sink_flac_ctx_t *flac_ctx = (dsdpipe_sink_flac_ctx_t *)ctx;

    if (!flac_ctx) {
        return;
    }

    flac_sink_close(ctx);
    sa_free(flac_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_sink_ops_t s_flac_sink_ops = {
    .open = flac_sink_open,
    .close = flac_sink_close,
    .track_start = flac_sink_track_start,
    .track_end = flac_sink_track_end,
    .write_frame = flac_sink_write_frame,
    .finalize = flac_sink_finalize,
    .get_capabilities = flac_sink_get_capabilities,
    .destroy = flac_sink_destroy
};

#endif /* HAVE_LIBFLAC */

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_sink_flac_create(dsdpipe_sink_t **sink,
                              const dsdpipe_sink_config_t *config)
{
#ifndef HAVE_LIBFLAC
    (void)sink;
    (void)config;
    return DSDPIPE_ERROR_FLAC_UNAVAILABLE;
#else
    if (!sink || !config) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dsdpipe_sink_t *new_sink = (dsdpipe_sink_t *)sa_calloc(1, sizeof(*new_sink));
    if (!new_sink) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    dsdpipe_sink_flac_ctx_t *ctx =
        (dsdpipe_sink_flac_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        sa_free(new_sink);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    ctx->bit_depth = config->opts.flac.bit_depth;
    ctx->compression = config->opts.flac.compression;
    ctx->track_filename_format = config->track_filename_format;

    /* Set defaults if not specified */
    if (ctx->bit_depth != 16 && ctx->bit_depth != 24) {
        ctx->bit_depth = 24;
    }
    if (ctx->compression < 0 || ctx->compression > 8) {
        ctx->compression = 5;
    }

    new_sink->type = DSDPIPE_SINK_FLAC;
    new_sink->ops = &s_flac_sink_ops;
    new_sink->ctx = ctx;
    new_sink->config = *config;
    new_sink->config.path = dsdpipe_strdup(config->path);
    new_sink->caps = flac_sink_get_capabilities(ctx);
    new_sink->is_open = false;

    *sink = new_sink;
    return DSDPIPE_OK;
#endif
}
