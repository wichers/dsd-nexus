/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD FUSE Operations - Implementation
 * FUSE callback implementations for the SACD overlay VFS.
 * Works with both native libfuse3 (Linux/macOS) and WinFSP (Windows).
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

#include "fuse_ops.h"

#include <libsacdvfs/sacd_overlay.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

/* Global overlay context */
static sacd_overlay_ctx_t *g_overlay_ctx = NULL;

/* Periodic idle cleanup */
static time_t g_last_cleanup = 0;
#define CLEANUP_INTERVAL_SECONDS 60

static void _maybe_cleanup_idle(void)
{
    time_t now = time(NULL);
    if (g_overlay_ctx && (now - g_last_cleanup) >= CLEANUP_INTERVAL_SECONDS) {
        g_last_cleanup = now;
        sacd_overlay_cleanup_idle(g_overlay_ctx);
    }
}

/* =============================================================================
 * Context Management
 * ===========================================================================*/

void sacd_fuse_set_context(sacd_overlay_ctx_t *ctx)
{
    g_overlay_ctx = ctx;
}

sacd_overlay_ctx_t *sacd_fuse_get_context(void)
{
    return g_overlay_ctx;
}

/* =============================================================================
 * FUSE Callbacks
 * ===========================================================================*/

/**
 * Get file attributes (stat).
 */
static int fuse_sacd_getattr(const char *path, fuse_compat_stat_t *stbuf,
                             struct fuse_file_info *fi)
{
    (void)fi;  /* Not using file info for now */

    _maybe_cleanup_idle();

    if (!g_overlay_ctx) {
        return -EIO;
    }

    memset(stbuf, 0, sizeof(fuse_compat_stat_t));

    sacd_overlay_entry_t entry;
    int result = sacd_overlay_stat(g_overlay_ctx, path, &entry);
    if (result != SACD_OVERLAY_OK) {
        return -sacd_overlay_error_to_errno(result);
    }

    /* Fill stat structure */
    if (entry.type == SACD_OVERLAY_ENTRY_FILE) {
        stbuf->st_mode = S_IFREG | (entry.mode ? entry.mode : 0644);
        stbuf->st_nlink = 1;
        stbuf->st_size = (fuse_compat_off_t)entry.size;
    } else {
        stbuf->st_mode = S_IFDIR | (entry.mode ? entry.mode : 0755);
        stbuf->st_nlink = 2;
    }

    stbuf->st_uid = fuse_compat_getuid();
    stbuf->st_gid = fuse_compat_getgid();

    time_t now = time(NULL);
    FUSE_SET_STAT_ATIME(stbuf, entry.atime ? entry.atime : now);
    FUSE_SET_STAT_MTIME(stbuf, entry.mtime ? entry.mtime : now);
    FUSE_SET_STAT_CTIME(stbuf, entry.ctime ? entry.ctime : now);

    return 0;
}

/**
 * Readdir callback adapter.
 */
typedef struct {
    void *buf;
    fuse_fill_dir_t filler;
    int stopped;
} fuse_readdir_ctx_t;

static int fuse_readdir_callback(const sacd_overlay_entry_t *entry, void *userdata)
{
    fuse_readdir_ctx_t *ctx = (fuse_readdir_ctx_t *)userdata;

    fuse_compat_stat_t st;
    memset(&st, 0, sizeof(st));

    if (entry->type == SACD_OVERLAY_ENTRY_FILE) {
        st.st_mode = S_IFREG | (entry->mode ? entry->mode : 0644);
        st.st_size = (fuse_compat_off_t)entry->size;
    } else {
        st.st_mode = S_IFDIR | (entry->mode ? entry->mode : 0755);
    }

    /* fuse_fill_dir_t signature:
     * int (*)(void *buf, const char *name, const struct stat *stbuf,
     *         off_t off, enum fuse_fill_dir_flags flags)
     */
    if (ctx->filler(ctx->buf, entry->name, (const struct stat *)&st, 0, 0) != 0) {
        ctx->stopped = 1;
        return -1;  /* Buffer full */
    }

    return 0;
}

/**
 * Read directory contents.
 */
static int fuse_sacd_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                             fuse_compat_off_t offset, struct fuse_file_info *fi,
                             enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;

    if (!g_overlay_ctx) {
        return -EIO;
    }

    /* Add . and .. */
    fuse_compat_stat_t st;
    memset(&st, 0, sizeof(st));
    st.st_mode = S_IFDIR | 0755;

    filler(buf, ".", (const struct stat *)&st, 0, 0);
    filler(buf, "..", (const struct stat *)&st, 0, 0);

    /* List directory contents */
    fuse_readdir_ctx_t ctx = { buf, filler, 0 };
    int result = sacd_overlay_readdir(g_overlay_ctx, path,
                                       fuse_readdir_callback, &ctx);

    if (result < 0) {
        return -sacd_overlay_error_to_errno(result);
    }

    return 0;
}

/**
 * Open a file.
 */
