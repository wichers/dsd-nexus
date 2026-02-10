/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
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

#include <libdsf/dsf.h>

#include "dsf_chunks.h"
#include "dsf_io.h"

#include <libsautil/reverse.h>
#include <libsautil/mem.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * Internal File Structure
 * ===========================================================================*/

struct dsf_s {
    dsf_chunk_t *io;
    dsf_file_mode_t mode;
    dsf_file_info_t info;

    /* Writing state */
    uint64_t samples_written;
    uint64_t bytes_written;

    /* Metadata */
    uint8_t *metadata_buffer;
    uint64_t metadata_size;
    int metadata_modified;

    /* Block accumulation buffers for writing.
     * DSF requires continuous DSD data in 4096-byte blocks per channel,
     * with padding only at the very end of the file.
     * We buffer partial blocks until we have complete block groups to write.
     */
    uint8_t channel_buffers[DSF_MAX_CHANNELS][DSF_BLOCK_SIZE_PER_CHANNEL];
    size_t bytes_buffered;  /* Bytes buffered per channel (same for all) */

    /* Read buffer for converting DSF blocks to byte-interleaved output.
     * Holds one block group worth of converted byte-interleaved data.
     */
    uint8_t read_buffer[DSF_BLOCK_SIZE_PER_CHANNEL * DSF_MAX_CHANNELS];
    size_t read_buffer_pos;    /* Current read position in buffer */
    size_t read_buffer_valid;  /* Valid bytes in read buffer */

    /* Scratch buffer for block I/O */
    uint8_t scratch_buffer[DSF_BLOCK_SIZE_PER_CHANNEL * DSF_MAX_CHANNELS];
};

/* =============================================================================
 * Internal Helper Functions
 * ===========================================================================*/

static void dsf_reset_file_state(dsf_t *handle) {
    if (!handle) return;

    handle->io = NULL;
    handle->mode = DSF_FILE_MODE_CLOSED;
    memset(&handle->info, 0, sizeof(dsf_file_info_t));
    handle->samples_written = 0;
    handle->bytes_written = 0;

    if (handle->metadata_buffer) {
        sa_free(handle->metadata_buffer);
        handle->metadata_buffer = NULL;
    }
    handle->metadata_size = 0;
    handle->metadata_modified = 0;

    /* Reset write block accumulation state */
    handle->bytes_buffered = 0;
    memset(handle->channel_buffers, 0, sizeof(handle->channel_buffers));

    /* Reset read buffer state */
    handle->read_buffer_pos = 0;
    handle->read_buffer_valid = 0;
}

static int dsf_read_file_structure(dsf_t *handle) {
    uint64_t file_size;
    uint64_t metadata_offset;
    uint64_t data_size;
    uint64_t data_offset;
    int ret;

    if (!handle || !handle->io) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Read DSD chunk */
    ret = dsf_chunk_read_dsd_header(handle->io, &file_size, &metadata_offset);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    handle->info.file_size = file_size;
    handle->info.metadata_offset = metadata_offset;

    /* Read fmt chunk */
    ret = dsf_chunk_read_fmt(handle->io, &handle->info);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Read data chunk header */
    ret = dsf_chunk_read_data_header(handle->io, &data_size, &data_offset);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    handle->info.audio_data_offset = data_offset;

    /* Validate data size matches calculated size */
    if (data_size != handle->info.audio_data_size) {
        /* Use the actual data size from file */
        handle->info.audio_data_size = data_size;

        /* Recalculate sample count from actual data size */
        if (handle->info.bits_per_sample == 1) {
            uint64_t bytes_per_channel;
            uint64_t blocks_per_channel;
            uint64_t samples_per_block;
            uint64_t temp_samples;

            /* Validate channel count to prevent division by zero */
            if (handle->info.channel_count == 0) {
                return DSF_ERROR_INVALID_CHANNELS;
            }

            bytes_per_channel = data_size / handle->info.channel_count;
            blocks_per_channel = bytes_per_channel / DSF_BLOCK_SIZE_PER_CHANNEL;

            /* Safe multiplication: blocks_per_channel * DSF_BLOCK_SIZE_PER_CHANNEL */
            if (dsf_uint64_mul_overflow(blocks_per_channel, DSF_BLOCK_SIZE_PER_CHANNEL, &samples_per_block)) {
                return DSF_ERROR_INVALID_CHUNK;
            }

            /* Safe multiplication: samples_per_block * 8 */
            if (dsf_uint64_mul_overflow(samples_per_block, 8, &temp_samples)) {
                return DSF_ERROR_INVALID_CHUNK;
            }

            handle->info.sample_count = temp_samples;
        }
    }

    /* Read metadata if present */
    if (metadata_offset > 0 && handle->mode != DSF_FILE_MODE_WRITE) {
        ret = dsf_chunk_read_metadata(handle->io,
                                       metadata_offset,
                                       &handle->metadata_size,
                                       &handle->metadata_buffer);
        if (ret != DSF_SUCCESS && ret != DSF_ERROR_NO_METADATA) {
            /* Non-fatal - continue without metadata */
            handle->metadata_size = 0;
            handle->metadata_buffer = NULL;
        }
    }

    return DSF_SUCCESS;
}

