/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSDIFF library type definitions
 * This file contains all type definitions, enumerations, and structures
 * used by the DSDIFF library. It is based on the DSDIFF 1.5 specification.
 * References:
 * - DSDIFF_1.5_file_format_specification.pdf
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

#ifndef LIBDSDIFF_DSDIFF_TYPES_H
#define LIBDSDIFF_DSDIFF_TYPES_H

#include <libsautil/bswap.h>

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Chunk FourCC Codes (Big-Endian)
 * ===========================================================================*/

#define DSDIFF_CHUNK_LIST(V)                                      \
  V(ABSS, 'A', 'B', 'S', 'S', "Absolute Start Time")              \
  V(CHNL, 'C', 'H', 'N', 'L', "Channels")                         \
  V(CMPR, 'C', 'M', 'P', 'R', "Compression Type")                 \
  V(COMT, 'C', 'O', 'M', 'T', "Comment (alternate)")              \
  V(DIAR, 'D', 'I', 'A', 'R', "Artist")                           \
  V(DIIN, 'D', 'I', 'I', 'N', "Edited Master Information")        \
  V(DITI, 'D', 'I', 'T', 'I', "Title")                            \
  V(DSD,  'D', 'S', 'D', ' ', "DSD Sound Data")                   \
  V(DST,  'D', 'S', 'T', ' ', "DST Sound Data")                   \
  V(DSTC, 'D', 'S', 'T', 'C', "DST Frame CRC")                    \
  V(DSTF, 'D', 'S', 'T', 'F', "DST Frame Data")                   \
  V(DSTI, 'D', 'S', 'T', 'I', "DST Sound Index")                  \
  V(EMID, 'E', 'M', 'I', 'D', "Edited Master ID")                 \
  V(FRM8, 'F', 'R', 'M', '8', "Form DSD Chunk")                   \
  V(FRTE, 'F', 'R', 'T', 'E', "DST Frame Information")            \
  V(FS,   'F', 'S', ' ', ' ', "Sample Rate")                      \
  V(FVER, 'F', 'V', 'E', 'R', "Format Version (alternate)")       \
  V(ID3,  'I', 'D', '3', ' ', "ID3 Chunk (not in specifications)")\
  V(LSCO, 'L', 'S', 'C', 'O', "Loudspeaker Configuration")        \
  V(MANF, 'M', 'A', 'N', 'F', "Manufacturer Chunk")               \
  V(MARK, 'M', 'A', 'R', 'K', "DSD Marker")                       \
  V(PROP, 'P', 'R', 'O', 'P', "Property Chunk")                   \
  V(SND,  'S', 'N', 'D', ' ', "Sound Data (generic)")

enum {
#define AS_ENUM(name, a, b, c, d, description) name##_FOURCC = MAKE_MARKER(a, b, c, d),
  DSDIFF_CHUNK_LIST(AS_ENUM)
#undef AS_ENUM
};

#define DSDIFF_CHANNEL_LIST(X)       \
  X(SLFT, 'S', 'L', 'F', 'T', 1000, "stereo left")                 \
  X(SRGT, 'S', 'R', 'G', 'T', 1001, "stereo right") \
  X(MLFT, 'M', 'L', 'F', 'T', 1002, "multi-channel left") \
  X(MRGT, 'M', 'R', 'G', 'T', 1003, "multi-channel right") \
  X(LS,   'L', 'S', ' ', ' ', 1004, "multi-channel left surround") \
  X(RS,   'R', 'S', ' ', ' ', 1005, "multi-channel right surround") \
  X(C,    'C', ' ', ' ', ' ', 1006, "multi-channel center")                      \
  X(LFE,  'L', 'F', 'E', ' ', 1007, "multi-channel low frequency enhancement")

enum {
#define AS_ENUM(name, a, b, c, d, id, description) \
  name##_FOURCC = MAKE_MARKER(a, b, c, d),
  DSDIFF_CHANNEL_LIST(AS_ENUM)
#undef AS_ENUM
};

