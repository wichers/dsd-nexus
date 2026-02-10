/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief CUE sheet metadata sink for edit master companion files
 * Generates CUE sheet files (.cue) as companion to DSDIFF edit master files.
 * Timing is in MM:SS:FF format (75 frames per second, SACD standard).
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


#include "dsdpipe_internal.h"

#include <libdsdpipe/metadata_tags.h>
#include <libsautil/mem.h>
#include <libsautil/sastring.h>
#include <libsautil/compat.h>

#include <string.h>
#include <stdio.h>

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum number of tracks to collect */
#define CUE_MAX_TRACKS      255

/** Frame rate for CUE sheet timing (SACD/CD standard) */
#define CUE_FRAMES_PER_SEC  75

/** Maximum catalog number length (SACD disc_catalog_number is 16 bytes) */
#define CUE_MAX_CATALOG     17

/*============================================================================
 * Track Info Structure
 *============================================================================*/

typedef struct cue_track_info_s {
    uint8_t track_number;
    char *title;                    /**< Track title */
    char *performer;                /**< Track performer */
    char isrc[13];                  /**< ISRC code */
    uint32_t start_frame;           /**< Start position in SACD frames (75fps) */
    uint32_t duration_frames;       /**< Duration in SACD frames (75fps) */
} cue_track_info_t;

/*============================================================================
 * CUE Sink Context
 *============================================================================*/

typedef struct dsdpipe_sink_cue_ctx_s {
    /* Configuration */
    char *path;                     /**< Output CUE file path */
    char *audio_filename;           /**< Referenced audio file name */

    /* Format info */
    dsdpipe_format_t format;

    /* Album metadata */
    char *album_title;
    char *album_artist;
    char *catalog_number;
    char *genre;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint16_t disc_number;
    uint16_t disc_total;

    /* Track collection */
    cue_track_info_t tracks[CUE_MAX_TRACKS];
    uint8_t track_count;
    uint8_t current_track;

    /* State */
    bool is_open;
} dsdpipe_sink_cue_ctx_t;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Escape quotes and trim whitespace for CUE sheet strings
 *
 * Replaces double quotes with escaped quotes and trims trailing whitespace.
 * Caller must free the returned string with sa_free().
 */
static char *cue_escape_string(const char *src)
{
    char *replaced;
    size_t len;

    if (!src || !src[0]) {
        return NULL;
    }

    replaced = sa_strireplace(src, "\"", "\\\"");
    if (!replaced) {
        return NULL;
    }

    /* Trim trailing whitespace */
    len = strlen(replaced);
    while (len > 0 && replaced[len - 1] == ' ') {
        replaced[--len] = '\0';
    }

    return replaced;
}

/*============================================================================
 * Sink Operations
 *============================================================================*/

