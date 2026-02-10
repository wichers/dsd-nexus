/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
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

#include "sacd_id3.h"

#include <libsacd/sacd.h>

#include <libsautil/mem.h>
#include <libsautil/macros.h>

#include <id3v2/id3v2.h>
#include <id3v2/id3v2Frame.h>
#include <id3v2/id3v2Context.h>
#include <id3v2/id3v2TagIdentity.h>
#include <id3v2/id3v2Types.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

/**
 * Helper function to add a custom TXXX frame (user-defined text)
 */
static int add_txxx_frame(Id3v2Tag *tag, const char *description, const char *str)
{
    if (!str || !tag || !description)
        return -1;

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
    if (entry) listInsertBack(entries, entry);

    entry = id3v2CreateContentEntry((void *)description, strlen(description) + 1);
    if (entry) listInsertBack(entries, entry);

    entry = id3v2CreateContentEntry((void *)str, strlen(str) + 1);
    if (entry) listInsertBack(entries, entry);

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

int sacd_id3_tag_render(sacd_t *ctx, uint8_t *buffer, uint8_t track_num)
{
    Id3v2Tag *tag = NULL;
    Id3v2TagHeader *header = NULL;
    List *frames = NULL;
    char tmp[200];
    int len = 0;
    int result;
    const char *str;
    uint8_t text_channel = 1;
    uint8_t *serialized_tag = NULL;
    size_t tag_size = 0;

    if (!ctx)
        return 0;

    /* Create ID3v2.4 tag structure */
    header = id3v2CreateTagHeader(ID3V2_TAG_VERSION_4, 0, 0, NULL);
    if (!header)
        return 0;

    frames = listCreate(id3v2PrintFrame, id3v2DeleteFrame,
                        id3v2CompareFrame, id3v2CopyFrame);
    if (!frames) {
        id3v2DestroyTagHeader(&header);
        return 0;
    }

    tag = id3v2CreateTag(header, frames);
    if (!tag)
        return 0;

    memset(tmp, 0, sizeof(tmp));

    /* TIT2: Track title */
    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_TITLE, &str);
    if (result == SACD_OK && str)
    {
        id3v2InsertTextFrame("TIT2", ID3V2_ENCODING_UTF8, str, tag);
    }
    else
    {
        /* Fallback: use album/disc title as track title */
        const char *album_title = NULL;

        /* Try album title first */
        result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE, &str);
        if (result == SACD_OK && str)
            album_title = str;
        else
        {
            /* Try album title phonetic */
            result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE_PHONETIC, &str);
            if (result == SACD_OK && str)
                album_title = str;
            else
            {
                /* Try disc title */
                result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE, &str);
                if (result == SACD_OK && str)
                    album_title = str;
                else
                {
                    /* Try disc title phonetic */
                    result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE_PHONETIC, &str);
                    if (result == SACD_OK && str)
                        album_title = str;
                }
            }
        }

        if (album_title)
            id3v2InsertTextFrame("TIT2", ID3V2_ENCODING_UTF8, album_title, tag);
    }

    /* TALB: Album title */
    {
        const char *album_title = NULL;

        result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE, &str);
        if (result == SACD_OK && str)
            album_title = str;
        else
        {
            result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE_PHONETIC, &str);
            if (result == SACD_OK && str)
                album_title = str;
            else
            {
                result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE, &str);
                if (result == SACD_OK && str)
                    album_title = str;
                else
                {
                    result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE_PHONETIC, &str);
                    if (result == SACD_OK && str)
                        album_title = str;
                }
            }
        }

        if (album_title)
            id3v2InsertTextFrame("TALB", ID3V2_ENCODING_UTF8, album_title, tag);
    }

    /* TPE1: Track artist/performer */
    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_PERFORMER, &str);
    if (result == SACD_OK && str)
    {
        id3v2InsertTextFrame("TPE1", ID3V2_ENCODING_UTF8, str, tag);
    }
    else
    {
        const char *artist = NULL;

        /* Try disc artist first */
        result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_ARTIST, &str);
        if (result == SACD_OK && str)
            artist = str;
        else
        {
            /* Try disc artist phonetic */
            result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_ARTIST_PHONETIC, &str);
            if (result == SACD_OK && str)
                artist = str;
            else
            {
                /* Try album artist */
                result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_ARTIST, &str);
                if (result == SACD_OK && str)
                    artist = str;
                else
                {
                    /* Try album artist phonetic */
                    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_ARTIST_PHONETIC, &str);
                    if (result == SACD_OK && str)
                        artist = str;
                }
            }
        }

        if (artist)
            id3v2InsertTextFrame("TPE1", ID3V2_ENCODING_UTF8, artist, tag);
    }

    /* TPE2: Album artist (band/orchestra) */
    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_ARTIST, &str);
    if (result == SACD_OK && str)
    {
        id3v2InsertTextFrame("TPE2", ID3V2_ENCODING_UTF8, str, tag);
    }

    /* TXXX: Performer (custom frame) */
    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_PERFORMER, &str);
    if (result == SACD_OK && str)
    {
        add_txxx_frame(tag, "Performer", str);
    }

    /* TCOM: Composer */
    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_COMPOSER, &str);
    if (result == SACD_OK && str)
    {
        id3v2InsertTextFrame("TCOM", ID3V2_ENCODING_UTF8, str, tag);
    }

    /* TSRC: ISRC code */
    {
        area_isrc_t isrc = {0};

        result = sacd_get_track_isrc_num(ctx, track_num, &isrc);
        if (result == SACD_OK && isrc.country_code[0])
        {
            char isrc_str[16];

            snprintf(isrc_str, sizeof(isrc_str), "%.2s%.3s%.2s%.5s",
                     isrc.country_code, isrc.owner_code,
                     isrc.recording_year, isrc.designation_code);

            id3v2InsertTextFrame("TSRC", ID3V2_ENCODING_UTF8, isrc_str, tag);
        }
    }

    /* TPUB: Publisher */
    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_PUBLISHER, &str);
    if (result == SACD_OK && str)
    {
        id3v2InsertTextFrame("TPUB", ID3V2_ENCODING_UTF8, str, tag);
    }

    /* TCOP: Copyright */
    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_COPYRIGHT, &str);
    if (result == SACD_OK && str)
    {
        id3v2InsertTextFrame("TCOP", ID3V2_ENCODING_UTF8, str, tag);
    }

    /* TEXT: Lyricist/Songwriter */
    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_SONGWRITER, &str);
    if (result == SACD_OK && str)
    {
        id3v2InsertTextFrame("TEXT", ID3V2_ENCODING_UTF8, str, tag);
    }

    /* TXXX:ARRANGER: Arranger */
    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_ARRANGER, &str);
    if (result == SACD_OK && str)
    {
        add_txxx_frame(tag, "Arranger", str);
    }

    /* COMM: Comment/Message */
    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_MESSAGE, &str);
    if (result == SACD_OK && str)
    {
        add_txxx_frame(tag, "Comment", str);
    }

    /* TXXX:EXTRA_MESSAGE: Extra message */
    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_EXTRA_MESSAGE, &str);
    if (result == SACD_OK && str)
    {
        add_txxx_frame(tag, "Extra Message", str);
    }

    /* TPOS: Part of set (disc sequence/set size) */
    {
        uint16_t disc_sequence_num = 0;
        uint16_t num_disc = 0;

        result = sacd_get_disc_sequence_num(ctx, &disc_sequence_num);
        if (result == SACD_OK)
        {
            result = sacd_get_album_disc_count(ctx, &num_disc);
            if (result == SACD_OK)
            {
                snprintf(tmp, sizeof(tmp), "%d/%d", disc_sequence_num, num_disc);
                id3v2InsertTextFrame("TPOS", ID3V2_ENCODING_UTF8, tmp, tag);
            }
        }
    }

    /* TCON: Genre */
    {
        uint8_t genre_table_id = 0;
        uint16_t genre_index = 0;

        result = sacd_get_track_genre(ctx, track_num, &genre_table_id, &genre_index);
        if (result == SACD_OK && genre_table_id)
        {
            switch (genre_table_id) {
                case CATEGORY_GENERAL:
                    id3v2InsertTextFrame("TCON", ID3V2_ENCODING_UTF8, album_genre_general[SAMIN(genre_index, 29)], tag);
                    break;
                case CATEGORY_JAPANESE:
                    id3v2InsertTextFrame("TCON", ID3V2_ENCODING_UTF8, album_genre_japanese[SAMIN(genre_index, 24)], tag);
                    break;
            }
        }
    }

    /* TDRC: Recording time (ID3v2.4 ISO 8601 format) */
    {
        uint16_t year = 0;
        uint8_t month = 0, day = 0;

        result = sacd_get_disc_date(ctx, &year, &month, &day);
        if (result == SACD_OK)
        {
            snprintf(tmp, sizeof(tmp), "%04d-%02d-%02d", year, month, day);
            id3v2InsertTextFrame("TDRC", ID3V2_ENCODING_UTF8, tmp, tag);
        }
    }

    /* TRCK: Track number/total tracks */
    {
        uint8_t track_count = 0;
        result = sacd_get_track_count(ctx, &track_count);
        if (result == SACD_OK)
        {
            snprintf(tmp, sizeof(tmp), "%d/%d", track_num, track_count);
            id3v2InsertTextFrame("TRCK", ID3V2_ENCODING_UTF8, tmp, tag);
        }
    }

    /* Serialize tag to buffer */
    serialized_tag = id3v2TagSerialize(tag, &tag_size);
    if (serialized_tag && buffer)
    {
        memcpy(buffer, serialized_tag, tag_size);
        len = (int)tag_size;
        free(serialized_tag);
    }

    /* Clean up */
    id3v2DestroyTag(&tag);

    return len;
}
