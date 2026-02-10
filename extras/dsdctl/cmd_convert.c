/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Convert command implementation
 * Converts DSD audio formats (SACD ISO, DSF, DSDIFF) to various output
 * formats using the libdsdpipe pipeline. Supports multi-channel extraction,
 * DSD-to-PCM conversion, and multiple simultaneous output sinks.
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


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "cmd_convert.h"
#include "cli_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <libdsdpipe/dsdpipe.h>
#include <libsautil/mem.h>
#include <libsautil/sa_path.h>
#include <libsautil/sastring.h>
#include <libsautil/getopt.h>
#include <libsautil/time.h>

/* ==========================================================================
 * Convert options
 * ========================================================================== */

typedef struct {
    const char *input_path;
    const char *output_dir;
    uint32_t out_formats;
    const char *area;
    const char *track_spec;
    int pcm_bit_depth;
    int pcm_sample_rate;
    dsdpipe_pcm_quality_t pcm_quality;
    int flac_compression;
    int write_dst;
    int write_id3;
    int artist_flag;
    dsdpipe_track_format_t track_format;
    int list_only;
    int verbose;
    int show_progress;
} convert_opts_t;

/* ==========================================================================
 * Long options for getopt_long
 * ========================================================================== */

enum {
    OPT_DSF = 256,
    OPT_DSDIFF,
    OPT_DFF,
    OPT_EDIT_MASTER,
    OPT_EM,
    OPT_WAV,
    OPT_FLAC,
    OPT_DST,
    OPT_DECODE_DST,
    OPT_ID3,
    OPT_NO_ID3,
    OPT_XML,
    OPT_CUE,
    OPT_CUESHEET,
    OPT_PRINT,
    OPT_ARTIST,
    OPT_TRACK_FORMAT,
    OPT_NO_PROGRESS
};

static struct option long_options[] = {
    /* Output formats */
    {"dsf",         no_argument,       NULL, OPT_DSF},
    {"dsdiff",      no_argument,       NULL, OPT_DSDIFF},
    {"dff",         no_argument,       NULL, OPT_DFF},
    {"edit-master", no_argument,       NULL, OPT_EDIT_MASTER},
    {"em",          no_argument,       NULL, OPT_EM},
    {"wav",         no_argument,       NULL, OPT_WAV},
    {"flac",        no_argument,       NULL, OPT_FLAC},
    /* Metadata formats */
    {"xml",         no_argument,       NULL, OPT_XML},
    {"cue",         no_argument,       NULL, OPT_CUE},
    {"cuesheet",    no_argument,       NULL, OPT_CUESHEET},
    {"print",       no_argument,       NULL, OPT_PRINT},
    /* Format specification */
    {"format",      required_argument, NULL, 'f'},
    /* PCM options */
    {"bits",        required_argument, NULL, 'b'},
    {"rate",        required_argument, NULL, 'r'},
    {"quality",     required_argument, NULL, 'q'},
    {"compression", required_argument, NULL, 'c'},
    /* DST options */
    {"dst",         no_argument,       NULL, OPT_DST},
    {"decode-dst",  no_argument,       NULL, OPT_DECODE_DST},
    /* Track/area selection */
    {"tracks",      required_argument, NULL, 't'},
    {"area",        required_argument, NULL, 'a'},
    /* Metadata options */
    {"id3",         no_argument,       NULL, OPT_ID3},
    {"no-id3",      no_argument,       NULL, OPT_NO_ID3},
    /* Output directory options */
    {"artist",      no_argument,       NULL, 'A'},
    {"track-format", required_argument, NULL, OPT_TRACK_FORMAT},
    /* Behavior */
    {"no-progress", no_argument,       NULL, OPT_NO_PROGRESS},
    {"list",        no_argument,       NULL, 'l'},
    {"verbose",     no_argument,       NULL, 'v'},
    {"help",        no_argument,       NULL, 'h'},
    {NULL,          0,                 NULL, 0}
};

/* ==========================================================================
 * Help
 * ========================================================================== */

