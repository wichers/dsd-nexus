/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD Overlay Virtual Filesystem - Implementation
 * This module provides a directory overlay layer that shadows a source
 * directory and automatically presents SACD ISO files as expandable folders.
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


#include "sacd_overlay_internal.h"

#include <libsautil/mem.h>
#include <libsautil/sa_tpool.h>
#include <libsautil/compat.h>
#include <libsautil/sastring.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#endif

/* =============================================================================
 * Error Strings
 * ===========================================================================*/

static const char *_error_strings[] = {
    "Success",
    "Invalid parameter",
    "Not found",
    "I/O error",
    "Memory allocation error",
    "Access denied",
    "Not a directory",
    "Is a directory",
    "Too many open files",
    "Not a valid SACD ISO",
    "Already mounted"
};

const char *sacd_overlay_error_string(int error)
{
    if (error >= 0) {
        return _error_strings[0];
    }
    int idx = -error;
    if (idx < (int)(sizeof(_error_strings) / sizeof(_error_strings[0]))) {
        return _error_strings[idx];
    }
    return "Unknown error";
}

int sacd_overlay_error_to_errno(int error)
{
    switch (error) {
    case SACD_OVERLAY_OK:
        return 0;
    case SACD_OVERLAY_ERROR_NOT_FOUND:
        return ENOENT;
    case SACD_OVERLAY_ERROR_IO:
        return EIO;
    case SACD_OVERLAY_ERROR_MEMORY:
        return ENOMEM;
    case SACD_OVERLAY_ERROR_ACCESS:
        return EACCES;
    case SACD_OVERLAY_ERROR_NOT_DIR:
        return ENOTDIR;
    case SACD_OVERLAY_ERROR_IS_DIR:
        return EISDIR;
    case SACD_OVERLAY_ERROR_TOO_MANY_OPEN:
        return EMFILE;
    default:
        return EINVAL;
    }
}

/* =============================================================================
 * Configuration
 * ===========================================================================*/

void sacd_overlay_config_init(sacd_overlay_config_t *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->iso_extensions = SACD_OVERLAY_EXT_DEFAULT;
    config->thread_pool_size = 0;  /* Auto */
    config->max_open_isos = SACD_OVERLAY_DEFAULT_MAX_ISOS;
    config->cache_timeout_seconds = SACD_OVERLAY_DEFAULT_CACHE_TIMEOUT;
    config->stereo_visible = true;
    config->multichannel_visible = true;
}

/* =============================================================================
 * Context Management
 * ===========================================================================*/

