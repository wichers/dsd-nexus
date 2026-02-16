/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD Overlay VFS - ISO Management
 * Handles mounting, caching, and lifecycle of SACD ISO contexts.
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
#include <libsautil/log.h>
#include <libsautil/sastring.h>
#include <libsautil/compat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#endif

/* =============================================================================
 * ISO Mount Management
 * ===========================================================================*/

/**
 * Find an existing ISO mount by its source path.
 */
iso_mount_t *_overlay_find_iso_mount(sacd_overlay_ctx_t *ctx, const char *iso_path)
{
    if (!ctx || !iso_path) return NULL;

    /* Caller should hold iso_table_lock, but we'll be defensive */

    for (int i = 0; i < ctx->iso_count; i++) {
        iso_mount_t *mount = ctx->iso_mounts[i];
        if (mount && strcmp(mount->iso_path, iso_path) == 0) {
            return mount;
        }
    }
    return NULL;
}

/**
 * Find an ISO mount that matches a virtual path.
 *
 * The virtual path might be:
 * - The ISO folder itself: /parent/Album
 * - Inside the ISO: /parent/Album/Stereo/01. Track.dsf
 */
iso_mount_t *_overlay_find_iso_by_vpath(sacd_overlay_ctx_t *ctx, const char *vpath)
{
    if (!ctx || !vpath) return NULL;

    /* Normalize the path */
    char norm_path[SACD_OVERLAY_MAX_PATH];
    sa_strlcpy(norm_path, vpath, sizeof(norm_path));

    /* Convert backslashes to forward slashes */
    for (char *p = norm_path; *p; p++) {
        if (*p == '\\') *p = '/';
    }

    /* Remove trailing slash */
    size_t len = strlen(norm_path);
    while (len > 1 && norm_path[len - 1] == '/') {
        norm_path[--len] = '\0';
    }

    mtx_lock(&ctx->iso_table_lock);

    iso_mount_t *found = NULL;
    size_t best_match_len = 0;

    for (int i = 0; i < ctx->iso_count; i++) {
        iso_mount_t *mount = ctx->iso_mounts[i];
        if (!mount) continue;

        /* Build expected virtual path for this ISO folder */
        char iso_vpath[SACD_OVERLAY_MAX_PATH];
        if (strcmp(mount->parent_vpath, "/") == 0) {
            /* Validate: "/" + display_name fits in buffer */
            size_t display_len = strlen(mount->display_name);
            if (1 + display_len >= SACD_OVERLAY_MAX_PATH) {
                continue;  /* Path too long, skip this mount */
            }
            sa_snprintf(iso_vpath, sizeof(iso_vpath), "/%s", mount->display_name);
        } else {
            /* Validate: parent_vpath + "/" + display_name fits in buffer */
            size_t parent_len = strlen(mount->parent_vpath);
            size_t display_len = strlen(mount->display_name);
            if (parent_len + 1 + display_len >= SACD_OVERLAY_MAX_PATH) {
                continue;  /* Path too long, skip this mount */
            }
            sa_snprintf(iso_vpath, sizeof(iso_vpath), "%s/%s",
                        mount->parent_vpath, mount->display_name);
        }

        size_t iso_vpath_len = strlen(iso_vpath);

        /* Check if vpath starts with or equals iso_vpath */
        if (strncmp(norm_path, iso_vpath, iso_vpath_len) == 0) {
            /* Must be exact match or followed by / */
            if (norm_path[iso_vpath_len] == '\0' ||
                norm_path[iso_vpath_len] == '/') {
                /* Prefer longer matches (more specific paths) */
                if (iso_vpath_len > best_match_len) {
                    found = mount;
                    best_match_len = iso_vpath_len;
                }
            }
        }
    }

    mtx_unlock(&ctx->iso_table_lock);

    return found;
}

/**
 * Get or create an ISO mount entry.
 */
