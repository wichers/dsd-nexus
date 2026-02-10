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
#include "dsf_types.h"

#include <libsautil/mem.h>

#include <stdlib.h>
#include <string.h>

/* =============================================================================
 * Chunk File Operations
 * ===========================================================================*/

int dsf_chunk_file_close(dsf_chunk_t *chunk) {
    int ret;
    if (!chunk) {
        return DSF_ERROR_INVALID_PARAMETER;
    }
    ret = dsf_io_close(chunk);
    sa_free(chunk);
    return ret;
}

int dsf_chunk_file_get_position(dsf_chunk_t *chunk, uint64_t *position) {
    if (!chunk || !position) {
        return DSF_ERROR_INVALID_PARAMETER;
    }
    return dsf_io_get_position(chunk, position);
}

int dsf_chunk_file_open_modify(dsf_chunk_t **chunk, const char *filename) {
    dsf_chunk_t *cf;
    int ret;

    if (!chunk || !filename) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    cf = (dsf_chunk_t *)sa_malloc(sizeof(dsf_chunk_t));
    if (!cf) {
        return DSF_ERROR_OUT_OF_MEMORY;
    }

    ret = dsf_io_open_modify(cf, filename);
    if (ret != DSF_SUCCESS) {
        sa_free(cf);
        return ret;
    }

    *chunk = cf;
    return DSF_SUCCESS;
}

int dsf_chunk_file_open_read(dsf_chunk_t **chunk, const char *filename) {
    dsf_chunk_t *cf;
    int ret;

    if (!chunk || !filename) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    cf = (dsf_chunk_t *)sa_malloc(sizeof(dsf_chunk_t));
    if (!cf) {
        return DSF_ERROR_OUT_OF_MEMORY;
    }

    ret = dsf_io_open_read(cf, filename);
    if (ret != DSF_SUCCESS) {
        sa_free(cf);
        return ret;
    }

    *chunk = cf;
    return DSF_SUCCESS;
}

int dsf_chunk_file_open_write(dsf_chunk_t **chunk, const char *filename) {
    dsf_chunk_t *cf;
    int ret;

    if (!chunk || !filename) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    cf = (dsf_chunk_t *)sa_malloc(sizeof(dsf_chunk_t));
    if (!cf) {
        return DSF_ERROR_OUT_OF_MEMORY;
    }

    ret = dsf_io_open_write(cf, filename);
    if (ret != DSF_SUCCESS) {
        sa_free(cf);
        return ret;
    }

    *chunk = cf;
    return DSF_SUCCESS;
}

int dsf_chunk_file_read_bytes(dsf_chunk_t *chunk, uint8_t *buffer,
                               size_t num_bytes, size_t *bytes_read) {
    if (!chunk) {
        return DSF_ERROR_INVALID_PARAMETER;
    }
    return dsf_io_read_bytes(chunk, buffer, num_bytes, bytes_read);
}

int dsf_chunk_file_seek(dsf_chunk_t *chunk, int64_t offset,
                        dsf_seek_dir_t origin, uint64_t *new_pos) {
    if (!chunk) {
        return DSF_ERROR_INVALID_PARAMETER;
    }
    return dsf_io_seek(chunk, offset, origin, new_pos);
}

int dsf_chunk_file_write_bytes(dsf_chunk_t *chunk, const uint8_t *buffer,
                                size_t num_bytes, size_t *bytes_written) {
    if (!chunk) {
        return DSF_ERROR_INVALID_PARAMETER;
    }
    return dsf_io_write_bytes(chunk, buffer, num_bytes, bytes_written);
}

int dsf_chunk_file_get_size(dsf_chunk_t *chunk, uint64_t *size) {
    if (!chunk || !size) {
        return DSF_ERROR_INVALID_PARAMETER;
    }
    return dsf_io_get_file_size(chunk, size);
}

