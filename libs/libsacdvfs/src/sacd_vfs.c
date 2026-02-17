/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD Virtual Filesystem - Implementation
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

#include <libsacdvfs/sacd_vfs.h>

/* SACD library headers */
#include <libsacd/sacd.h>
#include "sacd_id3.h"

/* DST decoder for compressed streams (single-threaded) */
#include <libdst/decoder.h>

/* Utility headers */
#include <libsautil/buffer.h>
#include <libsautil/mem.h>
#include <libsautil/reverse.h>
#include <libsautil/sastring.h>
#include <libsautil/sa_tpool.h>
#include <libsautil/base64.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __APPLE__
#include <libsautil/c11threads.h>
#else
#include <threads.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include <libsautil/sxmlc.h>

/* Performance profiling - set to 1 to enable timing output */
#define VFS_PROFILE_ENABLED 0

/* Debug output control - set to 1 to enable verbose debug output */
#define VFS_DEBUG_ENABLED 0

#if VFS_DEBUG_ENABLED
#define VFS_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define VFS_DEBUG(...) ((void)0)
#endif

/* Debug sequence counter */
static volatile uint32_t g_vfs_debug_seq = 0;

/* =============================================================================
 * Internal Types
 * ===========================================================================*/

/** Cached ID3 tag for a track */
typedef struct {
    uint8_t *data;
    size_t size;
    bool valid;
    bool dirty;      /* true if modified since load/save */
    bool from_xml;   /* true if loaded from XML sidecar */
} id3_cache_entry_t;

/** Area information cache */
typedef struct {
    bool available;
    uint8_t track_count;
    uint16_t channel_count;
    uint32_t sample_rate;
    sacd_vfs_frame_format_t frame_format;
    id3_cache_entry_t *id3_cache;  /* Array of track_count entries */
} area_info_t;

/* =============================================================================
 * Multi-threaded DST Decompression Types
 * ===========================================================================*/

/** Reader thread commands */
typedef enum {
    VFS_MT_CMD_NONE = 0,
    VFS_MT_CMD_SEEK,
    VFS_MT_CMD_SEEK_DONE,
    VFS_MT_CMD_CLOSE
} vfs_mt_cmd_t;

/** DST decode job (dispatched to thread pool workers) */
typedef struct {
    sa_buffer_ref_t *compressed_ref;    /* Pool ref for compressed DST frame */
    uint8_t *compressed_data;           /* == compressed_ref->data */
    int compressed_size;
    int channel_count;
    int sample_rate;
    uint32_t frame_number;
    sa_buffer_ref_t *decompressed_ref;  /* Pool ref for decompressed DSD frame */
    uint8_t *decompressed_data;         /* == decompressed_ref->data */
    int decompressed_size;
    int error_code;
    int is_eof;                         /* Sentinel: signals end of frames */
    sa_buffer_pool_t *decomp_pool;      /* Borrowed: pool for output allocation */
} vfs_dst_job_t;

/** Minimum queue depth for MT process queue */
#define VFS_MT_MIN_QUEUE_DEPTH 16

/** VFS context structure */
struct sacd_vfs_ctx {
    sacd_t *reader;
    char iso_path[SACD_VFS_MAX_PATH];
    char album_name[SACD_VFS_MAX_FILENAME];
    bool is_open;

    /* Area information */
    area_info_t areas[2];  /* [0]=stereo, [1]=multichannel */

    /* Area visibility settings */
    bool area_visible[2];  /* [0]=stereo visible, [1]=multichannel visible */
};


/** Virtual file handle */
struct sacd_vfs_file {
    sacd_vfs_ctx_t *ctx;
    sacd_vfs_area_t area;
    uint8_t track_num;

    /* Per-file SACD reader instance for concurrent access */
    sacd_t *reader;

    /* Virtual file layout */
    uint64_t position;          /* Current read position */
    sacd_vfs_file_info_t info;

    /* Pre-generated DSF header */
    uint8_t dsf_header[DSF_AUDIO_DATA_OFFSET + DSF_DATA_CHUNK_HEADER_SIZE];
    size_t dsf_header_size;

    /* Track timing */
    uint32_t start_frame;
    uint32_t end_frame;
    uint32_t current_frame;

    /* DST decoder (if needed) - single-threaded */
    dst_decoder_t *dst_decoder;
    uint8_t *dst_decode_buffer;     /* Buffer for decoded DSD frame */
    size_t dst_decode_buffer_size;

    /* Block accumulation buffers for DSF conversion.
     * DSF requires continuous DSD data in 4096-byte blocks per channel.
     * We accumulate data until we have complete blocks to output.
     */
    uint8_t *channel_buffers[MAX_CHANNEL_COUNT];
    size_t bytes_buffered;      /* Bytes buffered per channel (same for all) */

    /* Transformation output buffer (complete block groups) */
    uint8_t *transform_buffer;
    size_t transform_buffer_size;
    size_t transform_buffer_pos;
    size_t transform_buffer_len;

    /* Seek alignment - bytes to skip from output after seeking mid-audio */
    size_t seek_skip_bytes;

#if VFS_PROFILE_ENABLED
    /* Performance profiling accumulators (in QPC ticks) */
    int64_t prof_read_ticks;
    int64_t prof_decode_ticks;
    int64_t prof_transform_ticks;
    uint32_t prof_frame_count;
#endif

    /* Multi-threaded DST decompression */
    int mt_enabled;                 /* Non-zero if MT pipeline is active */
    sa_tpool *pool;                 /* Borrowed reference (not owned) */
    sa_tpool_process *process;      /* Per-file process queue */
    thrd_t reader_thread;           /* Dedicated reader thread */
    int reader_thread_active;       /* Non-zero if reader thread is running */
    mtx_t command_mtx;              /* Protects command and mt_seek_frame */
    cnd_t command_cnd;              /* Signals command changes */
    vfs_mt_cmd_t command;           /* Current command for reader thread */
    uint32_t mt_seek_frame;         /* Target frame for SEEK command */
    int mt_errcode;                 /* Error code from reader thread */
    sa_buffer_pool_t *compressed_pool;   /* Pool for compressed DST frame buffers */
    sa_buffer_pool_t *decompressed_pool; /* Pool for decompressed DSD frame buffers */
};

/* =============================================================================
 * Forward Declarations
 * ===========================================================================*/

static int _generate_dsf_header(sacd_vfs_file_t *file);
static int _calculate_virtual_file_size(sacd_vfs_file_t *file);
static int _read_header_region(sacd_vfs_file_t *file, uint8_t *buffer, size_t size, size_t *bytes_read);
static int _read_audio_region(sacd_vfs_file_t *file, uint8_t *buffer, size_t size, size_t *bytes_read);
static int _read_metadata_region(sacd_vfs_file_t *file, uint8_t *buffer, size_t size, size_t *bytes_read);
static int _transform_dsd_frame(sacd_vfs_file_t *file, const uint8_t *src, size_t src_len);
/* _sanitize_filename removed - using sa_sanitize_filename from libsautil */


/* ID3 overlay XML sidecar support */
static void _get_xml_sidecar_path(const char *iso_path, char *xml_path, size_t size);
static int _load_id3_overlay_xml(sacd_vfs_ctx_t *ctx);
static int _save_id3_overlay_xml(sacd_vfs_ctx_t *ctx);

/* Multi-threaded DST decompression */
static int _vfs_reader_thread(void *arg);
static void *_vfs_dst_decode_func(void *arg);
static void _vfs_job_cleanup(void *arg);
static void _vfs_result_cleanup(void *data);
static int _read_audio_region_mt(sacd_vfs_file_t *file, uint8_t *buffer,
                                  size_t size, size_t *bytes_read);

/* =============================================================================
 * Error Strings
 * ===========================================================================*/

static const char *_error_strings[] = {
    "Success",
    "Invalid parameter",
    "Not found",
    "I/O error",
    "Memory allocation error",
    "Not open",
    "Seek error",
    "Read error",
    "Format error",
    "DST decode error",
    "End of file"
};

const char *sacd_vfs_error_string(int error)
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

/* =============================================================================
 * VFS Context Management
 * ===========================================================================*/

sacd_vfs_ctx_t *sacd_vfs_create(void)
{
    sacd_vfs_ctx_t *ctx = sa_mallocz(sizeof(sacd_vfs_ctx_t));
    if (!ctx) {
        return NULL;
    }

    /* Default: both areas visible */
    ctx->area_visible[SACD_VFS_AREA_STEREO] = true;
    ctx->area_visible[SACD_VFS_AREA_MULTICHANNEL] = true;

    return ctx;
}

int sacd_vfs_open(sacd_vfs_ctx_t *ctx, const char *iso_path)
{
    if (!ctx || !iso_path) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (ctx->is_open) {
        sacd_vfs_close(ctx);
    }

    /* Store path */
    sa_strlcpy(ctx->iso_path, iso_path, sizeof(ctx->iso_path));

    /* Create SACD reader */
    ctx->reader = sacd_create();
    if (!ctx->reader) {
        return SACD_VFS_ERROR_MEMORY;
    }

    /* Initialize reader */
    int result = sacd_init(ctx->reader, iso_path, 1, 1);
    if (result != SACD_OK) {
        sacd_destroy(ctx->reader);
        ctx->reader = NULL;
        return SACD_VFS_ERROR_FORMAT;
    }

    /* Get album name */
    const char *album_text = NULL;
    result = sacd_get_album_text(ctx->reader, 1, ALBUM_TEXT_TYPE_TITLE, &album_text);
    if (result == SACD_OK && album_text && album_text[0]) {
        sa_strlcpy(ctx->album_name, album_text, sizeof(ctx->album_name));
    } else {
        /* Try disc title */
        result = sacd_get_disc_text(ctx->reader, 1, ALBUM_TEXT_TYPE_TITLE, &album_text);
        if (result == SACD_OK && album_text && album_text[0]) {
            sa_strlcpy(ctx->album_name, album_text, sizeof(ctx->album_name));
        } else {
            sa_strlcpy(ctx->album_name, "Unknown Album", sizeof(ctx->album_name));
        }
    }
    sa_sanitize_filename(ctx->album_name, sizeof(ctx->album_name));

    /* Check available areas and cache info */
    channel_t available_channels[2] = { 0 };
    uint16_t channel_count = 2;
    result = sacd_get_available_channel_types(ctx->reader, available_channels, &channel_count);

    VFS_DEBUG("VFS DEBUG: sacd_vfs_open: found %u areas\n", channel_count);

    for (uint16_t i = 0; i < channel_count && i < 2; i++) {
        int area_idx = (available_channels[i] == MULTI_CHANNEL) ? 1 : 0;
        ctx->areas[area_idx].available = true;

        VFS_DEBUG("VFS DEBUG: sacd_vfs_open: area[%u] = %s (channel_type=%d)\n",
                i, area_idx == 0 ? "STEREO" : "MULTICHANNEL", available_channels[i]);

        /* Select area to get info */
        int sel_result = sacd_select_channel_type(ctx->reader, available_channels[i]);
        if (sel_result != SACD_OK) {
            VFS_DEBUG("VFS DEBUG: sacd_vfs_open: select_channel_type failed: %d\n", sel_result);
            continue;
        }

        uint8_t track_count = 0;
        sacd_get_track_count(ctx->reader, &track_count);
        ctx->areas[area_idx].track_count = track_count;

        VFS_DEBUG("VFS DEBUG: sacd_vfs_open: area_idx=%d, select_result=%d, track_count=%u\n",
                area_idx, sel_result, track_count);

        uint16_t ch_count = 0;
        sacd_get_area_channel_count(ctx->reader, &ch_count);
        ctx->areas[area_idx].channel_count = ch_count;

        uint32_t sample_rate = 0;
        sacd_get_area_sample_frequency(ctx->reader, &sample_rate);
        ctx->areas[area_idx].sample_rate = sample_rate;

        frame_format_t fmt;
        sacd_get_area_frame_format_enum(ctx->reader, &fmt);
        ctx->areas[area_idx].frame_format = (sacd_vfs_frame_format_t)fmt;

        /* Allocate ID3 cache */
        if (track_count > 0) {
            ctx->areas[area_idx].id3_cache = sa_calloc(track_count, sizeof(id3_cache_entry_t));
            if (!ctx->areas[area_idx].id3_cache) {
                /* Allocation failed - clean up and return error */
                for (int j = 0; j < 2; j++) {
                    if (ctx->areas[j].id3_cache) {
                        sa_free(ctx->areas[j].id3_cache);
                        ctx->areas[j].id3_cache = NULL;
                    }
                }
                sacd_destroy(ctx->reader);
                ctx->reader = NULL;
                return SACD_VFS_ERROR_MEMORY;
            }
        }
    }

    ctx->is_open = true;

    /* Load ID3 overlays from XML sidecar file if present */
    _load_id3_overlay_xml(ctx);

    return SACD_VFS_OK;
}