iso_mount_t *_overlay_get_or_create_iso(sacd_overlay_ctx_t *ctx,
                                         const char *iso_path,
                                         const char *parent_vpath,
                                         const char *display_name,
                                         int collision_index)
{
    if (!ctx || !iso_path || !parent_vpath || !display_name) {
        return NULL;
    }

    mtx_lock(&ctx->iso_table_lock);

    /* Check if already exists */
    iso_mount_t *mount = NULL;
    for (int i = 0; i < ctx->iso_count; i++) {
        if (ctx->iso_mounts[i] && strcmp(ctx->iso_mounts[i]->iso_path, iso_path) == 0) {
            mount = ctx->iso_mounts[i];
            break;
        }
    }

    if (!mount) {
        /* Check user-configured soft limit */
        if (ctx->max_open_isos > 0 && ctx->iso_count >= ctx->max_open_isos) {
            sa_log(NULL, SA_LOG_WARNING,
                   "overlay: user limit reached (%d/%d), cannot mount: %s\n",
                   ctx->iso_count, ctx->max_open_isos, iso_path);
            mtx_unlock(&ctx->iso_table_lock);
            return NULL;
        }

        /* Grow array if needed */
        if (ctx->iso_count >= ctx->iso_capacity) {
            int new_cap = ctx->iso_capacity ? ctx->iso_capacity * 2
                                            : ISO_MOUNTS_INITIAL_CAPACITY;
            iso_mount_t **new_arr = sa_realloc_array(
                ctx->iso_mounts, (size_t)new_cap, sizeof(iso_mount_t *));
            if (!new_arr) {
                sa_log(NULL, SA_LOG_ERROR,
                       "overlay: failed to grow mount table from %d to %d\n",
                       ctx->iso_capacity, new_cap);
                mtx_unlock(&ctx->iso_table_lock);
                return NULL;
            }
            /* Zero the new slots */
            memset(new_arr + ctx->iso_capacity, 0,
                   (size_t)(new_cap - ctx->iso_capacity) * sizeof(iso_mount_t *));
            ctx->iso_mounts = new_arr;
            ctx->iso_capacity = new_cap;
        }

        /* Create new mount entry */
        mount = sa_mallocz(sizeof(iso_mount_t));
        if (!mount) {
            mtx_unlock(&ctx->iso_table_lock);
            return NULL;
        }

        sa_strlcpy(mount->iso_path, iso_path, sizeof(mount->iso_path));
        sa_strlcpy(mount->display_name, display_name, sizeof(mount->display_name));
        sa_strlcpy(mount->parent_vpath, parent_vpath, sizeof(mount->parent_vpath));
        mount->collision_index = collision_index;
        mount->vfs = NULL;  /* Lazy loaded */
        mount->ref_count = 0;
        mount->last_access = time(NULL);

        if (mtx_init(&mount->mount_lock, mtx_plain) != thrd_success) {
            sa_free(mount);
            mtx_unlock(&ctx->iso_table_lock);
            return NULL;
        }

        ctx->iso_mounts[ctx->iso_count++] = mount;

        sa_log(NULL, SA_LOG_VERBOSE,
               "overlay: registered ISO #%d: %s\n",
               ctx->iso_count, display_name);
    }

    mtx_unlock(&ctx->iso_table_lock);

    return mount;
}

/**
 * Ensure the ISO is mounted (lazy loading).
 */
sacd_vfs_ctx_t *_overlay_ensure_iso_mounted(sacd_overlay_ctx_t *ctx, iso_mount_t *mount)
{
    if (!ctx || !mount) return NULL;

    mtx_lock(&mount->mount_lock);

    if (!mount->vfs) {
        /* Create VFS context */
        mount->vfs = sacd_vfs_create();
        if (mount->vfs) {
            /* Apply area visibility settings from overlay context */
            sacd_vfs_set_area_visibility(mount->vfs, SACD_VFS_AREA_STEREO,
                                          ctx->stereo_visible);
            sacd_vfs_set_area_visibility(mount->vfs, SACD_VFS_AREA_MULTICHANNEL,
                                          ctx->multichannel_visible);

            int result = sacd_vfs_open(mount->vfs, mount->iso_path);
            if (result != SACD_VFS_OK) {
                sacd_vfs_destroy(mount->vfs);
                mount->vfs = NULL;
            }
        }
    }

    sacd_vfs_ctx_t *vfs = mount->vfs;
    mount->last_access = time(NULL);

    mtx_unlock(&mount->mount_lock);

    return vfs;
}

