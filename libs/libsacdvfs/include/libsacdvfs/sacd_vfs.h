/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD Virtual Filesystem - Header
 * This module provides a virtual filesystem layer that presents SACD ISO
 * contents as a directory of DSF files. It performs on-the-fly transformation
 * from SACD DSD/DST format to Sony DSF format.
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

#ifndef LIBSACDVFS_SACD_VFS_H
#define LIBSACDVFS_SACD_VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <libsacdvfs/sacdvfs_export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Constants
 * ===========================================================================*/

#define SACD_VFS_MAX_PATH           512
#define SACD_VFS_MAX_FILENAME       256
#define SACD_VFS_MAX_TRACKS         MAX_TRACK_COUNT

/* DSF file structure constants */
#define DSF_DSD_CHUNK_SIZE          28
#define DSF_FMT_CHUNK_SIZE          52
#define DSF_DATA_CHUNK_HEADER_SIZE  12
#define DSF_AUDIO_DATA_OFFSET       (DSF_DSD_CHUNK_SIZE + DSF_FMT_CHUNK_SIZE)
#define DSF_BLOCK_SIZE_PER_CHANNEL  4096

/*
 * SACD frame size constants - use definitions from sacd_specification.h:
 *   SACD_FRAME_SIZE_64     = 4704 bytes per channel (588 samples * 64 bits / 8)
 *   MAX_CHANNEL_COUNT      = 6
 *   SACD_MAX_DSD_SIZE      = 28224 bytes (4704 * 6)
 *   SACD_FRAMES_PER_SEC    = 75
 *   SACD_SAMPLES_PER_FRAME = 588
 *   SACD_SAMPLING_FREQUENCY = 2822400
 */

/* DST look-ahead buffer (25 seconds worth of frames) */
#define DST_LOOKAHEAD_FRAMES        (25 * SACD_FRAMES_PER_SEC)

/* =============================================================================
 * Error Codes
 * ===========================================================================*/

typedef enum {
    SACD_VFS_OK = 0,
    SACD_VFS_ERROR_INVALID_PARAMETER = -1,
    SACD_VFS_ERROR_NOT_FOUND = -2,
    SACD_VFS_ERROR_IO = -3,
    SACD_VFS_ERROR_MEMORY = -4,
    SACD_VFS_ERROR_NOT_OPEN = -5,
    SACD_VFS_ERROR_SEEK = -6,
    SACD_VFS_ERROR_READ = -7,
    SACD_VFS_ERROR_FORMAT = -8,
    SACD_VFS_ERROR_DST_DECODE = -9,
    SACD_VFS_ERROR_EOF = -10
} sacd_vfs_error_t;

/* =============================================================================
 * Types and Structures
 * ===========================================================================*/

/** Channel/area type */
typedef enum {
    SACD_VFS_AREA_STEREO = 0,
    SACD_VFS_AREA_MULTICHANNEL = 1,
    SACD_VFS_AREA_UNKNOWN = 2
} sacd_vfs_area_t;

/** Entry type in virtual directory */
typedef enum {
    SACD_VFS_ENTRY_DIRECTORY = 0,
    SACD_VFS_ENTRY_FILE = 1
} sacd_vfs_entry_type_t;

/** Frame format (matches SACD specification) */
typedef enum {
    SACD_VFS_FRAME_DST = 0,
    SACD_VFS_FRAME_DSD_3_IN_14 = 2,
    SACD_VFS_FRAME_DSD_3_IN_16 = 3
} sacd_vfs_frame_format_t;

/** Virtual directory entry */
typedef struct {
    char name[SACD_VFS_MAX_FILENAME];
    sacd_vfs_entry_type_t type;
    uint64_t size;              /* File size in bytes (0 for directories) */
    uint8_t track_num;          /* Track number (1-based, 0 for directories) */
    sacd_vfs_area_t area;       /* Area type */
} sacd_vfs_entry_t;

/** Virtual file info */
typedef struct {
    uint64_t total_size;        /* Total virtual file size */
    uint64_t header_size;       /* DSF header size (DSD + fmt chunks) */
    uint64_t audio_data_size;   /* Audio data size */
    uint64_t metadata_offset;   /* ID3 metadata offset (0 if none) */
    uint64_t metadata_size;     /* ID3 metadata size */
    uint32_t channel_count;     /* Number of audio channels */
    uint32_t sample_rate;       /* Sample rate in Hz */
    uint64_t sample_count;      /* Total samples per channel */
    double duration_seconds;    /* Track duration */
    sacd_vfs_frame_format_t frame_format;
} sacd_vfs_file_info_t;

/** Opaque VFS context handle */
typedef struct sacd_vfs_ctx sacd_vfs_ctx_t;

