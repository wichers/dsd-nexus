/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief ID3v2 tag parser implementation using id3dev library
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


#include "id3_parser.h"
#include "dsdpipe_internal.h"

#include <libdsdpipe/dsdpipe.h>
#include <libdsdpipe/metadata_tags.h>
#include <libsautil/mem.h>
#include <libsautil/sastring.h>

/* id3dev library headers */
#include <id3v2/id3v2.h>
#include <id3v2/id3v2Frame.h>
#include <id3v2/id3v2Parser.h>
#include <id3v2/id3v2Types.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*============================================================================
 * Constants
 *============================================================================*/

/* ID3v2 header size is always 10 bytes */
#define ID3V2_HEADER_SIZE 10

/* ID3v2 magic bytes "ID3" */
#define ID3V2_MAGIC_0 'I'
#define ID3V2_MAGIC_1 'D'
#define ID3V2_MAGIC_2 '3'

/*============================================================================
 * Frame Name Mapping
 *============================================================================*/

typedef struct frame_name_map_s {
    const char *frame_id;
    const char *name;
} frame_name_map_t;

static const frame_name_map_t frame_names[] = {
    {"TIT2", "Title"},
    {"TPE1", "Artist"},
    {"TPE2", "Album Artist"},
    {"TALB", "Album"},
    {"TCOM", "Composer"},
    {"TEXT", "Lyricist"},
    {"TCON", "Genre"},
    {"TRCK", "Track"},
    {"TPOS", "Disc"},
    {"TDRC", "Recording Date"},
    {"TYER", "Year"},
    {"TSRC", "ISRC"},
    {"TPUB", "Publisher"},
    {"TCOP", "Copyright"},
    {"TXXX", "User Text"},
    {"COMM", "Comment"},
    {"APIC", "Picture"},
    {"TLEN", "Length"},
    {"TBPM", "BPM"},
    {"TKEY", "Key"},
    {"TLAN", "Language"},
    {"TCMP", "Compilation"},
    {"TSOP", "Performer Sort"},
    {"TSOA", "Album Sort"},
    {"TSOT", "Title Sort"},
    {"TSO2", "Album Artist Sort"},
    {"WOAR", "Artist URL"},
    {"WOAS", "Source URL"},
    {"WPUB", "Publisher URL"},
    {NULL, NULL}
};

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Decode syncsafe integer (ID3v2.4 size encoding)
 */
static uint32_t decode_syncsafe(const uint8_t *data)
{
    return ((uint32_t)(data[0] & 0x7F) << 21) |
           ((uint32_t)(data[1] & 0x7F) << 14) |
           ((uint32_t)(data[2] & 0x7F) << 7) |
           ((uint32_t)(data[3] & 0x7F));
}

/**
 * @brief Parse "N/M" format string into two numbers
 */
static void parse_track_disc(const char *str, int *number, int *total)
{
    *number = 0;
    *total = 0;

    if (!str || !str[0]) {
        return;
    }

    /* Parse first number */
    *number = atoi(str);

    /* Look for slash separator */
    const char *slash = strchr(str, '/');
    if (slash && slash[1]) {
        *total = atoi(slash + 1);
    }
}

/**
 * @brief Parse ISO 8601 date string (YYYY-MM-DD or YYYY)
 */
static void parse_date(const char *str, uint16_t *year, uint8_t *month, uint8_t *day)
{
    *year = 0;
    *month = 0;
    *day = 0;

    if (!str || strlen(str) < 4) {
        return;
    }

    /* Parse year (always first 4 chars) */
    *year = (uint16_t)atoi(str);

    /* Check for month */
    if (strlen(str) >= 7 && str[4] == '-') {
        *month = (uint8_t)atoi(str + 5);

        /* Check for day */
        if (strlen(str) >= 10 && str[7] == '-') {
            *day = (uint8_t)atoi(str + 8);
        }
    }
}

/*============================================================================
 * Validation Functions
 *============================================================================*/

