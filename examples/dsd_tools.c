/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSD Tools - Command-line DSD audio converter using libdsdpipe
 * Supported Input Formats:
 * - SACD ISO images (.iso)
 * - DSF files (.dsf)
 * - DSDIFF files (.dff, .dsdiff)
 * Features:
 * - Convert DSD audio to DSF, DSDIFF, WAV, or FLAC output
 * - Multiple simultaneous output formats (e.g., --dsf --wav --flac)
 * - Extract tracks from SACD ISO images
 * - DSDIFF Edit Master output (single file with track markers)
 * - WAV output with DSD-to-PCM conversion (16/24/32-bit)
 * - FLAC output with DSD-to-PCM conversion (16/24-bit, configurable compression)
 * - Automatic DST decompression (or passthrough for DSDIFF)
 * - ID3v2 metadata tagging (DSF)
 * - Track selection (individual, ranges, or all)
 * - Progress reporting
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

/* Standard C headers */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

/* Library headers */
#include <libdsdpipe/dsdpipe.h>
#include <libsautil/mem.h>
#include <libsautil/sa_path.h>
#include <libsautil/sastring.h>
#include <libsautil/time.h>
#include <getopt.h>

/*============================================================================
 * Input Source Types
 *============================================================================*/

typedef enum {
    INPUT_SOURCE_SACD = 0,    /* SACD ISO image */
    INPUT_SOURCE_DSF,         /* DSF file */
    INPUT_SOURCE_DSDIFF       /* DSDIFF file */
} input_source_t;

/*============================================================================
 * Output Format Types (bitmask for multiple simultaneous sinks)
 *============================================================================*/

typedef enum {
    OUTPUT_FORMAT_DSF       = (1 << 0),   /* DSF file(s) */
    OUTPUT_FORMAT_DSDIFF    = (1 << 1),   /* DSDIFF file(s) */
    OUTPUT_FORMAT_DSDIFF_EM = (1 << 2),   /* DSDIFF Edit Master (single file) */
    OUTPUT_FORMAT_WAV       = (1 << 3),   /* WAV (PCM conversion) */
    OUTPUT_FORMAT_FLAC      = (1 << 4),   /* FLAC (PCM conversion) */
    OUTPUT_FORMAT_XML       = (1 << 5),   /* XML metadata export */
    OUTPUT_FORMAT_CUE       = (1 << 6),   /* CUE sheet */
    OUTPUT_FORMAT_PRINT     = (1 << 7)    /* Human-readable text output */
} output_format_t;

/* Combination of all DSD output formats */
#define OUTPUT_FORMAT_DSD_MASK (OUTPUT_FORMAT_DSF | OUTPUT_FORMAT_DSDIFF | OUTPUT_FORMAT_DSDIFF_EM)
/* Combination of all PCM output formats */
#define OUTPUT_FORMAT_PCM_MASK (OUTPUT_FORMAT_WAV | OUTPUT_FORMAT_FLAC)
/* Combination of all metadata-only output formats */
#define OUTPUT_FORMAT_META_MASK (OUTPUT_FORMAT_XML | OUTPUT_FORMAT_CUE | OUTPUT_FORMAT_PRINT)
/* Combination of all audio output formats */
#define OUTPUT_FORMAT_AUDIO_MASK (OUTPUT_FORMAT_DSD_MASK | OUTPUT_FORMAT_PCM_MASK)

/*============================================================================
 * Global State
 *============================================================================*/

static dsdpipe_t *g_pipe = NULL;
static volatile int g_interrupted = 0;

/*============================================================================
 * Timing and Statistics
 *============================================================================*/

/* Get current time in milliseconds */
static uint64_t get_time_ms(void)
{
    return (uint64_t)(sa_gettime_relative() / 1000);
}

typedef struct {
    int verbose;
    uint64_t start_time_ms;
    uint64_t bytes_written;
    uint64_t last_bytes_written;
    uint64_t last_speed_time_ms;     /* Last time speed was recalculated */
    uint64_t last_display_time_ms;   /* Last time display was refreshed */
    double current_speed_mbs;        /* Current speed in MB/s */
} progress_context_t;

/*============================================================================
 * Signal Handler
 *============================================================================*/

static void signal_handler(int sig)
{
    (void)sig;
    g_interrupted = 1;
    if (g_pipe) {
        dsdpipe_cancel(g_pipe);
    }
}

/*============================================================================
 * Progress Callback
 *============================================================================*/