static int dsf_write_file_structure(dsf_t *handle) {
    uint64_t data_offset;
    int ret;

    if (!handle || !handle->io) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Write DSD chunk (file size will be updated in finalize) */
    ret = dsf_chunk_write_dsd_header(handle->io, 0, 0);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write fmt chunk */
    ret = dsf_chunk_write_fmt(handle->io, &handle->info);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write data chunk header (size will be updated in finalize) */
    ret = dsf_chunk_write_data_header(handle->io, 0, &data_offset);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    handle->info.audio_data_offset = data_offset;

    return DSF_SUCCESS;
}

/* =============================================================================
 * File Lifecycle Operations
 * ===========================================================================*/

int dsf_alloc(dsf_t **handle) {
    dsf_t *h;

    if (!handle) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    h = (dsf_t *)sa_calloc(1, sizeof(dsf_t));
    if (!h) {
        return DSF_ERROR_OUT_OF_MEMORY;
    }

    dsf_reset_file_state(h);
    *handle = h;

    return DSF_SUCCESS;
}

int dsf_free(dsf_t *handle) {
    if (!handle) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode != DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    dsf_reset_file_state(handle);
    sa_free(handle);

    return DSF_SUCCESS;
}

int dsf_create(dsf_t *handle,
               const char *filename,
               uint32_t sample_rate,
               uint32_t channel_type,
               uint32_t channel_count,
               uint32_t bits_per_sample) {
    int ret;

    if (!handle || !filename) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode != DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_ALREADY_OPEN;
    }

    /* Validate parameters */
    if (!dsf_is_valid_sample_rate(sample_rate)) {
        return DSF_ERROR_INVALID_SAMPLE_RATE;
    }
    if (!dsf_is_valid_channel_type(channel_type)) {
        return DSF_ERROR_INVALID_CHANNELS;
    }
    if (channel_count < 1 || channel_count > DSF_MAX_CHANNELS) {
        return DSF_ERROR_INVALID_CHANNELS;
    }
    if (!dsf_is_valid_bits_per_sample(bits_per_sample)) {
        return DSF_ERROR_INVALID_BIT_DEPTH;
    }

    /* Open chunk file for writing */
    ret = dsf_chunk_file_open_write(&handle->io, filename);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Initialize file info */
    memset(&handle->info, 0, sizeof(dsf_file_info_t));
    handle->info.format_version = DSF_FORMAT_VERSION;
    handle->info.format_id = DSF_FORMAT_DSD_RAW;
    handle->info.channel_type = channel_type;
    handle->info.channel_count = channel_count;
    handle->info.sampling_frequency = sample_rate;
    handle->info.bits_per_sample = bits_per_sample;
    handle->info.sample_count = 0; /* Will be set when writing */
    handle->info.block_size_per_channel = DSF_BLOCK_SIZE_PER_CHANNEL;

    /* Write initial file structure */
    ret = dsf_write_file_structure(handle);
    if (ret != DSF_SUCCESS) {
        dsf_chunk_file_close(handle->io);
        handle->io = NULL;
        return ret;
    }

    handle->mode = DSF_FILE_MODE_WRITE;
    handle->samples_written = 0;
    handle->bytes_written = 0;

    return DSF_SUCCESS;
}

