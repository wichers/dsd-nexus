/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief ID3v2.4 tag file sink
 * Generates ID3v2.4 tag files (.id3) that can be:
 * - Embedded in DSF/DSDIFF files
 * - Used as standalone metadata files
 * - Generated per-track for individual track metadata
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

#include <id3v2/id3v2.h>
#include <id3v2/id3v2Frame.h>
#include <id3v2/id3v2Context.h>
#include <id3v2/id3v2TagIdentity.h>
#include <id3v2/id3v2Types.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum number of tracks to collect for per-track mode */
#define ID3_MAX_TRACKS      255

/*============================================================================
 * Track Info Structure
 *============================================================================*/

typedef struct id3_track_info_s {
    uint8_t track_number;
    char *title;
    char *performer;
    char *composer;
    char *arranger;
    char *songwriter;
    char *message;
    char isrc[13];
} id3_track_info_t;

/*============================================================================
 * ID3 Sink Context
 *============================================================================*/

typedef struct dsdpipe_sink_id3_ctx_s {
    /* Configuration */
    char *path;                     /**< Output path (directory for per-track, file otherwise) */
    bool per_track;                 /**< Generate per-track ID3 files */

    /* Album metadata */
    char *album_title;
    char *album_artist;
    char *album_publisher;
    char *album_copyright;
    char *catalog_number;
    char *genre;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint16_t disc_number;
    uint16_t disc_total;
    uint16_t track_total;

    /* Track collection */
    id3_track_info_t tracks[ID3_MAX_TRACKS];
    uint8_t track_count;
    uint8_t current_track_idx;

    /* State */
    bool is_open;
} dsdpipe_sink_id3_ctx_t;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Add a TXXX (user-defined text) frame
 */
static int add_txxx_frame(Id3v2Tag *tag, const char *description, const char *str)
{
    if (!str || !tag || !description) {
        return -1;
    }

    List *context = id3v2CreateUserDefinedTextFrameContext();
    if (!context) {
        return -1;
    }

    List *entries = listCreate(id3v2PrintContentEntry, id3v2DeleteContentEntry,
                               id3v2CompareContentEntry, id3v2CopyContentEntry);
    if (!entries) {
        listFree(context);
        return -1;
    }

    /* Add encoding byte (UTF-8 = 0x03) */
    Id3v2ContentEntry *entry = id3v2CreateContentEntry((void *)"\x03", 1);
    if (entry) {
        listInsertBack(entries, entry);
    }

    entry = id3v2CreateContentEntry((void *)description, strlen(description) + 1);
    if (entry) {
        listInsertBack(entries, entry);
    }

    entry = id3v2CreateContentEntry((void *)str, strlen(str) + 1);
    if (entry) {
        listInsertBack(entries, entry);
    }

    /* Create frame header */
    Id3v2FrameHeader *frameHeader = id3v2CreateFrameHeader(
        (uint8_t *)"TXXX", 0, 0, 0, 0, 0, 0, 0);

    if (!frameHeader) {
        listFree(entries);
        listFree(context);
        return -1;
    }

    /* Create and attach frame */
    Id3v2Frame *frame = id3v2CreateFrame(frameHeader, context, entries);
    if (!frame) {
        return -1;
    }

    return id3v2AttachFrameToTag(tag, frame);
}

/**
 * @brief Create and serialize an ID3v2.4 tag from metadata
 */