int dsf_chunk_file_get_filename(dsf_chunk_t *chunk, char *filename,
                                 size_t buffer_size) {
    if (!chunk || !filename) {
        return DSF_ERROR_INVALID_PARAMETER;
    }
    return dsf_io_get_filename(chunk, filename, buffer_size);
}

/* =============================================================================
 * DSD Chunk Operations (28 bytes total)
 * ===========================================================================*/

int dsf_chunk_read_dsd_header(dsf_chunk_t *chunk,
                               uint64_t *file_size,
                               uint64_t *metadata_offset) {
    uint32_t chunk_id;
    uint64_t chunk_size;
    int ret;

    if (!chunk || !file_size || !metadata_offset) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    *file_size = 0;
    *metadata_offset = 0;

    /* Read and validate chunk ID ('DSD ') */
    ret = dsf_io_read_uint32_le(chunk, &chunk_id);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (chunk_id != DSF_DSD_CHUNK_ID) {
        return DSF_ERROR_INVALID_DSF;
    }

    /* Read and validate chunk size (must be 28) */
    ret = dsf_io_read_uint64_le(chunk, &chunk_size);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (chunk_size != DSF_DSD_CHUNK_SIZE) {
        return DSF_ERROR_INVALID_CHUNK;
    }

    /* Read total file size */
    ret = dsf_io_read_uint64_le(chunk, file_size);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Read metadata pointer (0 if no metadata) */
    ret = dsf_io_read_uint64_le(chunk, metadata_offset);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    return DSF_SUCCESS;
}

int dsf_chunk_write_dsd_header(dsf_chunk_t *chunk,
                                uint64_t file_size,
                                uint64_t metadata_offset) {
    int ret;

    if (!chunk) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Write chunk ID ('DSD ') */
    ret = dsf_io_write_uint32_le(chunk, DSF_DSD_CHUNK_ID);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write chunk size (28) */
    ret = dsf_io_write_uint64_le(chunk, DSF_DSD_CHUNK_SIZE);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write total file size */
    ret = dsf_io_write_uint64_le(chunk, file_size);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write metadata pointer */
    ret = dsf_io_write_uint64_le(chunk, metadata_offset);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    return DSF_SUCCESS;
}

int dsf_chunk_update_file_size(dsf_chunk_t *chunk, uint64_t file_size) {
    uint64_t saved_pos;
    uint64_t new_pos;
    int ret;

    if (!chunk) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Save current position */
    ret = dsf_io_get_position(chunk, &saved_pos);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Seek to file size field (offset 12 in DSD chunk) */
    ret = dsf_io_seek(chunk, 12, DSF_SEEK_SET, &new_pos);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write file size */
    ret = dsf_io_write_uint64_le(chunk, file_size);
    if (ret != DSF_SUCCESS) {
        /* Try to restore position even on error */
        dsf_io_seek(chunk, saved_pos, DSF_SEEK_SET, &new_pos);
        return ret;
    }

    /* Restore original position */
    ret = dsf_io_seek(chunk, saved_pos, DSF_SEEK_SET, &new_pos);
    return ret;
}

int dsf_chunk_update_metadata_offset(dsf_chunk_t *chunk, uint64_t metadata_offset) {
    uint64_t saved_pos;
    uint64_t new_pos;
    int ret;

    if (!chunk) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Save current position */
    ret = dsf_io_get_position(chunk, &saved_pos);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Seek to metadata offset field (offset 20 in DSD chunk) */
    ret = dsf_io_seek(chunk, 20, DSF_SEEK_SET, &new_pos);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write metadata offset */
    ret = dsf_io_write_uint64_le(chunk, metadata_offset);
    if (ret != DSF_SUCCESS) {
        /* Try to restore position even on error */
        dsf_io_seek(chunk, saved_pos, DSF_SEEK_SET, &new_pos);
        return ret;
    }

    /* Restore original position */
    ret = dsf_io_seek(chunk, saved_pos, DSF_SEEK_SET, &new_pos);
    return ret;
}