int sacd_vfs_close(sacd_vfs_ctx_t *ctx)
{
    if (!ctx) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    /* Free ID3 caches */
    for (int i = 0; i < 2; i++) {
        if (ctx->areas[i].id3_cache) {
            for (uint8_t t = 0; t < ctx->areas[i].track_count; t++) {
                if (ctx->areas[i].id3_cache[t].data) {
                    sa_free(ctx->areas[i].id3_cache[t].data);
                }
            }
            sa_free(ctx->areas[i].id3_cache);
            ctx->areas[i].id3_cache = NULL;
        }
        ctx->areas[i].available = false;
        ctx->areas[i].track_count = 0;
    }

    if (ctx->reader) {
        sacd_close(ctx->reader);
        sacd_destroy(ctx->reader);
        ctx->reader = NULL;
    }

    ctx->is_open = false;
    return SACD_VFS_OK;
}

void sacd_vfs_destroy(sacd_vfs_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->is_open) {
        sacd_vfs_close(ctx);
    }

    sa_free(ctx);
}

/* =============================================================================
 * Directory Operations
 * ===========================================================================*/

int sacd_vfs_get_album_name(sacd_vfs_ctx_t *ctx, char *name, size_t size)
{
    if (!ctx || !name || size == 0) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->is_open) {
        return SACD_VFS_ERROR_NOT_OPEN;
    }

    sa_strlcpy(name, ctx->album_name, size);
    return SACD_VFS_OK;
}

bool sacd_vfs_has_area(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area)
{
    if (!ctx || !ctx->is_open || area > SACD_VFS_AREA_MULTICHANNEL) {
        return false;
    }
    return ctx->areas[area].available;
}

int sacd_vfs_set_area_visibility(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area, bool visible)
{
    if (!ctx || area > SACD_VFS_AREA_MULTICHANNEL) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }
    ctx->area_visible[area] = visible;
    return SACD_VFS_OK;
}

bool sacd_vfs_get_area_visibility(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area)
{
    if (!ctx || area > SACD_VFS_AREA_MULTICHANNEL) {
        return true;  /* Default to visible on error */
    }
    return ctx->area_visible[area];
}

bool sacd_vfs_should_show_area(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area)
{
    if (!ctx || !ctx->is_open || area > SACD_VFS_AREA_MULTICHANNEL) {
        return false;
    }

    /* Area must exist on disc */
    if (!ctx->areas[area].available) {
        return false;
    }

    /* If visibility is enabled, show it */
    if (ctx->area_visible[area]) {
        return true;
    }

    /* Fallback: if this is the only available area, show it anyway */
    sacd_vfs_area_t other_area = (area == SACD_VFS_AREA_STEREO)
        ? SACD_VFS_AREA_MULTICHANNEL
        : SACD_VFS_AREA_STEREO;

    if (!ctx->areas[other_area].available) {
        return true;  /* This is the only area, show it regardless */
    }

    return false;
}

int sacd_vfs_get_track_count(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area, uint8_t *track_count)
{
    if (!ctx || !track_count) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->is_open) {
        return SACD_VFS_ERROR_NOT_OPEN;
    }

    if (!ctx->areas[area].available) {
        return SACD_VFS_ERROR_NOT_FOUND;
    }

    *track_count = ctx->areas[area].track_count;
    return SACD_VFS_OK;
}

int sacd_vfs_get_track_filename(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area,
                                uint8_t track_num, char *filename, size_t size)
{
    if (!ctx || !filename || size == 0 || track_num == 0) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->is_open) {
        return SACD_VFS_ERROR_NOT_OPEN;
    }

    if (!ctx->areas[area].available || track_num > ctx->areas[area].track_count) {
        return SACD_VFS_ERROR_NOT_FOUND;
    }

    /* Select area and get track title - MUST select channel type first! */
    channel_t ch_type = (area == SACD_VFS_AREA_MULTICHANNEL) ? MULTI_CHANNEL : TWO_CHANNEL;
    int select_result = sacd_select_channel_type(ctx->reader, ch_type);
    if (select_result != SACD_OK) {
        VFS_DEBUG("VFS DEBUG: get_track_filename: FAILED to select ch_type=%d, result=%d\n",
                ch_type, select_result);
        /* Fallback to generic name */
        snprintf(filename, size, "%02d. Track %02d.dsf", track_num, track_num);
        return SACD_VFS_OK;
    }

    /* Debug: verify area selection */
    uint8_t verify_tracks = 0;
    sacd_get_track_count(ctx->reader, &verify_tracks);
    VFS_DEBUG("VFS DEBUG: get_track_filename: area=%d, ch_type=%d, tracks=%u (requested track %u)\n",
            area, ch_type, verify_tracks, track_num);

    const char *track_title = NULL;
    int result = sacd_get_track_text(ctx->reader, track_num, 1, TRACK_TYPE_TITLE, &track_title);

    char title_buf[SACD_VFS_MAX_FILENAME];
    if (result == SACD_OK && track_title && track_title[0]) {
        sa_strlcpy(title_buf, track_title, sizeof(title_buf));
        VFS_DEBUG("VFS DEBUG: get_track_filename: track %u raw title=\"%s\"\n", track_num, title_buf);
        sa_sanitize_filename(title_buf, sizeof(title_buf));
    } else {
        snprintf(title_buf, sizeof(title_buf), "Track %02d", track_num);
        VFS_DEBUG("VFS DEBUG: get_track_filename: track %u no title (result=%d)\n", track_num, result);
    }

    snprintf(filename, size, "%02d. %s.dsf", track_num, title_buf);
    return SACD_VFS_OK;
}

int sacd_vfs_readdir(sacd_vfs_ctx_t *ctx, const char *path,
                     sacd_vfs_readdir_callback_t callback, void *userdata)
{
    if (!ctx || !path || !callback) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->is_open) {
        return SACD_VFS_ERROR_NOT_OPEN;
    }

    sacd_vfs_entry_t entry;
    int count = 0;

    /* Root directory */
    if (strcmp(path, "/") == 0) {
        memset(&entry, 0, sizeof(entry));
        sa_strlcpy(entry.name, ctx->album_name, sizeof(entry.name));
        entry.type = SACD_VFS_ENTRY_DIRECTORY;
        entry.size = 0;
        if (callback(&entry, userdata) != 0) {
            return count;
        }
        count++;
        return count;
    }

    /* Album directory */
    char album_path[SACD_VFS_MAX_PATH];
    snprintf(album_path, sizeof(album_path), "/%s", ctx->album_name);

    if (strcmp(path, album_path) == 0 || strcmp(path, album_path + 1) == 0) {
        /* List available areas (with visibility and fallback logic) */
        if (sacd_vfs_should_show_area(ctx, SACD_VFS_AREA_STEREO)) {
            memset(&entry, 0, sizeof(entry));
            sa_strlcpy(entry.name, "Stereo", sizeof(entry.name));
            entry.type = SACD_VFS_ENTRY_DIRECTORY;
            if (callback(&entry, userdata) != 0) {
                return count;
            }
            count++;
        }

        if (sacd_vfs_should_show_area(ctx, SACD_VFS_AREA_MULTICHANNEL)) {
            memset(&entry, 0, sizeof(entry));
            sa_strlcpy(entry.name, "Multi-channel", sizeof(entry.name));
            entry.type = SACD_VFS_ENTRY_DIRECTORY;
            if (callback(&entry, userdata) != 0) {
                return count;
            }
            count++;
        }

        return count;
    }

    /* Area directories (Stereo or Multi-channel) */
    char stereo_path[SACD_VFS_MAX_PATH];
    char mc_path[SACD_VFS_MAX_PATH];
    snprintf(stereo_path, sizeof(stereo_path), "/%s/Stereo", ctx->album_name);
    snprintf(mc_path, sizeof(mc_path), "/%s/Multi-channel", ctx->album_name);

    sacd_vfs_area_t area = SACD_VFS_AREA_UNKNOWN;
    bool found = false;

    if (strstr(path, "Stereo") != NULL && sacd_vfs_should_show_area(ctx, SACD_VFS_AREA_STEREO)) {
        area = SACD_VFS_AREA_STEREO;
        found = true;
    } else if (strstr(path, "Multi-channel") != NULL && sacd_vfs_should_show_area(ctx, SACD_VFS_AREA_MULTICHANNEL)) {
        area = SACD_VFS_AREA_MULTICHANNEL;
        found = true;
    }

    if (found) {
        /* List tracks in this area */
        uint8_t track_count = ctx->areas[area].track_count;

        for (uint8_t t = 1; t <= track_count; t++) {
            memset(&entry, 0, sizeof(entry));

            sacd_vfs_get_track_filename(ctx, area, t, entry.name, SACD_VFS_MAX_FILENAME);
            entry.type = SACD_VFS_ENTRY_FILE;
            entry.track_num = t;
            entry.area = area;

            /* Calculate file size */
            sacd_vfs_file_t *file = NULL;
            char track_path[SACD_VFS_MAX_PATH];
            snprintf(track_path, sizeof(track_path), "%s/%s", path, entry.name);

            if (sacd_vfs_file_open(ctx, track_path, &file) == SACD_VFS_OK) {
                entry.size = file->info.total_size;
                sacd_vfs_file_close(file);
            }

            if (callback(&entry, userdata) != 0) {
                return count;
            }
            count++;
        }

        return count;
    }

    return SACD_VFS_ERROR_NOT_FOUND;
}

