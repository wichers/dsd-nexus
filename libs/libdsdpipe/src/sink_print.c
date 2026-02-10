/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Human-readable text metadata sink
 * Outputs formatted metadata to stdout or file.
 * Metadata-only sink - ignores audio data.
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
#include <libsautil/compat.h>

#include <string.h>
#include <stdio.h>

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum number of tracks to collect */
#define PRINT_MAX_TRACKS    255

/*============================================================================
 * Track Info Structure
 *============================================================================*/

typedef struct print_track_info_s {
    uint8_t track_number;
    dsdpipe_metadata_t metadata;
    uint64_t frame_count;
    double duration_seconds;
    bool has_metadata;
} print_track_info_t;

/*============================================================================
 * Print Sink Context
 *============================================================================*/

typedef struct dsdpipe_sink_print_ctx_s {
    /* Configuration */
    char *path;                     /**< Output path (NULL = stdout) */
    FILE *output;                   /**< Output file handle */
    bool use_stdout;                /**< True if writing to stdout */

    /* Format info */
    dsdpipe_format_t format;

    /* Album metadata */
    dsdpipe_metadata_t album_metadata;
    bool has_album_metadata;

    /* Track collection */
    print_track_info_t tracks[PRINT_MAX_TRACKS];
    uint8_t track_count;
    uint8_t current_track;

    /* State */
    bool is_open;
} dsdpipe_sink_print_ctx_t;

/*============================================================================
 * Helper Functions
 *============================================================================*/

static void print_separator(FILE *out)
{
    sa_fprintf(out, "================================================================================\n");
}

static void print_subseparator(FILE *out)
{
    sa_fprintf(out, "--------------------------------------------------------------------------------\n");
}

static void print_field(FILE *out, const char *label, const char *value)
{
    if (value && value[0]) {
        sa_fprintf(out, "  %-20s: %s\n", label, value);
    }
}

static void print_int_field(FILE *out, const char *label, int value)
{
    if (value > 0) {
        sa_fprintf(out, "  %-20s: %d\n", label, value);
    }
}

static void print_duration(FILE *out, const char *label, double seconds)
{
    if (seconds > 0) {
        int mins = (int)(seconds / 60);
        int secs = (int)(seconds) % 60;
        int frames = (int)((seconds - (int)seconds) * 75);
        sa_fprintf(out, "  %-20s: %02d:%02d:%02d [MM:SS:FF]\n", label, mins, secs, frames);
    }
}

/**
 * @brief Callback for printing all tags
 */
static int print_tag_callback(void *ctx, const char *key, const char *value)
{
    FILE *out = (FILE *)ctx;
    if (key && value && value[0]) {
        sa_fprintf(out, "      %-16s: %s\n", key, value);
    }
    return 0;  /* Continue enumeration */
}

/*============================================================================
 * Sink Operations
 *============================================================================*/

static int print_sink_open(void *ctx, const char *path,
                            const dsdpipe_format_t *format,
                            const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_print_ctx_t *print_ctx = (dsdpipe_sink_print_ctx_t *)ctx;

    if (!print_ctx) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Determine output destination */
    if (path && path[0]) {
        print_ctx->path = dsdpipe_strdup(path);
        if (!print_ctx->path) {
            return DSDPIPE_ERROR_OUT_OF_MEMORY;
        }
        /* File will be opened at finalize() to avoid empty files on error */
        print_ctx->use_stdout = false;
    } else {
        print_ctx->path = NULL;
        print_ctx->use_stdout = true;
    }

    /* Store format */
    if (format) {
        print_ctx->format = *format;
    }

    /* Store album metadata */
    if (metadata) {
        int result = dsdpipe_metadata_copy(&print_ctx->album_metadata, metadata);
        if (result == DSDPIPE_OK) {
            print_ctx->has_album_metadata = true;
        }
    }

    /* Initialize track collection */
    print_ctx->track_count = 0;
    print_ctx->current_track = 0;
    memset(print_ctx->tracks, 0, sizeof(print_ctx->tracks));

    print_ctx->is_open = true;
    return DSDPIPE_OK;
}

