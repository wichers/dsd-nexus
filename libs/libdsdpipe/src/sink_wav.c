/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief WAV sink implementation using dr_wav
 * This sink converts PCM data to WAV files using the dr_wav library.
 * One WAV file is created per track, with optional LIST INFO metadata.
 * Supported input formats:
 *   - DSDPIPE_FORMAT_PCM_INT16
 *   - DSDPIPE_FORMAT_PCM_INT24
 *   - DSDPIPE_FORMAT_PCM_INT32
 *   - DSDPIPE_FORMAT_PCM_FLOAT32
 *   - DSDPIPE_FORMAT_PCM_FLOAT64
 * Output formats:
 *   - 16-bit: WAV with 16-bit integer PCM samples
 *   - 24-bit: WAV with 24-bit integer PCM samples
 *   - 32-bit: WAV with 32-bit IEEE float samples
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


/* dr_wav implementation (single-header library) */
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

#include "dsdpipe_internal.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <libsautil/mem.h>
#include <libsautil/sa_path.h>
#include <libsautil/sastring.h>
#include <libsautil/compat.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define WAV_SINK_MAX_CHANNELS       6
#define WAV_SINK_SAMPLE_BUFFER_SIZE 8192
#define WAV_SINK_MAX_METADATA       8

/*============================================================================
 * WAV Sink Context
 *============================================================================*/

typedef struct dsdpipe_sink_wav_ctx_s {
    /* Configuration */
    char *base_path;            /**< Base output path (without extension) */
    int bit_depth;              /**< Requested output bit depth (16, 24, 32) */
    int sample_rate;            /**< Output sample rate (0 = auto from source) */
    dsdpipe_track_format_t track_filename_format; /**< Track filename format */

    /* Source format */
    dsdpipe_format_t format;   /**< Source audio format */

    /* Track state */
    uint8_t current_track;      /**< Current track number */
    bool track_file_open;       /**< Whether a track file is currently open */

    /* dr_wav instance */
    drwav wav;                  /**< dr_wav writer state */
    FILE *wav_file;             /**< File handle for callback-based I/O */

    /* Conversion buffer (for converting input PCM to float32) */
    float *conv_buffer;         /**< Float32 intermediate buffer */
    size_t conv_buffer_size;    /**< Conv buffer capacity (in samples) */

    /* Output buffer (for converting float32 to target format) */
    void *write_buffer;         /**< Output format buffer */
    size_t write_buffer_size;   /**< Write buffer capacity (in bytes) */

    /* Album-level metadata (stored from open() for use in track_start()) */
    char *album_title;
    char *album_artist;
    char *album_copyright;
    char *genre;
    uint16_t year;

    /* Metadata storage (must outlive drwav for uninit RIFF size calculation) */
    drwav_metadata meta[WAV_SINK_MAX_METADATA]; /**< Metadata entries for dr_wav */
    char track_num_buf[16];     /**< Track number string buffer */
    char year_buf[16];          /**< Year string buffer */

    /* Statistics */
    uint64_t frames_written;    /**< Total frames written (all tracks) */
    uint64_t bytes_written;     /**< Total bytes written (all tracks) */
    uint64_t samples_written;   /**< Total samples written (all tracks) */
    uint64_t tracks_written;    /**< Number of tracks completed */
    uint64_t track_samples;     /**< Samples written for current track */
} dsdpipe_sink_wav_ctx_t;

/*============================================================================
 * dr_wav I/O Callbacks
 *============================================================================*/

static size_t wav_drwav_write(void *user_data, const void *data, size_t bytes)
{
    FILE *f = (FILE *)user_data;
    return fwrite(data, 1, bytes, f);
}

static drwav_bool32 wav_drwav_seek(void *user_data, int offset,
                                    drwav_seek_origin origin)
{
    FILE *f = (FILE *)user_data;
    int whence;

    switch (origin) {
        case DRWAV_SEEK_SET: whence = SEEK_SET; break;
        case DRWAV_SEEK_CUR: whence = SEEK_CUR; break;
        case DRWAV_SEEK_END: whence = SEEK_END; break;
        default: return DRWAV_FALSE;
    }

    return sa_fseek64(f, (int64_t)offset, whence) == 0;
}

