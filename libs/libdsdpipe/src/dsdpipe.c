/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Main pipeline implementation
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
#include "frame_queue.h"
#include "reader_thread.h"
#include <libdsdpipe/version.h>

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include <libsautil/mem.h>
#include <libsautil/sastring.h>

/*============================================================================
 * Error String Table
 *============================================================================*/

static const char *s_error_strings[] = {
    "Success",                              /* DSDPIPE_OK */
    "Invalid argument",                     /* DSDPIPE_ERROR_INVALID_ARG */
    "Out of memory",                        /* DSDPIPE_ERROR_OUT_OF_MEMORY */
    "Pipeline not configured",              /* DSDPIPE_ERROR_NOT_CONFIGURED */
    "Pipeline already running",             /* DSDPIPE_ERROR_ALREADY_RUNNING */
    "Failed to open source",                /* DSDPIPE_ERROR_SOURCE_OPEN */
    "Failed to open sink",                  /* DSDPIPE_ERROR_SINK_OPEN */
    "Read error",                           /* DSDPIPE_ERROR_READ */
    "Write error",                          /* DSDPIPE_ERROR_WRITE */
    "DST decoding error",                   /* DSDPIPE_ERROR_DST_DECODE */
    "PCM conversion error",                 /* DSDPIPE_ERROR_PCM_CONVERT */
    "Operation cancelled",                  /* DSDPIPE_ERROR_CANCELLED */
    "No source configured",                 /* DSDPIPE_ERROR_NO_SOURCE */
    "No sinks configured",                  /* DSDPIPE_ERROR_NO_SINKS */
    "Track not found",                      /* DSDPIPE_ERROR_TRACK_NOT_FOUND */
    "Unsupported operation",                /* DSDPIPE_ERROR_UNSUPPORTED */
    "Internal error",                       /* DSDPIPE_ERROR_INTERNAL */
    "FLAC support not available",           /* DSDPIPE_ERROR_FLAC_UNAVAILABLE */
    "Invalid track specification"           /* DSDPIPE_ERROR_INVALID_TRACK_SPEC */
};

/*============================================================================
 * Error Handling
 *============================================================================*/

void dsdpipe_set_error(dsdpipe_t *pipe, dsdpipe_error_t error,
                        const char *format, ...)
{
    if (!pipe) {
        return;
    }

    pipe->last_error = error;

    if (format) {
        va_list args;
        va_start(args, format);
#ifdef _MSC_VER
        vsnprintf_s(pipe->error_message, sizeof(pipe->error_message),
                    _TRUNCATE, format, args);
#else
        vsnprintf(pipe->error_message, sizeof(pipe->error_message), format, args);
#endif
        va_end(args);
    } else {
        /* Use default error string */
        int idx = -error;
        if (idx >= 0 && idx < (int)(sizeof(s_error_strings) / sizeof(s_error_strings[0]))) {
            sa_strlcpy(pipe->error_message, s_error_strings[idx],
                       sizeof(pipe->error_message));
        } else {
            pipe->error_message[0] = '\0';
        }
    }
}

const char *dsdpipe_get_error_message(dsdpipe_t *pipe)
{
    if (!pipe) {
        return "Invalid pipeline handle";
    }
    return pipe->error_message;
}

const char *dsdpipe_error_string(dsdpipe_error_t error)
{
    int idx = -error;
    if (idx >= 0 && idx < (int)(sizeof(s_error_strings) / sizeof(s_error_strings[0]))) {
        return s_error_strings[idx];
    }
    return "Unknown error";
}

/*============================================================================
 * Buffer Pool Management
 *============================================================================*/

int dsdpipe_init_pools(dsdpipe_t *pipe)
{
    if (!pipe || pipe->pools_initialized) {
        return DSDPIPE_OK;
    }

    pipe->dsd_pool = sa_buffer_pool_init(DSDPIPE_MAX_DSD_SIZE, NULL);
    if (!pipe->dsd_pool) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    pipe->pcm_pool = sa_buffer_pool_init(DSDPIPE_MAX_DSD_SIZE * 4, NULL);
    if (!pipe->pcm_pool) {
        sa_buffer_pool_uninit(&pipe->dsd_pool);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    pipe->pools_initialized = true;
    return DSDPIPE_OK;
}

void dsdpipe_free_pools(dsdpipe_t *pipe)
{
    if (!pipe || !pipe->pools_initialized) {
        return;
    }

    sa_buffer_pool_uninit(&pipe->dsd_pool);
    sa_buffer_pool_uninit(&pipe->pcm_pool);
    pipe->pools_initialized = false;
}

dsdpipe_buffer_t *dsdpipe_buffer_alloc_dsd(dsdpipe_t *pipe)
{
    if (!pipe) {
        return NULL;
    }

    sa_buffer_ref_t *ref = sa_buffer_pool_get(pipe->dsd_pool);
    if (!ref) {
        return NULL;
    }

    dsdpipe_buffer_t *buffer = (dsdpipe_buffer_t *)sa_calloc(1, sizeof(*buffer));
    if (!buffer) {
        sa_buffer_unref(&ref);
        return NULL;
    }

    buffer->ref = ref;
    buffer->data = ref->data;
    buffer->capacity = ref->size;
    buffer->size = 0;
    buffer->flags = 0;

    return buffer;
}

dsdpipe_buffer_t *dsdpipe_buffer_alloc_pcm(dsdpipe_t *pipe)
{
    if (!pipe) {
        return NULL;
    }

    sa_buffer_ref_t *ref = sa_buffer_pool_get(pipe->pcm_pool);
    if (!ref) {
        return NULL;
    }

    dsdpipe_buffer_t *buffer = (dsdpipe_buffer_t *)sa_calloc(1, sizeof(*buffer));
    if (!buffer) {
        sa_buffer_unref(&ref);
        return NULL;
    }

    buffer->ref = ref;
    buffer->data = ref->data;
    buffer->capacity = ref->size;
    buffer->size = 0;
    buffer->flags = 0;

    return buffer;
}

void dsdpipe_buffer_unref(dsdpipe_buffer_t *buffer)
{
    if (buffer) {
        if (buffer->ref) {
            sa_buffer_unref(&buffer->ref);
        }
        sa_free(buffer);
    }
}

/*============================================================================
 * Pipeline Lifecycle
 *============================================================================*/

dsdpipe_t *dsdpipe_create(void)
{
    dsdpipe_t *pipe = (dsdpipe_t *)sa_calloc(1, sizeof(*pipe));
    if (!pipe) {
        return NULL;
    }

    pipe->state = DSDPIPE_STATE_CREATED;
    pipe->cancelled = 0;
    pipe->pcm_quality = DSDPIPE_PCM_QUALITY_NORMAL;
    pipe->pcm_use_fp64 = false;
    pipe->track_filename_format = DSDPIPE_TRACK_NUM_TITLE;  /* Default format */

    /* Initialize track selection */
    if (dsdpipe_track_selection_init(&pipe->tracks) != DSDPIPE_OK) {
        sa_free(pipe);
        return NULL;
    }

    return pipe;
}

void dsdpipe_destroy(dsdpipe_t *pipe)
{
    if (!pipe) {
        return;
    }

    /* Destroy source */
    dsdpipe_source_destroy(&pipe->source);

    /* Destroy sinks */
    for (int i = 0; i < pipe->sink_count; i++) {
        dsdpipe_sink_destroy(pipe->sinks[i]);
        pipe->sinks[i] = NULL;
    }
    pipe->sink_count = 0;

    /* Destroy transforms */
    if (pipe->dst_decoder) {
        dsdpipe_transform_destroy(pipe->dst_decoder);
        pipe->dst_decoder = NULL;
    }
    if (pipe->dsd2pcm) {
        dsdpipe_transform_destroy(pipe->dsd2pcm);
        pipe->dsd2pcm = NULL;
    }

    /* Free track selection */
    dsdpipe_track_selection_free(&pipe->tracks);

    /* Free buffer pools */
    dsdpipe_free_pools(pipe);

    sa_free(pipe);
}

int dsdpipe_reset(dsdpipe_t *pipe)
{
    if (!pipe) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Clear sinks */
    for (int i = 0; i < pipe->sink_count; i++) {
        dsdpipe_sink_destroy(pipe->sinks[i]);
        pipe->sinks[i] = NULL;
    }
    pipe->sink_count = 0;

    /* Clear track selection */
    dsdpipe_track_selection_clear(&pipe->tracks);

    /* Clear transforms */
    if (pipe->dst_decoder) {
        dsdpipe_transform_destroy(pipe->dst_decoder);
        pipe->dst_decoder = NULL;
    }
    if (pipe->dsd2pcm) {
        dsdpipe_transform_destroy(pipe->dsd2pcm);
        pipe->dsd2pcm = NULL;
    }

    /* Reset state */
    pipe->state = (pipe->source.type != DSDPIPE_SOURCE_NONE)
                  ? DSDPIPE_STATE_CONFIGURED
                  : DSDPIPE_STATE_CREATED;
    atomic_store(&pipe->cancelled, 0);
    pipe->last_error = DSDPIPE_OK;
    pipe->error_message[0] = '\0';

    /* Reset progress */
    memset(&pipe->progress, 0, sizeof(pipe->progress));

    return DSDPIPE_OK;
}

/*============================================================================
 * Source Configuration
 *============================================================================*/

int dsdpipe_set_source_sacd(dsdpipe_t *pipe, const char *iso_path,
                             dsdpipe_channel_type_t channel_type)
{
    if (!pipe || !iso_path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (pipe->state == DSDPIPE_STATE_RUNNING) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_ALREADY_RUNNING, NULL);
        return DSDPIPE_ERROR_ALREADY_RUNNING;
    }

    /* Destroy existing source */
    dsdpipe_source_destroy(&pipe->source);

    /* Create new SACD source */
    int result = dsdpipe_source_sacd_create(&pipe->source, channel_type);
    if (result != DSDPIPE_OK) {
        dsdpipe_set_error(pipe, result, "Failed to create SACD source");
        return result;
    }

    /* Open the source */
    result = pipe->source.ops->open(pipe->source.ctx, iso_path);
    if (result != DSDPIPE_OK) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_SOURCE_OPEN,
                          "Failed to open SACD: %s", iso_path);
        dsdpipe_source_destroy(&pipe->source);
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    pipe->source.is_open = true;

    /* Cache format */
    pipe->source.ops->get_format(pipe->source.ctx, &pipe->source.format);

    pipe->state = DSDPIPE_STATE_CONFIGURED;
    return DSDPIPE_OK;
}

