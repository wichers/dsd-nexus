/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSD to PCM conversion transform implementation using libdsdpcm
 * This transform converts DSD (Direct Stream Digital) audio data to PCM
 * (Pulse Code Modulation) using the libdsdpcm library. It supports multiple
 * quality modes and both 32-bit and 64-bit floating point precision.
 * Quality mapping:
 * - DSDPIPE_PCM_QUALITY_FAST   -> DSDPCM_CONV_DIRECT (30kHz lowpass)
 * - DSDPIPE_PCM_QUALITY_NORMAL -> DSDPCM_CONV_MULTISTAGE (best quality)
 * - DSDPIPE_PCM_QUALITY_HIGH   -> DSDPCM_CONV_MULTISTAGE with FP64
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

#include <stdlib.h>
#include <string.h>

#include <libdsdpcm/dsdpcm.h>
#include <libsautil/mem.h>

/*============================================================================
 * Constants
 *============================================================================*/

/** Default PCM sample rate when not specified (DSD64 / 32 = 88200 Hz) */
#define DSD2PCM_DEFAULT_DECIMATION 32

/** DSD64 sample rate */
#define DSD64_SAMPLE_RATE 2822400

/** SACD frame rate */
#define SACD_FRAME_RATE 75

/** Maximum batch size for concatenated DSD data */
#define DSD2PCM_MAX_BATCH_SIZE 32

/** DSD bytes per channel per SACD frame (588 samples * 8 bits = 4704) */
#define DSD_BYTES_PER_CHANNEL_FRAME 4704

/*============================================================================
 * DSD2PCM Transform Context
 *============================================================================*/

typedef struct dsdpipe_transform_dsd2pcm_ctx_s {
    /* libdsdpcm decoder handle */
    dsdpcm_decoder_t *decoder;

    /* Configuration */
    dsdpipe_pcm_quality_t quality;
    bool use_fp64;
    int pcm_sample_rate;

    /* Format information */
    dsdpipe_format_t input_format;
    dsdpipe_format_t output_format;
    bool is_initialized;

    /* Cached conversion parameters */
    dsdpcm_conv_type_t conv_type;
    dsdpcm_precision_t precision;

    /* Batch processing buffers (allocated on first batch call) */
    uint8_t *batch_dsd_buffer;        /**< Concatenated DSD input */
    uint8_t *batch_pcm_buffer;        /**< Concatenated PCM output */
    size_t batch_dsd_capacity;        /**< Capacity of DSD buffer */
    size_t batch_pcm_capacity;        /**< Capacity of PCM buffer */

    /* Statistics */
    uint64_t frames_processed;
    uint64_t samples_out;
    uint64_t bytes_in;
    uint64_t bytes_out;
} dsdpipe_transform_dsd2pcm_ctx_t;

/*============================================================================
 * Helper: Map quality to conversion type
 *============================================================================*/

static dsdpcm_conv_type_t quality_to_conv_type(dsdpipe_pcm_quality_t quality)
{
    switch (quality) {
        case DSDPIPE_PCM_QUALITY_FAST:
            return DSDPCM_CONV_DIRECT;
        case DSDPIPE_PCM_QUALITY_NORMAL:
        case DSDPIPE_PCM_QUALITY_HIGH:
        default:
            return DSDPCM_CONV_MULTISTAGE;
    }
}

/*============================================================================
 * Transform Operations
 *============================================================================*/

