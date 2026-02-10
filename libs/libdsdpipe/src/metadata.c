/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Metadata handling utilities
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

#include <string.h>

/* Internal maximum lengths for filename components */
#define _MAX_ARTIST_COMPONENT_LEN  60
#define _MAX_TITLE_COMPONENT_LEN   120

/*============================================================================
 * String Utilities
 *============================================================================*/

char *dsdpipe_strdup(const char *src)
{
    if (!src) {
        return NULL;
    }

    size_t len = strlen(src);
    char *dup = (char *)sa_malloc(len + 1);
    if (!dup) {
        return NULL;
    }

    sa_strlcpy(dup, src, len + 1);

    return dup;
}

int dsdpipe_metadata_set_string(char **field, const char *value)
{
    if (!field) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Free existing value */
    if (*field) {
        sa_free(*field);
        *field = NULL;
    }

    /* Set new value */
    if (value) {
        *field = dsdpipe_strdup(value);
        if (!*field) {
            return DSDPIPE_ERROR_OUT_OF_MEMORY;
        }
    }

    return DSDPIPE_OK;
}

/*============================================================================
 * Metadata Lifecycle
 *============================================================================*/

void dsdpipe_metadata_init(dsdpipe_metadata_t *metadata)
{
    if (!metadata) {
        return;
    }

    memset(metadata, 0, sizeof(*metadata));
}

void dsdpipe_metadata_free(dsdpipe_metadata_t *metadata)
{
    if (!metadata) {
        return;
    }

    /* Album fields */
    if (metadata->album_title) {
        sa_free(metadata->album_title);
        metadata->album_title = NULL;
    }
    if (metadata->album_artist) {
        sa_free(metadata->album_artist);
        metadata->album_artist = NULL;
    }
    if (metadata->album_publisher) {
        sa_free(metadata->album_publisher);
        metadata->album_publisher = NULL;
    }
    if (metadata->album_copyright) {
        sa_free(metadata->album_copyright);
        metadata->album_copyright = NULL;
    }
    if (metadata->catalog_number) {
        sa_free(metadata->catalog_number);
        metadata->catalog_number = NULL;
    }
    if (metadata->genre) {
        sa_free(metadata->genre);
        metadata->genre = NULL;
    }

    /* Track fields */
    if (metadata->track_title) {
        sa_free(metadata->track_title);
        metadata->track_title = NULL;
    }
    if (metadata->track_performer) {
        sa_free(metadata->track_performer);
        metadata->track_performer = NULL;
    }
    if (metadata->track_composer) {
        sa_free(metadata->track_composer);
        metadata->track_composer = NULL;
    }
    if (metadata->track_arranger) {
        sa_free(metadata->track_arranger);
        metadata->track_arranger = NULL;
    }
    if (metadata->track_songwriter) {
        sa_free(metadata->track_songwriter);
        metadata->track_songwriter = NULL;
    }
    if (metadata->track_message) {
        sa_free(metadata->track_message);
        metadata->track_message = NULL;
    }

    /* Free tags */
    if (metadata->tags) {
        metadata_tags_destroy(metadata->tags);
        metadata->tags = NULL;
    }

    /* Clear non-pointer fields */
    metadata->year = 0;
    metadata->month = 0;
    metadata->day = 0;
    metadata->track_number = 0;
    metadata->track_total = 0;
    metadata->disc_number = 0;
    metadata->disc_total = 0;
    metadata->start_frame = 0;
    metadata->duration_frames = 0;
    metadata->duration_seconds = 0.0;
    memset(metadata->isrc, 0, sizeof(metadata->isrc));
}