sacd_overlay_ctx_t *sacd_overlay_create(const sacd_overlay_config_t *config)
{
    if (!config || !config->source_dir || config->source_dir[0] == '\0') {
        return NULL;
    }

    /* Verify source directory exists */
    struct stat st;
    if (stat(config->source_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return NULL;
    }

    sacd_overlay_ctx_t *ctx = sa_mallocz(sizeof(sacd_overlay_ctx_t));
    if (!ctx) {
        return NULL;
    }

    /* Copy configuration */
    sa_strlcpy(ctx->source_dir, config->source_dir, sizeof(ctx->source_dir));

    /* Normalize path: remove trailing separator */
    size_t len = strlen(ctx->source_dir);
    while (len > 1 && (ctx->source_dir[len - 1] == '/' ||
                       ctx->source_dir[len - 1] == '\\')) {
        ctx->source_dir[--len] = '\0';
    }

    ctx->iso_extensions = config->iso_extensions;
    ctx->max_open_isos = config->max_open_isos;
    ctx->cache_timeout_seconds = config->cache_timeout_seconds;
    ctx->thread_pool_size = config->thread_pool_size;
    ctx->stereo_visible = config->stereo_visible;
    ctx->multichannel_visible = config->multichannel_visible;
    ctx->iso_count = 0;

    if (mtx_init(&ctx->iso_table_lock, mtx_plain) != thrd_success) {
        sa_free(ctx);
        return NULL;
    }

    /* Create shared thread pool for multi-threaded DST decompression.
     * thread_pool_size of 0 means auto (default: 4 worker threads).
     * A negative value disables multi-threading entirely.
     */
    ctx->thread_pool = NULL;
    if (ctx->thread_pool_size >= 0) {
        int nthreads = ctx->thread_pool_size;
        if (nthreads == 0) {
            nthreads = 4;  /* Reasonable default for DST decompression */
        }
        ctx->thread_pool = sa_tpool_init(nthreads);
        /* NULL is not fatal - falls back to single-threaded DST decoding */
    }

    return ctx;
}

void sacd_overlay_destroy(sacd_overlay_ctx_t *ctx)
{
    if (!ctx) return;

    /* Flush all pending changes and close all ISOs */
    sacd_overlay_flush_all(ctx);

    mtx_lock(&ctx->iso_table_lock);

    for (int i = 0; i < ctx->iso_count; i++) {
        if (ctx->iso_mounts[i]) {
            iso_mount_t *mount = ctx->iso_mounts[i];

            mtx_lock(&mount->mount_lock);
            if (mount->vfs) {
                sacd_vfs_close(mount->vfs);
                sacd_vfs_destroy(mount->vfs);
                mount->vfs = NULL;
            }
            mtx_unlock(&mount->mount_lock);
            mtx_destroy(&mount->mount_lock);
            sa_free(mount);
            ctx->iso_mounts[i] = NULL;
        }
    }

    mtx_unlock(&ctx->iso_table_lock);
    mtx_destroy(&ctx->iso_table_lock);

    /* Destroy thread pool after all ISOs are unmounted
     * (MT readers must be stopped before pool destruction) */
    if (ctx->thread_pool) {
        sa_tpool_destroy(ctx->thread_pool);
        ctx->thread_pool = NULL;
    }

    sa_free(ctx);
}

const char *sacd_overlay_get_source_dir(sacd_overlay_ctx_t *ctx)
{
    if (!ctx) return NULL;
    return ctx->source_dir;
}

/* =============================================================================
 * Path Resolution - Public API
 * ===========================================================================*/

int sacd_overlay_stat(sacd_overlay_ctx_t *ctx, const char *path,
                      sacd_overlay_entry_t *entry)
{
    if (!ctx || !path || !entry) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    memset(entry, 0, sizeof(*entry));

    /* Handle root */
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
        sa_strlcpy(entry->name, "/", sizeof(entry->name));
        entry->type = SACD_OVERLAY_ENTRY_DIRECTORY;
        entry->source = SACD_OVERLAY_SOURCE_PASSTHROUGH;
        entry->mode = 0755;
        return SACD_OVERLAY_OK;
    }

    /* Build source path */
    char source_path[SACD_OVERLAY_MAX_PATH];
    int result = _overlay_build_source_path(ctx, path, source_path, sizeof(source_path));

    if (result == SACD_OVERLAY_OK) {
        /* Check if it's a real file/directory */
        struct stat st;
        if (stat(source_path, &st) == 0) {
            /* Extract filename */
            const char *name = strrchr(path, '/');
            name = name ? name + 1 : path;

            /* Check if this is an ISO file being accessed directly */
            if (S_ISREG(st.st_mode) && _overlay_is_iso_file(source_path, ctx->iso_extensions)) {
                /* ISO files are hidden - return not found */
                return SACD_OVERLAY_ERROR_NOT_FOUND;
            }

            /* Check if this is an XML sidecar file */
            size_t name_len = strlen(name);
            if (name_len > 8 && strcmp(name + name_len - 8, ".iso.xml") == 0) {
                return SACD_OVERLAY_ERROR_NOT_FOUND;
            }

            sa_strlcpy(entry->name, name, sizeof(entry->name));
            entry->type = S_ISDIR(st.st_mode) ? SACD_OVERLAY_ENTRY_DIRECTORY : SACD_OVERLAY_ENTRY_FILE;
            entry->source = SACD_OVERLAY_SOURCE_PASSTHROUGH;
            entry->size = S_ISREG(st.st_mode) ? st.st_size : 0;
            entry->mtime = st.st_mtime;
            entry->atime = st.st_atime;
            entry->ctime = st.st_ctime;
            entry->mode = st.st_mode & 0777;
            entry->writable = (entry->type == SACD_OVERLAY_ENTRY_FILE);
            return SACD_OVERLAY_OK;
        }
    }

    /* Check if this is a virtual ISO folder */
    iso_mount_t *mount = _overlay_find_iso_by_vpath(ctx, path);
    if (mount) {
        /* This is an ISO folder or something inside it */
        const char *rel_path = path + strlen(mount->parent_vpath);
        if (*rel_path == '/') rel_path++;

        /* Skip the display name part */
        const char *inner_path = strchr(rel_path, '/');

        if (!inner_path || strcmp(inner_path, "/") == 0) {
            /* This is the ISO folder itself */
            sa_strlcpy(entry->name, mount->display_name, sizeof(entry->name));
            entry->type = SACD_OVERLAY_ENTRY_ISO_FOLDER;
            entry->source = SACD_OVERLAY_SOURCE_VIRTUAL;
            entry->mode = 0755;
            return SACD_OVERLAY_OK;
        }

        /* Delegate to libsacdvfs for inner paths */
        sacd_vfs_ctx_t *vfs = _overlay_ensure_iso_mounted(ctx, mount);
        if (!vfs) {
            return SACD_OVERLAY_ERROR_IO;
        }

        sacd_vfs_entry_t vfs_entry;
        result = sacd_vfs_stat(vfs, inner_path, &vfs_entry);
        if (result != SACD_VFS_OK) {
            return SACD_OVERLAY_ERROR_NOT_FOUND;
        }

        sa_strlcpy(entry->name, vfs_entry.name, sizeof(entry->name));
        entry->type = (vfs_entry.type == SACD_VFS_ENTRY_DIRECTORY) ?
                      SACD_OVERLAY_ENTRY_DIRECTORY : SACD_OVERLAY_ENTRY_FILE;
        entry->source = SACD_OVERLAY_SOURCE_VIRTUAL;
        entry->size = vfs_entry.size;
        entry->mode = (entry->type == SACD_OVERLAY_ENTRY_DIRECTORY) ? 0755 : 0666;  /* 0666 for WinFSP write support */
        entry->writable = (entry->type == SACD_OVERLAY_ENTRY_FILE);  /* ID3 writable */
        return SACD_OVERLAY_OK;
    }

    /* Check if this could be an ISO folder that hasn't been mounted yet */
    char parent_dir[SACD_OVERLAY_MAX_PATH];
    char filename[SACD_OVERLAY_MAX_FILENAME];
    result = _overlay_parse_path(ctx, path, parent_dir, sizeof(parent_dir),
                                  filename, sizeof(filename));
    if (result == SACD_OVERLAY_OK) {
        /* Build source path for parent */
        char parent_source[SACD_OVERLAY_MAX_PATH];
        result = _overlay_build_source_path(ctx, parent_dir,
                                            parent_source, sizeof(parent_source));
        if (result == SACD_OVERLAY_OK) {
            /* Look for ISO file with this name */
            size_t filename_len = strlen(filename);

            /* Validate that filename + ".iso" fits in buffer */
            if (filename_len + 4 >= SACD_OVERLAY_MAX_FILENAME) {
                return SACD_OVERLAY_ERROR_NOT_FOUND;  /* Filename too long */
            }

            char iso_name[SACD_OVERLAY_MAX_FILENAME];
            sa_snprintf(iso_name, sizeof(iso_name), "%s.iso", filename);

            /* Validate that parent_source + separator + iso_name fits in buffer */
            size_t parent_len = strlen(parent_source);
            size_t iso_name_len = strlen(iso_name);
            if (parent_len + 1 + iso_name_len >= SACD_OVERLAY_MAX_PATH) {
                return SACD_OVERLAY_ERROR_NOT_FOUND;  /* Path too long */
            }

            char iso_path[SACD_OVERLAY_MAX_PATH];
            sa_snprintf(iso_path, sizeof(iso_path), "%s%c%s",
                        parent_source, PATH_SEPARATOR, iso_name);

            struct stat st;
            if (stat(iso_path, &st) == 0 && S_ISREG(st.st_mode)) {
                if (_overlay_check_sacd_magic(iso_path)) {
                    sa_strlcpy(entry->name, filename, sizeof(entry->name));
                    entry->type = SACD_OVERLAY_ENTRY_ISO_FOLDER;
                    entry->source = SACD_OVERLAY_SOURCE_VIRTUAL;
                    entry->mode = 0755;
                    return SACD_OVERLAY_OK;
                }
            }
        }
    }

    return SACD_OVERLAY_ERROR_NOT_FOUND;
}