static int dsd2pcm_transform_init(void *ctx, const dsdpipe_format_t *input_format,
                                  dsdpipe_format_t *output_format)
{
    dsdpipe_transform_dsd2pcm_ctx_t *dsd2pcm_ctx =
        (dsdpipe_transform_dsd2pcm_ctx_t *)ctx;
    int ret;

    if (!dsd2pcm_ctx || !input_format || !output_format) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Input must be DSD */
    if (input_format->type != DSDPIPE_FORMAT_DSD_RAW) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Validate channel count */
    if (input_format->channel_count < 1 || input_format->channel_count > 6) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dsd2pcm_ctx->input_format = *input_format;

    /* Calculate output sample rate if not specified */
    if (dsd2pcm_ctx->pcm_sample_rate == 0) {
        /* Default: DSD rate / 32 (e.g., 88200 for DSD64) */
        dsd2pcm_ctx->pcm_sample_rate = (int)(input_format->sample_rate / DSD2PCM_DEFAULT_DECIMATION);
    }

    /* Determine conversion type and precision */
    dsd2pcm_ctx->conv_type = quality_to_conv_type(dsd2pcm_ctx->quality);
    dsd2pcm_ctx->precision = dsd2pcm_ctx->use_fp64
                             ? DSDPCM_PRECISION_FP64
                             : DSDPCM_PRECISION_FP32;

    /* Create libdsdpcm decoder if not already created */
    if (!dsd2pcm_ctx->decoder) {
        dsd2pcm_ctx->decoder = dsdpcm_create();
        if (!dsd2pcm_ctx->decoder) {
            return DSDPIPE_ERROR_OUT_OF_MEMORY;
        }
    }

    /* Initialize the decoder */
    ret = dsdpcm_init(
        dsd2pcm_ctx->decoder,
        input_format->channel_count,
        input_format->frame_rate > 0 ? input_format->frame_rate : SACD_FRAME_RATE,
        input_format->sample_rate,
        (size_t)dsd2pcm_ctx->pcm_sample_rate,
        dsd2pcm_ctx->conv_type,
        dsd2pcm_ctx->precision,
        NULL  /* No custom FIR filter */
    );

    if (ret != DSDPCM_OK) {
        return DSDPIPE_ERROR_PCM_CONVERT;
    }

    /* Setup output format */
    dsd2pcm_ctx->output_format.type = dsd2pcm_ctx->use_fp64
                                      ? DSDPIPE_FORMAT_PCM_FLOAT64
                                      : DSDPIPE_FORMAT_PCM_FLOAT32;
    dsd2pcm_ctx->output_format.sample_rate = (uint32_t)dsd2pcm_ctx->pcm_sample_rate;
    dsd2pcm_ctx->output_format.channel_count = input_format->channel_count;
    dsd2pcm_ctx->output_format.bits_per_sample = dsd2pcm_ctx->use_fp64 ? 64 : 32;
    dsd2pcm_ctx->output_format.frame_rate = input_format->frame_rate;

    *output_format = dsd2pcm_ctx->output_format;

    /* Reset statistics */
    dsd2pcm_ctx->frames_processed = 0;
    dsd2pcm_ctx->samples_out = 0;
    dsd2pcm_ctx->bytes_in = 0;
    dsd2pcm_ctx->bytes_out = 0;

    dsd2pcm_ctx->is_initialized = true;
    return DSDPIPE_OK;
}

static int dsd2pcm_transform_process(void *ctx, const dsdpipe_buffer_t *input,
                                     dsdpipe_buffer_t *output)
{
    dsdpipe_transform_dsd2pcm_ctx_t *dsd2pcm_ctx =
        (dsdpipe_transform_dsd2pcm_ctx_t *)ctx;
    int ret;
    size_t pcm_samples = 0;

    if (!dsd2pcm_ctx || !input || !output) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsd2pcm_ctx->is_initialized || !dsd2pcm_ctx->decoder) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* Perform the conversion based on precision */
    if (dsd2pcm_ctx->use_fp64) {
        ret = dsdpcm_convert_fp64(
            dsd2pcm_ctx->decoder,
            input->data,
            input->size,
            (dsdpcm_sample64_t *)output->data,
            &pcm_samples
        );
    } else {
        ret = dsdpcm_convert_fp32(
            dsd2pcm_ctx->decoder,
            input->data,
            input->size,
            (dsdpcm_sample32_t *)output->data,
            &pcm_samples
        );
    }

    if (ret != DSDPCM_OK) {
        return DSDPIPE_ERROR_PCM_CONVERT;
    }

    /* Calculate output size in bytes */
    size_t bytes_per_sample = dsd2pcm_ctx->use_fp64 ? sizeof(double) : sizeof(float);
    output->size = pcm_samples * bytes_per_sample;

    /* Copy metadata from input to output */
    output->format = dsd2pcm_ctx->output_format;
    output->frame_number = input->frame_number;
    output->sample_offset = input->sample_offset;
    output->track_number = input->track_number;
    output->flags = input->flags;

    /* Update statistics */
    dsd2pcm_ctx->frames_processed++;
    dsd2pcm_ctx->samples_out += pcm_samples;
    dsd2pcm_ctx->bytes_in += input->size;
    dsd2pcm_ctx->bytes_out += output->size;

    return DSDPIPE_OK;
}

