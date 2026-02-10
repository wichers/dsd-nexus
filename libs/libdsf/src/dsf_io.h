/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSF I/O abstraction layer
 * This module provides endian-aware file I/O operations for reading and
 * writing DSF files. It handles:
 * - File open/close/seek operations
 * - Endian conversion (little-endian)
 * - Chunk ID operations
 * - Byte-level I/O with buffering
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

#ifndef LIBDSF_DSF_IO_H
#define LIBDSF_DSF_IO_H

#include <libdsf/dsf.h>

#include "dsf_types.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * I/O Handle Structure
 * ===========================================================================*/

struct dsf_io_s {
    FILE *file;
    char filename[DSF_MAX_STR_SIZE];
    dsf_file_mode_t mode;
};

/* =============================================================================
 * Types
 * ===========================================================================*/

/**
 * @brief Opaque I/O handle (internal use)
 */
typedef struct dsf_io_s dsf_io_t;

/* =============================================================================
 * File Operations
 * ===========================================================================*/

/**
 * @brief Open file for writing
 *
 * @param io Pointer to I/O handle (will be allocated)
 * @param filename File path
 * @return 0 on success, negative error code on failure
 */
int dsf_io_open_write(dsf_io_t *io, const char *filename);

/**
 * @brief Open file for reading
 *
 * @param io Pointer to I/O handle (will be allocated)
 * @param filename File path
 * @return 0 on success, negative error code on failure
 */
int dsf_io_open_read(dsf_io_t *io, const char *filename);

/**
 * @brief Open file for modification (read/write metadata)
 *
 * @param io Pointer to I/O handle (will be allocated)
 * @param filename File path
 * @return 0 on success, negative error code on failure
 */
int dsf_io_open_modify(dsf_io_t *io, const char *filename);

/**
 * @brief Close file and free I/O handle
 *
 * @param io I/O handle
 * @return 0 on success, negative error code on failure
 */
int dsf_io_close(dsf_io_t *io);

/**
 * @brief Close and delete file
 *
 * @param io I/O handle
 * @return 0 on success, negative error code on failure
 */
int dsf_io_remove_file(dsf_io_t *io);

/**
 * @brief Get filename
 *
 * @param io I/O handle
 * @param filename Buffer to receive filename
 * @param buffer_size Size of buffer
 * @return 0 on success, negative error code on failure
 */
int dsf_io_get_filename(dsf_io_t *io, char *filename, size_t buffer_size);

/**
 * @brief Check if file is open
 *
 * @param io I/O handle
 * @param is_open Pointer to receive result (1=open, 0=closed)
 * @return 0 on success, negative error code on failure
 */
int dsf_io_is_file_open(dsf_io_t *io, int *is_open);

/* =============================================================================
 * Position Operations
 * ===========================================================================*/

/**
 * @brief Seek to position in file
 *
 * @param io I/O handle
 * @param offset Offset in bytes
 * @param origin Seek origin (DSF_SEEK_SET, DSF_SEEK_CUR, DSF_SEEK_END)
 * @param new_pos Pointer to receive new position (may be NULL)
 * @return 0 on success, negative error code on failure
 */
int dsf_io_seek(dsf_io_t *io, int64_t offset, dsf_seek_dir_t origin,
                   uint64_t *new_pos);

/**
 * @brief Get current file position
 *
 * @param io I/O handle
 * @param position Pointer to receive position
 * @return 0 on success, negative error code on failure
 */
int dsf_io_get_position(dsf_io_t *io, uint64_t *position);

/**
 * @brief Set file position
 *
 * @param io I/O handle
 * @param position New position
 * @return 0 on success, negative error code on failure
 */
int dsf_io_set_position(dsf_io_t *io, uint64_t position);

/**
 * @brief Get file size
 *
 * @param io I/O handle
 * @param size Pointer to receive file size
 * @return 0 on success, negative error code on failure
 */
int dsf_io_get_file_size(dsf_io_t *io, uint64_t *size);

/**
 * @brief Claim extra space for file (pre-allocate)
 *
 * @param io I/O handle
 * @param extra_bytes Number of extra bytes to allocate
 * @return 0 on success, negative error code on failure
 */
int dsf_io_claim_extra_size(dsf_io_t *io, uint64_t extra_bytes);

/* =============================================================================
 * Chunk ID Operations
 * ===========================================================================*/