static void print_sink_close(void *ctx)
{
    dsdpipe_sink_print_ctx_t *print_ctx = (dsdpipe_sink_print_ctx_t *)ctx;

    if (!print_ctx) {
        return;
    }

    if (print_ctx->output && !print_ctx->use_stdout) {
        fclose(print_ctx->output);
        print_ctx->output = NULL;
    }

    if (print_ctx->path) {
        sa_free(print_ctx->path);
        print_ctx->path = NULL;
    }

    /* Free album metadata */
    dsdpipe_metadata_free(&print_ctx->album_metadata);
    print_ctx->has_album_metadata = false;

    /* Free track metadata */
    for (int i = 0; i < print_ctx->track_count; i++) {
        dsdpipe_metadata_free(&print_ctx->tracks[i].metadata);
    }
    print_ctx->track_count = 0;

    print_ctx->is_open = false;
}

static int print_sink_track_start(void *ctx, uint8_t track_number,
                                   const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_print_ctx_t *print_ctx = (dsdpipe_sink_print_ctx_t *)ctx;

    if (!print_ctx || !print_ctx->is_open) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (print_ctx->track_count >= PRINT_MAX_TRACKS) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store track info */
    print_track_info_t *track = &print_ctx->tracks[print_ctx->track_count];
    track->track_number = track_number;
    track->frame_count = 0;
    track->duration_seconds = 0;

    if (metadata) {
        int result = dsdpipe_metadata_copy(&track->metadata, metadata);
        track->has_metadata = (result == DSDPIPE_OK);

        /* Use duration from metadata if available */
        if (metadata->duration_seconds > 0) {
            track->duration_seconds = metadata->duration_seconds;
        }
    }

    print_ctx->current_track = track_number;
    print_ctx->track_count++;

    return DSDPIPE_OK;
}

static int print_sink_track_end(void *ctx, uint8_t track_number)
{
    dsdpipe_sink_print_ctx_t *print_ctx = (dsdpipe_sink_print_ctx_t *)ctx;

    if (!print_ctx || !print_ctx->is_open) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    (void)track_number;  /* Track info already stored */
    return DSDPIPE_OK;
}

static int print_sink_write_frame(void *ctx, const dsdpipe_buffer_t *buffer)
{
    dsdpipe_sink_print_ctx_t *print_ctx = (dsdpipe_sink_print_ctx_t *)ctx;

    if (!print_ctx || !print_ctx->is_open || !buffer) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Metadata-only sink - count frames but ignore audio data */
    if (print_ctx->track_count > 0) {
        print_track_info_t *track = &print_ctx->tracks[print_ctx->track_count - 1];
        track->frame_count++;
    }

    return DSDPIPE_OK;
}