int dsf_open(dsf_t *handle, const char *filename) {
    int ret;

    if (!handle || !filename) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode != DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_ALREADY_OPEN;
    }

    /* Open chunk file for reading */
    ret = dsf_chunk_file_open_read(&handle->io, filename);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Read and parse file structure */
    ret = dsf_read_file_structure(handle);
    if (ret != DSF_SUCCESS) {
        dsf_chunk_file_close(handle->io);
        handle->io = NULL;
        return ret;
    }

    handle->mode = DSF_FILE_MODE_READ;

    return DSF_SUCCESS;
}

int dsf_open_modify(dsf_t *handle, const char *filename) {
    int ret;

    if (!handle || !filename) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode != DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_ALREADY_OPEN;
    }

    /* Open chunk file for modification */
    ret = dsf_chunk_file_open_modify(&handle->io, filename);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Read and parse file structure */
    ret = dsf_read_file_structure(handle);
    if (ret != DSF_SUCCESS) {
        dsf_chunk_file_close(handle->io);
        handle->io = NULL;
        return ret;
    }

    handle->mode = DSF_FILE_MODE_MODIFY;

    return DSF_SUCCESS;
}

int dsf_finalize(dsf_t *handle) {
    uint64_t current_pos;
    uint64_t metadata_offset;
    uint64_t file_size;
    int ret;

    if (!handle) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode != DSF_FILE_MODE_WRITE && handle->mode != DSF_FILE_MODE_MODIFY) {
        return DSF_ERROR_INVALID_MODE;
    }

    if (!handle->io) {
        return DSF_ERROR_NOT_OPEN;
    }

    /* Flush any remaining buffered audio data with zero padding */
    if (handle->mode == DSF_FILE_MODE_WRITE && handle->bytes_buffered > 0) {
        ret = dsf_flush_audio_data(handle);
        if (ret != DSF_SUCCESS) {
            return ret;
        }
    }

    /* Get current position (end of audio data) */
    ret = dsf_chunk_file_get_position(handle->io, &current_pos);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Update data chunk size */
    {
        uint64_t audio_data_offset = DSF_DSD_CHUNK_SIZE + DSF_FMT_CHUNK_SIZE + DSF_DATA_CHUNK_HEADER_SIZE;
        uint64_t data_size = current_pos - audio_data_offset;

        ret = dsf_chunk_update_data_size(handle->io, data_size);
        if (ret != DSF_SUCCESS) {
            return ret;
        }
        handle->info.audio_data_size = data_size;
    }

    /* Update sample count in fmt chunk */
    ret = dsf_chunk_update_sample_count(handle->io, handle->info.sample_count);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    metadata_offset = 0;

    /* Write metadata if present */
    if (handle->metadata_buffer && handle->metadata_size > 0) {
        ret = dsf_chunk_write_metadata(handle->io,
                                        handle->metadata_buffer,
                                        handle->metadata_size,
                                        &metadata_offset);
        if (ret != DSF_SUCCESS) {
            return ret;
        }
    }

    /* Get final file size */
    ret = dsf_chunk_file_get_position(handle->io, &file_size);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Update file size in DSD chunk */
    ret = dsf_chunk_update_file_size(handle->io, file_size);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Update metadata pointer if metadata was written */
    if (metadata_offset > 0) {
        ret = dsf_chunk_update_metadata_offset(handle->io, metadata_offset);
        if (ret != DSF_SUCCESS) {
            return ret;
        }
    }

    handle->info.file_size = file_size;
    handle->info.metadata_offset = metadata_offset;

    return DSF_SUCCESS;
}

int dsf_close(dsf_t *handle) {
    int ret;

    if (!handle) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_SUCCESS; /* Already closed */
    }

    if (!handle->io) {
        return DSF_ERROR_NOT_OPEN;
    }

    ret = dsf_chunk_file_close(handle->io);
    handle->io = NULL;

    dsf_reset_file_state(handle);

    return ret;
}

int dsf_remove_file(dsf_t *handle) {
    char filename[DSF_MAX_STR_SIZE];
    int ret;

    if (!handle || !handle->io) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Get filename before closing */
    ret = dsf_chunk_file_get_filename(handle->io, filename, sizeof(filename));
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Close file */
    ret = dsf_close(handle);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Delete file */
    if (remove(filename) != 0) {
        return DSF_ERROR_GENERIC;
    }

    return DSF_SUCCESS;
}

/* =============================================================================
 * File Properties
 * ===========================================================================*/