/**
 * Release a reference to an ISO mount.
 */
void _overlay_release_iso(iso_mount_t *mount)
{
    if (!mount) return;

    mtx_lock(&mount->mount_lock);

    if (mount->ref_count > 0) {
        mount->ref_count--;
    }

    mtx_unlock(&mount->mount_lock);
}

/**
 * Cleanup and free an ISO mount.
 */
void _overlay_cleanup_iso(sacd_overlay_ctx_t *ctx, iso_mount_t *mount)
{
    if (!ctx || !mount) return;

    mtx_lock(&ctx->iso_table_lock);

    /* Find and remove from array */
    for (int i = 0; i < ctx->iso_count; i++) {
        if (ctx->iso_mounts[i] == mount) {
            /* Close VFS */
            mtx_lock(&mount->mount_lock);
            if (mount->vfs) {
                if (sacd_vfs_has_unsaved_id3_changes(mount->vfs)) {
                    sacd_vfs_save_id3_overlay(mount->vfs);
                }
                sacd_vfs_close(mount->vfs);
                sacd_vfs_destroy(mount->vfs);
                mount->vfs = NULL;
            }
            mtx_unlock(&mount->mount_lock);
            mtx_destroy(&mount->mount_lock);

            /* Remove from array */
            for (int j = i; j < ctx->iso_count - 1; j++) {
                ctx->iso_mounts[j] = ctx->iso_mounts[j + 1];
            }
            ctx->iso_mounts[--ctx->iso_count] = NULL;

            sa_free(mount);
            break;
        }
    }

    mtx_unlock(&ctx->iso_table_lock);
}

/* =============================================================================
 * Directory Scanning
 * ===========================================================================*/

typedef int (*dir_scan_callback_t)(const char *name, int is_dir, void *userdata);

/**
 * Scan a source directory and call callback for each entry.
 */
int _overlay_scan_source_dir(sacd_overlay_ctx_t *ctx, const char *source_path,
                             dir_scan_callback_t callback, void *userdata)
{
    (void)ctx;  /* May be used for filtering in future */

    if (!source_path || !callback) {
        return SACD_OVERLAY_ERROR_INVALID_PARAMETER;
    }

#ifdef _WIN32
    /* Windows implementation using FindFirstFile/FindNextFile */
    char search_path[SACD_OVERLAY_MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", source_path);

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return SACD_OVERLAY_ERROR_NOT_FOUND;
        }
        return SACD_OVERLAY_ERROR_IO;
    }

    do {
        int is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        callback(find_data.cFileName, is_dir, userdata);
    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
    return SACD_OVERLAY_OK;

#else
    /* POSIX implementation using opendir/readdir */
    DIR *dir = opendir(source_path);
    if (!dir) {
        return SACD_OVERLAY_ERROR_NOT_FOUND;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Determine if directory */
        int is_dir = 0;
#ifdef _DIRENT_HAVE_D_TYPE
        if (entry->d_type == DT_DIR) {
            is_dir = 1;
        } else if (entry->d_type == DT_UNKNOWN) {
            /* Fall back to stat */
#endif
            char full_path[SACD_OVERLAY_MAX_PATH];
            snprintf(full_path, sizeof(full_path), "%s/%s", source_path, entry->d_name);
            struct stat st;
            if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                is_dir = 1;
            }
#ifdef _DIRENT_HAVE_D_TYPE
        }
#endif

        callback(entry->d_name, is_dir, userdata);
    }

    closedir(dir);
    return SACD_OVERLAY_OK;
#endif
}
