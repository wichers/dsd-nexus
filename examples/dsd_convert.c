/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSD audio format converter (DSF <-> DSDIFF)
 * This utility converts DSD audio files between DSF and DSDIFF formats.
 * It supports:
 * - DSF to DSDIFF conversion
 * - DSDIFF to DSF conversion
 * - DSF to DSF re-encoding (copy with fresh headers)
 * - DSDIFF to DSDIFF re-encoding (copy with fresh headers)
 * - Automatic format detection from input file
 * - Output format selection by file extension or explicit flag
 * Usage:
 *   dsd_convert <input_file> <output_file>
 *   dsd_convert -i <input_file> -o <output_file> [-f dsf|dsdiff]
 * The output format is determined by:
 * 1. Explicit -f flag if provided
 * 2. Output file extension (.dsf -> DSF, .dff -> DSDIFF)
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

#include <libsautil/mem.h>
#include <libsautil/sastring.h>
#include <libsautil/compat.h>

#include <libdsf/dsf.h>
#include <libdsdiff/dsdiff.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Buffer size for audio data transfer (1 MB) */
#define TRANSFER_BUFFER_SIZE (1024 * 1024)

/* File format detection */
typedef enum {
    FORMAT_UNKNOWN,
    FORMAT_DSF,
    FORMAT_DSDIFF
} file_format_t;

/* Conversion context */
typedef struct {
    const char *input_path;
    const char *output_path;
    file_format_t input_format;
    file_format_t output_format;
    bool verbose;
} convert_ctx_t;

/**
 * @brief Detect file format from magic bytes
 */
static file_format_t detect_format(const char *filename)
{
    FILE *f = NULL;
    uint8_t magic[4];

    f = sa_fopen(filename, "rb");
    if (f == NULL) {
        return FORMAT_UNKNOWN;
    }

    if (fread(magic, 1, 4, f) != 4) {
        fclose(f);
        return FORMAT_UNKNOWN;
    }
    fclose(f);

    /* Check for DSF magic: "DSD " (little-endian) */
    if (magic[0] == 'D' && magic[1] == 'S' && magic[2] == 'D' && magic[3] == ' ') {
        return FORMAT_DSF;
    }

    /* Check for DSDIFF magic: "FRM8" (big-endian) */
    if (magic[0] == 'F' && magic[1] == 'R' && magic[2] == 'M' && magic[3] == '8') {
        return FORMAT_DSDIFF;
    }

    return FORMAT_UNKNOWN;
}

/**
 * @brief Detect format from file extension
 */
static file_format_t format_from_extension(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (!ext) {
        return FORMAT_UNKNOWN;
    }

    if (sa_strcasecmp(ext, ".dsf") == 0) {
        return FORMAT_DSF;
    }
    if (sa_strcasecmp(ext, ".dff") == 0 || sa_strcasecmp(ext, ".dsdiff") == 0) {
        return FORMAT_DSDIFF;
    }

    return FORMAT_UNKNOWN;
}

/**
 * @brief Print usage information
 */
static void print_usage(const char *program_name)
{
    fprintf(stderr, "DSD Audio Format Converter\n\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s <input_file> <output_file>\n", program_name);
    fprintf(stderr, "  %s -i <input_file> -o <output_file> [-f dsf|dsdiff] [-v]\n\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -i <file>      Input file (DSF or DSDIFF)\n");
    fprintf(stderr, "  -o <file>      Output file\n");
    fprintf(stderr, "  -f <format>    Output format: dsf or dsdiff\n");
    fprintf(stderr, "  -v             Verbose output\n");
    fprintf(stderr, "  -h, --help     Show this help message\n\n");
    fprintf(stderr, "Supported conversions:\n");
    fprintf(stderr, "  DSF    -> DSDIFF (DSD only, no DST compression)\n");
    fprintf(stderr, "  DSDIFF -> DSF    (DSD only, DST files not supported)\n");
    fprintf(stderr, "  DSF    -> DSF    (re-encode/copy)\n");
    fprintf(stderr, "  DSDIFF -> DSDIFF (re-encode/copy, DSD only)\n\n");
    fprintf(stderr, "Output format is determined by:\n");
    fprintf(stderr, "  1. Explicit -f flag\n");
    fprintf(stderr, "  2. Output file extension (.dsf -> DSF, .dff/.dsdiff -> DSDIFF)\n");
    fprintf(stderr, "  3. If unspecified and same extension, converts to opposite format\n");
}

/**
 * @brief Parse command line arguments
 */
