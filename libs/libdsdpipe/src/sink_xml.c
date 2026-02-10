/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief XML metadata sink for edit master companion files
 * Generates XML metadata files (.xml) as companion to DSDIFF edit master files.
 * Uses the sxmlc library for XML generation.
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
#include <libsautil/sxmlc.h>
#include <libsautil/compat.h>
#include <libsautil/attributes.h>

#include <string.h>
#include <stdio.h>

/*============================================================================
 * Constants
 *============================================================================*/

/** Maximum number of tracks to collect */
#define XML_MAX_TRACKS      255

/** Frame rate for SACD timing (CD standard) */
#define XML_FRAMES_PER_SEC  75

/** Maximum catalog number length (SACD disc_catalog_number is 16 bytes) */
#define XML_MAX_CATALOG     17

/*============================================================================
 * Track Info Structure
 *============================================================================*/

typedef struct xml_track_info_s {
    uint8_t track_number;
    char *title;                    /**< Track title */
    char *performer;                /**< Track performer */
    char *composer;                 /**< Track composer */
    char *arranger;                 /**< Track arranger */
    char *songwriter;               /**< Track songwriter */
    char *message;                  /**< Track message/comment */
    char isrc[13];                  /**< ISRC code */
    uint32_t start_frame;           /**< Start position in SACD frames (75fps) */
    uint32_t duration_frames;       /**< Duration in SACD frames (75fps) */
    double duration_seconds;        /**< Duration in seconds */
} xml_track_info_t;

/*============================================================================
 * XML Sink Context
 *============================================================================*/

typedef struct dsdpipe_sink_xml_ctx_s {
    /* Configuration */
    char *path;                     /**< Output XML file path */

    /* Format info */
    dsdpipe_format_t format;
    uint32_t sample_rate;

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
    xml_track_info_t tracks[XML_MAX_TRACKS];
    uint8_t track_count;
    uint8_t current_track_idx;

    /* State */
    bool is_open;
} dsdpipe_sink_xml_ctx_t;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Add a text element to an XML node
 */
static XMLNode *xml_add_text_element(XMLNode *parent, const char *tag, const char *text)
{
    XMLNode *node;

    if (!parent || !tag || !text || !text[0]) {
        return NULL;
    }

    node = XMLNode_new(TAG_FATHER, tag, text);
    if (!node) {
        return NULL;
    }

    if (!XMLNode_add_child(parent, node)) {
        XMLNode_free(node);
        return NULL;
    }

    return node;
}

/**
 * @brief Add an integer element to an XML node
 */
static XMLNode *xml_add_int_element(XMLNode *parent, const char *tag, int value)
{
    char buf[32];
#ifdef _MSC_VER
    sprintf_s(buf, sizeof(buf), "%d", value);
#else
    snprintf(buf, sizeof(buf), "%d", value);
#endif
    return xml_add_text_element(parent, tag, buf);
}

/**
 * @brief Add a uint64 element to an XML node
 */
sa_unused
static XMLNode *xml_add_uint64_element(XMLNode *parent, const char *tag, uint64_t value)
{
    char buf[32];
#ifdef _MSC_VER
    sprintf_s(buf, sizeof(buf), "%llu", (unsigned long long)value);
#else
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
#endif
    return xml_add_text_element(parent, tag, buf);
}

/**
 * @brief Add a double element to an XML node
 */
static XMLNode *xml_add_double_element(XMLNode *parent, const char *tag, double value)
{
    char buf[32];
#ifdef _MSC_VER
    sprintf_s(buf, sizeof(buf), "%.6f", value);
#else
    snprintf(buf, sizeof(buf), "%.6f", value);
#endif
    return xml_add_text_element(parent, tag, buf);
}

/**
 * @brief Get format type string
 */
static const char *xml_format_type_string(dsdpipe_audio_format_t type)
{
    switch (type) {
        case DSDPIPE_FORMAT_DSD_RAW:      return "DSD";
        case DSDPIPE_FORMAT_DST:          return "DST";
        case DSDPIPE_FORMAT_PCM_INT16:    return "PCM_INT16";
        case DSDPIPE_FORMAT_PCM_INT24:    return "PCM_INT24";
        case DSDPIPE_FORMAT_PCM_INT32:    return "PCM_INT32";
        case DSDPIPE_FORMAT_PCM_FLOAT32:  return "PCM_FLOAT32";
        case DSDPIPE_FORMAT_PCM_FLOAT64:  return "PCM_FLOAT64";
        default:                          return "UNKNOWN";
    }
}