int sacd_vfs_stat(sacd_vfs_ctx_t *ctx, const char *path, sacd_vfs_entry_t *entry)
{
    if (!ctx || !path || !entry) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->is_open) {
        return SACD_VFS_ERROR_NOT_OPEN;
    }

    memset(entry, 0, sizeof(*entry));

    /* Root */
    if (strcmp(path, "/") == 0) {
        sa_strlcpy(entry->name, "/", sizeof(entry->name));
        entry->type = SACD_VFS_ENTRY_DIRECTORY;
        return SACD_VFS_OK;
    }

    /* Album directory */
    char album_path[SACD_VFS_MAX_PATH];
    snprintf(album_path, sizeof(album_path), "/%s", ctx->album_name);

    if (strcmp(path, album_path) == 0) {
        sa_strlcpy(entry->name, ctx->album_name, sizeof(entry->name));
        entry->type = SACD_VFS_ENTRY_DIRECTORY;
        return SACD_VFS_OK;
    }

    /* Area directories */
    char stereo_path[SACD_VFS_MAX_PATH];
    char mc_path[SACD_VFS_MAX_PATH];
    snprintf(stereo_path, sizeof(stereo_path), "/%s/Stereo", ctx->album_name);
    snprintf(mc_path, sizeof(mc_path), "/%s/Multi-channel", ctx->album_name);

    if (strcmp(path, stereo_path) == 0 && sacd_vfs_should_show_area(ctx, SACD_VFS_AREA_STEREO)) {
        sa_strlcpy(entry->name, "Stereo", sizeof(entry->name));
        entry->type = SACD_VFS_ENTRY_DIRECTORY;
        return SACD_VFS_OK;
    }

    if (strcmp(path, mc_path) == 0 && sacd_vfs_should_show_area(ctx, SACD_VFS_AREA_MULTICHANNEL)) {
        sa_strlcpy(entry->name, "Multi-channel", sizeof(entry->name));
        entry->type = SACD_VFS_ENTRY_DIRECTORY;
        return SACD_VFS_OK;
    }

    /* Try to open as file */
    sacd_vfs_file_t *file = NULL;
    int result = sacd_vfs_file_open(ctx, path, &file);
    if (result == SACD_VFS_OK) {
        /* Extract filename from path */
        const char *fname = strrchr(path, '/');
        if (fname) {
            fname++;
        } else {
            fname = path;
        }

        sa_strlcpy(entry->name, fname, sizeof(entry->name));
        entry->type = SACD_VFS_ENTRY_FILE;
        entry->size = file->info.total_size;
        entry->track_num = file->track_num;
        entry->area = file->area;

        sacd_vfs_file_close(file);
        return SACD_VFS_OK;
    }

    return SACD_VFS_ERROR_NOT_FOUND;
}

/* =============================================================================
 * File Operations
 * ===========================================================================*/

int sacd_vfs_file_open(sacd_vfs_ctx_t *ctx, const char *path, sacd_vfs_file_t **file)
{
    if (!ctx || !path || !file) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->is_open) {
        return SACD_VFS_ERROR_NOT_OPEN;
    }

    *file = NULL;

    /* Parse path to extract area and track number */
    sacd_vfs_area_t area;
    uint8_t track_num = 0;

    if (strstr(path, "Stereo") != NULL) {
        area = SACD_VFS_AREA_STEREO;
    } else if (strstr(path, "Multi-channel") != NULL) {
        area = SACD_VFS_AREA_MULTICHANNEL;
    } else {
        return SACD_VFS_ERROR_NOT_FOUND;
    }

    /* Check if area is visible (respects visibility settings and fallback) */
    if (!sacd_vfs_should_show_area(ctx, area)) {
        return SACD_VFS_ERROR_NOT_FOUND;
    }

    if (!ctx->areas[area].available) {
        return SACD_VFS_ERROR_NOT_FOUND;
    }

    /* Extract track number from filename (expect "NN. *.dsf") */
    const char *fname = strrchr(path, '/');
    if (fname) {
        fname++;
    } else {
        fname = path;
    }

    if (sscanf(fname, "%hhu.", &track_num) != 1 || track_num == 0) {
        return SACD_VFS_ERROR_NOT_FOUND;
    }

    if (track_num > ctx->areas[area].track_count) {
        return SACD_VFS_ERROR_NOT_FOUND;
    }

    /* Allocate file handle */
    sacd_vfs_file_t *f = sa_mallocz(sizeof(sacd_vfs_file_t));
    if (!f) {
        return SACD_VFS_ERROR_MEMORY;
    }

    f->ctx = ctx;
    f->area = area;
    f->track_num = track_num;
    f->position = 0;

    /* Create per-file SACD reader instance for concurrent access.
     * Each open file gets its own reader to avoid race conditions when
     * multiple files are read simultaneously (e.g., by audio players).
     */
    f->reader = sacd_create();
    if (!f->reader) {
        sa_free(f);
        return SACD_VFS_ERROR_MEMORY;
    }

    int result = sacd_init(f->reader, ctx->iso_path, 1, 1);
    if (result != SACD_OK) {
        VFS_DEBUG("VFS DEBUG: Reader init failed: result=%d, iso=%s\n", result, ctx->iso_path);
        sacd_destroy(f->reader);
        sa_free(f);
        return SACD_VFS_ERROR_FORMAT;
    }

    /* Debug: Show available areas */
    channel_t avail_channels[2];
    uint16_t avail_count = 2;
    sacd_get_available_channel_types(f->reader, avail_channels, &avail_count);
    VFS_DEBUG("VFS DEBUG: Reader init OK, available areas=%u\n", avail_count);

    /* Select area in this file's reader */
    channel_t ch_type = (area == SACD_VFS_AREA_MULTICHANNEL) ? MULTI_CHANNEL : TWO_CHANNEL;
    result = sacd_select_channel_type(f->reader, ch_type);
    if (result != SACD_OK) {
        VFS_DEBUG("VFS DEBUG: Failed to select channel type %d: result=%d, area=%d\n", ch_type, result, area);
        sacd_close(f->reader);
        sacd_destroy(f->reader);
        sa_free(f);
        return SACD_VFS_ERROR_NOT_FOUND;
    }

    /* Debug: Verify selection worked */
    uint8_t verify_tracks = 0;
    sacd_get_track_count(f->reader, &verify_tracks);
    VFS_DEBUG("VFS DEBUG: Channel type %d selected, track_count=%u\n", ch_type, verify_tracks);

    /* Get track info */
    f->info.channel_count = ctx->areas[area].channel_count;
    f->info.sample_rate = ctx->areas[area].sample_rate;
    f->info.frame_format = ctx->areas[area].frame_format;

    /* Get track frame range using per-file reader */
    uint32_t track_frame_length = 0;
    sacd_get_track_frame_length(f->reader, track_num, &track_frame_length);

    /* Get track start frame using index_start (like sacd-extract does) */
    uint32_t index_start = 0;
    result = sacd_get_track_index_start(f->reader, track_num, 1, &index_start);
    if (result != SACD_OK) {
        sacd_close(f->reader);
        sacd_destroy(f->reader);
        sa_free(f);
        return SACD_VFS_ERROR_READ;
    }

    f->start_frame = index_start;
    f->end_frame = index_start + track_frame_length;
    f->current_frame = f->start_frame;

#if VFS_DEBUG_ENABLED    
    /* Debug: print frame range info and pointers */
    uint32_t seq = ++g_vfs_debug_seq;
    VFS_DEBUG("VFS DEBUG [%u]: File OPEN track %u: file=%p, reader=%p, index_start=%u, end_frame=%u\n",
            seq, track_num, (void*)f, (void*)f->reader, index_start, f->end_frame);
#endif

    /* Calculate sample count and duration.
     * For 1-bit DSD: sample_count = bytes_per_channel * 8 (8 samples per byte)
     * SACD_FRAME_SIZE_64 = 4704 bytes per channel per frame
     */
    f->info.sample_count = (uint64_t)track_frame_length * SACD_FRAME_SIZE_64 * 8;
    f->info.duration_seconds = (double)track_frame_length / SACD_FRAMES_PER_SEC;

    /* Calculate virtual file size */
    result = _calculate_virtual_file_size(f);
    if (result != SACD_VFS_OK) {
        sacd_close(f->reader);
        sacd_destroy(f->reader);
        sa_free(f);
        return result;
    }

    /* Generate DSF header */
    result = _generate_dsf_header(f);
    if (result != SACD_VFS_OK) {
        sacd_close(f->reader);
        sacd_destroy(f->reader);
        sa_free(f);
        return result;
    }

    /* Allocate per-channel block accumulation buffers */
    f->bytes_buffered = 0;
    for (uint32_t ch = 0; ch < f->info.channel_count; ch++) {
        f->channel_buffers[ch] = sa_malloc(DSF_BLOCK_SIZE_PER_CHANNEL);
        if (!f->channel_buffers[ch]) {
            /* Free already allocated buffers */
            for (uint32_t j = 0; j < ch; j++) {
                sa_free(f->channel_buffers[j]);
            }
            sacd_close(f->reader);
            sacd_destroy(f->reader);
            sa_free(f);
            return SACD_VFS_ERROR_MEMORY;
        }
    }

    /* Allocate transformation output buffer (holds complete block groups) */
    size_t buffer_size = DSF_BLOCK_SIZE_PER_CHANNEL * f->info.channel_count;
    f->transform_buffer = sa_malloc(buffer_size);
    if (!f->transform_buffer) {
        for (uint32_t ch = 0; ch < f->info.channel_count; ch++) {
            sa_free(f->channel_buffers[ch]);
        }
        sacd_close(f->reader);
        sacd_destroy(f->reader);
        sa_free(f);
        return SACD_VFS_ERROR_MEMORY;
    }
    f->transform_buffer_size = buffer_size;
    f->transform_buffer_pos = 0;
    f->transform_buffer_len = 0;
    f->seek_skip_bytes = 0;

    /* Initialize DST decoder if needed (single-threaded) */
    if (f->info.frame_format == SACD_VFS_FRAME_DST) {

        /* Initialize decoder */
        dst_decoder_init(&f->dst_decoder, (int)f->info.channel_count, (int)f->info.sample_rate);

        /* Allocate decode output buffer.
         * DST frame decodes to SACD_FRAME_SIZE_64 bytes per channel.
         * DST_SAMPLES_PER_FRAME = 588 * 64 = 37632 one-bit samples per channel
         * nb_samples = 37632 / 8 = 4704 bytes per channel = SACD_FRAME_SIZE_64
         */
        f->dst_decode_buffer_size = SACD_FRAME_SIZE_64 * f->info.channel_count;
        f->dst_decode_buffer = sa_malloc(f->dst_decode_buffer_size);
        if (!f->dst_decode_buffer) {
            sa_free(f->dst_decoder);
            f->dst_decoder = NULL;
            sa_free(f->transform_buffer);
            for (uint32_t ch = 0; ch < f->info.channel_count; ch++) {
                sa_free(f->channel_buffers[ch]);
            }
            sacd_close(f->reader);
            sacd_destroy(f->reader);
            sa_free(f);
            return SACD_VFS_ERROR_MEMORY;
        }
    }

    *file = f;
    return SACD_VFS_OK;
}