static int progress_callback(const dsdpipe_progress_t *progress, void *userdata)
{
    progress_context_t *ctx = (progress_context_t *)userdata;
    uint64_t now_ms = get_time_ms();
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

/*============================================================================
 * Usage
 *============================================================================*/

static void print_usage(const char *prog_name)
{
    printf("DSD Tools - DSD Audio Converter\n");
    printf("================================\n\n");
    printf("Usage: %s [options] <input> <output_dir>\n\n", prog_name);
    printf("Supported Input Formats:\n");
    printf("  - SACD ISO images (.iso)\n");
    printf("  - DSF files (.dsf)\n");
    printf("  - DSDIFF files (.dff, .dsdiff)\n\n");
    printf("Output Format Options (can specify multiple for simultaneous output):\n");
    printf("  -f, --format <fmt>      Add output format (can be repeated)\n");
    printf("                          Formats: dsf, dsdiff, dff, em, wav, flac, xml, cue, print\n");
    printf("  --dsf                   Output as DSF files (shortcut for -f dsf)\n");
    printf("  --dsdiff, --dff         Output as DSDIFF files (shortcut for -f dsdiff)\n");
    printf("  --edit-master, --em     Output as single DSDIFF Edit Master file\n");
    printf("  --wav                   Output as WAV files (DSD-to-PCM conversion)\n");
    printf("  --flac                  Output as FLAC files (DSD-to-PCM conversion)\n");
    printf("\n");
    printf("Metadata Export Options (companion files for audio output):\n");
    printf("  --xml                   Export metadata as XML file\n");
    printf("  --cue, --cuesheet       Generate CUE sheet (for Edit Master companion)\n");
    printf("  --print                 Export metadata as human-readable text file\n");
    printf("\n");
    printf("NOTE: Multiple output formats can be specified. If none specified, defaults to DSF.\n");
    printf("      Example: --dsf --wav outputs both DSF and WAV files simultaneously.\n");
    printf("      Metadata sinks (--xml, --cue, --print) can be combined with audio sinks.\n");
    printf("\n");
    printf("WAV/FLAC Output Options (PCM formats):\n");
    printf("  -b, --bits <depth>      PCM bit depth: 16, 24 for FLAC; 16, 24, 32 for WAV\n");
    printf("                          (default: 24)\n");
    printf("  -r, --rate <Hz>         PCM sample rate (default: auto from DSD rate)\n");
    printf("                          Common rates: 44100, 88200, 176400, 352800\n");
    printf("  -q, --quality <level>   DSD-to-PCM quality: fast, normal, high (default: normal)\n");
    printf("\n");
    printf("FLAC-Specific Options:\n");
    printf("  -c, --compression <0-8> FLAC compression level (default: 5)\n");
    printf("                          0=fastest, 8=best compression\n");
    printf("\n");
    printf("DST Compression Options:\n");
    printf("  --dst                   Keep DST compression in output (DSDIFF only)\n");
    printf("                          DST-compressed SACDs will NOT be decoded\n");
    printf("  --decode-dst            Decode DST to raw DSD (default)\n");
    printf("                          Uses multithreaded DST decoder for speed\n");
    printf("\n");
    printf("Track/Area Selection:\n");
    printf("  -t, --tracks <spec>     Track selection (default: all)\n");
    printf("                          Examples: \"all\", \"1\", \"1-5\", \"1,3,5\", \"1-3,7-9\"\n");
    printf("  -a, --area <type>       Audio area: stereo, multichannel (default: stereo)\n");
    printf("                          Note: Only applies to SACD ISO input\n");
    printf("\n");
    printf("Metadata Options:\n");
    printf("  -i, --id3               Write ID3v2 metadata tags (default: enabled)\n");
    printf("  -n, --no-id3            Disable ID3v2 metadata tags\n");
    printf("\n");
    printf("Output Directory Options:\n");
    printf("  -A, --artist            Include artist in output directory name\n");
    printf("                          Creates: output_dir/Artist - Album Title/\n");
    printf("                          Without: output_dir/Album Title/\n");
    printf("  --track-format <fmt>    Track filename format (default: title)\n");
    printf("                          number: 01, 02, 03...\n");
    printf("                          title:  01 - Track Title\n");
    printf("                          artist: 01 - Artist Name - Track Title\n");
    printf("\n");
    printf("Other Options:\n");
    printf("  -l, --list              List tracks only, don't extract\n");
    printf("  -v, --verbose           Enable verbose output\n");
    printf("  -h, --help              Show this help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  # Extract all tracks from SACD ISO to DSF (default)\n");
    printf("  %s album.iso ./output\n\n", prog_name);
    printf("  # Extract tracks to DSDIFF format\n");
    printf("  %s --dsdiff album.iso ./output\n\n", prog_name);
    printf("  # Create single DSDIFF Edit Master file with track markers\n");
    printf("  %s --edit-master album.iso ./output\n\n", prog_name);
    printf("  # Extract DSDIFF keeping DST compression (no decoding)\n");
    printf("  %s --dsdiff --dst album.iso ./output\n\n", prog_name);
    printf("  # Extract to WAV (24-bit PCM at 88.2kHz)\n");
    printf("  %s --wav -b 24 -r 88200 album.iso ./output\n\n", prog_name);
    printf("  # Extract to WAV with high-quality DSD-to-PCM conversion\n");
    printf("  %s --wav -q high album.iso ./output\n\n", prog_name);
    printf("  # Extract to FLAC (24-bit with default compression)\n");
    printf("  %s --flac album.iso ./output\n\n", prog_name);
    printf("  # Extract to FLAC with best compression\n");
    printf("  %s --flac -c 8 album.iso ./output\n\n", prog_name);
    printf("  # Extract to FLAC at 16-bit with fast compression\n");
    printf("  %s --flac -b 16 -c 0 album.iso ./output\n\n", prog_name);
    printf("  # Extract tracks 1-5 from stereo area to DSF\n");
    printf("  %s -t 1-5 -a stereo album.iso ./output\n\n", prog_name);
    printf("  # Extract specific tracks from multichannel area to DSDIFF\n");
    printf("  %s --dff -t 1,3,5,7 -a multichannel album.iso ./output\n\n", prog_name);
    printf("  # Extract without ID3 tags\n");
    printf("  %s -n album.iso ./output\n\n", prog_name);
    printf("  # Convert DSF file to DSDIFF\n");
    printf("  %s --dsdiff track.dsf ./output\n\n", prog_name);
    printf("  # Convert DSDIFF to WAV (24-bit PCM)\n");
    printf("  %s --wav album.dff ./output\n\n", prog_name);
    printf("  # Convert DSF to FLAC with high quality DSD-to-PCM\n");
    printf("  %s --flac -q high track.dsf ./output\n\n", prog_name);
    printf("\n");
    printf("Multi-Sink Examples (simultaneous output to multiple formats):\n");
    printf("  # Extract to both DSF and WAV simultaneously\n");
    printf("  %s --dsf --wav album.iso ./output\n\n", prog_name);
    printf("  # Extract to DSF, DSDIFF Edit Master, and WAV\n");
    printf("  %s --dsf --edit-master --wav album.iso ./output\n\n", prog_name);
    printf("  # Extract to all DSD formats (DSF + DSDIFF + Edit Master)\n");
    printf("  %s --dsf --dsdiff --em album.iso ./output\n\n", prog_name);
    printf("  # Extract to both WAV and FLAC (different PCM encodings)\n");
    printf("  %s --wav --flac -b 24 -q high album.iso ./output\n\n", prog_name);
    printf("  # Using -f repeatedly for multiple formats\n");
    printf("  %s -f dsf -f wav -f flac album.iso ./output\n\n", prog_name);
    printf("\n");
    printf("Metadata Export Examples:\n");
    printf("  # Print metadata to stdout without extracting audio\n");
    printf("  %s --print album.iso ./output\n\n", prog_name);
    printf("  # Create Edit Master with CUE sheet and XML metadata\n");
    printf("  %s --edit-master --cue --xml album.iso ./output\n\n", prog_name);
    printf("  # Export metadata as XML alongside DSF files\n");
    printf("  %s --dsf --xml album.iso ./output\n\n", prog_name);
}

/*============================================================================
 * Input Source Detection
 *============================================================================*/

/**
 * @brief Detect input source type from file extension
 */
static input_source_t detect_input_source(const char *filename)
{
    const char *ext = NULL;
    size_t len;

    if (!filename) {
        return INPUT_SOURCE_SACD;
    }

    len = strlen(filename);
    if (len < 4) {
        return INPUT_SOURCE_SACD;
    }

    /* Find last dot */
    ext = strrchr(filename, '.');
    if (!ext) {
        return INPUT_SOURCE_SACD;
    }

    /* Check extension */
    if (sa_strcasecmp(ext, ".dsf") == 0) {
        return INPUT_SOURCE_DSF;
    }
    if (sa_strcasecmp(ext, ".dff") == 0 || sa_strcasecmp(ext, ".dsdiff") == 0) {
        return INPUT_SOURCE_DSDIFF;
    }
    if (sa_strcasecmp(ext, ".iso") == 0) {
        return INPUT_SOURCE_SACD;
    }

    /* Default to SACD for unknown extensions */
    return INPUT_SOURCE_SACD;
}

static const char *get_input_source_name(input_source_t source)
{
    switch (source) {
        case INPUT_SOURCE_SACD:   return "SACD ISO";
        case INPUT_SOURCE_DSF:    return "DSF";
        case INPUT_SOURCE_DSDIFF: return "DSDIFF";
        default:                  return "Unknown";
    }
}

/*============================================================================
 * Print Metadata
 *============================================================================*/

static void print_album_info(dsdpipe_t *pipe)
{
    dsdpipe_metadata_t meta;
    dsdpipe_format_t format;
    uint8_t track_count = 0;

    dsdpipe_metadata_init(&meta);

    if (dsdpipe_get_album_metadata(pipe, &meta) == DSDPIPE_OK) {
        printf("\nAlbum Information:\n");
        printf("------------------\n");
        if (meta.album_title) {
            printf("  Title:     %s\n", meta.album_title);
        }
        if (meta.album_artist) {
            printf("  Artist:    %s\n", meta.album_artist);
        }
        if (meta.year > 0) {
            printf("  Year:      %d\n", meta.year);
        }
        if (meta.genre) {
            printf("  Genre:     %s\n", meta.genre);
        }
        if (meta.catalog_number) {
            printf("  Catalog:   %s\n", meta.catalog_number);
        }
        if (meta.disc_total > 1) {
            printf("  Disc:      %d of %d\n", meta.disc_number, meta.disc_total);
        }
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

/*============================================================================
 * Parse Output Format
 *============================================================================*/

/**
 * @brief Parse output format string and return bitmask value
 * @param str Format string (dsf, dsdiff, dff, em, wav, flac, etc.)
 * @return Bitmask value for the format, or 0 if unknown
 */
static uint32_t parse_output_format(const char *str)
{
    if (sa_strcasecmp(str, "dsf") == 0) {
        return OUTPUT_FORMAT_DSF;
    }
    if (sa_strcasecmp(str, "dsdiff") == 0 || sa_strcasecmp(str, "dff") == 0) {
        return OUTPUT_FORMAT_DSDIFF;
    }
    if (sa_strcasecmp(str, "em") == 0 ||
        sa_strcasecmp(str, "edit-master") == 0 ||
        sa_strcasecmp(str, "editmaster") == 0 ||
        sa_strcasecmp(str, "master") == 0) {
        return OUTPUT_FORMAT_DSDIFF_EM;
    }
    if (sa_strcasecmp(str, "wav") == 0 || sa_strcasecmp(str, "wave") == 0) {
        return OUTPUT_FORMAT_WAV;
    }
    if (sa_strcasecmp(str, "flac") == 0) {
        return OUTPUT_FORMAT_FLAC;
    }
    if (sa_strcasecmp(str, "xml") == 0) {
        return OUTPUT_FORMAT_XML;
    }
    if (sa_strcasecmp(str, "cue") == 0 || sa_strcasecmp(str, "cuesheet") == 0) {
        return OUTPUT_FORMAT_CUE;
    }
    if (sa_strcasecmp(str, "print") == 0 || sa_strcasecmp(str, "text") == 0 ||
        sa_strcasecmp(str, "txt") == 0) {
        return OUTPUT_FORMAT_PRINT;
    }
    return 0;  /* Unknown format */
}

/**
 * @brief Count number of bits set in output format mask
 */
static int count_output_formats(uint32_t formats)
{
    int count = 0;
    while (formats) {
        count += (formats & 1);
        formats >>= 1;
    }
    return count;
}

/*============================================================================
 * Parse PCM Quality
 *============================================================================*/

static int parse_pcm_quality(const char *str, dsdpipe_pcm_quality_t *quality)
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
    return -1;  /* Unknown quality */
}

static const char *get_pcm_quality_name(dsdpipe_pcm_quality_t quality)
{
    switch (quality) {
        case DSDPIPE_PCM_QUALITY_FAST:   return "fast";
        case DSDPIPE_PCM_QUALITY_NORMAL: return "normal";
        case DSDPIPE_PCM_QUALITY_HIGH:   return "high";
        default:                          return "unknown";
    }
}

/*============================================================================
 * Parse Track Filename Format
 *============================================================================*/

static int parse_track_format(const char *str, dsdpipe_track_format_t *format)
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
    return -1;  /* Unknown format */
}

static const char *get_track_format_name(dsdpipe_track_format_t format)
{
    switch (format) {
        case DSDPIPE_TRACK_NUM_ONLY:         return "number";
        case DSDPIPE_TRACK_NUM_TITLE:        return "title";
        case DSDPIPE_TRACK_NUM_ARTIST_TITLE: return "artist";
        default:                              return "unknown";
    }
}

/*============================================================================
 * Main
 *============================================================================*/

/*============================================================================
 * Long Option Definitions for getopt_long
 *============================================================================*/

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
    OPT_TRACK_FORMAT
};