/*============================================================================
 * Sink Operations
 *============================================================================*/

static int xml_sink_open(void *ctx, const char *path,
                          const dsdpipe_format_t *format,
                          const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_xml_ctx_t *xml_ctx = (dsdpipe_sink_xml_ctx_t *)ctx;

    if (!xml_ctx || !path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store output path */
    xml_ctx->path = dsdpipe_strdup(path);
    if (!xml_ctx->path) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    /* Store format */
    if (format) {
        xml_ctx->format = *format;
        xml_ctx->sample_rate = format->sample_rate;
    }

    /* Store album metadata */
    if (metadata) {
        if (metadata->album_title) {
            xml_ctx->album_title = dsdpipe_strdup(metadata->album_title);
        }
        if (metadata->album_artist) {
            xml_ctx->album_artist = dsdpipe_strdup(metadata->album_artist);
        }
        if (metadata->album_publisher) {
            xml_ctx->album_publisher = dsdpipe_strdup(metadata->album_publisher);
        }
        if (metadata->album_copyright) {
            xml_ctx->album_copyright = dsdpipe_strdup(metadata->album_copyright);
        }
        if (metadata->catalog_number) {
            xml_ctx->catalog_number = dsdpipe_strdup(metadata->catalog_number);
        }
        if (metadata->genre) {
            xml_ctx->genre = dsdpipe_strdup(metadata->genre);
        }
        xml_ctx->year = metadata->year;
        xml_ctx->month = metadata->month;
        xml_ctx->day = metadata->day;
        xml_ctx->disc_number = metadata->disc_number;
        xml_ctx->disc_total = metadata->disc_total;
        xml_ctx->track_total = metadata->track_total;
    }

    /* Initialize track collection */
    xml_ctx->track_count = 0;
    xml_ctx->current_track_idx = 0;
    memset(xml_ctx->tracks, 0, sizeof(xml_ctx->tracks));

    xml_ctx->is_open = true;
    return DSDPIPE_OK;
}

static void xml_sink_close(void *ctx)
{
    dsdpipe_sink_xml_ctx_t *xml_ctx = (dsdpipe_sink_xml_ctx_t *)ctx;

    if (!xml_ctx) {
        return;
    }

    /* Free path */
    if (xml_ctx->path) {
        sa_free(xml_ctx->path);
        xml_ctx->path = NULL;
    }

    /* Free album metadata */
    if (xml_ctx->album_title) {
        sa_free(xml_ctx->album_title);
        xml_ctx->album_title = NULL;
    }
    if (xml_ctx->album_artist) {
        sa_free(xml_ctx->album_artist);
        xml_ctx->album_artist = NULL;
    }
    if (xml_ctx->album_publisher) {
        sa_free(xml_ctx->album_publisher);
        xml_ctx->album_publisher = NULL;
    }
    if (xml_ctx->album_copyright) {
        sa_free(xml_ctx->album_copyright);
        xml_ctx->album_copyright = NULL;
    }
    if (xml_ctx->catalog_number) {
        sa_free(xml_ctx->catalog_number);
        xml_ctx->catalog_number = NULL;
    }
    if (xml_ctx->genre) {
        sa_free(xml_ctx->genre);
        xml_ctx->genre = NULL;
    }

    /* Free track metadata */
    for (int i = 0; i < xml_ctx->track_count; i++) {
        if (xml_ctx->tracks[i].title) {
            sa_free(xml_ctx->tracks[i].title);
        }
        if (xml_ctx->tracks[i].performer) {
            sa_free(xml_ctx->tracks[i].performer);
        }
        if (xml_ctx->tracks[i].composer) {
            sa_free(xml_ctx->tracks[i].composer);
        }
        if (xml_ctx->tracks[i].arranger) {
            sa_free(xml_ctx->tracks[i].arranger);
        }
        if (xml_ctx->tracks[i].songwriter) {
            sa_free(xml_ctx->tracks[i].songwriter);
        }
        if (xml_ctx->tracks[i].message) {
            sa_free(xml_ctx->tracks[i].message);
        }
    }
    xml_ctx->track_count = 0;

    xml_ctx->is_open = false;
}

static int xml_sink_track_start(void *ctx, uint8_t track_number,
                                 const dsdpipe_metadata_t *metadata)
{
    dsdpipe_sink_xml_ctx_t *xml_ctx = (dsdpipe_sink_xml_ctx_t *)ctx;

    if (!xml_ctx || !xml_ctx->is_open) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    if (xml_ctx->track_count >= XML_MAX_TRACKS) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Store track info */
    xml_track_info_t *track = &xml_ctx->tracks[xml_ctx->track_count];
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
        track->start_frame = metadata->start_frame;
        track->duration_frames = metadata->duration_frames;
        track->duration_seconds = metadata->duration_seconds;
    }

    xml_ctx->current_track_idx = xml_ctx->track_count;
    xml_ctx->track_count++;

    return DSDPIPE_OK;
}