int dsdpipe_metadata_copy(dsdpipe_metadata_t *dest,
                           const dsdpipe_metadata_t *src)
{
    if (!dest || !src) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Free existing destination */
    dsdpipe_metadata_free(dest);

    /* Copy non-pointer fields */
    dest->year = src->year;
    dest->month = src->month;
    dest->day = src->day;
    dest->track_number = src->track_number;
    dest->track_total = src->track_total;
    dest->disc_number = src->disc_number;
    dest->disc_total = src->disc_total;
    dest->start_frame = src->start_frame;
    dest->duration_frames = src->duration_frames;
    dest->duration_seconds = src->duration_seconds;

    /* Copy ISRC */
    sa_strlcpy(dest->isrc, src->isrc, sizeof(dest->isrc));

    /* Copy string fields */
    int result;

    result = dsdpipe_metadata_set_string(&dest->album_title, src->album_title);
    if (result != DSDPIPE_OK) goto error;

    result = dsdpipe_metadata_set_string(&dest->album_artist, src->album_artist);
    if (result != DSDPIPE_OK) goto error;

    result = dsdpipe_metadata_set_string(&dest->album_publisher, src->album_publisher);
    if (result != DSDPIPE_OK) goto error;

    result = dsdpipe_metadata_set_string(&dest->album_copyright, src->album_copyright);
    if (result != DSDPIPE_OK) goto error;

    result = dsdpipe_metadata_set_string(&dest->catalog_number, src->catalog_number);
    if (result != DSDPIPE_OK) goto error;

    result = dsdpipe_metadata_set_string(&dest->genre, src->genre);
    if (result != DSDPIPE_OK) goto error;

    result = dsdpipe_metadata_set_string(&dest->track_title, src->track_title);
    if (result != DSDPIPE_OK) goto error;

    result = dsdpipe_metadata_set_string(&dest->track_performer, src->track_performer);
    if (result != DSDPIPE_OK) goto error;

    result = dsdpipe_metadata_set_string(&dest->track_composer, src->track_composer);
    if (result != DSDPIPE_OK) goto error;

    result = dsdpipe_metadata_set_string(&dest->track_arranger, src->track_arranger);
    if (result != DSDPIPE_OK) goto error;

    result = dsdpipe_metadata_set_string(&dest->track_songwriter, src->track_songwriter);
    if (result != DSDPIPE_OK) goto error;

    result = dsdpipe_metadata_set_string(&dest->track_message, src->track_message);
    if (result != DSDPIPE_OK) goto error;

    /* Copy tags */
    if (src->tags) {
        dest->tags = metadata_tags_copy(src->tags);
        if (!dest->tags) {
            result = DSDPIPE_ERROR_OUT_OF_MEMORY;
            goto error;
        }
    }

    return DSDPIPE_OK;

error:
    dsdpipe_metadata_free(dest);
    return result;
}

/*============================================================================
 * Tag API Functions
 *============================================================================*/

int dsdpipe_metadata_set_tag(dsdpipe_metadata_t *metadata,
                              const char *key,
                              const char *value)
{
    if (!metadata || !key || !key[0] || !value) {
        return DSDPIPE_ERROR_INVALID_ARG;
    }

    /* Create tags container if needed */
    if (!metadata->tags) {
        metadata->tags = metadata_tags_create();
        if (!metadata->tags) {
            return DSDPIPE_ERROR_OUT_OF_MEMORY;
        }
    }

    if (metadata_tags_set(metadata->tags, key, value) < 0) {
        return DSDPIPE_ERROR_OUT_OF_MEMORY;
    }

    return DSDPIPE_OK;
}

const char *dsdpipe_metadata_get_tag(const dsdpipe_metadata_t *metadata,
                                      const char *key)
{
    if (!metadata || !metadata->tags || !key || !key[0]) {
        return NULL;
    }

    return metadata_tags_get(metadata->tags, key);
}

void dsdpipe_metadata_enumerate_tags(const dsdpipe_metadata_t *metadata,
                                      void *ctx,
                                      dsdpipe_tag_callback_t callback)
{
    if (!metadata || !metadata->tags || !callback) {
        return;
    }

    metadata_tags_enumerate(metadata->tags, ctx, callback);
}

size_t dsdpipe_metadata_tag_count(const dsdpipe_metadata_t *metadata)
{
    if (!metadata || !metadata->tags) {
        return 0;
    }

    return metadata_tags_count(metadata->tags);
}