static void print_convert_help(void)
{
    printf("Usage: dsdctl convert [options] <input> [output_dir]\n\n");
    printf("Convert DSD audio formats with support for multiple simultaneous outputs.\n\n");

    printf("Supported Input Formats:\n");
    printf("  SACD ISO images (.iso), DSF files (.dsf), DSDIFF files (.dff, .dsdiff)\n\n");

    printf("Output Format Options (can specify multiple for simultaneous output):\n");
    printf("  -f, --format <fmt>      Add output format (can be repeated)\n");
    printf("                          Formats: dsf, dsdiff, dff, em, wav, flac, xml, cue, print\n");
    printf("  --dsf                   Output as DSF files\n");
    printf("  --dsdiff, --dff         Output as DSDIFF files\n");
    printf("  --edit-master, --em     Output as single DSDIFF Edit Master\n");
    printf("  --wav                   Output as WAV (DSD-to-PCM conversion)\n");
    printf("  --flac                  Output as FLAC (DSD-to-PCM conversion)\n");
    printf("  --xml                   Export XML metadata\n");
    printf("  --cue, --cuesheet       Generate CUE sheet\n");
    printf("  --print                 Print metadata to stdout\n");
    printf("\n");
    printf("  NOTE: If no format specified, defaults to DSF.\n");
    printf("        Example: --dsf --wav outputs both formats simultaneously.\n\n");

    printf("WAV/FLAC Options (PCM formats):\n");
    printf("  -b, --bits <depth>      PCM bit depth: 16, 24, 32 (default: 24)\n");
    printf("  -r, --rate <Hz>         PCM sample rate (default: auto from DSD rate)\n");
    printf("  -q, --quality <level>   DSD-to-PCM quality: fast, normal, high (default: normal)\n");
    printf("  -c, --compression <0-8> FLAC compression level (default: 5)\n\n");

    printf("DST Options:\n");
    printf("  --dst                   Keep DST compression (DSDIFF only)\n");
    printf("  --decode-dst            Decode DST to raw DSD (default)\n\n");

    printf("Track/Area Selection:\n");
    printf("  -t, --tracks <spec>     Track selection (default: all)\n");
    printf("                          Examples: \"all\", \"1\", \"1-5\", \"1,3,5\"\n");
    printf("  -a, --area <type>       Audio area: stereo, multichannel (default: stereo)\n\n");

    printf("Metadata Options:\n");
    printf("  -i, --id3               Write ID3v2 metadata tags (default)\n");
    printf("  -n, --no-id3            Disable ID3v2 tags\n\n");

    printf("Output Directory Options:\n");
    printf("  -A, --artist            Include artist in output directory name\n");
    printf("  --track-format <fmt>    Track filename format (default: artist)\n");
    printf("                          number: 01, 02, ...\n");
    printf("                          title:  01 - Track Title\n");
    printf("                          artist: 01 - Artist - Track Title\n\n");

    printf("Other Options:\n");
    printf("  -l, --list              List tracks only, don't convert\n");
    printf("  --no-progress           Disable progress bar\n");
    printf("  -v, --verbose           Verbose output\n");
    printf("  -h, --help              Show this help\n\n");

    printf("Examples:\n");
    printf("  dsdctl convert album.iso ./output\n");
    printf("  dsdctl convert --dsdiff album.iso ./output\n");
    printf("  dsdctl convert --flac -b 24 -q high album.iso ./output\n");
    printf("  dsdctl convert --dsf --wav --flac album.iso ./output\n");
    printf("  dsdctl convert --edit-master --cue --xml album.iso ./output\n");
    printf("  dsdctl convert -a multichannel --dsf album.iso ./output\n");
    printf("  dsdctl convert --dsdiff track.dsf ./output\n");
    printf("  dsdctl convert -l album.iso\n");
}

/* ==========================================================================
 * Print album and track metadata
 * ========================================================================== */

static void print_album_info(dsdpipe_t *pipe)
{
    dsdpipe_metadata_t meta;
    dsdpipe_format_t format;
    uint8_t track_count = 0;

    dsdpipe_metadata_init(&meta);

    if (dsdpipe_get_album_metadata(pipe, &meta) == DSDPIPE_OK) {
        printf("\nAlbum Information:\n");
        printf("------------------\n");
        if (meta.album_title)
            printf("  Title:     %s\n", meta.album_title);
        if (meta.album_artist)
            printf("  Artist:    %s\n", meta.album_artist);
        if (meta.year > 0)
            printf("  Year:      %d\n", meta.year);
        if (meta.genre)
            printf("  Genre:     %s\n", meta.genre);
        if (meta.catalog_number)
            printf("  Catalog:   %s\n", meta.catalog_number);
        if (meta.disc_total > 1)
            printf("  Disc:      %d of %d\n", meta.disc_number, meta.disc_total);
        dsdpipe_metadata_free(&meta);
    }

    if (dsdpipe_get_source_format(pipe, &format) == DSDPIPE_OK) {
        printf("\nAudio Format:\n");
        printf("-------------\n");
        printf("  Channels:    %s (%d ch)\n",
               dsdpipe_get_speaker_config_string(&format),
               format.channel_count);
        printf("  Sample Rate: %u Hz (DSD%d)\n",
               format.sample_rate,
               format.sample_rate / 44100);
        printf("  Format:      %s\n",
               dsdpipe_get_frame_format_string(&format));
    }

    if (dsdpipe_get_track_count(pipe, &track_count) == DSDPIPE_OK) {
        printf("  Tracks:      %d\n", track_count);
    }

    printf("\n");
}

