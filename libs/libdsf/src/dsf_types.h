/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSF library type definitions
 * This file contains all type definitions, enumerations, and structures
 * used by the DSF library. It is based on the DSF specification.
 * DSF File Structure:
 * - DSD Chunk (28 bytes) - File header
 * - fmt Chunk (52 bytes) - Format information
 * - data Chunk - Audio data
 * - (Optional) Metadata chunk (ID3v2)
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

#ifndef LIBDSF_DSF_TYPES_H
#define LIBDSF_DSF_TYPES_H

#include <libdsf/dsf.h>

#include <libsautil/bswap.h>

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Chunk FourCC Codes (Little-Endian)
 * ===========================================================================*/

/* DSF Chunk IDs (as they appear in file - little-endian) */
#define DSF_DSD_CHUNK_ID   MAKE_MARKER('D', 'S', 'D', ' ')  /* 0x20445344 */
#define DSF_FMT_CHUNK_ID   MAKE_MARKER('f', 'm', 't', ' ')  /* 0x20746D66 */
#define DSF_DATA_CHUNK_ID  MAKE_MARKER('d', 'a', 't', 'a')  /* 0x61746164 */

/* =============================================================================
 * Constants
 * ===========================================================================*/

/** DSF format version (always 1 for current specification) */
#define DSF_FORMAT_VERSION 1

/** Maximum string size for internal buffers */
#define DSF_MAX_STR_SIZE 4096

/** Maximum DSF data size */
#define DSF_MAX_DATA_SIZE (INT64_MAX - 100000)

/** Maximum reasonable chunk size (10 GB - prevents obvious malicious values) */
#define DSF_MAX_REASONABLE_CHUNK_SIZE ((uint64_t)10 * 1024 * 1024 * 1024)

/** Maximum reasonable metadata size (100 MB) */
#define DSF_MAX_REASONABLE_METADATA_SIZE ((uint64_t)100 * 1024 * 1024)

/** Minimum DSF file size (DSD chunk + fmt chunk + data chunk headers) */
#define DSF_MIN_FILE_SIZE (28 + 52 + 12)

/** DSD chunk size (fixed at 28 bytes) */
#define DSF_DSD_CHUNK_SIZE 28

/** fmt chunk size (fixed at 52 bytes) */
#define DSF_FMT_CHUNK_SIZE 52

/** Data chunk header size (12 bytes) */
#define DSF_DATA_CHUNK_HEADER_SIZE 12

/**
 * @brief Validate DSF chunk ID
 */
static inline int dsf_is_valid_chunk_id(uint32_t chunk_id) {
    return (chunk_id == DSF_DSD_CHUNK_ID ||
            chunk_id == DSF_FMT_CHUNK_ID ||
            chunk_id == DSF_DATA_CHUNK_ID);
}

#ifdef __cplusplus
}
#endif

#endif /* LIBDSF_DSF_TYPES_H */