static int parse_args(int argc, char *argv[], convert_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->output_format = FORMAT_UNKNOWN;

    /* Simple 2-argument form */
    if (argc == 3 && argv[1][0] != '-') {
        ctx->input_path = argv[1];
        ctx->output_path = argv[2];
        return 0;
    }

    /* Option form */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return -1;
        }
        else if (strcmp(argv[i], "-v") == 0) {
            ctx->verbose = true;
        }
        else if (strcmp(argv[i], "-i") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: -i requires a filename argument\n");
                return -1;
            }
            ctx->input_path = argv[i];
        }
        else if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: -o requires a filename argument\n");
                return -1;
            }
            ctx->output_path = argv[i];
        }
        else if (strcmp(argv[i], "-f") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: -f requires a format argument (dsf or dsdiff)\n");
                return -1;
            }
            if (sa_strcasecmp(argv[i], "dsf") == 0) {
                ctx->output_format = FORMAT_DSF;
            }
            else if (sa_strcasecmp(argv[i], "dsdiff") == 0 || sa_strcasecmp(argv[i], "dff") == 0) {
                ctx->output_format = FORMAT_DSDIFF;
            }
            else {
                fprintf(stderr, "Error: Unknown format '%s' (use dsf or dsdiff)\n", argv[i]);
                return -1;
            }
        }
        else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            return -1;
        }
    }

    if (!ctx->input_path || !ctx->output_path) {
        fprintf(stderr, "Error: Both input and output files must be specified\n");
        return -1;
    }

    return 0;
}

/**
 * @brief Map DSDIFF channel count to DSF channel type
 */
static uint32_t dsdiff_to_dsf_channel_type(uint16_t channel_count)
{
    switch (channel_count) {
        case 1: return DSF_CHANNEL_TYPE_MONO;
        case 2: return DSF_CHANNEL_TYPE_STEREO;
        case 3: return DSF_CHANNEL_TYPE_3_CHANNELS;
        case 4: return DSF_CHANNEL_TYPE_QUAD;
        case 5: return DSF_CHANNEL_TYPE_5_CHANNELS;
        case 6: return DSF_CHANNEL_TYPE_5_1_CHANNELS;
        default: return DSF_CHANNEL_TYPE_STEREO;
    }
}

/**
 * @brief Convert DSF to DSDIFF
 */