/*============================================================================
 * Track Filename Generation
 *============================================================================*/

const char *dsdpipe_get_best_artist(const dsdpipe_metadata_t *metadata)
{
    if (!metadata) {
        return NULL;
    }

    /* Try track performer first */
    if (metadata->track_performer != NULL && metadata->track_performer[0] != '\0') {
        return metadata->track_performer;
    }

    /* Fall back to album artist */
    if (metadata->album_artist != NULL && metadata->album_artist[0] != '\0') {
        return metadata->album_artist;
    }

    return NULL;
}

char *dsdpipe_get_track_filename(const dsdpipe_metadata_t *metadata,
                                  dsdpipe_track_format_t format)
{
    if (!metadata) {
        return NULL;
    }

    uint8_t track_num = metadata->track_number;

    /* Simple case: track number only */
    if (format == DSDPIPE_TRACK_NUM_ONLY) {
        return sa_asprintf("%02u", track_num);
    }

    /* Get track title */
    const char *raw_title = metadata->track_title;

    /* Get performer for this track (with fallback) */
    const char *raw_performer = NULL;
    if (format == DSDPIPE_TRACK_NUM_ARTIST_TITLE) {
        raw_performer = dsdpipe_get_best_artist(metadata);
    }

    /* Prepare sanitized components */
    char title[_MAX_TITLE_COMPONENT_LEN + 1] = {0};
    char performer[_MAX_ARTIST_COMPONENT_LEN + 1] = {0};

    if (raw_title != NULL && raw_title[0] != '\0') {
        sa_utf8_strlcpy(title, raw_title, sizeof(title));
        sa_sanitize_filename(title, sizeof(title));
    }

    if (raw_performer != NULL && raw_performer[0] != '\0') {
        sa_extract_first_token(performer, raw_performer, sizeof(performer), NULL);
        sa_sanitize_filename(performer, sizeof(performer));
    }

    /* Build the filename */
    char *result = NULL;

    if (format == DSDPIPE_TRACK_NUM_ARTIST_TITLE &&
        performer[0] != '\0' && title[0] != '\0') {
        result = sa_asprintf("%02u - %s - %s", track_num, performer, title);
    } else if (title[0] != '\0') {
        result = sa_asprintf("%02u - %s", track_num, title);
    } else if (performer[0] != '\0') {
        result = sa_asprintf("%02u - %s", track_num, performer);
    } else {
        result = sa_asprintf("%02u - Track %u", track_num, track_num);
    }

    return result;
}

/*============================================================================
 * Album Directory/Path Generation
 *============================================================================*/

char *dsdpipe_get_album_dir(const dsdpipe_metadata_t *metadata,
                             dsdpipe_album_format_t format)
{
    if (!metadata) {
        return NULL;
    }

    /* Prepare sanitized components */
    char artist[_MAX_ARTIST_COMPONENT_LEN + 1] = {0};
    char title[_MAX_TITLE_COMPONENT_LEN + 1] = {0};

    /* Get album artist */
    const char *raw_artist = metadata->album_artist;
    if (raw_artist != NULL && raw_artist[0] != '\0') {
        sa_extract_first_token(artist, raw_artist, sizeof(artist), NULL);
        sa_sanitize_filename(artist, sizeof(artist));
    }

    /* Get album title */
    const char *raw_title = metadata->album_title;
    if (raw_title != NULL && raw_title[0] != '\0') {
        sa_utf8_strlcpy(title, raw_title, sizeof(title));
        sa_sanitize_filename(title, sizeof(title));
    }

    /* Build the directory name */
    char *result = NULL;
    char *base_name = NULL;

    if (format == DSDPIPE_ALBUM_ARTIST_TITLE &&
        artist[0] != '\0' && title[0] != '\0') {
        base_name = sa_asprintf("%s - %s", artist, title);
    } else if (title[0] != '\0') {
        base_name = sa_asprintf("%s", title);
    } else if (artist[0] != '\0') {
        base_name = sa_asprintf("%s", artist);
    } else {
        base_name = sa_asprintf("Unknown Album");
    }

    if (!base_name) {
        return NULL;
    }

    /* Append disc info for multi-disc sets */
    if (metadata->disc_total > 1 && metadata->disc_number > 0) {
        result = sa_asprintf("%s (disc %u-%u)", base_name,
                             metadata->disc_number, metadata->disc_total);
        sa_free(base_name);
    } else {
        result = base_name;
    }

    return result;
}