int id3_is_valid(const uint8_t *data, size_t size)
{
    if (!data || size < ID3V2_HEADER_SIZE) {
        return 0;
    }

    /* Check magic bytes "ID3" */
    if (data[0] != ID3V2_MAGIC_0 ||
        data[1] != ID3V2_MAGIC_1 ||
        data[2] != ID3V2_MAGIC_2) {
        return 0;
    }

    /* Check version (must be 2, 3, or 4) */
    uint8_t major = data[3];
    if (major < 2 || major > 4) {
        return 0;
    }

    /* Check that size bytes don't have MSB set (syncsafe requirement) */
    if ((data[6] & 0x80) || (data[7] & 0x80) ||
        (data[8] & 0x80) || (data[9] & 0x80)) {
        return 0;
    }

    return 1;
}

int id3_get_version(const uint8_t *data, size_t size, id3_version_t *version)
{
    if (!data || !version) {
        return ID3_PARSE_ERROR_INVALID;
    }

    if (!id3_is_valid(data, size)) {
        return ID3_PARSE_ERROR_INVALID;
    }

    version->major = data[3];
    version->revision = data[4];
    version->flags = data[5];
    version->size = decode_syncsafe(data + 6);

    return ID3_PARSE_OK;
}

size_t id3_get_total_size(const uint8_t *data, size_t size)
{
    id3_version_t version;

    if (id3_get_version(data, size, &version) != ID3_PARSE_OK) {
        return 0;
    }

    /* Total size = header (10) + tag content size + optional footer (10) */
    size_t total = ID3V2_HEADER_SIZE + version.size;

    /* Check footer flag (bit 4 of flags byte) - ID3v2.4 only */
    if (version.major == 4 && (version.flags & 0x10)) {
        total += 10;
    }

    return total;
}

/*============================================================================
 * Parser Implementation using id3dev
 *============================================================================*/

int id3_parse_to_tags(const uint8_t *data, size_t size, metadata_tags_t *tags)
{
    Id3v2Tag *tag = NULL;
    ListIter traverser;
    Id3v2Frame *frame = NULL;
    char frame_id[ID3V2_FRAME_ID_MAX_SIZE + 1];
    char *value = NULL;
    int result = ID3_PARSE_OK;

    if (!data || !tags) {
        return ID3_PARSE_ERROR_INVALID;
    }

    if (!id3_is_valid(data, size)) {
        return ID3_PARSE_ERROR_INVALID;
    }

    /* Parse tag using id3dev */
    tag = id3v2ParseTagFromBuffer((uint8_t *)data, size, NULL);
    if (!tag) {
        return ID3_PARSE_ERROR_INVALID;
    }

    /* Iterate through all frames */
    traverser = id3v2CreateFrameTraverser(tag);

    while ((frame = id3v2FrameTraverse(&traverser)) != NULL) {
        if (!frame->header) {
            continue;
        }

        /* Get frame ID as null-terminated string */
        memset(frame_id, 0, sizeof(frame_id));
        memcpy(frame_id, frame->header->id, ID3V2_FRAME_ID_MAX_SIZE);

        /* Skip non-text frames for now (APIC, etc.) */
        if (frame_id[0] != 'T' && frame_id[0] != 'W') {
            /* Handle COMM (comment) frame specially */
            if (strcmp(frame_id, "COMM") == 0) {
                value = id3v2ReadComment(tag);
                if (value) {
                    metadata_tags_set(tags, "COMM", value);
                    free(value);
                    value = NULL;
                }
            }
            continue;
        }

        /* Handle TXXX (user-defined text) frames specially */
        if (strcmp(frame_id, "TXXX") == 0) {
            /* TXXX has: encoding, description, value */
            ListIter entryIter = id3v2CreateFrameEntryTraverser(frame);
            size_t dataSize = 0;

            /* Skip encoding byte */
            (void)id3v2ReadFrameEntry(&entryIter, &dataSize);

            /* Get description */
            char *description = id3v2ReadFrameEntryAsChar(&entryIter, &dataSize);

            /* Get value */
            char *txxx_value = id3v2ReadFrameEntryAsChar(&entryIter, &dataSize);

            if (description && txxx_value) {
                /* Store as "TXXX:{description}" */
                char key[256];
                snprintf(key, sizeof(key), "TXXX:%s", description);
                metadata_tags_set(tags, key, txxx_value);
            }

            if (description) free(description);
            if (txxx_value) free(txxx_value);
            continue;
        }

        /* Read text frame content using id3dev helper */
        value = id3v2ReadTextFrameContent(frame_id, tag);
        if (value && value[0]) {
            metadata_tags_set(tags, frame_id, value);
            free(value);
            value = NULL;
        }
    }

    /* Clean up */
    id3v2DestroyTag(&tag);

    return result;
}