static int convert_dsf_to_dsdiff(convert_ctx_t *ctx)
{
    dsf_t *dsf_in = NULL;
    dsdiff_t *dsdiff_out = NULL;
    uint8_t *dsf_buffer = NULL;
    int result = -1;
    int ret;

    /* Open DSF input file */
    ret = dsf_alloc(&dsf_in);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to allocate DSF handle\n");
        goto cleanup;
    }

    ret = dsf_open(dsf_in, ctx->input_path);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to open DSF file '%s': %s\n",
                ctx->input_path, dsf_error_string(ret));
        goto cleanup;
    }

    /* Get DSF file info */
    dsf_file_info_t dsf_info;
    ret = dsf_get_file_info(dsf_in, &dsf_info);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to get DSF file info: %s\n", dsf_error_string(ret));
        goto cleanup;
    }

    if (ctx->verbose) {
        printf("Input: %s\n", ctx->input_path);
        printf("  Format: DSF\n");
        printf("  Channels: %u (%s)\n", dsf_info.channel_count,
               dsf_channel_type_to_string(dsf_info.channel_type));
        printf("  Sample Rate: %u Hz (%s)\n", dsf_info.sampling_frequency,
               dsf_sample_rate_to_string(dsf_info.sampling_frequency));
        printf("  Duration: %.2f seconds\n", dsf_info.duration_seconds);
        printf("  Audio Data Size: %llu bytes\n", (unsigned long long)dsf_info.audio_data_size);
    }

    /* Create DSDIFF output file (channel IDs are auto-set based on channel count) */
    ret = dsdiff_new(&dsdiff_out);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to allocate DSDIFF handle\n");
        goto cleanup;
    }

    ret = dsdiff_create(dsdiff_out, ctx->output_path, DSDIFF_AUDIO_DSD,
                             (uint16_t)dsf_info.channel_count, 1, dsf_info.sampling_frequency);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to create DSDIFF file '%s': error code %d\n", ctx->output_path, ret);
        goto cleanup;
    }

    /* Copy ID3 metadata from DSF to DSDIFF if present */
    {
        int has_metadata = 0;
        ret = dsf_has_metadata(dsf_in, &has_metadata);
        if (ret == DSF_SUCCESS && has_metadata) {
            uint8_t *metadata_buffer = NULL;
            uint64_t metadata_size = 0;
            ret = dsf_read_metadata(dsf_in, &metadata_buffer, &metadata_size);
            if (ret == DSF_SUCCESS && metadata_buffer != NULL && metadata_size > 0) {
                ret = dsdiff_set_id3_tag(dsdiff_out, metadata_buffer, (uint32_t)metadata_size);
                if (ret != DSDIFF_SUCCESS) {
                    fprintf(stderr, "Warning: Failed to copy ID3 metadata to output file\n");
                } else if (ctx->verbose) {
                    printf("  ID3 Metadata Size: %llu bytes\n", (unsigned long long)metadata_size);
                }
                sa_free(metadata_buffer);
            }
        }
    }

    /* Allocate transfer buffers */
    /* For DSF->DSDIFF, we need to read in multiples of block_size * channel_count */
    size_t dsf_block_size = DSF_BLOCK_SIZE_PER_CHANNEL * dsf_info.channel_count;
    size_t blocks_per_transfer = TRANSFER_BUFFER_SIZE / dsf_block_size;
    if (blocks_per_transfer == 0) blocks_per_transfer = 1;
    size_t transfer_size = blocks_per_transfer * dsf_block_size;

    dsf_buffer = sa_malloc(transfer_size);
    if (!dsf_buffer) {
        fprintf(stderr, "Error: Failed to allocate transfer buffer\n");
        goto cleanup;
    }

    /* Seek to start of audio data */
    ret = dsf_seek_to_audio_start(dsf_in);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to seek to audio start\n");
        goto cleanup;
    }

    /* Transfer audio data */
    uint64_t total_transferred = 0;
    uint64_t audio_data_size = dsf_info.audio_data_size;
    if (ctx->verbose) {
        printf("\nConverting...\n");
    }

    while (total_transferred < audio_data_size) {
        size_t to_read = transfer_size;
        if (total_transferred + to_read > audio_data_size) {
            to_read = (size_t)(audio_data_size - total_transferred);
            /* Ensure we read complete block groups */
            to_read = (to_read / dsf_block_size) * dsf_block_size;
            if (to_read == 0) {
                /* Remaining data is less than one block group - read full block anyway */
                to_read = dsf_block_size;
            }
        }

        size_t bytes_read = 0;
        ret = dsf_read_audio_data(dsf_in, dsf_buffer, to_read, &bytes_read);
        if (bytes_read == 0) {
            break;
        }
        if (ret != DSF_SUCCESS) {
            fprintf(stderr, "Error: Failed to read DSF audio data: %s\n", dsf_error_string(ret));
            goto cleanup;
        }

        /* Write to DSDIFF */
        uint32_t bytes_written = 0;
        ret = dsdiff_write_dsd_data(dsdiff_out, dsf_buffer, (uint32_t)bytes_read, &bytes_written);
        if (ret != DSDIFF_SUCCESS) {
            fprintf(stderr, "Error: Failed to write DSDIFF audio data: error code %d\n", ret);
            goto cleanup;
        }

        total_transferred += bytes_read;
        if (ctx->verbose && total_transferred % (10 * 1024 * 1024) == 0) {
            printf("  Progress: %llu MB\r", (unsigned long long)(total_transferred / (1024 * 1024)));
            fflush(stdout);
        }
    }

    /* Finalize DSDIFF file */
    ret = dsdiff_finalize(dsdiff_out);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to finalize DSDIFF file\n");
        goto cleanup;
    }

    if (ctx->verbose) {
        printf("\nConversion complete!\n");
        printf("  Output: %s\n", ctx->output_path);
        printf("  Total transferred: %llu bytes\n", (unsigned long long)total_transferred);
    }

    result = 0;

cleanup:
    sa_free(dsf_buffer);
    if (dsdiff_out) {
        dsdiff_close(dsdiff_out);
    }
    if (dsf_in) {
        dsf_close(dsf_in);
        dsf_free(dsf_in);
    }

    return result;
}

/**
 * @brief Convert DSDIFF to DSF
 */