int dsf_get_file_info(dsf_t *handle, dsf_file_info_t *info) {
    if (!handle || !info) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    memcpy(info, &handle->info, sizeof(dsf_file_info_t));

    return DSF_SUCCESS;
}

int dsf_get_file_mode(dsf_t *handle, dsf_file_mode_t *mode) {
    if (!handle || !mode) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    *mode = handle->mode;

    return DSF_SUCCESS;
}

int dsf_get_filename(dsf_t *handle, char *filename, size_t buffer_size) {
    if (!handle || !filename) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (!handle->io) {
        return DSF_ERROR_NOT_OPEN;
    }

    return dsf_chunk_file_get_filename(handle->io, filename, buffer_size);
}

int dsf_get_channel_count(dsf_t *handle, uint32_t *channel_count) {
    if (!handle || !channel_count) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    *channel_count = handle->info.channel_count;

    return DSF_SUCCESS;
}

int dsf_get_channel_type(dsf_t *handle, uint32_t *channel_type) {
    if (!handle || !channel_type) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    *channel_type = handle->info.channel_type;

    return DSF_SUCCESS;
}

int dsf_get_bits_per_sample(dsf_t *handle, uint32_t *bits_per_sample) {
    if (!handle || !bits_per_sample) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    *bits_per_sample = handle->info.bits_per_sample;

    return DSF_SUCCESS;
}

int dsf_get_sample_rate(dsf_t *handle, uint32_t *sample_rate) {
    if (!handle || !sample_rate) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    *sample_rate = handle->info.sampling_frequency;

    return DSF_SUCCESS;
}

int dsf_get_sample_count(dsf_t *handle, uint64_t *sample_count) {
    if (!handle || !sample_count) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    *sample_count = handle->info.sample_count;

    return DSF_SUCCESS;
}

int dsf_get_audio_data_size(dsf_t *handle, uint64_t *data_size) {
    if (!handle || !data_size) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    *data_size = handle->info.audio_data_size;

    return DSF_SUCCESS;
}

int dsf_get_file_size(dsf_t *handle, uint64_t *file_size) {
    if (!handle || !file_size) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    *file_size = handle->info.file_size;

    return DSF_SUCCESS;
}

int dsf_get_duration(dsf_t *handle, double *duration) {
    if (!handle || !duration) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    *duration = handle->info.duration_seconds;

    return DSF_SUCCESS;
}

/* =============================================================================
 * Audio Data I/O
 * ===========================================================================*/

/**
 * @brief Convert one DSF block group to byte-interleaved format
 *
 * Converts DSF block-interleaved data to DSDIFF byte-interleaved format.
 * Also performs bit reversal (DSF=LSB-first to DSDIFF=MSB-first).
 *
 * @param dsf_data Source DSF data (one block group: 4096 * channel_count bytes)
 * @param dsdiff_data Destination buffer for byte-interleaved output
 * @param channel_count Number of channels
 */
static void dsf_convert_block_to_byte_interleaved(const uint8_t *dsf_data,
                                                   uint8_t *dsdiff_data,
                                                   uint32_t channel_count) {
    /* Convert from DSF block-interleaved to DSDIFF byte-interleaved format
     * DSF:    [L0..L4095][R0..R4095]
     * DSDIFF: [L0][R0][L1][R1][L2][R2]...
     *
     * Also bit-reverse each byte (DSF=LSB-first, DSDIFF=MSB-first)
     */
    for (size_t byte_in_block = 0; byte_in_block < DSF_BLOCK_SIZE_PER_CHANNEL; byte_in_block++) {
        for (uint32_t ch = 0; ch < channel_count; ch++) {
            /* Source: block-interleaved position */
            size_t src_offset = ch * DSF_BLOCK_SIZE_PER_CHANNEL + byte_in_block;
            /* Destination: byte-interleaved position */
            size_t dst_offset = byte_in_block * channel_count + ch;

            dsdiff_data[dst_offset] = ff_reverse[dsf_data[src_offset]];
        }
    }
}

/**
 * @brief Read and buffer one block group from file
 *
 * Reads one DSF block group (4096 * channel_count bytes), converts it to
 * byte-interleaved format, and stores it in the read buffer.
 *
 * @param handle File handle
 * @return DSF_SUCCESS, DSF_ERROR_END_OF_DATA, or error code
 */
