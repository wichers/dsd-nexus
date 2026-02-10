/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Convert command for DSD format conversion
 * Converts DSD audio formats (SACD ISO, DSF, DSDIFF) to various
 * output formats (DSF, DSDIFF, WAV, FLAC, etc.) with support for
 * multi-channel extraction and DSD-to-PCM conversion.
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

#ifndef DSDCTL_CMD_CONVERT_H
#define DSDCTL_CMD_CONVERT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute the convert command.
 *
 * Usage: dsdctl convert [options] <input> [output_dir]
 *
 * Output Format Options (can specify multiple):
 *   -f, --format <fmt>      Add output format (repeatable)
 *   --dsf                   Output as DSF files
 *   --dsdiff, --dff         Output as DSDIFF files
 *   --edit-master, --em     Output as single DSDIFF Edit Master
 *   --wav                   Output as WAV (DSD-to-PCM)
 *   --flac                  Output as FLAC (DSD-to-PCM)
 *   --xml                   Export XML metadata
 *   --cue, --cuesheet       Generate CUE sheet
 *   --print                 Text metadata to stdout
 *
 * PCM Options (WAV/FLAC):
 *   -b, --bits <16|24|32>   Bit depth (default: 24)
 *   -r, --rate <Hz>         Sample rate (default: auto)
 *   -q, --quality <level>   fast, normal, high (default: normal)
 *   -c, --compression <0-8> FLAC compression (default: 5)
 *
 * DST Options:
 *   --dst                   Keep DST compression (DSDIFF only)
 *   --decode-dst            Decode DST to raw DSD (default)
 *
 * Track/Area Selection:
 *   -t, --tracks <spec>     Track selection (default: all)
 *   -a, --area <type>       stereo, multichannel (default: stereo)
 *
 * Metadata:
 *   -i, --id3               Enable ID3v2 tags (default)
 *   -n, --no-id3            Disable ID3v2 tags
 *   -A, --artist            Include artist in output directory
 *   --track-format <fmt>    number, title, artist (default: artist)
 *
 * Other:
 *   -l, --list              List tracks only
 *   --no-progress           Disable progress bar
 *   -v, --verbose           Verbose output
 *   -h, --help              Show help
 *
 * @param argc Argument count (including "convert")
 * @param argv Argument vector
 * @return Exit code (0 on success)
 */
int cmd_convert(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* DSDCTL_CMD_CONVERT_H */