int id3_parse_to_metadata(const uint8_t *data, size_t size,
                           struct dsdpipe_metadata_s *metadata)
{
    Id3v2Tag *tag = NULL;
    char *value = NULL;
    int result = ID3_PARSE_OK;

    if (!data || !metadata) {
        return ID3_PARSE_ERROR_INVALID;
    }

    if (!id3_is_valid(data, size)) {
        return ID3_PARSE_ERROR_INVALID;
    }

    /* Parse tag using id3dev */
    tag = id3v2ParseTagFromBuffer((uint8_t *)data, size, NULL);
    if (!tag) {
        return ID3_PARSE_ERROR_INVALID;
    }

    /* Create tags container if needed */
    if (!metadata->tags) {
        metadata->tags = metadata_tags_create();
        if (!metadata->tags) {
            id3v2DestroyTag(&tag);
            return ID3_PARSE_ERROR_MEMORY;
        }
    }

    /* TIT2: Track title */
    value = id3v2ReadTitle(tag);
    if (value) {
        dsdpipe_metadata_set_string(&metadata->track_title, value);
        metadata_tags_set(metadata->tags, "TIT2", value);
        free(value);
    }

    /* TPE1: Track artist/performer */
    value = id3v2ReadArtist(tag);
    if (value) {
        dsdpipe_metadata_set_string(&metadata->track_performer, value);
        metadata_tags_set(metadata->tags, "TPE1", value);
        free(value);
    }

    /* TPE2: Album artist */
    value = id3v2ReadAlbumArtist(tag);
    if (value) {
        dsdpipe_metadata_set_string(&metadata->album_artist, value);
        metadata_tags_set(metadata->tags, "TPE2", value);
        free(value);
    }

    /* TALB: Album */
    value = id3v2ReadAlbum(tag);
    if (value) {
        dsdpipe_metadata_set_string(&metadata->album_title, value);
        metadata_tags_set(metadata->tags, "TALB", value);
        free(value);
    }

    /* TCOM: Composer */
    value = id3v2ReadComposer(tag);
    if (value) {
        dsdpipe_metadata_set_string(&metadata->track_composer, value);
        metadata_tags_set(metadata->tags, "TCOM", value);
        free(value);
    }

    /* TCON: Genre */
    value = id3v2ReadGenre(tag);
    if (value) {
        dsdpipe_metadata_set_string(&metadata->genre, value);
        metadata_tags_set(metadata->tags, "TCON", value);
        free(value);
    }

    /* TRCK: Track number/total */
    value = id3v2ReadTrack(tag);
    if (value) {
        int track_num, track_total;
        parse_track_disc(value, &track_num, &track_total);
        metadata->track_number = (uint8_t)track_num;
        metadata->track_total = (uint8_t)track_total;
        metadata_tags_set(metadata->tags, "TRCK", value);
        free(value);
    }

    /* TPOS: Disc number/total */
    value = id3v2ReadDisc(tag);
    if (value) {
        int disc_num, disc_total;
        parse_track_disc(value, &disc_num, &disc_total);
        metadata->disc_number = (uint8_t)disc_num;
        metadata->disc_total = (uint8_t)disc_total;
        metadata_tags_set(metadata->tags, "TPOS", value);
        free(value);
    }

    /* TDRC or TYER: Year/Date */
    value = id3v2ReadYear(tag);
    if (value) {
        parse_date(value, &metadata->year, &metadata->month, &metadata->day);
        metadata_tags_set(metadata->tags, "TDRC", value);
        free(value);
    }