static int dsf_read_and_buffer_block_group(dsf_t *handle) {
    size_t block_group_size = DSF_BLOCK_SIZE_PER_CHANNEL * handle->info.channel_count;
    size_t file_bytes_read;
    int ret;

    ret = dsf_chunk_read_audio_data(handle->io, handle->scratch_buffer,
                                     block_group_size, &file_bytes_read);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    if (file_bytes_read == 0) {
        return DSF_ERROR_END_OF_DATA;
    }

    /* Convert to byte-interleaved and store in read buffer */
    dsf_convert_block_to_byte_interleaved(handle->scratch_buffer,
                                           handle->read_buffer,
                                           handle->info.channel_count);

    handle->read_buffer_pos = 0;
    handle->read_buffer_valid = file_bytes_read;  /* Same size, just reordered */

    return DSF_SUCCESS;
}

int dsf_read_audio_data(dsf_t *handle,
                        uint8_t *buffer,
                        size_t num_bytes,
                        size_t *bytes_read) {
    size_t total_read = 0;
    size_t output_pos = 0;
    int ret;

    if (!handle || !buffer || !bytes_read) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    *bytes_read = 0;

    if (handle->mode != DSF_FILE_MODE_READ && handle->mode != DSF_FILE_MODE_MODIFY) {
        return DSF_ERROR_INVALID_MODE;
    }

    if (!handle->io) {
        return DSF_ERROR_NOT_OPEN;
    }

    /* Read data using buffered approach:
     * 1. First serve any data remaining in the read buffer
     * 2. Read and convert new block groups as needed
     */
    while (total_read < num_bytes) {
        /* If read buffer has data, copy from it */
        if (handle->read_buffer_pos < handle->read_buffer_valid) {
            size_t available = handle->read_buffer_valid - handle->read_buffer_pos;
            size_t needed = num_bytes - total_read;
            size_t to_copy = (available < needed) ? available : needed;

            memcpy(&buffer[output_pos], &handle->read_buffer[handle->read_buffer_pos], to_copy);
            handle->read_buffer_pos += to_copy;
            output_pos += to_copy;
            total_read += to_copy;
        } else {
            /* Buffer exhausted, read next block group */
            ret = dsf_read_and_buffer_block_group(handle);
            if (ret == DSF_ERROR_END_OF_DATA) {
                /* End of file - return what we have */
                break;
            }
            if (ret != DSF_SUCCESS) {
                *bytes_read = total_read;
                return ret;
            }
        }
    }

    *bytes_read = total_read;
    return DSF_SUCCESS;
}

/**
 * @brief Write one complete block group to file
 *
 * Assembles channel buffers into DSF block-interleaved format and writes.
 * Each block group is [Ch0 block][Ch1 block]...[ChN block], each 4096 bytes.
 *
 * @param handle File handle
 * @param bytes_to_write Bytes per channel to write (up to 4096)
 * @param pad_to_block If true, zero-pad to 4096 bytes per channel
 * @return DSF_SUCCESS or error code
 */
static int dsf_write_block_group(dsf_t *handle, size_t bytes_to_write, int pad_to_block) {
    uint32_t ch;
    size_t written;
    size_t block_bytes = pad_to_block ? DSF_BLOCK_SIZE_PER_CHANNEL : bytes_to_write;
    size_t total_bytes = block_bytes * handle->info.channel_count;
    int ret;

    /* Assemble block group: [Ch0][Ch1]...[ChN], each block_bytes */
    for (ch = 0; ch < handle->info.channel_count; ch++) {
        size_t offset = ch * block_bytes;
        memcpy(&handle->scratch_buffer[offset], handle->channel_buffers[ch], bytes_to_write);
        if (pad_to_block && bytes_to_write < DSF_BLOCK_SIZE_PER_CHANNEL) {
            /* Zero-pad the remainder of this channel's block */
            memset(&handle->scratch_buffer[offset + bytes_to_write], 0,
                   DSF_BLOCK_SIZE_PER_CHANNEL - bytes_to_write);
        }
    }

    ret = dsf_chunk_write_audio_data(handle->io, handle->scratch_buffer,
                                      total_bytes, &written);
    if (ret == DSF_SUCCESS) {
        handle->bytes_written += written;
    }

    return ret;
}