int dsf_chunk_update_data_size(dsf_chunk_t *chunk, uint64_t data_size) {
    uint64_t saved_pos;
    uint64_t new_pos;
    uint64_t chunk_size;
    int ret;

    if (!chunk) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Save current position */
    ret = dsf_io_get_position(chunk, &saved_pos);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Seek to data chunk size field (offset 4 in data chunk, after DSD + fmt chunks) */
    /* data chunk starts at offset DSF_DSD_CHUNK_SIZE + DSF_FMT_CHUNK_SIZE = 80 */
    /* chunk size field is at offset 4 within data chunk = file offset 84 */
    ret = dsf_io_seek(chunk, DSF_DSD_CHUNK_SIZE + DSF_FMT_CHUNK_SIZE + 4, DSF_SEEK_SET, &new_pos);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* chunk_size = header size (12) + data size */
    if (dsf_uint64_add_overflow(DSF_DATA_CHUNK_HEADER_SIZE, data_size, &chunk_size)) {
        dsf_io_seek(chunk, saved_pos, DSF_SEEK_SET, &new_pos);
        return DSF_ERROR_INVALID_DSF;
    }

    /* Write chunk size */
    ret = dsf_io_write_uint64_le(chunk, chunk_size);
    if (ret != DSF_SUCCESS) {
        /* Try to restore position even on error */
        dsf_io_seek(chunk, saved_pos, DSF_SEEK_SET, &new_pos);
        return ret;
    }

    /* Restore original position */
    ret = dsf_io_seek(chunk, saved_pos, DSF_SEEK_SET, &new_pos);
    return ret;
}

int dsf_chunk_update_sample_count(dsf_chunk_t *chunk, uint64_t sample_count) {
    uint64_t saved_pos;
    uint64_t new_pos;
    int ret;

    if (!chunk) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Save current position */
    ret = dsf_io_get_position(chunk, &saved_pos);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Seek to sample count field in fmt chunk */
    /* fmt chunk starts at offset DSF_DSD_CHUNK_SIZE = 28 */
    /* sample count is at offset 36 within fmt chunk (after chunk_id, chunk_size,
       format_version, format_id, channel_type, channel_count, sampling_frequency,
       bits_per_sample) = 4 + 8 + 4 + 4 + 4 + 4 + 4 + 4 = 36 */
    /* So file offset = 28 + 36 = 64 */
    ret = dsf_io_seek(chunk, DSF_DSD_CHUNK_SIZE + 36, DSF_SEEK_SET, &new_pos);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write sample count */
    ret = dsf_io_write_uint64_le(chunk, sample_count);
    if (ret != DSF_SUCCESS) {
        /* Try to restore position even on error */
        dsf_io_seek(chunk, saved_pos, DSF_SEEK_SET, &new_pos);
        return ret;
    }

    /* Restore original position */
    ret = dsf_io_seek(chunk, saved_pos, DSF_SEEK_SET, &new_pos);
    return ret;
}

/* =============================================================================
 * fmt Chunk Operations (52 bytes total)
 * ===========================================================================*/