static struct option long_options[] = {
    /* Output formats (can be combined) */
    {"dsf",         no_argument,       NULL, OPT_DSF},
    {"dsdiff",      no_argument,       NULL, OPT_DSDIFF},
    {"dff",         no_argument,       NULL, OPT_DFF},
    {"edit-master", no_argument,       NULL, OPT_EDIT_MASTER},
    {"em",          no_argument,       NULL, OPT_EM},
    {"wav",         no_argument,       NULL, OPT_WAV},
    {"flac",        no_argument,       NULL, OPT_FLAC},
    /* Metadata output formats */
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
    /* Other options */
    {"list",        no_argument,       NULL, 'l'},
    {"verbose",     no_argument,       NULL, 'v'},
    {"help",        no_argument,       NULL, 'h'},
    {NULL,          0,                 NULL, 0}
};

int main(int argc, char *argv[])
{
    const char *input = NULL;
    const char *output = NULL;
    const char *track_spec = "all";
    const char *area = "stereo";
    uint32_t out_formats = 0;  /* Bitmask for multiple output formats */
    input_source_t in_source = INPUT_SOURCE_SACD;
    int write_id3 = 1;
    int write_dst = 0;  /* Decode DST by default */
    int verbose = 0;
    int show_help = 0;
    int list_only = 0;
    int artist_flag = 1;  /* Include artist name in output directory/filenames */
    dsdpipe_track_format_t track_format = DSDPIPE_TRACK_NUM_ARTIST_TITLE;
    dsdpipe_channel_type_t channel_type;
    int result;
    int opt;
    uint32_t fmt;

    /* PCM output options (WAV/FLAC) */
    int pcm_bit_depth = 24;                                    /* Default: 24-bit */
    int pcm_sample_rate = 0;                                   /* Default: auto from DSD rate */
    dsdpipe_pcm_quality_t pcm_quality = DSDPIPE_PCM_QUALITY_NORMAL;

    /* FLAC-specific options */
    int flac_compression = 5;                                  /* Default: level 5 */

    /* Parse command-line arguments using getopt_long */
    while ((opt = getopt_long(argc, argv, "f:b:r:q:c:t:a:inlvhA", long_options, NULL)) != -1) {
        switch (opt) {
            /* Output format flags (accumulate in bitmask) */
            case OPT_DSF:
                out_formats |= OUTPUT_FORMAT_DSF;
                break;
            case OPT_DSDIFF:
            case OPT_DFF:
                out_formats |= OUTPUT_FORMAT_DSDIFF;
                break;
            case OPT_EDIT_MASTER:
            case OPT_EM:
                out_formats |= OUTPUT_FORMAT_DSDIFF_EM;
                break;
            case OPT_WAV:
                out_formats |= OUTPUT_FORMAT_WAV;
                break;
            case OPT_FLAC:
                out_formats |= OUTPUT_FORMAT_FLAC;
                break;

            /* Metadata output formats */
            case OPT_XML:
                out_formats |= OUTPUT_FORMAT_XML;
                break;
            case OPT_CUE:
            case OPT_CUESHEET:
                out_formats |= OUTPUT_FORMAT_CUE;
                break;
            case OPT_PRINT:
                out_formats |= OUTPUT_FORMAT_PRINT;
                break;

            /* Format specification via -f/--format (can be repeated) */
            case 'f':
                fmt = parse_output_format(optarg);
                if (fmt == 0) {
                    fprintf(stderr, "Error: Unknown output format: %s\n", optarg);
                    fprintf(stderr, "       Use: dsf, dsdiff, dff, em, edit-master, wav, or flac\n");
                    return 1;
                }
                out_formats |= fmt;
                break;

            /* PCM output options */
            case 'b':
                pcm_bit_depth = atoi(optarg);
                if (pcm_bit_depth != 16 && pcm_bit_depth != 24 && pcm_bit_depth != 32) {
                    fprintf(stderr, "Error: Invalid bit depth: %d\n", pcm_bit_depth);
                    fprintf(stderr, "       Use: 16, 24, or 32 (FLAC only supports 16 or 24)\n");
                    return 1;
                }
                break;
            case 'r':
                pcm_sample_rate = atoi(optarg);
                if (pcm_sample_rate < 0) {
                    fprintf(stderr, "Error: Invalid sample rate: %s\n", optarg);
                    return 1;
                }
                break;
            case 'q':
                if (parse_pcm_quality(optarg, &pcm_quality) != 0) {
                    fprintf(stderr, "Error: Unknown quality level: %s\n", optarg);
                    fprintf(stderr, "       Use: fast, normal, or high\n");
                    return 1;
                }
                break;

            /* FLAC compression */
            case 'c':
                flac_compression = atoi(optarg);
                if (flac_compression < 0 || flac_compression > 8) {
                    fprintf(stderr, "Error: Invalid FLAC compression level: %d\n", flac_compression);
                    fprintf(stderr, "       Use: 0-8 (0=fastest, 8=best compression)\n");
                    return 1;
                }
                break;

            /* DST options */
            case OPT_DST:
                write_dst = 1;
                break;
            case OPT_DECODE_DST:
                write_dst = 0;
                break;

            /* Track/area selection */
            case 't':
                track_spec = optarg;
                break;
            case 'a':
                area = optarg;
                break;

            /* Metadata options */
            case 'i':
            case OPT_ID3:
                write_id3 = 1;
                break;
            case 'n':
            case OPT_NO_ID3:
                write_id3 = 0;
                break;

            /* Output directory options */
            case 'A':
                artist_flag = 1;
                break;
            case OPT_TRACK_FORMAT:
                if (parse_track_format(optarg, &track_format) != 0) {
                    fprintf(stderr, "Error: Unknown track format: %s\n", optarg);
                    fprintf(stderr, "       Use: number, title, or artist\n");
                    return 1;
                }
                break;

            /* Other options */
            case 'l':
                list_only = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'h':
                show_help = 1;
                break;

            default:
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
        }
    }

    /* Get positional arguments (input and output) */
    for (int i = optind; i < argc; i++) {
        if (!input) {
            input = argv[i];
        } else if (!output) {
            output = argv[i];
        }
    }

    /* Default to DSF if no format specified */
    if (out_formats == 0) {
        out_formats = OUTPUT_FORMAT_DSF;
    }

    /* Show help */
    if (show_help) {
        print_usage(argv[0]);
        return 0;
    }

    /* Validate input */
    if (!input) {
        fprintf(stderr, "Error: No input file specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Output is required unless listing only */
    if (!output && !list_only) {
        fprintf(stderr, "Error: No output directory specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Detect input source type */
    in_source = detect_input_source(input);

    /* Parse area type (only relevant for SACD) */
    if (sa_strcasecmp(area, "stereo") == 0 || sa_strcasecmp(area, "2ch") == 0) {
        channel_type = DSDPIPE_CHANNEL_STEREO;
    } else if (sa_strcasecmp(area, "multichannel") == 0 ||
               sa_strcasecmp(area, "multi") == 0 ||
               sa_strcasecmp(area, "5.1") == 0) {
        channel_type = DSDPIPE_CHANNEL_MULTICHANNEL;
    } else {
        fprintf(stderr, "Error: Unknown area type: %s\n", area);
        fprintf(stderr, "       Use 'stereo' or 'multichannel'\n");
        return 1;
    }

    /* Validate options for formats */
    /* DST passthrough only makes sense for DSDIFF output */
    if (write_dst && (out_formats & ~(OUTPUT_FORMAT_DSDIFF | OUTPUT_FORMAT_DSDIFF_EM))) {
        /* Non-DSDIFF formats present - warn about DST passthrough */
        if (out_formats & OUTPUT_FORMAT_DSF) {
            fprintf(stderr, "Warning: DSF format does not support DST passthrough.\n");
            fprintf(stderr, "         DST will be decoded to DSD for DSF output.\n");
        }
        if (out_formats & OUTPUT_FORMAT_PCM_MASK) {
            fprintf(stderr, "Warning: PCM formats (WAV/FLAC) do not support DST passthrough.\n");
            fprintf(stderr, "         DST will be decoded and converted to PCM.\n");
        }
    }

    /* FLAC only supports 16 and 24-bit */
    if ((out_formats & OUTPUT_FORMAT_FLAC) && pcm_bit_depth == 32) {
        fprintf(stderr, "Warning: FLAC does not support 32-bit. Using 24-bit for FLAC.\n");
        /* Note: We don't modify pcm_bit_depth here as WAV may still want 32-bit */
    }

    /* Check FLAC availability */
    if ((out_formats & OUTPUT_FORMAT_FLAC) && !dsdpipe_has_flac_support()) {
        fprintf(stderr, "Error: FLAC support not available (libFLAC not compiled in).\n");
        return 1;
    }

    /* Install signal handler */
    signal(SIGINT, signal_handler);
#ifndef _WIN32
    signal(SIGTERM, signal_handler);
#endif

    /* Create pipeline */
    g_pipe = dsdpipe_create();
    if (!g_pipe) {
        fprintf(stderr, "Error: Failed to create pipeline\n");
        return 1;
    }

    /* Configure source based on input type */
    printf("Opening: %s\n", input);
    printf("Source:  %s\n", get_input_source_name(in_source));

    switch (in_source) {
        case INPUT_SOURCE_SACD:
            printf("Area:    %s\n", area);
            result = dsdpipe_set_source_sacd(g_pipe, input, channel_type);
            if (result != DSDPIPE_OK) {
                fprintf(stderr, "Error: Failed to open SACD: %s\n",
                        dsdpipe_get_error_message(g_pipe));
                dsdpipe_destroy(g_pipe);
                return 1;
            }
            break;

        case INPUT_SOURCE_DSF:
            result = dsdpipe_set_source_dsf(g_pipe, input);
            if (result != DSDPIPE_OK) {
                fprintf(stderr, "Error: Failed to open DSF file: %s\n",
                        dsdpipe_get_error_message(g_pipe));
                dsdpipe_destroy(g_pipe);
                return 1;
            }
            break;

        case INPUT_SOURCE_DSDIFF:
            result = dsdpipe_set_source_dsdiff(g_pipe, input);
            if (result != DSDPIPE_OK) {
                fprintf(stderr, "Error: Failed to open DSDIFF file: %s\n",
                        dsdpipe_get_error_message(g_pipe));
                dsdpipe_destroy(g_pipe);
                return 1;
            }
            break;
    }

    /* Note if area option was specified for non-SACD sources */
    if (in_source != INPUT_SOURCE_SACD && sa_strcasecmp(area, "stereo") != 0) {
        printf("Note:    Area option ignored for %s input\n",
               get_input_source_name(in_source));
    }

    /* Print album information */
    print_album_info(g_pipe);

    /* If list-only mode, print tracks and exit */
    if (list_only) {
        print_track_list(g_pipe);
        dsdpipe_destroy(g_pipe);
        return 0;
    }

    /* Print track list in verbose mode */
    if (verbose) {
        print_track_list(g_pipe);
    }

    /* Select tracks */
    result = dsdpipe_select_tracks_str(g_pipe, track_spec);
    if (result != DSDPIPE_OK) {
        fprintf(stderr, "Error: Invalid track selection: %s\n", track_spec);
        fprintf(stderr, "       %s\n", dsdpipe_get_error_message(g_pipe));
        dsdpipe_destroy(g_pipe);
        return 1;
    }

    /* Print selected tracks */
    {
        uint8_t selected[256];
        size_t count = 0;
        if (dsdpipe_get_selected_tracks(g_pipe, selected, 256, &count) == DSDPIPE_OK) {
            printf("Selected: %zu track(s)", count);
            if (verbose && count <= 20) {
                printf(" [");
                for (size_t i = 0; i < count; i++) {
                    printf("%s%d", i > 0 ? "," : "", selected[i]);
                }
                printf("]");
            }
            printf("\n");
        }
    }

    /* Generate album output directory if metadata is available.
     * Uses sa_unique_path() so that if the directory already exists
     * a new one is created with " (1)", " (2)", etc. suffix. */
    char *album_output_path = NULL;
    {
        dsdpipe_metadata_t album_meta = {0};
        if (dsdpipe_get_album_metadata(g_pipe, &album_meta) == DSDPIPE_OK) {
            dsdpipe_album_format_t dir_format = artist_flag
                ? DSDPIPE_ALBUM_ARTIST_TITLE
                : DSDPIPE_ALBUM_TITLE_ONLY;

            /* Get top-level album directory name (single component) */
            char *album_dir = dsdpipe_get_album_dir(&album_meta, dir_format);
            if (album_dir != NULL) {
                /* Find a unique directory path under the output base */
                album_output_path = sa_unique_path(output, album_dir, NULL);

                /* For multi-disc sets, append "Disc N" subdirectory */
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

    /* Use generated album path or fall back to user-specified output */
    const char *final_output = album_output_path ? album_output_path : output;

    /* Derive album base name for metadata files (XML, CUE, Edit Master)
     * Uses same format as the output directory: "Artist - Album Title" */
    char album_base_name[256] = "album";
    {
        dsdpipe_metadata_t meta_tmp = {0};
        if (dsdpipe_get_album_metadata(g_pipe, &meta_tmp) == DSDPIPE_OK) {
            dsdpipe_album_format_t name_format = artist_flag
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
    if (album_output_path) {
        printf("         (auto-generated from album metadata)\n");
    }
    {
        int format_count = count_output_formats(out_formats);
        printf("Formats: %d output%s\n", format_count, format_count > 1 ? "s" : "");
    }

    /* Set PCM conversion quality if any PCM format is requested */
    if (out_formats & OUTPUT_FORMAT_PCM_MASK) {
        dsdpipe_set_pcm_quality(g_pipe, pcm_quality);
    }

    /* Set track filename format */
    dsdpipe_set_track_filename_format(g_pipe, track_format);
    if (verbose) {
        printf("Track naming: %s\n", get_track_format_name(track_format));
    }

    /* Configure sinks based on output formats (can add multiple) */
    result = DSDPIPE_OK;
    int sink_count = 0;

    /* Add text/print metadata sink FIRST if requested (shows metadata before extraction) */
    /* Print sink outputs to stdout for immediate visibility */
    if ((out_formats & OUTPUT_FORMAT_PRINT) && result == DSDPIPE_OK) {
        printf("\n[Sink %d] Text Metadata\n", ++sink_count);
        printf("  Output: stdout\n");
        result = dsdpipe_add_sink_print(g_pipe, NULL);
        if (result != DSDPIPE_OK) {
            fprintf(stderr, "Error: Failed to configure text output: %s\n",
                    dsdpipe_get_error_message(g_pipe));
        }
    }

    /* Add DSF sink if requested */
    if ((out_formats & OUTPUT_FORMAT_DSF) && result == DSDPIPE_OK) {
        printf("\n[Sink %d] DSF\n", ++sink_count);
        printf("  ID3: %s\n", write_id3 ? "enabled" : "disabled");
        result = dsdpipe_add_sink_dsf(g_pipe, final_output, write_id3);
        if (result != DSDPIPE_OK) {
            fprintf(stderr, "Error: Failed to configure DSF output: %s\n",
                    dsdpipe_get_error_message(g_pipe));
        }
    }

    /* Add DSDIFF sink if requested */
    if ((out_formats & OUTPUT_FORMAT_DSDIFF) && result == DSDPIPE_OK) {
        printf("\n[Sink %d] DSDIFF\n", ++sink_count);
        printf("  DST: %s\n", write_dst ? "passthrough" : "decode to DSD");
        printf("  ID3: %s\n", write_id3 ? "enabled" : "disabled");
        result = dsdpipe_add_sink_dsdiff(g_pipe, final_output, write_dst, false, write_id3);
        if (result != DSDPIPE_OK) {
            fprintf(stderr, "Error: Failed to configure DSDIFF output: %s\n",
                    dsdpipe_get_error_message(g_pipe));
        }
    }

    /* Add DSDIFF Edit Master sink if requested */
    if ((out_formats & OUTPUT_FORMAT_DSDIFF_EM) && result == DSDPIPE_OK) {
        printf("\n[Sink %d] DSDIFF Edit Master\n", ++sink_count);
        printf("  Mode: Single file with track markers\n");
        printf("  DST:  %s\n", write_dst ? "passthrough" : "decode to DSD");
        printf("  ID3:  %s\n", write_id3 ? "enabled" : "disabled");
        result = dsdpipe_add_sink_dsdiff(g_pipe, final_output, write_dst, true, write_id3);
        if (result != DSDPIPE_OK) {
            fprintf(stderr, "Error: Failed to configure DSDIFF Edit Master output: %s\n",
                    dsdpipe_get_error_message(g_pipe));
        }
    }

    /* Add WAV sink if requested */
    if ((out_formats & OUTPUT_FORMAT_WAV) && result == DSDPIPE_OK) {
        printf("\n[Sink %d] WAV\n", ++sink_count);
        printf("  Bits:    %d-bit\n", pcm_bit_depth);
        if (pcm_sample_rate > 0) {
            printf("  Rate:    %d Hz\n", pcm_sample_rate);
        } else {
            printf("  Rate:    auto (from DSD rate)\n");
        }
        printf("  Quality: %s\n", get_pcm_quality_name(pcm_quality));
        result = dsdpipe_add_sink_wav(g_pipe, final_output, pcm_bit_depth, pcm_sample_rate);
        if (result != DSDPIPE_OK) {
            fprintf(stderr, "Error: Failed to configure WAV output: %s\n",
                    dsdpipe_get_error_message(g_pipe));
        }
    }

    /* Add FLAC sink if requested */
    if ((out_formats & OUTPUT_FORMAT_FLAC) && result == DSDPIPE_OK) {
        /* FLAC only supports 16 and 24-bit */
        int flac_bit_depth = (pcm_bit_depth == 32) ? 24 : pcm_bit_depth;
        printf("\n[Sink %d] FLAC\n", ++sink_count);
        printf("  Bits:        %d-bit%s\n", flac_bit_depth,
               (pcm_bit_depth == 32) ? " (32-bit not supported)" : "");
        if (pcm_sample_rate > 0) {
            printf("  Rate:        %d Hz\n", pcm_sample_rate);
        } else {
            printf("  Rate:        auto (from DSD rate)\n");
        }
        printf("  Quality:     %s\n", get_pcm_quality_name(pcm_quality));
        printf("  Compression: %d\n", flac_compression);
        result = dsdpipe_add_sink_flac(g_pipe, final_output, flac_bit_depth, flac_compression);
        if (result != DSDPIPE_OK) {
            fprintf(stderr, "Error: Failed to configure FLAC output: %s\n",
                    dsdpipe_get_error_message(g_pipe));
        }
    }

    /* Add XML metadata sink if requested */
    if ((out_formats & OUTPUT_FORMAT_XML) && result == DSDPIPE_OK) {
        char *xml_path = sa_make_path(final_output, NULL, album_base_name, "xml");
        if (!xml_path) {
            fprintf(stderr, "Error: Failed to build XML output path\n");
            result = DSDPIPE_ERROR_OUT_OF_MEMORY;
        } else {
            printf("\n[Sink %d] XML Metadata\n", ++sink_count);
            printf("  File: %s\n", xml_path);
            result = dsdpipe_add_sink_xml(g_pipe, xml_path);
            if (result != DSDPIPE_OK) {
                fprintf(stderr, "Error: Failed to configure XML output: %s\n",
                        dsdpipe_get_error_message(g_pipe));
            }
            sa_free(xml_path);
        }
    }

    /* Add CUE sheet sink if requested */
    if ((out_formats & OUTPUT_FORMAT_CUE) && result == DSDPIPE_OK) {
        /* Build audio file reference using the album base name */
        char audio_ref[280];
        if (out_formats & OUTPUT_FORMAT_DSDIFF_EM) {
            snprintf(audio_ref, sizeof(audio_ref), "%s.dff", album_base_name);
        } else if (out_formats & OUTPUT_FORMAT_DSDIFF) {
            snprintf(audio_ref, sizeof(audio_ref), "%s.dff", album_base_name);
        } else if (out_formats & OUTPUT_FORMAT_DSF) {
            snprintf(audio_ref, sizeof(audio_ref), "%s.dsf", album_base_name);
        } else if (out_formats & OUTPUT_FORMAT_WAV) {
            snprintf(audio_ref, sizeof(audio_ref), "%s.wav", album_base_name);
        } else if (out_formats & OUTPUT_FORMAT_FLAC) {
            snprintf(audio_ref, sizeof(audio_ref), "%s.flac", album_base_name);
        } else {
            snprintf(audio_ref, sizeof(audio_ref), "%s.dff", album_base_name);
        }
        char *cue_path = sa_make_path(final_output, NULL, album_base_name, "cue");
        if (!cue_path) {
            fprintf(stderr, "Error: Failed to build CUE output path\n");
            result = DSDPIPE_ERROR_OUT_OF_MEMORY;
        } else {
            printf("\n[Sink %d] CUE Sheet\n", ++sink_count);
            printf("  File:      %s\n", cue_path);
            printf("  Audio ref: %s\n", audio_ref);
            result = dsdpipe_add_sink_cue(g_pipe, cue_path, audio_ref);
            if (result != DSDPIPE_OK) {
                fprintf(stderr, "Error: Failed to configure CUE sheet output: %s\n",
                        dsdpipe_get_error_message(g_pipe));
            }
            sa_free(cue_path);
        }
    }

    if (result != DSDPIPE_OK) {
        sa_free(album_output_path);
        dsdpipe_destroy(g_pipe);
        return 1;
    }

    /* Preview files to be created */
    {
        uint8_t selected[256];
        size_t sel_count = 0;
        dsdpipe_get_selected_tracks(g_pipe, selected, 256, &sel_count);

        /* Per-track audio formats */
        int has_per_track = out_formats & (OUTPUT_FORMAT_DSF | OUTPUT_FORMAT_DSDIFF |
                                           OUTPUT_FORMAT_WAV | OUTPUT_FORMAT_FLAC);
        if (has_per_track && sel_count > 0) {
            printf("\nFiles:\n");
            for (size_t i = 0; i < sel_count; i++) {
                dsdpipe_metadata_t trk_meta = {0};
                dsdpipe_get_track_metadata(g_pipe, selected[i], &trk_meta);
                char *trk_name = dsdpipe_get_track_filename(&trk_meta, track_format);
                if (!trk_name) {
                    trk_name = sa_asprintf("%02u - Track %u",
                                           selected[i], selected[i]);
                }

                if (out_formats & OUTPUT_FORMAT_DSF)
                    printf("  %s.dsf\n", trk_name);
                if (out_formats & OUTPUT_FORMAT_DSDIFF)
                    printf("  %s.dff\n", trk_name);
                if (out_formats & OUTPUT_FORMAT_WAV)
                    printf("  %s.wav\n", trk_name);
                if (out_formats & OUTPUT_FORMAT_FLAC)
                    printf("  %s.flac\n", trk_name);

                sa_free(trk_name);
                dsdpipe_metadata_free(&trk_meta);
            }
        }

        /* Non-track files (using album base name) */
        if (out_formats & OUTPUT_FORMAT_DSDIFF_EM)
            printf("  %s.dff  [Edit Master]\n", album_base_name);
        if (out_formats & OUTPUT_FORMAT_XML)
            printf("  %s.xml\n", album_base_name);
        if (out_formats & OUTPUT_FORMAT_CUE)
            printf("  %s.cue\n", album_base_name);
    }

    /* Set up progress context with timing */
    progress_context_t prog_ctx = {0};
    prog_ctx.verbose = verbose;
    prog_ctx.start_time_ms = get_time_ms();
    prog_ctx.last_speed_time_ms = prog_ctx.start_time_ms;
    prog_ctx.last_display_time_ms = prog_ctx.start_time_ms;
    prog_ctx.bytes_written = 0;
    prog_ctx.last_bytes_written = 0;
    prog_ctx.current_speed_mbs = 0.0;

    /* Set progress callback */
    dsdpipe_set_progress_callback(g_pipe, progress_callback, &prog_ctx);

    /* Check if DST decoding will be performed */
    {
        dsdpipe_format_t src_format;
        if (dsdpipe_get_source_format(g_pipe, &src_format) == DSDPIPE_OK) {
            int needs_pcm = (out_formats & OUTPUT_FORMAT_PCM_MASK) != 0;
            int needs_dsd = (out_formats & OUTPUT_FORMAT_DSD_MASK) != 0;
            int dst_passthrough_possible = write_dst &&
                ((out_formats & (OUTPUT_FORMAT_DSDIFF | OUTPUT_FORMAT_DSDIFF_EM)) != 0);

            if (src_format.type == DSDPIPE_FORMAT_DST) {
                if (dst_passthrough_possible && !needs_pcm &&
                    !(out_formats & OUTPUT_FORMAT_DSF)) {
                    printf("\nNote: Source is DST-compressed. Passthrough mode for DSDIFF output.\n");
                } else if (needs_pcm && needs_dsd) {
                    printf("\nNote: Source is DST-compressed. DST decoder will decompress to DSD,\n");
                    printf("      then DSD-to-PCM converter will produce PCM for WAV/FLAC sinks.\n");
                } else if (needs_pcm) {
                    printf("\nNote: Source is DST-compressed. DST decoder will decompress,\n");
                    printf("      then DSD-to-PCM converter will produce PCM output.\n");
                } else {
                    printf("\nNote: Source is DST-compressed. DST decoder will decompress to DSD.\n");
                }
            } else if (needs_pcm) {
                printf("\nNote: DSD-to-PCM converter will produce PCM for WAV/FLAC output.\n");
            }
        }
    }

    /* Run pipeline */
    {
        /* Build format list string for display */
        char format_list[256] = "";
        size_t pos = 0;
        if (out_formats & OUTPUT_FORMAT_DSF) {
            pos += (size_t)snprintf(format_list + pos, sizeof(format_list) - pos, "%sDSF", pos > 0 ? ", " : "");
        }
        if (out_formats & OUTPUT_FORMAT_DSDIFF) {
            pos += (size_t)snprintf(format_list + pos, sizeof(format_list) - pos, "%sDSDIFF", pos > 0 ? ", " : "");
        }
        if (out_formats & OUTPUT_FORMAT_DSDIFF_EM) {
            pos += (size_t)snprintf(format_list + pos, sizeof(format_list) - pos, "%sEdit Master", pos > 0 ? ", " : "");
        }
        if (out_formats & OUTPUT_FORMAT_WAV) {
            pos += (size_t)snprintf(format_list + pos, sizeof(format_list) - pos, "%sWAV", pos > 0 ? ", " : "");
        }
        if (out_formats & OUTPUT_FORMAT_FLAC) {
            pos += (size_t)snprintf(format_list + pos, sizeof(format_list) - pos, "%sFLAC", pos > 0 ? ", " : "");
        }
        if (out_formats & OUTPUT_FORMAT_XML) {
            pos += (size_t)snprintf(format_list + pos, sizeof(format_list) - pos, "%sXML", pos > 0 ? ", " : "");
        }
        if (out_formats & OUTPUT_FORMAT_CUE) {
            pos += (size_t)snprintf(format_list + pos, sizeof(format_list) - pos, "%sCUE", pos > 0 ? ", " : "");
        }
        if (out_formats & OUTPUT_FORMAT_PRINT) {
            pos += (size_t)snprintf(format_list + pos, sizeof(format_list) - pos, "%sTEXT", pos > 0 ? ", " : "");
        }
        printf("\nExtracting to %s...\n", format_list);
    }

    uint64_t extraction_start_ms = get_time_ms();
    result = dsdpipe_run(g_pipe);
    uint64_t extraction_end_ms = get_time_ms();

    printf("\n");

    /* Calculate and display timing statistics */
    {
        double elapsed_secs = (double)(extraction_end_ms - extraction_start_ms) / 1000.0;
        double total_mb = (double)prog_ctx.bytes_written / (1024.0 * 1024.0);
        double avg_speed_mbs = (elapsed_secs > 0.0) ? (total_mb / elapsed_secs) : 0.0;

        int hours = (int)(elapsed_secs / 3600);
        int minutes = (int)((elapsed_secs - hours * 3600) / 60);
        double seconds = elapsed_secs - hours * 3600 - minutes * 60;

        printf("\n");
        printf("Statistics:\n");
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

    if (result == DSDPIPE_ERROR_CANCELLED) {
        printf("Cancelled by user.\n");
    } else if (result != DSDPIPE_OK) {
        fprintf(stderr, "Error: Extraction failed: %s\n",
                dsdpipe_get_error_message(g_pipe));
        sa_free(album_output_path);
        dsdpipe_destroy(g_pipe);
        return 1;
    } else {
        printf("Done!\n");
    }

    /* Cleanup */
    sa_free(album_output_path);
    dsdpipe_destroy(g_pipe);
    g_pipe = NULL;

    return (result == DSDPIPE_OK) ? 0 : 1;
}