int dsf_write_audio_data(dsf_t *handle,
                         const uint8_t *buffer,
                         size_t num_bytes,
                         size_t *bytes_written) {
    uint32_t ch;
    size_t input_pos = 0;
    size_t bytes_per_channel;
    size_t total_written = 0;
    int ret;

    if (!handle || !buffer || !bytes_written) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode != DSF_FILE_MODE_WRITE) {
        return DSF_ERROR_INVALID_MODE;
    }

    if (!handle->io) {
        return DSF_ERROR_NOT_OPEN;
    }

    if (num_bytes % handle->info.channel_count != 0) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    bytes_per_channel = num_bytes / handle->info.channel_count;

    /* Process input data: de-interleave into channel buffers, bit-reverse,
     * and write complete blocks as they fill up.
     *
     * Input format (DSDIFF byte-interleaved): [L0][R0][L1][R1][L2][R2]...
     * We accumulate into per-channel buffers with bit reversal.
     * When buffers reach 4096 bytes, write a complete block group.
     */
    for (size_t i = 0; i < bytes_per_channel; i++) {
        /* De-interleave and bit-reverse each sample into channel buffers */
        for (ch = 0; ch < handle->info.channel_count; ch++) {
            handle->channel_buffers[ch][handle->bytes_buffered] =
                ff_reverse[buffer[input_pos++]];
        }
        handle->bytes_buffered++;

        /* When we have a complete block (4096 bytes per channel), write it */
        if (handle->bytes_buffered == DSF_BLOCK_SIZE_PER_CHANNEL) {
            ret = dsf_write_block_group(handle, DSF_BLOCK_SIZE_PER_CHANNEL, 0);
            if (ret != DSF_SUCCESS) {
                *bytes_written = total_written;
                return ret;
            }
            total_written += DSF_BLOCK_SIZE_PER_CHANNEL * handle->info.channel_count;
            handle->bytes_buffered = 0;
        }
    }

    /* Update sample count from actual input data */
    if (handle->info.bits_per_sample == 1) {
        /* For DSD, 8 samples per byte per channel */
        handle->samples_written += bytes_per_channel * 8;
    } else {
        /* For 8-bit, 1 sample per byte per channel */
        handle->samples_written += bytes_per_channel;
    }

    handle->info.sample_count = handle->samples_written;
    handle->info.audio_data_size = handle->bytes_written;

    /* Recalculate derived properties */
    handle->info.duration_seconds = dsf_calculate_duration(
        handle->info.sample_count,
        handle->info.sampling_frequency);

    handle->info.bit_rate = dsf_calculate_bit_rate(
        handle->info.channel_count,
        handle->info.sampling_frequency,
        handle->info.bits_per_sample);

    *bytes_written = total_written;
    return DSF_SUCCESS;
}

/**
 * @brief Flush any buffered audio data with zero padding
 *
 * Call this before dsf_finalize() to write any remaining partial block
 * with zero padding as required by DSF specification.
 *
 * @param handle File handle
 * @return DSF_SUCCESS or error code
 */
int dsf_flush_audio_data(dsf_t *handle) {
    int ret;

    if (!handle) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode != DSF_FILE_MODE_WRITE) {
        return DSF_ERROR_INVALID_MODE;
    }

    if (!handle->io) {
        return DSF_ERROR_NOT_OPEN;
    }

    /* Write any remaining buffered data with zero padding */
    if (handle->bytes_buffered > 0) {
        ret = dsf_write_block_group(handle, handle->bytes_buffered, 1);
        if (ret != DSF_SUCCESS) {
            return ret;
        }
        handle->info.audio_data_size = handle->bytes_written;
        handle->bytes_buffered = 0;
    }

    return DSF_SUCCESS;
}

int dsf_seek_to_audio_start(dsf_t *handle) {
    uint64_t new_pos;
    int ret;

    if (!handle) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode != DSF_FILE_MODE_READ && handle->mode != DSF_FILE_MODE_MODIFY) {
        return DSF_ERROR_INVALID_MODE;
    }

    if (!handle->io) {
        return DSF_ERROR_NOT_OPEN;
    }

    ret = dsf_chunk_file_seek(handle->io,
                               handle->info.audio_data_offset,
                               DSF_SEEK_SET,
                               &new_pos);

    /* Invalidate read buffer after seek */
    if (ret == DSF_SUCCESS) {
        handle->read_buffer_pos = 0;
        handle->read_buffer_valid = 0;
    }

    return ret;
}