static void print_track_list(dsdpipe_t *pipe)
{
    uint8_t track_count = 0;
    dsdpipe_metadata_t meta;

    if (dsdpipe_get_track_count(pipe, &track_count) != DSDPIPE_OK) {
        return;
    }

    printf("Track List:\n");
    printf("-----------\n");

    for (uint8_t i = 1; i <= track_count; i++) {
        dsdpipe_metadata_init(&meta);
        if (dsdpipe_get_track_metadata(pipe, i, &meta) == DSDPIPE_OK) {
            int minutes = (int)(meta.duration_seconds / 60);
            int seconds = (int)(meta.duration_seconds) % 60;
            printf("  %2d. %-40s %d:%02d\n",
                   i,
                   meta.track_title ? meta.track_title : "(untitled)",
                   minutes, seconds);
            dsdpipe_metadata_free(&meta);
        }
    }
    printf("\n");
}

/* ==========================================================================
 * Convert implementation
 * ========================================================================== */

static int do_convert(const convert_opts_t *opts)
{
    dsdpipe_t *pipe = NULL;
    cli_input_type_t in_type;
    dsdpipe_channel_type_t channel_type;
    int result;

    /* Parse area type */
    if (sa_strcasecmp(opts->area, "stereo") == 0 ||
        sa_strcasecmp(opts->area, "2ch") == 0) {
        channel_type = DSDPIPE_CHANNEL_STEREO;
    } else if (sa_strcasecmp(opts->area, "multichannel") == 0 ||
               sa_strcasecmp(opts->area, "multi") == 0 ||
               sa_strcasecmp(opts->area, "5.1") == 0) {
        channel_type = DSDPIPE_CHANNEL_MULTICHANNEL;
    } else {
        cli_error("Unknown area type: %s (use 'stereo' or 'multichannel')", opts->area);
        return 1;
    }

    /* Validate format-specific options */
    if (opts->write_dst &&
        (opts->out_formats & ~(CLI_FORMAT_DSDIFF | CLI_FORMAT_DSDIFF_EM))) {
        if (opts->out_formats & CLI_FORMAT_DSF)
            cli_warning("DSF does not support DST passthrough. DST will be decoded.");
        if (opts->out_formats & CLI_FORMAT_PCM_MASK)
            cli_warning("PCM formats do not support DST passthrough. DST will be decoded.");
    }

    if ((opts->out_formats & CLI_FORMAT_FLAC) && opts->pcm_bit_depth == 32) {
        cli_warning("FLAC does not support 32-bit. Using 24-bit for FLAC.");
    }

    if ((opts->out_formats & CLI_FORMAT_FLAC) && !dsdpipe_has_flac_support()) {
        cli_error("FLAC support not available (libFLAC not compiled in).");
        return 1;
    }

    /* Install signal handler */
    cli_install_signal_handler();

    /* Create pipeline */
    pipe = dsdpipe_create();
    if (!pipe) {
        cli_error("Failed to create pipeline");
        return 1;
    }

    cli_set_pipe_for_cancel(pipe);

    /* Detect and configure source */
    in_type = cli_detect_input_type(opts->input_path);
    printf("Opening: %s\n", opts->input_path);
    printf("Source:  %s\n", cli_input_type_name(in_type));

    switch (in_type) {
    case CLI_INPUT_SACD:
        printf("Area:    %s\n", opts->area);
        result = dsdpipe_set_source_sacd(pipe, opts->input_path, channel_type);
        break;
    case CLI_INPUT_DSF:
        result = dsdpipe_set_source_dsf(pipe, opts->input_path);
        break;
    case CLI_INPUT_DSDIFF:
        result = dsdpipe_set_source_dsdiff(pipe, opts->input_path);
        break;
    default:
        cli_error("Unsupported input type for convert: %s",
                  cli_input_type_name(in_type));
        dsdpipe_destroy(pipe);
        cli_set_pipe_for_cancel(NULL);
        return 1;
    }

    if (result != DSDPIPE_OK) {
        cli_error("Failed to open source: %s", dsdpipe_get_error_message(pipe));
        dsdpipe_destroy(pipe);
        cli_set_pipe_for_cancel(NULL);
        return 1;
    }

    if (in_type != CLI_INPUT_SACD && sa_strcasecmp(opts->area, "stereo") != 0) {
        printf("Note:    Area option ignored for %s input\n",
               cli_input_type_name(in_type));
    }

    /* Print album information */
    print_album_info(pipe);

    /* List-only mode */
    if (opts->list_only) {
        print_track_list(pipe);
        dsdpipe_destroy(pipe);
        cli_set_pipe_for_cancel(NULL);
        return 0;
    }

    if (opts->verbose) {
        print_track_list(pipe);
    }

    /* Select tracks */
    result = dsdpipe_select_tracks_str(pipe, opts->track_spec);
    if (result != DSDPIPE_OK) {
        cli_error("Invalid track selection: %s (%s)",
                  opts->track_spec, dsdpipe_get_error_message(pipe));
        dsdpipe_destroy(pipe);
        cli_set_pipe_for_cancel(NULL);
        return 1;
    }

    /* Print selected tracks */
    {
        uint8_t selected[256];
        size_t count = 0;
        if (dsdpipe_get_selected_tracks(pipe, selected, 256, &count) == DSDPIPE_OK) {
            printf("Selected: %zu track(s)", count);
            if (opts->verbose && count <= 20) {
                printf(" [");
                for (size_t i = 0; i < count; i++) {
                    printf("%s%d", i > 0 ? "," : "", selected[i]);
                }
                printf("]");
            }
            printf("\n");
        }
    }

    /* Generate album output directory from metadata */
    char *album_output_path = NULL;
    {
        dsdpipe_metadata_t album_meta = {0};
        if (dsdpipe_get_album_metadata(pipe, &album_meta) == DSDPIPE_OK) {
            dsdpipe_album_format_t dir_format = opts->artist_flag
                ? DSDPIPE_ALBUM_ARTIST_TITLE
                : DSDPIPE_ALBUM_TITLE_ONLY;

            char *album_dir = dsdpipe_get_album_dir(&album_meta, dir_format);
            if (album_dir != NULL) {
                album_output_path = sa_unique_path(opts->output_dir, album_dir, NULL);

                if (album_output_path &&
                    album_meta.disc_total > 1 && album_meta.disc_number > 0) {
                    char disc_subdir[32];
                    snprintf(disc_subdir, sizeof(disc_subdir),
                             "Disc %u", album_meta.disc_number);
                    char *full_path = sa_append_path_component(
                        album_output_path, disc_subdir);
                    sa_free(album_output_path);
                    album_output_path = full_path;
                }

                sa_free(album_dir);
            }
            dsdpipe_metadata_free(&album_meta);
        }
    }

    const char *final_output = album_output_path ? album_output_path : opts->output_dir;

    /* Derive album base name for metadata files */
    char album_base_name[256] = "album";
    {
        dsdpipe_metadata_t meta_tmp = {0};
        if (dsdpipe_get_album_metadata(pipe, &meta_tmp) == DSDPIPE_OK) {
            dsdpipe_album_format_t name_format = opts->artist_flag
                ? DSDPIPE_ALBUM_ARTIST_TITLE
                : DSDPIPE_ALBUM_TITLE_ONLY;
            char *dir_name = dsdpipe_get_album_dir(&meta_tmp, name_format);
            if (dir_name) {
                sa_strlcpy(album_base_name, dir_name, sizeof(album_base_name));
                sa_free(dir_name);
            }
            dsdpipe_metadata_free(&meta_tmp);
        }
    }

    /* Print output configuration */
    printf("Output:  %s\n", final_output);
    if (album_output_path)
        printf("         (auto-generated from album metadata)\n");

    {
        int format_count = cli_count_formats(opts->out_formats);
        printf("Formats: %d output%s\n", format_count, format_count > 1 ? "s" : "");
    }

    /* Set PCM quality if any PCM format requested */
    if (opts->out_formats & CLI_FORMAT_PCM_MASK) {
        dsdpipe_set_pcm_quality(pipe, opts->pcm_quality);
    }

    /* Set track filename format */
    dsdpipe_set_track_filename_format(pipe, opts->track_format);
    if (opts->verbose) {
        printf("Track naming: %s\n", cli_track_format_name(opts->track_format));
    }

    /* Configure sinks based on format bitmask */
    result = DSDPIPE_OK;
    int sink_count = 0;

    /* Print sink (first, for immediate visibility) */
    if ((opts->out_formats & CLI_FORMAT_PRINT) && result == DSDPIPE_OK) {
        printf("\n[Sink %d] Text Metadata\n", ++sink_count);
        result = dsdpipe_add_sink_print(pipe, NULL);
        if (result != DSDPIPE_OK)
            cli_error("Failed to configure text output: %s",
                      dsdpipe_get_error_message(pipe));
    }

    /* DSF sink */
    if ((opts->out_formats & CLI_FORMAT_DSF) && result == DSDPIPE_OK) {
        printf("\n[Sink %d] DSF (ID3: %s)\n", ++sink_count,
               opts->write_id3 ? "enabled" : "disabled");
        result = dsdpipe_add_sink_dsf(pipe, final_output, opts->write_id3);
        if (result != DSDPIPE_OK)
            cli_error("Failed to configure DSF output: %s",
                      dsdpipe_get_error_message(pipe));
    }

    /* DSDIFF sink */
    if ((opts->out_formats & CLI_FORMAT_DSDIFF) && result == DSDPIPE_OK) {
        printf("\n[Sink %d] DSDIFF (DST: %s, ID3: %s)\n", ++sink_count,
               opts->write_dst ? "passthrough" : "decode",
               opts->write_id3 ? "enabled" : "disabled");
        result = dsdpipe_add_sink_dsdiff(pipe, final_output,
                                          opts->write_dst, false, opts->write_id3);
        if (result != DSDPIPE_OK)
            cli_error("Failed to configure DSDIFF output: %s",
                      dsdpipe_get_error_message(pipe));
    }

    /* DSDIFF Edit Master sink */
    if ((opts->out_formats & CLI_FORMAT_DSDIFF_EM) && result == DSDPIPE_OK) {
        printf("\n[Sink %d] DSDIFF Edit Master (DST: %s, ID3: %s)\n", ++sink_count,
               opts->write_dst ? "passthrough" : "decode",
               opts->write_id3 ? "enabled" : "disabled");
        result = dsdpipe_add_sink_dsdiff(pipe, final_output,
                                          opts->write_dst, true, opts->write_id3);
        if (result != DSDPIPE_OK)
            cli_error("Failed to configure DSDIFF Edit Master output: %s",
                      dsdpipe_get_error_message(pipe));
    }

    /* WAV sink */
    if ((opts->out_formats & CLI_FORMAT_WAV) && result == DSDPIPE_OK) {
        printf("\n[Sink %d] WAV (%d-bit, %s, quality: %s)\n", ++sink_count,
               opts->pcm_bit_depth,
               opts->pcm_sample_rate > 0 ? "custom rate" : "auto rate",
               cli_pcm_quality_name(opts->pcm_quality));
        result = dsdpipe_add_sink_wav(pipe, final_output,
                                       opts->pcm_bit_depth, opts->pcm_sample_rate);
        if (result != DSDPIPE_OK)
            cli_error("Failed to configure WAV output: %s",
                      dsdpipe_get_error_message(pipe));
    }

    /* FLAC sink */
    if ((opts->out_formats & CLI_FORMAT_FLAC) && result == DSDPIPE_OK) {
        int flac_bit_depth = (opts->pcm_bit_depth == 32) ? 24 : opts->pcm_bit_depth;
        printf("\n[Sink %d] FLAC (%d-bit, compression: %d, quality: %s)\n",
               ++sink_count, flac_bit_depth, opts->flac_compression,
               cli_pcm_quality_name(opts->pcm_quality));
        result = dsdpipe_add_sink_flac(pipe, final_output,
                                        flac_bit_depth, opts->flac_compression);
        if (result != DSDPIPE_OK)
            cli_error("Failed to configure FLAC output: %s",
                      dsdpipe_get_error_message(pipe));
    }

    /* XML metadata sink */
    if ((opts->out_formats & CLI_FORMAT_XML) && result == DSDPIPE_OK) {
        char *xml_path = sa_make_path(final_output, NULL, album_base_name, "xml");
        if (!xml_path) {
            cli_error("Failed to build XML output path");
            result = DSDPIPE_ERROR_OUT_OF_MEMORY;
        } else {
            printf("\n[Sink %d] XML Metadata: %s\n", ++sink_count, xml_path);
            result = dsdpipe_add_sink_xml(pipe, xml_path);
            if (result != DSDPIPE_OK)
                cli_error("Failed to configure XML output: %s",
                          dsdpipe_get_error_message(pipe));
            sa_free(xml_path);
        }
    }

    /* CUE sheet sink */
    if ((opts->out_formats & CLI_FORMAT_CUE) && result == DSDPIPE_OK) {
        char audio_ref[280];
        if (opts->out_formats & CLI_FORMAT_DSDIFF_EM)
            snprintf(audio_ref, sizeof(audio_ref), "%s.dff", album_base_name);
        else if (opts->out_formats & CLI_FORMAT_DSDIFF)
            snprintf(audio_ref, sizeof(audio_ref), "%s.dff", album_base_name);
        else if (opts->out_formats & CLI_FORMAT_DSF)
            snprintf(audio_ref, sizeof(audio_ref), "%s.dsf", album_base_name);
        else if (opts->out_formats & CLI_FORMAT_WAV)
            snprintf(audio_ref, sizeof(audio_ref), "%s.wav", album_base_name);
        else if (opts->out_formats & CLI_FORMAT_FLAC)
            snprintf(audio_ref, sizeof(audio_ref), "%s.flac", album_base_name);
        else
            snprintf(audio_ref, sizeof(audio_ref), "%s.dff", album_base_name);

        char *cue_path = sa_make_path(final_output, NULL, album_base_name, "cue");
        if (!cue_path) {
            cli_error("Failed to build CUE output path");
            result = DSDPIPE_ERROR_OUT_OF_MEMORY;
        } else {
            printf("\n[Sink %d] CUE Sheet: %s (ref: %s)\n",
                   ++sink_count, cue_path, audio_ref);
            result = dsdpipe_add_sink_cue(pipe, cue_path, audio_ref);
            if (result != DSDPIPE_OK)
                cli_error("Failed to configure CUE output: %s",
                          dsdpipe_get_error_message(pipe));
            sa_free(cue_path);
        }
    }

    if (result != DSDPIPE_OK) {
        sa_free(album_output_path);
        dsdpipe_destroy(pipe);
        cli_set_pipe_for_cancel(NULL);
        return 1;
    }

    /* Preview files to be created */
    {
        uint8_t selected[256];
        size_t sel_count = 0;
        dsdpipe_get_selected_tracks(pipe, selected, 256, &sel_count);

        int has_per_track = opts->out_formats &
            (CLI_FORMAT_DSF | CLI_FORMAT_DSDIFF | CLI_FORMAT_WAV | CLI_FORMAT_FLAC);
        if (has_per_track && sel_count > 0) {
            printf("\nFiles:\n");
            for (size_t i = 0; i < sel_count; i++) {
                dsdpipe_metadata_t trk_meta = {0};
                dsdpipe_get_track_metadata(pipe, selected[i], &trk_meta);
                char *trk_name = dsdpipe_get_track_filename(&trk_meta,
                                                             opts->track_format);
                if (!trk_name)
                    trk_name = sa_asprintf("%02u - Track %u",
                                           selected[i], selected[i]);

                if (opts->out_formats & CLI_FORMAT_DSF)
                    printf("  %s.dsf\n", trk_name);
                if (opts->out_formats & CLI_FORMAT_DSDIFF)
                    printf("  %s.dff\n", trk_name);
                if (opts->out_formats & CLI_FORMAT_WAV)
                    printf("  %s.wav\n", trk_name);
                if (opts->out_formats & CLI_FORMAT_FLAC)
                    printf("  %s.flac\n", trk_name);

                sa_free(trk_name);
                dsdpipe_metadata_free(&trk_meta);
            }
        }

        if (opts->out_formats & CLI_FORMAT_DSDIFF_EM)
            printf("  %s.dff  [Edit Master]\n", album_base_name);
        if (opts->out_formats & CLI_FORMAT_XML)
            printf("  %s.xml\n", album_base_name);
        if (opts->out_formats & CLI_FORMAT_CUE)
            printf("  %s.cue\n", album_base_name);
    }

    /* Set up progress context */
    cli_progress_ctx_t prog_ctx = {0};
    prog_ctx.verbose = opts->verbose;
    prog_ctx.start_time_ms = (uint64_t)(sa_gettime_relative() / 1000);
    prog_ctx.last_speed_time_ms = prog_ctx.start_time_ms;
    prog_ctx.last_display_time_ms = prog_ctx.start_time_ms;

    if (opts->show_progress) {
        dsdpipe_set_progress_callback(pipe, cli_progress_callback, &prog_ctx);
    }

    /* Note DST handling */
    {
        dsdpipe_format_t src_format;
        if (dsdpipe_get_source_format(pipe, &src_format) == DSDPIPE_OK) {
            int needs_pcm = (opts->out_formats & CLI_FORMAT_PCM_MASK) != 0;
            int needs_dsd = (opts->out_formats & CLI_FORMAT_DSD_MASK) != 0;

            if (src_format.type == DSDPIPE_FORMAT_DST) {
                if (needs_pcm && needs_dsd)
                    printf("\nNote: DST source. Decoding to DSD + converting to PCM.\n");
                else if (needs_pcm)
                    printf("\nNote: DST source. Decoding + converting to PCM.\n");
                else
                    printf("\nNote: DST source. Decoding to DSD.\n");
            } else if (needs_pcm) {
                printf("\nNote: DSD-to-PCM conversion for WAV/FLAC output.\n");
            }
        }
    }

    /* Build format list for display */
    {
        char format_list[256] = "";
        size_t pos = 0;
        static const struct { uint32_t flag; const char *name; } fmts[] = {
            {CLI_FORMAT_DSF, "DSF"}, {CLI_FORMAT_DSDIFF, "DSDIFF"},
            {CLI_FORMAT_DSDIFF_EM, "Edit Master"}, {CLI_FORMAT_WAV, "WAV"},
            {CLI_FORMAT_FLAC, "FLAC"}, {CLI_FORMAT_XML, "XML"},
            {CLI_FORMAT_CUE, "CUE"}, {CLI_FORMAT_PRINT, "TEXT"},
        };
        for (int i = 0; i < (int)(sizeof(fmts) / sizeof(fmts[0])); i++) {
            if (opts->out_formats & fmts[i].flag) {
                pos += (size_t)snprintf(format_list + pos,
                                         sizeof(format_list) - pos,
                                         "%s%s", pos > 0 ? ", " : "",
                                         fmts[i].name);
            }
        }
        printf("\nConverting to %s...\n", format_list);
    }

    /* Run pipeline */
    uint64_t run_start_ms = (uint64_t)(sa_gettime_relative() / 1000);
    result = dsdpipe_run(pipe);
    uint64_t run_end_ms = (uint64_t)(sa_gettime_relative() / 1000);

    if (opts->show_progress)
        cli_progress_clear();

    printf("\n");

    /* Print statistics */
    cli_print_statistics(run_start_ms, run_end_ms, prog_ctx.bytes_written);

    if (result == DSDPIPE_ERROR_CANCELLED) {
        printf("Cancelled by user.\n");
    } else if (result != DSDPIPE_OK) {
        cli_error("Conversion failed: %s", dsdpipe_get_error_message(pipe));
        sa_free(album_output_path);
        dsdpipe_destroy(pipe);
        cli_set_pipe_for_cancel(NULL);
        return 1;
    } else {
        printf("Done!\n");
    }

    /* Cleanup */
    sa_free(album_output_path);
    dsdpipe_destroy(pipe);
    cli_set_pipe_for_cancel(NULL);

    return (result == DSDPIPE_OK) ? 0 : 1;
}

