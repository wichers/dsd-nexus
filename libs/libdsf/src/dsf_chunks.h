/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSF chunk I/O operations
 * This module handles reading and writing of DSF file chunks.
 * DSF files consist of three main chunks in a fixed order:
 * DSF File Structure (fixed order):
 * 1. DSD Chunk (28 bytes) - File header with file size and metadata pointer
 * 2. fmt Chunk (52 bytes) - Format information (sample rate, channels, etc.)
 * 3. data Chunk - Audio data (12-byte header + DSD audio data)
 * 4. (Optional) ID3v2 metadata chunk at end of file
 * Key differences from DSDIFF:
 * - DSF uses little-endian byte order (DSDIFF uses big-endian)
 * - DSF has a simpler, fixed structure (no hierarchical chunks)
 * - DSF audio data is interleaved in 4096-byte blocks per channel
 * References:
 * - DSF_file_format_specification_E.pdf
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

#ifndef LIBDSF_DSF_CHUNKS_H
#define LIBDSF_DSF_CHUNKS_H

#include <libdsf/dsf.h>

#include "dsf_types.h"
#include "dsf_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque chunk file handle (internal use)
 */
typedef dsf_io_t dsf_chunk_t;

/* =============================================================================
 * Chunk File Operations
 * ===========================================================================*/

/**
 * @brief Close chunk file
 */
int dsf_chunk_file_close(dsf_chunk_t *chunk);

/**
 * @brief Get current file position
 */
int dsf_chunk_file_get_position(dsf_chunk_t *chunk, uint64_t *position);

/**
 * @brief Open chunk file for modification
 */
int dsf_chunk_file_open_modify(dsf_chunk_t **chunk, const char *filename);

/**
 * @brief Open chunk file for reading
 */
int dsf_chunk_file_open_read(dsf_chunk_t **chunk, const char *filename);

/**
 * @brief Open chunk file for writing
 */
int dsf_chunk_file_open_write(dsf_chunk_t **chunk, const char *filename);

/**
 * @brief Read bytes from chunk file
 */
int dsf_chunk_file_read_bytes(dsf_chunk_t *chunk, uint8_t *buffer,
                               size_t num_bytes, size_t *bytes_read);

/**
 * @brief Seek in chunk file
 */
int dsf_chunk_file_seek(dsf_chunk_t *chunk, int64_t offset,
                        dsf_seek_dir_t origin, uint64_t *new_pos);

/**
 * @brief Write bytes to chunk file
 */
int dsf_chunk_file_write_bytes(dsf_chunk_t *chunk, const uint8_t *buffer,
                                size_t num_bytes, size_t *bytes_written);

/**
 * @brief Get file size
 */
int dsf_chunk_file_get_size(dsf_chunk_t *chunk, uint64_t *size);

/**
 * @brief Get filename
 */
int dsf_chunk_file_get_filename(dsf_chunk_t *chunk, char *filename,
                                 size_t buffer_size);

/**
 * @brief Flush pending writes
 *
 * Ensures all buffered writes are committed to storage.
 * Should be called before seeking backwards to update headers.
 */
int dsf_chunk_file_flush(dsf_chunk_t *chunk);

/* =============================================================================
 * DSD Chunk Operations (28 bytes total)
 * ===========================================================================*/

/**
 * @brief Read DSD chunk header
 *
 * The DSD chunk is the file header and must be the first chunk.
 * Structure (all little-endian):
 *   - Bytes 0-3:   Chunk ID ('DSD ')
 *   - Bytes 4-11:  Chunk size (28)
 *   - Bytes 12-19: Total file size
 *   - Bytes 20-27: Metadata pointer (0 if no metadata)
 *
 * @param chunk Chunk file handle
 * @param file_size Pointer to receive total file size
 * @param metadata_offset Pointer to receive metadata offset (0 if none)
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_read_dsd_header(dsf_chunk_t *chunk,
                               uint64_t *file_size,
                               uint64_t *metadata_offset);

/**
 * @brief Write DSD chunk header
 *
 * @param chunk Chunk file handle
 * @param file_size Total file size (or 0 to update later)
 * @param metadata_offset Metadata offset (or 0 if no metadata)
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_write_dsd_header(dsf_chunk_t *chunk,
                                uint64_t file_size,
                                uint64_t metadata_offset);

/**
 * @brief Update DSD chunk file size field
 *
 * Seeks to file size field in DSD chunk and updates it.
 *
 * @param chunk Chunk file handle
 * @param file_size Total file size
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_update_file_size(dsf_chunk_t *chunk, uint64_t file_size);

/**
 * @brief Update DSD chunk metadata offset field
 *
 * Seeks to metadata offset field in DSD chunk and updates it.
 *
 * @param chunk Chunk file handle
 * @param metadata_offset Metadata offset (or 0 if no metadata)
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_update_metadata_offset(dsf_chunk_t *chunk, uint64_t metadata_offset);

/**
 * @brief Update data chunk size field
 *
 * Seeks to chunk size field in data chunk and updates it.
 *
 * @param chunk Chunk file handle
 * @param data_size Audio data size in bytes
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_update_data_size(dsf_chunk_t *chunk, uint64_t data_size);

/**
 * @brief Update fmt chunk sample count field
 *
 * Seeks to sample count field in fmt chunk and updates it.
 *
 * @param chunk Chunk file handle
 * @param sample_count Number of samples per channel
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_update_sample_count(dsf_chunk_t *chunk, uint64_t sample_count);

/* =============================================================================
 * fmt Chunk Operations (52 bytes total)
 * ===========================================================================*/