int sacd_vfs_file_open_mt(sacd_vfs_ctx_t *ctx, const char *path,
                           sa_tpool *pool, sacd_vfs_file_t **file)
{
    /* Perform all common setup via the single-threaded open */
    int ret = sacd_vfs_file_open(ctx, path, file);
    if (ret != SACD_VFS_OK) {
        return ret;
    }

    sacd_vfs_file_t *f = *file;

    /* Only enable MT for DST-compressed tracks with a valid pool */
    if (pool == NULL || f->info.frame_format != SACD_VFS_FRAME_DST) {
        return SACD_VFS_OK;
    }

    /* Free the ST decoder - MT path uses per-job decoders in the worker pool */
    if (f->dst_decoder) {
        dst_decoder_close(f->dst_decoder);
        f->dst_decoder = NULL;
    }
    if (f->dst_decode_buffer) {
        sa_free(f->dst_decode_buffer);
        f->dst_decode_buffer = NULL;
        f->dst_decode_buffer_size = 0;
    }

    /* Initialize MT pipeline */
    f->pool = pool;
    int qsize = sa_tpool_size(pool) * 2;
    if (qsize < VFS_MT_MIN_QUEUE_DEPTH) {
        qsize = VFS_MT_MIN_QUEUE_DEPTH;
    }

    f->process = sa_tpool_process_init(pool, qsize, 0);
    if (!f->process) {
        sacd_vfs_file_close(f);
        *file = NULL;
        return SACD_VFS_ERROR_MEMORY;
    }

    /* Initialize command synchronization */
    if (mtx_init(&f->command_mtx, mtx_plain) != thrd_success) {
        sa_tpool_process_destroy(f->process);
        f->process = NULL;
        sacd_vfs_file_close(f);
        *file = NULL;
        return SACD_VFS_ERROR_MEMORY;
    }

    if (cnd_init(&f->command_cnd) != thrd_success) {
        mtx_destroy(&f->command_mtx);
        sa_tpool_process_destroy(f->process);
        f->process = NULL;
        sacd_vfs_file_close(f);
        *file = NULL;
        return SACD_VFS_ERROR_MEMORY;
    }

    f->command = VFS_MT_CMD_NONE;
    f->mt_errcode = 0;

    /* Create buffer pools for the MT pipeline */
    f->compressed_pool = sa_buffer_pool_init(SACD_MAX_DSD_SIZE, NULL);
    if (!f->compressed_pool) {
        cnd_destroy(&f->command_cnd);
        mtx_destroy(&f->command_mtx);
        sa_tpool_process_destroy(f->process);
        f->process = NULL;
        sacd_vfs_file_close(f);
        *file = NULL;
        return SACD_VFS_ERROR_MEMORY;
    }

    size_t decomp_size = (size_t)SACD_FRAME_SIZE_64 * f->info.channel_count;
    f->decompressed_pool = sa_buffer_pool_init(decomp_size, NULL);
    if (!f->decompressed_pool) {
        sa_buffer_pool_uninit(&f->compressed_pool);
        cnd_destroy(&f->command_cnd);
        mtx_destroy(&f->command_mtx);
        sa_tpool_process_destroy(f->process);
        f->process = NULL;
        sacd_vfs_file_close(f);
        *file = NULL;
        return SACD_VFS_ERROR_MEMORY;
    }

    /* Start the dedicated reader thread */
    int tret = thrd_create(&f->reader_thread, _vfs_reader_thread, f);
    if (tret != thrd_success) {
        sa_buffer_pool_uninit(&f->decompressed_pool);
        sa_buffer_pool_uninit(&f->compressed_pool);
        cnd_destroy(&f->command_cnd);
        mtx_destroy(&f->command_mtx);
        sa_tpool_process_destroy(f->process);
        f->process = NULL;
        sacd_vfs_file_close(f);
        *file = NULL;
        return SACD_VFS_ERROR_MEMORY;
    }

    f->reader_thread_active = 1;
    f->mt_enabled = 1;

    VFS_DEBUG("VFS DEBUG: MT pipeline started for track %u (pool_size=%d, qsize=%d)\n",
              f->track_num, sa_tpool_size(pool), qsize);

    return SACD_VFS_OK;
}

int sacd_vfs_file_close(sacd_vfs_file_t *file)
{
    if (!file) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

#if VFS_DEBUG_ENABLED    
    uint32_t seq = ++g_vfs_debug_seq;
    VFS_DEBUG("VFS DEBUG [%u]: File CLOSE track %u: file=%p, reader=%p\n",
            seq, file->track_num, (void*)file, (void*)file->reader);
#endif

#if VFS_PROFILE_ENABLED
    if (file->prof_frame_count > 0) {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        double read_ms = (double)file->prof_read_ticks * 1000.0 / (double)freq.QuadPart;
        double decode_ms = (double)file->prof_decode_ticks * 1000.0 / (double)freq.QuadPart;
        double transform_ms = (double)file->prof_transform_ticks * 1000.0 / (double)freq.QuadPart;
        fprintf(stderr, "\n=== VFS PROFILE (track %u, %u frames) ===\n", file->track_num, file->prof_frame_count);
        fprintf(stderr, "  Read frames:  %.1f ms (%.3f ms/frame)\n", read_ms, read_ms / file->prof_frame_count);
        fprintf(stderr, "  DST decode:   %.1f ms (%.3f ms/frame)\n", decode_ms, decode_ms / file->prof_frame_count);
        fprintf(stderr, "  Transform:    %.1f ms (%.3f ms/frame)\n", transform_ms, transform_ms / file->prof_frame_count);
        fprintf(stderr, "  Total:        %.1f ms\n", read_ms + decode_ms + transform_ms);
        fprintf(stderr, "===================================\n\n");
    }
#endif

    /* Shut down MT pipeline if active */
    if (file->mt_enabled) {
        VFS_DEBUG("VFS DEBUG: Shutting down MT pipeline for track %u\n", file->track_num);

        /* Signal reader thread to close */
        mtx_lock(&file->command_mtx);
        file->command = VFS_MT_CMD_CLOSE;
        cnd_signal(&file->command_cnd);
        mtx_unlock(&file->command_mtx);

        /* Wake reader if blocked on full queue + signal process queue shutdown */
        if (file->process) {
            sa_tpool_wake_dispatch(file->process);
            sa_tpool_process_shutdown(file->process);
        }

        /* Join reader thread */
        if (file->reader_thread_active) {
            thrd_join(file->reader_thread, NULL);
            file->reader_thread_active = 0;
        }

        /* Destroy process queue */
        if (file->process) {
            sa_tpool_process_destroy(file->process);
            file->process = NULL;
        }

        /* Destroy command synchronization primitives */
        cnd_destroy(&file->command_cnd);
        mtx_destroy(&file->command_mtx);

        /* Destroy buffer pools */
        if (file->compressed_pool) {
            sa_buffer_pool_uninit(&file->compressed_pool);
        }
        if (file->decompressed_pool) {
            sa_buffer_pool_uninit(&file->decompressed_pool);
        }

        file->pool = NULL;
        file->mt_enabled = 0;
    }

    /* Free DST decoder resources (ST path, or if MT init failed partway) */
    if (file->dst_decoder) {
        dst_decoder_close(file->dst_decoder);
        file->dst_decoder = NULL;
    }
    if (file->dst_decode_buffer) {
        sa_free(file->dst_decode_buffer);
        file->dst_decode_buffer = NULL;
    }

    if (file->transform_buffer) {
        sa_free(file->transform_buffer);
        file->transform_buffer = NULL;
    }

    /* Free per-channel block accumulation buffers */
    for (uint32_t ch = 0; ch < file->info.channel_count; ch++) {
        if (file->channel_buffers[ch]) {
            sa_free(file->channel_buffers[ch]);
            file->channel_buffers[ch] = NULL;
        }
    }

    /* Close and destroy per-file reader */
    if (file->reader) {
        sacd_close(file->reader);
        sacd_destroy(file->reader);
        file->reader = NULL;
    }

    sa_free(file);
    return SACD_VFS_OK;
}

int sacd_vfs_file_get_info(sacd_vfs_file_t *file, sacd_vfs_file_info_t *info)
{
    if (!file || !info) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    *info = file->info;
    return SACD_VFS_OK;
}

int sacd_vfs_file_read(sacd_vfs_file_t *file, uint8_t *buffer, size_t size, size_t *bytes_read)
{
    if (!file || !buffer || !bytes_read) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    *bytes_read = 0;

    if (file->position >= file->info.total_size) {
        return SACD_VFS_ERROR_EOF;
    }

    size_t total_read = 0;
    size_t remaining = size;

    while (remaining > 0 && file->position < file->info.total_size) {
        size_t chunk_read = 0;
        int result;

        if (file->position < file->dsf_header_size) {
            /* Reading from header region */
            result = _read_header_region(file, buffer + total_read, remaining, &chunk_read);
        } else if (file->position < file->info.metadata_offset) {
            /* Reading from audio data region */
            result = _read_audio_region(file, buffer + total_read, remaining, &chunk_read);
        } else {
            /* Reading from metadata region */
            result = _read_metadata_region(file, buffer + total_read, remaining, &chunk_read);
        }

        if (result != SACD_VFS_OK && result != SACD_VFS_ERROR_EOF) {
            if (total_read > 0) {
                break;  /* Return what we have */
            }
            return result;
        }

        total_read += chunk_read;
        remaining -= chunk_read;

        if (chunk_read == 0) {
            break;
        }
    }

    *bytes_read = total_read;
    return SACD_VFS_OK;
}

int sacd_vfs_file_seek(sacd_vfs_file_t *file, int64_t offset, int whence)
{
    if (!file) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    int64_t new_pos;

    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = (int64_t)file->position + offset;
        break;
    case SEEK_END:
        new_pos = (int64_t)file->info.total_size + offset;
        break;
    default:
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (new_pos < 0) {
        return SACD_VFS_ERROR_SEEK;
    }

    /* Fast path: position unchanged — skip expensive state reset and MT drain.
     * This is critical for FUSE/WinFSP which calls seek before every read,
     * even for sequential access patterns. Without this, every FUSE read
     * would drain the entire MT prefetch queue. */
    if ((uint64_t)new_pos == file->position) {
        return SACD_VFS_OK;
    }

    file->position = (uint64_t)new_pos;

    /* Invalidate transform buffer on seek */
    file->transform_buffer_pos = 0;
    file->transform_buffer_len = 0;
    file->seek_skip_bytes = 0;

    /* For single-threaded DST decoder, no special flush needed */

    /* Calculate new current frame based on position */
    if (file->position < file->dsf_header_size) {
        /* Seeking to header region - reset to start of track */
        file->current_frame = file->start_frame;
        file->bytes_buffered = 0;
    } else if (file->position < file->info.metadata_offset) {
        /* Seeking within audio region.
         *
         * KEY INSIGHT: SACD frames are 4704 bytes/channel, DSF blocks are 4096 bytes.
         * GCD(4704, 4096) = 32, so LCM = 602112 bytes per channel.
         * Frame and block boundaries only align every 128 frames (602112/4704).
         *
         * After processing F frames:
         * - Total raw bytes per channel: F * 4704
         * - Complete blocks output per channel: floor(F * 4704 / 4096)
         * - Bytes buffered per channel: (F * 4704) % 4096
         * - Total output bytes: floor(F * 4704 / 4096) * 4096 * channel_count
         */
        uint64_t audio_offset = file->position - file->dsf_header_size;
        uint32_t channel_count = file->info.channel_count;
        uint64_t bytes_per_block_group = (uint64_t)DSF_BLOCK_SIZE_PER_CHANNEL * channel_count;

        /* Both DSD and DST frames are independently decodable.
         * DST frames are self-contained (each frame has its own coding parameters).
         * We can seek to aligned boundaries and skip a small amount of output.
         *
         * FRAME_BLOCK_ALIGNMENT: frames where bytes_buffered = 0 (block-aligned)
         * This occurs when (F * 4704) % 4096 = 0, i.e., F is multiple of 128
         */
        {
            #define FRAME_BLOCK_ALIGNMENT 128

            /* Calculate output position per aligned frame group:
             * After 128 frames: output = floor(128 * 4704 / 4096) * 4096 * channels
             *                         = 147 * 4096 * channels = 147 blocks per channel
             */
            uint64_t output_per_alignment = (uint64_t)FRAME_BLOCK_ALIGNMENT * SACD_FRAME_SIZE_64 /
                                            DSF_BLOCK_SIZE_PER_CHANNEL * bytes_per_block_group;

            /* Find which alignment group contains our target */
            uint32_t alignment_group = (uint32_t)(audio_offset / output_per_alignment);
            uint32_t aligned_frame = alignment_group * FRAME_BLOCK_ALIGNMENT;
            uint64_t aligned_output_pos = alignment_group * output_per_alignment;

            /* Set frame position to this aligned boundary */
            file->current_frame = file->start_frame + aligned_frame;
            if (file->current_frame > file->end_frame) {
                file->current_frame = file->end_frame;
                aligned_output_pos = (uint64_t)(file->end_frame - file->start_frame) *
                                     SACD_FRAME_SIZE_64 / DSF_BLOCK_SIZE_PER_CHANNEL *
                                     bytes_per_block_group;
            }

            /* At an aligned frame boundary, bytes_buffered is 0 (clean state) */
            file->bytes_buffered = 0;

            /* Skip bytes from aligned position to target position */
            file->seek_skip_bytes = (size_t)(audio_offset - aligned_output_pos);

            #undef FRAME_BLOCK_ALIGNMENT
        }
    } else {
        /* Metadata region - reset audio state */
        file->current_frame = file->end_frame;
        file->bytes_buffered = 0;
    }

    /* For MT pipeline: signal the reader thread to seek to the computed frame.
     * The reader drains the process queue, resets, and resumes from the target.
     */
    if (file->mt_enabled) {
        mtx_lock(&file->command_mtx);
        file->mt_seek_frame = file->current_frame;
        file->command = VFS_MT_CMD_SEEK;
        cnd_signal(&file->command_cnd);
        mtx_unlock(&file->command_mtx);

        /* Wake reader if blocked on full queue */
        sa_tpool_wake_dispatch(file->process);

        /* Wait for the reader thread to finish draining and repositioning */
        mtx_lock(&file->command_mtx);
        while (file->command != VFS_MT_CMD_SEEK_DONE) {
            cnd_wait(&file->command_cnd, &file->command_mtx);
        }
        file->command = VFS_MT_CMD_NONE;
        mtx_unlock(&file->command_mtx);

        /* Clear error state from previous read */
        file->mt_errcode = 0;
    }

    return SACD_VFS_OK;
}