/**
 * @brief Batch process multiple DSD frames with optimized single conversion
 *
 * Unlike DST's true parallel batch processing, DSD-to-PCM requires sequential
 * processing due to FIR filter state. However, by concatenating multiple frames
 * into a single large buffer and converting in one call, we allow the TBB-based
 * channel parallelism in libdsdpcm to work more efficiently with larger data.
 *
 * This approach significantly reduces per-frame overhead and improves
 * cache utilization.
 */
static int dsd2pcm_transform_process_batch(void *ctx,
                                            const uint8_t *inputs[],
                                            const size_t input_sizes[],
                                            uint8_t *outputs[],
                                            size_t output_sizes[],
                                            size_t count)
{
    dsdpipe_transform_dsd2pcm_ctx_t *dsd2pcm_ctx =
        (dsdpipe_transform_dsd2pcm_ctx_t *)ctx;

    if (!dsd2pcm_ctx || !inputs || !input_sizes || !outputs || !output_sizes) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dsd2pcm_ctx->is_initialized || !dsd2pcm_ctx->decoder) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (count == 0) {
        return DSDPIPE_OK;
    }

    /* Validate sample rates are set */
    if (dsd2pcm_ctx->output_format.sample_rate == 0 ||
        dsd2pcm_ctx->input_format.sample_rate == 0) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    /* Calculate total DSD input size */
    size_t total_dsd_size = 0;
    for (size_t i = 0; i < count; i++) {
        total_dsd_size += input_sizes[i];
    }

    /* Estimate PCM output size
     * DSD: 1 byte = 8 DSD samples (interleaved for all channels)
     * PCM: samples = DSD samples / decimation ratio
     * For DSD64 (2.8224MHz) -> 88.2kHz: decimation = 32
     */
    size_t decimation = dsd2pcm_ctx->input_format.sample_rate /
                        dsd2pcm_ctx->output_format.sample_rate;
    if (decimation == 0) {
        decimation = 32;  /* Default for DSD64 -> 88.2kHz */
    }
    size_t bytes_per_sample = dsd2pcm_ctx->use_fp64 ? sizeof(double) : sizeof(float);
    size_t est_pcm_samples = (total_dsd_size * 8) / decimation;
    est_pcm_samples += 4096;  /* Margin for filter delay */
    size_t required_pcm_size = est_pcm_samples * bytes_per_sample;

    /* Ensure batch buffers are allocated and large enough */
    if (dsd2pcm_ctx->batch_dsd_capacity < total_dsd_size) {
        uint8_t *new_buf = (uint8_t *)sa_realloc(dsd2pcm_ctx->batch_dsd_buffer,
                                                  total_dsd_size);
        if (!new_buf) {
            return DSDPIPE_ERROR_OUT_OF_MEMORY;
        }
        dsd2pcm_ctx->batch_dsd_buffer = new_buf;
        dsd2pcm_ctx->batch_dsd_capacity = total_dsd_size;
    }

    if (dsd2pcm_ctx->batch_pcm_capacity < required_pcm_size) {
        uint8_t *new_buf = (uint8_t *)sa_realloc(dsd2pcm_ctx->batch_pcm_buffer,
                                                  required_pcm_size);
        if (!new_buf) {
            return DSDPIPE_ERROR_OUT_OF_MEMORY;
        }
        dsd2pcm_ctx->batch_pcm_buffer = new_buf;
        dsd2pcm_ctx->batch_pcm_capacity = required_pcm_size;
    }

    /* Concatenate all DSD input frames */
    size_t offset = 0;
    for (size_t i = 0; i < count; i++) {
        memcpy(dsd2pcm_ctx->batch_dsd_buffer + offset, inputs[i], input_sizes[i]);
        offset += input_sizes[i];
    }

    /* Convert all DSD data in one call */
    int ret;
    size_t total_pcm_samples = 0;

    if (dsd2pcm_ctx->use_fp64) {
        ret = dsdpcm_convert_fp64(
            dsd2pcm_ctx->decoder,
            dsd2pcm_ctx->batch_dsd_buffer,
            total_dsd_size,
            (dsdpcm_sample64_t *)dsd2pcm_ctx->batch_pcm_buffer,
            &total_pcm_samples
        );
    } else {
        ret = dsdpcm_convert_fp32(
            dsd2pcm_ctx->decoder,
            dsd2pcm_ctx->batch_dsd_buffer,
            total_dsd_size,
            (dsdpcm_sample32_t *)dsd2pcm_ctx->batch_pcm_buffer,
            &total_pcm_samples
        );
    }

    if (ret != DSDPCM_OK) {
        return DSDPIPE_ERROR_PCM_CONVERT;
    }

    /* Distribute PCM output back to individual frame buffers */
    /* Each frame should get proportional PCM output based on input size ratio */
    size_t pcm_offset = 0;
    size_t total_output_bytes = total_pcm_samples * bytes_per_sample;

    for (size_t i = 0; i < count; i++) {
        /* Calculate this frame's share of PCM samples (proportional to DSD input) */
        size_t frame_pcm_samples;
        if (i == count - 1) {
            /* Last frame gets remaining samples */
            frame_pcm_samples = total_pcm_samples - (pcm_offset / bytes_per_sample);
        } else {
            /* Proportional distribution based on DSD input size */
            frame_pcm_samples = (total_pcm_samples * input_sizes[i]) / total_dsd_size;
        }

        size_t frame_pcm_bytes = frame_pcm_samples * bytes_per_sample;

        /* Copy PCM data to output buffer */
        if (pcm_offset + frame_pcm_bytes <= total_output_bytes) {
            memcpy(outputs[i], dsd2pcm_ctx->batch_pcm_buffer + pcm_offset, frame_pcm_bytes);
            output_sizes[i] = frame_pcm_bytes;
        } else {
            /* Handle edge case: not enough samples remaining */
            size_t remaining = total_output_bytes - pcm_offset;
            if (remaining > 0) {
                memcpy(outputs[i], dsd2pcm_ctx->batch_pcm_buffer + pcm_offset, remaining);
            }
            output_sizes[i] = remaining;
        }

        pcm_offset += frame_pcm_bytes;

        /* Update statistics */
        dsd2pcm_ctx->frames_processed++;
        dsd2pcm_ctx->bytes_in += input_sizes[i];
        dsd2pcm_ctx->bytes_out += output_sizes[i];
    }

    dsd2pcm_ctx->samples_out += total_pcm_samples;

    return DSDPIPE_OK;
}