/**
 * @brief Channel identifiers
 *
 * Defines standard channel identification for DSDIFF files.
 * See DSDIFF specification section 3.2.2.
 */
typedef enum {
    DSDIFF_CHAN_C000 = 0,      /**< Generic channel 0 */
    DSDIFF_CHAN_C999 = 999,    /**< Generic channel 999 */
#define AS_ENUM(name, a, b, c, d, id, desc) DSDIFF_CHAN_##name = id,
    DSDIFF_CHANNEL_LIST(AS_ENUM)
#undef AS_ENUM
    DSDIFF_CHAN_INVALID = 9999 /**< Invalid channel */
} dsdiff_channel_id_t;

/* =============================================================================
 * Constants
 * ===========================================================================*/

/** Maximum string size for internal buffers */
#define DSDIFF_MAX_STR_SIZE 4096

/** Maximum DSDIFF data size */
#define DSDIFF_MAX_DATA_SIZE (INT64_MAX - 100000)

/** DSDIFF format version constants */
#define DSDIFF_FILE_VERSION_15  0x01050000  /**< DSDIFF v1.5 (latest) */

/* =============================================================================
 * Enumerations
 * ===========================================================================*/

typedef enum {
    DSDIFF_FILE_DSD        = 0,  /**< Uncompressed DSD */
    DSDIFF_FILE_DST        = 1,  /**< DST compressed */
    DSDIFF_FILE_UNKNOWN    = 2   /**< Unknown format */
} dsdiff_file_type_t;

/**
 * @brief Chunk types (internal use)
 */
typedef enum {
#define AS_ENUM(name, a, b, c, d, desc) DSDIFF_CHUNK_##name,
  DSDIFF_CHUNK_LIST(AS_ENUM)
#undef AS_ENUM
  DSDIFF_CHUNK_MAX, /**< Maximum chunk type */
  DSDIFF_CHUNK_UNKNOWN  /**< Unknown chunk type */
} dsdiff_chunk_type_t;

/**
 * @brief Marker sort type
 */
typedef enum {
    DSDIFF_MARKER_SORT_TIMESTAMP = 0,  /**< Sort by timestamp */
} dsdiff_marker_sort_t;

/* =============================================================================
 * Structures
 * ===========================================================================*/

/**
 * @brief DST frame index entry
 *
 * Index for random access to DST frames.
 */
typedef struct {
    uint64_t offset;  /**< Offset in file (bytes) from start of DST data */
    uint32_t length;  /**< Length of frame in bytes */
} dsdiff_index_t;

/* =============================================================================
 * Opaque Handle Types
 * ===========================================================================*/

/**
 * @brief Opaque DSDIFF file handle
 *
 * Internal structure is hidden from API users.
 * Create with dsdiff_create() or dsdiff_open().
 * Destroy with dsdiff_close().
 */
typedef struct dsdiff_s dsdiff_t;

/* =============================================================================
 * Internal Channel ID Functions
 * ===========================================================================*/

/**
 * @brief Get channel IDs (internal)
 *
 * Retrieves the channel ID array. The caller must allocate the array
 * with size equal to the number of channels.
 *
 * @param handle File handle
 * @param channel_ids Array to receive channel IDs (size = channel_count)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_get_channel_ids(dsdiff_t *handle, dsdiff_channel_id_t *channel_ids);

/**
 * @brief Set channel IDs (internal)
 *
 * Sets or modifies the channel ID array. Used internally by dsdiff_create()
 * and for modifying channel IDs on existing files.
 *
 * @param handle File handle (must be in write or modify mode)
 * @param channel_ids Array of channel IDs to set
 * @param channel_count Number of channels
 * @return 0 on success, negative error code on failure
 */
int dsdiff_set_channel_ids(dsdiff_t *handle, const dsdiff_channel_id_t *channel_ids, uint16_t channel_count);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDIFF_DSDIFF_TYPES_H */