static int xml_sink_track_end(void *ctx, uint8_t track_number)
{
    dsdpipe_sink_xml_ctx_t *xml_ctx = (dsdpipe_sink_xml_ctx_t *)ctx;

    if (!xml_ctx || !xml_ctx->is_open) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    (void)track_number;
    return DSDPIPE_OK;
}

static int xml_sink_write_frame(void *ctx, const dsdpipe_buffer_t *buffer)
{
    (void)ctx;
    (void)buffer;
    /* XML sink is metadata-only; timing comes from start_frame/duration_frames */
    return DSDPIPE_OK;
}

static int xml_sink_finalize(void *ctx)
{
    dsdpipe_sink_xml_ctx_t *xml_ctx = (dsdpipe_sink_xml_ctx_t *)ctx;
    XMLDoc doc;
    XMLNode *root, *album_node, *tracks_node, *format_node, *track_node;
    FILE *fd = NULL;
    int result = DSDPIPE_OK;
    char buf[64];

    if (!xml_ctx || !xml_ctx->is_open || !xml_ctx->path) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Initialize XML document */
    XMLDoc_init(&doc);

    /* Add XML prolog */
    XMLNode *prolog = XMLNode_new(TAG_INSTR, "xml version=\"1.0\" encoding=\"UTF-8\"", NULL);
    if (!prolog) {
        result = DSDPIPE_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }
    XMLDoc_add_node(&doc, prolog);

    /* Create root element */
    root = XMLNode_new(TAG_FATHER, "sacd_metadata", NULL);
    if (!root) {
        result = DSDPIPE_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }
    XMLNode_set_attribute(root, "version", "1.0");
    XMLDoc_add_node(&doc, root);

    /* Create album section */
    album_node = XMLNode_new(TAG_FATHER, "album", NULL);
    if (!album_node) {
        result = DSDPIPE_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }
    XMLNode_add_child(root, album_node);

    /* Album fields */
    if (xml_ctx->album_title && xml_ctx->album_title[0]) {
        xml_add_text_element(album_node, "title", xml_ctx->album_title);
    }
    if (xml_ctx->album_artist && xml_ctx->album_artist[0]) {
        xml_add_text_element(album_node, "artist", xml_ctx->album_artist);
    }
    if (xml_ctx->album_publisher && xml_ctx->album_publisher[0]) {
        xml_add_text_element(album_node, "publisher", xml_ctx->album_publisher);
    }
    if (xml_ctx->album_copyright && xml_ctx->album_copyright[0]) {
        xml_add_text_element(album_node, "copyright", xml_ctx->album_copyright);
    }
    if (xml_ctx->catalog_number && xml_ctx->catalog_number[0]) {
        char catalog_buf[XML_MAX_CATALOG];
        sa_strlcpy(catalog_buf, xml_ctx->catalog_number, sizeof(catalog_buf));

        /* Trim trailing whitespace */
        size_t cat_len = strlen(catalog_buf);
        while (cat_len > 0 && catalog_buf[cat_len - 1] == ' ') {
            catalog_buf[--cat_len] = '\0';
        }

        if (catalog_buf[0] != '\0') {
            xml_add_text_element(album_node, "catalog_number", catalog_buf);
        }
    }
    if (xml_ctx->genre && xml_ctx->genre[0]) {
        xml_add_text_element(album_node, "genre", xml_ctx->genre);
    }

    /* Date */
    if (xml_ctx->year > 0) {
        if (xml_ctx->month > 0 && xml_ctx->day > 0) {
#ifdef _MSC_VER
            sprintf_s(buf, sizeof(buf), "%04d-%02d-%02d",
                      xml_ctx->year, xml_ctx->month, xml_ctx->day);
#else
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                     xml_ctx->year, xml_ctx->month, xml_ctx->day);
#endif
        } else {
#ifdef _MSC_VER
            sprintf_s(buf, sizeof(buf), "%04d", xml_ctx->year);
#else
            snprintf(buf, sizeof(buf), "%04d", xml_ctx->year);
#endif
        }
        xml_add_text_element(album_node, "date", buf);
    }

    /* Disc info */
    if (xml_ctx->disc_number > 0) {
        xml_add_int_element(album_node, "disc_number", xml_ctx->disc_number);
    }
    if (xml_ctx->disc_total > 0) {
        xml_add_int_element(album_node, "disc_total", xml_ctx->disc_total);
    }
    if (xml_ctx->track_total > 0) {
        xml_add_int_element(album_node, "track_total", xml_ctx->track_total);
    }

    /* Create tracks section */
    tracks_node = XMLNode_new(TAG_FATHER, "tracks", NULL);
    if (!tracks_node) {
        result = DSDPIPE_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }
#ifdef _MSC_VER
    sprintf_s(buf, sizeof(buf), "%d", xml_ctx->track_count);
#else
    snprintf(buf, sizeof(buf), "%d", xml_ctx->track_count);
#endif
    XMLNode_set_attribute(tracks_node, "count", buf);
    XMLNode_add_child(root, tracks_node);

    /* Add each track */
    for (int i = 0; i < xml_ctx->track_count; i++) {
        xml_track_info_t *track = &xml_ctx->tracks[i];

        track_node = XMLNode_new(TAG_FATHER, "track", NULL);
        if (!track_node) {
            result = DSDPIPE_ERROR_OUT_OF_MEMORY;
            goto cleanup;
        }

        /* Track number attribute */
#ifdef _MSC_VER
        sprintf_s(buf, sizeof(buf), "%d", track->track_number);
#else
        snprintf(buf, sizeof(buf), "%d", track->track_number);
#endif
        XMLNode_set_attribute(track_node, "number", buf);
        XMLNode_add_child(tracks_node, track_node);

        /* Track fields */
        if (track->title && track->title[0]) {
            xml_add_text_element(track_node, "title", track->title);
        }
        if (track->performer && track->performer[0]) {
            xml_add_text_element(track_node, "performer", track->performer);
        }
        if (track->composer && track->composer[0]) {
            xml_add_text_element(track_node, "composer", track->composer);
        }
        if (track->arranger && track->arranger[0]) {
            xml_add_text_element(track_node, "arranger", track->arranger);
        }
        if (track->songwriter && track->songwriter[0]) {
            xml_add_text_element(track_node, "songwriter", track->songwriter);
        }
        if (track->message && track->message[0]) {
            xml_add_text_element(track_node, "message", track->message);
        }
        if (track->isrc[0]) {
            xml_add_text_element(track_node, "isrc", track->isrc);
        }

        /* Timing info - frame-based from SACD TOC */
        XMLNode *timing_node = XMLNode_new(TAG_SELF, "timing", NULL);
        if (timing_node) {
            /* Start time in MM:SS:FF format (75 frames per second) */
#ifdef _MSC_VER
            sprintf_s(buf, sizeof(buf), "%02d:%02d:%02d",
                      (int)((track->start_frame / XML_FRAMES_PER_SEC) / 60),
                      (int)((track->start_frame / XML_FRAMES_PER_SEC) % 60),
                      (int)(track->start_frame % XML_FRAMES_PER_SEC));
#else
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                     (int)((track->start_frame / XML_FRAMES_PER_SEC) / 60),
                     (int)((track->start_frame / XML_FRAMES_PER_SEC) % 60),
                     (int)(track->start_frame % XML_FRAMES_PER_SEC));
#endif
            XMLNode_set_attribute(timing_node, "start_time", buf);

            /* Duration in MM:SS:FF format */
            if (track->duration_frames > 0) {
#ifdef _MSC_VER
                sprintf_s(buf, sizeof(buf), "%02d:%02d:%02d",
                          (int)((track->duration_frames / XML_FRAMES_PER_SEC) / 60),
                          (int)((track->duration_frames / XML_FRAMES_PER_SEC) % 60),
                          (int)(track->duration_frames % XML_FRAMES_PER_SEC));
#else
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                         (int)((track->duration_frames / XML_FRAMES_PER_SEC) / 60),
                         (int)((track->duration_frames / XML_FRAMES_PER_SEC) % 60),
                         (int)(track->duration_frames % XML_FRAMES_PER_SEC));
#endif
                XMLNode_set_attribute(timing_node, "duration_time", buf);
            }

            /* Raw frame values for programmatic use */
#ifdef _MSC_VER
            sprintf_s(buf, sizeof(buf), "%u", track->start_frame);
#else
            snprintf(buf, sizeof(buf), "%u", track->start_frame);