int sacd_vfs_file_tell(sacd_vfs_file_t *file, uint64_t *position)
{
    if (!file || !position) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    *position = file->position;
    return SACD_VFS_OK;
}

/* =============================================================================
 * ID3 Metadata Operations
 * ===========================================================================*/

int sacd_vfs_get_id3_tag(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area,
                         uint8_t track_num, uint8_t **buffer, size_t *size)
{
    if (!ctx || !buffer || !size || track_num == 0) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->is_open) {
        return SACD_VFS_ERROR_NOT_OPEN;
    }

    if (area > SACD_VFS_AREA_MULTICHANNEL) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->areas[area].available || track_num > ctx->areas[area].track_count) {
        return SACD_VFS_ERROR_NOT_FOUND;
    }

    if (!ctx->areas[area].id3_cache) {
        return SACD_VFS_ERROR_MEMORY;
    }

    *buffer = NULL;
    *size = 0;

    /* Check cache first */
    id3_cache_entry_t *cache = &ctx->areas[area].id3_cache[track_num - 1];
    if (cache->valid && cache->data) {
        uint8_t *copy = sa_malloc(cache->size);
        if (!copy) {
            return SACD_VFS_ERROR_MEMORY;
        }
        memcpy(copy, cache->data, cache->size);
        *buffer = copy;
        *size = cache->size;
        return SACD_VFS_OK;
    }

    /* Generate ID3 tag */
    channel_t ch_type = (area == SACD_VFS_AREA_MULTICHANNEL) ? MULTI_CHANNEL : TWO_CHANNEL;
    sacd_select_channel_type(ctx->reader, ch_type);

    /* Allocate temporary buffer for ID3 tag generation */
    uint8_t *temp_buffer = sa_malloc(16384);  /* 16KB should be enough */
    if (!temp_buffer) {
        return SACD_VFS_ERROR_MEMORY;
    }

    int tag_len = sacd_id3_tag_render(ctx->reader, temp_buffer, track_num);
    if (tag_len <= 0) {
        sa_free(temp_buffer);
        return SACD_VFS_ERROR_FORMAT;
    }

    /* Cache the result */
    cache->data = sa_malloc(tag_len);
    if (cache->data) {
        memcpy(cache->data, temp_buffer, tag_len);
        cache->size = tag_len;
        cache->valid = true;
    }

    /* Return a copy */
    uint8_t *result = sa_malloc(tag_len);
    if (!result) {
        sa_free(temp_buffer);
        return SACD_VFS_ERROR_MEMORY;
    }
    memcpy(result, temp_buffer, tag_len);
    sa_free(temp_buffer);

    *buffer = result;
    *size = tag_len;
    return SACD_VFS_OK;
}

int sacd_vfs_set_id3_overlay(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area,
                             uint8_t track_num, const uint8_t *buffer, size_t size)
{
    if (!ctx || !buffer || size == 0 || track_num == 0) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->is_open) {
        return SACD_VFS_ERROR_NOT_OPEN;
    }

    if (area > SACD_VFS_AREA_MULTICHANNEL) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->areas[area].available || track_num > ctx->areas[area].track_count) {
        return SACD_VFS_ERROR_NOT_FOUND;
    }

    if (!ctx->areas[area].id3_cache) {
        return SACD_VFS_ERROR_MEMORY;
    }

    /* Update cache with overlay data */
    id3_cache_entry_t *cache = &ctx->areas[area].id3_cache[track_num - 1];

    if (cache->data) {
        sa_free(cache->data);
    }

    cache->data = sa_malloc(size);
    if (!cache->data) {
        cache->size = 0;
        cache->valid = false;
        return SACD_VFS_ERROR_MEMORY;
    }

    memcpy(cache->data, buffer, size);
    cache->size = size;
    cache->valid = true;
    cache->dirty = true;  /* Mark as modified for XML persistence */

    return SACD_VFS_OK;
}

int sacd_vfs_save_id3_overlay(sacd_vfs_ctx_t *ctx)
{
    return _save_id3_overlay_xml(ctx);
}

bool sacd_vfs_has_unsaved_id3_changes(sacd_vfs_ctx_t *ctx)
{
    if (!ctx || !ctx->is_open) {
        return false;
    }

    for (int area = 0; area < 2; area++) {
        if (!ctx->areas[area].available) continue;
        if (!ctx->areas[area].id3_cache) continue;
        for (uint8_t t = 0; t < ctx->areas[area].track_count; t++) {
            if (ctx->areas[area].id3_cache[t].dirty) {
                return true;
            }
        }
    }

    return false;
}

int sacd_vfs_clear_id3_overlay(sacd_vfs_ctx_t *ctx, sacd_vfs_area_t area,
                               uint8_t track_num)
{
    if (!ctx || track_num == 0) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->is_open) {
        return SACD_VFS_ERROR_NOT_OPEN;
    }

    if (area > SACD_VFS_AREA_MULTICHANNEL) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->areas[area].available || track_num > ctx->areas[area].track_count) {
        return SACD_VFS_ERROR_NOT_FOUND;
    }

    if (!ctx->areas[area].id3_cache) {
        return SACD_VFS_ERROR_MEMORY;
    }

    id3_cache_entry_t *cache = &ctx->areas[area].id3_cache[track_num - 1];

    /* Free existing data */
    if (cache->data) {
        sa_free(cache->data);
        cache->data = NULL;
    }

    cache->size = 0;
    cache->valid = false;
    cache->dirty = true;   /* Mark as changed so save will update XML */
    cache->from_xml = false;

    return SACD_VFS_OK;
}

/* =============================================================================
 * DST Decoder Support (Multi-threaded)
 * ===========================================================================*/

/**
 * @brief Cleanup function for a DST decode job (before execution).
 */
static void _vfs_job_cleanup(void *arg)
{
    vfs_dst_job_t *job = (vfs_dst_job_t *)arg;
    if (!job) return;
    if (job->compressed_ref) {
        sa_buffer_unref(&job->compressed_ref);
    }
    if (job->decompressed_ref) {
        sa_buffer_unref(&job->decompressed_ref);
    }
    sa_free(job);
}

/**
 * @brief Cleanup function for a DST decode result (discarded result).
 */
static void _vfs_result_cleanup(void *data)
{
    _vfs_job_cleanup(data);
}

/**
 * @brief Worker function: decode a single DST frame.
 *
 * Each invocation creates its own dst_decoder_t instance (DST decoders
 * are not thread-safe). This is safe because DST frames are independently
 * decodable. A future optimization could use per-worker decoder arrays.
 *
 * @param arg  Pointer to vfs_dst_job_t
 * @return The same vfs_dst_job_t pointer with decompressed_data filled in
 */
static void *_vfs_dst_decode_func(void *arg)
{
    vfs_dst_job_t *job = (vfs_dst_job_t *)arg;
    if (!job) return NULL;

    /* EOF sentinel: pass through without decoding */
    if (job->is_eof) {
        return job;
    }

    /* Create per-job decoder */
    dst_decoder_t *decoder = NULL;
    int ret = dst_decoder_init(&decoder, job->channel_count, job->sample_rate);
    if (ret != 0 || !decoder) {
        job->error_code = -1;
        return job;
    }

    /* Allocate output buffer from pool.
     * DST frame decodes to SACD_FRAME_SIZE_64 * channel_count bytes.
     */
    sa_buffer_ref_t *decomp_ref = sa_buffer_pool_get(job->decomp_pool);
    if (!decomp_ref) {
        dst_decoder_close(decoder);
        job->error_code = -2;
        return job;
    }
    job->decompressed_ref = decomp_ref;
    job->decompressed_data = decomp_ref->data;

    /* Decode */
    int decoded_len = 0;
    ret = dst_decoder_decode(decoder, job->compressed_data, job->compressed_size,
                             job->decompressed_data, &decoded_len);
    dst_decoder_close(decoder);

    if (ret != 0 || decoded_len <= 0) {
        job->error_code = -3;
        return job;
    }

    job->decompressed_size = decoded_len;
    job->error_code = 0;
    return job;
}

/**
 * @brief Dedicated reader thread for multi-threaded DST decompression.
 *
 * Reads compressed frames from the SACD ISO and dispatches decode jobs
 * to the thread pool. Handles SEEK and CLOSE commands from the main thread.
 */