static int convert_dsdiff_to_dsf(convert_ctx_t *ctx)
{
    dsdiff_t *dsdiff_in = NULL;
    dsf_t *dsf_out = NULL;
    uint8_t *dsdiff_buffer = NULL;
    int result = -1;
    int ret;

    /* Open DSDIFF input file */
    ret = dsdiff_new(&dsdiff_in);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to allocate DSDIFF handle\n");
        goto cleanup;
    }

    ret = dsdiff_open(dsdiff_in, ctx->input_path);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to open DSDIFF file '%s'\n", ctx->input_path);
        goto cleanup;
    }

    /* Check file type */
    dsdiff_audio_type_t file_type;
    ret = dsdiff_get_audio_type(dsdiff_in, &file_type);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to get DSDIFF file type\n");
        goto cleanup;
    }

    if (file_type == DSDIFF_AUDIO_DST) {
        fprintf(stderr, "Error: DST-compressed DSDIFF files are not supported for conversion to DSF.\n");
        fprintf(stderr, "       DSF format does not support DST compression.\n");
        goto cleanup;
    }

    /* Get DSDIFF file info */
    uint16_t channel_count;
    uint32_t sample_rate;
    uint64_t dsd_data_size;

    ret = dsdiff_get_channel_count(dsdiff_in, &channel_count);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to get channel count\n");
        goto cleanup;
    }

    ret = dsdiff_get_sample_rate(dsdiff_in, &sample_rate);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to get sample rate\n");
        goto cleanup;
    }

    ret = dsdiff_get_dsd_data_size(dsdiff_in, &dsd_data_size);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to get DSD data size\n");
        goto cleanup;
    }

    /* Calculate sample count from data size */
    uint64_t sample_count = (dsd_data_size / channel_count) * 8;  /* 8 samples per byte */

    /* Calculate duration */
    double duration = (double)sample_count / (double)sample_rate;

    if (ctx->verbose) {
        printf("Input: %s\n", ctx->input_path);
        printf("  Format: DSDIFF (DSD)\n");
        printf("  Channels: %u\n", channel_count);
        printf("  Sample Rate: %u Hz (%s)\n", sample_rate,
               dsf_sample_rate_to_string(sample_rate));
        printf("  Duration: %.2f seconds\n", duration);
        printf("  DSD Data Size: %llu bytes\n", (unsigned long long)dsd_data_size);
    }

    /* Map DSDIFF channels to DSF */
    uint32_t dsf_channel_type = dsdiff_to_dsf_channel_type(channel_count);

    /* Create DSF output file */
    ret = dsf_alloc(&dsf_out);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to allocate DSF handle\n");
        goto cleanup;
    }

    ret = dsf_create(dsf_out, ctx->output_path, sample_rate, dsf_channel_type,
                     channel_count, 1);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to create DSF file '%s': %s\n",
                ctx->output_path, dsf_error_string(ret));
        goto cleanup;
    }

    /* Copy ID3 tag from DSDIFF to DSF if present */
    {
        uint8_t *id3_tag = NULL;
        uint32_t id3_tag_size = 0;
        if (dsdiff_get_id3_tag(dsdiff_in, &id3_tag, &id3_tag_size) == DSDIFF_SUCCESS &&
            id3_tag != NULL && id3_tag_size > 0) {
            ret = dsf_write_metadata(dsf_out, id3_tag, id3_tag_size);
            if (ret != DSF_SUCCESS) {
                fprintf(stderr, "Warning: Failed to copy ID3 tag to output file\n");
            } else if (ctx->verbose) {
                printf("  ID3 Tag Size: %u bytes\n", id3_tag_size);
            }
            sa_free(id3_tag);
        }
    }

    /* Allocate transfer buffers */
    /* For DSDIFF->DSF, transfer size should be multiple of channel_count and DSF block size */
    size_t dsf_block_size = DSF_BLOCK_SIZE_PER_CHANNEL * channel_count;
    size_t blocks_per_transfer = TRANSFER_BUFFER_SIZE / dsf_block_size;
    if (blocks_per_transfer == 0) blocks_per_transfer = 1;
    size_t transfer_size = blocks_per_transfer * dsf_block_size;

    dsdiff_buffer = sa_malloc(transfer_size);
    if (!dsdiff_buffer) {
        fprintf(stderr, "Error: Failed to allocate transfer buffer\n");
        goto cleanup;
    }

    /* Seek to start of audio data */
    ret = dsdiff_seek_dsd_start(dsdiff_in);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to seek to audio start\n");
        goto cleanup;
    }

    /* Transfer audio data */
    uint64_t total_transferred = 0;
    if (ctx->verbose) {
        printf("\nConverting...\n");
    }

    while (total_transferred < dsd_data_size) {
        uint32_t to_read = (uint32_t)transfer_size;
        if (total_transferred + to_read > dsd_data_size) {
            to_read = (uint32_t)(dsd_data_size - total_transferred);
        }

        uint32_t bytes_read = 0;
        ret = dsdiff_read_dsd_data(dsdiff_in, dsdiff_buffer, to_read, &bytes_read);
        if (ret == DSDIFF_ERROR_END_OF_DATA || bytes_read == 0) {
            break;
        }
        if (ret != DSDIFF_SUCCESS && ret != DSDIFF_ERROR_END_OF_DATA) {
            fprintf(stderr, "Error: Failed to read DSDIFF audio data\n");
            goto cleanup;
        }

        /* Write to DSF */
        size_t bytes_written = 0;
        ret = dsf_write_audio_data(dsf_out, dsdiff_buffer, bytes_read, &bytes_written);
        if (ret != DSF_SUCCESS) {
            fprintf(stderr, "Error: Failed to write DSF audio data: %s\n", dsf_error_string(ret));
            goto cleanup;
        }

        total_transferred += bytes_read;
        if (ctx->verbose && total_transferred % (10 * 1024 * 1024) == 0) {
            printf("  Progress: %llu MB\r", (unsigned long long)(total_transferred / (1024 * 1024)));
            fflush(stdout);
        }
    }

    /* Finalize DSF file */
    ret = dsf_finalize(dsf_out);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to finalize DSF file: %s\n", dsf_error_string(ret));
        goto cleanup;
    }

    if (ctx->verbose) {
        printf("\nConversion complete!\n");
        printf("  Output: %s\n", ctx->output_path);
        printf("  Total transferred: %llu bytes\n", (unsigned long long)total_transferred);
    }

    result = 0;

