/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD Overlay Virtual Filesystem - Header
 * This module provides a directory overlay layer that shadows a source
 * directory and automatically presents SACD ISO files as expandable folders
 * containing virtual DSF files.
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

#ifndef LIBSACDVFS_SACD_OVERLAY_H
#define LIBSACDVFS_SACD_OVERLAY_H

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

#define SACD_OVERLAY_MAX_PATH       1024
#define SACD_OVERLAY_MAX_FILENAME   256

/** ISO extension detection flags */
#define SACD_OVERLAY_EXT_ISO        0x01    /**< Detect .iso files */
#define SACD_OVERLAY_EXT_ISO_UPPER  0x02    /**< Detect .ISO files */
#define SACD_OVERLAY_EXT_DEFAULT    (SACD_OVERLAY_EXT_ISO | SACD_OVERLAY_EXT_ISO_UPPER)

/** Default configuration values */
#define SACD_OVERLAY_DEFAULT_CACHE_TIMEOUT  300     /**< 5 minutes */
#define SACD_OVERLAY_DEFAULT_MAX_ISOS       0       /**< Unlimited */

/* =============================================================================
 * Error Codes
 * ===========================================================================*/

typedef enum {
    SACD_OVERLAY_OK = 0,
    SACD_OVERLAY_ERROR_INVALID_PARAMETER = -1,
    SACD_OVERLAY_ERROR_NOT_FOUND = -2,      /**< ENOENT */
    SACD_OVERLAY_ERROR_IO = -3,             /**< EIO */
    SACD_OVERLAY_ERROR_MEMORY = -4,         /**< ENOMEM */
    SACD_OVERLAY_ERROR_ACCESS = -5,         /**< EACCES */
    SACD_OVERLAY_ERROR_NOT_DIR = -6,        /**< ENOTDIR */
    SACD_OVERLAY_ERROR_IS_DIR = -7,         /**< EISDIR */
    SACD_OVERLAY_ERROR_TOO_MANY_OPEN = -8,  /**< EMFILE */
    SACD_OVERLAY_ERROR_NOT_SACD = -9,       /**< Not a valid SACD ISO */
    SACD_OVERLAY_ERROR_ALREADY_MOUNTED = -10
} sacd_overlay_error_t;

/* =============================================================================
 * Types and Structures
 * ===========================================================================*/

/** Entry type enumeration */
typedef enum {
    SACD_OVERLAY_ENTRY_FILE = 0,        /**< Regular file */
    SACD_OVERLAY_ENTRY_DIRECTORY = 1,   /**< Directory */
    SACD_OVERLAY_ENTRY_ISO_FOLDER = 2   /**< SACD ISO presented as folder */
} sacd_overlay_entry_type_t;

/** File source type */
typedef enum {
    SACD_OVERLAY_SOURCE_PASSTHROUGH = 0, /**< Direct passthrough to source */
    SACD_OVERLAY_SOURCE_VIRTUAL = 1      /**< Virtual file from libsacdvfs */
} sacd_overlay_source_t;

/** Open mode flags */
typedef enum {
    SACD_OVERLAY_OPEN_READ = 0x01,      /**< Open for reading */
    SACD_OVERLAY_OPEN_WRITE = 0x02      /**< Open for writing (ID3 only) */
} sacd_overlay_open_flags_t;

/** Directory entry for stat/readdir */
typedef struct {
    char name[SACD_OVERLAY_MAX_FILENAME];
    sacd_overlay_entry_type_t type;
    sacd_overlay_source_t source;
    uint64_t size;              /**< File size in bytes (0 for directories) */
    uint64_t mtime;             /**< Modification time (Unix timestamp) */
    uint64_t atime;             /**< Access time */
    uint64_t ctime;             /**< Creation/status change time */
    uint32_t mode;              /**< Unix permission mode (e.g., 0644, 0755) */
    int writable;               /**< True if ID3 writes are supported */
} sacd_overlay_entry_t;