    /* TSRC: ISRC */
    value = id3v2ReadTextFrameContent("TSRC", tag);
    if (value) {
        size_t len = strlen(value);
        if (len < sizeof(metadata->isrc)) {
            sa_strlcpy(metadata->isrc, value, sizeof(metadata->isrc));
        }
        metadata_tags_set(metadata->tags, "TSRC", value);
        free(value);
    }

    /* TPUB: Publisher */
    value = id3v2ReadTextFrameContent("TPUB", tag);
    if (value) {
        dsdpipe_metadata_set_string(&metadata->album_publisher, value);
        metadata_tags_set(metadata->tags, "TPUB", value);
        free(value);
    }

    /* TCOP: Copyright */
    value = id3v2ReadTextFrameContent("TCOP", tag);
    if (value) {
        dsdpipe_metadata_set_string(&metadata->album_copyright, value);
        metadata_tags_set(metadata->tags, "TCOP", value);
        free(value);
    }

    /* TEXT: Lyricist/Songwriter */
    value = id3v2ReadTextFrameContent("TEXT", tag);
    if (value) {
        dsdpipe_metadata_set_string(&metadata->track_songwriter, value);
        metadata_tags_set(metadata->tags, "TEXT", value);
        free(value);
    }

    /* COMM: Comment -> track_message */
    value = id3v2ReadComment(tag);
    if (value) {
        dsdpipe_metadata_set_string(&metadata->track_message, value);
        metadata_tags_set(metadata->tags, "COMM", value);
        free(value);
    }

    /* Now iterate all frames to capture any we missed */
    ListIter traverser = id3v2CreateFrameTraverser(tag);
    Id3v2Frame *frame;
    char frame_id[ID3V2_FRAME_ID_MAX_SIZE + 1];

    while ((frame = id3v2FrameTraverse(&traverser)) != NULL) {
        if (!frame->header) {
            continue;
        }

        memset(frame_id, 0, sizeof(frame_id));
        memcpy(frame_id, frame->header->id, ID3V2_FRAME_ID_MAX_SIZE);

        /* Skip if already in tags */
        if (metadata_tags_has(metadata->tags, frame_id)) {
            continue;
        }

        /* Handle text frames we haven't processed */
        if (frame_id[0] == 'T' && strcmp(frame_id, "TXXX") != 0) {
            value = id3v2ReadTextFrameContent(frame_id, tag);
            if (value && value[0]) {
                metadata_tags_set(metadata->tags, frame_id, value);
                free(value);
            }
        }
        /* Handle TXXX frames */
        else if (strcmp(frame_id, "TXXX") == 0) {
            ListIter entryIter = id3v2CreateFrameEntryTraverser(frame);
            size_t dataSize = 0;

            /* Skip encoding byte */
            (void)id3v2ReadFrameEntry(&entryIter, &dataSize);

            /* Get description */
            char *description = id3v2ReadFrameEntryAsChar(&entryIter, &dataSize);

            /* Get value */
            char *txxx_value = id3v2ReadFrameEntryAsChar(&entryIter, &dataSize);

            if (description && txxx_value) {
                char key[256];
                snprintf(key, sizeof(key), "TXXX:%s", description);

                /* Map specific TXXX to metadata fields */
                if (strcmp(description, "Arranger") == 0) {
                    dsdpipe_metadata_set_string(&metadata->track_arranger, txxx_value);
                }

                metadata_tags_set(metadata->tags, key, txxx_value);
            }

            if (description) free(description);
            if (txxx_value) free(txxx_value);
        }
    }

    /* Clean up */
    id3v2DestroyTag(&tag);

    return result;
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

const char *id3_frame_name(const char *frame_id)
{
    if (!frame_id) {
        return "Unknown";
    }

    for (const frame_name_map_t *map = frame_names; map->frame_id; map++) {
        if (strncmp(frame_id, map->frame_id, 4) == 0) {
            return map->name;
        }
    }

    return frame_id;
}