static int create_id3_tag(const dsdpipe_sink_id3_ctx_t *ctx,
                          const id3_track_info_t *track,
                          uint8_t **out_data, size_t *out_size)
{
    Id3v2Tag *tag = NULL;
    Id3v2TagHeader *header = NULL;
    List *frames = NULL;
    char tmp[64];
    uint8_t *serialized = NULL;
    size_t tag_size = 0;

    /* Create ID3v2.4 tag structure */
    header = id3v2CreateTagHeader(ID3V2_TAG_VERSION_4, 0, 0, NULL);
    if (!header) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    frames = listCreate(id3v2PrintFrame, id3v2DeleteFrame,
                        id3v2CompareFrame, id3v2CopyFrame);
    if (!frames) {
        id3v2DestroyTagHeader(&header);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    tag = id3v2CreateTag(header, frames);
    if (!tag) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* TIT2: Track title */
    if (track && track->title && track->title[0]) {
        id3v2InsertTextFrame("TIT2", ID3V2_ENCODING_UTF8, track->title, tag);
    }

    /* TALB: Album title */
    if (ctx->album_title && ctx->album_title[0]) {
        id3v2InsertTextFrame("TALB", ID3V2_ENCODING_UTF8, ctx->album_title, tag);
    }

    /* TPE1: Track artist/performer */
    if (track && track->performer && track->performer[0]) {
        id3v2InsertTextFrame("TPE1", ID3V2_ENCODING_UTF8, track->performer, tag);
    } else if (ctx->album_artist && ctx->album_artist[0]) {
        /* Fall back to album artist */
        id3v2InsertTextFrame("TPE1", ID3V2_ENCODING_UTF8, ctx->album_artist, tag);
    }

    /* TPE2: Album artist */
    if (ctx->album_artist && ctx->album_artist[0]) {
        id3v2InsertTextFrame("TPE2", ID3V2_ENCODING_UTF8, ctx->album_artist, tag);
    }

    /* TCOM: Composer */
    if (track && track->composer && track->composer[0]) {
        id3v2InsertTextFrame("TCOM", ID3V2_ENCODING_UTF8, track->composer, tag);
    }

    /* TEXT: Lyricist/Songwriter */
    if (track && track->songwriter && track->songwriter[0]) {
        id3v2InsertTextFrame("TEXT", ID3V2_ENCODING_UTF8, track->songwriter, tag);
    }

    /* TXXX:Arranger */
    if (track && track->arranger && track->arranger[0]) {
        add_txxx_frame(tag, "Arranger", track->arranger);
    }

    /* TXXX:Comment */
    if (track && track->message && track->message[0]) {
        add_txxx_frame(tag, "Comment", track->message);
    }

    /* TSRC: ISRC code */
    if (track && track->isrc[0]) {
        id3v2InsertTextFrame("TSRC", ID3V2_ENCODING_UTF8, track->isrc, tag);
    }

    /* TPUB: Publisher */
    if (ctx->album_publisher && ctx->album_publisher[0]) {
        id3v2InsertTextFrame("TPUB", ID3V2_ENCODING_UTF8, ctx->album_publisher, tag);
    }

    /* TCOP: Copyright */
    if (ctx->album_copyright && ctx->album_copyright[0]) {
        id3v2InsertTextFrame("TCOP", ID3V2_ENCODING_UTF8, ctx->album_copyright, tag);
    }

    /* TCON: Genre */
    if (ctx->genre && ctx->genre[0]) {
        id3v2InsertTextFrame("TCON", ID3V2_ENCODING_UTF8, ctx->genre, tag);
    }

    /* TDRC: Recording date (ISO 8601) */
    if (ctx->year > 0) {
        if (ctx->month > 0 && ctx->day > 0) {
#ifdef _MSC_VER
            sprintf_s(tmp, sizeof(tmp), "%04d-%02d-%02d", ctx->year, ctx->month, ctx->day);
#else
            snprintf(tmp, sizeof(tmp), "%04d-%02d-%02d", ctx->year, ctx->month, ctx->day);
#endif
        } else {
#ifdef _MSC_VER
            sprintf_s(tmp, sizeof(tmp), "%04d", ctx->year);
#else
            snprintf(tmp, sizeof(tmp), "%04d", ctx->year);
#endif
        }
        id3v2InsertTextFrame("TDRC", ID3V2_ENCODING_UTF8, tmp, tag);
    }

    /* TRCK: Track number/total */
    if (track) {
        if (ctx->track_total > 0) {
#ifdef _MSC_VER
            sprintf_s(tmp, sizeof(tmp), "%d/%d", track->track_number, ctx->track_total);
#else
            snprintf(tmp, sizeof(tmp), "%d/%d", track->track_number, ctx->track_total);
#endif
        } else {
#ifdef _MSC_VER
            sprintf_s(tmp, sizeof(tmp), "%d", track->track_number);
#else
            snprintf(tmp, sizeof(tmp), "%d", track->track_number);
#endif
        }
        id3v2InsertTextFrame("TRCK", ID3V2_ENCODING_UTF8, tmp, tag);
    }

    /* TPOS: Disc number/total */
    if (ctx->disc_number > 0) {
        if (ctx->disc_total > 0) {
#ifdef _MSC_VER
            sprintf_s(tmp, sizeof(tmp), "%d/%d", ctx->disc_number, ctx->disc_total);
#else
            snprintf(tmp, sizeof(tmp), "%d/%d", ctx->disc_number, ctx->disc_total);
#endif
        } else {
#ifdef _MSC_VER
            sprintf_s(tmp, sizeof(tmp), "%d", ctx->disc_number);
#else
            snprintf(tmp, sizeof(tmp), "%d", ctx->disc_number);
#endif
        }
        id3v2InsertTextFrame("TPOS", ID3V2_ENCODING_UTF8, tmp, tag);
    }

    /* Serialize tag */
    serialized = id3v2TagSerialize(tag, &tag_size);
    if (!serialized) {
        id3v2DestroyTag(&tag);
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    *out_data = serialized;
    *out_size = tag_size;

    id3v2DestroyTag(&tag);
    return DSDPIPE_OK;
}

/**
 * @brief Write ID3 tag to file
 */
static int write_id3_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *fd;
    fd = sa_fopen(path, "wb");
    if (!fd) {
        return DSDPIPE_ERROR_FILE_CREATE;
    }

    size_t written = fwrite(data, 1, size, fd);
    fclose(fd);

    if (written != size) {
        return DSDPIPE_ERROR_FILE_WRITE;
    }

    return DSDPIPE_OK;
}

/*============================================================================
 * Sink Operations
 *============================================================================*/

static int id3_sink_open(void *ctx, const char *path,
                          const dsdpipe_format_t *format,
                          const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_id3_ctx_t *id3_ctx = (dsdpipe_sink_id3_ctx_t *)ctx;

    if (!id3_ctx || !path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    (void)format;  /* Not needed for ID3 */

    /* Store output path */
    id3_ctx->path = dsdpipe_strdup(path);
    if (!id3_ctx->path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Store album metadata */
    if (metadata) {
        if (metadata->album_title) {
            id3_ctx->album_title = dsdpipe_strdup(metadata->album_title);
        }
        if (metadata->album_artist) {
            id3_ctx->album_artist = dsdpipe_strdup(metadata->album_artist);
        }
        if (metadata->album_publisher) {
            id3_ctx->album_publisher = dsdpipe_strdup(metadata->album_publisher);
        }
        if (metadata->album_copyright) {
            id3_ctx->album_copyright = dsdpipe_strdup(metadata->album_copyright);
        }
        if (metadata->catalog_number) {
            id3_ctx->catalog_number = dsdpipe_strdup(metadata->catalog_number);
        }
        if (metadata->genre) {
            id3_ctx->genre = dsdpipe_strdup(metadata->genre);
        }
        id3_ctx->year = metadata->year;
        id3_ctx->month = metadata->month;
        id3_ctx->day = metadata->day;
        id3_ctx->disc_number = metadata->disc_number;
        id3_ctx->disc_total = metadata->disc_total;
        id3_ctx->track_total = metadata->track_total;
    }

    /* Initialize track collection */
    id3_ctx->track_count = 0;
    id3_ctx->current_track_idx = 0;
    memset(id3_ctx->tracks, 0, sizeof(id3_ctx->tracks));

    id3_ctx->is_open = true;
    return DSDPIPE_OK;
}

static void id3_sink_close(void *ctx)
{
    dsdpipe_sink_id3_ctx_t *id3_ctx = (dsdpipe_sink_id3_ctx_t *)ctx;

    if (!id3_ctx) {
        return;
    }

    /* Free path */
    if (id3_ctx->path) {
        sa_free(id3_ctx->path);
        id3_ctx->path = NULL;
    }

    /* Free album metadata */
    if (id3_ctx->album_title) {
        sa_free(id3_ctx->album_title);
        id3_ctx->album_title = NULL;
    }
    if (id3_ctx->album_artist) {
        sa_free(id3_ctx->album_artist);
        id3_ctx->album_artist = NULL;
    }
    if (id3_ctx->album_publisher) {
        sa_free(id3_ctx->album_publisher);
        id3_ctx->album_publisher = NULL;
    }
    if (id3_ctx->album_copyright) {
        sa_free(id3_ctx->album_copyright);
        id3_ctx->album_copyright = NULL;
    }
    if (id3_ctx->catalog_number) {
        sa_free(id3_ctx->catalog_number);
        id3_ctx->catalog_number = NULL;
    }
    if (id3_ctx->genre) {
        sa_free(id3_ctx->genre);
        id3_ctx->genre = NULL;
    }

    /* Free track metadata */
    for (int i = 0; i < id3_ctx->track_count; i++) {
        if (id3_ctx->tracks[i].title) {
            sa_free(id3_ctx->tracks[i].title);
        }
        if (id3_ctx->tracks[i].performer) {
            sa_free(id3_ctx->tracks[i].performer);
        }
        if (id3_ctx->tracks[i].composer) {
            sa_free(id3_ctx->tracks[i].composer);
        }
        if (id3_ctx->tracks[i].arranger) {
            sa_free(id3_ctx->tracks[i].arranger);
        }
        if (id3_ctx->tracks[i].songwriter) {
            sa_free(id3_ctx->tracks[i].songwriter);
        }
        if (id3_ctx->tracks[i].message) {
            sa_free(id3_ctx->tracks[i].message);
        }
    }
    id3_ctx->track_count = 0;

    id3_ctx->is_open = false;
}

static int id3_sink_track_start(void *ctx, uint8_t track_number,
                                 const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_id3_ctx_t *id3_ctx = (dsdpipe_sink_id3_ctx_t *)ctx;

    if (!id3_ctx || !id3_ctx->is_open) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (id3_ctx->track_count >= ID3_MAX_TRACKS) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store track info */
    id3_track_info_t *track = &id3_ctx->tracks[id3_ctx->track_count];
    track->track_number = track_number;

    if (metadata) {
        if (metadata->track_title) {
            track->title = dsdpipe_strdup(metadata->track_title);
        }
        if (metadata->track_performer) {
            track->performer = dsdpipe_strdup(metadata->track_performer);
        }
        if (metadata->track_composer) {
            track->composer = dsdpipe_strdup(metadata->track_composer);
        }
        if (metadata->track_arranger) {
            track->arranger = dsdpipe_strdup(metadata->track_arranger);
        }
        if (metadata->track_songwriter) {
            track->songwriter = dsdpipe_strdup(metadata->track_songwriter);
        }
        if (metadata->track_message) {
            track->message = dsdpipe_strdup(metadata->track_message);
        }
        if (metadata->isrc[0]) {
            sa_strlcpy(track->isrc, metadata->isrc, sizeof(track->isrc));
        }
    }

    id3_ctx->current_track_idx = id3_ctx->track_count;
    id3_ctx->track_count++;

    return DSDPIPE_OK;
}

static int id3_sink_track_end(void *ctx, uint8_t track_number)
{
    dsdpipe_sink_id3_ctx_t *id3_ctx = (dsdpipe_sink_id3_ctx_t *)ctx;

    if (!id3_ctx || !id3_ctx->is_open) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    (void)track_number;

    /* In per-track mode, write ID3 file immediately */
    if (id3_ctx->per_track && id3_ctx->current_track_idx < id3_ctx->track_count) {
        id3_track_info_t *track = &id3_ctx->tracks[id3_ctx->current_track_idx];
        uint8_t *tag_data = NULL;
        size_t tag_size = 0;

        int result = create_id3_tag(id3_ctx, track, &tag_data, &tag_size);
        if (result != DSDPIPE_OK) {
            return result;
        }

        /* Generate per-track filename */
        char track_path[512];
#ifdef _MSC_VER
        sprintf_s(track_path, sizeof(track_path), "%s\\track%02d.id3",
                  id3_ctx->path, track->track_number);
#else
        snprintf(track_path, sizeof(track_path), "%s/track%02d.id3",
                 id3_ctx->path, track->track_number);
#endif

        result = write_id3_file(track_path, tag_data, tag_size);
        free(tag_data);

        if (result != DSDPIPE_OK) {
            return result;
        }
    }

    return DSDPIPE_OK;
}

static int id3_sink_write_frame(void *ctx, const dsdpipe_buffer_t *buffer)
{
    (void)ctx;
    (void)buffer;
    /* ID3 sink doesn't process audio data */
    return DSDPIPE_OK;
}

static int id3_sink_finalize(void *ctx)
{
    dsdpipe_sink_id3_ctx_t *id3_ctx = (dsdpipe_sink_id3_ctx_t *)ctx;

    if (!id3_ctx || !id3_ctx->is_open || !id3_ctx->path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* In per-track mode, files are already written in track_end */
    if (id3_ctx->per_track) {
        return DSDPIPE_OK;
    }

    /* Single-file mode: write first track (or album-only metadata) */
    id3_track_info_t *track = (id3_ctx->track_count > 0) ? &id3_ctx->tracks[0] : NULL;
    uint8_t *tag_data = NULL;
    size_t tag_size = 0;

    int result = create_id3_tag(id3_ctx, track, &tag_data, &tag_size);
    if (result != DSDPIPE_OK) {
        return result;
    }

    result = write_id3_file(id3_ctx->path, tag_data, tag_size);
    free(tag_data);

    return result;
}

static uint32_t id3_sink_get_capabilities(void *ctx)
{
    (void)ctx;
    /* Metadata-only sink */
    return DSDPIPE_SINK_CAP_METADATA;
}

static void id3_sink_destroy(void *ctx)
{
    dsdpipe_sink_id3_ctx_t *id3_ctx = (dsdpipe_sink_id3_ctx_t *)ctx;

    if (!id3_ctx) {
        return;
    }

    id3_sink_close(ctx);
    sa_free(id3_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_sink_ops_t s_id3_sink_ops = {
    .open = id3_sink_open,
    .close = id3_sink_close,
    .track_start = id3_sink_track_start,
    .track_end = id3_sink_track_end,
    .write_frame = id3_sink_write_frame,
    .finalize = id3_sink_finalize,
    .get_capabilities = id3_sink_get_capabilities,
    .destroy = id3_sink_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_sink_id3_create(dsdpipe_sink_t *sink, bool per_track)
{
    dsdpipe_sink_id3_ctx_t *ctx;

    if (!sink) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    ctx = (dsdpipe_sink_id3_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    ctx->per_track = per_track;

    sink->type = DSDPIPE_SINK_ID3;
    sink->ops = &s_id3_sink_ops;
    sink->ctx = ctx;
    sink->is_open = false;

    return DSDPIPE_OK;
}

/*============================================================================
 * Public API for ID3 Tag Buffer Generation
 *============================================================================*/

int dsdpipe_id3_render(const dsdpipe_metadata_t *metadata,
                        uint8_t track_number,
                        uint8_t **out_data,
                        size_t *out_size)
{
    dsdpipe_sink_id3_ctx_t temp_ctx;
    id3_track_info_t temp_track;

    if (!metadata || !out_data || !out_size) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Set up temporary context from metadata */
    memset(&temp_ctx, 0, sizeof(temp_ctx));
    temp_ctx.album_title = (char *)metadata->album_title;
    temp_ctx.album_artist = (char *)metadata->album_artist;
    temp_ctx.album_publisher = (char *)metadata->album_publisher;
    temp_ctx.album_copyright = (char *)metadata->album_copyright;
    temp_ctx.catalog_number = (char *)metadata->catalog_number;
    temp_ctx.genre = (char *)metadata->genre;
    temp_ctx.year = metadata->year;
    temp_ctx.month = metadata->month;
    temp_ctx.day = metadata->day;
    temp_ctx.disc_number = metadata->disc_number;
    temp_ctx.disc_total = metadata->disc_total;
    temp_ctx.track_total = metadata->track_total;

    /* Set up temporary track from metadata */
    memset(&temp_track, 0, sizeof(temp_track));
    temp_track.track_number = track_number;
    temp_track.title = (char *)metadata->track_title;
    temp_track.performer = (char *)metadata->track_performer;
    temp_track.composer = (char *)metadata->track_composer;
    temp_track.arranger = (char *)metadata->track_arranger;
    temp_track.songwriter = (char *)metadata->track_songwriter;
    temp_track.message = (char *)metadata->track_message;
    sa_strlcpy(temp_track.isrc, metadata->isrc, sizeof(temp_track.isrc));

    return create_id3_tag(&temp_ctx, &temp_track, out_data, out_size);
}