cleanup:
    sa_free(dsdiff_buffer);
    if (dsf_out) {
        dsf_close(dsf_out);
        dsf_free(dsf_out);
    }
    if (dsdiff_in) {
        dsdiff_close(dsdiff_in);
    }

    return result;
}

/**
 * @brief Convert DSF to DSF (re-encode/copy)
 */
static int convert_dsf_to_dsf(convert_ctx_t *ctx)
{
    dsf_t *dsf_in = NULL;
    dsf_t *dsf_out = NULL;
    uint8_t *buffer = NULL;
    uint8_t *metadata_buffer = NULL;
    uint64_t metadata_size = 0;
    int result = -1;
    int ret;

    /* Open DSF input file */
    ret = dsf_alloc(&dsf_in);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to allocate DSF input handle\n");
        goto cleanup;
    }

    ret = dsf_open(dsf_in, ctx->input_path);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to open DSF file '%s': %s\n",
                ctx->input_path, dsf_error_string(ret));
        goto cleanup;
    }

    /* Get DSF file info */
    dsf_file_info_t dsf_info;
    ret = dsf_get_file_info(dsf_in, &dsf_info);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to get DSF file info: %s\n", dsf_error_string(ret));
        goto cleanup;
    }

    if (ctx->verbose) {
        printf("Input: %s\n", ctx->input_path);
        printf("  Format: DSF\n");
        printf("  Channels: %u (%s)\n", dsf_info.channel_count,
               dsf_channel_type_to_string(dsf_info.channel_type));
        printf("  Sample Rate: %u Hz (%s)\n", dsf_info.sampling_frequency,
               dsf_sample_rate_to_string(dsf_info.sampling_frequency));
        printf("  Duration: %.2f seconds\n", dsf_info.duration_seconds);
        printf("  Audio Data Size: %llu bytes\n", (unsigned long long)dsf_info.audio_data_size);
    }

    /* Read metadata (ID3 tag) from input file if present */
    int has_metadata = 0;
    ret = dsf_has_metadata(dsf_in, &has_metadata);
    if (ret == DSF_SUCCESS && has_metadata) {
        ret = dsf_read_metadata(dsf_in, &metadata_buffer, &metadata_size);
        if (ret != DSF_SUCCESS) {
            fprintf(stderr, "Warning: Failed to read metadata from input file\n");
            metadata_buffer = NULL;
            metadata_size = 0;
        } else if (ctx->verbose) {
            printf("  Metadata Size: %llu bytes\n", (unsigned long long)metadata_size);
        }
    }

    /* Create DSF output file */
    ret = dsf_alloc(&dsf_out);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to allocate DSF output handle\n");
        goto cleanup;
    }

    ret = dsf_create(dsf_out, ctx->output_path, dsf_info.sampling_frequency,
                     dsf_info.channel_type, dsf_info.channel_count, 1);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to create DSF file '%s': %s\n",
                ctx->output_path, dsf_error_string(ret));
        goto cleanup;
    }

    /* Allocate transfer buffer */
    buffer = sa_malloc(TRANSFER_BUFFER_SIZE);
    if (!buffer) {
        fprintf(stderr, "Error: Failed to allocate transfer buffer\n");
        goto cleanup;
    }

    /* Seek to start of audio data */
    ret = dsf_seek_to_audio_start(dsf_in);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to seek to audio start\n");
        goto cleanup;
    }

    /* Transfer audio data */
    uint64_t total_transferred = 0;
    uint64_t audio_data_size = dsf_info.audio_data_size;
    if (ctx->verbose) {
        printf("\nCopying...\n");
    }

    while (total_transferred < audio_data_size) {
        size_t to_read = TRANSFER_BUFFER_SIZE;
        if (total_transferred + to_read > audio_data_size) {
            to_read = (size_t)(audio_data_size - total_transferred);
        }

        size_t bytes_read = 0;
        ret = dsf_read_audio_data(dsf_in, buffer, to_read, &bytes_read);
        if (bytes_read == 0) {
            break;
        }
        if (ret != DSF_SUCCESS) {
            fprintf(stderr, "Error: Failed to read DSF audio data: %s\n", dsf_error_string(ret));
            goto cleanup;
        }

        /* Write to output DSF */
        size_t bytes_written = 0;
        ret = dsf_write_audio_data(dsf_out, buffer, bytes_read, &bytes_written);
        if (ret != DSF_SUCCESS) {
            fprintf(stderr, "Error: Failed to write DSF audio data: %s\n", dsf_error_string(ret));
            goto cleanup;
        }

        total_transferred += bytes_read;
        if (ctx->verbose && total_transferred % (10 * 1024 * 1024) == 0) {
            printf("  Progress: %llu MB\r", (unsigned long long)(total_transferred / (1024 * 1024)));
            fflush(stdout);
        }
    }

    /* Write metadata (ID3 tag) to output file if present */
    if (metadata_buffer && metadata_size > 0) {
        ret = dsf_write_metadata(dsf_out, metadata_buffer, metadata_size);
        if (ret != DSF_SUCCESS) {
            fprintf(stderr, "Warning: Failed to write metadata to output file\n");
        } else if (ctx->verbose) {
            printf("  Metadata copied: %llu bytes\n", (unsigned long long)metadata_size);
        }
    }

    /* Finalize DSF file */
    ret = dsf_finalize(dsf_out);
    if (ret != DSF_SUCCESS) {
        fprintf(stderr, "Error: Failed to finalize DSF file: %s\n", dsf_error_string(ret));
        goto cleanup;
    }

    if (ctx->verbose) {
        printf("\nCopy complete!\n");
        printf("  Output: %s\n", ctx->output_path);
        printf("  Total transferred: %llu bytes\n", (unsigned long long)total_transferred);
    }

    result = 0;

