/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief ID3v2 tag parser for extracting metadata from DSF/DSDIFF files
 * Uses id3dev library to parse ID3v2.2/2.3/2.4 tags and extract frames
 * into metadata_tags_t for storage.
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

#ifndef LIBDSDPIPE_ID3_PARSER_H
#define LIBDSDPIPE_ID3_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <libdsdpipe/metadata_tags.h>
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Error Codes
 *============================================================================*/

#define ID3_PARSE_OK              0
#define ID3_PARSE_ERROR_INVALID  -1
#define ID3_PARSE_ERROR_MEMORY   -2
#define ID3_PARSE_ERROR_VERSION  -3
#define ID3_PARSE_ERROR_ENCODING -4

/*============================================================================
 * ID3 Version Info
 *============================================================================*/

/**
 * @brief ID3v2 version information
 */
typedef struct id3_version_s {
    uint8_t major;      /**< Major version (2, 3, or 4) */
    uint8_t revision;   /**< Revision number */
    uint8_t flags;      /**< Header flags */
    uint32_t size;      /**< Tag size (excluding header) */
} id3_version_t;

/*============================================================================
 * Parser Functions
 *============================================================================*/

/**
 * @brief Check if data contains a valid ID3v2 header
 *
 * Checks for "ID3" magic and valid version/flags.
 *
 * @param data Pointer to data
 * @param size Size of data (must be >= 10 for valid header)
 * @return 1 if valid ID3v2 header, 0 otherwise
 */
int id3_is_valid(const uint8_t *data, size_t size);

/**
 * @brief Get ID3v2 version information
 *
 * @param data Pointer to ID3v2 data
 * @param size Size of data
 * @param version Output version info
 * @return ID3_PARSE_OK on success, error code otherwise
 */
int id3_get_version(const uint8_t *data, size_t size, id3_version_t *version);

/**
 * @brief Parse ID3v2 tag into metadata tags
 *
 * Uses id3dev library to parse all frames. Text frames are stored with
 * their frame ID as key (e.g., "TIT2", "TPE1", "TALB").
 * TXXX frames are stored as "TXXX:{description}".
 *
 * @param data Pointer to ID3v2 data (starting with "ID3" header)
 * @param size Size of data
 * @param tags Output tags container (must be created by caller)
 * @return ID3_PARSE_OK on success, error code otherwise
 */
int id3_parse_to_tags(const uint8_t *data, size_t size, metadata_tags_t *tags);

/**
 * @brief Parse ID3v2 tag and populate dsdpipe_metadata_t
 *
 * Convenience function that parses ID3 and maps standard frames to
 * the corresponding dsdpipe_metadata_t fields:
 *   TIT2 -> track_title
 *   TPE1 -> track_performer
 *   TPE2 -> album_artist
 *   TALB -> album_title
 *   TCOM -> track_composer
 *   TCON -> genre
 *   TRCK -> track_number, track_total
 *   TPOS -> disc_number, disc_total
 *   TDRC/TYER -> year, month, day
 *   TSRC -> isrc
 *   TPUB -> album_publisher
 *   TCOP -> album_copyright
 *   TEXT -> track_songwriter
 *
 * All frames are also stored in the tags container.
 *
 * @param data Pointer to ID3v2 data
 * @param size Size of data
 * @param metadata Output metadata structure
 * @return ID3_PARSE_OK on success, error code otherwise
 */
struct dsdpipe_metadata_s;
int id3_parse_to_metadata(const uint8_t *data, size_t size,
                           struct dsdpipe_metadata_s *metadata);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Get human-readable frame name
 *
 * @param frame_id Four-character frame ID (e.g., "TIT2")
 * @return Human-readable name (e.g., "Title"), or frame_id if unknown
 */
const char *id3_frame_name(const char *frame_id);

/**
 * @brief Calculate total ID3v2 tag size from header
 *
 * Returns the complete tag size including the 10-byte header.
 * Useful for skipping ID3 tags or allocating buffers.
 *
 * @param data Pointer to ID3v2 data (at least 10 bytes)
 * @param size Size of data
 * @return Total tag size, or 0 if invalid header
 */
size_t id3_get_total_size(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDPIPE_ID3_PARSER_H */