/** Opaque virtual file handle */
typedef struct sacd_vfs_file sacd_vfs_file_t;

/** Forward declaration for thread pool (from libsautil/sa_tpool.h) */
typedef struct sa_tpool sa_tpool;

/** Directory listing callback */
typedef int (*sacd_vfs_readdir_callback_t)(const sacd_vfs_entry_t *entry, void *userdata);

/* =============================================================================
 * VFS Context Management
 * ===========================================================================*/

/**
 * Create a new VFS context.
 *
 * @return New context or NULL on failure
 */
SACDVFS_API sacd_vfs_ctx_t *sacd_vfs_create(void);

/**
 * Open an SACD ISO image for virtual filesystem access.
 *
 * @param ctx       VFS context
 * @param iso_path  Path to the SACD ISO file
 * @return SACD_VFS_OK on success, error code on failure
 */
int SACDVFS_API sacd_vfs_open(sacd_vfs_ctx_t *ctx, const char *iso_path);

/**
 * Close the VFS context and release resources.
 *
 * @param ctx  VFS context
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_close(sacd_vfs_ctx_t *ctx);

/**
 * Destroy the VFS context.
 *
 * @param ctx  VFS context (may be NULL)
 */
void SACDVFS_API sacd_vfs_destroy(sacd_vfs_ctx_t *ctx);

/* =============================================================================
 * Directory Operations
 * ===========================================================================*/

/**
 * Get album name from the SACD.
 *
 * @param ctx   VFS context
 * @param name  Output buffer for album name
 * @param size  Size of output buffer
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_get_album_name(sacd_vfs_ctx_t *ctx, char *name, size_t size);

/**
 * Check if an area (stereo/multichannel) is available.
 *
 * @param ctx   VFS context
 * @param area  Area to check
 * @return true if area is available
 */
bool SACDVFS_API sacd_vfs_has_area(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area);

/**
 * Set area visibility preference.
 *
 * Controls whether an area appears in directory listings. If a disc only
 * contains one area type, that area will be shown regardless of the
 * visibility setting (fallback behavior).
 *
 * @param ctx     VFS context
 * @param area    Area type (SACD_VFS_AREA_STEREO or SACD_VFS_AREA_MULTICHANNEL)
 * @param visible true to show area, false to hide
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_set_area_visibility(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area, bool visible);

/**
 * Get area visibility preference.
 *
 * @param ctx   VFS context
 * @param area  Area type
 * @return true if area is set to be visible
 */
bool SACDVFS_API sacd_vfs_get_area_visibility(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area);

/**
 * Check if an area should be shown (considering fallback logic).
 *
 * Returns true if:
 * - The area exists AND visibility is enabled, OR
 * - The area exists AND it's the only available area (fallback)
 *
 * @param ctx   VFS context
 * @param area  Area type
 * @return true if area should be shown in listings
 */
bool SACDVFS_API sacd_vfs_should_show_area(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area);

/**
 * Get the number of tracks in an area.
 *
 * @param ctx         VFS context
 * @param area        Area type
 * @param track_count Output for track count
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_get_track_count(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area, uint8_t *track_count);

/**
 * Read directory contents at the given path.
 *
 * Virtual directory structure:
 *   /                           - Root (contains album directory)
 *   /[Album Name]/              - Album directory
 *   /[Album Name]/Stereo/       - Stereo tracks (if available)
 *   /[Album Name]/Multi-channel/- Multi-channel tracks (if available)
 *
 * @param ctx       VFS context
 * @param path      Virtual path to list
 * @param callback  Callback function for each entry
 * @param userdata  User data passed to callback
 * @return SACD_VFS_OK on success, or number of entries
 */
int SACDVFS_API sacd_vfs_readdir(sacd_vfs_ctx_t *ctx, const char *path,
                     sacd_vfs_readdir_callback_t callback, void *userdata);

/**
 * Get information about a virtual path (stat).
 *
 * @param ctx    VFS context
 * @param path   Virtual path
 * @param entry  Output entry information
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_stat(sacd_vfs_ctx_t *ctx, const char *path, sacd_vfs_entry_t *entry);

/* =============================================================================
 * File Operations
 * ===========================================================================*/