static int fuse_sacd_open(const char *path, struct fuse_file_info *fi)
{
    if (!g_overlay_ctx) {
        return -EIO;
    }

    /* Determine open flags */
    int flags = SACD_OVERLAY_OPEN_READ;
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        flags |= SACD_OVERLAY_OPEN_WRITE;
    }

    sacd_overlay_file_t *file = NULL;
    int result = sacd_overlay_open(g_overlay_ctx, path, flags, &file);
    if (result != SACD_OVERLAY_OK) {
        return -sacd_overlay_error_to_errno(result);
    }

    fi->fh = (uint64_t)(uintptr_t)file;
    fi->direct_io = 1;  /* Bypass kernel cache for virtual files */
    fi->keep_cache = 0;

    return 0;
}

/**
 * Read from a file.
 */
static int fuse_sacd_read(const char *path, char *buf, size_t size,
                          fuse_compat_off_t offset, struct fuse_file_info *fi)
{
    (void)path;

    if (!fi->fh) {
        return -EBADF;
    }

    sacd_overlay_file_t *file = (sacd_overlay_file_t *)(uintptr_t)fi->fh;
    size_t bytes_read = 0;

    int result = sacd_overlay_read(file, buf, size, (uint64_t)offset, &bytes_read);
    if (result != SACD_OVERLAY_OK) {
        return -sacd_overlay_error_to_errno(result);
    }

    return (int)bytes_read;
}

/**
 * Write to a file.
 */
static int fuse_sacd_write(const char *path, const char *buf, size_t size,
                           fuse_compat_off_t offset, struct fuse_file_info *fi)
{
    (void)path;

    if (!fi->fh) {
        return -EBADF;
    }

    sacd_overlay_file_t *file = (sacd_overlay_file_t *)(uintptr_t)fi->fh;
    size_t bytes_written = 0;

    int result = sacd_overlay_write(file, buf, size, (uint64_t)offset, &bytes_written);
    if (result != SACD_OVERLAY_OK) {
        return -sacd_overlay_error_to_errno(result);
    }

    return (int)bytes_written;
}

/**
 * Flush file data.
 */
static int fuse_sacd_flush(const char *path, struct fuse_file_info *fi)
{
    (void)path;

    if (!fi->fh) {
        return 0;  /* No file handle, nothing to flush */
    }

    sacd_overlay_file_t *file = (sacd_overlay_file_t *)(uintptr_t)fi->fh;
    int result = sacd_overlay_flush(file);

    return result == SACD_OVERLAY_OK ? 0 : -sacd_overlay_error_to_errno(result);
}

/**
 * Release (close) a file.
 */
static int fuse_sacd_release(const char *path, struct fuse_file_info *fi)
{
    (void)path;

    if (!fi->fh) {
        return 0;
    }

    sacd_overlay_file_t *file = (sacd_overlay_file_t *)(uintptr_t)fi->fh;
    sacd_overlay_close(file);
    fi->fh = 0;

    return 0;
}

/**
 * Filesystem initialization.
 */
static void *fuse_sacd_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void)conn;

    cfg->kernel_cache = 1;      /* Enable kernel page cache (VFS content is static) */
    cfg->auto_cache = 0;        /* Don't auto-invalidate (content doesn't change) */
    cfg->entry_timeout = 300.0; /* 5 min entry cache (directory listing stable) */
    cfg->attr_timeout = 300.0;  /* 5 min attr cache (file sizes don't change) */
    cfg->negative_timeout = 60; /* 1 min negative cache (avoid re-probing missing) */

    return g_overlay_ctx;
}

/**
 * Filesystem cleanup.
 */
static void fuse_sacd_destroy(void *private_data)
{
    (void)private_data;
    /* Cleanup is handled by main */
}

/**
 * Check file access permissions.
 * Required by WinFSP to allow write access to files.
 */
static int fuse_sacd_access(const char *path, int mask)
{
    if (!g_overlay_ctx) {
        return -EIO;
    }

    sacd_overlay_entry_t entry;
    int result = sacd_overlay_stat(g_overlay_ctx, path, &entry);
    if (result != SACD_OVERLAY_OK) {
        return -ENOENT;
    }

    /* Check write permission for virtual files */
    if ((mask & W_OK) && !entry.writable) {
        return -EACCES;
    }

    return 0;  /* Allow access */
}

/**
 * Truncate a file.
 * For virtual DSF files, this is effectively a no-op since the file layout
 * is determined by the SACD content. We accept truncate calls to allow
 * applications that truncate before writing (common on Windows).
 */
static int fuse_sacd_truncate(const char *path, fuse_compat_off_t size,
                              struct fuse_file_info *fi)
{
    (void)path;
    (void)size;
    (void)fi;

    if (!g_overlay_ctx) {
        return -EIO;
    }

    /* For virtual files, truncate is a no-op - the ID3 region is managed
     * internally and the DSF structure must remain intact */
    return 0;
}

/* =============================================================================
 * Operations Structure
 * ===========================================================================*/

void sacd_fuse_init_ops(struct fuse_operations *ops)
{
    memset(ops, 0, sizeof(*ops));

    ops->getattr    = fuse_sacd_getattr;
    ops->readdir    = fuse_sacd_readdir;
    ops->open       = fuse_sacd_open;
    ops->read       = fuse_sacd_read;
    ops->write      = fuse_sacd_write;
    ops->flush      = fuse_sacd_flush;
    ops->release    = fuse_sacd_release;
    ops->access     = fuse_sacd_access;
    ops->truncate   = fuse_sacd_truncate;
    ops->init       = fuse_sacd_init;
    ops->destroy    = fuse_sacd_destroy;
}