static int dsd2pcm_transform_flush(void *ctx, dsdpipe_buffer_t *output)
{
    dsdpipe_transform_dsd2pcm_ctx_t *dsd2pcm_ctx =
        (dsdpipe_transform_dsd2pcm_ctx_t *)ctx;

    if (!dsd2pcm_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /*
     * libdsdpcm uses FIR filters which have inherent delay.
     * The filter delay can be queried via dsdpcm_get_delay().
     * However, for simplicity, we don't flush tail samples here.
     * The filter delay is typically small and the audio quality
     * impact of not flushing is minimal for typical use cases.
     *
     * If precise sample-accurate output is needed, the caller
     * should account for the filter delay separately.
     */
    (void)output;

    return DSDPIPE_OK;
}

static void dsd2pcm_transform_reset(void *ctx)
{
    dsdpipe_transform_dsd2pcm_ctx_t *dsd2pcm_ctx =
        (dsdpipe_transform_dsd2pcm_ctx_t *)ctx;

    if (!dsd2pcm_ctx) {
        return;
    }

    /* Free and recreate the decoder to reset internal filter state */
    if (dsd2pcm_ctx->decoder) {
        dsdpcm_free(dsd2pcm_ctx->decoder);
    }

    /* Free batch buffers - will be reallocated on demand */
    if (dsd2pcm_ctx->batch_dsd_buffer) {
        sa_free(dsd2pcm_ctx->batch_dsd_buffer);
        dsd2pcm_ctx->batch_dsd_buffer = NULL;
        dsd2pcm_ctx->batch_dsd_capacity = 0;
    }
    if (dsd2pcm_ctx->batch_pcm_buffer) {
        sa_free(dsd2pcm_ctx->batch_pcm_buffer);
        dsd2pcm_ctx->batch_pcm_buffer = NULL;
        dsd2pcm_ctx->batch_pcm_capacity = 0;
    }

    /* Reset statistics */
    dsd2pcm_ctx->frames_processed = 0;
    dsd2pcm_ctx->samples_out = 0;
    dsd2pcm_ctx->bytes_in = 0;
    dsd2pcm_ctx->bytes_out = 0;

    dsd2pcm_ctx->is_initialized = false;
}

static void dsd2pcm_transform_destroy(void *ctx)
{
    dsdpipe_transform_dsd2pcm_ctx_t *dsd2pcm_ctx =
        (dsdpipe_transform_dsd2pcm_ctx_t *)ctx;

    if (!dsd2pcm_ctx) {
        return;
    }

    /* Destroy libdsdpcm decoder */
    if (dsd2pcm_ctx->decoder) {
        dsdpcm_destroy(dsd2pcm_ctx->decoder);
        dsd2pcm_ctx->decoder = NULL;
    }

    /* Free batch buffers */
    if (dsd2pcm_ctx->batch_dsd_buffer) {
        sa_free(dsd2pcm_ctx->batch_dsd_buffer);
        dsd2pcm_ctx->batch_dsd_buffer = NULL;
    }
    if (dsd2pcm_ctx->batch_pcm_buffer) {
        sa_free(dsd2pcm_ctx->batch_pcm_buffer);
        dsd2pcm_ctx->batch_pcm_buffer = NULL;
    }

    sa_free(dsd2pcm_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_transform_ops_t s_dsd2pcm_transform_ops = {
    .init = dsd2pcm_transform_init,
    .process = dsd2pcm_transform_process,
    /*
     * Batch processing enabled: The C wrapper (dsdpcm_wrapper.cpp) now
     * handles large inputs by internally chunking them into frame-sized
     * pieces before passing to the C++ engine. This allows batch processing
     * at the pipeline level while respecting the engine's buffer constraints.
     */
    .process_batch = dsd2pcm_transform_process_batch,
    .flush = dsd2pcm_transform_flush,
    .reset = dsd2pcm_transform_reset,
    .destroy = dsd2pcm_transform_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_transform_dsd2pcm_create(dsdpipe_transform_t **transform,
                                      dsdpipe_pcm_quality_t quality,
                                      bool use_fp64,
                                      int pcm_sample_rate)
{
    if (!transform) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Validate quality parameter */
    if (quality < DSDPIPE_PCM_QUALITY_FAST || quality > DSDPIPE_PCM_QUALITY_HIGH) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Validate sample rate if specified */
    if (pcm_sample_rate < 0) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dsdpipe_transform_t *new_transform =
        (dsdpipe_transform_t *)sa_calloc(1, sizeof(*new_transform));
    if (!new_transform) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    dsdpipe_transform_dsd2pcm_ctx_t *ctx =
        (dsdpipe_transform_dsd2pcm_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        sa_free(new_transform);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Store configuration */
    ctx->quality = quality;
    ctx->use_fp64 = use_fp64;
    ctx->pcm_sample_rate = pcm_sample_rate;
    ctx->decoder = NULL;
    ctx->is_initialized = false;

    new_transform->ops = &s_dsd2pcm_transform_ops;
    new_transform->ctx = ctx;
    new_transform->is_initialized = false;

    *transform = new_transform;
    return DSDPIPE_OK;
}