/* ==========================================================================
 * Command entry point
 * ========================================================================== */

int cmd_convert(int argc, char *argv[])
{
    convert_opts_t opts = {
        .input_path = NULL,
        .output_dir = NULL,
        .out_formats = 0,
        .area = "stereo",
        .track_spec = "all",
        .pcm_bit_depth = 24,
        .pcm_sample_rate = 0,
        .pcm_quality = DSDPIPE_PCM_QUALITY_NORMAL,
        .flac_compression = 5,
        .write_dst = 0,
        .write_id3 = 1,
        .artist_flag = 1,
        .track_format = DSDPIPE_TRACK_NUM_ARTIST_TITLE,
        .list_only = 0,
        .verbose = 0,
        .show_progress = 1
    };

    uint32_t fmt;
    int opt;

    /* Reset getopt state for subcommand parsing */
    optind = 1;

    while ((opt = getopt_long(argc, argv, "f:b:r:q:c:t:a:inlvhA",
                               long_options, NULL)) != -1) {
        switch (opt) {
        /* Output format flags */
        case OPT_DSF:
            opts.out_formats |= CLI_FORMAT_DSF;
            break;
        case OPT_DSDIFF:
        case OPT_DFF:
            opts.out_formats |= CLI_FORMAT_DSDIFF;
            break;
        case OPT_EDIT_MASTER:
        case OPT_EM:
            opts.out_formats |= CLI_FORMAT_DSDIFF_EM;
            break;
        case OPT_WAV:
            opts.out_formats |= CLI_FORMAT_WAV;
            break;
        case OPT_FLAC:
            opts.out_formats |= CLI_FORMAT_FLAC;
            break;
        case OPT_XML:
            opts.out_formats |= CLI_FORMAT_XML;
            break;
        case OPT_CUE:
        case OPT_CUESHEET:
            opts.out_formats |= CLI_FORMAT_CUE;
            break;
        case OPT_PRINT:
            opts.out_formats |= CLI_FORMAT_PRINT;
            break;

        /* Format via -f */
        case 'f':
            fmt = cli_parse_format(optarg);
            if (fmt == 0) {
                cli_error("Unknown output format: %s", optarg);
                fprintf(stderr, "  Use: dsf, dsdiff, dff, em, wav, flac, xml, cue, print\n");
                return 1;
            }
            opts.out_formats |= fmt;
            break;

        /* PCM options */
        case 'b':
            opts.pcm_bit_depth = atoi(optarg);
            if (opts.pcm_bit_depth != 16 && opts.pcm_bit_depth != 24 &&
                opts.pcm_bit_depth != 32) {
                cli_error("Invalid bit depth: %d (use 16, 24, or 32)", opts.pcm_bit_depth);
                return 1;
            }
            break;
        case 'r':
            opts.pcm_sample_rate = atoi(optarg);
            if (opts.pcm_sample_rate < 0) {
                cli_error("Invalid sample rate: %s", optarg);
                return 1;
            }
            break;
        case 'q':
            if (cli_parse_pcm_quality(optarg, &opts.pcm_quality) != 0) {
                cli_error("Unknown quality: %s (use fast, normal, or high)", optarg);
                return 1;
            }
            break;
        case 'c':
            opts.flac_compression = atoi(optarg);
            if (opts.flac_compression < 0 || opts.flac_compression > 8) {
                cli_error("Invalid FLAC compression: %d (use 0-8)",
                          opts.flac_compression);
                return 1;
            }
            break;

        /* DST options */
        case OPT_DST:
            opts.write_dst = 1;
            break;
        case OPT_DECODE_DST:
            opts.write_dst = 0;
            break;

        /* Track/area */
        case 't':
            opts.track_spec = optarg;
            break;
        case 'a':
            opts.area = optarg;
            break;

        /* Metadata */
        case 'i':
        case OPT_ID3:
            opts.write_id3 = 1;
            break;
        case 'n':
        case OPT_NO_ID3:
            opts.write_id3 = 0;
            break;

        /* Output directory options */
        case 'A':
            opts.artist_flag = 1;
            break;
        case OPT_TRACK_FORMAT:
            if (cli_parse_track_format(optarg, &opts.track_format) != 0) {
                cli_error("Unknown track format: %s (use number, title, or artist)",
                          optarg);
                return 1;
            }
            break;

        /* Behavior */
        case OPT_NO_PROGRESS:
            opts.show_progress = 0;
            break;
        case 'l':
            opts.list_only = 1;
            break;
        case 'v':
            opts.verbose = 1;
            cli_set_verbose(true);
            break;
        case 'h':
            print_convert_help();
            return 0;

        default:
            fprintf(stderr, "Try 'dsdctl convert --help' for usage.\n");
            return 1;
        }
    }

    /* Get positional arguments */
    for (int i = optind; i < argc; i++) {
        if (!opts.input_path)
            opts.input_path = argv[i];
        else if (!opts.output_dir)
            opts.output_dir = argv[i];
    }

    /* Default to DSF if no format specified */
    if (opts.out_formats == 0) {
        opts.out_formats = CLI_FORMAT_DSF;
    }

    /* Validate required arguments */
    if (!opts.input_path) {
        cli_error("No input file specified");
        fprintf(stderr, "Run 'dsdctl convert --help' for usage.\n");
        return 1;
    }

    if (!opts.output_dir && !opts.list_only) {
        cli_error("No output directory specified");
        fprintf(stderr, "Run 'dsdctl convert --help' for usage.\n");
        return 1;
    }

    return do_convert(&opts);
}