/**
 * @brief Read fmt chunk
 *
 * The fmt chunk contains format information and must follow the DSD chunk.
 * Structure (all little-endian):
 *   - Bytes 0-3:   Chunk ID ('fmt ')
 *   - Bytes 4-11:  Chunk size (52)
 *   - Bytes 12-15: Format version (1)
 *   - Bytes 16-19: Format ID (0 = DSD)
 *   - Bytes 20-23: Channel type (1-7)
 *   - Bytes 24-27: Channel num (1-7)
 *   - Bytes 28-31: Sampling frequency (Hz)
 *   - Bytes 32-35: Bits per sample (1 or 8)
 *   - Bytes 36-43: Sample count
 *   - Bytes 44-47: Block size per channel (4096)
 *   - Bytes 48-51: Reserved (0)
 *
 * @param chunk Chunk file handle
 * @param info Pointer to receive format information
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_read_fmt(dsf_chunk_t *chunk, dsf_file_info_t *info);

/**
 * @brief Write fmt chunk
 *
 * @param chunk Chunk file handle
 * @param info Format information to write
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_write_fmt(dsf_chunk_t *chunk, const dsf_file_info_t *info);

/* =============================================================================
 * data Chunk Operations
 * ===========================================================================*/

/**
 * @brief Read data chunk header
 *
 * The data chunk contains DSD audio data and must follow the fmt chunk.
 * Structure (all little-endian):
 *   - Bytes 0-3:  Chunk ID ('data')
 *   - Bytes 4-11: Chunk size (12 + data size)
 *   - Followed by DSD audio data
 *
 * Audio data format:
 * - Interleaved in blocks of 4096 bytes per channel
 * - Block order: Ch1[4096], Ch2[4096], ..., ChN[4096], repeat...
 * - Within each block, DSD samples are LSB first
 *
 * @param chunk Chunk file handle
 * @param data_size Pointer to receive audio data size in bytes
 * @param data_offset Pointer to receive offset to audio data
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_read_data_header(dsf_chunk_t *chunk,
                                uint64_t *data_size,
                                uint64_t *data_offset);

/**
 * @brief Write data chunk header
 *
 * @param chunk Chunk file handle
 * @param data_size Audio data size (or 0 to update later)
 * @param data_offset Pointer to receive offset to audio data
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_write_data_header(dsf_chunk_t *chunk,
                                 uint64_t data_size,
                                 uint64_t *data_offset);

/**
 * @brief Read audio data
 *
 * @param chunk Chunk file handle
 * @param buffer Buffer to receive audio data
 * @param num_bytes Number of bytes to read
 * @param bytes_read Pointer to receive actual bytes read
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_read_audio_data(dsf_chunk_t *chunk,
                               uint8_t *buffer,
                               size_t num_bytes,
                               size_t *bytes_read);

/**
 * @brief Write audio data
 *
 * @param chunk Chunk file handle
 * @param buffer Buffer containing audio data
 * @param num_bytes Number of bytes to write
 * @param bytes_written Pointer to receive actual bytes written
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_write_audio_data(dsf_chunk_t *chunk,
                                const uint8_t *buffer,
                                size_t num_bytes,
                                size_t *bytes_written);

/* =============================================================================
 * Metadata Operations (ID3v2)
 * ===========================================================================*/

/**
 * @brief Read metadata chunk (ID3v2)
 *
 * The metadata chunk is optional and appears at the end of the file.
 * The offset is stored in the DSD chunk's metadata pointer field.
 *
 * @param chunk Chunk file handle
 * @param metadata_offset Offset to metadata chunk
 * @param metadata_size Pointer to receive metadata size
 * @param metadata_buffer Pointer to receive allocated metadata buffer
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_read_metadata(dsf_chunk_t *chunk,
                             uint64_t metadata_offset,
                             uint64_t *metadata_size,
                             uint8_t **metadata_buffer);

/**
 * @brief Write metadata chunk (ID3v2)
 *
 * @param chunk Chunk file handle
 * @param metadata_buffer Metadata buffer
 * @param metadata_size Metadata size
 * @param metadata_offset Pointer to receive metadata offset
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_write_metadata(dsf_chunk_t *chunk,
                              const uint8_t *metadata_buffer,
                              uint64_t metadata_size,
                              uint64_t *metadata_offset);

/* =============================================================================
 * Utility Functions
 * ===========================================================================*/

/**
 * @brief Validate DSF file structure
 *
 * Checks that the file has valid DSD, fmt, and data chunks in order.
 *
 * @param chunk Chunk file handle
 * @return 0 if valid, negative error code if invalid
 */
int dsf_chunk_validate_file(dsf_chunk_t *chunk);

/**
 * @brief Read chunk ID at current position
 *
 * @param chunk Chunk file handle
 * @param chunk_id Pointer to receive chunk ID
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_read_id(dsf_chunk_t *chunk, uint32_t *chunk_id);

/**
 * @brief Write chunk ID
 *
 * @param chunk Chunk file handle
 * @param chunk_id Chunk ID to write
 * @return 0 on success, negative error code on failure
 */
int dsf_chunk_write_id(dsf_chunk_t *chunk, uint32_t chunk_id);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSF_DSF_CHUNKS_H */