int dsdpipe_set_source_dsdiff(dsdpipe_t *pipe, const char *path)
{
    if (!pipe || !path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (pipe->state == DSDPIPE_STATE_RUNNING) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_ALREADY_RUNNING, NULL);
        return DSDPIPE_ERROR_ALREADY_RUNNING;
    }

    /* Destroy existing source */
    dsdpipe_source_destroy(&pipe->source);

    /* Create new DSDIFF source */
    int result = dsdpipe_source_dsdiff_create(&pipe->source);
    if (result != DSDPIPE_OK) {
        dsdpipe_set_error(pipe, result, "Failed to create DSDIFF source");
        return result;
    }

    /* Open the source */
    result = pipe->source.ops->open(pipe->source.ctx, path);
    if (result != DSDPIPE_OK) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_SOURCE_OPEN,
                          "Failed to open DSDIFF: %s", path);
        dsdpipe_source_destroy(&pipe->source);
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    pipe->source.is_open = true;

    /* Cache format */
    pipe->source.ops->get_format(pipe->source.ctx, &pipe->source.format);

    pipe->state = DSDPIPE_STATE_CONFIGURED;
    return DSDPIPE_OK;
}

int dsdpipe_set_source_dsf(dsdpipe_t *pipe, const char *path)
{
    if (!pipe || !path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (pipe->state == DSDPIPE_STATE_RUNNING) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_ALREADY_RUNNING, NULL);
        return DSDPIPE_ERROR_ALREADY_RUNNING;
    }

    /* Destroy existing source */
    dsdpipe_source_destroy(&pipe->source);

    /* Create new DSF source */
    int result = dsdpipe_source_dsf_create(&pipe->source);
    if (result != DSDPIPE_OK) {
        dsdpipe_set_error(pipe, result, "Failed to create DSF source");
        return result;
    }

    /* Open the source */
    result = pipe->source.ops->open(pipe->source.ctx, path);
    if (result != DSDPIPE_OK) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_SOURCE_OPEN,
                          "Failed to open DSF: %s", path);
        dsdpipe_source_destroy(&pipe->source);
        return DSDPIPE_ERROR_SOURCE_OPEN;
    }

    pipe->source.is_open = true;

    /* Cache format */
    pipe->source.ops->get_format(pipe->source.ctx, &pipe->source.format);

    pipe->state = DSDPIPE_STATE_CONFIGURED;
    return DSDPIPE_OK;
}

dsdpipe_source_type_t dsdpipe_get_source_type(dsdpipe_t *pipe)
{
    if (!pipe) {
        return DSDPIPE_SOURCE_NONE;
    }
    return pipe->source.type;
}

int dsdpipe_get_source_format(dsdpipe_t *pipe, dsdpipe_format_t *format)
{
    if (!pipe || !format) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (pipe->source.type == DSDPIPE_SOURCE_NONE) {
        return DSDPIPE_ERROR_NO_SOURCE;
    }

    *format = pipe->source.format;
    return DSDPIPE_OK;
}

/*============================================================================
 * Track Selection
 *============================================================================*/

int dsdpipe_get_track_count(dsdpipe_t *pipe, uint8_t *count)
{
    if (!pipe || !count) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (pipe->source.type == DSDPIPE_SOURCE_NONE || !pipe->source.ops) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_NO_SOURCE, NULL);
        return DSDPIPE_ERROR_NO_SOURCE;
    }

    return pipe->source.ops->get_track_count(pipe->source.ctx, count);
}

int dsdpipe_select_tracks(dsdpipe_t *pipe, const uint8_t *track_numbers,
                           size_t count)
{
    if (!pipe) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!track_numbers || count == 0) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dsdpipe_track_selection_clear(&pipe->tracks);

    for (size_t i = 0; i < count; i++) {
        int result = dsdpipe_track_selection_add(&pipe->tracks, track_numbers[i]);
        if (result != DSDPIPE_OK) {
            return result;
        }
    }

    return DSDPIPE_OK;
}