static int print_sink_finalize(void *ctx)
{
    dsdpipe_sink_print_ctx_t *print_ctx = (dsdpipe_sink_print_ctx_t *)ctx;
    FILE *out;

    if (!print_ctx || !print_ctx->is_open) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Open output file if needed */
    if (print_ctx->use_stdout) {
        out = stdout;
    } else {
        out = sa_fopen(print_ctx->path, "w");
        if (!out) {
            return DSDPIPE_ERROR_FILE_CREATE;
        }
        print_ctx->output = out;
    }

    /* Print header */
    print_separator(out);
    sa_fprintf(out, "DSD Audio Metadata Summary\n");
    print_separator(out);
    sa_fprintf(out, "\n");

    /* Print format info */
    sa_fprintf(out, "Audio Format:\n");
    print_subseparator(out);
    {
        const char *type_str = "Unknown";
        switch (print_ctx->format.type) {
            case DSDPIPE_FORMAT_DSD_RAW: type_str = "DSD Raw"; break;
            case DSDPIPE_FORMAT_DST: type_str = "DST Compressed"; break;
            case DSDPIPE_FORMAT_PCM_INT16: type_str = "PCM 16-bit"; break;
            case DSDPIPE_FORMAT_PCM_INT24: type_str = "PCM 24-bit"; break;
            case DSDPIPE_FORMAT_PCM_INT32: type_str = "PCM 32-bit"; break;
            case DSDPIPE_FORMAT_PCM_FLOAT32: type_str = "PCM Float32"; break;
            case DSDPIPE_FORMAT_PCM_FLOAT64: type_str = "PCM Float64"; break;
            default: break;
        }
        print_field(out, "Type", type_str);
    }
    print_int_field(out, "Sample Rate", (int)print_ctx->format.sample_rate);
    print_int_field(out, "Channels", print_ctx->format.channel_count);
    print_int_field(out, "Bits/Sample", print_ctx->format.bits_per_sample);
    sa_fprintf(out, "\n");

    /* Print album metadata */
    if (print_ctx->has_album_metadata) {
        const dsdpipe_metadata_t *album = &print_ctx->album_metadata;

        sa_fprintf(out, "Album Information:\n");
        print_subseparator(out);
        print_field(out, "Title", album->album_title);
        print_field(out, "Artist", album->album_artist);
        print_field(out, "Publisher", album->album_publisher);
        print_field(out, "Copyright", album->album_copyright);
        print_field(out, "Catalog #", album->catalog_number);
        print_field(out, "Genre", album->genre);

        if (album->year > 0) {
            if (album->month > 0 && album->day > 0) {
                sa_fprintf(out, "  %-20s: %04d-%02d-%02d\n", "Date",
                           album->year, album->month, album->day);
            } else {
                print_int_field(out, "Year", album->year);
            }
        }

        if (album->disc_number > 0) {
            if (album->disc_total > 0) {
                sa_fprintf(out, "  %-20s: %d/%d\n", "Disc",
                           album->disc_number, album->disc_total);
            } else {
                print_int_field(out, "Disc", album->disc_number);
            }
        }

        print_int_field(out, "Total Tracks", album->track_total);

        /* Print additional tags */
        if (album->tags && dsdpipe_metadata_tag_count(album) > 0) {
            sa_fprintf(out, "\n  Additional Tags:\n");
            dsdpipe_metadata_enumerate_tags(album, out, print_tag_callback);
        }

        sa_fprintf(out, "\n");
    }

    /* Print track list */
    if (print_ctx->track_count > 0) {
        sa_fprintf(out, "Track List (%d tracks):\n", print_ctx->track_count);
        print_subseparator(out);

        for (int i = 0; i < print_ctx->track_count; i++) {
            print_track_info_t *track = &print_ctx->tracks[i];
            const dsdpipe_metadata_t *meta = &track->metadata;

            sa_fprintf(out, "\n  Track %d:\n", track->track_number);

            if (track->has_metadata) {
                print_field(out, "    Title", meta->track_title);
                print_field(out, "    Performer", meta->track_performer);
                print_field(out, "    Composer", meta->track_composer);
                print_field(out, "    Arranger", meta->track_arranger);
                print_field(out, "    Songwriter", meta->track_songwriter);
                print_field(out, "    Message", meta->track_message);

                if (meta->isrc[0]) {
                    print_field(out, "    ISRC", meta->isrc);
                }
            }

            /* Duration */
            if (track->duration_seconds > 0) {
                print_duration(out, "    Duration", track->duration_seconds);
            } else if (track->frame_count > 0) {
                double duration = (double)track->frame_count / 75.0;
                print_duration(out, "    Duration", duration);
            }

            /* Track tags */
            if (track->has_metadata && meta->tags &&
                dsdpipe_metadata_tag_count(meta) > 0) {
                sa_fprintf(out, "\n    Tags:\n");
                dsdpipe_metadata_enumerate_tags(meta, out, print_tag_callback);
            }
        }

        sa_fprintf(out, "\n");
    }

    print_separator(out);

    /* Flush output */
    fflush(out);

    return DSDPIPE_OK;
}

static uint32_t print_sink_get_capabilities(void *ctx)
{
    (void)ctx;
    /* Metadata-only sink - doesn't process audio data */
    return DSDPIPE_SINK_CAP_METADATA;
}

static void print_sink_destroy(void *ctx)
{
    dsdpipe_sink_print_ctx_t *print_ctx = (dsdpipe_sink_print_ctx_t *)ctx;

    if (!print_ctx) {
        return;
    }

    print_sink_close(ctx);
    sa_free(print_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_sink_ops_t s_print_sink_ops = {
    .open = print_sink_open,
    .close = print_sink_close,
    .track_start = print_sink_track_start,
    .track_end = print_sink_track_end,
    .write_frame = print_sink_write_frame,
    .finalize = print_sink_finalize,
    .get_capabilities = print_sink_get_capabilities,
    .destroy = print_sink_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_sink_print_create(dsdpipe_sink_t *sink)
{
    dsdpipe_sink_print_ctx_t *ctx;

    if (!sink) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    ctx = (dsdpipe_sink_print_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    sink->type = DSDPIPE_SINK_PRINT;
    sink->ops = &s_print_sink_ops;
    sink->ctx = ctx;
    sink->is_open = false;

    return DSDPIPE_OK;
}