cleanup:
    sa_free(buffer);
    sa_free(metadata_buffer);
    if (dsf_out) {
        dsf_close(dsf_out);
        dsf_free(dsf_out);
    }
    if (dsf_in) {
        dsf_close(dsf_in);
        dsf_free(dsf_in);
    }

    return result;
}

/**
 * @brief Convert DSDIFF to DSDIFF (re-encode/copy)
 */
static int convert_dsdiff_to_dsdiff(convert_ctx_t *ctx)
{
    dsdiff_t *dsdiff_in = NULL;
    dsdiff_t *dsdiff_out = NULL;
    uint8_t *buffer = NULL;
    int result = -1;
    int ret;

    /* Open DSDIFF input file */
    ret = dsdiff_new(&dsdiff_in);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to allocate DSDIFF input handle\n");
        goto cleanup;
    }

    ret = dsdiff_open(dsdiff_in, ctx->input_path);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to open DSDIFF file '%s'\n", ctx->input_path);
        goto cleanup;
    }

    /* Check file type */
    dsdiff_audio_type_t file_type;
    ret = dsdiff_get_audio_type(dsdiff_in, &file_type);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to get DSDIFF file type\n");
        goto cleanup;
    }

    if (file_type == DSDIFF_AUDIO_DST) {
        fprintf(stderr, "Error: DST-compressed DSDIFF files are not supported for re-encoding.\n");
        goto cleanup;
    }

    /* Get DSDIFF file info */
    uint16_t channel_count;
    uint32_t sample_rate;
    uint64_t dsd_data_size;

    ret = dsdiff_get_channel_count(dsdiff_in, &channel_count);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to get channel count\n");
        goto cleanup;
    }

    ret = dsdiff_get_sample_rate(dsdiff_in, &sample_rate);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to get sample rate\n");
        goto cleanup;
    }

    ret = dsdiff_get_dsd_data_size(dsdiff_in, &dsd_data_size);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to get DSD data size\n");
        goto cleanup;
    }

    /* Get loudspeaker config if present */
    int has_ls_config = 0;
    dsdiff_loudspeaker_config_t ls_config = DSDIFF_LS_CONFIG_INVALID;
    dsdiff_has_loudspeaker_config(dsdiff_in, &has_ls_config);
    if (has_ls_config) {
        dsdiff_get_loudspeaker_config(dsdiff_in, &ls_config);
    }

    /* Get disc artist if present */
    int has_disc_artist = 0;
    char disc_artist[256] = {0};
    uint32_t artist_len = sizeof(disc_artist);
    dsdiff_has_disc_artist(dsdiff_in, &has_disc_artist);
    if (has_disc_artist) {
        dsdiff_get_disc_artist(dsdiff_in, &artist_len, disc_artist);
    }

    /* Get disc title if present */
    int has_disc_title = 0;
    char disc_title[256] = {0};
    uint32_t title_len = sizeof(disc_title);
    dsdiff_has_disc_title(dsdiff_in, &has_disc_title);
    if (has_disc_title) {
        dsdiff_get_disc_title(dsdiff_in, &title_len, disc_title);
    }

    /* Calculate duration */
    uint64_t sample_count = (dsd_data_size / channel_count) * 8;
    double duration = (double)sample_count / (double)sample_rate;

    if (ctx->verbose) {
        printf("Input: %s\n", ctx->input_path);
        printf("  Format: DSDIFF (DSD)\n");
        printf("  Channels: %u\n", channel_count);
        printf("  Sample Rate: %u Hz (%s)\n", sample_rate,
               dsf_sample_rate_to_string(sample_rate));
        printf("  Duration: %.2f seconds\n", duration);
        printf("  DSD Data Size: %llu bytes\n", (unsigned long long)dsd_data_size);
    }

    /* Create DSDIFF output file (channel IDs are auto-set based on channel count) */
    ret = dsdiff_new(&dsdiff_out);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to allocate DSDIFF output handle\n");
        goto cleanup;
    }

    /* Set loudspeaker config if present in input */
    if (has_ls_config) {
        dsdiff_set_loudspeaker_config(dsdiff_out, ls_config);
    }

    ret = dsdiff_create(dsdiff_out, ctx->output_path, DSDIFF_AUDIO_DSD,
                             channel_count, 1, sample_rate);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to create DSDIFF file '%s': error code %d\n", ctx->output_path, ret);
        goto cleanup;
    }

    /* Copy metadata from input to output */
    if (has_disc_artist) {
        dsdiff_set_disc_artist(dsdiff_out, disc_artist);
    }
    if (has_disc_title) {
        dsdiff_set_disc_title(dsdiff_out, disc_title);
    }

    /* Copy comments from input to output */
    {
        int comment_count = 0;
        dsdiff_get_comment_count(dsdiff_in, &comment_count);
        for (int i = 0; i < comment_count; i++) {
            dsdiff_comment_t comment;
            memset(&comment, 0, sizeof(comment));
            if (dsdiff_get_comment(dsdiff_in, i, &comment) == DSDIFF_SUCCESS) {
                dsdiff_add_comment(dsdiff_out, &comment);
                /* Note: comment.text is a shallow copy pointing to input handle's memory.
                 * dsdiff_add_comment makes its own copy, so we don't free it here. */
            }
        }
    }

    /* Copy DSD markers from input to output */
    {
        int marker_count = 0;
        dsdiff_get_dsd_marker_count(dsdiff_in, &marker_count);
        for (int i = 0; i < marker_count; i++) {
            dsdiff_marker_t marker;
            memset(&marker, 0, sizeof(marker));
            if (dsdiff_get_dsd_marker(dsdiff_in, i, &marker) == DSDIFF_SUCCESS) {
                dsdiff_add_dsd_marker(dsdiff_out, &marker);
                /* Note: marker.marker_text is a shallow copy pointing to input handle's memory.
                 * dsdiff_add_dsd_marker makes its own copy, so we don't free it here. */
            }
        }
    }

    /* Copy ID3 tag from input to output if present */
    {
        uint8_t *id3_tag = NULL;
        uint32_t id3_tag_size = 0;
        if (dsdiff_get_id3_tag(dsdiff_in, &id3_tag, &id3_tag_size) == DSDIFF_SUCCESS &&
            id3_tag != NULL && id3_tag_size > 0) {
            ret = dsdiff_set_id3_tag(dsdiff_out, id3_tag, id3_tag_size);
            if (ret != DSDIFF_SUCCESS) {
                fprintf(stderr, "Warning: Failed to copy ID3 tag to output file\n");
            } else if (ctx->verbose) {
                printf("  ID3 Tag Size: %u bytes\n", id3_tag_size);
            }
            sa_free(id3_tag);
        }
    }

    /* Allocate transfer buffer */
    buffer = sa_malloc(TRANSFER_BUFFER_SIZE);
    if (!buffer) {
        fprintf(stderr, "Error: Failed to allocate transfer buffer\n");
        goto cleanup;
    }

    /* Seek to start of audio data */
    ret = dsdiff_seek_dsd_start(dsdiff_in);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to seek to audio start\n");
        goto cleanup;
    }

    /* Transfer audio data */
    uint64_t total_transferred = 0;
    if (ctx->verbose) {
        printf("\nCopying...\n");
    }

    while (total_transferred < dsd_data_size) {
        uint32_t to_read = TRANSFER_BUFFER_SIZE;
        if (total_transferred + to_read > dsd_data_size) {
            to_read = (uint32_t)(dsd_data_size - total_transferred);
        }

        uint32_t bytes_read = 0;
        ret = dsdiff_read_dsd_data(dsdiff_in, buffer, to_read, &bytes_read);
        if (ret == DSDIFF_ERROR_END_OF_DATA || bytes_read == 0) {
            break;
        }
        if (ret != DSDIFF_SUCCESS && ret != DSDIFF_ERROR_END_OF_DATA) {
            fprintf(stderr, "Error: Failed to read DSDIFF audio data\n");
            goto cleanup;
        }

        /* Write to output DSDIFF */
        uint32_t bytes_written = 0;
        ret = dsdiff_write_dsd_data(dsdiff_out, buffer, bytes_read, &bytes_written);
        if (ret != DSDIFF_SUCCESS) {
            fprintf(stderr, "Error: Failed to write DSDIFF audio data\n");
            goto cleanup;
        }

        total_transferred += bytes_read;
        if (ctx->verbose && total_transferred % (10 * 1024 * 1024) == 0) {
            printf("  Progress: %llu MB\r", (unsigned long long)(total_transferred / (1024 * 1024)));
            fflush(stdout);
        }
    }

    /* Finalize DSDIFF file */
    ret = dsdiff_finalize(dsdiff_out);
    if (ret != DSDIFF_SUCCESS) {
        fprintf(stderr, "Error: Failed to finalize DSDIFF file\n");
        goto cleanup;
    }

    if (ctx->verbose) {
        printf("\nCopy complete!\n");
        printf("  Output: %s\n", ctx->output_path);
        printf("  Total transferred: %llu bytes\n", (unsigned long long)total_transferred);
    }

    result = 0;