static int _vfs_reader_thread(void *arg)
{
    sacd_vfs_file_t *file = (sacd_vfs_file_t *)arg;

    VFS_DEBUG("VFS DEBUG: MT reader thread started for track %u\n", file->track_num);

restart:
    while (file->current_frame < file->end_frame) {
        /* Check for commands before each frame read */
        mtx_lock(&file->command_mtx);
        vfs_mt_cmd_t cmd = file->command;
        mtx_unlock(&file->command_mtx);

        if (cmd == VFS_MT_CMD_CLOSE) {
            VFS_DEBUG("VFS DEBUG: MT reader thread got CLOSE command\n");
            return 0;
        }

        if (cmd == VFS_MT_CMD_SEEK) {
            VFS_DEBUG("VFS DEBUG: MT reader thread got SEEK command to frame %u\n",
                      file->mt_seek_frame);

            /* Reset the process queue: drains input, waits for processing,
             * discards output. This is safe because the main thread is not
             * consuming during seek. */
            sa_tpool_process_reset(file->process, 1);

            /* Update frame position */
            mtx_lock(&file->command_mtx);
            file->current_frame = file->mt_seek_frame;
            file->command = VFS_MT_CMD_SEEK_DONE;
            cnd_signal(&file->command_cnd);
            mtx_unlock(&file->command_mtx);

            /* Restart reading from new position */
            goto restart;
        }

        /* Read next compressed frame from SACD */
        uint8_t frame_buffer[SACD_MAX_DSD_SIZE];
        uint32_t frames_to_read = 1;
        uint16_t frame_size = 0;

        int result = sacd_get_sound_data(file->reader, frame_buffer,
                                                  file->current_frame,
                                                  &frames_to_read, &frame_size);

        if (result != SACD_OK || frames_to_read == 0) {
            VFS_DEBUG("VFS DEBUG: MT reader thread read error at frame %u\n",
                      file->current_frame);
            file->mt_errcode = SACD_VFS_ERROR_READ;
            break;
        }

        /* Create decode job with pooled compressed data */
        vfs_dst_job_t *job = sa_mallocz(sizeof(vfs_dst_job_t));
        if (!job) {
            file->mt_errcode = SACD_VFS_ERROR_MEMORY;
            break;
        }

        sa_buffer_ref_t *comp_ref = sa_buffer_pool_get(file->compressed_pool);
        if (!comp_ref) {
            sa_free(job);
            file->mt_errcode = SACD_VFS_ERROR_MEMORY;
            break;
        }

        memcpy(comp_ref->data, frame_buffer, frame_size);
        job->compressed_ref = comp_ref;
        job->compressed_data = comp_ref->data;
        job->compressed_size = frame_size;
        job->channel_count = (int)file->info.channel_count;
        job->sample_rate = (int)file->info.sample_rate;
        job->frame_number = file->current_frame;
        job->decomp_pool = file->decompressed_pool;
        job->is_eof = 0;

        /* Dispatch to thread pool (blocks if queue is full) */
        int dispatch_ret = sa_tpool_dispatch3(file->pool, file->process,
                                              _vfs_dst_decode_func, job,
                                              _vfs_job_cleanup,
                                              _vfs_result_cleanup, 0);
        if (dispatch_ret != 0) {
            /* Dispatch failed - check if we got a command while blocked */
            _vfs_job_cleanup(job);

            mtx_lock(&file->command_mtx);
            cmd = file->command;
            mtx_unlock(&file->command_mtx);

            if (cmd == VFS_MT_CMD_CLOSE) {
                return 0;
            }
            if (cmd == VFS_MT_CMD_SEEK) {
                sa_tpool_process_reset(file->process, 1);
                mtx_lock(&file->command_mtx);
                file->current_frame = file->mt_seek_frame;
                file->command = VFS_MT_CMD_SEEK_DONE;
                cnd_signal(&file->command_cnd);
                mtx_unlock(&file->command_mtx);
                goto restart;
            }

            file->mt_errcode = SACD_VFS_ERROR_IO;
            break;
        }

        file->current_frame++;
    }

    /* Dispatch EOF sentinel so the consumer knows reading is done */
    {
        vfs_dst_job_t *eof_job = sa_mallocz(sizeof(vfs_dst_job_t));
        if (eof_job) {
            eof_job->is_eof = 1;
            int ret = sa_tpool_dispatch3(file->pool, file->process,
                                         _vfs_dst_decode_func, eof_job,
                                         _vfs_job_cleanup,
                                         _vfs_result_cleanup, -1);
            if (ret != 0) {
                _vfs_job_cleanup(eof_job);
            }
        }
    }

    /* Wait for commands (SEEK to restart, CLOSE to exit) */
    mtx_lock(&file->command_mtx);
    while (file->command != VFS_MT_CMD_CLOSE && file->command != VFS_MT_CMD_SEEK) {
        cnd_wait(&file->command_cnd, &file->command_mtx);
    }

    if (file->command == VFS_MT_CMD_SEEK) {
        sa_tpool_process_reset(file->process, 1);
        file->current_frame = file->mt_seek_frame;
        file->command = VFS_MT_CMD_SEEK_DONE;
        cnd_signal(&file->command_cnd);
        mtx_unlock(&file->command_mtx);
        file->mt_errcode = 0;
        goto restart;
    }

    mtx_unlock(&file->command_mtx);

    VFS_DEBUG("VFS DEBUG: MT reader thread exiting for track %u\n", file->track_num);
    return 0;
}

/* =============================================================================
 * ID3 Overlay XML Sidecar Support
 * ===========================================================================*/

static void _get_xml_sidecar_path(const char *iso_path, char *xml_path, size_t size)
{
    if (!iso_path || !xml_path || size == 0) {
        if (xml_path && size > 0) xml_path[0] = '\0';
        return;
    }

    /* Generate path: {iso_path}.xml */
    size_t iso_len = strlen(iso_path);
    if (iso_len + 5 >= size) {  /* +5 for ".xml\0" */
        xml_path[0] = '\0';
        return;
    }

    memcpy(xml_path, iso_path, iso_len);
    memcpy(xml_path + iso_len, ".xml", 5);  /* includes null terminator */
}

static int _load_id3_overlay_xml(sacd_vfs_ctx_t *ctx)
{
    if (!ctx || !ctx->is_open) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    char xml_path[SACD_VFS_MAX_PATH];
    _get_xml_sidecar_path(ctx->iso_path, xml_path, sizeof(xml_path));

    if (xml_path[0] == '\0') {
        return SACD_VFS_OK;  /* No valid path, not an error */
    }

    /* Check if file exists */
    FILE *f = fopen(xml_path, "r");
    if (!f) {
        return SACD_VFS_OK;  /* No sidecar file, not an error */
    }
    fclose(f);

    /* Parse XML document */
    XMLDoc doc = {0};
    if (!XMLDoc_init(&doc)) {
        return SACD_VFS_OK;  /* Init failed, skip */
    }

    if (!XMLDoc_parse_file_DOM(xml_path, &doc)) {
        XMLDoc_free(&doc);
        return SACD_VFS_OK;  /* Parse failed, skip */
    }

    XMLNode *root = XMLDoc_root(&doc);
    if (!root) {
        XMLDoc_free(&doc);
        return SACD_VFS_OK;
    }

    /* Iterate through <Area> elements */
    int area_count = XMLNode_get_children_count(root);
    for (int ai = 0; ai < area_count; ai++) {
        XMLNode *area_node = XMLNode_get_child(root, ai);
        if (!area_node || !area_node->tag) continue;

        /* Check if this is an Area element */
        if (strcmp(area_node->tag, "Area") != 0) continue;

        /* Get area type attribute */
        const char *area_type = NULL;
        XMLNode_get_attribute(area_node, "type", &area_type);
        if (!area_type) continue;

        int area_idx = -1;
        if (strcmp(area_type, "stereo") == 0) {
            area_idx = SACD_VFS_AREA_STEREO;
        } else if (strcmp(area_type, "multichannel") == 0) {
            area_idx = SACD_VFS_AREA_MULTICHANNEL;
        }

        if (area_idx < 0 || !ctx->areas[area_idx].available) continue;
        if (!ctx->areas[area_idx].id3_cache) continue;

        /* Iterate through <Track> elements */
        int track_count = XMLNode_get_children_count(area_node);
        for (int ti = 0; ti < track_count; ti++) {
            XMLNode *track_node = XMLNode_get_child(area_node, ti);
            if (!track_node || !track_node->tag) continue;

            if (strcmp(track_node->tag, "Track") != 0) continue;

            /* Get track number attribute */
            const char *num_str = NULL;
            XMLNode_get_attribute(track_node, "number", &num_str);
            if (!num_str) continue;

            int track_num = atoi(num_str);
            if (track_num < 1 || track_num > ctx->areas[area_idx].track_count) continue;

            /* Find <Id3> child element */
            int id3_count = XMLNode_get_children_count(track_node);
            for (int ii = 0; ii < id3_count; ii++) {
                XMLNode *id3_node = XMLNode_get_child(track_node, ii);
                if (!id3_node || !id3_node->tag) continue;

                if (strcmp(id3_node->tag, "Id3") != 0) continue;

                /* Get base64 text content */
                if (!id3_node->text || id3_node->text[0] == '\0') continue;

                /* Decode base64 */
                size_t b64_len = strlen(id3_node->text);
                size_t max_decoded = SA_BASE64_DECODE_SIZE(b64_len);
                uint8_t *decoded = sa_malloc(max_decoded);
                if (!decoded) continue;

                int decoded_len = sa_base64_decode(decoded, id3_node->text, (int)max_decoded);
                if (decoded_len <= 0) {
                    sa_free(decoded);
                    continue;
                }

                /* Store in ID3 cache */
                id3_cache_entry_t *cache = &ctx->areas[area_idx].id3_cache[track_num - 1];
                if (cache->data) {
                    sa_free(cache->data);
                }
                cache->data = decoded;
                cache->size = (size_t)decoded_len;
                cache->valid = true;
                cache->dirty = false;
                cache->from_xml = true;

                break;  /* Only one Id3 per track */
            }
        }
    }

    XMLDoc_free(&doc);
    return SACD_VFS_OK;
}

static int _save_id3_overlay_xml(sacd_vfs_ctx_t *ctx)
{
    if (!ctx) {
        return SACD_VFS_ERROR_INVALID_PARAMETER;
    }

    if (!ctx->is_open) {
        return SACD_VFS_ERROR_NOT_OPEN;
    }

    char xml_path[SACD_VFS_MAX_PATH];
    _get_xml_sidecar_path(ctx->iso_path, xml_path, sizeof(xml_path));

    if (xml_path[0] == '\0') {
        return SACD_VFS_ERROR_IO;
    }

    /* Check if there's anything to save */
    bool has_data = false;
    for (int area = 0; area < 2; area++) {
        if (!ctx->areas[area].available) continue;
        if (!ctx->areas[area].id3_cache) continue;
        for (uint8_t t = 0; t < ctx->areas[area].track_count; t++) {
            id3_cache_entry_t *cache = &ctx->areas[area].id3_cache[t];
            if (cache->valid && (cache->dirty || cache->from_xml)) {
                has_data = true;
                break;
            }
        }
        if (has_data) break;
    }

    if (!has_data) {
        /* Nothing to save - remove existing file if present */
        remove(xml_path);
        return SACD_VFS_OK;
    }

    /* Initialize XML document */
    XMLDoc doc;
    if (!XMLDoc_init(&doc)) {
        return SACD_VFS_ERROR_MEMORY;
    }

    /* Add XML prolog */
    XMLNode *prolog = XMLNode_new(TAG_INSTR, "xml version=\"1.0\" encoding=\"UTF-8\"", NULL);
    if (prolog) {
        XMLDoc_add_node(&doc, prolog);
    }

    /* Create root element: <SacdId3Overlay version="1.0" iso="filename.iso"> */
    XMLNode *root = XMLNode_new(TAG_FATHER, "SacdId3Overlay", NULL);
    if (!root) {
        XMLDoc_free(&doc);
        return SACD_VFS_ERROR_MEMORY;
    }
    XMLNode_set_attribute(root, "version", "1.0");

    /* Extract basename from iso_path for iso attribute */
    const char *basename = ctx->iso_path;
    const char *p = ctx->iso_path;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            basename = p + 1;
        }
        p++;
    }
    XMLNode_set_attribute(root, "iso", basename);
    XMLDoc_add_node(&doc, root);

    /* For each area (stereo, multichannel) */
    const char *area_names[] = { "stereo", "multichannel" };
    for (int area = 0; area < 2; area++) {
        if (!ctx->areas[area].available) continue;
        if (!ctx->areas[area].id3_cache) continue;

        /* Check if this area has any data to save */
        bool area_has_data = false;
        for (uint8_t t = 0; t < ctx->areas[area].track_count; t++) {
            id3_cache_entry_t *cache = &ctx->areas[area].id3_cache[t];
            if (cache->valid && (cache->dirty || cache->from_xml)) {
                area_has_data = true;
                break;
            }
        }
        if (!area_has_data) continue;

        /* Create <Area type="stereo|multichannel"> */
        XMLNode *area_node = XMLNode_new(TAG_FATHER, "Area", NULL);
        if (!area_node) continue;
        XMLNode_set_attribute(area_node, "type", area_names[area]);
        XMLNode_add_child(root, area_node);

        /* For each track with overlay data */
        for (uint8_t t = 0; t < ctx->areas[area].track_count; t++) {
            id3_cache_entry_t *cache = &ctx->areas[area].id3_cache[t];
            if (!cache->valid || (!cache->dirty && !cache->from_xml)) continue;

            /* Create <Track number="N"> */
            XMLNode *track_node = XMLNode_new(TAG_FATHER, "Track", NULL);
            if (!track_node) continue;

            char num_buf[8];
            snprintf(num_buf, sizeof(num_buf), "%d", t + 1);
            XMLNode_set_attribute(track_node, "number", num_buf);
            XMLNode_add_child(area_node, track_node);

            /* Encode ID3 data to base64 */
            size_t b64_size = SA_BASE64_SIZE(cache->size);
            char *b64 = sa_malloc(b64_size);
            if (!b64) continue;

            if (!sa_base64_encode(b64, (int)b64_size, cache->data, (int)cache->size)) {
                sa_free(b64);
                continue;
            }

            /* Create <Id3>base64data</Id3> */
            XMLNode *id3_node = XMLNode_new(TAG_FATHER, "Id3", b64);
            if (id3_node) {
                XMLNode_add_child(track_node, id3_node);
            }

            sa_free(b64);

            /* Mark as saved */
            cache->dirty = false;
            cache->from_xml = true;
        }
    }

    /* Write to file */
    FILE *f = fopen(xml_path, "w");
    if (!f) {
        XMLDoc_free(&doc);
        return SACD_VFS_ERROR_IO;
    }

    XMLDoc_print(&doc, f, "\n", "  ", 0, 0, 2);
    fclose(f);
    XMLDoc_free(&doc);

    return SACD_VFS_OK;
}