int sacd_overlay_get_source_path(sacd_overlay_ctx_t *ctx, const char *path,
                                  char *source_path, size_t size)
{
    if (!ctx || !path || !source_path || size == 0) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    /* Check if this is a virtual path */
    if (sacd_overlay_is_virtual_path(ctx, path)) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;  /* Can't get source for virtual */
    }

    return _overlay_build_source_path(ctx, path, source_path, size);
}

bool sacd_overlay_is_virtual_path(sacd_overlay_ctx_t *ctx, const char *path)
{
    if (!ctx || !path) return false;

    iso_mount_t *mount = _overlay_find_iso_by_vpath(ctx, path);
    return (mount != NULL);
}

/* =============================================================================
 * Directory Operations
 * ===========================================================================*/

/** Context for readdir scanning */
typedef struct {
    sacd_overlay_ctx_t *ctx;
    const char *vpath;
    sacd_overlay_readdir_cb callback;
    void *userdata;
    int count;
    int stopped;

    /* Name tracking for collision detection */
    char **seen_names;
    int seen_count;
    int seen_capacity;
} readdir_ctx_t;

static int _add_seen_name(readdir_ctx_t *rctx, const char *name)
{
    if (rctx->seen_count >= rctx->seen_capacity) {
        int new_cap = rctx->seen_capacity ? rctx->seen_capacity * 2 : 64;
        char **new_names = sa_realloc(rctx->seen_names, new_cap * sizeof(char *));
        if (!new_names) return -1;
        rctx->seen_names = new_names;
        rctx->seen_capacity = new_cap;
    }
    rctx->seen_names[rctx->seen_count++] = sa_strdup(name);
    return 0;
}

