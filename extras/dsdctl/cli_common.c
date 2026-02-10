/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Shared CLI utilities implementation for dsdctl
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


/* Platform headers first */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "cli_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>

#include <libsautil/time.h>
#include <libsautil/sastring.h>
#include <libsautil/sa_path.h>

/* ==========================================================================
 * Global state
 * ========================================================================== */

static bool g_verbose = false;
static volatile int g_interrupted = 0;
static dsdpipe_t *g_cancel_pipe = NULL;

/* ==========================================================================
 * Console initialization
 * ========================================================================== */

void cli_init_console(void)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    /* Enable ANSI escape codes */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
        }
    }
#endif
}

/* ==========================================================================
 * Signal handling
 * ========================================================================== */

static void signal_handler(int sig)
{
    (void)sig;
    g_interrupted = 1;
    if (g_cancel_pipe) {
        dsdpipe_cancel(g_cancel_pipe);
    }
}

void cli_install_signal_handler(void)
{
    signal(SIGINT, signal_handler);
#ifndef _WIN32
    signal(SIGTERM, signal_handler);
#endif
}

int cli_is_interrupted(void)
{
    return g_interrupted;
}

void cli_set_pipe_for_cancel(dsdpipe_t *pipe)
{
    g_cancel_pipe = pipe;
}

/* ==========================================================================
 * Output format handling
 * ========================================================================== */

uint32_t cli_parse_format(const char *format)
{
    if (!format) {
        return CLI_FORMAT_NONE;
    }

    if (sa_strcasecmp(format, "dsf") == 0) {
        return CLI_FORMAT_DSF;
    }
    if (sa_strcasecmp(format, "dsdiff") == 0 || sa_strcasecmp(format, "dff") == 0) {
        return CLI_FORMAT_DSDIFF;
    }
    if (sa_strcasecmp(format, "em") == 0 ||
        sa_strcasecmp(format, "edit-master") == 0 ||
        sa_strcasecmp(format, "editmaster") == 0 ||
        sa_strcasecmp(format, "master") == 0) {
        return CLI_FORMAT_DSDIFF_EM;
    }
    if (sa_strcasecmp(format, "wav") == 0 || sa_strcasecmp(format, "wave") == 0) {
        return CLI_FORMAT_WAV;
    }
    if (sa_strcasecmp(format, "flac") == 0) {
        return CLI_FORMAT_FLAC;
    }
    if (sa_strcasecmp(format, "xml") == 0) {
        return CLI_FORMAT_XML;
    }
    if (sa_strcasecmp(format, "cue") == 0 || sa_strcasecmp(format, "cuesheet") == 0) {
        return CLI_FORMAT_CUE;
    }
    if (sa_strcasecmp(format, "print") == 0 || sa_strcasecmp(format, "text") == 0 ||
        sa_strcasecmp(format, "txt") == 0) {
        return CLI_FORMAT_PRINT;
    }

    return CLI_FORMAT_NONE;
}

const char *cli_format_extension(uint32_t format)
{
    switch (format) {
    case CLI_FORMAT_DSF:       return ".dsf";
    case CLI_FORMAT_DSDIFF:    return ".dff";
    case CLI_FORMAT_DSDIFF_EM: return ".dff";
    case CLI_FORMAT_WAV:       return ".wav";
    case CLI_FORMAT_FLAC:      return ".flac";
    case CLI_FORMAT_XML:       return ".xml";
    case CLI_FORMAT_CUE:       return ".cue";
    case CLI_FORMAT_PRINT:     return ".txt";
    default:                   return "";
    }
}

const char *cli_format_name(uint32_t format)
{
    switch (format) {
    case CLI_FORMAT_DSF:       return "DSF";
    case CLI_FORMAT_DSDIFF:    return "DSDIFF";
    case CLI_FORMAT_DSDIFF_EM: return "DSDIFF Edit Master";
    case CLI_FORMAT_WAV:       return "WAV";
    case CLI_FORMAT_FLAC:      return "FLAC";
    case CLI_FORMAT_XML:       return "XML Metadata";
    case CLI_FORMAT_CUE:       return "CUE Sheet";
    case CLI_FORMAT_PRINT:     return "Text Metadata";
    default:                   return "Unknown";
    }
}

int cli_count_formats(uint32_t formats)
{
    int count = 0;
    while (formats) {
        count += (int)(formats & 1);
        formats >>= 1;
    }
    return count;
}