/** Configuration options */
typedef struct {
    const char *source_dir;         /**< Root source directory to shadow */
    int iso_extensions;             /**< Bitmask of SACD_OVERLAY_EXT_* flags */
    int thread_pool_size;           /**< DST decoder threads (0 = auto) */
    int max_open_isos;              /**< Max concurrent ISOs (0 = unlimited) */
    int cache_timeout_seconds;      /**< ISO cache timeout (0 = no timeout) */
    bool stereo_visible;            /**< Show stereo area (default: true) */
    bool multichannel_visible;      /**< Show multichannel area (default: true) */
} sacd_overlay_config_t;

/** Opaque overlay context handle */
typedef struct sacd_overlay_ctx sacd_overlay_ctx_t;

/** Opaque file handle */
typedef struct sacd_overlay_file sacd_overlay_file_t;

/** Directory listing callback */
typedef int (*sacd_overlay_readdir_cb)(const sacd_overlay_entry_t *entry, void *userdata);

/* =============================================================================
 * Context Management
 * ===========================================================================*/

/**
 * Create overlay context with configuration.
 *
 * @param config Configuration options (source_dir is required)
 * @return New context or NULL on failure
 */
SACDVFS_API sacd_overlay_ctx_t *sacd_overlay_create(const sacd_overlay_config_t *config);

/**
 * Destroy overlay context and release all resources.
 * Saves any pending ID3 overlay changes before cleanup.
 *
 * @param ctx Context to destroy (may be NULL)
 */
void SACDVFS_API sacd_overlay_destroy(sacd_overlay_ctx_t *ctx);

/**
 * Get the source directory path.
 *
 * @param ctx Overlay context
 * @return Source directory path or NULL if invalid
 */
SACDVFS_API const char *sacd_overlay_get_source_dir(sacd_overlay_ctx_t *ctx);

/* =============================================================================
 * Path Resolution
 * ===========================================================================*/

/**
 * Resolve a virtual path and get entry information (stat).
 *
 * Path resolution rules:
 * 1. If path points to real file/directory in source: PASSTHROUGH
 * 2. If path matches ISO basename where ISO exists: ISO_FOLDER
 * 3. If path is inside an ISO_FOLDER: VIRTUAL (delegate to libsacdvfs)
 *
 * @param ctx   Overlay context
 * @param path  Virtual path (relative to mount point, starting with /)
 * @param entry Output entry information
 * @return SACD_OVERLAY_OK on success, error code on failure
 */
int SACDVFS_API sacd_overlay_stat(sacd_overlay_ctx_t *ctx, const char *path,
                      sacd_overlay_entry_t *entry);

/**
 * Translate virtual path to source filesystem path.
 * Only valid for PASSTHROUGH entries.
 *
 * @param ctx         Overlay context
 * @param path        Virtual path
 * @param source_path Output buffer for source path
 * @param size        Buffer size
 * @return SACD_OVERLAY_OK on success, error code on failure
 */
int SACDVFS_API sacd_overlay_get_source_path(sacd_overlay_ctx_t *ctx, const char *path,
                                  char *source_path, size_t size);

/**
 * Check if a path points to a virtual (ISO-based) entry.
 *
 * @param ctx  Overlay context
 * @param path Virtual path
 * @return true if path is inside an ISO folder
 */
bool SACDVFS_API sacd_overlay_is_virtual_path(sacd_overlay_ctx_t *ctx, const char *path);

/* =============================================================================
 * Directory Operations
 * ===========================================================================*/

/**
 * List directory contents.
 *
 * For passthrough directories: lists source directory entries with ISO replacement
 * For ISO folders: lists virtual SACD contents (Stereo/, Multi-channel/)
 *
 * Special handling:
 * - ISO files are hidden and replaced with virtual folders
 * - XML sidecar files (.iso.xml) are hidden
 * - Name collisions are resolved with (1), (2), etc.
 *
 * @param ctx       Overlay context
 * @param path      Directory path
 * @param callback  Called for each entry (return non-zero to stop)
 * @param userdata  Passed to callback
 * @return Number of entries on success, or negative error code
 */
int SACDVFS_API sacd_overlay_readdir(sacd_overlay_ctx_t *ctx, const char *path,
                         sacd_overlay_readdir_cb callback, void *userdata);

/* =============================================================================
 * File Operations
 * ===========================================================================*/

