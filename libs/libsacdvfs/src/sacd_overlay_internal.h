/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD Overlay VFS - Internal Header
 * Internal type definitions shared between overlay implementation files.
 * This header is NOT part of the public API.
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

#ifndef LIBSACDVFS_SACD_OVERLAY_INTERNAL_H
#define LIBSACDVFS_SACD_OVERLAY_INTERNAL_H

#include <libsacdvfs/sacd_overlay.h>
#include <libsacdvfs/sacd_vfs.h>

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <sys/stat.h>
#define PATH_SEPARATOR '\\'
#define PATH_SEPARATOR_STR "\\"
/* POSIX macros not available on Windows */
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#else
#include <dirent.h>
#include <unistd.h>
#define PATH_SEPARATOR '/'
#define PATH_SEPARATOR_STR "/"
#endif

/* Threading support */
#ifdef __APPLE__
#include <libsautil/c11threads.h>
#else
#include <threads.h>
#endif

/* =============================================================================
 * Internal Constants
 * ===========================================================================*/

#define ISO_MOUNTS_INITIAL_CAPACITY  64
#define COLLISION_SUFFIX_MAX        32

/* =============================================================================
 * Internal Types
 * ===========================================================================*/

/** Mounted ISO context */
typedef struct iso_mount {
    char iso_path[SACD_OVERLAY_MAX_PATH];       /**< Full path to ISO file */
    char display_name[SACD_OVERLAY_MAX_FILENAME]; /**< Virtual folder name */
    char parent_vpath[SACD_OVERLAY_MAX_PATH];   /**< Virtual parent directory */
    char iso_vpath[SACD_OVERLAY_MAX_PATH];      /**< Pre-computed virtual path */
    size_t iso_vpath_len;                       /**< Length of iso_vpath */
    sacd_vfs_ctx_t *vfs;                        /**< libsacdvfs context (lazy) */
    int ref_count;                              /**< Reference count */
    time_t last_access;                         /**< Last access time */
    int collision_index;                        /**< 0=none, 1="(1)", etc. */
    mtx_t mount_lock;                        /**< Per-ISO lock */
} iso_mount_t;

/** Overlay context structure */
struct sacd_overlay_ctx {
    char source_dir[SACD_OVERLAY_MAX_PATH];
    int iso_extensions;
    int max_open_isos;
    int cache_timeout_seconds;
    int thread_pool_size;

    /* Area visibility settings */
    bool stereo_visible;                        /**< Show stereo area */
    bool multichannel_visible;                  /**< Show multichannel area */

    /* ISO mount table (dynamically grown) */
    iso_mount_t **iso_mounts;
    int iso_count;
    int iso_capacity;

    mtx_t iso_table_lock;                    /**< Protects iso_mounts array */
    sa_tpool *thread_pool;                      /**< Shared DST decode pool */
};

/** File handle structure */
struct sacd_overlay_file {
    sacd_overlay_ctx_t *ctx;
    sacd_overlay_source_t source;
    int open_flags;
    char vpath[SACD_OVERLAY_MAX_PATH];          /**< Virtual path */

    union {
        struct {
            FILE *fp;                           /**< Native file handle */
            char source_path[SACD_OVERLAY_MAX_PATH];
        } passthrough;
        struct {
            iso_mount_t *mount;                 /**< Reference to ISO mount */
            sacd_vfs_file_t *vfs_file;          /**< libsacdvfs file handle */
            uint8_t *id3_write_buf;             /**< Buffer for ID3 writes */
            size_t id3_write_len;               /**< Length of buffered data */
            size_t id3_write_offset;            /**< Offset of first write */
            int id3_dirty;                      /**< True if ID3 modified */
        } virt;
    };
};

/* =============================================================================
 * Internal Function Declarations
 * ===========================================================================*/

/* Path resolution (implemented in sacd_overlay_path.c) */
int _overlay_parse_path(sacd_overlay_ctx_t *ctx, const char *vpath,
                        char *parent_dir, size_t parent_size,
                        char *filename, size_t filename_size);
int _overlay_build_source_path(sacd_overlay_ctx_t *ctx, const char *vpath,
                               char *source_path, size_t size);
int _overlay_is_iso_file(const char *path, int ext_mask);
int _overlay_check_sacd_magic(const char *path);

/* ISO management (implemented in sacd_overlay_iso.c) */
iso_mount_t *_overlay_find_iso_mount(sacd_overlay_ctx_t *ctx,
                                      const char *iso_path);
iso_mount_t *_overlay_find_iso_by_vpath(sacd_overlay_ctx_t *ctx,
                                         const char *vpath);
iso_mount_t *_overlay_get_or_create_iso(sacd_overlay_ctx_t *ctx,
                                         const char *iso_path,
                                         const char *parent_vpath,
                                         const char *display_name,
                                         int collision_index);
sacd_vfs_ctx_t *_overlay_ensure_iso_mounted(sacd_overlay_ctx_t *ctx, iso_mount_t *mount);
void _overlay_release_iso(iso_mount_t *mount);
void _overlay_cleanup_iso(sacd_overlay_ctx_t *ctx, iso_mount_t *mount);

/* Directory scanning */
typedef int (*dir_scan_callback_t)(const char *name, int is_dir, void *userdata);
int _overlay_scan_source_dir(sacd_overlay_ctx_t *ctx, const char *source_path,
                             dir_scan_callback_t callback, void *userdata);

#endif /* LIBSACDVFS_SACD_OVERLAY_INTERNAL_H */