int dsf_chunk_read_fmt(dsf_chunk_t *chunk, dsf_file_info_t *info) {
    uint32_t chunk_id;
    uint64_t chunk_size;
    uint32_t reserved;
    int ret;

    if (!chunk || !info) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Initialize info structure */
    memset(info, 0, sizeof(dsf_file_info_t));

    /* Read and validate chunk ID ('fmt ') */
    ret = dsf_io_read_uint32_le(chunk, &chunk_id);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (chunk_id != DSF_FMT_CHUNK_ID) {
        return DSF_ERROR_INVALID_CHUNK;
    }

    /* Read and validate chunk size (must be 52) */
    ret = dsf_io_read_uint64_le(chunk, &chunk_size);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (chunk_size != DSF_FMT_CHUNK_SIZE) {
        return DSF_ERROR_INVALID_CHUNK;
    }

    /* Read format version */
    ret = dsf_io_read_uint32_le(chunk, &info->format_version);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (info->format_version != DSF_FORMAT_VERSION) {
        return DSF_ERROR_INVALID_VERSION;
    }

    /* Read format ID */
    ret = dsf_io_read_uint32_le(chunk, &info->format_id);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (info->format_id != DSF_FORMAT_DSD_RAW) {
        return DSF_ERROR_UNSUPPORTED_COMPRESSION;
    }

    /* Read channel type */
    ret = dsf_io_read_uint32_le(chunk, &info->channel_type);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (!dsf_is_valid_channel_type(info->channel_type)) {
        return DSF_ERROR_INVALID_CHANNELS;
    }

    /* Read channel count */
    ret = dsf_io_read_uint32_le(chunk, &info->channel_count);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (info->channel_count < 1 || info->channel_count > DSF_MAX_CHANNELS) {
        return DSF_ERROR_INVALID_CHANNELS;
    }

    /* Read sampling frequency */
    ret = dsf_io_read_uint32_le(chunk, &info->sampling_frequency);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (!dsf_is_valid_sample_rate(info->sampling_frequency)) {
        return DSF_ERROR_INVALID_SAMPLE_RATE;
    }

    /* Read bits per sample */
    ret = dsf_io_read_uint32_le(chunk, &info->bits_per_sample);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (!dsf_is_valid_bits_per_sample(info->bits_per_sample)) {
        return DSF_ERROR_INVALID_BIT_DEPTH;
    }

    /* Read sample count */
    ret = dsf_io_read_uint64_le(chunk, &info->sample_count);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Read block size per channel */
    ret = dsf_io_read_uint32_le(chunk, &info->block_size_per_channel);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (info->block_size_per_channel != DSF_BLOCK_SIZE_PER_CHANNEL) {
        return DSF_ERROR_INVALID_BLOCK_SIZE;
    }

    /* Read reserved field (should be 0) */
    ret = dsf_io_read_uint32_le(chunk, &reserved);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Calculate derived information */
    info->audio_data_size = dsf_calculate_audio_data_size(
        info->channel_count,
        info->sample_count,
        info->bits_per_sample);

    info->duration_seconds = dsf_calculate_duration(
        info->sample_count,
        info->sampling_frequency);

    info->bit_rate = dsf_calculate_bit_rate(
        info->channel_count,
        info->sampling_frequency,
        info->bits_per_sample);

    return DSF_SUCCESS;
}

int dsf_chunk_write_fmt(dsf_chunk_t *chunk, const dsf_file_info_t *info) {
    int ret;

    if (!chunk || !info) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Validate format parameters */
    if (info->format_version != DSF_FORMAT_VERSION) {
        return DSF_ERROR_INVALID_VERSION;
    }
    if (info->format_id != DSF_FORMAT_DSD_RAW) {
        return DSF_ERROR_UNSUPPORTED_COMPRESSION;
    }
    if (!dsf_is_valid_channel_type(info->channel_type)) {
        return DSF_ERROR_INVALID_CHANNELS;
    }
    if (info->channel_count < 1 || info->channel_count > DSF_MAX_CHANNELS) {
        return DSF_ERROR_INVALID_CHANNELS;
    }
    if (!dsf_is_valid_sample_rate(info->sampling_frequency)) {
        return DSF_ERROR_INVALID_SAMPLE_RATE;
    }
    if (!dsf_is_valid_bits_per_sample(info->bits_per_sample)) {
        return DSF_ERROR_INVALID_BIT_DEPTH;
    }
    if (info->block_size_per_channel != DSF_BLOCK_SIZE_PER_CHANNEL) {
        return DSF_ERROR_INVALID_BLOCK_SIZE;
    }

    /* Write chunk ID ('fmt ') */
    ret = dsf_io_write_uint32_le(chunk, DSF_FMT_CHUNK_ID);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write chunk size (52) */
    ret = dsf_io_write_uint64_le(chunk, DSF_FMT_CHUNK_SIZE);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write format version */
    ret = dsf_io_write_uint32_le(chunk, info->format_version);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write format ID */
    ret = dsf_io_write_uint32_le(chunk, info->format_id);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write channel type */
    ret = dsf_io_write_uint32_le(chunk, info->channel_type);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write channel count */
    ret = dsf_io_write_uint32_le(chunk, info->channel_count);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write sampling frequency */
    ret = dsf_io_write_uint32_le(chunk, info->sampling_frequency);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write bits per sample */
    ret = dsf_io_write_uint32_le(chunk, info->bits_per_sample);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write sample count */
    ret = dsf_io_write_uint64_le(chunk, info->sample_count);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write block size per channel */
    ret = dsf_io_write_uint32_le(chunk, info->block_size_per_channel);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write reserved field (0) */
    ret = dsf_io_write_uint32_le(chunk, 0);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    return DSF_SUCCESS;
}