/**
 * Open a virtual DSF file for reading.
 *
 * @param ctx    VFS context
 * @param path   Virtual path to the DSF file
 * @param file   Output file handle
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_file_open(sacd_vfs_ctx_t *ctx, const char *path, sacd_vfs_file_t **file);

/**
 * Open a virtual DSF file with multi-threaded DST decompression.
 *
 * For DST-encoded tracks, a dedicated reader thread reads compressed frames
 * ahead and dispatches decode jobs to the shared worker pool. Results are
 * consumed in dispatch order by the main read path. DSD (uncompressed)
 * tracks bypass the MT pipeline entirely.
 *
 * @param ctx    VFS context
 * @param path   Virtual path to the DSF file
 * @param pool   Thread pool (borrowed, not owned). May be NULL for ST fallback.
 * @param file   Output file handle
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_file_open_mt(sacd_vfs_ctx_t *ctx, const char *path,
                           sa_tpool *pool, sacd_vfs_file_t **file);

/**
 * Close a virtual file.
 *
 * @param file  File handle
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_file_close(sacd_vfs_file_t *file);

/**
 * Get file information.
 *
 * @param file  File handle
 * @param info  Output file information
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_file_get_info(sacd_vfs_file_t *file, sacd_vfs_file_info_t *info);

/**
 * Read data from a virtual file.
 *
 * This function handles:
 * - DSF header generation (DSD and fmt chunks)
 * - On-the-fly DSD transformation (bit-reversal, block interleaving)
 * - DST decompression (using thread pool for look-ahead)
 * - ID3 metadata injection
 *
 * @param file        File handle
 * @param buffer      Output buffer
 * @param size        Number of bytes to read
 * @param bytes_read  Output: actual bytes read
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_file_read(sacd_vfs_file_t *file, uint8_t *buffer, size_t size, size_t *bytes_read);

/**
 * Seek within a virtual file.
 *
 * @param file    File handle
 * @param offset  Offset to seek to
 * @param whence  SEEK_SET, SEEK_CUR, or SEEK_END
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_file_seek(sacd_vfs_file_t *file, int64_t offset, int whence);

/**
 * Get current position in the virtual file.
 *
 * @param file      File handle
 * @param position  Output current position
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_file_tell(sacd_vfs_file_t *file, uint64_t *position);

/* =============================================================================
 * ID3 Metadata Operations
 * ===========================================================================*/

/**
 * Get ID3 tag data for a track.
 *
 * The returned buffer is allocated and must be freed by the caller.
 *
 * @param ctx        VFS context
 * @param area       Area type
 * @param track_num  Track number (1-based)
 * @param buffer     Output buffer (allocated, caller must free)
 * @param size       Output buffer size
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_get_id3_tag(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area,
                         uint8_t track_num, uint8_t **buffer, size_t *size);

/**
 * Write ID3 tag overlay for a track.
 *
 * Since the ISO is read-only, ID3 modifications are stored in a sidecar
 * overlay database. This allows virtual "editing" of metadata.
 *
 * @param ctx        VFS context
 * @param area       Area type
 * @param track_num  Track number (1-based)
 * @param buffer     ID3 tag data
 * @param size       Buffer size
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_set_id3_overlay(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area,
                             uint8_t track_num, const uint8_t *buffer, size_t size);

/**
 * Save all modified ID3 tag overlays to XML sidecar file.
 *
 * The sidecar file is created alongside the ISO with a .xml extension
 * (e.g., album.iso -> album.iso.xml). ID3 tags are stored as base64-encoded
 * data within the XML structure.
 *
 * @param ctx  VFS context
 * @return SACD_VFS_OK on success, error code on failure
 */
int SACDVFS_API sacd_vfs_save_id3_overlay(sacd_vfs_ctx_t *ctx);

/**
 * Check if any ID3 overlays have been modified and need saving.
 *
 * @param ctx  VFS context
 * @return true if there are unsaved changes
 */
bool SACDVFS_API sacd_vfs_has_unsaved_id3_changes(sacd_vfs_ctx_t *ctx);

/**
 * Clear a specific ID3 overlay (revert to original from disc).
 *
 * This removes the overlay from memory. To persist the removal,
 * call sacd_vfs_save_id3_overlay() afterward.
 *
 * @param ctx        VFS context
 * @param area       Area type
 * @param track_num  Track number (1-based)
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_clear_id3_overlay(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area,
                               uint8_t track_num);

/* =============================================================================
 * Utility Functions
 * ===========================================================================*/

/**
 * Get error string for an error code.
 *
 * @param error  Error code
 * @return Human-readable error string
 */
SACDVFS_API const char *sacd_vfs_error_string(int error);

/**
 * Generate virtual filename for a track.
 *
 * Format: "NN. Track Title.dsf" where NN is zero-padded track number.
 *
 * @param ctx        VFS context
 * @param area       Area type
 * @param track_num  Track number (1-based)
 * @param filename   Output buffer
 * @param size       Buffer size
 * @return SACD_VFS_OK on success
 */
int SACDVFS_API sacd_vfs_get_track_filename(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area,
                                uint8_t track_num, char *filename, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* LIBSACDVFS_SACD_VFS_H */
