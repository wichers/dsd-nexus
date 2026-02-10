/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Info command implementation
 * Displays metadata information for SACD ISO images, DSF files,
 * DSDIFF files, PS3 drives, and PS3 network streaming servers.
 * Supports both human-readable text and JSON output.
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

#include "cmd_info.h"
#include "cli_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <libdsdpipe/dsdpipe.h>
#include <libsautil/sastring.h>

/* ==========================================================================
 * Info options
 * ========================================================================== */

typedef struct {
    const char *input_path;
    const char *area;
    bool json_output;
    bool verbose;
} info_opts_t;

/* ==========================================================================
 * Help
 * ========================================================================== */

static void print_info_help(void)
{
    printf("Usage: dsdctl info [options] [input]\n\n");
    printf("Display metadata information about DSD audio files or SACD discs.\n\n");
    printf("Options:\n");
    printf("  -i, --input <path>   Input file, device, or network address\n");
    printf("  --json               Output in JSON format\n");
    printf("  -a, --area <type>    Audio area: stereo, multichannel (SACD only)\n");
    printf("  -v, --verbose        Show detailed track listing\n");
    printf("  -h, --help           Show this help message\n");
    printf("\n");
    printf("Supported Inputs:\n");
    printf("  SACD ISO images (.iso)\n");
    printf("  DSF files (.dsf)\n");
    printf("  DSDIFF files (.dff, .dsdiff)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  dsdctl info album.iso\n");
    printf("  dsdctl info track.dsf\n");
    printf("  dsdctl info --json album.iso\n");
    printf("  dsdctl info -a multichannel album.iso\n");
    printf("  dsdctl info -v album.iso\n");
}

/* ==========================================================================
 * JSON helper
 * ========================================================================== */

static void json_print_string(const char *key, const char *value, bool comma)
{
    if (!value) {
        printf("    \"%s\": null%s\n", key, comma ? "," : "");
        return;
    }

    printf("    \"%s\": \"", key);
    for (const char *p = value; *p; p++) {
        switch (*p) {
        case '"':  printf("\\\""); break;
        case '\\': printf("\\\\"); break;
        case '\n': printf("\\n"); break;
        case '\r': printf("\\r"); break;
        case '\t': printf("\\t"); break;
        default:   putchar(*p); break;
        }
    }
    printf("\"%s\n", comma ? "," : "");
}

/* ==========================================================================
 * Text output
 * ========================================================================== */

static void print_info_text(dsdpipe_t *pipe, bool verbose)
{
    dsdpipe_metadata_t meta;
    dsdpipe_format_t format;
    uint8_t track_count = 0;

    dsdpipe_metadata_init(&meta);

    /* Album information */
    if (dsdpipe_get_album_metadata(pipe, &meta) == DSDPIPE_OK) {
        printf("Album Information\n");
        printf("==================\n\n");
        if (meta.album_title)
            printf("  Title:         %s\n", meta.album_title);
        if (meta.album_artist)
            printf("  Artist:        %s\n", meta.album_artist);
        if (meta.year > 0)
            printf("  Year:          %d\n", meta.year);
        if (meta.genre)
            printf("  Genre:         %s\n", meta.genre);
        if (meta.catalog_number)
            printf("  Catalog:       %s\n", meta.catalog_number);
        if (meta.disc_total > 1)
            printf("  Disc:          %d of %d\n", meta.disc_number, meta.disc_total);
        printf("\n");
        dsdpipe_metadata_free(&meta);
    }

    /* Audio format */
    if (dsdpipe_get_source_format(pipe, &format) == DSDPIPE_OK) {
        printf("Audio Format\n");
        printf("============\n\n");
        printf("  Type:          %s\n",
               dsdpipe_get_frame_format_string(&format));
        printf("  Sample Rate:   %u Hz (DSD%d)\n",
               format.sample_rate,
               format.sample_rate / 44100);
        printf("  Channels:      %s (%d ch)\n",
               dsdpipe_get_speaker_config_string(&format),
               format.channel_count);
        printf("\n");
    }

    /* Track listing */
    if (dsdpipe_get_track_count(pipe, &track_count) == DSDPIPE_OK) {
        printf("Tracks: %d\n", track_count);
        printf("==========\n\n");

        if (verbose || track_count > 0) {
            for (uint8_t i = 1; i <= track_count; i++) {
                dsdpipe_metadata_init(&meta);
                if (dsdpipe_get_track_metadata(pipe, i, &meta) == DSDPIPE_OK) {
                    int minutes = (int)(meta.duration_seconds / 60);
                    int seconds = (int)(meta.duration_seconds) % 60;

                    if (verbose) {
                        printf("  %2d. %s", i,
                               meta.track_title ? meta.track_title : "(untitled)");
                        if (meta.track_performer && meta.track_performer[0])
                            printf(" - %s", meta.track_performer);
                        printf("  [%d:%02d]", minutes, seconds);
                        if (meta.isrc && meta.isrc[0])
                            printf("  ISRC: %s", meta.isrc);
                        printf("\n");
                    } else {
                        printf("  %2d. %-40s %d:%02d\n",
                               i,
                               meta.track_title ? meta.track_title : "(untitled)",
                               minutes, seconds);
                    }

                    dsdpipe_metadata_free(&meta);
                }
            }
        }
    }
}

/* ==========================================================================
 * JSON output
 * ========================================================================== */