/* =============================================================================
 * data Chunk Operations
 * ===========================================================================*/

int dsf_chunk_read_data_header(dsf_chunk_t *chunk,
                                uint64_t *data_size,
                                uint64_t *data_offset) {
    uint32_t chunk_id;
    uint64_t chunk_size;
    int ret;

    if (!chunk || !data_size || !data_offset) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    *data_size = 0;
    *data_offset = 0;

    /* Read and validate chunk ID ('data') */
    ret = dsf_io_read_uint32_le(chunk, &chunk_id);
    if (ret != DSF_SUCCESS) {
        return ret;
    }
    if (chunk_id != DSF_DATA_CHUNK_ID) {
        return DSF_ERROR_INVALID_CHUNK;
    }

    /* Read chunk size (includes 12-byte header) */
    ret = dsf_io_read_uint64_le(chunk, &chunk_size);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Validate chunk size bounds */
    if (chunk_size < DSF_DATA_CHUNK_HEADER_SIZE) {
        return DSF_ERROR_INVALID_CHUNK;
    }

    /* Validate chunk size is not unreasonably large */
    if (chunk_size > DSF_MAX_REASONABLE_CHUNK_SIZE) {
        return DSF_ERROR_INVALID_CHUNK;
    }

    /* Data size = chunk size - header size (safe due to validation above) */
    if (dsf_uint64_sub_underflow(chunk_size, DSF_DATA_CHUNK_HEADER_SIZE, data_size)) {
        return DSF_ERROR_INVALID_CHUNK;
    }

    /* Get current position (start of audio data) */
    ret = dsf_io_get_position(chunk, data_offset);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    return DSF_SUCCESS;
}

int dsf_chunk_write_data_header(dsf_chunk_t *chunk,
                                 uint64_t data_size,
                                 uint64_t *data_offset) {
    uint64_t chunk_size;
    int ret;

    if (!chunk || !data_offset) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    *data_offset = 0;

    /* Calculate chunk size (header + data) with overflow check */
    if (dsf_uint64_add_overflow(DSF_DATA_CHUNK_HEADER_SIZE, data_size, &chunk_size)) {
        return DSF_ERROR_INVALID_CHUNK;
    }

    /* Validate chunk size is not unreasonably large */
    if (chunk_size > DSF_MAX_REASONABLE_CHUNK_SIZE) {
        return DSF_ERROR_INVALID_CHUNK;
    }

    /* Write chunk ID ('data') */
    ret = dsf_io_write_uint32_le(chunk, DSF_DATA_CHUNK_ID);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write chunk size */
    ret = dsf_io_write_uint64_le(chunk, chunk_size);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Get current position (start of audio data) */
    ret = dsf_io_get_position(chunk, data_offset);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    return DSF_SUCCESS;
}

int dsf_chunk_read_audio_data(dsf_chunk_t *chunk,
                               uint8_t *buffer,
                               size_t num_bytes,
                               size_t *bytes_read) {
    if (!chunk || !buffer || !bytes_read) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    return dsf_io_read_bytes(chunk, buffer, num_bytes, bytes_read);
}