int dsdpipe_select_tracks_str(dsdpipe_t *pipe, const char *selection)
{
    if (!pipe || !selection) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    uint8_t max_track = 0;
    int result = dsdpipe_get_track_count(pipe, &max_track);
    if (result != DSDPIPE_OK) {
        return result;
    }

    dsdpipe_track_selection_clear(&pipe->tracks);

    result = dsdpipe_track_selection_parse(&pipe->tracks, selection, max_track);
    if (result != DSDPIPE_OK) {
        dsdpipe_set_error(pipe, result, "Invalid track specification: %s", selection);
    }

    return result;
}

int dsdpipe_select_all_tracks(dsdpipe_t *pipe)
{
    if (!pipe) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    uint8_t count = 0;
    int result = dsdpipe_get_track_count(pipe, &count);
    if (result != DSDPIPE_OK) {
        return result;
    }

    dsdpipe_track_selection_clear(&pipe->tracks);

    for (uint8_t i = 1; i <= count; i++) {
        result = dsdpipe_track_selection_add(&pipe->tracks, i);
        if (result != DSDPIPE_OK) {
            return result;
        }
    }

    return DSDPIPE_OK;
}

int dsdpipe_get_selected_tracks(dsdpipe_t *pipe, uint8_t *tracks,
                                 size_t max_count, size_t *count)
{
    if (!pipe || !count) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    *count = pipe->tracks.count;

    if (tracks && max_count > 0) {
        size_t copy_count = (pipe->tracks.count < max_count)
                           ? pipe->tracks.count : max_count;
        memcpy(tracks, pipe->tracks.tracks, copy_count);
    }

    return DSDPIPE_OK;
}

/*============================================================================
 * Sink Configuration
 *============================================================================*/

static int dsdpipe_add_sink_internal(dsdpipe_t *pipe, dsdpipe_sink_t *sink)
{
    if (pipe->sink_count >= DSDPIPE_MAX_SINKS) {
        dsdpipe_sink_destroy(sink);
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_INVALID_ARG,
                          "Maximum number of sinks (%d) exceeded",
                          DSDPIPE_MAX_SINKS);
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    pipe->sinks[pipe->sink_count++] = sink;
    return DSDPIPE_OK;
}