int dsf_seek_audio_data(dsf_t *handle,
                        int64_t byte_offset,
                        dsf_seek_dir_t origin) {
    uint64_t new_pos;
    int64_t actual_offset;
    int ret;

    if (!handle) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode != DSF_FILE_MODE_READ && handle->mode != DSF_FILE_MODE_MODIFY) {
        return DSF_ERROR_INVALID_MODE;
    }

    if (!handle->io) {
        return DSF_ERROR_NOT_OPEN;
    }

    /* Calculate actual file offset based on origin */
    switch (origin) {
        case DSF_SEEK_SET:
            actual_offset = handle->info.audio_data_offset + byte_offset;
            ret = dsf_chunk_file_seek(handle->io, actual_offset, DSF_SEEK_SET, &new_pos);
            break;

        case DSF_SEEK_CUR:
            ret = dsf_chunk_file_seek(handle->io, byte_offset, DSF_SEEK_CUR, &new_pos);
            break;

        case DSF_SEEK_END:
            actual_offset = handle->info.audio_data_offset + handle->info.audio_data_size + byte_offset;
            ret = dsf_chunk_file_seek(handle->io, actual_offset, DSF_SEEK_SET, &new_pos);
            break;

        default:
            return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Invalidate read buffer after seek */
    if (ret == DSF_SUCCESS) {
        handle->read_buffer_pos = 0;
        handle->read_buffer_valid = 0;
    }

    return ret;
}

int dsf_get_audio_position(dsf_t *handle, uint64_t *position) {
    uint64_t file_pos;
    int ret;

    if (!handle || !position) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode != DSF_FILE_MODE_READ && handle->mode != DSF_FILE_MODE_MODIFY) {
        return DSF_ERROR_INVALID_MODE;
    }

    if (!handle->io) {
        return DSF_ERROR_NOT_OPEN;
    }

    ret = dsf_chunk_file_get_position(handle->io, &file_pos);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Convert file position to audio data position */
    if (file_pos < handle->info.audio_data_offset) {
        *position = 0;
    } else {
        *position = file_pos - handle->info.audio_data_offset;
    }

    return DSF_SUCCESS;
}

/* =============================================================================
 * Metadata Operations
 * ===========================================================================*/

int dsf_has_metadata(dsf_t *handle, int *has_metadata) {
    if (!handle || !has_metadata) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    *has_metadata = (handle->metadata_buffer != NULL && handle->metadata_size > 0) ? 1 : 0;

    return DSF_SUCCESS;
}

int dsf_get_metadata_size(dsf_t *handle, uint64_t *metadata_size) {
    if (!handle || !metadata_size) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    *metadata_size = handle->metadata_size;

    return DSF_SUCCESS;
}

int dsf_read_metadata(dsf_t *handle, uint8_t **buffer, uint64_t *size) {
    uint8_t *metadata_copy;
    size_t size_to_alloc;

    if (!handle || !buffer || !size) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED) {
        return DSF_ERROR_NOT_OPEN;
    }

    if (!handle->metadata_buffer || handle->metadata_size == 0) {
        return DSF_ERROR_NO_METADATA;
    }

    /* Safe cast to size_t */
    if (dsf_uint64_to_sizet(handle->metadata_size, &size_to_alloc)) {
        return DSF_ERROR_INVALID_METADATA;
    }

    /* Allocate copy of metadata */
    metadata_copy = (uint8_t *)sa_malloc(size_to_alloc);
    if (!metadata_copy) {
        return DSF_ERROR_OUT_OF_MEMORY;
    }

    memcpy(metadata_copy, handle->metadata_buffer, size_to_alloc);

    *buffer = metadata_copy;
    *size = handle->metadata_size;

    return DSF_SUCCESS;
}