static int cue_sink_open(void *ctx, const char *path,
                          const dsdpipe_format_t *format,
                          const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_cue_ctx_t *cue_ctx = (dsdpipe_sink_cue_ctx_t *)ctx;

    if (!cue_ctx || !path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store output path */
    cue_ctx->path = dsdpipe_strdup(path);
    if (!cue_ctx->path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Store format */
    if (format) {
        cue_ctx->format = *format;
    }

    /* Store album metadata */
    if (metadata) {
        if (metadata->album_title) {
            cue_ctx->album_title = dsdpipe_strdup(metadata->album_title);
        }
        if (metadata->album_artist) {
            cue_ctx->album_artist = dsdpipe_strdup(metadata->album_artist);
        }
        if (metadata->catalog_number) {
            cue_ctx->catalog_number = dsdpipe_strdup(metadata->catalog_number);
        }
        if (metadata->genre) {
            cue_ctx->genre = dsdpipe_strdup(metadata->genre);
        }
        cue_ctx->year = metadata->year;
        cue_ctx->month = metadata->month;
        cue_ctx->day = metadata->day;
        cue_ctx->disc_number = metadata->disc_number;
        cue_ctx->disc_total = metadata->disc_total;
    }

    /* Initialize track collection */
    cue_ctx->track_count = 0;
    cue_ctx->current_track = 0;
    memset(cue_ctx->tracks, 0, sizeof(cue_ctx->tracks));

    cue_ctx->is_open = true;
    return DSDPIPE_OK;
}

static void cue_sink_close(void *ctx)
{
    dsdpipe_sink_cue_ctx_t *cue_ctx = (dsdpipe_sink_cue_ctx_t *)ctx;

    if (!cue_ctx) {
        return;
    }

    /* Free path */
    if (cue_ctx->path) {
        sa_free(cue_ctx->path);
        cue_ctx->path = NULL;
    }

    /* Free audio filename */
    if (cue_ctx->audio_filename) {
        sa_free(cue_ctx->audio_filename);
        cue_ctx->audio_filename = NULL;
    }

    /* Free album metadata */
    if (cue_ctx->album_title) {
        sa_free(cue_ctx->album_title);
        cue_ctx->album_title = NULL;
    }
    if (cue_ctx->album_artist) {
        sa_free(cue_ctx->album_artist);
        cue_ctx->album_artist = NULL;
    }
    if (cue_ctx->catalog_number) {
        sa_free(cue_ctx->catalog_number);
        cue_ctx->catalog_number = NULL;
    }
    if (cue_ctx->genre) {
        sa_free(cue_ctx->genre);
        cue_ctx->genre = NULL;
    }

    /* Free track metadata */
    for (int i = 0; i < cue_ctx->track_count; i++) {
        if (cue_ctx->tracks[i].title) {
            sa_free(cue_ctx->tracks[i].title);
        }
        if (cue_ctx->tracks[i].performer) {
            sa_free(cue_ctx->tracks[i].performer);
        }
    }
    cue_ctx->track_count = 0;

    cue_ctx->is_open = false;
}

static int cue_sink_track_start(void *ctx, uint8_t track_number,
                                 const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_cue_ctx_t *cue_ctx = (dsdpipe_sink_cue_ctx_t *)ctx;

    if (!cue_ctx || !cue_ctx->is_open) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (cue_ctx->track_count >= CUE_MAX_TRACKS) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store track info */
    cue_track_info_t *track = &cue_ctx->tracks[cue_ctx->track_count];
    track->track_number = track_number;

    if (metadata) {
        if (metadata->track_title) {
            track->title = dsdpipe_strdup(metadata->track_title);
        }
        if (metadata->track_performer) {
            track->performer = dsdpipe_strdup(metadata->track_performer);
        } else if (cue_ctx->album_artist && !track->performer) {
            /* Fall back to album artist */
            track->performer = dsdpipe_strdup(cue_ctx->album_artist);
        }
        if (metadata->isrc[0]) {
            sa_strlcpy(track->isrc, metadata->isrc, sizeof(track->isrc));
        }
        track->start_frame = metadata->start_frame;
        track->duration_frames = metadata->duration_frames;
    }

    cue_ctx->current_track = track_number;
    cue_ctx->track_count++;

    return DSDPIPE_OK;
}

static int cue_sink_track_end(void *ctx, uint8_t track_number)
{
    dsdpipe_sink_cue_ctx_t *cue_ctx = (dsdpipe_sink_cue_ctx_t *)ctx;

    if (!cue_ctx || !cue_ctx->is_open) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    (void)track_number;
    return DSDPIPE_OK;
}

static int cue_sink_write_frame(void *ctx, const dsdpipe_buffer_t *buffer)
{
    (void)ctx;
    (void)buffer;
    /* CUE sheet is metadata-only; timing comes from start_frame/duration_frames */
    return DSDPIPE_OK;
}

static int cue_sink_finalize(void *ctx)
{
    dsdpipe_sink_cue_ctx_t *cue_ctx = (dsdpipe_sink_cue_ctx_t *)ctx;
    FILE *fd;
    char *escaped;

    if (!cue_ctx || !cue_ctx->is_open || !cue_ctx->path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }
    fd = sa_fopen(cue_ctx->path, "wb");
    if (!fd) {
        return DSDPIPE_ERROR_FILE_CREATE;
    }

    /* Write UTF-8 BOM */
    fputc(0xef, fd);
    fputc(0xbb, fd);
    fputc(0xbf, fd);

    /* Write header comment */
    fprintf(fd, "\nREM Generated by libdsdpipe\n");

    /* Write genre */
    if (cue_ctx->genre && cue_ctx->genre[0]) {
        fprintf(fd, "REM GENRE %s\n", cue_ctx->genre);
    }

    /* Write date */
    if (cue_ctx->year > 0) {
        if (cue_ctx->month > 0 && cue_ctx->day > 0) {
            fprintf(fd, "REM DATE %04d-%02d-%02d\n",
                    cue_ctx->year, cue_ctx->month, cue_ctx->day);
        } else {
            fprintf(fd, "REM DATE %04d\n", cue_ctx->year);
        }
    }

    /* Write disc info */
    if (cue_ctx->disc_total > 1) {
        fprintf(fd, "REM DISC %d / %d\n", cue_ctx->disc_number, cue_ctx->disc_total);
    }

    /* Write performer */
    if (cue_ctx->album_artist && cue_ctx->album_artist[0]) {
        escaped = cue_escape_string(cue_ctx->album_artist);
        if (escaped) {
            fprintf(fd, "PERFORMER \"%s\"\n", escaped);
            sa_free(escaped);
        }
    }

    /* Write title */
    if (cue_ctx->album_title && cue_ctx->album_title[0]) {
        escaped = cue_escape_string(cue_ctx->album_title);
        if (escaped) {
            fprintf(fd, "TITLE \"%s\"\n", escaped);
            sa_free(escaped);
        }
    }

    /* Write catalog number (truncated to 16 chars, trimmed) */
    if (cue_ctx->catalog_number && cue_ctx->catalog_number[0]) {
        char catalog_buf[CUE_MAX_CATALOG];
        sa_strlcpy(catalog_buf, cue_ctx->catalog_number, sizeof(catalog_buf));

        /* Trim trailing whitespace */
        size_t len = strlen(catalog_buf);
        while (len > 0 && catalog_buf[len - 1] == ' ') {
            catalog_buf[--len] = '\0';
        }

        if (catalog_buf[0] != '\0') {
            escaped = cue_escape_string(catalog_buf);
            if (escaped) {
                fprintf(fd, "CATALOG \"%s\"\n", escaped);
                sa_free(escaped);
            }
        }
    }

    /* Write audio file reference */
    {
        const char *audio_file = cue_ctx->audio_filename
                                     ? cue_ctx->audio_filename : "audio.dff";
        escaped = cue_escape_string(audio_file);
        if (escaped) {
            fprintf(fd, "FILE \"%s\" WAVE\n", escaped);
            sa_free(escaped);
        }
    }

    /* Write tracks using frame-based timing from SACD TOC */
    {
        uint64_t prev_abs_end = 0;

        for (int i = 0; i < cue_ctx->track_count; i++) {
            cue_track_info_t *track = &cue_ctx->tracks[i];

            fprintf(fd, "  TRACK %02d AUDIO\n", track->track_number);

            /* Track title */
            if (track->title && track->title[0]) {
                escaped = cue_escape_string(track->title);
                if (escaped) {
                    fprintf(fd, "      TITLE \"%s\"\n", escaped);
                    sa_free(escaped);
                }
            }

            /* Track performer */
            if (track->performer && track->performer[0]) {
                escaped = cue_escape_string(track->performer);
                if (escaped) {
                    fprintf(fd, "      PERFORMER \"%s\"\n", escaped);
                    sa_free(escaped);
                }
            }

            /* ISRC */
            if (track->isrc[0]) {
                fprintf(fd, "      ISRC %s\n", track->isrc);
            }

            /* INDEX 00 (pre-gap) and INDEX 01 (track start) */
            if ((uint64_t)track->start_frame > prev_abs_end) {
                /* There is a gap between previous track end and this track start.
                 * Write INDEX 00 at the previous track's end position. */
                int prev_sec = (int)(prev_abs_end / CUE_FRAMES_PER_SEC);
                fprintf(fd, "      INDEX 00 %02d:%02d:%02d\n",
                        prev_sec / 60,
                        prev_sec % 60,
                        (int)(prev_abs_end % CUE_FRAMES_PER_SEC));

                fprintf(fd, "      INDEX 01 %02d:%02d:%02d\n",
                        (int)((track->start_frame / CUE_FRAMES_PER_SEC) / 60),
                        (int)((track->start_frame / CUE_FRAMES_PER_SEC) % 60),
                        (int)(track->start_frame % CUE_FRAMES_PER_SEC));
            } else {
                fprintf(fd, "      INDEX 01 %02d:%02d:%02d\n",
                        (int)((track->start_frame / CUE_FRAMES_PER_SEC) / 60),
                        (int)((track->start_frame / CUE_FRAMES_PER_SEC) % 60),
                        (int)(track->start_frame % CUE_FRAMES_PER_SEC));
            }

            /* Update previous track absolute end for next iteration */
            prev_abs_end = (uint64_t)track->start_frame +
                           (uint64_t)track->duration_frames;
        }
    }

    fclose(fd);
    return DSDPIPE_OK;
}

static uint32_t cue_sink_get_capabilities(void *ctx)
{
    (void)ctx;
    /* Metadata-only sink */
    return DSDPIPE_SINK_CAP_METADATA | DSDPIPE_SINK_CAP_MARKERS;
}

static void cue_sink_destroy(void *ctx)
{
    dsdpipe_sink_cue_ctx_t *cue_ctx = (dsdpipe_sink_cue_ctx_t *)ctx;

    if (!cue_ctx) {
        return;
    }

    cue_sink_close(ctx);
    sa_free(cue_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_sink_ops_t s_cue_sink_ops = {
    .open = cue_sink_open,
    .close = cue_sink_close,
    .track_start = cue_sink_track_start,
    .track_end = cue_sink_track_end,
    .write_frame = cue_sink_write_frame,
    .finalize = cue_sink_finalize,
    .get_capabilities = cue_sink_get_capabilities,
    .destroy = cue_sink_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_sink_cue_create(dsdpipe_sink_t *sink, const char *audio_filename)
{
    dsdpipe_sink_cue_ctx_t *ctx;

    if (!sink) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    ctx = (dsdpipe_sink_cue_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Store audio filename reference */
    if (audio_filename) {
        ctx->audio_filename = dsdpipe_strdup(audio_filename);
    }

    sink->type = DSDPIPE_SINK_CUE;
    sink->ops = &s_cue_sink_ops;
    sink->ctx = ctx;
    sink->is_open = false;

    return DSDPIPE_OK;
}