char *dsdpipe_get_album_path(const dsdpipe_metadata_t *metadata,
                              dsdpipe_album_format_t format)
{
    if (!metadata) {
        return NULL;
    }

    /* Prepare sanitized components */
    char artist[_MAX_ARTIST_COMPONENT_LEN + 1] = {0};
    char title[_MAX_TITLE_COMPONENT_LEN + 1] = {0};

    /* Get album artist */
    const char *raw_artist = metadata->album_artist;
    if (raw_artist != NULL && raw_artist[0] != '\0') {
        sa_extract_first_token(artist, raw_artist, sizeof(artist), NULL);
        sa_sanitize_filename(artist, sizeof(artist));
    }

    /* Get album title */
    const char *raw_title = metadata->album_title;
    if (raw_title != NULL && raw_title[0] != '\0') {
        sa_utf8_strlcpy(title, raw_title, sizeof(title));
        sa_sanitize_filename(title, sizeof(title));
    }

    /* Build the base directory name */
    char *base_name = NULL;

    if (format == DSDPIPE_ALBUM_ARTIST_TITLE &&
        artist[0] != '\0' && title[0] != '\0') {
        base_name = sa_asprintf("%s - %s", artist, title);
    } else if (title[0] != '\0') {
        base_name = sa_asprintf("%s", title);
    } else if (artist[0] != '\0') {
        base_name = sa_asprintf("%s", artist);
    } else {
        base_name = sa_asprintf("Unknown Album");
    }

    if (!base_name) {
        return NULL;
    }

    /* Add disc subdirectory for multi-disc sets */
    char *result = NULL;
    if (metadata->disc_total > 1 && metadata->disc_number > 0) {
#if defined(_WIN32) || defined(WIN32)
        result = sa_asprintf("%s\\Disc %u", base_name, metadata->disc_number);
#else
        result = sa_asprintf("%s/Disc %u", base_name, metadata->disc_number);
#endif
        sa_free(base_name);
    } else {
        result = base_name;
    }

    return result;
}

/*============================================================================
 * Format String Functions
 *============================================================================*/

const char *dsdpipe_get_speaker_config_string(const dsdpipe_format_t *format)
{
    if (!format) {
        return "Unknown";
    }

    switch (format->channel_count) {
    case 1:
        return "Mono";
    case 2:
        return "Stereo";
    case 3:
        return "3ch";
    case 4:
        return "4ch";
    case 5:
        return "5ch";
    case 6:
        return "5.1ch";
    default:
        return "Unknown";
    }
}

const char *dsdpipe_get_frame_format_string(const dsdpipe_format_t *format)
{
    if (!format) {
        return "Unknown";
    }

    switch (format->type) {
    case DSDPIPE_FORMAT_DSD_RAW:
        return "DSD";
    case DSDPIPE_FORMAT_DST:
        return "Lossless DST";
    case DSDPIPE_FORMAT_PCM_INT16:
        return "PCM 16-bit";
    case DSDPIPE_FORMAT_PCM_INT24:
        return "PCM 24-bit";
    case DSDPIPE_FORMAT_PCM_INT32:
        return "PCM 32-bit";
    case DSDPIPE_FORMAT_PCM_FLOAT32:
        return "PCM Float32";
    case DSDPIPE_FORMAT_PCM_FLOAT64:
        return "PCM Float64";
    default:
        return "Unknown";
    }
}