int dsf_chunk_write_audio_data(dsf_chunk_t *chunk,
                                const uint8_t *buffer,
                                size_t num_bytes,
                                size_t *bytes_written) {
    if (!chunk || !buffer || !bytes_written) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    return dsf_io_write_bytes(chunk, buffer, num_bytes, bytes_written);
}

/* =============================================================================
 * Metadata Operations (ID3v2)
 * ===========================================================================*/

int dsf_chunk_read_metadata(dsf_chunk_t *chunk,
                             uint64_t metadata_offset,
                             uint64_t *metadata_size,
                             uint8_t **metadata_buffer) {
    uint64_t file_size;
    uint64_t size;
    uint8_t *buffer;
    size_t bytes_read;
    int ret;

    if (!chunk || !metadata_size || !metadata_buffer) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    *metadata_size = 0;
    *metadata_buffer = NULL;

    if (metadata_offset == 0) {
        return DSF_ERROR_NO_METADATA;
    }

    /* Get file size */
    ret = dsf_io_get_file_size(chunk, &file_size);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Validate metadata offset */
    if (metadata_offset >= file_size) {
        return DSF_ERROR_INVALID_METADATA;
    }

    /* Calculate metadata size with underflow protection */
    if (dsf_uint64_sub_underflow(file_size, metadata_offset, &size)) {
        return DSF_ERROR_INVALID_METADATA;
    }

    if (size == 0) {
        return DSF_ERROR_NO_METADATA;
    }

    /* Validate metadata size is reasonable */
    if (size > DSF_MAX_REASONABLE_METADATA_SIZE) {
        return DSF_ERROR_INVALID_METADATA;
    }

    /* Safe cast to size_t */
    size_t size_to_alloc;
    if (dsf_uint64_to_sizet(size, &size_to_alloc)) {
        return DSF_ERROR_INVALID_METADATA;
    }

    /* Allocate buffer */
    buffer = (uint8_t *)sa_malloc(size_to_alloc);
    if (!buffer) {
        return DSF_ERROR_OUT_OF_MEMORY;
    }

    /* Seek to metadata */
    ret = dsf_io_seek(chunk, metadata_offset, DSF_SEEK_SET, NULL);
    if (ret != DSF_SUCCESS) {
        sa_free(buffer);
        return ret;
    }

    /* Read metadata */
    ret = dsf_io_read_bytes(chunk, buffer, size_to_alloc, &bytes_read);
    if (ret != DSF_SUCCESS || bytes_read != size_to_alloc) {
        sa_free(buffer);
        return ret != DSF_SUCCESS ? ret : DSF_ERROR_READ;
    }

    *metadata_size = size;
    *metadata_buffer = buffer;
    return DSF_SUCCESS;
}

int dsf_chunk_write_metadata(dsf_chunk_t *chunk,
                              const uint8_t *metadata_buffer,
                              uint64_t metadata_size,
                              uint64_t *metadata_offset) {
    size_t bytes_written;
    size_t size_to_write;
    int ret;

    if (!chunk || !metadata_buffer || !metadata_offset) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (metadata_size == 0) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Validate metadata size is reasonable */
    if (metadata_size > DSF_MAX_REASONABLE_METADATA_SIZE) {
        return DSF_ERROR_INVALID_METADATA;
    }

    /* Safe cast to size_t */
    if (dsf_uint64_to_sizet(metadata_size, &size_to_write)) {
        return DSF_ERROR_INVALID_METADATA;
    }

    /* Get current position (metadata offset) */
    ret = dsf_io_get_position(chunk, metadata_offset);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Write metadata */
    ret = dsf_io_write_bytes(chunk, metadata_buffer, size_to_write, &bytes_written);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    if (bytes_written != size_to_write) {
        return DSF_ERROR_WRITE;
    }

    return DSF_SUCCESS;
}

/* =============================================================================
 * Utility Functions
 * ===========================================================================*/

