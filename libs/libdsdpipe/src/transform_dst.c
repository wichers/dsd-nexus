/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DST decoder transform implementation using libdst batch decoder
 * This transform decodes DST (Direct Stream Transfer) compressed audio
 * to raw DSD (Direct Stream Digital) data using the batch decoder from libdst.
 * The batch decoder uses a thread pool internally for parallel processing.
 * Currently processes single frames for API compatibility; batch processing
 * at the pipeline level would provide additional performance benefits.
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
#include <stdio.h>

#include <libdst/decoder_batch.h>
#include <libsautil/mem.h>

/*============================================================================
 * Constants
 *============================================================================*/

/** DSD frame size: 588 samples * 8 bits = 4704 bytes per channel */
#define DST_DSD_FRAME_SIZE 4704

/** Maximum DSD output size per frame (6 channels * 4704 bytes) */
#define DST_MAX_DSD_OUTPUT_SIZE (6 * DST_DSD_FRAME_SIZE)

/*============================================================================
 * DST Transform Context
 *============================================================================*/

typedef struct dsdpipe_transform_dst_ctx_s {
    /* Format information */
    dsdpipe_format_t input_format;
    dsdpipe_format_t output_format;
    bool is_initialized;

    /* Batch DST decoder handle */
    dst_batch_decoder_t *decoder;

    /* Statistics */
    uint64_t frames_processed;
    uint64_t bytes_in;
    uint64_t bytes_out;
    uint64_t errors_count;
} dsdpipe_transform_dst_ctx_t;

/*============================================================================
 * Transform Operations
 *============================================================================*/

static int dst_transform_init(void *ctx, const dsdpipe_format_t *input_format,
                              dsdpipe_format_t *output_format)
{
    dsdpipe_transform_dst_ctx_t *dst_ctx = (dsdpipe_transform_dst_ctx_t *)ctx;

    if (!dst_ctx || !input_format || !output_format) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Input must be DST */
    if (input_format->type != DSDPIPE_FORMAT_DST) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Validate channel count */
    if (input_format->channel_count < 1 || input_format->channel_count > 6) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dst_ctx->input_format = *input_format;

    /* Output is raw DSD with same parameters */
    dst_ctx->output_format = *input_format;
    dst_ctx->output_format.type = DSDPIPE_FORMAT_DSD_RAW;
    *output_format = dst_ctx->output_format;

    /* Create batch DST decoder with auto-detected thread count */
    dst_ctx->decoder = dst_batch_decoder_create(input_format->channel_count, 0);
    if (!dst_ctx->decoder) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Initialize statistics */
    dst_ctx->frames_processed = 0;
    dst_ctx->bytes_in = 0;
    dst_ctx->bytes_out = 0;
    dst_ctx->errors_count = 0;

    dst_ctx->is_initialized = true;
    return DSDPIPE_OK;
}

static int dst_transform_process(void *ctx, const dsdpipe_buffer_t *input,
                                 dsdpipe_buffer_t *output)
{
    dsdpipe_transform_dst_ctx_t *dst_ctx = (dsdpipe_transform_dst_ctx_t *)ctx;

    if (!dst_ctx || !input || !output) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dst_ctx->is_initialized) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (!dst_ctx->decoder) {
        return DSDPIPE_ERROR_INTERNAL;
    }

    /* Setup batch arrays for single-frame decode */
    const uint8_t *inputs[1] = { input->data };
    size_t input_sizes[1] = { input->size };
    uint8_t *outputs[1] = { output->data };
    size_t output_sizes[1] = { 0 };

    /* Decode single frame using batch API */
    int result = dst_batch_decode(
        dst_ctx->decoder,
        inputs, input_sizes,
        outputs, output_sizes,
        1  /* count */
    );

    if (result != 0) {
        dst_ctx->errors_count++;
        return DSDPIPE_ERROR_DST_DECODE;
    }

    /* Set output buffer metadata */
    output->size = output_sizes[0];
    output->format = dst_ctx->output_format;
    output->frame_number = input->frame_number;
    output->sample_offset = input->sample_offset;
    output->track_number = input->track_number;
    output->flags = input->flags;

    /* Update statistics */
    dst_ctx->frames_processed++;
    dst_ctx->bytes_in += input->size;
    dst_ctx->bytes_out += output->size;

    return DSDPIPE_OK;
}