/**
 * Open a file for reading (and optionally writing for virtual DSF files).
 *
 * For passthrough files: opens source file directly
 * For virtual files: opens libsacdvfs file handle
 *
 * @param ctx   Overlay context
 * @param path  File path
 * @param flags Open flags (SACD_OVERLAY_OPEN_*)
 * @param file  Output file handle
 * @return SACD_OVERLAY_OK on success, error code on failure
 */
int SACDVFS_API sacd_overlay_open(sacd_overlay_ctx_t *ctx, const char *path,
                      int flags, sacd_overlay_file_t **file);

/**
 * Close a file handle.
 * For virtual files: saves any pending ID3 overlay changes.
 *
 * @param file File handle
 * @return SACD_OVERLAY_OK on success
 */
int SACDVFS_API sacd_overlay_close(sacd_overlay_file_t *file);

/**
 * Read from file at specified offset.
 *
 * @param file       File handle
 * @param buffer     Output buffer
 * @param size       Bytes to read
 * @param offset     File offset
 * @param bytes_read Output: actual bytes read
 * @return SACD_OVERLAY_OK on success, error code on failure
 */
int SACDVFS_API sacd_overlay_read(sacd_overlay_file_t *file, void *buffer, size_t size,
                      uint64_t offset, size_t *bytes_read);

/**
 * Write to file at specified offset.
 *
 * For passthrough files: writes to source file
 * For virtual DSF files: only ID3 region is writable (EACCES otherwise)
 *
 * @param file          File handle
 * @param buffer        Input buffer
 * @param size          Bytes to write
 * @param offset        File offset
 * @param bytes_written Output: actual bytes written
 * @return SACD_OVERLAY_OK on success, error code on failure
 */
int SACDVFS_API sacd_overlay_write(sacd_overlay_file_t *file, const void *buffer,
                       size_t size, uint64_t offset, size_t *bytes_written);

/**
 * Flush pending writes.
 * For virtual files: saves ID3 overlay changes to XML sidecar.
 *
 * @param file File handle
 * @return SACD_OVERLAY_OK on success
 */
int SACDVFS_API sacd_overlay_flush(sacd_overlay_file_t *file);

/**
 * Get file attributes from open handle (fstat).
 *
 * @param file  File handle
 * @param entry Output entry information
 * @return SACD_OVERLAY_OK on success
 */
int SACDVFS_API sacd_overlay_fstat(sacd_overlay_file_t *file, sacd_overlay_entry_t *entry);

/**
 * Get file size from open handle.
 *
 * @param file File handle
 * @param size Output file size
 * @return SACD_OVERLAY_OK on success
 */
int SACDVFS_API sacd_overlay_file_size(sacd_overlay_file_t *file, uint64_t *size);

/* =============================================================================
 * ISO Management
 * ===========================================================================*/

/**
 * Get the number of currently mounted ISOs.
 *
 * @param ctx Overlay context
 * @return Number of mounted ISOs
 */
int SACDVFS_API sacd_overlay_get_mounted_iso_count(sacd_overlay_ctx_t *ctx);

/**
 * Flush all pending ID3 changes across all mounted ISOs.
 *
 * @param ctx Overlay context
 * @return SACD_OVERLAY_OK on success
 */
int SACDVFS_API sacd_overlay_flush_all(sacd_overlay_ctx_t *ctx);

/**
 * Unmount idle ISOs that haven't been accessed recently.
 * Called automatically by the cache timeout mechanism.
 *
 * @param ctx Overlay context
 * @return Number of ISOs unmounted
 */
int SACDVFS_API sacd_overlay_cleanup_idle(sacd_overlay_ctx_t *ctx);

/* =============================================================================
 * Utility Functions
 * ===========================================================================*/

/**
 * Get error string for an error code.
 *
 * @param error Error code
 * @return Human-readable error string
 */
SACDVFS_API const char *sacd_overlay_error_string(int error);

/**
 * Convert overlay error code to errno value.
 *
 * @param error Overlay error code
 * @return Corresponding errno value (e.g., ENOENT, EIO)
 */
int SACDVFS_API sacd_overlay_error_to_errno(int error);

/**
 * Initialize default configuration.
 *
 * @param config Configuration structure to initialize
 */
void SACDVFS_API sacd_overlay_config_init(sacd_overlay_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* LIBSACDVFS_SACD_OVERLAY_H */