cleanup:
    sa_free(buffer);
    if (dsdiff_out) {
        dsdiff_close(dsdiff_out);
    }
    if (dsdiff_in) {
        dsdiff_close(dsdiff_in);
    }

    return result;
}

/**
 * @brief Main entry point
 */
int main(int argc, char *argv[])
{
    convert_ctx_t ctx;
    int ret;

    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    ret = parse_args(argc, argv, &ctx);
    if (ret != 0) {
        print_usage(argv[0]);
        return 1;
    }

    /* Detect input format */
    ctx.input_format = detect_format(ctx.input_path);
    if (ctx.input_format == FORMAT_UNKNOWN) {
        fprintf(stderr, "Error: Could not detect input file format for '%s'\n", ctx.input_path);
        fprintf(stderr, "       File must be a valid DSF or DSDIFF file.\n");
        return 1;
    }

    /* Determine output format */
    if (ctx.output_format == FORMAT_UNKNOWN) {
        ctx.output_format = format_from_extension(ctx.output_path);
    }
    if (ctx.output_format == FORMAT_UNKNOWN) {
        /* Default: convert to opposite format */
        ctx.output_format = (ctx.input_format == FORMAT_DSF) ? FORMAT_DSDIFF : FORMAT_DSF;
    }

    if (ctx.verbose) {
        printf("DSD Audio Format Converter\n");
        printf("==========================\n\n");
    }

    /* Perform conversion */
    if (ctx.input_format == FORMAT_DSF && ctx.output_format == FORMAT_DSDIFF) {
        ret = convert_dsf_to_dsdiff(&ctx);
    }
    else if (ctx.input_format == FORMAT_DSDIFF && ctx.output_format == FORMAT_DSF) {
        ret = convert_dsdiff_to_dsf(&ctx);
    }
    else if (ctx.input_format == FORMAT_DSF && ctx.output_format == FORMAT_DSF) {
        ret = convert_dsf_to_dsf(&ctx);
    }
    else if (ctx.input_format == FORMAT_DSDIFF && ctx.output_format == FORMAT_DSDIFF) {
        ret = convert_dsdiff_to_dsdiff(&ctx);
    }
    else {
        fprintf(stderr, "Error: Unsupported conversion\n");
        return 1;
    }

    return ret;
}