#endif
            XMLNode_set_attribute(timing_node, "start_frame", buf);

            if (track->duration_frames > 0) {
#ifdef _MSC_VER
                sprintf_s(buf, sizeof(buf), "%u", track->duration_frames);
#else
                snprintf(buf, sizeof(buf), "%u", track->duration_frames);
#endif
                XMLNode_set_attribute(timing_node, "duration_frames", buf);
            }

            if (track->duration_seconds > 0) {
#ifdef _MSC_VER
                sprintf_s(buf, sizeof(buf), "%.3f", track->duration_seconds);
#else
                snprintf(buf, sizeof(buf), "%.3f", track->duration_seconds);
#endif
                XMLNode_set_attribute(timing_node, "duration_seconds", buf);
            }

            XMLNode_add_child(track_node, timing_node);
        }
    }

    /* Create audio_format section */
    format_node = XMLNode_new(TAG_FATHER, "audio_format", NULL);
    if (!format_node) {
        result = DSDPIPE_ERROR_OUT_OF_MEMORY;
        goto cleanup;
    }
    XMLNode_add_child(root, format_node);

    xml_add_text_element(format_node, "type", xml_format_type_string(xml_ctx->format.type));
    xml_add_int_element(format_node, "sample_rate", xml_ctx->sample_rate);
    xml_add_int_element(format_node, "channels", xml_ctx->format.channel_count);
    xml_add_int_element(format_node, "bits_per_sample", xml_ctx->format.bits_per_sample);

    /* Calculate total play time from track frames */
    {
        uint32_t total_frames = 0;
        for (int i = 0; i < xml_ctx->track_count; i++) {
            total_frames += xml_ctx->tracks[i].duration_frames;
        }
        if (total_frames > 0) {
#ifdef _MSC_VER
            sprintf_s(buf, sizeof(buf), "%02d:%02d:%02d",
                      (int)((total_frames / XML_FRAMES_PER_SEC) / 60),
                      (int)((total_frames / XML_FRAMES_PER_SEC) % 60),
                      (int)(total_frames % XML_FRAMES_PER_SEC));
#else
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                     (int)((total_frames / XML_FRAMES_PER_SEC) / 60),
                     (int)((total_frames / XML_FRAMES_PER_SEC) % 60),
                     (int)(total_frames % XML_FRAMES_PER_SEC));
#endif
            xml_add_text_element(format_node, "total_play_time", buf);
            xml_add_double_element(format_node, "total_duration_seconds",
                                   (double)total_frames / XML_FRAMES_PER_SEC);
        }
    }

    /* Open output file */
    fd = sa_fopen(xml_ctx->path, "wb");
    if (!fd) {
        result = DSDPIPE_ERROR_FILE_CREATE;
        goto cleanup;
    }

    /* Write UTF-8 BOM */
    fputc(0xef, fd);
    fputc(0xbb, fd);
    fputc(0xbf, fd);

    /* Write XML document */
    XMLDoc_print(&doc, fd, "\n", "  ", 0, 0, 4);

cleanup:
    if (fd) {
        fclose(fd);
    }
    XMLDoc_free(&doc);

    return result;
}

static uint32_t xml_sink_get_capabilities(void *ctx)
{
    (void)ctx;
    /* Metadata-only sink */
    return DSDPIPE_SINK_CAP_METADATA | DSDPIPE_SINK_CAP_MARKERS;
}

static void xml_sink_destroy(void *ctx)
{
    dsdpipe_sink_xml_ctx_t *xml_ctx = (dsdpipe_sink_xml_ctx_t *)ctx;

    if (!xml_ctx) {
        return;
    }

    xml_sink_close(ctx);
    sa_free(xml_ctx);
}

/*============================================================================
 * Operations Table
 *============================================================================*/

static const dsdpipe_sink_ops_t s_xml_sink_ops = {
    .open = xml_sink_open,
    .close = xml_sink_close,
    .track_start = xml_sink_track_start,
    .track_end = xml_sink_track_end,
    .write_frame = xml_sink_write_frame,
    .finalize = xml_sink_finalize,
    .get_capabilities = xml_sink_get_capabilities,
    .destroy = xml_sink_destroy
};

/*============================================================================
 * Factory Function
 *============================================================================*/

int dsdpipe_sink_xml_create(dsdpipe_sink_t *sink)
{
    dsdpipe_sink_xml_ctx_t *ctx;

    if (!sink) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    ctx = (dsdpipe_sink_xml_ctx_t *)sa_calloc(1, sizeof(*ctx));
    if (!ctx) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    sink->type = DSDPIPE_SINK_XML;
    sink->ops = &s_xml_sink_ops;
    sink->ctx = ctx;
    sink->is_open = false;

    return DSDPIPE_OK;
}