static int _calculate_virtual_file_size(sacd_vfs_file_t *file)
{
    /* DSF header size */
    file->dsf_header_size = DSF_DSD_CHUNK_SIZE + DSF_FMT_CHUNK_SIZE + DSF_DATA_CHUNK_HEADER_SIZE;

    /* Audio data size calculation:
     * - Each SACD frame is SACD_FRAME_SIZE_64 (4704) bytes per channel
     * - Total frame size = SACD_FRAME_SIZE_64 * channel_count
     * - DSF uses 4096-byte blocks per channel
     * - Need to convert and pad to block boundaries
     */
    uint32_t frame_count = file->end_frame - file->start_frame;
    size_t bytes_per_channel = (size_t)frame_count * SACD_FRAME_SIZE_64;
    size_t num_blocks = (bytes_per_channel + DSF_BLOCK_SIZE_PER_CHANNEL - 1) / DSF_BLOCK_SIZE_PER_CHANNEL;
    file->info.audio_data_size = num_blocks * DSF_BLOCK_SIZE_PER_CHANNEL * file->info.channel_count;

    VFS_DEBUG("VFS DEBUG: _calculate_virtual_file_size: start=%u end=%u frame_count=%u bytes_per_ch=%zu num_blocks=%zu audio_size=%llu\n",
              file->start_frame, file->end_frame, frame_count, bytes_per_channel, num_blocks,
              (unsigned long long)file->info.audio_data_size);

    /* Get ID3 tag size */
    uint8_t *id3_data = NULL;
    size_t id3_size = 0;
    int result = sacd_vfs_get_id3_tag(file->ctx, file->area, file->track_num, &id3_data, &id3_size);
    if (result == SACD_VFS_OK && id3_data) {
        file->info.metadata_size = id3_size;
        sa_free(id3_data);
    } else {
        file->info.metadata_size = 0;
    }

    /* Calculate metadata offset */
    file->info.metadata_offset = file->dsf_header_size + file->info.audio_data_size;

    /* Total size */
    file->info.total_size = file->info.metadata_offset + file->info.metadata_size;

    file->info.header_size = file->dsf_header_size;

    return SACD_VFS_OK;
}

static int _generate_dsf_header(sacd_vfs_file_t *file)
{
    uint8_t *p = file->dsf_header;
    size_t pos = 0;

    /* =========== DSD Chunk (28 bytes) =========== */

    /* Chunk ID: 'DSD ' */
    p[pos++] = 'D';
    p[pos++] = 'S';
    p[pos++] = 'D';
    p[pos++] = ' ';

    /* Chunk size: 28 (little-endian uint64) */
    uint64_t dsd_size = DSF_DSD_CHUNK_SIZE;
    memcpy(p + pos, &dsd_size, 8);
    pos += 8;

    /* Total file size (little-endian uint64) */
    memcpy(p + pos, &file->info.total_size, 8);
    pos += 8;

    /* Metadata offset (little-endian uint64)
     * Always set to enable ID3 tag writes even when no metadata exists yet */
    uint64_t meta_offset = file->info.metadata_offset;
    memcpy(p + pos, &meta_offset, 8);
    pos += 8;

    /* =========== fmt Chunk (52 bytes) =========== */

    /* Chunk ID: 'fmt ' */
    p[pos++] = 'f';
    p[pos++] = 'm';
    p[pos++] = 't';
    p[pos++] = ' ';

    /* Chunk size: 52 (little-endian uint64) */
    uint64_t fmt_size = DSF_FMT_CHUNK_SIZE;
    memcpy(p + pos, &fmt_size, 8);
    pos += 8;

    /* Format version: 1 (little-endian uint32) */
    uint32_t version = 1;
    memcpy(p + pos, &version, 4);
    pos += 4;

    /* Format ID: 0 = DSD raw (little-endian uint32) */
    uint32_t format_id = 0;
    memcpy(p + pos, &format_id, 4);
    pos += 4;

    /* Channel type (little-endian uint32) */
    uint32_t channel_type;
    switch (file->info.channel_count) {
    case 1: channel_type = 1; break;  /* Mono */
    case 2: channel_type = 2; break;  /* Stereo */
    case 3: channel_type = 3; break;  /* 3 channels */
    case 4: channel_type = 4; break;  /* Quad */
    case 5: channel_type = 6; break;  /* 5 channels */
    case 6: channel_type = 7; break;  /* 5.1 */
    default: channel_type = 2; break;
    }
    memcpy(p + pos, &channel_type, 4);
    pos += 4;

    /* Channel count (little-endian uint32) */
    memcpy(p + pos, &file->info.channel_count, 4);
    pos += 4;

    /* Sampling frequency (little-endian uint32) */
    memcpy(p + pos, &file->info.sample_rate, 4);
    pos += 4;

    /* Bits per sample: 1 for DSD (little-endian uint32) */
    uint32_t bits = 1;
    memcpy(p + pos, &bits, 4);
    pos += 4;

    /* Sample count per channel (little-endian uint64) */
    memcpy(p + pos, &file->info.sample_count, 8);
    pos += 8;

    /* Block size per channel: 4096 (little-endian uint32) */
    uint32_t block_size = DSF_BLOCK_SIZE_PER_CHANNEL;
    memcpy(p + pos, &block_size, 4);
    pos += 4;

    /* Reserved: 0 (uint32) */
    uint32_t reserved = 0;
    memcpy(p + pos, &reserved, 4);
    pos += 4;

    /* =========== data Chunk Header (12 bytes) =========== */

    /* Chunk ID: 'data' */
    p[pos++] = 'd';
    p[pos++] = 'a';
    p[pos++] = 't';
    p[pos++] = 'a';

    /* Chunk size: 12 + audio_data_size (little-endian uint64) */
    uint64_t data_chunk_size = DSF_DATA_CHUNK_HEADER_SIZE + file->info.audio_data_size;
    memcpy(p + pos, &data_chunk_size, 8);
    pos += 8;

    file->dsf_header_size = pos;
    return SACD_VFS_OK;
}

static int _read_header_region(sacd_vfs_file_t *file, uint8_t *buffer, size_t size, size_t *bytes_read)
{
    size_t header_remaining = file->dsf_header_size - file->position;
    size_t to_read = (size < header_remaining) ? size : header_remaining;

    memcpy(buffer, file->dsf_header + file->position, to_read);
    file->position += to_read;
    *bytes_read = to_read;

    return SACD_VFS_OK;
}

/**
 * @brief Write a complete block group to the transform buffer
 *
 * Assembles channel buffers into DSF block-interleaved format.
 * Each block group is [Ch0 block][Ch1 block]...[ChN block], each 4096 bytes.
 */
static void _write_block_group(sacd_vfs_file_t *file, size_t bytes_per_channel, int pad_to_block)
{
    uint32_t channel_count = file->info.channel_count;
    size_t block_bytes = pad_to_block ? DSF_BLOCK_SIZE_PER_CHANNEL : bytes_per_channel;

    /* Assemble block group: [Ch0][Ch1]...[ChN], each block_bytes */
    for (uint32_t ch = 0; ch < channel_count; ch++) {
        size_t offset = ch * block_bytes;
        memcpy(&file->transform_buffer[offset], file->channel_buffers[ch], bytes_per_channel);
        if (pad_to_block && bytes_per_channel < DSF_BLOCK_SIZE_PER_CHANNEL) {
            /* Zero-pad the remainder of this channel's block */
            memset(&file->transform_buffer[offset + bytes_per_channel], 0,
                   DSF_BLOCK_SIZE_PER_CHANNEL - bytes_per_channel);
        }
    }

    file->transform_buffer_len = block_bytes * channel_count;
    file->transform_buffer_pos = 0;
}

/**
 * @brief Flush remaining buffered data with zero padding
 *
 * Called when reaching end of track to output final partial block.
 */
static int _flush_block_buffers(sacd_vfs_file_t *file)
{
    if (file->bytes_buffered > 0) {
        _write_block_group(file, file->bytes_buffered, 1);
        file->bytes_buffered = 0;
    }
    return SACD_VFS_OK;
}

/**
 * @brief Transform DSD frame data for DSF output
 *
 * Accumulates frame data into per-channel buffers and outputs complete
 * 4096-byte blocks to the transform buffer. DSF requires continuous DSD
 * data with padding only at the very end of the file.
 *
 * The transform buffer may contain multiple block groups after this call.
 * Partial data remains in channel_buffers for the next frame.
 *
 * @param file VFS file handle
 * @param src Source frame data (byte-interleaved, MSB-first)
 * @param src_len Source data length
 * @return SACD_VFS_OK or error code
 */