/**
 * @brief Batch process multiple DST frames in parallel
 *
 * This is the key performance optimization - decodes multiple frames
 * simultaneously using the thread pool.
 */
static int dst_transform_process_batch(void *ctx,
                                        const uint8_t *inputs[],
                                        const size_t input_sizes[],
                                        uint8_t *outputs[],
                                        size_t output_sizes[],
                                        size_t count)
{
    dsdpipe_transform_dst_ctx_t *dst_ctx = (dsdpipe_transform_dst_ctx_t *)ctx;

    if (!dst_ctx || !inputs || !input_sizes || !outputs || !output_sizes) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (!dst_ctx->is_initialized || !dst_ctx->decoder) {
        return DSDPIPE_ERROR_NOT_CONFIGURED;
    }

    if (count == 0) {
        return DSDPIPE_OK;
    }

    /* Decode all frames in parallel using batch API */
    int result = dst_batch_decode(
        dst_ctx->decoder,
        inputs, input_sizes,
        outputs, output_sizes,
        count
    );

    if (result != 0) {
        dst_ctx->errors_count++;
        return DSDPIPE_ERROR_DST_DECODE;
    }

    /* Update statistics */
    for (size_t i = 0; i < count; i++) {
        dst_ctx->frames_processed++;
        dst_ctx->bytes_in += input_sizes[i];
        dst_ctx->bytes_out += output_sizes[i];
    }

    return DSDPIPE_OK;
}

static int dst_transform_flush(void *ctx, dsdpipe_buffer_t *output)
{
    dsdpipe_transform_dst_ctx_t *dst_ctx = (dsdpipe_transform_dst_ctx_t *)ctx;

    if (!dst_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* DST decoding is frame-based with no internal buffering, nothing to flush */
    (void)output;

    return DSDPIPE_OK;
}

static void dst_transform_reset(void *ctx)
{
    dsdpipe_transform_dst_ctx_t *dst_ctx = (dsdpipe_transform_dst_ctx_t *)ctx;

    if (!dst_ctx) {
        return;
    }

    /* Reset statistics */
    dst_ctx->frames_processed = 0;
    dst_ctx->bytes_in = 0;
    dst_ctx->bytes_out = 0;
    dst_ctx->errors_count = 0;
}

static void dst_transform_destroy(void *ctx)
{
    dsdpipe_transform_dst_ctx_t *dst_ctx = (dsdpipe_transform_dst_ctx_t *)ctx;

    if (!dst_ctx) {
        return;
    }

    /* Destroy batch decoder */
    if (dst_ctx->decoder) {
        dst_batch_decoder_destroy(dst_ctx->decoder);
        dst_ctx->decoder = NULL;
    }

    sa_free(dst_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_transform_ops_t s_dst_transform_ops = {
    .init = dst_transform_init,
    .process = dst_transform_process,
    .process_batch = dst_transform_process_batch,
    .flush = dst_transform_flush,
    .reset = dst_transform_reset,
    .destroy = dst_transform_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_transform_dst_create(dsdpipe_transform_t **transform)
{
    if (!transform) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    dsdpipe_transform_t *new_transform =
        (dsdpipe_transform_t *)sa_calloc(1, sizeof(*new_transform));
    if (!new_transform) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    dsdpipe_transform_dst_ctx_t *ctx =
        (dsdpipe_transform_dst_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        sa_free(new_transform);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    new_transform->ops = &s_dst_transform_ops;
    new_transform->ctx = ctx;
    new_transform->is_initialized = false;

    *transform = new_transform;
    return DSDPIPE_OK;
}