/* ==========================================================================
 * PCM quality
 * ========================================================================== */

int cli_parse_pcm_quality(const char *str, dsdpipe_pcm_quality_t *quality)
{
    if (sa_strcasecmp(str, "fast") == 0) {
        *quality = DSDPIPE_PCM_QUALITY_FAST;
        return 0;
    }
    if (sa_strcasecmp(str, "normal") == 0 || sa_strcasecmp(str, "standard") == 0) {
        *quality = DSDPIPE_PCM_QUALITY_NORMAL;
        return 0;
    }
    if (sa_strcasecmp(str, "high") == 0 || sa_strcasecmp(str, "best") == 0) {
        *quality = DSDPIPE_PCM_QUALITY_HIGH;
        return 0;
    }
    return -1;
}

const char *cli_pcm_quality_name(dsdpipe_pcm_quality_t quality)
{
    switch (quality) {
    case DSDPIPE_PCM_QUALITY_FAST:   return "fast";
    case DSDPIPE_PCM_QUALITY_NORMAL: return "normal";
    case DSDPIPE_PCM_QUALITY_HIGH:   return "high";
    default:                          return "unknown";
    }
}

/* ==========================================================================
 * Track filename format
 * ========================================================================== */

int cli_parse_track_format(const char *str, dsdpipe_track_format_t *format)
{
    if (sa_strcasecmp(str, "number") == 0 || sa_strcasecmp(str, "num") == 0) {
        *format = DSDPIPE_TRACK_NUM_ONLY;
        return 0;
    }
    if (sa_strcasecmp(str, "title") == 0 || sa_strcasecmp(str, "num-title") == 0) {
        *format = DSDPIPE_TRACK_NUM_TITLE;
        return 0;
    }
    if (sa_strcasecmp(str, "artist") == 0 || sa_strcasecmp(str, "num-artist-title") == 0) {
        *format = DSDPIPE_TRACK_NUM_ARTIST_TITLE;
        return 0;
    }
    return -1;
}

const char *cli_track_format_name(dsdpipe_track_format_t format)
{
    switch (format) {
    case DSDPIPE_TRACK_NUM_ONLY:         return "number";
    case DSDPIPE_TRACK_NUM_TITLE:        return "title";
    case DSDPIPE_TRACK_NUM_ARTIST_TITLE: return "artist";
    default:                              return "unknown";
    }
}

/* ==========================================================================
 * Input source detection
 * ========================================================================== */

cli_input_type_t cli_detect_input_type(const char *path)
{
    if (!path) {
        return CLI_INPUT_SACD;
    }

    /* Check for network address pattern: host:port
     * Look for a colon followed by digits (but not a Windows drive letter) */
    const char *colon = strchr(path, ':');
    if (colon && colon != path) {
        /* Looks like host:port */
        const char *p = colon + 1;
        bool all_digits = (*p != '\0');
        while (*p) {
            if (*p < '0' || *p > '9') {
                all_digits = false;
                break;
            }
            p++;
        }
        if (all_digits) {
            return CLI_INPUT_NETWORK;
        }
    }

    /* Detect file type from extension */
    size_t len = strlen(path);
    if (len < 4) {
        return CLI_INPUT_SACD;
    }

    const char *ext = strrchr(path, '.');
    if (!ext) {
        return CLI_INPUT_SACD;
    }

    if (sa_strcasecmp(ext, ".dsf") == 0) {
        return CLI_INPUT_DSF;
    }
    if (sa_strcasecmp(ext, ".dff") == 0 || sa_strcasecmp(ext, ".dsdiff") == 0) {
        return CLI_INPUT_DSDIFF;
    }

    /* Default to SACD for .iso and anything else */
    return CLI_INPUT_SACD;
}

const char *cli_input_type_name(cli_input_type_t type)
{
    switch (type) {
    case CLI_INPUT_SACD:       return "SACD ISO";
    case CLI_INPUT_DSF:        return "DSF";
    case CLI_INPUT_DSDIFF:     return "DSDIFF";
    case CLI_INPUT_PS3_DEVICE: return "PS3 Drive";
    case CLI_INPUT_NETWORK:    return "Network";
    default:                   return "Unknown";
    }
}

/* ==========================================================================
 * Progress display
 * ========================================================================== */