static int _has_seen_name(readdir_ctx_t *rctx, const char *name)
{
    for (int i = 0; i < rctx->seen_count; i++) {
        if (rctx->seen_names[i] && strcmp(rctx->seen_names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void _free_seen_names(readdir_ctx_t *rctx)
{
    for (int i = 0; i < rctx->seen_count; i++) {
        sa_free(rctx->seen_names[i]);
    }
    sa_free(rctx->seen_names);
    rctx->seen_names = NULL;
    rctx->seen_count = 0;
    rctx->seen_capacity = 0;
}

static int _readdir_source_callback(const char *name, int is_dir, void *userdata)
{
    readdir_ctx_t *rctx = (readdir_ctx_t *)userdata;
    if (rctx->stopped) return 0;

    /* Skip . and .. */
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }

    /* Build full source path */
    char source_path[SACD_OVERLAY_MAX_PATH];
    int result = _overlay_build_source_path(rctx->ctx, rctx->vpath,
                                            source_path, sizeof(source_path));
    if (result != SACD_OVERLAY_OK) return 0;

    /* Validate that source_path + separator + name fits in buffer */
    size_t source_len = strlen(source_path);
    size_t name_len = strlen(name);
    if (source_len + 1 + name_len >= SACD_OVERLAY_MAX_PATH) {
        return 0;  /* Path too long, skip this entry */
    }

    char full_path[SACD_OVERLAY_MAX_PATH];
    sa_snprintf(full_path, sizeof(full_path), "%s%c%s", source_path, PATH_SEPARATOR, name);

    /* Check if this is an ISO file - hide it and add as virtual folder */
    if (!is_dir && _overlay_is_iso_file(full_path, rctx->ctx->iso_extensions)) {
        /* Debug: ISO file detected */
        fprintf(stderr, "OVERLAY DEBUG: Found ISO file: %s\n", full_path);

        /* Check if it's a valid SACD */
        int is_sacd = _overlay_check_sacd_magic(full_path);
        fprintf(stderr, "OVERLAY DEBUG: SACD magic check: %s\n", is_sacd ? "PASS" : "FAIL");

        if (is_sacd) {
            /* Get base name (without .iso) */
            char base_name[SACD_OVERLAY_MAX_FILENAME];
            sa_strlcpy(base_name, name, sizeof(base_name));
            char *ext = strrchr(base_name, '.');
            if (ext) *ext = '\0';

            /* Resolve collision */
            char display_name[SACD_OVERLAY_MAX_FILENAME];
            sa_strlcpy(display_name, base_name, sizeof(display_name));
            int collision_idx = 0;

            while (_has_seen_name(rctx, display_name)) {
                collision_idx++;
                /* Reserve space for " (NNN)" suffix - max 10 chars for safety */
                size_t base_len = strlen(base_name);
                if (base_len + 10 >= SACD_OVERLAY_MAX_FILENAME) {
                    /* Base name too long, truncate it */
                    base_name[SACD_OVERLAY_MAX_FILENAME - 11] = '\0';
                }
                sa_snprintf(display_name, sizeof(display_name), "%s (%d)", base_name, collision_idx);
            }

            _add_seen_name(rctx, display_name);

            /* Register ISO mount */
            _overlay_get_or_create_iso(rctx->ctx, full_path, rctx->vpath,
                                       display_name, collision_idx);

            /* Add as directory entry */
            sacd_overlay_entry_t entry;
            memset(&entry, 0, sizeof(entry));
            sa_strlcpy(entry.name, display_name, sizeof(entry.name));
            entry.type = SACD_OVERLAY_ENTRY_ISO_FOLDER;
            entry.source = SACD_OVERLAY_SOURCE_VIRTUAL;
            entry.mode = 0755;

            if (rctx->callback(&entry, rctx->userdata) != 0) {
                rctx->stopped = 1;
                return 0;
            }
            rctx->count++;
        }
        return 0;  /* Either way, don't show the .iso file */
    }

    /* Hide XML sidecar files (reuse name_len from above) */
    if (name_len > 8 && strcmp(name + name_len - 8, ".iso.xml") == 0) {
        return 0;
    }

    /* Regular file or directory */
    _add_seen_name(rctx, name);

    sacd_overlay_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    sa_strlcpy(entry.name, name, sizeof(entry.name));
    entry.type = is_dir ? SACD_OVERLAY_ENTRY_DIRECTORY : SACD_OVERLAY_ENTRY_FILE;
    entry.source = SACD_OVERLAY_SOURCE_PASSTHROUGH;

    /* Get file stats */
    struct stat st;
    if (stat(full_path, &st) == 0) {
        entry.size = is_dir ? 0 : st.st_size;
        entry.mtime = st.st_mtime;
        entry.atime = st.st_atime;
        entry.ctime = st.st_ctime;
        entry.mode = st.st_mode & 0777;
    } else {
        entry.mode = is_dir ? 0755 : 0666;  /* 0666 for WinFSP write support */
    }
    entry.writable = !is_dir;

    if (rctx->callback(&entry, rctx->userdata) != 0) {
        rctx->stopped = 1;
        return 0;
    }
    rctx->count++;
    return 0;
}

/** Context for VFS readdir adapter callback */
typedef struct {
    sacd_overlay_readdir_cb cb;
    void *ud;
    int count;
} vfs_readdir_cb_ctx_t;

/** Adapter callback for VFS readdir to overlay readdir */
static int _vfs_readdir_cb(const sacd_vfs_entry_t *vfs_entry, void *ud)
{
    vfs_readdir_cb_ctx_t *vctx = (vfs_readdir_cb_ctx_t *)ud;
    sacd_overlay_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    sa_strlcpy(entry.name, vfs_entry->name, sizeof(entry.name));
    entry.type = (vfs_entry->type == SACD_VFS_ENTRY_DIRECTORY) ?
                 SACD_OVERLAY_ENTRY_DIRECTORY : SACD_OVERLAY_ENTRY_FILE;
    entry.source = SACD_OVERLAY_SOURCE_VIRTUAL;
    entry.size = vfs_entry->size;
    entry.mode = (entry.type == SACD_OVERLAY_ENTRY_DIRECTORY) ? 0755 : 0666;  /* 0666 for WinFSP write support */
    entry.writable = (entry.type == SACD_OVERLAY_ENTRY_FILE);
    vctx->count++;
    return vctx->cb(&entry, vctx->ud);
}

int sacd_overlay_readdir(sacd_overlay_ctx_t *ctx, const char *path,
                         sacd_overlay_readdir_cb callback, void *userdata)
{
    if (!ctx || !path || !callback) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    /* Check if this is inside an ISO folder */
    iso_mount_t *mount = _overlay_find_iso_by_vpath(ctx, path);
    if (mount) {
        /* Get inner path within ISO */
        const char *rel_path = path + strlen(mount->parent_vpath);
        if (*rel_path == '/') rel_path++;

        /* Skip the display name */
        const char *inner_path = strchr(rel_path, '/');
        if (!inner_path) inner_path = "/";

        /* Delegate to libsacdvfs */
        sacd_vfs_ctx_t *vfs = _overlay_ensure_iso_mounted(ctx, mount);
        if (!vfs) {
            return SACD_OVERLAY_ERROR_IO;
        }

        vfs_readdir_cb_ctx_t vctx = { callback, userdata, 0 };
        int result = sacd_vfs_readdir(vfs, inner_path, _vfs_readdir_cb, &vctx);
        if (result < 0) {
            return SACD_OVERLAY_ERROR_IO;
        }
        return vctx.count;
    }

    /* Build source path */
    char source_path[SACD_OVERLAY_MAX_PATH];
    int result = _overlay_build_source_path(ctx, path, source_path, sizeof(source_path));
    if (result != SACD_OVERLAY_OK) {
        return result;
    }

    /* Verify it's a directory */
    struct stat st;
    if (stat(source_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return SACD_OVERLAY_ERROR_NOT_DIR;
    }

    /* Scan directory */
    readdir_ctx_t rctx = {
        .ctx = ctx,
        .vpath = path,
        .callback = callback,
        .userdata = userdata,
        .count = 0,
        .stopped = 0,
        .seen_names = NULL,
        .seen_count = 0,
        .seen_capacity = 0
    };

    result = _overlay_scan_source_dir(ctx, source_path, _readdir_source_callback, &rctx);

    _free_seen_names(&rctx);

    if (result != SACD_OVERLAY_OK && result != SACD_OVERLAY_ERROR_INVALID_PARAMETER) {
        return result;
    }

    return rctx.count;
}

/* =============================================================================
 * File Operations
 * ===========================================================================*/

int sacd_overlay_open(sacd_overlay_ctx_t *ctx, const char *path,
                      int flags, sacd_overlay_file_t **file)
{
    if (!ctx || !path || !file) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    *file = NULL;

    /* Check if this is a virtual path (inside ISO) */
    iso_mount_t *mount = _overlay_find_iso_by_vpath(ctx, path);
    if (mount) {
        /* Get inner path within ISO */
        const char *rel_path = path + strlen(mount->parent_vpath);
        if (*rel_path == '/') rel_path++;

        /* Skip the display name */
        const char *inner_path = strchr(rel_path, '/');
        if (!inner_path) {
            /* Trying to open the ISO folder itself as a file */
            return SACD_OVERLAY_ERROR_IS_DIR;
        }

        /* Mount ISO if needed */
        sacd_vfs_ctx_t *vfs = _overlay_ensure_iso_mounted(ctx, mount);
        if (!vfs) {
            return SACD_OVERLAY_ERROR_IO;
        }

        /* Open file via libsacdvfs (with MT DST decode if pool available) */
        sacd_vfs_file_t *vfs_file = NULL;
        int result = sacd_vfs_file_open_mt(vfs, inner_path,
                                            ctx->thread_pool, &vfs_file);
        if (result != SACD_VFS_OK) {
            return SACD_OVERLAY_ERROR_NOT_FOUND;
        }

        /* Create file handle */
        sacd_overlay_file_t *f = sa_mallocz(sizeof(sacd_overlay_file_t));
        if (!f) {
            sacd_vfs_file_close(vfs_file);
            return SACD_OVERLAY_ERROR_MEMORY;
        }

        f->ctx = ctx;
        f->source = SACD_OVERLAY_SOURCE_VIRTUAL;
        f->open_flags = flags;
        sa_strlcpy(f->vpath, path, sizeof(f->vpath));
        f->virt.mount = mount;
        f->virt.vfs_file = vfs_file;
        f->virt.id3_write_buf = NULL;
        f->virt.id3_write_len = 0;
        f->virt.id3_write_offset = 0;
        f->virt.id3_dirty = 0;

        /* Increment ref count on mount */
        mount->ref_count++;
        mount->last_access = time(NULL);

        *file = f;
        return SACD_OVERLAY_OK;
    }

    /* Passthrough file */
    char source_path[SACD_OVERLAY_MAX_PATH];
    int result = _overlay_build_source_path(ctx, path, source_path, sizeof(source_path));
    if (result != SACD_OVERLAY_OK) {
        return result;
    }

    /* Check if it's an ISO file (should be hidden) */
    if (_overlay_is_iso_file(source_path, ctx->iso_extensions)) {
        return SACD_OVERLAY_ERROR_NOT_FOUND;
    }

    /* Check if file exists and is not a directory */
    struct stat st;
    if (stat(source_path, &st) != 0) {
        return SACD_OVERLAY_ERROR_NOT_FOUND;
    }
    if (S_ISDIR(st.st_mode)) {
        return SACD_OVERLAY_ERROR_IS_DIR;
    }

    /* Open file */
    const char *mode = (flags & SACD_OVERLAY_OPEN_WRITE) ? "r+b" : "rb";
    FILE *fp = sa_fopen(source_path, mode);
    if (!fp) {
        return SACD_OVERLAY_ERROR_IO;
    }

    /* Create file handle */
    sacd_overlay_file_t *f = sa_mallocz(sizeof(sacd_overlay_file_t));
    if (!f) {
        fclose(fp);
        return SACD_OVERLAY_ERROR_MEMORY;
    }

    f->ctx = ctx;
    f->source = SACD_OVERLAY_SOURCE_PASSTHROUGH;
    f->open_flags = flags;
    sa_strlcpy(f->vpath, path, sizeof(f->vpath));
    f->passthrough.fp = fp;
    sa_strlcpy(f->passthrough.source_path, source_path, sizeof(f->passthrough.source_path));

    *file = f;
    return SACD_OVERLAY_OK;
}

int sacd_overlay_close(sacd_overlay_file_t *file)
{
    if (!file) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    /* Flush any pending writes */
    sacd_overlay_flush(file);

    if (file->source == SACD_OVERLAY_SOURCE_PASSTHROUGH) {
        if (file->passthrough.fp) {
            fclose(file->passthrough.fp);
            file->passthrough.fp = NULL;
        }
    } else {
        /* Virtual file */
        if (file->virt.vfs_file) {
            sacd_vfs_file_close(file->virt.vfs_file);
            file->virt.vfs_file = NULL;
        }
        if (file->virt.id3_write_buf) {
            sa_free(file->virt.id3_write_buf);
            file->virt.id3_write_buf = NULL;
        }
        if (file->virt.mount) {
            _overlay_release_iso(file->virt.mount);
            file->virt.mount = NULL;
        }
    }

    sa_free(file);
    return SACD_OVERLAY_OK;
}

int sacd_overlay_read(sacd_overlay_file_t *file, void *buffer, size_t size,
                      uint64_t offset, size_t *bytes_read)
{
    if (!file || !buffer || !bytes_read) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    *bytes_read = 0;

    if (file->source == SACD_OVERLAY_SOURCE_PASSTHROUGH) {
        /* Passthrough read */
        if (!file->passthrough.fp) {
            return SACD_OVERLAY_ERROR_IO;
        }

        if (sa_fseek64(file->passthrough.fp, (int64_t)offset, SEEK_SET) != 0) {
            return SACD_OVERLAY_ERROR_IO;
        }

        size_t read = fread(buffer, 1, size, file->passthrough.fp);
        *bytes_read = read;
        return SACD_OVERLAY_OK;
    } else {
        /* Virtual file via libsacdvfs */
        if (!file->virt.vfs_file) {
            return SACD_OVERLAY_ERROR_IO;
        }

        int result = sacd_vfs_file_seek(file->virt.vfs_file, (int64_t)offset, SEEK_SET);
        if (result != SACD_VFS_OK) {
            return SACD_OVERLAY_ERROR_IO;
        }

        result = sacd_vfs_file_read(file->virt.vfs_file, buffer, size, bytes_read);
        if (result != SACD_VFS_OK && result != SACD_VFS_ERROR_EOF) {
            return SACD_OVERLAY_ERROR_IO;
        }

        return SACD_OVERLAY_OK;
    }
}

int sacd_overlay_write(sacd_overlay_file_t *file, const void *buffer,
                       size_t size, uint64_t offset, size_t *bytes_written)
{
    if (!file || !buffer || !bytes_written) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    *bytes_written = 0;

    if (!(file->open_flags & SACD_OVERLAY_OPEN_WRITE)) {
        return SACD_OVERLAY_ERROR_ACCESS;
    }

    if (file->source == SACD_OVERLAY_SOURCE_PASSTHROUGH) {
        /* Passthrough write */
        if (!file->passthrough.fp) {
            return SACD_OVERLAY_ERROR_IO;
        }

        if (sa_fseek64(file->passthrough.fp, (int64_t)offset, SEEK_SET) != 0) {
            return SACD_OVERLAY_ERROR_IO;
        }

        size_t written = fwrite(buffer, 1, size, file->passthrough.fp);
        *bytes_written = written;
        return SACD_OVERLAY_OK;
    } else {
        /* Virtual file - only ID3 region is writable */
        if (!file->virt.vfs_file || !file->virt.mount) {
            return SACD_OVERLAY_ERROR_IO;
        }

        sacd_vfs_file_info_t info;
        if (sacd_vfs_file_get_info(file->virt.vfs_file, &info) != SACD_VFS_OK) {
            return SACD_OVERLAY_ERROR_IO;
        }

        /* Handle writes that span or precede the ID3 region */
        uint64_t write_end = offset + size;

        if (write_end <= info.metadata_offset) {
            /* Write is entirely in header/audio region - silently accept but do nothing */
            *bytes_written = size;
            return SACD_OVERLAY_OK;
        }

        /* Calculate what portion of the write is in the ID3 region */
        uint64_t id3_start = (offset < info.metadata_offset) ? info.metadata_offset : offset;
        size_t skip_bytes = (size_t)(id3_start - offset);  /* Bytes to skip from buffer */
        size_t id3_write_size = size - skip_bytes;         /* Bytes to actually write */

        /* Buffer the write for later commit */
        size_t id3_offset = (size_t)(id3_start - info.metadata_offset);
        size_t required_size = id3_offset + id3_write_size;

        if (required_size > file->virt.id3_write_len) {
            uint8_t *new_buf = sa_realloc(file->virt.id3_write_buf, required_size);
            if (!new_buf) {
                return SACD_OVERLAY_ERROR_MEMORY;
            }
            /* Zero fill gap if extending */
            if (file->virt.id3_write_len < id3_offset) {
                memset(new_buf + file->virt.id3_write_len, 0,
                       id3_offset - file->virt.id3_write_len);
            }
            file->virt.id3_write_buf = new_buf;
            file->virt.id3_write_len = required_size;
        }

        memcpy(file->virt.id3_write_buf + id3_offset,
               (const uint8_t *)buffer + skip_bytes, id3_write_size);
        file->virt.id3_dirty = 1;
        *bytes_written = size;
        return SACD_OVERLAY_OK;
    }
}

int sacd_overlay_flush(sacd_overlay_file_t *file)
{
    if (!file) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    if (file->source == SACD_OVERLAY_SOURCE_PASSTHROUGH) {
        if (file->passthrough.fp) {
            fflush(file->passthrough.fp);
        }
        return SACD_OVERLAY_OK;
    }

    /* Virtual file - commit ID3 changes */
    if (file->virt.id3_dirty && file->virt.id3_write_buf && file->virt.mount) {
        /* Get track info from file path */
        /* Path format: /parent/Album/Stereo/01. Track.dsf */
        sacd_vfs_file_info_t info;
        if (sacd_vfs_file_get_info(file->virt.vfs_file, &info) != SACD_VFS_OK) {
            return SACD_OVERLAY_ERROR_IO;
        }

        sacd_vfs_ctx_t *vfs = file->virt.mount->vfs;
        if (!vfs) {
            return SACD_OVERLAY_ERROR_IO;
        }

        /* Determine area and track from path */
        sacd_vfs_area_t area = SACD_VFS_AREA_STEREO;
        if (strstr(file->vpath, "Multi-channel") != NULL) {
            area = SACD_VFS_AREA_MULTICHANNEL;
        }

        /* Extract track number from filename (NN. ...) */
        const char *fname = strrchr(file->vpath, '/');
        if (!fname) fname = file->vpath;
        else fname++;

        uint8_t track_num = 0;
        sscanf(fname, "%hhu.", &track_num);

        if (track_num > 0) {
            /* Set ID3 overlay */
            int result = sacd_vfs_set_id3_overlay(vfs, area, track_num,
                                                   file->virt.id3_write_buf,
                                                   file->virt.id3_write_len);
            if (result == SACD_VFS_OK) {
                /* Save to XML sidecar */
                sacd_vfs_save_id3_overlay(vfs);
            }
        }

        file->virt.id3_dirty = 0;
    }

    return SACD_OVERLAY_OK;
}

int sacd_overlay_fstat(sacd_overlay_file_t *file, sacd_overlay_entry_t *entry)
{
    if (!file || !entry) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    return sacd_overlay_stat(file->ctx, file->vpath, entry);
}

int sacd_overlay_file_size(sacd_overlay_file_t *file, uint64_t *size)
{
    if (!file || !size) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    if (file->source == SACD_OVERLAY_SOURCE_PASSTHROUGH) {
        struct stat st;
        if (stat(file->passthrough.source_path, &st) != 0) {
            return SACD_OVERLAY_ERROR_IO;
        }
        *size = st.st_size;
    } else {
        sacd_vfs_file_info_t info;
        if (sacd_vfs_file_get_info(file->virt.vfs_file, &info) != SACD_VFS_OK) {
            return SACD_OVERLAY_ERROR_IO;
        }
        *size = info.total_size;
    }

    return SACD_OVERLAY_OK;
}

/* =============================================================================
 * ISO Management - Public API
 * ===========================================================================*/

int sacd_overlay_get_mounted_iso_count(sacd_overlay_ctx_t *ctx)
{
    if (!ctx) return 0;

    int count = 0;
    mtx_lock(&ctx->iso_table_lock);

    for (int i = 0; i < ctx->iso_count; i++) {
        if (ctx->iso_mounts[i] && ctx->iso_mounts[i]->vfs) {
            count++;
        }
    }

    mtx_unlock(&ctx->iso_table_lock);
    return count;
}

int sacd_overlay_flush_all(sacd_overlay_ctx_t *ctx)
{
    if (!ctx) return SACD_OVERLAY_ERROR_INVALID_PARAMETER;

    mtx_lock(&ctx->iso_table_lock);

    for (int i = 0; i < ctx->iso_count; i++) {
        iso_mount_t *mount = ctx->iso_mounts[i];
        if (mount && mount->vfs) {
            if (sacd_vfs_has_unsaved_id3_changes(mount->vfs)) {
                sacd_vfs_save_id3_overlay(mount->vfs);
            }
        }
    }

    mtx_unlock(&ctx->iso_table_lock);
    return SACD_OVERLAY_OK;
}

int sacd_overlay_cleanup_idle(sacd_overlay_ctx_t *ctx)
{
    if (!ctx || ctx->cache_timeout_seconds <= 0) return 0;

    time_t now = time(NULL);
    int cleaned = 0;

    mtx_lock(&ctx->iso_table_lock);

    for (int i = 0; i < ctx->iso_count; i++) {
        iso_mount_t *mount = ctx->iso_mounts[i];
        if (!mount || !mount->vfs) continue;

        if (mount->ref_count <= 0 &&
            (now - mount->last_access) > ctx->cache_timeout_seconds) {
            /* Save any pending changes */
            if (sacd_vfs_has_unsaved_id3_changes(mount->vfs)) {
                sacd_vfs_save_id3_overlay(mount->vfs);
            }

            /* Close VFS */
            mtx_lock(&mount->mount_lock);
            sacd_vfs_close(mount->vfs);
            sacd_vfs_destroy(mount->vfs);
            mount->vfs = NULL;
            mtx_unlock(&mount->mount_lock);
            cleaned++;
        }
    }

    mtx_unlock(&ctx->iso_table_lock);
    return cleaned;
}