int dsf_write_metadata(dsf_t *handle, const uint8_t *buffer, uint64_t size) {
    uint8_t *new_buffer;
    size_t size_to_alloc;

    if (!handle || !buffer || size == 0) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode != DSF_FILE_MODE_WRITE && handle->mode != DSF_FILE_MODE_MODIFY) {
        return DSF_ERROR_INVALID_MODE;
    }

    /* Validate metadata size is reasonable */
    if (size > DSF_MAX_REASONABLE_METADATA_SIZE) {
        return DSF_ERROR_INVALID_METADATA;
    }

    /* Safe cast to size_t */
    if (dsf_uint64_to_sizet(size, &size_to_alloc)) {
        return DSF_ERROR_INVALID_METADATA;
    }

    /* Allocate new buffer */
    new_buffer = (uint8_t *)sa_malloc(size_to_alloc);
    if (!new_buffer) {
        return DSF_ERROR_OUT_OF_MEMORY;
    }

    memcpy(new_buffer, buffer, size_to_alloc);

    /* Free old buffer if exists */
    if (handle->metadata_buffer) {
        sa_free(handle->metadata_buffer);
    }

    handle->metadata_buffer = new_buffer;
    handle->metadata_size = size;
    handle->metadata_modified = 1;

    return DSF_SUCCESS;
}

/* =============================================================================
 * Utility Functions
 * ===========================================================================*/

int dsf_validate(dsf_t *handle) {
    if (!handle) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (handle->mode == DSF_FILE_MODE_CLOSED || !handle->io) {
        return DSF_ERROR_NOT_OPEN;
    }

    return dsf_chunk_validate_file(handle->io);
}

const char* dsf_error_string(int error_code) {
    switch (error_code) {
        case DSF_SUCCESS:
            return "Success";

        /* File state errors */
        case DSF_ERROR_ALREADY_OPEN:
            return "File already open";
        case DSF_ERROR_NOT_OPEN:
            return "File not open";
        case DSF_ERROR_OPEN_READ:
            return "File is open for reading";
        case DSF_ERROR_OPEN_WRITE:
            return "File is open for writing";

        /* File format errors */
        case DSF_ERROR_INVALID_FILE:
            return "Invalid DSF file";
        case DSF_ERROR_INVALID_CHUNK:
            return "Invalid chunk structure";
        case DSF_ERROR_INVALID_DSF:
            return "Invalid DSF format";
        case DSF_ERROR_INVALID_VERSION:
            return "Invalid DSF version";
        case DSF_ERROR_UNSUPPORTED_COMPRESSION:
            return "Unsupported compression";
        case DSF_ERROR_UNEXPECTED_EOF:
            return "Unexpected end of file";

        /* I/O errors */
        case DSF_ERROR_READ:
            return "Read error";
        case DSF_ERROR_WRITE:
            return "Write error";
        case DSF_ERROR_SEEK:
            return "Seek error";
        case DSF_ERROR_END_OF_DATA:
            return "End of sound data reached";
        case DSF_ERROR_MAX_FILE_SIZE:
            return "Maximum file size exceeded";
        case DSF_ERROR_FILE_NOT_FOUND:
            return "File not found";
        case DSF_ERROR_CANNOT_CREATE_FILE:
            return "Cannot create file";
        case DSF_ERROR_CONVERSION_FAILED:
            return "String conversion failed";
        case DSF_ERROR_GENERIC:
            return "Generic error";

        /* Data errors */
        case DSF_ERROR_NO_CHANNEL_INFO:
            return "No channel information";
        case DSF_ERROR_INVALID_CHANNELS:
            return "Invalid number of channels";
        case DSF_ERROR_CHANNELS_INCORRECT:
            return "Channel identifiers incorrect";
        case DSF_ERROR_INVALID_SAMPLE_RATE:
            return "Invalid sample rate";
        case DSF_ERROR_INVALID_BIT_DEPTH:
            return "Invalid bits per sample";
        case DSF_ERROR_INVALID_BLOCK_SIZE:
            return "Invalid block size";

        /* Operation errors */
        case DSF_ERROR_INVALID_ARG:
            return "Invalid argument";
        case DSF_ERROR_OUT_OF_MEMORY:
            return "Out of memory";
        case DSF_ERROR_BUFFER_TOO_SMALL:
            return "Buffer too small";
        case DSF_ERROR_INVALID_MODE:
            return "Invalid file mode";
        case DSF_ERROR_OPERATION_NOT_ALLOWED:
            return "Operation not allowed in current state";

        /* Metadata errors */
        case DSF_ERROR_NO_METADATA:
            return "No metadata";
        case DSF_ERROR_INVALID_METADATA:
            return "Invalid metadata";

        default:
            return "Unknown error";
    }
}