int dsf_chunk_validate_file(dsf_chunk_t *chunk) {
    uint64_t file_size;
    uint64_t actual_file_size;
    uint64_t metadata_offset;
    dsf_file_info_t info;
    uint64_t data_size;
    uint64_t data_offset;
    uint64_t expected_min_size;
    uint64_t audio_end_offset;
    int ret;

    if (!chunk) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Seek to start of file */
    ret = dsf_io_seek(chunk, 0, DSF_SEEK_SET, NULL);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Get actual file size for validation */
    ret = dsf_io_get_file_size(chunk, &actual_file_size);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Validate minimum file size */
    if (actual_file_size < DSF_MIN_FILE_SIZE) {
        return DSF_ERROR_INVALID_FILE;
    }

    /* Read and validate DSD chunk */
    ret = dsf_chunk_read_dsd_header(chunk, &file_size, &metadata_offset);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Validate file size in header matches actual file size */
    if (file_size != actual_file_size) {
        /* Allow some tolerance, but reject obvious mismatches */
        if (file_size > actual_file_size || actual_file_size - file_size > 4096) {
            return DSF_ERROR_INVALID_FILE;
        }
    }

    /* Validate metadata offset if present */
    if (metadata_offset > 0) {
        if (metadata_offset >= actual_file_size) {
            return DSF_ERROR_INVALID_METADATA;
        }
        /* Metadata should be after all chunks */
        if (metadata_offset < DSF_MIN_FILE_SIZE) {
            return DSF_ERROR_INVALID_METADATA;
        }
    }

    /* Read and validate fmt chunk */
    ret = dsf_chunk_read_fmt(chunk, &info);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Read and validate data chunk */
    ret = dsf_chunk_read_data_header(chunk, &data_size, &data_offset);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    /* Validate that data offset is correct (should be at offset 92) */
    if (data_offset != (DSF_DSD_CHUNK_SIZE + DSF_FMT_CHUNK_SIZE + DSF_DATA_CHUNK_HEADER_SIZE)) {
        return DSF_ERROR_INVALID_FILE;
    }

    /* Validate audio data doesn't extend beyond file with overflow protection */
    if (dsf_uint64_add_overflow(data_offset, data_size, &audio_end_offset)) {
        return DSF_ERROR_INVALID_FILE;
    }

    if (audio_end_offset > actual_file_size) {
        return DSF_ERROR_INVALID_FILE;
    }

    /* Validate file size consistency - calculate expected minimum size */
    expected_min_size = data_offset + data_size;

    /* If metadata is present, validate it fits in file */
    if (metadata_offset > 0) {
        if (metadata_offset < audio_end_offset) {
            return DSF_ERROR_INVALID_FILE;
        }
        if (metadata_offset > actual_file_size) {
            return DSF_ERROR_INVALID_METADATA;
        }
    }

    if (file_size < expected_min_size) {
        return DSF_ERROR_INVALID_FILE;
    }

    /* Validate calculated audio data size matches what's in the header */
    uint64_t calculated_size = dsf_calculate_audio_data_size(
        info.channel_count,
        info.sample_count,
        info.bits_per_sample);

    if (calculated_size == 0) {
        /* Overflow detected in calculation */
        return DSF_ERROR_INVALID_FILE;
    }

    /* Allow some tolerance for rounding, but reject large mismatches */
    if (calculated_size > data_size) {
        if (calculated_size - data_size > DSF_BLOCK_SIZE_PER_CHANNEL * info.channel_count) {
            return DSF_ERROR_INVALID_FILE;
        }
    } else if (data_size > calculated_size) {
        if (data_size - calculated_size > DSF_BLOCK_SIZE_PER_CHANNEL * info.channel_count) {
            return DSF_ERROR_INVALID_FILE;
        }
    }

    return DSF_SUCCESS;
}

int dsf_chunk_read_id(dsf_chunk_t *chunk, uint32_t *chunk_id) {
    if (!chunk || !chunk_id) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    return dsf_io_read_uint32_le(chunk, chunk_id);
}

int dsf_chunk_write_id(dsf_chunk_t *chunk, uint32_t chunk_id) {
    if (!chunk) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    return dsf_io_write_uint32_le(chunk, chunk_id);
}