/*============================================================================
 * Helper: Convert float64 to float32 (no dr_wav equivalent)
 *============================================================================*/

static void convert_float64_to_float32(const double *src, float *dst, size_t samples)
{
    for (size_t i = 0; i < samples; i++) {
        dst[i] = (float)src[i];
    }
}

/*============================================================================
 * Helper: Convert float32 to packed 24-bit little-endian
 *============================================================================*/

static void convert_float32_to_int24(const float *src, uint8_t *dst, size_t samples)
{
    for (size_t i = 0; i < samples; i++) {
        float val = src[i];

        /* Clamp to [-1.0, 1.0] */
        if (val > 1.0f) val = 1.0f;
        if (val < -1.0f) val = -1.0f;

        int32_t s24 = (int32_t)(val * 8388607.0f);

        /* Clamp to 24-bit range */
        if (s24 > 8388607) s24 = 8388607;
        if (s24 < -8388608) s24 = -8388608;

        /* Pack as 3-byte little-endian */
        dst[0] = (uint8_t)(s24 & 0xFF);
        dst[1] = (uint8_t)((s24 >> 8) & 0xFF);
        dst[2] = (uint8_t)((s24 >> 16) & 0xFF);
        dst += 3;
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
 * Helper: Get bytes per output sample based on bit depth
 *============================================================================*/

static size_t get_output_bytes_per_sample(int bit_depth)
{
    switch (bit_depth) {
        case 16: return 2;
        case 24: return 3;
        case 32: return 4;
        default: return 2;
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

    char *full_path = sa_make_path(base_path, NULL, track_name, "wav");
    sa_free(track_name);
    return full_path;
}

/*============================================================================
 * Helper: Ensure conversion buffer is large enough
 *============================================================================*/

static int ensure_conv_buffer(dsdpipe_sink_wav_ctx_t *ctx, size_t samples)
{
    if (ctx->conv_buffer_size >= samples) {
        return DSDPIPE_OK;
    }

    size_t new_size = samples + (samples / 4);
    if (new_size < WAV_SINK_SAMPLE_BUFFER_SIZE) {
        new_size = WAV_SINK_SAMPLE_BUFFER_SIZE;
    }

    float *new_buffer = (float *)sa_realloc(ctx->conv_buffer, new_size * sizeof(float));
    if (!new_buffer) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    ctx->conv_buffer = new_buffer;
    ctx->conv_buffer_size = new_size;
    return DSDPIPE_OK;
}

/*============================================================================
 * Helper: Ensure write buffer is large enough
 *============================================================================*/

static int ensure_write_buffer(dsdpipe_sink_wav_ctx_t *ctx, size_t bytes)
{
    if (ctx->write_buffer_size >= bytes) {
        return DSDPIPE_OK;
    }

    size_t new_size = bytes + (bytes / 4);
    void *new_buffer = sa_realloc(ctx->write_buffer, new_size);
    if (!new_buffer) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    ctx->write_buffer = new_buffer;
    ctx->write_buffer_size = new_size;
    return DSDPIPE_OK;
}

/*============================================================================
 * Helper: Build drwav metadata array from pipeline metadata
 *============================================================================*/

static void add_info_text(drwav_metadata *meta, drwav_metadata_type type,
                          const char *str)
{
    meta->type = type;
    meta->data.infoText.pString = (char *)str;
    meta->data.infoText.stringLength = (drwav_uint32)strlen(str);
}

static drwav_uint32 build_metadata(dsdpipe_sink_wav_ctx_t *ctx,
                                    const dsdpipe_metadata_t *track_meta,
                                    drwav_metadata *meta,
                                    char *track_num_buf, size_t track_num_buf_size,
                                    char *year_buf, size_t year_buf_size)
{
    drwav_uint32 count = 0;

    /* Track title */
    const char *title = (track_meta && track_meta->track_title)
                        ? track_meta->track_title : NULL;
    if (title) {
        add_info_text(&meta[count++],
                      drwav_metadata_type_list_info_title, title);
    }

    /* Artist (track performer, or album artist as fallback) */
    const char *artist = NULL;
    if (track_meta && track_meta->track_performer) {
        artist = track_meta->track_performer;
    } else if (ctx->album_artist) {
        artist = ctx->album_artist;
    }
    if (artist) {
        add_info_text(&meta[count++],
                      drwav_metadata_type_list_info_artist, artist);
    }

    /* Album */
    const char *album = (track_meta && track_meta->album_title)
                        ? track_meta->album_title : ctx->album_title;
    if (album) {
        add_info_text(&meta[count++],
                      drwav_metadata_type_list_info_album, album);
    }

    /* Genre */
    const char *genre_str = (track_meta && track_meta->genre)
                            ? track_meta->genre : ctx->genre;
    if (genre_str) {
        add_info_text(&meta[count++],
                      drwav_metadata_type_list_info_genre, genre_str);
    }

    /* Track number */
    uint8_t track_num = (track_meta && track_meta->track_number > 0)
                        ? track_meta->track_number : ctx->current_track;
    if (track_num > 0) {
#ifdef _MSC_VER
        sprintf_s(track_num_buf, track_num_buf_size, "%u", track_num);
#else
        snprintf(track_num_buf, track_num_buf_size, "%u", track_num);
#endif
        add_info_text(&meta[count++],
                      drwav_metadata_type_list_info_tracknumber, track_num_buf);
    }

    /* Year/date */
    uint16_t year = (track_meta && track_meta->year > 0)
                    ? track_meta->year : ctx->year;
    if (year > 0) {
#ifdef _MSC_VER
        sprintf_s(year_buf, year_buf_size, "%u", year);
#else
        snprintf(year_buf, year_buf_size, "%u", year);
#endif
        add_info_text(&meta[count++],
                      drwav_metadata_type_list_info_date, year_buf);
    }

    /* Copyright */
    const char *copyright = (track_meta && track_meta->album_copyright)
                            ? track_meta->album_copyright
                            : ctx->album_copyright;
    if (copyright) {
        add_info_text(&meta[count++],
                      drwav_metadata_type_list_info_copyright, copyright);
    }

    /* Comment (track message) */
    if (track_meta && track_meta->track_message) {
        add_info_text(&meta[count++],
                      drwav_metadata_type_list_info_comment,
                      track_meta->track_message);
    }

    return count;
}

/*============================================================================
 * Helper: Close current track safely
 *============================================================================*/

static void close_current_track(dsdpipe_sink_wav_ctx_t *ctx)
{
    if (!ctx->track_file_open) {
        return;
    }

    drwav_uninit(&ctx->wav);

    if (ctx->wav_file) {
        fclose(ctx->wav_file);
        ctx->wav_file = NULL;
    }

    ctx->track_file_open = false;
}

/*============================================================================
 * Sink Operations
 *============================================================================*/

static int wav_sink_open(void *ctx, const char *path,
                         const dsdpipe_format_t *format,
                         const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_wav_ctx_t *wav_ctx = (dsdpipe_sink_wav_ctx_t *)ctx;

    if (!wav_ctx || !path || !format) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store base path */
    wav_ctx->base_path = dsdpipe_strdup(path);
    if (!wav_ctx->base_path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Ensure output directory exists */
    if (sa_mkdir_p(path, NULL, 0755) != 0) {
        sa_freep(&wav_ctx->base_path);
        return DSDPIPE_ERROR_FILE_CREATE;
    }

    wav_ctx->format = *format;
    wav_ctx->frames_written = 0;
    wav_ctx->bytes_written = 0;
    wav_ctx->samples_written = 0;
    wav_ctx->tracks_written = 0;
    wav_ctx->track_file_open = false;
    wav_ctx->wav_file = NULL;

    /* Determine output sample rate if not specified */
    if (wav_ctx->sample_rate == 0) {
        if (format->sample_rate > 100000) {
            wav_ctx->sample_rate = (int)(format->sample_rate / 32);
        } else {
            wav_ctx->sample_rate = (int)format->sample_rate;
        }
    }

    /* Validate bit depth, default to 24 if not specified or invalid */
    if (wav_ctx->bit_depth != 16 && wav_ctx->bit_depth != 24 &&
        wav_ctx->bit_depth != 32) {
        wav_ctx->bit_depth = 24;
    }

    /* Allocate initial conversion buffer */
    int ret = ensure_conv_buffer(wav_ctx, WAV_SINK_SAMPLE_BUFFER_SIZE);
    if (ret != DSDPIPE_OK) {
        sa_freep(&wav_ctx->base_path);
        return ret;
    }

    /* Store album-level metadata for use in track_start() */
    if (metadata) {
        if (metadata->album_title) {
            wav_ctx->album_title = dsdpipe_strdup(metadata->album_title);
        }
        if (metadata->album_artist) {
            wav_ctx->album_artist = dsdpipe_strdup(metadata->album_artist);
        }
        if (metadata->album_copyright) {
            wav_ctx->album_copyright = dsdpipe_strdup(metadata->album_copyright);
        }
        if (metadata->genre) {
            wav_ctx->genre = dsdpipe_strdup(metadata->genre);
        }
        wav_ctx->year = metadata->year;
    }

    return DSDPIPE_OK;
}

static void wav_sink_close(void *ctx)
{
    dsdpipe_sink_wav_ctx_t *wav_ctx = (dsdpipe_sink_wav_ctx_t *)ctx;

    if (!wav_ctx) {
        return;
    }

    close_current_track(wav_ctx);

    sa_freep(&wav_ctx->base_path);
    sa_freep(&wav_ctx->album_title);
    sa_freep(&wav_ctx->album_artist);
    sa_freep(&wav_ctx->album_copyright);
    sa_freep(&wav_ctx->genre);

    if (wav_ctx->conv_buffer) {
        sa_freep(&wav_ctx->conv_buffer);
        wav_ctx->conv_buffer_size = 0;
    }

    if (wav_ctx->write_buffer) {
        sa_freep(&wav_ctx->write_buffer);
        wav_ctx->write_buffer_size = 0;
    }
}

static int wav_sink_track_start(void *ctx, uint8_t track_number,
                                const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_wav_ctx_t *wav_ctx = (dsdpipe_sink_wav_ctx_t *)ctx;

    if (!wav_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Close previous track file if still open */
    close_current_track(wav_ctx);

    wav_ctx->current_track = track_number;
    wav_ctx->track_samples = 0;

    /* Generate unique output filename for this track */
    char *output_path = generate_track_filename(wav_ctx->base_path, metadata,
                                                wav_ctx->track_filename_format);
    if (!output_path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Open file for writing (with UTF-8 support) */
    wav_ctx->wav_file = sa_fopen(output_path, "wb");
    sa_free(output_path);
    if (!wav_ctx->wav_file) {
        return DSDPIPE_ERROR_FILE_CREATE;
    }

    /* Configure WAV format */
    drwav_data_format wav_format;
    wav_format.container = drwav_container_riff;
    wav_format.channels = wav_ctx->format.channel_count;
    wav_format.sampleRate = (drwav_uint32)wav_ctx->sample_rate;

    if (wav_ctx->bit_depth == 32) {
        wav_format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
        wav_format.bitsPerSample = 32;
    } else {
        wav_format.format = DR_WAVE_FORMAT_PCM;
        wav_format.bitsPerSample = (drwav_uint32)wav_ctx->bit_depth;
    }

    /* Build metadata array (stored in ctx — dr_wav keeps the pointer until uninit) */
    memset(wav_ctx->meta, 0, sizeof(wav_ctx->meta));
    drwav_uint32 meta_count = build_metadata(wav_ctx, metadata, wav_ctx->meta,
                                              wav_ctx->track_num_buf,
                                              sizeof(wav_ctx->track_num_buf),
                                              wav_ctx->year_buf,
                                              sizeof(wav_ctx->year_buf));

    /* Initialize dr_wav writer with metadata */
    drwav_bool32 init_ok;
    if (meta_count > 0) {
        init_ok = drwav_init_write_with_metadata(
            &wav_ctx->wav, &wav_format,
            wav_drwav_write, wav_drwav_seek,
            wav_ctx->wav_file, NULL,
            wav_ctx->meta, meta_count);
    } else {
        init_ok = drwav_init_write_with_metadata(
            &wav_ctx->wav, &wav_format,
            wav_drwav_write, wav_drwav_seek,
            wav_ctx->wav_file, NULL,
            NULL, 0);
    }

    if (!init_ok) {
        fclose(wav_ctx->wav_file);
        wav_ctx->wav_file = NULL;
        return DSDPIPE_ERROR_FILE_CREATE;
    }

    wav_ctx->track_file_open = true;
    return DSDPIPE_OK;
}

static int wav_sink_track_end(void *ctx, uint8_t track_number)
{
    dsdpipe_sink_wav_ctx_t *wav_ctx = (dsdpipe_sink_wav_ctx_t *)ctx;

    if (!wav_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    (void)track_number;

    if (wav_ctx->track_file_open) {
        close_current_track(wav_ctx);
        wav_ctx->tracks_written++;
    }

    return DSDPIPE_OK;
}

static int wav_sink_write_frame(void *ctx, const dsdpipe_buffer_t *buffer)
{
    dsdpipe_sink_wav_ctx_t *wav_ctx = (dsdpipe_sink_wav_ctx_t *)ctx;

    if (!wav_ctx || !buffer) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!wav_ctx->track_file_open) {
        return DSDPIPE_ERROR_INVALID_STATE;
    }

    /* Validate that we received PCM data */
    dsdpipe_audio_format_t type = buffer->format.type;
    if (type != DSDPIPE_FORMAT_PCM_INT16 &&
        type != DSDPIPE_FORMAT_PCM_INT24 &&
        type != DSDPIPE_FORMAT_PCM_INT32 &&
        type != DSDPIPE_FORMAT_PCM_FLOAT32 &&
        type != DSDPIPE_FORMAT_PCM_FLOAT64) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Calculate number of samples (total, all channels interleaved) */
    size_t bytes_per_sample = get_bytes_per_sample(type);
    if (bytes_per_sample == 0) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    size_t total_samples = buffer->size / bytes_per_sample;
    if (total_samples == 0) {
        return DSDPIPE_OK;
    }

    /* Calculate number of frames (samples per channel) */
    int channels = buffer->format.channel_count;
    if (channels <= 0 || channels > WAV_SINK_MAX_CHANNELS) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    size_t frames = total_samples / (size_t)channels;
    if (frames == 0) {
        return DSDPIPE_OK;
    }

    /* Ensure conversion buffer is large enough for float32 intermediate */
    int ret = ensure_conv_buffer(wav_ctx, total_samples);
    if (ret != DSDPIPE_OK) {
        return ret;
    }

    /* Ensure write buffer is large enough for output format */
    size_t out_bytes_per_sample = get_output_bytes_per_sample(wav_ctx->bit_depth);
    size_t write_bytes = total_samples * out_bytes_per_sample;
    ret = ensure_write_buffer(wav_ctx, write_bytes);
    if (ret != DSDPIPE_OK) {
        return ret;
    }

    /*
     * Step 1: Convert input PCM to float32 intermediate.
     * For float32 input with 32-bit output, we can skip straight to writing.
     */
    bool need_float_conversion = true;

    if (type == DSDPIPE_FORMAT_PCM_FLOAT32 && wav_ctx->bit_depth == 32) {
        /* Float32 input → float32 output: write directly */
        drwav_uint64 written = drwav_write_pcm_frames(
            &wav_ctx->wav, frames, buffer->data);
        if (written < frames) {
            return DSDPIPE_ERROR_FILE_WRITE;
        }
        need_float_conversion = false;
    }

    if (need_float_conversion) {
        /* Convert input to float32 using dr_wav built-in converters */
        switch (type) {
            case DSDPIPE_FORMAT_PCM_INT16:
                drwav_s16_to_f32(wav_ctx->conv_buffer,
                                 (const drwav_int16 *)buffer->data,
                                 total_samples);
                break;

            case DSDPIPE_FORMAT_PCM_INT24:
                drwav_s24_to_f32(wav_ctx->conv_buffer,
                                 buffer->data, total_samples);
                break;

            case DSDPIPE_FORMAT_PCM_INT32:
                drwav_s32_to_f32(wav_ctx->conv_buffer,
                                 (const drwav_int32 *)buffer->data,
                                 total_samples);
                break;

            case DSDPIPE_FORMAT_PCM_FLOAT32:
                memcpy(wav_ctx->conv_buffer, buffer->data,
                       total_samples * sizeof(float));
                break;

            case DSDPIPE_FORMAT_PCM_FLOAT64:
                convert_float64_to_float32((const double *)buffer->data,
                                           wav_ctx->conv_buffer, total_samples);
                break;

            default:
                return DSDPIPE_ERROR_INVALID_ARG;
        }

        /*
         * Step 2: Convert float32 to output format and write.
         */
        drwav_uint64 written;

        switch (wav_ctx->bit_depth) {
            case 16:
                /* float32 → int16 */
                drwav_f32_to_s16((drwav_int16 *)wav_ctx->write_buffer,
                                 wav_ctx->conv_buffer, total_samples);
                written = drwav_write_pcm_frames(
                    &wav_ctx->wav, frames, wav_ctx->write_buffer);
                break;

            case 24:
                /* float32 → packed 24-bit LE */
                convert_float32_to_int24(wav_ctx->conv_buffer,
                                         (uint8_t *)wav_ctx->write_buffer,
                                         total_samples);
                written = drwav_write_pcm_frames(
                    &wav_ctx->wav, frames, wav_ctx->write_buffer);
                break;

            case 32:
                /* float32 → float32 (already in conv_buffer) */
                written = drwav_write_pcm_frames(
                    &wav_ctx->wav, frames, wav_ctx->conv_buffer);
                break;

            default:
                return DSDPIPE_ERROR_INVALID_ARG;
        }

        if (written < frames) {
            return DSDPIPE_ERROR_FILE_WRITE;
        }
    }

    /* Update statistics */
    wav_ctx->frames_written++;
    wav_ctx->bytes_written += buffer->size;
    wav_ctx->samples_written += total_samples;
    wav_ctx->track_samples += total_samples;

    return DSDPIPE_OK;
}

static int wav_sink_finalize(void *ctx)
{
    dsdpipe_sink_wav_ctx_t *wav_ctx = (dsdpipe_sink_wav_ctx_t *)ctx;

    if (!wav_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    close_current_track(wav_ctx);
    return DSDPIPE_OK;
}

static uint32_t wav_sink_get_capabilities(void *ctx)
{
    (void)ctx;
    return DSDPIPE_SINK_CAP_PCM | DSDPIPE_SINK_CAP_METADATA;
}

static void wav_sink_destroy(void *ctx)
{
    dsdpipe_sink_wav_ctx_t *wav_ctx = (dsdpipe_sink_wav_ctx_t *)ctx;

    if (!wav_ctx) {
        return;
    }

    wav_sink_close(ctx);
    sa_free(wav_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_sink_ops_t s_wav_sink_ops = {
    .open = wav_sink_open,
    .close = wav_sink_close,
    .track_start = wav_sink_track_start,
    .track_end = wav_sink_track_end,
    .write_frame = wav_sink_write_frame,
    .finalize = wav_sink_finalize,
    .get_capabilities = wav_sink_get_capabilities,
    .destroy = wav_sink_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_sink_wav_create(dsdpipe_sink_t **sink,
                             const dsdpipe_sink_config_t *config)
{
    if (!sink || !config) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dsdpipe_sink_t *new_sink = (dsdpipe_sink_t *)sa_calloc(1, sizeof(*new_sink));
    if (!new_sink) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    dsdpipe_sink_wav_ctx_t *ctx =
        (dsdpipe_sink_wav_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        sa_free(new_sink);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    ctx->bit_depth = config->opts.wav.bit_depth;
    ctx->sample_rate = config->opts.wav.sample_rate;
    ctx->track_filename_format = config->track_filename_format;

    new_sink->type = DSDPIPE_SINK_WAV;
    new_sink->ops = &s_wav_sink_ops;
    new_sink->ctx = ctx;
    new_sink->config = *config;
    new_sink->config.path = dsdpipe_strdup(config->path);
    new_sink->caps = wav_sink_get_capabilities(ctx);
    new_sink->is_open = false;

    *sink = new_sink;
    return DSDPIPE_OK;
}
