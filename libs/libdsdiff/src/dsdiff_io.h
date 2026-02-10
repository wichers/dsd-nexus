/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSDIFF I/O abstraction layer
 * This module provides endian-aware file I/O operations for reading and
 * writing DSDIFF files. It handles:
 * - File open/close/seek operations
 * - Endian conversion (big-endian)
 * - Chunk ID operations
 * - String operations (Pascal strings, fixed-length strings)
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

#ifndef LIBDSDIFF_DSDIFF_IO_H
#define LIBDSDIFF_DSDIFF_IO_H

#include <libdsdiff/dsdiff.h>

#include "dsdiff_types.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * I/O Handle Structure
 * ===========================================================================*/

struct dsdiff_io_s {
    FILE *file;
    char filename[DSDIFF_MAX_STR_SIZE];
    dsdiff_file_mode_t mode;
};

/* =============================================================================
 * Types
 * ===========================================================================*/

/**
 * @brief Opaque I/O handle (internal use)
 */
typedef struct dsdiff_io_s dsdiff_io_t;

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
int dsdiff_io_open_write(dsdiff_io_t *io, const char *filename);

/**
 * @brief Open file for reading
 *
 * @param io Pointer to I/O handle (will be allocated)
 * @param filename File path
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_open_read(dsdiff_io_t *io, const char *filename);

/**
 * @brief Open file for modification (read/write metadata)
 *
 * @param io Pointer to I/O handle (will be allocated)
 * @param filename File path
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_open_modify(dsdiff_io_t *io, const char *filename);

/**
 * @brief Close file and free I/O handle
 *
 * @param io I/O handle
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_close(dsdiff_io_t *io);

/**
 * @brief Get filename
 *
 * @param io I/O handle
 * @param filename Buffer to receive filename
 * @param buffer_size Size of buffer
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_get_filename(dsdiff_io_t *io, char *filename, size_t buffer_size);

/**
 * @brief Check if file is open
 *
 * @param io I/O handle
 * @param is_open Pointer to receive result (1=open, 0=closed)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_is_open(dsdiff_io_t *io, int *is_open);

/* =============================================================================
 * Position Operations
 * ===========================================================================*/

/**
 * @brief Seek to position in file
 *
 * @param io I/O handle
 * @param offset Offset in bytes
 * @param origin Seek origin (DSDIFF_SEEK_SET, DSDIFF_SEEK_CUR, DSDIFF_SEEK_END)
 * @param new_pos Pointer to receive new position (may be NULL)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_seek(dsdiff_io_t *io, int64_t offset, dsdiff_seek_dir_t origin,
                   uint64_t *new_pos);

/**
 * @brief Get current file position
 *
 * @param io I/O handle
 * @param position Pointer to receive position
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_get_position(dsdiff_io_t *io, uint64_t *position);

/**
 * @brief Set file position
 *
 * @param io I/O handle
 * @param position New position
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_set_position(dsdiff_io_t *io, uint64_t position);

/**
 * @brief Get file size
 *
 * @param io I/O handle
 * @param size Pointer to receive file size
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_get_file_size(dsdiff_io_t *io, uint64_t *size);

/**
 * @brief Pre-allocate extra space for file
 *
 * @param io I/O handle
 * @param extra_bytes Number of extra bytes to allocate
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_preallocate(dsdiff_io_t *io, uint64_t extra_bytes);

/* =============================================================================
 * Chunk ID Operations
 * ===========================================================================*/

