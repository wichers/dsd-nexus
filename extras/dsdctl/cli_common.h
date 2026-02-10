/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Shared CLI utilities, types, and helpers for dsdctl
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

#ifndef DSDCTL_CLI_COMMON_H
#define DSDCTL_CLI_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <libdsdpipe/dsdpipe.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Console initialization
 * ========================================================================== */

/**
 * Initialize console for UTF-8 output (Windows-specific).
 * Enables VT processing for ANSI escape codes on Windows.
 */
void cli_init_console(void);

/* ==========================================================================
 * Signal handling
 * ========================================================================== */

/**
 * Install SIGINT (and SIGTERM on Unix) handler.
 * The handler sets an internal interrupted flag.
 */
void cli_install_signal_handler(void);

/**
 * Check if the user has pressed Ctrl+C.
 * @return Non-zero if interrupted
 */
int cli_is_interrupted(void);

/**
 * Set the global dsdpipe handle for signal-based cancellation.
 * When a signal is received, dsdpipe_cancel() will be called on this handle.
 * @param pipe  Pipeline handle (may be NULL to clear)
 */
void cli_set_pipe_for_cancel(dsdpipe_t *pipe);

/* ==========================================================================
 * Output format bitmask
 * ========================================================================== */

typedef enum {
    CLI_FORMAT_NONE       = 0,
    CLI_FORMAT_DSF        = (1 << 0),
    CLI_FORMAT_DSDIFF     = (1 << 1),
    CLI_FORMAT_DSDIFF_EM  = (1 << 2),
    CLI_FORMAT_WAV        = (1 << 3),
    CLI_FORMAT_FLAC       = (1 << 4),
    CLI_FORMAT_XML        = (1 << 5),
    CLI_FORMAT_CUE        = (1 << 6),
    CLI_FORMAT_PRINT      = (1 << 7)
} cli_format_flags_t;

#define CLI_FORMAT_DSD_MASK   (CLI_FORMAT_DSF | CLI_FORMAT_DSDIFF | CLI_FORMAT_DSDIFF_EM)
#define CLI_FORMAT_PCM_MASK   (CLI_FORMAT_WAV | CLI_FORMAT_FLAC)
#define CLI_FORMAT_META_MASK  (CLI_FORMAT_XML | CLI_FORMAT_CUE | CLI_FORMAT_PRINT)
#define CLI_FORMAT_AUDIO_MASK (CLI_FORMAT_DSD_MASK | CLI_FORMAT_PCM_MASK)

/**
 * Parse format string to flags.
 * @param format  Format string (e.g., "dsf", "dsdiff", "em", "wav", "flac", "xml", "cue", "print")
 * @return Format flag or CLI_FORMAT_NONE if invalid
 */
uint32_t cli_parse_format(const char *format);

/**
 * Get file extension for a format.
 * @param format  Single format flag
 * @return File extension (e.g., ".dsf") or ""
 */
const char *cli_format_extension(uint32_t format);

/**
 * Get display name for a format.
 * @param format  Single format flag
 * @return Human-readable name (e.g., "DSF", "DSDIFF Edit Master")
 */
const char *cli_format_name(uint32_t format);

/**
 * Count number of bits set in output format mask.
 */
int cli_count_formats(uint32_t formats);

/* ==========================================================================
 * PCM quality
 * ========================================================================== */

/**
 * Parse PCM quality string.
 * @return 0 on success, -1 if unknown
 */
int cli_parse_pcm_quality(const char *str, dsdpipe_pcm_quality_t *quality);

/**
 * Get display name for PCM quality.
 */
const char *cli_pcm_quality_name(dsdpipe_pcm_quality_t quality);

/* ==========================================================================
 * Track filename format
 * ========================================================================== */

/**
 * Parse track format string.
 * @return 0 on success, -1 if unknown
 */
int cli_parse_track_format(const char *str, dsdpipe_track_format_t *format);

/**
 * Get display name for track format.
 */
const char *cli_track_format_name(dsdpipe_track_format_t format);

/* ==========================================================================
 * Input source detection
 * ========================================================================== */

typedef enum {
    CLI_INPUT_SACD = 0,      /* SACD ISO image (.iso) */
    CLI_INPUT_DSF,            /* DSF file (.dsf) */
    CLI_INPUT_DSDIFF,         /* DSDIFF file (.dff, .dsdiff) */
    CLI_INPUT_PS3_DEVICE,     /* Physical PS3 drive (/dev/sr0, D:) */
    CLI_INPUT_NETWORK         /* PS3 network address (host:port) */
} cli_input_type_t;

/**
 * Detect input type from path string.
 * Recognizes file extensions, device paths, and network addresses.
 */
cli_input_type_t cli_detect_input_type(const char *path);

/**
 * Get display name for input type.
 */
const char *cli_input_type_name(cli_input_type_t type);

/* ==========================================================================
 * Progress display
 * ========================================================================== */

typedef struct {
    int verbose;
    uint64_t start_time_ms;
    uint64_t bytes_written;
    uint64_t last_bytes_written;
    uint64_t last_speed_time_ms;     /* Last time speed was recalculated */
    uint64_t last_display_time_ms;   /* Last time display was refreshed */
    double current_speed_mbs;
} cli_progress_ctx_t;

/**
 * Progress callback compatible with dsdpipe_set_progress_callback().
 */
int cli_progress_callback(const dsdpipe_progress_t *progress, void *userdata);

/**
 * Clear the progress line.
 */
void cli_progress_clear(void);

/* ==========================================================================
 * Timing and statistics
 * ========================================================================== */

/**
 * Print elapsed time, data written, and average speed.
 */
void cli_print_statistics(uint64_t start_ms, uint64_t end_ms,
                          uint64_t bytes_written);

/* ==========================================================================
 * Error handling and logging
 * ========================================================================== */

/**
 * Print error message to stderr (prefixed with "Error: ").
 */
void cli_error(const char *fmt, ...);

/**
 * Print warning message to stderr (prefixed with "Warning: ").
 */
void cli_warning(const char *fmt, ...);

/**
 * Print info message to stdout (only if verbose mode is on).
 */
void cli_info(const char *fmt, ...);

/**
 * Set verbose mode.
 */
void cli_set_verbose(bool verbose);

/**
 * Check if verbose mode is enabled.
 */
bool cli_is_verbose(void);

/* ==========================================================================
 * Option parsing helpers
 * ========================================================================== */

/**
 * Check if argument is an option (starts with '-').
 */
bool cli_is_option(const char *arg);

/**
 * Check if argument matches short or long option.
 */
bool cli_match_option(const char *arg, const char *short_opt, const char *long_opt);

/**
 * Get option value (next argument). Advances *idx if successful.
 * @return Value string or NULL if missing
 */
const char *cli_get_option_value(int argc, char *argv[], int *idx);

#ifdef __cplusplus
}
#endif

#endif /* DSDCTL_CLI_COMMON_H */