int cli_progress_callback(const dsdpipe_progress_t *progress, void *userdata)
{
    cli_progress_ctx_t *ctx = (cli_progress_ctx_t *)userdata;
    uint64_t now_ms = (uint64_t)(sa_gettime_relative() / 1000);
    double speed_elapsed_sec;
    uint64_t display_elapsed_ms;

    if (g_interrupted) {
        return 1;  /* Cancel */
    }

    /* Update bytes written */
    ctx->bytes_written = progress->bytes_written;

    /* Recalculate speed every 500ms to smooth the value */
    speed_elapsed_sec = (double)(now_ms - ctx->last_speed_time_ms) / 1000.0;
    if (speed_elapsed_sec >= 0.5) {
        uint64_t bytes_delta = ctx->bytes_written - ctx->last_bytes_written;
        ctx->current_speed_mbs = (double)bytes_delta / (1024.0 * 1024.0) / speed_elapsed_sec;
        ctx->last_bytes_written = ctx->bytes_written;
        ctx->last_speed_time_ms = now_ms;
    }

    /* Refresh display every 250ms */
    display_elapsed_ms = now_ms - ctx->last_display_time_ms;
    if (display_elapsed_ms < 250) {
        return 0;
    }
    ctx->last_display_time_ms = now_ms;

    if (ctx->verbose) {
        printf("\r[%d/%d] Track %d: %.1f%% @ %.2f MB/s - %-40s",
               progress->track_number,
               progress->track_total,
               progress->track_number,
               progress->track_percent,
               ctx->current_speed_mbs,
               progress->track_title ? progress->track_title : "");
    } else {
        printf("\rProgress: %3d%% @ %.2f MB/s",
               (int)progress->total_percent, ctx->current_speed_mbs);
    }
    fflush(stdout);

    return 0;  /* Continue */
}

void cli_progress_clear(void)
{
    printf("\r%80s\r", "");
    fflush(stdout);
}

/* ==========================================================================
 * Timing and statistics
 * ========================================================================== */

void cli_print_statistics(uint64_t start_ms, uint64_t end_ms,
                          uint64_t bytes_written)
{
    double elapsed_secs = (double)(end_ms - start_ms) / 1000.0;
    double total_mb = (double)bytes_written / (1024.0 * 1024.0);
    double avg_speed_mbs = (elapsed_secs > 0.0) ? (total_mb / elapsed_secs) : 0.0;

    int hours = (int)(elapsed_secs / 3600);
    int minutes = (int)((elapsed_secs - hours * 3600) / 60);
    double seconds = elapsed_secs - hours * 3600 - minutes * 60;

    printf("\nStatistics:\n");
    printf("-----------\n");
    if (hours > 0) {
        printf("  Elapsed time:  %d:%02d:%05.2f\n", hours, minutes, seconds);
    } else if (minutes > 0) {
        printf("  Elapsed time:  %d:%05.2f\n", minutes, seconds);
    } else {
        printf("  Elapsed time:  %.2f seconds\n", seconds);
    }
    printf("  Data written:  %.2f MB\n", total_mb);
    printf("  Average speed: %.2f MB/s\n", avg_speed_mbs);
    printf("\n");
}

/* ==========================================================================
 * Error handling and logging
 * ========================================================================== */

void cli_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void cli_warning(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "Warning: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void cli_info(const char *fmt, ...)
{
    if (!g_verbose) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

void cli_set_verbose(bool verbose)
{
    g_verbose = verbose;
}

bool cli_is_verbose(void)
{
    return g_verbose;
}

/* ==========================================================================
 * Option parsing helpers
 * ========================================================================== */

bool cli_is_option(const char *arg)
{
    return arg && arg[0] == '-';
}

bool cli_match_option(const char *arg, const char *short_opt, const char *long_opt)
{
    if (!arg) {
        return false;
    }
    if (short_opt && strcmp(arg, short_opt) == 0) {
        return true;
    }
    if (long_opt && strcmp(arg, long_opt) == 0) {
        return true;
    }
    return false;
}

const char *cli_get_option_value(int argc, char *argv[], int *idx)
{
    int i = *idx;
    if (i + 1 >= argc) {
        return NULL;
    }
    const char *next = argv[i + 1];
    if (cli_is_option(next)) {
        return NULL;
    }
    (*idx)++;
    return next;
}