/**
 * @brief Read chunk ID (4 bytes, big-endian)
 *
 * @param io I/O handle
 * @param chunk_id Pointer to receive chunk ID
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_read_chunk_id(dsdiff_io_t *io, uint32_t *chunk_id);

/**
 * @brief Write chunk ID (4 bytes, big-endian)
 *
 * @param io I/O handle
 * @param chunk_id Chunk ID to write
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_write_chunk_id(dsdiff_io_t *io, uint32_t chunk_id);

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
int dsdiff_io_read_uint8(dsdiff_io_t *io, uint8_t *data);

/**
 * @brief Write 8-bit unsigned integer
 *
 * @param io I/O handle
 * @param data Data to write
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_write_uint8(dsdiff_io_t *io, uint8_t data);

/**
 * @brief Read 16-bit unsigned integer (big-endian)
 *
 * DSDIFF uses big-endian for most data.
 *
 * @param io I/O handle
 * @param data Pointer to receive data
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_read_uint16_be(dsdiff_io_t *io, uint16_t *data);

/**
 * @brief Write 16-bit unsigned integer (big-endian)
 *
 * @param io I/O handle
 * @param data Data to write
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_write_uint16_be(dsdiff_io_t *io, uint16_t data);

/**
 * @brief Read 32-bit unsigned integer (big-endian)
 *
 * @param io I/O handle
 * @param data Pointer to receive data
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_read_uint32_be(dsdiff_io_t *io, uint32_t *data);

/**
 * @brief Write 32-bit unsigned integer (big-endian)
 *
 * @param io I/O handle
 * @param data Data to write
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_write_uint32_be(dsdiff_io_t *io, uint32_t data);

/**
 * @brief Read 32-bit signed integer (big-endian)
 *
 * @param io I/O handle
 * @param data Pointer to receive data
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_read_int32_be(dsdiff_io_t *io, int32_t *data);

/**
 * @brief Write 32-bit signed integer (big-endian)
 *
 * @param io I/O handle
 * @param data Data to write
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_write_int32_be(dsdiff_io_t *io, int32_t data);

/**
 * @brief Read 64-bit unsigned integer (big-endian)
 *
 * @param io I/O handle
 * @param data Pointer to receive data
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_read_uint64_be(dsdiff_io_t *io, uint64_t *data);

/**
 * @brief Write 64-bit unsigned integer (big-endian)
 *
 * @param io I/O handle
 * @param data Data to write
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_write_uint64_be(dsdiff_io_t *io, uint64_t data);

/* =============================================================================
 * Padding Operations
 * ===========================================================================*/

/**
 * @brief Read padding byte (for odd-length chunks)
 *
 * DSDIFF chunks must be even-length. This reads the pad byte if present.
 *
 * @param io I/O handle
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_read_pad_byte(dsdiff_io_t *io);

/**
 * @brief Write padding byte (for odd-length chunks)
 *
 * @param io I/O handle
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_write_pad_byte(dsdiff_io_t *io);

/* =============================================================================
 * String Operations
 * ===========================================================================*/

/**
 * @brief Read Pascal string (length-prefixed string, 16-bit length)
 *
 * @param io I/O handle
 * @param length Pointer to receive string length
 * @param string Buffer to receive string
 * @param buffer_size Maximum buffer size
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_read_pstring(dsdiff_io_t *io, uint16_t *length,
                            char *string, size_t buffer_size);

/**
 * @brief Write Pascal string (length-prefixed string, 16-bit length)
 *
 * @param io I/O handle
 * @param length String length
 * @param string String to write
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_write_pstring(dsdiff_io_t *io, uint16_t length,
                             const char *string);

/**
 * @brief Read fixed-length string
 *
 * @param io I/O handle
 * @param length Number of characters to read
 * @param string Buffer to receive string (must be at least length+1 bytes)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_read_string(dsdiff_io_t *io, size_t length,
                           char *string);

/**
 * @brief Write fixed-length string
 *
 * @param io I/O handle
 * @param length Number of characters to write
 * @param string String to write
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_write_string(dsdiff_io_t *io, size_t length,
                            const char *string);

/* =============================================================================
 * Raw Byte Operations
 * ===========================================================================*/

/**
 * @brief Read raw bytes
 *
 * @param io I/O handle
 * @param buffer Buffer to receive data
 * @param byte_count Number of bytes to read
 * @param out_bytes_read Pointer to receive actual bytes read (may be NULL)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_read_bytes(dsdiff_io_t *io, uint8_t *buffer, size_t byte_count,
                         size_t *out_bytes_read);

/**
 * @brief Write raw bytes
 *
 * @param io I/O handle
 * @param buffer Buffer containing data to write
 * @param byte_count Number of bytes to write
 * @param out_bytes_written Pointer to receive actual bytes written (may be NULL)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_io_write_bytes(dsdiff_io_t *io, const uint8_t *buffer,
                          size_t byte_count, size_t *out_bytes_written);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDIFF_DSDIFF_IO_H */