static void print_info_json(dsdpipe_t *pipe)
{
    dsdpipe_metadata_t meta;
    dsdpipe_format_t format;
    uint8_t track_count = 0;

    printf("{\n");

    /* Album */
    dsdpipe_metadata_init(&meta);
    printf("  \"album\": {\n");
    if (dsdpipe_get_album_metadata(pipe, &meta) == DSDPIPE_OK) {
        json_print_string("title", meta.album_title, true);
        json_print_string("artist", meta.album_artist, true);
        printf("    \"year\": %d,\n", meta.year);
        json_print_string("genre", meta.genre, true);
        json_print_string("catalog_number", meta.catalog_number, true);
        printf("    \"disc_number\": %d,\n", meta.disc_number);
        printf("    \"disc_total\": %d\n", meta.disc_total);
        dsdpipe_metadata_free(&meta);
    }
    printf("  },\n");

    /* Format */
    printf("  \"format\": {\n");
    if (dsdpipe_get_source_format(pipe, &format) == DSDPIPE_OK) {
        json_print_string("type",
                          dsdpipe_get_frame_format_string(&format), true);
        printf("    \"sample_rate\": %u,\n", format.sample_rate);
        printf("    \"channels\": %d,\n", format.channel_count);
        json_print_string("speaker_config",
                          dsdpipe_get_speaker_config_string(&format), false);
    }
    printf("  },\n");

    /* Tracks */
    dsdpipe_get_track_count(pipe, &track_count);
    printf("  \"tracks\": [\n");
    for (uint8_t i = 1; i <= track_count; i++) {
        dsdpipe_metadata_init(&meta);
        printf("    {\n");

        if (dsdpipe_get_track_metadata(pipe, i, &meta) == DSDPIPE_OK) {
            printf("      \"number\": %u,\n", i);
            json_print_string("title", meta.track_title, true);
            json_print_string("performer", meta.track_performer, true);
            printf("      \"duration_seconds\": %.1f,\n", meta.duration_seconds);
            json_print_string("isrc", meta.isrc, false);
            dsdpipe_metadata_free(&meta);
        } else {
            printf("      \"number\": %u\n", i);
        }

        printf("    }%s\n", (i < track_count) ? "," : "");
    }
    printf("  ]\n");

    printf("}\n");
}

/* ==========================================================================
 * Info implementation
 * ========================================================================== */

static int do_info(const info_opts_t *opts)
{
    dsdpipe_t *pipe = NULL;
    cli_input_type_t in_type;
    dsdpipe_channel_type_t channel_type = DSDPIPE_CHANNEL_STEREO;
    int result;

    /* Parse area type */
    if (opts->area) {
        if (sa_strcasecmp(opts->area, "stereo") == 0 ||
            sa_strcasecmp(opts->area, "2ch") == 0) {
            channel_type = DSDPIPE_CHANNEL_STEREO;
        } else if (sa_strcasecmp(opts->area, "multichannel") == 0 ||
                   sa_strcasecmp(opts->area, "multi") == 0 ||
                   sa_strcasecmp(opts->area, "5.1") == 0) {
            channel_type = DSDPIPE_CHANNEL_MULTICHANNEL;
        } else {
            cli_error("Unknown area type: %s (use 'stereo' or 'multichannel')",
                      opts->area);
            return 1;
        }
    }

    /* Create pipeline */
    pipe = dsdpipe_create();
    if (!pipe) {
        cli_error("Failed to create pipeline");
        return 1;
    }

    /* Detect and configure source */
    in_type = cli_detect_input_type(opts->input_path);

    switch (in_type) {
    case CLI_INPUT_SACD:
        result = dsdpipe_set_source_sacd(pipe, opts->input_path, channel_type);
        break;
    case CLI_INPUT_DSF:
        result = dsdpipe_set_source_dsf(pipe, opts->input_path);
        break;
    case CLI_INPUT_DSDIFF:
        result = dsdpipe_set_source_dsdiff(pipe, opts->input_path);
        break;
    default:
        cli_error("Unsupported input type for info: %s",
                  cli_input_type_name(in_type));
        dsdpipe_destroy(pipe);
        return 1;
    }

    if (result != DSDPIPE_OK) {
        cli_error("Failed to open: %s (%s)",
                  opts->input_path, dsdpipe_get_error_message(pipe));
        dsdpipe_destroy(pipe);
        return 1;
    }

    /* Print information */
    if (opts->json_output) {
        print_info_json(pipe);
    } else {
        print_info_text(pipe, opts->verbose);
    }

    dsdpipe_destroy(pipe);
    return 0;
}

/* ==========================================================================
 * Command entry point
 * ========================================================================== */

int cmd_info(int argc, char *argv[])
{
    info_opts_t opts = {
        .input_path = NULL,
        .area = "stereo",
        .json_output = false,
        .verbose = false
    };

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (cli_match_option(arg, "-h", "--help")) {
            print_info_help();
            return 0;
        }

        if (cli_match_option(arg, "-i", "--input")) {
            opts.input_path = cli_get_option_value(argc, argv, &i);
            if (!opts.input_path) {
                cli_error("Missing value for --input");
                return 1;
            }
            continue;
        }

        if (cli_match_option(arg, "-a", "--area")) {
            opts.area = cli_get_option_value(argc, argv, &i);
            if (!opts.area) {
                cli_error("Missing value for --area");
                return 1;
            }
            continue;
        }

        if (strcmp(arg, "--json") == 0) {
            opts.json_output = true;
            continue;
        }

        if (cli_match_option(arg, "-v", "--verbose")) {
            opts.verbose = true;
            cli_set_verbose(true);
            continue;
        }

        /* Unknown option */
        if (cli_is_option(arg)) {
            cli_error("Unknown option: %s", arg);
            return 1;
        }

        /* Positional argument */
        if (!opts.input_path) {
            opts.input_path = arg;
        } else {
            cli_error("Unexpected argument: %s", arg);
            return 1;
        }
    }

    if (!opts.input_path) {
        cli_error("Input path is required (-i/--input)");
        fprintf(stderr, "Run 'dsdctl info --help' for usage.\n");
        return 1;
    }

    return do_info(&opts);
}