/**
 * @brief Read chunk ID (4 bytes, little-endian)
 *
 * @param io I/O handle
 * @param chunk_id Pointer to receive chunk ID
 * @return 0 on success, negative error code on failure
 */
int dsf_io_read_chunk_id(dsf_io_t *io, uint32_t *chunk_id);

/**
 * @brief Write chunk ID (4 bytes, little-endian)
 *
 * @param io I/O handle
 * @param chunk_id Chunk ID to write
 * @return 0 on success, negative error code on failure
 */
int dsf_io_write_chunk_id(dsf_io_t *io, uint32_t chunk_id);

/* =============================================================================
 * Integer I/O Operations (with endian conversion)
 * ===========================================================================*/

/**
 * @brief Read 8-bit unsigned integer
 *
 * @param io I/O handle
 * @param data Pointer to receive data
 * @return 0 on success, negative error code on failure
 */
int dsf_io_read_uint8(dsf_io_t *io, uint8_t *data);

/**
 * @brief Write 8-bit unsigned integer
 *
 * @param io I/O handle
 * @param data Data to write
 * @return 0 on success, negative error code on failure
 */
int dsf_io_write_uint8(dsf_io_t *io, uint8_t data);

/**
 * @brief Read 16-bit unsigned integer (little-endian)
 *
 * DSF uses little-endian for most data.
 *
 * @param io I/O handle
 * @param data Pointer to receive data
 * @return 0 on success, negative error code on failure
 */
int dsf_io_read_uint16_le(dsf_io_t *io, uint16_t *data);

/**
 * @brief Write 16-bit unsigned integer (little-endian)
 *
 * @param io I/O handle
 * @param data Data to write
 * @return 0 on success, negative error code on failure
 */
int dsf_io_write_uint16_le(dsf_io_t *io, uint16_t data);

/**
 * @brief Read 32-bit unsigned integer (little-endian)
 *
 * @param io I/O handle
 * @param data Pointer to receive data
 * @return 0 on success, negative error code on failure
 */
int dsf_io_read_uint32_le(dsf_io_t *io, uint32_t *data);

/**
 * @brief Write 32-bit unsigned integer (little-endian)
 *
 * @param io I/O handle
 * @param data Data to write
 * @return 0 on success, negative error code on failure
 */
int dsf_io_write_uint32_le(dsf_io_t *io, uint32_t data);

/**
 * @brief Read 32-bit signed integer (little-endian)
 *
 * @param io I/O handle
 * @param data Pointer to receive data
 * @return 0 on success, negative error code on failure
 */
int dsf_io_read_int32_le(dsf_io_t *io, int32_t *data);

/**
 * @brief Write 32-bit signed integer (little-endian)
 *
 * @param io I/O handle
 * @param data Data to write
 * @return 0 on success, negative error code on failure
 */
int dsf_io_write_int32_le(dsf_io_t *io, int32_t data);

/**
 * @brief Read 64-bit unsigned integer (little-endian)
 *
 * @param io I/O handle
 * @param data Pointer to receive data
 * @return 0 on success, negative error code on failure
 */
int dsf_io_read_uint64_le(dsf_io_t *io, uint64_t *data);

/**
 * @brief Write 64-bit unsigned integer (little-endian)
 *
 * @param io I/O handle
 * @param data Data to write
 * @return 0 on success, negative error code on failure
 */
int dsf_io_write_uint64_le(dsf_io_t *io, uint64_t data);

/* =============================================================================
 * Raw Byte Operations
 * ===========================================================================*/

/**
 * @brief Read raw bytes
 *
 * @param io I/O handle
 * @param buffer Buffer to receive data
 * @param num_bytes Number of bytes to read
 * @param bytes_read Pointer to receive actual bytes read (may be NULL)
 * @return 0 on success, negative error code on failure
 */
int dsf_io_read_bytes(dsf_io_t *io, uint8_t *buffer, size_t num_bytes,
                          size_t *bytes_read);

/**
 * @brief Write raw bytes
 *
 * @param io I/O handle
 * @param buffer Buffer containing data to write
 * @param num_bytes Number of bytes to write
 * @param bytes_written Pointer to receive actual bytes written (may be NULL)
 * @return 0 on success, negative error code on failure
 */
int dsf_io_write_bytes(dsf_io_t *io, const uint8_t *buffer,
                           size_t num_bytes, size_t *bytes_written);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSF_DSF_IO_H */