int dsdpipe_add_sink_dsf(dsdpipe_t *pipe, const char *output_path, bool write_id3)
{
    if (!pipe || !output_path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dsdpipe_sink_config_t config = {0};
    config.type = DSDPIPE_SINK_DSF;
    config.path = dsdpipe_strdup(output_path);
    config.track_filename_format = pipe->track_filename_format;
    config.opts.dsf.write_id3 = write_id3;

    if (!config.path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    dsdpipe_sink_t *sink = NULL;
    int result = dsdpipe_sink_dsf_create(&sink, &config);
    if (result != DSDPIPE_OK) {
        sa_free(config.path);
        dsdpipe_set_error(pipe, result, "Failed to create DSF sink");
        return result;
    }

    return dsdpipe_add_sink_internal(pipe, sink);
}

int dsdpipe_add_sink_dsdiff(dsdpipe_t *pipe, const char *output_path,
                             bool write_dst, bool edit_master, bool write_id3)
{
    if (!pipe || !output_path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dsdpipe_sink_config_t config = {0};
    config.type = edit_master ? DSDPIPE_SINK_DSDIFF_EDIT : DSDPIPE_SINK_DSDIFF;
    config.path = dsdpipe_strdup(output_path);
    config.track_filename_format = pipe->track_filename_format;
    config.opts.dsdiff.write_dst = write_dst;
    config.opts.dsdiff.edit_master = edit_master;
    config.opts.dsdiff.write_id3 = write_id3;

    if (!config.path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    dsdpipe_sink_t *sink = NULL;
    int result = dsdpipe_sink_dsdiff_create(&sink, &config);
    if (result != DSDPIPE_OK) {
        sa_free(config.path);
        dsdpipe_set_error(pipe, result, "Failed to create DSDIFF sink");
        return result;
    }

    return dsdpipe_add_sink_internal(pipe, sink);
}

int dsdpipe_add_sink_wav(dsdpipe_t *pipe, const char *output_path,
                          int bit_depth, int sample_rate)
{
    if (!pipe || !output_path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Validate bit depth */
    if (bit_depth != 16 && bit_depth != 24 && bit_depth != 32) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_INVALID_ARG,
                          "Invalid bit depth %d (must be 16, 24, or 32)", bit_depth);
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dsdpipe_sink_config_t config = {0};
    config.type = DSDPIPE_SINK_WAV;
    config.path = dsdpipe_strdup(output_path);
    config.track_filename_format = pipe->track_filename_format;
    config.opts.wav.bit_depth = bit_depth;
    config.opts.wav.sample_rate = sample_rate;

    if (!config.path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    dsdpipe_sink_t *sink = NULL;
    int result = dsdpipe_sink_wav_create(&sink, &config);
    if (result != DSDPIPE_OK) {
        sa_free(config.path);
        dsdpipe_set_error(pipe, result, "Failed to create WAV sink");
        return result;
    }

    return dsdpipe_add_sink_internal(pipe, sink);
}

int dsdpipe_add_sink_flac(dsdpipe_t *pipe, const char *output_path,
                           int bit_depth, int compression)
{
    if (!pipe || !output_path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

#ifndef HAVE_LIBFLAC
    dsdpipe_set_error(pipe, DSDPIPE_ERROR_FLAC_UNAVAILABLE, NULL);
    return DSDPIPE_ERROR_FLAC_UNAVAILABLE;
#endif

    /* Validate bit depth */
    if (bit_depth != 16 && bit_depth != 24) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_INVALID_ARG,
                          "Invalid bit depth %d for FLAC (must be 16 or 24)", bit_depth);
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Validate compression */
    if (compression < 0 || compression > 8) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_INVALID_ARG,
                          "Invalid FLAC compression %d (must be 0-8)", compression);
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dsdpipe_sink_config_t config = {0};
    config.type = DSDPIPE_SINK_FLAC;
    config.path = dsdpipe_strdup(output_path);
    config.track_filename_format = pipe->track_filename_format;
    config.opts.flac.bit_depth = bit_depth;
    config.opts.flac.compression = compression;

    if (!config.path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    dsdpipe_sink_t *sink = NULL;
    int result = dsdpipe_sink_flac_create(&sink, &config);
    if (result != DSDPIPE_OK) {
        sa_free(config.path);
        dsdpipe_set_error(pipe, result, "Failed to create FLAC sink");
        return result;
    }

    return dsdpipe_add_sink_internal(pipe, sink);
}

int dsdpipe_add_sink_print(dsdpipe_t *pipe, const char *output_path)
{
    if (!pipe) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Allocate sink structure */
    dsdpipe_sink_t *sink = (dsdpipe_sink_t *)sa_calloc(1, sizeof(dsdpipe_sink_t));
    if (!sink) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_OUT_OF_MEMORY, "Failed to allocate print sink");
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    int result = dsdpipe_sink_print_create(sink);
    if (result != DSDPIPE_OK) {
        sa_free(sink);
        dsdpipe_set_error(pipe, result, "Failed to create print sink");
        return result;
    }

    /* Cache capabilities */
    sink->caps = sink->ops->get_capabilities(sink->ctx);

    /* Store path in config (NULL = stdout) */
    if (output_path) {
        sink->config.path = dsdpipe_strdup(output_path);
    }

    return dsdpipe_add_sink_internal(pipe, sink);
}

int dsdpipe_add_sink_xml(dsdpipe_t *pipe, const char *output_path)
{
    if (!pipe || !output_path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Allocate sink structure */
    dsdpipe_sink_t *sink = (dsdpipe_sink_t *)sa_calloc(1, sizeof(dsdpipe_sink_t));
    if (!sink) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_OUT_OF_MEMORY, "Failed to allocate XML sink");
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    int result = dsdpipe_sink_xml_create(sink);
    if (result != DSDPIPE_OK) {
        sa_free(sink);
        dsdpipe_set_error(pipe, result, "Failed to create XML sink");
        return result;
    }

    /* Cache capabilities */
    sink->caps = sink->ops->get_capabilities(sink->ctx);

    /* Store path in config */
    sink->config.path = dsdpipe_strdup(output_path);
    if (!sink->config.path) {
        sink->ops->destroy(sink->ctx);
        sa_free(sink);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    return dsdpipe_add_sink_internal(pipe, sink);
}

int dsdpipe_add_sink_cue(dsdpipe_t *pipe, const char *output_path,
                          const char *audio_filename)
{
    if (!pipe || !output_path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Allocate sink structure */
    dsdpipe_sink_t *sink = (dsdpipe_sink_t *)sa_calloc(1, sizeof(dsdpipe_sink_t));
    if (!sink) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_OUT_OF_MEMORY, "Failed to allocate CUE sink");
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    int result = dsdpipe_sink_cue_create(sink, audio_filename);
    if (result != DSDPIPE_OK) {
        sa_free(sink);
        dsdpipe_set_error(pipe, result, "Failed to create CUE sheet sink");
        return result;
    }

    /* Cache capabilities */
    sink->caps = sink->ops->get_capabilities(sink->ctx);

    /* Store path in config */
    sink->config.path = dsdpipe_strdup(output_path);
    if (!sink->config.path) {
        sink->ops->destroy(sink->ctx);
        sa_free(sink);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    return dsdpipe_add_sink_internal(pipe, sink);
}

int dsdpipe_get_sink_count(dsdpipe_t *pipe)
{
    return pipe ? pipe->sink_count : 0;
}

int dsdpipe_clear_sinks(dsdpipe_t *pipe)
{
    if (!pipe) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    for (int i = 0; i < pipe->sink_count; i++) {
        dsdpipe_sink_destroy(pipe->sinks[i]);
        pipe->sinks[i] = NULL;
    }
    pipe->sink_count = 0;

    return DSDPIPE_OK;
}

/*============================================================================
 * Transformation Configuration
 *============================================================================*/

int dsdpipe_set_pcm_quality(dsdpipe_t *pipe, dsdpipe_pcm_quality_t quality)
{
    if (!pipe) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    pipe->pcm_quality = quality;
    return DSDPIPE_OK;
}

int dsdpipe_set_pcm_use_fp64(dsdpipe_t *pipe, bool use_fp64)
{
    if (!pipe) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    pipe->pcm_use_fp64 = use_fp64;
    return DSDPIPE_OK;
}

int dsdpipe_set_track_filename_format(dsdpipe_t *pipe,
                                       dsdpipe_track_format_t format)
{
    if (!pipe) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    pipe->track_filename_format = format;
    return DSDPIPE_OK;
}

dsdpipe_track_format_t dsdpipe_get_track_filename_format(dsdpipe_t *pipe)
{
    if (!pipe) {
        return DSDPIPE_TRACK_NUM_TITLE;  /* Default */
    }

    return pipe->track_filename_format;
}

/*============================================================================
 * Progress
 *============================================================================*/

int dsdpipe_set_progress_callback(dsdpipe_t *pipe, dsdpipe_progress_cb callback,
                                   void *userdata)
{
    if (!pipe) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    pipe->progress_callback = callback;
    pipe->progress_userdata = userdata;
    return DSDPIPE_OK;
}

static int dsdpipe_report_progress(dsdpipe_t *pipe)
{
    if (!pipe->progress_callback) {
        return 0;
    }

    return pipe->progress_callback(&pipe->progress, pipe->progress_userdata);
}

/*============================================================================
 * Cancellation
 *============================================================================*/

void dsdpipe_cancel(dsdpipe_t *pipe)
{
    if (pipe) {
        atomic_store(&pipe->cancelled, 1);
    }
}

bool dsdpipe_is_cancelled(dsdpipe_t *pipe)
{
    return pipe ? atomic_load(&pipe->cancelled) : false;
}

/*============================================================================
 * Pipeline Execution Helpers
 *============================================================================*/

/**
 * @brief Check if any sink needs PCM data
 */
static bool dsdpipe_needs_pcm(dsdpipe_t *pipe)
{
    for (int i = 0; i < pipe->sink_count; i++) {
        uint32_t caps = pipe->sinks[i]->caps;
        if ((caps & DSDPIPE_SINK_CAP_PCM) && !(caps & DSDPIPE_SINK_CAP_DSD)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check if any sink needs DSD data
 */
static bool dsdpipe_needs_dsd(dsdpipe_t *pipe)
{
    for (int i = 0; i < pipe->sink_count; i++) {
        uint32_t caps = pipe->sinks[i]->caps;
        if (caps & DSDPIPE_SINK_CAP_DSD) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check if any sink can accept DST directly
 */
static bool dsdpipe_can_accept_dst(dsdpipe_t *pipe)
{
    for (int i = 0; i < pipe->sink_count; i++) {
        uint32_t caps = pipe->sinks[i]->caps;
        if (caps & DSDPIPE_SINK_CAP_DST) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Setup transforms based on source format and sink requirements
 */
static int dsdpipe_setup_transforms(dsdpipe_t *pipe)
{
    dsdpipe_format_t src_format = pipe->source.format;
    bool need_dsd = dsdpipe_needs_dsd(pipe);
    bool need_pcm = dsdpipe_needs_pcm(pipe);
    bool can_dst = dsdpipe_can_accept_dst(pipe);

    /* If source is DST and we need DSD (or PCM), insert DST decoder */
    if (src_format.type == DSDPIPE_FORMAT_DST && (need_dsd || need_pcm) && !can_dst) {
        int result = dsdpipe_transform_dst_create(&pipe->dst_decoder);
        if (result != DSDPIPE_OK) {
            dsdpipe_set_error(pipe, result, "Failed to create DST decoder");
            return result;
        }

        /* Initialize decoder */
        dsdpipe_format_t dsd_format;
        result = pipe->dst_decoder->ops->init(pipe->dst_decoder->ctx,
                                              &src_format, &dsd_format);
        if (result != DSDPIPE_OK) {
            dsdpipe_set_error(pipe, DSDPIPE_ERROR_DST_DECODE,
                              "Failed to initialize DST decoder");
            return DSDPIPE_ERROR_DST_DECODE;
        }

        pipe->dst_decoder->is_initialized = true;
        pipe->dst_decoder->input_format = src_format;
        pipe->dst_decoder->output_format = dsd_format;

        /* Update effective source format */
        src_format = dsd_format;
    }

    /* If we need PCM, insert DSD-to-PCM converter */
    if (need_pcm) {
        /* Determine output sample rate (typically DSD rate / 32 or / 64) */
        int pcm_rate = src_format.sample_rate / 32;  /* Default: 88200 for DSD64 */

        int result = dsdpipe_transform_dsd2pcm_create(&pipe->dsd2pcm,
                                                       pipe->pcm_quality,
                                                       pipe->pcm_use_fp64,
                                                       pcm_rate);
        if (result != DSDPIPE_OK) {
            dsdpipe_set_error(pipe, result, "Failed to create DSD-to-PCM converter");
            return result;
        }

        /* Initialize converter */
        dsdpipe_format_t pcm_format;
        result = pipe->dsd2pcm->ops->init(pipe->dsd2pcm->ctx,
                                          &src_format, &pcm_format);
        if (result != DSDPIPE_OK) {
            dsdpipe_set_error(pipe, DSDPIPE_ERROR_PCM_CONVERT,
                              "Failed to initialize DSD-to-PCM converter");
            return DSDPIPE_ERROR_PCM_CONVERT;
        }

        pipe->dsd2pcm->is_initialized = true;
        pipe->dsd2pcm->input_format = src_format;
        pipe->dsd2pcm->output_format = pcm_format;
    }

    return DSDPIPE_OK;
}

/**
 * @brief Open all sinks
 */
static int dsdpipe_open_sinks(dsdpipe_t *pipe, const dsdpipe_metadata_t *album_meta)
{
    for (int i = 0; i < pipe->sink_count; i++) {
        dsdpipe_sink_t *sink = pipe->sinks[i];

        /* Pass track selection count to DSDIFF edit master sink for ID3 renumbering */
        if (sink->type == DSDPIPE_SINK_DSDIFF_EDIT) {
            dsdpipe_sink_dsdiff_set_track_count(sink->ctx, (uint8_t)pipe->tracks.count);
        }

        /* Determine the format this sink will receive */
        dsdpipe_format_t sink_format;
        if ((sink->caps & DSDPIPE_SINK_CAP_PCM) && pipe->dsd2pcm) {
            sink_format = pipe->dsd2pcm->output_format;
        } else if (pipe->dst_decoder) {
            sink_format = pipe->dst_decoder->output_format;
        } else {
            sink_format = pipe->source.format;
        }

        int result = sink->ops->open(sink->ctx, sink->config.path,
                                     &sink_format, album_meta);
        if (result != DSDPIPE_OK) {
            dsdpipe_set_error(pipe, DSDPIPE_ERROR_SINK_OPEN,
                              "Failed to open sink: %s", sink->config.path);
            return DSDPIPE_ERROR_SINK_OPEN;
        }
        sink->is_open = true;
    }

    return DSDPIPE_OK;
}

/**
 * @brief Close all sinks
 */
static void dsdpipe_close_sinks(dsdpipe_t *pipe)
{
    for (int i = 0; i < pipe->sink_count; i++) {
        dsdpipe_sink_t *sink = pipe->sinks[i];
        if (sink && sink->is_open && sink->ops && sink->ops->close) {
            sink->ops->close(sink->ctx);
            sink->is_open = false;
        }
    }
}

/**
 * @brief Write buffer to all sinks that accept it
 */
static int dsdpipe_write_to_sinks(dsdpipe_t *pipe, dsdpipe_buffer_t *buffer)
{
    bool is_pcm = (buffer->format.type == DSDPIPE_FORMAT_PCM_INT16 ||
                   buffer->format.type == DSDPIPE_FORMAT_PCM_INT24 ||
                   buffer->format.type == DSDPIPE_FORMAT_PCM_INT32 ||
                   buffer->format.type == DSDPIPE_FORMAT_PCM_FLOAT32 ||
                   buffer->format.type == DSDPIPE_FORMAT_PCM_FLOAT64);
    bool is_dst = (buffer->format.type == DSDPIPE_FORMAT_DST);
    bool is_dsd = (buffer->format.type == DSDPIPE_FORMAT_DSD_RAW);

    for (int i = 0; i < pipe->sink_count; i++) {
        dsdpipe_sink_t *sink = pipe->sinks[i];
        uint32_t caps = sink->caps;

        /* Check if sink accepts this format */
        bool accepts = false;
        if (is_pcm && (caps & DSDPIPE_SINK_CAP_PCM)) accepts = true;
        if (is_dst && (caps & DSDPIPE_SINK_CAP_DST)) accepts = true;
        if (is_dsd && (caps & DSDPIPE_SINK_CAP_DSD)) accepts = true;

        if (accepts) {
            int result = sink->ops->write_frame(sink->ctx, buffer);
            if (result != DSDPIPE_OK) {
                dsdpipe_set_error(pipe, DSDPIPE_ERROR_WRITE,
                                  "Write error to sink: %s", sink->config.path);
                return DSDPIPE_ERROR_WRITE;
            }
        }
    }

    return DSDPIPE_OK;
}

/*============================================================================
 * Batch Processing Constants and Helpers
 *============================================================================*/

/** Number of frames to process in a batch for DST decoding.
 * Smaller batches give more responsive progress updates at the cost of
 * slightly reduced throughput. 16 frames ≈ 0.2s of audio at 75 fps,
 * giving ~5 progress updates per second. */
#define DSDPIPE_BATCH_SIZE 16

/** Frame queue capacity for async reader.
 * Keep larger than batch size so reader stays ahead of processing. */
#define DSDPIPE_FRAME_QUEUE_CAPACITY 64

/**
 * @brief Process a single track with async reader and batch DST decoding
 *
 * Architecture:
 * - Reader thread reads frames from source → frame queue
 * - Main thread pops batch from queue → decodes in parallel → writes to sinks
 * - I/O overlaps with decode for maximum throughput
 */
static int dsdpipe_process_track(dsdpipe_t *pipe, uint8_t track_number)
{
    int result = DSDPIPE_OK;
    bool need_dst_decode = (pipe->dst_decoder != NULL &&
                            pipe->source.format.type == DSDPIPE_FORMAT_DST);
    dsdpipe_frame_queue_t *frame_queue = NULL;
    dsdpipe_reader_thread_t *reader = NULL;

    /* Get track metadata */
    dsdpipe_metadata_t track_meta;
    dsdpipe_metadata_init(&track_meta);
    pipe->source.ops->get_track_metadata(pipe->source.ctx, track_number, &track_meta);

    /* Update progress */
    pipe->progress.track_number = track_number;
    pipe->progress.track_title = track_meta.track_title;
    pipe->progress.frames_done = 0;

    /* Get total frames for this track */
    uint64_t total_frames = 0;
    if (pipe->source.ops->get_track_frames) {
        pipe->source.ops->get_track_frames(pipe->source.ctx, track_number, &total_frames);
    }
    pipe->progress.frames_total = total_frames;

    /* Debug: log total frames */
    if (total_frames > 0) {
        /* Successfully got frame count */
    } else {
        /* Fall back to estimate based on typical track sizes */
        /* ~75 SACD frames per second, estimate 5 minutes per track */
        total_frames = 75 * 60 * 5;
    }

    /* Notify sinks of track start */
    for (int i = 0; i < pipe->sink_count; i++) {
        if (pipe->sinks[i]->ops->track_start) {
            result = pipe->sinks[i]->ops->track_start(pipe->sinks[i]->ctx,
                                                      track_number, &track_meta);
            if (result != DSDPIPE_OK) {
                dsdpipe_set_error(pipe, result,
                                  "Failed to start track %d on sink %s",
                                  track_number, pipe->sinks[i]->config.path);
                dsdpipe_metadata_free(&track_meta);
                return result;
            }
        }
    }

    /* Create frame queue for async I/O */
    frame_queue = dsdpipe_frame_queue_create(DSDPIPE_FRAME_QUEUE_CAPACITY);
    if (!frame_queue) {
        dsdpipe_metadata_free(&track_meta);
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_OUT_OF_MEMORY,
                          "Failed to create frame queue");
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Create and start reader thread */
    reader = dsdpipe_reader_thread_create(pipe, frame_queue);
    if (!reader) {
        dsdpipe_frame_queue_destroy(frame_queue);
        dsdpipe_metadata_free(&track_meta);
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_OUT_OF_MEMORY,
                          "Failed to create reader thread");
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Start reading the track */
    if (dsdpipe_reader_thread_start_track(reader, track_number) != 0) {
        dsdpipe_reader_thread_destroy(reader);
        dsdpipe_frame_queue_destroy(frame_queue);
        dsdpipe_metadata_free(&track_meta);
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_READ, "Failed to start reader thread");
        return DSDPIPE_ERROR_READ;
    }

    /* Batch processing loop - reader pre-fetches frames in background */
    bool track_complete = false;

    while (!atomic_load(&pipe->cancelled) && !track_complete) {
        dsdpipe_buffer_t *batch_inputs[DSDPIPE_BATCH_SIZE];
        dsdpipe_buffer_t *batch_outputs[DSDPIPE_BATCH_SIZE];
        size_t batch_count = 0;

        /*
         * Phase 1: Pop batch of frames from queue (already read by reader thread)
         */
        result = dsdpipe_frame_queue_pop_batch(frame_queue, batch_inputs,
                                                 DSDPIPE_BATCH_SIZE,
                                                 &batch_count, &track_complete);
        if (result != 0) {
            /* Queue cancelled or error */
            if (dsdpipe_reader_thread_has_error(reader)) {
                result = dsdpipe_reader_thread_get_error(reader);
                dsdpipe_set_error(pipe, result, "Reader thread error during batch pop");
            } else {
                result = DSDPIPE_ERROR_READ;
                dsdpipe_set_error(pipe, result, "Frame queue error during batch pop");
            }
            break;
        }

        if (batch_count == 0) {
            /* No frames available and EOF - check if reader had error */
            if (dsdpipe_reader_thread_has_error(reader)) {
                result = dsdpipe_reader_thread_get_error(reader);
                dsdpipe_set_error(pipe, result, "Reader thread error");
            }
            /* Track done (normally or due to error) */
            break;
        }

        /* Initialize output array */
        for (size_t j = 0; j < batch_count; j++) {
            batch_outputs[j] = NULL;
        }

        /*
         * Phase 2: Allocate output buffers and decode batch in parallel
         */
        if (need_dst_decode) {
            /* Allocate output buffers */
            for (size_t j = 0; j < batch_count; j++) {
                batch_outputs[j] = dsdpipe_buffer_alloc_dsd(pipe);
                if (!batch_outputs[j]) {
                    /* Cleanup on allocation failure */
                    for (size_t k = 0; k < j; k++) {
                        if (batch_outputs[k]) dsdpipe_buffer_unref(batch_outputs[k]);
                    }
                    for (size_t k = 0; k < batch_count; k++) {
                        dsdpipe_buffer_unref(batch_inputs[k]);
                    }
                    dsdpipe_reader_thread_cancel(reader);
                    dsdpipe_reader_thread_wait(reader);
                    dsdpipe_reader_thread_destroy(reader);
                    dsdpipe_frame_queue_destroy(frame_queue);
                    dsdpipe_metadata_free(&track_meta);
                    return DSDPIPE_ERROR_OUT_OF_MEMORY;
                }
            }

            /* Build arrays for batch decode */
            const uint8_t *inputs[DSDPIPE_BATCH_SIZE];
            size_t input_sizes[DSDPIPE_BATCH_SIZE];
            uint8_t *outputs[DSDPIPE_BATCH_SIZE];
            size_t output_sizes[DSDPIPE_BATCH_SIZE];

            for (size_t j = 0; j < batch_count; j++) {
                inputs[j] = batch_inputs[j]->data;
                input_sizes[j] = batch_inputs[j]->size;
                outputs[j] = batch_outputs[j]->data;
                output_sizes[j] = 0;
            }

            /* Decode all frames in parallel */
            if (pipe->dst_decoder->ops->process_batch) {
                result = pipe->dst_decoder->ops->process_batch(pipe->dst_decoder->ctx,
                    inputs, input_sizes, outputs, output_sizes, batch_count);

                /* Update output buffer metadata */
                for (size_t j = 0; j < batch_count; j++) {
                    batch_outputs[j]->size = output_sizes[j];
                    batch_outputs[j]->format = pipe->dst_decoder->output_format;
                    batch_outputs[j]->frame_number = batch_inputs[j]->frame_number;
                    batch_outputs[j]->sample_offset = batch_inputs[j]->sample_offset;
                    batch_outputs[j]->track_number = batch_inputs[j]->track_number;
                    batch_outputs[j]->flags = batch_inputs[j]->flags;
                }
            } else {
                /* Fallback: decode sequentially */
                for (size_t j = 0; j < batch_count; j++) {
                    result = pipe->dst_decoder->ops->process(pipe->dst_decoder->ctx,
                                                             batch_inputs[j], batch_outputs[j]);
                    if (result != DSDPIPE_OK) break;
                }
            }

            if (result != DSDPIPE_OK) {
                dsdpipe_set_error(pipe, result, "DST decode error");
                for (size_t j = 0; j < batch_count; j++) {
                    dsdpipe_buffer_unref(batch_inputs[j]);
                    if (batch_outputs[j]) dsdpipe_buffer_unref(batch_outputs[j]);
                }
                break;
            }
        }

        /*
         * Phase 3: Write batch of frames to sinks (in order)
         *
         * For DSD-to-PCM conversion, we use batch processing to improve
         * performance - converting multiple frames in one call allows better
         * utilization of the TBB parallel channel processing in libdsdpcm.
         */

        /* Write DSD to sinks first (if any need it) */
        if (dsdpipe_needs_dsd(pipe)) {
            for (size_t j = 0; j < batch_count; j++) {
                dsdpipe_buffer_t *dsd_buffer = need_dst_decode ? batch_outputs[j] : batch_inputs[j];
                result = dsdpipe_write_to_sinks(pipe, dsd_buffer);
                if (result != DSDPIPE_OK) {
                    for (size_t k = j; k < batch_count; k++) {
                        dsdpipe_buffer_unref(batch_inputs[k]);
                        if (batch_outputs[k]) dsdpipe_buffer_unref(batch_outputs[k]);
                    }
                    goto cleanup;
                }
            }
        }

        /* Batch convert DSD to PCM if needed */
        if (pipe->dsd2pcm && dsdpipe_needs_pcm(pipe) && pipe->dsd2pcm->ops->process_batch) {
            /* Allocate PCM buffers for entire batch */
            dsdpipe_buffer_t *pcm_buffers[DSDPIPE_BATCH_SIZE];
            const uint8_t *dsd_inputs[DSDPIPE_BATCH_SIZE];
            size_t dsd_sizes[DSDPIPE_BATCH_SIZE];
            uint8_t *pcm_outputs[DSDPIPE_BATCH_SIZE];
            size_t pcm_sizes[DSDPIPE_BATCH_SIZE];

            for (size_t j = 0; j < batch_count; j++) {
                pcm_buffers[j] = dsdpipe_buffer_alloc_pcm(pipe);
                if (!pcm_buffers[j]) {
                    /* Free allocated PCM buffers on failure */
                    for (size_t k = 0; k < j; k++) {
                        dsdpipe_buffer_unref(pcm_buffers[k]);
                    }
                    for (size_t k = 0; k < batch_count; k++) {
                        dsdpipe_buffer_unref(batch_inputs[k]);
                        if (batch_outputs[k]) dsdpipe_buffer_unref(batch_outputs[k]);
                    }
                    result = DSDPIPE_ERROR_OUT_OF_MEMORY;
                    dsdpipe_set_error(pipe, result, "Failed to allocate PCM buffers for batch");
                    goto cleanup;
                }

                /* Build arrays for batch conversion */
                dsdpipe_buffer_t *dsd_buffer = need_dst_decode ? batch_outputs[j] : batch_inputs[j];
                dsd_inputs[j] = dsd_buffer->data;
                dsd_sizes[j] = dsd_buffer->size;
                pcm_outputs[j] = pcm_buffers[j]->data;
                pcm_sizes[j] = 0;
            }

            /* Batch convert all DSD frames to PCM in one call */
            result = pipe->dsd2pcm->ops->process_batch(
                pipe->dsd2pcm->ctx,
                dsd_inputs, dsd_sizes,
                pcm_outputs, pcm_sizes,
                batch_count
            );

            if (result != DSDPIPE_OK) {
                for (size_t j = 0; j < batch_count; j++) {
                    dsdpipe_buffer_unref(pcm_buffers[j]);
                }
                for (size_t k = 0; k < batch_count; k++) {
                    dsdpipe_buffer_unref(batch_inputs[k]);
                    if (batch_outputs[k]) dsdpipe_buffer_unref(batch_outputs[k]);
                }
                dsdpipe_set_error(pipe, result, "DSD-to-PCM batch conversion failed");
                goto cleanup;
            }

            /* Write PCM buffers to sinks and update metadata */
            for (size_t j = 0; j < batch_count; j++) {
                dsdpipe_buffer_t *dsd_buffer = need_dst_decode ? batch_outputs[j] : batch_inputs[j];
                pcm_buffers[j]->size = pcm_sizes[j];
                pcm_buffers[j]->format = pipe->dsd2pcm->output_format;
                pcm_buffers[j]->frame_number = dsd_buffer->frame_number;
                pcm_buffers[j]->sample_offset = dsd_buffer->sample_offset;
                pcm_buffers[j]->track_number = dsd_buffer->track_number;
                pcm_buffers[j]->flags = dsd_buffer->flags;

                result = dsdpipe_write_to_sinks(pipe, pcm_buffers[j]);
                dsdpipe_buffer_unref(pcm_buffers[j]);

                if (result != DSDPIPE_OK) {
                    /* Free remaining PCM buffers */
                    for (size_t k = j + 1; k < batch_count; k++) {
                        dsdpipe_buffer_unref(pcm_buffers[k]);
                    }
                    for (size_t k = 0; k < batch_count; k++) {
                        dsdpipe_buffer_unref(batch_inputs[k]);
                        if (batch_outputs[k]) dsdpipe_buffer_unref(batch_outputs[k]);
                    }
                    dsdpipe_set_error(pipe, result, "Failed to write PCM data to sink");
                    goto cleanup;
                }
            }
        } else if (pipe->dsd2pcm && dsdpipe_needs_pcm(pipe)) {
            /* Fallback: frame-by-frame conversion if no batch support */
            for (size_t j = 0; j < batch_count; j++) {
                dsdpipe_buffer_t *dsd_buffer = need_dst_decode ? batch_outputs[j] : batch_inputs[j];
                dsdpipe_buffer_t *pcm_buffer = dsdpipe_buffer_alloc_pcm(pipe);
                if (!pcm_buffer) {
                    for (size_t k = j; k < batch_count; k++) {
                        dsdpipe_buffer_unref(batch_inputs[k]);
                        if (batch_outputs[k]) dsdpipe_buffer_unref(batch_outputs[k]);
                    }
                    result = DSDPIPE_ERROR_OUT_OF_MEMORY;
                    dsdpipe_set_error(pipe, result, "Failed to allocate PCM buffer");
                    goto cleanup;
                }

                result = pipe->dsd2pcm->ops->process(pipe->dsd2pcm->ctx,
                                                     dsd_buffer, pcm_buffer);
                if (result != DSDPIPE_OK) {
                    dsdpipe_buffer_unref(pcm_buffer);
                    for (size_t k = j; k < batch_count; k++) {
                        dsdpipe_buffer_unref(batch_inputs[k]);
                        if (batch_outputs[k]) dsdpipe_buffer_unref(batch_outputs[k]);
                    }
                    dsdpipe_set_error(pipe, result, "DSD-to-PCM conversion failed");
                    goto cleanup;
                }

                result = dsdpipe_write_to_sinks(pipe, pcm_buffer);
                dsdpipe_buffer_unref(pcm_buffer);

                if (result != DSDPIPE_OK) {
                    for (size_t k = j; k < batch_count; k++) {
                        dsdpipe_buffer_unref(batch_inputs[k]);
                        if (batch_outputs[k]) dsdpipe_buffer_unref(batch_outputs[k]);
                    }
                    dsdpipe_set_error(pipe, result, "Failed to write PCM data to sink");
                    goto cleanup;
                }
            }
        }

        /* Update progress and release buffers */
        for (size_t j = 0; j < batch_count; j++) {
            dsdpipe_buffer_t *dsd_buffer = need_dst_decode ? batch_outputs[j] : batch_inputs[j];
            pipe->progress.bytes_written += dsd_buffer->size;
            pipe->progress.frames_done++;

            dsdpipe_buffer_unref(batch_inputs[j]);
            if (batch_outputs[j]) dsdpipe_buffer_unref(batch_outputs[j]);
        }

        /* Update progress */
        if (total_frames > 0) {
            pipe->progress.track_percent =
                (float)pipe->progress.frames_done / (float)total_frames * 100.0f;
        }

        /* Compute overall progress from completed tracks + current track fraction */
        if (pipe->tracks.count > 0) {
            float completed_fraction = (float)pipe->tracks.current_idx / (float)pipe->tracks.count;
            float current_fraction = (pipe->progress.track_percent / 100.0f) / (float)pipe->tracks.count;
            pipe->progress.total_percent = (completed_fraction + current_fraction) * 100.0f;
        }

        /* Report progress */
        if (dsdpipe_report_progress(pipe) != 0) {
            atomic_store(&pipe->cancelled, 1);
            result = DSDPIPE_ERROR_CANCELLED;
            break;
        }
    }

cleanup:
    /* Wait for reader thread to finish */
    if (reader) {
        if (result != DSDPIPE_OK || atomic_load(&pipe->cancelled)) {
            dsdpipe_reader_thread_cancel(reader);
        }
        dsdpipe_reader_thread_wait(reader);
        dsdpipe_reader_thread_destroy(reader);
    }

    /* Destroy frame queue */
    if (frame_queue) {
        dsdpipe_frame_queue_destroy(frame_queue);
    }

    /* Notify sinks of track end */
    for (int i = 0; i < pipe->sink_count; i++) {
        if (pipe->sinks[i]->ops->track_end) {
            pipe->sinks[i]->ops->track_end(pipe->sinks[i]->ctx, track_number);
        }
    }

    dsdpipe_metadata_free(&track_meta);

    if (atomic_load(&pipe->cancelled)) {
        return DSDPIPE_ERROR_CANCELLED;
    }

    return result;
}

/*============================================================================
 * Main Run Function
 *============================================================================*/

int dsdpipe_run(dsdpipe_t *pipe)
{
    if (!pipe) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Validate state */
    if (pipe->source.type == DSDPIPE_SOURCE_NONE) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_NO_SOURCE, NULL);
        return DSDPIPE_ERROR_NO_SOURCE;
    }

    if (pipe->sink_count == 0) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_NO_SINKS, NULL);
        return DSDPIPE_ERROR_NO_SINKS;
    }

    if (pipe->state == DSDPIPE_STATE_RUNNING) {
        dsdpipe_set_error(pipe, DSDPIPE_ERROR_ALREADY_RUNNING, NULL);
        return DSDPIPE_ERROR_ALREADY_RUNNING;
    }

    /* Select all tracks if none selected */
    if (pipe->tracks.count == 0) {
        int result = dsdpipe_select_all_tracks(pipe);
        if (result != DSDPIPE_OK) {
            return result;
        }
    }

    /* Initialize buffer pools */
    int result = dsdpipe_init_pools(pipe);
    if (result != DSDPIPE_OK) {
        return result;
    }

    /* Get sink capabilities */
    for (int i = 0; i < pipe->sink_count; i++) {
        pipe->sinks[i]->caps = pipe->sinks[i]->ops->get_capabilities(pipe->sinks[i]->ctx);
    }

    /* Setup transforms based on source and sink requirements */
    result = dsdpipe_setup_transforms(pipe);
    if (result != DSDPIPE_OK) {
        return result;
    }

    /* Get album metadata */
    dsdpipe_metadata_t album_meta;
    dsdpipe_metadata_init(&album_meta);
    pipe->source.ops->get_album_metadata(pipe->source.ctx, &album_meta);

    /* Open all sinks */
    result = dsdpipe_open_sinks(pipe, &album_meta);
    if (result != DSDPIPE_OK) {
        dsdpipe_metadata_free(&album_meta);
        return result;
    }

    /* Set running state */
    pipe->state = DSDPIPE_STATE_RUNNING;
    atomic_store(&pipe->cancelled, 0);

    /* Initialize progress */
    pipe->progress.track_total = (uint8_t)pipe->tracks.count;
    pipe->progress.total_percent = 0.0f;
    pipe->progress.bytes_written = 0;

    /* Process each selected track */
    for (size_t i = 0; i < pipe->tracks.count; i++) {
        uint8_t track_num = pipe->tracks.tracks[i];

        /* Store selection index for track renumbering in edit master mode */
        pipe->tracks.current_idx = i;

        result = dsdpipe_process_track(pipe, track_num);
        if (result != DSDPIPE_OK) {
            break;
        }

        /* Update overall progress */
        pipe->progress.total_percent =
            (float)(i + 1) / (float)pipe->tracks.count * 100.0f;
    }

    /* Finalize sinks */
    for (int i = 0; i < pipe->sink_count; i++) {
        if (pipe->sinks[i]->is_open && pipe->sinks[i]->ops->finalize) {
            pipe->sinks[i]->ops->finalize(pipe->sinks[i]->ctx);
        }
    }

    /* Close sinks */
    dsdpipe_close_sinks(pipe);

    /* Cleanup */
    dsdpipe_metadata_free(&album_meta);

    /* Update state */
    pipe->state = (result == DSDPIPE_OK) ? DSDPIPE_STATE_FINISHED
                                          : DSDPIPE_STATE_ERROR;

    return result;
}

/*============================================================================
 * Metadata Functions
 *============================================================================*/

int dsdpipe_get_album_metadata(dsdpipe_t *pipe, dsdpipe_metadata_t *metadata)
{
    if (!pipe || !metadata) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (pipe->source.type == DSDPIPE_SOURCE_NONE || !pipe->source.ops) {
        return DSDPIPE_ERROR_NO_SOURCE;
    }

    dsdpipe_metadata_init(metadata);
    return pipe->source.ops->get_album_metadata(pipe->source.ctx, metadata);
}

int dsdpipe_get_track_metadata(dsdpipe_t *pipe, uint8_t track_number,
                                dsdpipe_metadata_t *metadata)
{
    if (!pipe || !metadata) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (pipe->source.type == DSDPIPE_SOURCE_NONE || !pipe->source.ops) {
        return DSDPIPE_ERROR_NO_SOURCE;
    }

    dsdpipe_metadata_init(metadata);
    return pipe->source.ops->get_track_metadata(pipe->source.ctx, track_number,
                                                metadata);
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

bool dsdpipe_has_flac_support(void)
{
#ifdef HAVE_LIBFLAC
    return true;
#else
    return false;
#endif
}

const char *dsdpipe_version_string(void)
{
    return DSDPIPE_VERSION_STRING;
}

int dsdpipe_version_int(void)
{
    return DSDPIPE_VERSION_INT;
}

/*============================================================================
 * Source/Sink Destroy Helpers
 *============================================================================*/

void dsdpipe_source_destroy(dsdpipe_source_t *source)
{
    if (!source) {
        return;
    }

    if (source->is_open && source->ops && source->ops->close) {
        source->ops->close(source->ctx);
    }

    if (source->ops && source->ops->destroy) {
        source->ops->destroy(source->ctx);
    }

    source->type = DSDPIPE_SOURCE_NONE;
    source->ops = NULL;
    source->ctx = NULL;
    source->is_open = false;
}

void dsdpipe_sink_destroy(dsdpipe_sink_t *sink)
{
    if (!sink) {
        return;
    }

    if (sink->is_open && sink->ops && sink->ops->close) {
        sink->ops->close(sink->ctx);
    }

    if (sink->ops && sink->ops->destroy) {
        sink->ops->destroy(sink->ctx);
    }

    if (sink->config.path) {
        sa_free(sink->config.path);
    }

    sa_free(sink);
}

void dsdpipe_transform_destroy(dsdpipe_transform_t *transform)
{
    if (!transform) {
        return;
    }

    if (transform->ops && transform->ops->destroy) {
        transform->ops->destroy(transform->ctx);
    }

    sa_free(transform);
}
