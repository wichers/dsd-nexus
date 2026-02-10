/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD Overlay VFS - Path Resolution
 * Handles path parsing, translation, and ISO file detection.
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

#include <libsacd/sacd.h>
#include <libsautil/sastring.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

/* =============================================================================
 * Internal Helpers
 * ===========================================================================*/

/**
 * Normalize a path: convert backslashes to forward slashes (internal format)
 */
static void _normalize_path(char *path)
{
    while (*path) {
        if (*path == '\\') *path = '/';
        path++;
    }
}

/**
 * Convert internal path to native path separator
 */
static void _to_native_path(char *path)
{
#ifdef _WIN32
    while (*path) {
        if (*path == '/') *path = '\\';
        path++;
    }
#else
    (void)path;  /* Already uses forward slashes */
#endif
}

/* =============================================================================
 * Path Resolution Functions
 * ===========================================================================*/

/**
 * Parse a virtual path into parent directory and filename components.
 */
int _overlay_parse_path(sacd_overlay_ctx_t *ctx, const char *vpath,
                        char *parent_dir, size_t parent_size,
                        char *filename, size_t filename_size)
{
    (void)ctx;

    if (!vpath || !parent_dir || !filename) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    /* Make a working copy */
    char path_copy[SACD_OVERLAY_MAX_PATH];
    sa_strlcpy(path_copy, vpath, sizeof(path_copy));
    _normalize_path(path_copy);

    /* Remove trailing slashes */
    size_t len = strlen(path_copy);
    while (len > 1 && path_copy[len - 1] == '/') {
        path_copy[--len] = '\0';
    }

    /* Handle root */
    if (len == 0 || strcmp(path_copy, "/") == 0) {
        sa_strlcpy(parent_dir, "/", parent_size);
        filename[0] = '\0';
        return SACD_OVERLAY_OK;
    }

    /* Find last separator */
    const char *last_sep = strrchr(path_copy, '/');
    if (!last_sep) {
        /* No separator - entire path is filename, parent is root */
        sa_strlcpy(parent_dir, "/", parent_size);
        sa_strlcpy(filename, path_copy, filename_size);
    } else if (last_sep == path_copy) {
        /* Separator at start - parent is root */
        sa_strlcpy(parent_dir, "/", parent_size);
        sa_strlcpy(filename, last_sep + 1, filename_size);
    } else {
        /* Normal case */
        size_t parent_len = last_sep - path_copy;
        if (parent_len >= parent_size) parent_len = parent_size - 1;
        memcpy(parent_dir, path_copy, parent_len);
        parent_dir[parent_len] = '\0';

        sa_strlcpy(filename, last_sep + 1, filename_size);
    }

    return SACD_OVERLAY_OK;
}

/**
 * Build the source filesystem path from a virtual path.
 */
int _overlay_build_source_path(sacd_overlay_ctx_t *ctx, const char *vpath,
                               char *source_path, size_t size)
{
    if (!ctx || !vpath || !source_path || size == 0) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

    /* Make a working copy */
    char path_copy[SACD_OVERLAY_MAX_PATH];
    sa_strlcpy(path_copy, vpath, sizeof(path_copy));
    _normalize_path(path_copy);

    /* Skip leading slash */
    const char *rel_path = path_copy;
    while (*rel_path == '/') rel_path++;

    /* Build full path */
    if (*rel_path == '\0') {
        /* Root - return source directory */
        sa_strlcpy(source_path, ctx->source_dir, size);
    } else {
        int written = snprintf(source_path, size, "%s%c%s",
                               ctx->source_dir, PATH_SEPARATOR, rel_path);
        if (written < 0 || (size_t)written >= size) {
            return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
        }
    }

    source_path[size - 1] = '\0';

    /* Convert to native path separators */
    _to_native_path(source_path);

    return SACD_OVERLAY_OK;
}

/**
 * Check if a file path looks like an ISO file based on extension.
 */
int _overlay_is_iso_file(const char *path, int ext_mask)
{
    if (!path) return 0;

    const char *ext = strrchr(path, '.');
    if (!ext) return 0;

    if ((ext_mask & SACD_OVERLAY_EXT_ISO) && strcmp(ext, ".iso") == 0) {
        return 1;
    }
    if ((ext_mask & SACD_OVERLAY_EXT_ISO_UPPER) && strcmp(ext, ".ISO") == 0) {
        return 1;
    }

    /* Also check case-insensitively if both flags are set */
    if ((ext_mask & (SACD_OVERLAY_EXT_ISO | SACD_OVERLAY_EXT_ISO_UPPER)) ==
        (SACD_OVERLAY_EXT_ISO | SACD_OVERLAY_EXT_ISO_UPPER)) {
        if (sa_strcasecmp(ext, ".iso") == 0) {
            return 1;
        }
    }

    return 0;
}

/**
 * Check if a file is a valid SACD ISO using libsacd.
 *
 * Instead of manually checking magic bytes (which can vary based on sector size),
 * we use libsacd to attempt opening the file. If libsacd can open and initialize
 * it successfully, it's a valid SACD image.
 */
int _overlay_check_sacd_magic(const char *path)
{
    if (!path) return 0;

    /* Use libsacd to check if this is a valid SACD image.
     * This handles different sector sizes and formats automatically.
     */
    sacd_t *reader = sacd_create();
    if (!reader) {
        fprintf(stderr, "OVERLAY DEBUG: Failed to create reader for: %s\n", path);
        return 0;
    }

    /* Try to initialize the reader with this file.
     * Use TOC copy 1 for both master and area (same as actual file open).
     */
    int result = sacd_init(reader, path, 1, 1);

    int is_sacd = (result == SACD_OK);

    fprintf(stderr, "OVERLAY DEBUG: libsacd check for %s: %s\n",
            path, is_sacd ? "VALID SACD" : "NOT SACD");

    /* Clean up */
    if (is_sacd) {
        sacd_close(reader);
    }
    sacd_destroy(reader);

    return is_sacd;
}