static int _transform_dsd_frame(sacd_vfs_file_t *file, const uint8_t *src, size_t src_len)
{
    uint32_t channel_count = file->info.channel_count;
    size_t bytes_per_channel = src_len / channel_count;
    size_t input_pos = 0;
    size_t output_pos = 0;
    size_t block_group_size = DSF_BLOCK_SIZE_PER_CHANNEL * channel_count;

    /* Calculate maximum output: could produce multiple complete blocks
     * For 4704 bytes/channel input with 608 bytes already buffered:
     * - First block completes after 4096 - 608 = 3488 bytes
     * - Leaves 4704 - 3488 = 1216 bytes, which produces another block after
     *   accumulating 4096 - 1216 = 2880 more bytes from next frame
     */
    size_t max_blocks = (file->bytes_buffered + bytes_per_channel) / DSF_BLOCK_SIZE_PER_CHANNEL;
    size_t max_output = max_blocks * block_group_size;

    /* Ensure transform buffer is large enough */
    if (max_output > file->transform_buffer_size) {
        uint8_t *new_buf = sa_realloc(file->transform_buffer, max_output);
        if (!new_buf) {
            return SACD_VFS_ERROR_MEMORY;
        }
        file->transform_buffer = new_buf;
        file->transform_buffer_size = max_output;
    }

    /* Process input data: de-interleave into channel buffers, bit-reverse,
     * and output complete blocks as they fill up.
     *
     * Input format (SACD byte-interleaved): [L0][R0][L1][R1][L2][R2]...
     * We accumulate into per-channel buffers with bit reversal.
     * When buffers reach 4096 bytes, output a complete block group.
     */
    for (size_t i = 0; i < bytes_per_channel; i++) {
        /* De-interleave and bit-reverse each sample into channel buffers */
        for (uint32_t ch = 0; ch < channel_count; ch++) {
            file->channel_buffers[ch][file->bytes_buffered] = ff_reverse[src[input_pos++]];
        }
        file->bytes_buffered++;

        /* When we have a complete block (4096 bytes per channel), output it */
        if (file->bytes_buffered == DSF_BLOCK_SIZE_PER_CHANNEL) {
            /* Assemble block group directly into transform buffer */
            for (uint32_t ch = 0; ch < channel_count; ch++) {
                memcpy(&file->transform_buffer[output_pos + ch * DSF_BLOCK_SIZE_PER_CHANNEL],
                       file->channel_buffers[ch], DSF_BLOCK_SIZE_PER_CHANNEL);
            }
            output_pos += block_group_size;
            file->bytes_buffered = 0;
        }
    }

    file->transform_buffer_len = output_pos;
    file->transform_buffer_pos = 0;

    return SACD_VFS_OK;
}

static int _read_audio_region(sacd_vfs_file_t *file, uint8_t *buffer, size_t size, size_t *bytes_read)
{
    if (file->mt_enabled) {
        return _read_audio_region_mt(file, buffer, size, bytes_read);
    }

    *bytes_read = 0;
    size_t total_read = 0;

    /* Note: Channel type was selected once during file open (sacd_vfs_file_open).
     * Do NOT call sacd_select_channel_type here - it should only be used once.
     */

    while (size > 0 && file->position < file->info.metadata_offset) {
        /* Check if we have data in transform buffer */
        if (file->transform_buffer_pos < file->transform_buffer_len) {
            size_t available = file->transform_buffer_len - file->transform_buffer_pos;
            size_t to_copy = (size < available) ? size : available;

            memcpy(buffer + total_read, file->transform_buffer + file->transform_buffer_pos, to_copy);
            file->transform_buffer_pos += to_copy;
            file->position += to_copy;
            total_read += to_copy;
            size -= to_copy;
            continue;
        }

        /* Need to read more frames */
        if (file->current_frame >= file->end_frame) {
            /* No more frames - flush any remaining buffered data with proper padding */
            if (file->bytes_buffered > 0) {
                int result = _flush_block_buffers(file);
                if (result != SACD_VFS_OK) {
                    if (total_read > 0) {
                        break;
                    }
                    return result;
                }
                /* Continue loop to consume the flushed data from transform buffer */
                continue;
            }
            /* All data flushed - we're done with audio region */
            break;
        }

        /* Read next frame from SACD (sized for max channel count) */
        uint8_t frame_buffer[SACD_MAX_DSD_SIZE];
        uint32_t frames_to_read = 1;
        uint16_t frame_size = 0;

#if VFS_PROFILE_ENABLED
        LARGE_INTEGER t0, t1, t2, t3;
        QueryPerformanceCounter(&t0);
#endif

        int result = sacd_get_sound_data(file->reader, frame_buffer,
                                                  file->current_frame, &frames_to_read, &frame_size);

#if VFS_PROFILE_ENABLED
        QueryPerformanceCounter(&t1);
        file->prof_read_ticks += t1.QuadPart - t0.QuadPart;
#endif

        if (result != SACD_OK || frames_to_read == 0) {
            /* Debug: print detailed error info */
            VFS_DEBUG("VFS DEBUG: READ ERROR result=%d: track=%u, frame=%u/%u-%u\n",
                    result, file->track_num, file->current_frame, file->start_frame, file->end_frame);
            if (total_read > 0) {
                break;
            }
            return SACD_VFS_ERROR_READ;
        }

        file->current_frame++;

        /* Note: For DST, frame_size is the compressed size, not decoded size */

        /* Handle DST decompression if needed */
        size_t data_len;
        uint8_t *data_ptr;

        if (file->info.frame_format == SACD_VFS_FRAME_DST) {
            /* DST frames need decompression (single-threaded) */
            if (!file->dst_decoder || !file->dst_decode_buffer) {
                if (total_read > 0) {
                    break;
                }
                return SACD_VFS_ERROR_DST_DECODE;
            }

            /* Decode frame directly */
            int decoded_len = 0;
#if VFS_PROFILE_ENABLED
            QueryPerformanceCounter(&t1);
#endif
            int decode_result = dst_decoder_decode(file->dst_decoder, frame_buffer, frame_size, file->dst_decode_buffer, &decoded_len);
#if VFS_PROFILE_ENABLED
            QueryPerformanceCounter(&t2);
            file->prof_decode_ticks += t2.QuadPart - t1.QuadPart;
#endif
            if (decode_result != 0 || decoded_len <= 0) {
                VFS_DEBUG("VFS DEBUG: DST decode FAILED\n");
                if (total_read > 0) {
                    break;
                }
                return SACD_VFS_ERROR_DST_DECODE;
            }

            data_ptr = file->dst_decode_buffer;
            data_len = (size_t)decoded_len;
        } else {
            /* Raw DSD frame: use actual frame_size from reader (like sacd-extract does) */
            data_ptr = frame_buffer;
            data_len = frame_size;
        }

        /* Transform the frame data */
#if VFS_PROFILE_ENABLED
        QueryPerformanceCounter(&t2);
#endif
        result = _transform_dsd_frame(file, data_ptr, data_len);
#if VFS_PROFILE_ENABLED
        QueryPerformanceCounter(&t3);
        file->prof_transform_ticks += t3.QuadPart - t2.QuadPart;
        file->prof_frame_count++;
#endif
        if (result != SACD_VFS_OK) {
            if (total_read > 0) {
                break;
            }
            return result;
        }

        /* After a seek, skip bytes to align with target position */
        if (file->seek_skip_bytes > 0 && file->transform_buffer_len > 0) {
            size_t skip = file->seek_skip_bytes;
            if (skip > file->transform_buffer_len) {
                skip = file->transform_buffer_len;
            }
            file->transform_buffer_pos = skip;
            file->seek_skip_bytes -= skip;
        }
    }

    *bytes_read = total_read;
    return SACD_VFS_OK;
}

/**
 * @brief Multi-threaded audio region read.
 *
 * Consumes decoded frames from the thread pool process queue, transforms
 * them into DSF block-interleaved format, and copies to the user buffer.
 * The reader thread runs independently, dispatching decode jobs ahead of
 * consumption.
 */
static int _read_audio_region_mt(sacd_vfs_file_t *file, uint8_t *buffer,
                                  size_t size, size_t *bytes_read)
{
    *bytes_read = 0;
    size_t total_read = 0;

    while (size > 0 && file->position < file->info.metadata_offset) {
        /* Check if we have data in transform buffer */
        if (file->transform_buffer_pos < file->transform_buffer_len) {
            size_t available = file->transform_buffer_len - file->transform_buffer_pos;
            size_t to_copy = (size < available) ? size : available;

            memcpy(buffer + total_read,
                   file->transform_buffer + file->transform_buffer_pos, to_copy);
            file->transform_buffer_pos += to_copy;
            file->position += to_copy;
            total_read += to_copy;
            size -= to_copy;
            continue;
        }

        /* Pull next decoded result from the process queue (blocking) */
        sa_tpool_result *result = sa_tpool_next_result_wait(file->process);
        if (!result) {
            /* Queue shutdown or error */
            if (total_read > 0) break;
            return (file->mt_errcode != 0) ? file->mt_errcode : SACD_VFS_ERROR_READ;
        }

        vfs_dst_job_t *job = (vfs_dst_job_t *)sa_tpool_result_data(result);
        sa_tpool_delete_result(result, 0);  /* Don't free data - we own the job */

        if (!job) {
            if (total_read > 0) break;
            return SACD_VFS_ERROR_READ;
        }

        /* Check for EOF sentinel */
        if (job->is_eof) {
            sa_free(job);
            /* Flush any remaining buffered data with proper padding */
            if (file->bytes_buffered > 0) {
                int flush_ret = _flush_block_buffers(file);
                if (flush_ret != SACD_VFS_OK) {
                    if (total_read > 0) break;
                    return flush_ret;
                }
                /* Continue loop to consume the flushed data */
                continue;
            }
            break;
        }

        /* Check for decode error */
        if (job->error_code != 0 || !job->decompressed_data || job->decompressed_size <= 0) {
            VFS_DEBUG("VFS DEBUG: MT decode error %d at frame %u\n",
                      job->error_code, job->frame_number);
            _vfs_job_cleanup(job);
            if (total_read > 0) break;
            return SACD_VFS_ERROR_DST_DECODE;
        }

        /* Transform the decoded DSD frame */
        int transform_ret = _transform_dsd_frame(file, job->decompressed_data,
                                                  (size_t)job->decompressed_size);

        /* Free job resources */
        _vfs_job_cleanup(job);

        if (transform_ret != SACD_VFS_OK) {
            if (total_read > 0) break;
            return transform_ret;
        }

        /* After a seek, skip bytes to align with target position */
        if (file->seek_skip_bytes > 0 && file->transform_buffer_len > 0) {
            size_t skip = file->seek_skip_bytes;
            if (skip > file->transform_buffer_len) {
                skip = file->transform_buffer_len;
            }
            file->transform_buffer_pos = skip;
            file->seek_skip_bytes -= skip;
        }
    }

    *bytes_read = total_read;
    return SACD_VFS_OK;
}

static int _read_metadata_region(sacd_vfs_file_t *file, uint8_t *buffer, size_t size, size_t *bytes_read)
{
    *bytes_read = 0;

    if (file->info.metadata_size == 0) {
        return SACD_VFS_ERROR_EOF;
    }

    /* Get ID3 tag data */
    uint8_t *id3_data = NULL;
    size_t id3_size = 0;

    int result = sacd_vfs_get_id3_tag(file->ctx, file->area, file->track_num, &id3_data, &id3_size);
    if (result != SACD_VFS_OK || !id3_data) {
        return SACD_VFS_ERROR_FORMAT;
    }

    /* Calculate offset within metadata */
    uint64_t meta_offset = file->position - file->info.metadata_offset;
    if (meta_offset >= id3_size) {
        sa_free(id3_data);
        return SACD_VFS_ERROR_EOF;
    }

    size_t remaining = id3_size - meta_offset;
    size_t to_read = (size < remaining) ? size : remaining;

    memcpy(buffer, id3_data + meta_offset, to_read);
    file->position += to_read;
    *bytes_read = to_read;

    sa_free(id3_data);
    return SACD_VFS_OK;
}
