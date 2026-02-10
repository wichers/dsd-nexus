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

#include <libsacd/sacd.h>

#include "assert.h"
#include "sacd_specification.h"

#include <libsautil/compat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

static void sacd_print_album_metadata(sacd_t *ctx, uint8_t text_channel)
{
    const char *str;
    int result;

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tTitle: %s\n", str);

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tTitle Phonetic: %s\n", str);

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_ARTIST, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tArtist: %s\n", str);

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_ARTIST_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tArtist Phonetic: %s\n", str);

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_PUBLISHER, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tPublisher: %s\n", str);

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_PUBLISHER_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tPublisher Phonetic: %s\n", str);

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_COPYRIGHT, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tCopyright: %s\n", str);

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_COPYRIGHT_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tCopyright Phonetic: %s\n", str);
}

static const char *character_sets[] =
{
    "ISO 646 (US-ASCII equivalent)", 
    "ISO 646 (US-ASCII)",
    "ISO 8859-1 (Latin-1, Western European)",
    "Shift-JIS (Japanese)",
    "KSC 5601 (Korean)",
    "GB 2312 (Simplified Chinese)",
    "Big5 (Traditional Chinese)",
    "ISO 8859-1 (fallback)",
};

static void sacd_print_disc_metadata(sacd_t *ctx, uint8_t text_channel)
{
    char lang[2] = {0};
    const char *language_code = lang;
    uint8_t character_set_code;
    const char *str;
    int result;

    result = sacd_get_master_text_channel_info(ctx, text_channel, &language_code, &character_set_code);
    if (result == SACD_OK)
    {
        const char *current_charset_name = character_sets[character_set_code & 0x07];
        if (current_charset_name[0] != '\0' && current_charset_name[1] != '\0')
            sa_fprintf(stdout, "\tLocale: %.2s, Code character set:[%d], %s\n", language_code, character_set_code, current_charset_name);
        else
            sa_fprintf(stdout, "\tLocale: (zero) unspecified, asume Code character set:[%d], %s\n", character_set_code, current_charset_name);
    }

    result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tTitle: %s\n", str);

    result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tTitle Phonetic: %s\n", str);

    result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_ARTIST, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tArtist: %s\n", str);

    result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_ARTIST_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tArtist Phonetic: %s\n", str);

    result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_PUBLISHER, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tPublisher: %s\n", str);

    result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_PUBLISHER_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tPublisher Phonetic: %s\n", str);

    result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_COPYRIGHT, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tCopyright: %s\n", str);

    result = sacd_get_disc_text(ctx, text_channel, ALBUM_TEXT_TYPE_COPYRIGHT_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\tCopyright Phonetic: %s\n", str);
}

static void sacd_print_disc_info(sacd_t *ctx)
{
    uint8_t num_text_channels = 0;
    uint8_t text_channel;
    uint16_t i;
    int result;

    sa_fprintf(stdout, "\nDisc Information:\n");
    {
        uint8_t major, minor;
        result = sacd_get_disc_spec_version(ctx, &major, &minor);
        if (result == SACD_OK)
            sa_fprintf(stdout, "\tVersion: %2i.%02i\n", major, minor);
    }

    {
        uint16_t year;
        uint8_t month, day;
        result = sacd_get_disc_date(ctx, &year, &month, &day);
        if (result == SACD_OK)
            sa_fprintf(stdout, "\tCreation date: %4i-%02i-%02i\n", year, month, day);
    }

    {
        const char *catalog_num;
        result = sacd_get_disc_catalog_num(ctx, &catalog_num);
        if (result == SACD_OK)
            sa_fprintf(stdout, "\tDisc Catalog Number: %s\n", catalog_num);
    }

    for (i = 1; i < 5; i++)
    {
        uint8_t genre_table;
        uint16_t genre_index;
        result = sacd_get_disc_genre(ctx, i, &genre_table, &genre_index);
        if (result == SACD_OK && genre_table)
        {
            sa_fprintf(stdout, "\tDisc Category: %s\n", album_category[genre_table]);
            switch (genre_table) {
                case CATEGORY_GENERAL:
                    sa_fprintf(stdout, "\tDisc Genre: %s\n", album_genre_general[genre_index]);
                    break;
                case CATEGORY_JAPANESE:
                    sa_fprintf(stdout, "\tDisc Genre: %s\n", album_genre_japanese[genre_index]);
                    break;
            }
        }
    }

    /* Get number of text channels (languages) and print metadata for each */
    result = sacd_get_master_text_channel_count(ctx, &num_text_channels);
    if (result == SACD_OK && num_text_channels > 0)
    {
        sa_fprintf(stdout, "\tText Channels: %d\n", num_text_channels);
        for (text_channel = 1; text_channel <= num_text_channels; text_channel++)
        {
            sa_fprintf(stdout, "\n\tDisc Text [Channel %d]:\n", text_channel);
            sacd_print_disc_metadata(ctx, text_channel);
        }
    }
    else
    {
        /* Fallback to channel 1 if count unavailable */
        sacd_print_disc_metadata(ctx, 1);
    }

    sa_fprintf(stdout, "\nAlbum Information:\n");
    {
        const char *catalog_num;
        result = sacd_get_album_catalog_num(ctx, &catalog_num);
        if (result == SACD_OK)
            sa_fprintf(stdout, "\tAlbum Catalog Number: %s\n", catalog_num);
    }

    {
        uint16_t disc_sequence_num;
        result = sacd_get_disc_sequence_num(ctx, &disc_sequence_num);
        if (result == SACD_OK)
            sa_fprintf(stdout, "\tSequence Number: %i\n", disc_sequence_num);
    }

    {
        uint16_t num_disc;
        result = sacd_get_album_disc_count(ctx, &num_disc);
        if (result == SACD_OK)
            sa_fprintf(stdout, "\tSet Size: %i\n", num_disc);
    }

    for (i = 1; i < 5; i++)
    {
        uint8_t genre_table;
        uint16_t genre_index;
        result = sacd_get_album_genre(ctx, i, &genre_table, &genre_index);
        if (result == SACD_OK && genre_table)
        {
            sa_fprintf(stdout, "\tAlbum Category: %s\n", album_category[genre_table]);
            switch (genre_table) {
                case CATEGORY_GENERAL:
                    sa_fprintf(stdout, "\tAlbum Genre: %s\n", album_genre_general[genre_index]);
                    break;
                case CATEGORY_JAPANESE:
                    sa_fprintf(stdout, "\tAlbum Genre: %s\n", album_genre_japanese[genre_index]);
                    break;
            }
        }
    }

    /* Print album metadata for each text channel */
    if (num_text_channels > 0)
    {
        for (text_channel = 1; text_channel <= num_text_channels; text_channel++)
        {
            sa_fprintf(stdout, "\n\tAlbum Text [Channel %d]:\n", text_channel);
            sacd_print_album_metadata(ctx, text_channel);
        }
    }
    else
    {
        sacd_print_album_metadata(ctx, 1);
    }
}

static void sacd_print_track_text(sacd_t *ctx, uint8_t track_num, uint8_t text_channel)
{
    const char *str;
    int result;

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_TITLE, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tTitle: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_TITLE_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tTitle Phonetic: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_PERFORMER, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tPerformer: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_PERFORMER_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tPerformer Phonetic: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_SONGWRITER, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tSongwriter: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_SONGWRITER_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tSongwriter Phonetic: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_COMPOSER, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tComposer: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_COMPOSER_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tComposer Phonetic: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_ARRANGER, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tArranger: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_ARRANGER_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tArranger Phonetic: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_MESSAGE, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tMessage: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_MESSAGE_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tMessage Phonetic: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_EXTRA_MESSAGE, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tExtra Message: %s\n", str);

    result = sacd_get_track_text(ctx, track_num, text_channel, TRACK_TYPE_EXTRA_MESSAGE_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\t\tExtra Message Phonetic: %s\n", str);
}

static void sacd_print_track_list(sacd_t *ctx, int area_idx)
{
    uint8_t i, track_count = 0;
    uint8_t num_text_channels = 0;
    uint8_t text_channel;
    int result;

    result = sacd_get_track_count(ctx, &track_count);
    if (result != SACD_OK)
        return;

    /* Get number of text channels for this area */
    result = sacd_get_area_text_channel_count(ctx, &num_text_channels);
    if (result != SACD_OK || num_text_channels == 0)
        num_text_channels = 1;

    sa_fprintf(stdout, "\tTrack list [%d] (%d text channels):\n", area_idx, num_text_channels);

    for (i = 1; i <= track_count; i++)
    {
        sa_fprintf(stdout, "\t\tTrack %d:\n", i);

        /* Print track timing info (independent of text channel) */
        {
            uint32_t frame_length, index_start;
            result = sacd_get_track_frame_length(ctx, i, &frame_length);
            if (result == SACD_OK)
            {
                time_sacd_t frame_length_time = frame_to_time(frame_length);
                result = sacd_get_track_index_start(ctx, i, 1, &index_start);
                if (result == SACD_OK)
                {
                    time_sacd_t time_start = frame_to_time(index_start);
                    sa_fprintf(stdout, "\t\t\tStart: %02d:%02d:%02d, Duration: %02d:%02d:%02d [mins:secs:frames]\n",
                               time_start.minutes, time_start.seconds, time_start.frames,
                               frame_length_time.minutes, frame_length_time.seconds, frame_length_time.frames);
                }
            }
        }

        /* Print text for each available text channel */
        for (text_channel = 1; text_channel <= num_text_channels; text_channel++)
        {
            char lang[2] = {0};
            const char *language_code = lang;
            uint8_t character_set_code = 0;

            result = sacd_get_area_text_channel_info(ctx, text_channel, &language_code, &character_set_code);
            if (result == SACD_OK && language_code[0] && language_code[1])
            {
                sa_fprintf(stdout, "\t\t\t[Channel %d - %.2s]:\n", text_channel, language_code);
            }
            else if (num_text_channels > 1)
            {
                sa_fprintf(stdout, "\t\t\t[Channel %d]:\n", text_channel);
            }

            sacd_print_track_text(ctx, i, text_channel);
        }
        sa_fprintf(stdout, "\n");
    }
}

static void sacd_print_area_text(sacd_t *ctx, uint8_t text_channel)
{
    const char *str;
    int result;

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_COPYRIGHT, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\tCopyright: %s\n", str);

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_COPYRIGHT_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\tCopyright Phonetic: %s\n", str);

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\tArea Description: %s\n", str);

    result = sacd_get_album_text(ctx, text_channel, ALBUM_TEXT_TYPE_TITLE_PHONETIC, &str);
    if (result == SACD_OK && str)
        sa_fprintf(stdout, "\t\tArea Description Phonetic: %s\n", str);
}

static void sacd_print_area_info(sacd_t *ctx, int area_idx)
{
    int result;
    uint8_t track_count = 0;
    uint8_t num_text_channels = 0;
    uint8_t text_channel;

    sa_fprintf(stdout, "\tArea Information [%i]:\n\n", area_idx);
    {
        uint8_t major, minor;
        result = sacd_get_area_spec_version(ctx, &major, &minor);
        if (result == SACD_OK)
            sa_fprintf(stdout, "\tVersion: %2i.%02i\n", major, minor);
    }

    {
        result = sacd_get_track_count(ctx, &track_count);
        if (result == SACD_OK)
            sa_fprintf(stdout, "\tTrack Count: %i\n", track_count);
    }

    {
        uint32_t tpf;
        result = sacd_get_total_area_play_time(ctx, &tpf);
        if (result == SACD_OK)
        {
            time_sacd_t tpt = frame_to_time(tpf);
            sa_fprintf(stdout, "\tTotal play time: %02d:%02d:%02d [mins:secs:frames]\n", tpt.minutes, tpt.seconds, tpt.frames);
        }
    }

    {
        uint16_t channel_count;
        uint8_t loudspeaker_config, usage_ch4;
        result = sacd_get_area_channel_count(ctx, &channel_count);
        if (result == SACD_OK)
        {
            result = sacd_get_area_loudspeaker_config(ctx, &loudspeaker_config, &usage_ch4);
            if (result == SACD_OK)
            {
                sa_fprintf(stdout, "\tSpeaker config: ");
                if (channel_count == 2 && usage_ch4 == 0)
                {
                    sa_fprintf(stdout, "2 Channel\n");
                }
                else if (channel_count == 5 && usage_ch4 == 3)
                {
                    sa_fprintf(stdout, "5 Channel\n");
                }
                else if (channel_count == 6 && usage_ch4 == 4)
                {
                    sa_fprintf(stdout, "6 Channel\n");
                }
                else
                {
                    sa_fprintf(stdout, "Unknown\n");
                }
            }
        }
    }

    /* Get and display text channels info for this area */
    result = sacd_get_area_text_channel_count(ctx, &num_text_channels);
    if (result == SACD_OK && num_text_channels > 0)
    {
        sa_fprintf(stdout, "\tText Channels: %d\n", num_text_channels);

        for (text_channel = 1; text_channel <= num_text_channels; text_channel++)
        {
            char lang[2] = {0};
            const char *language_code = lang;
            uint8_t character_set_code = 0;

            result = sacd_get_area_text_channel_info(ctx, text_channel, &language_code, &character_set_code);
            if (result == SACD_OK)
            {
                const char *charset_name = character_sets[character_set_code & 0x07];
                if (language_code[0] && language_code[1])
                {
                    sa_fprintf(stdout, "\n\tArea Text [Channel %d - %.2s, %s]:\n", text_channel, language_code, charset_name);
                }
                else
                {
                    sa_fprintf(stdout, "\n\tArea Text [Channel %d - %s]:\n", text_channel, charset_name);
                }
            }
            else
            {
                sa_fprintf(stdout, "\n\tArea Text [Channel %d]:\n", text_channel);
            }

            sacd_print_area_text(ctx, text_channel);
        }
    }
    else
    {
        /* Fallback to channel 1 */
        sa_fprintf(stdout, "\n\tArea Text:\n");
        sacd_print_area_text(ctx, 1);
    }

    sacd_print_track_list(ctx, area_idx);

    /* Print ISRC information for each track */
    sa_fprintf(stdout, "\tISRC Information:\n");
    for (uint8_t i = 1; i <= track_count; i++)
    {
        area_isrc_t isrc = {0};

        result = sacd_get_track_isrc_num(ctx, i, &isrc);
        if (result == SACD_OK && isrc.country_code[0])
        {
            sa_fprintf(stdout, "\t\tTrack %d: %.2s-%.3s-%.2s-%.5s\n",
                       i, isrc.country_code, isrc.owner_code,
                       isrc.recording_year, isrc.designation_code);
        }
    }
}

void sacd_print_disc_summary(sacd_t *ctx)
{
    int result;

    if (!ctx)
        return;

    sacd_print_disc_info(ctx);

    uint16_t nr_types = 2;
    channel_t channel_types[2];
    result = sacd_get_available_channel_types(ctx, channel_types, &nr_types);
    if (result != SACD_OK)
        return;

    sa_fprintf(stdout, "\nArea count: %i\n", nr_types);

    result = sacd_select_channel_type(ctx, TWO_CHANNEL);
    if (result == SACD_OK)
    {
        sacd_print_area_info(ctx, TWO_CHANNEL);
    }

    result = sacd_select_channel_type(ctx, MULTI_CHANNEL);
    if (result == SACD_OK)
    {
        sacd_print_area_info(ctx, MULTI_CHANNEL);
    }
}
