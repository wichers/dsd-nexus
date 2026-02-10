/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief dsdctl - Unified DSD audio control utility
 * Subcommands:
 *   convert - Convert DSD formats (ISO, DSF, DSDIFF) to various outputs
 *   extract - Extract raw SACD ISO from PS3 drive or network
 *   info    - Display file/disc metadata information
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "cmd_convert.h"
#include "cmd_extract.h"
#include "cmd_info.h"
#include "cli_common.h"

#define DSDCTL_VERSION "1.0.0"

/* ==========================================================================
 * Usage and help
 * ========================================================================== */

static void print_version(void)
{
    printf("dsdctl %s\n", DSDCTL_VERSION);
    printf("Copyright (c) 2024-2026\n");
    printf("License: MIT\n");
}

static void print_usage(const char *prog)
{
    printf("Usage: %s <command> [options]\n\n", prog);
    printf("Unified DSD audio processing utility.\n\n");
    printf("Commands:\n");
    printf("  convert    Convert between DSD audio formats (ISO, DSF, DSDIFF -> DSF, DSDIFF, WAV, FLAC, etc.)\n");
    printf("  extract    Extract raw SACD ISO image from PS3 drive or network\n");
    printf("  info       Display file or disc metadata information\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -V, --version  Show version information\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s convert --dsf album.iso ./output\n", prog);
    printf("  %s convert --flac -q high -b 24 album.iso ./output\n", prog);
    printf("  %s extract -d /dev/sr0 -o album.iso\n", prog);
    printf("  %s extract -n 192.168.1.100:2002 -o album.iso\n", prog);
    printf("  %s info album.iso\n", prog);
    printf("  %s info --json track.dsf\n", prog);
    printf("\n");
    printf("Run '%s <command> --help' for more information on a command.\n", prog);
}

/* ==========================================================================
 * Main entry point
 * ========================================================================== */

int main(int argc, char *argv[])
{
    /* Initialize UTF-8 console on Windows */
    cli_init_console();

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    /* Handle global options */
    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(cmd, "-V") == 0 || strcmp(cmd, "--version") == 0) {
        print_version();
        return 0;
    }

    /* Dispatch to subcommand */
    if (strcmp(cmd, "convert") == 0) {
        return cmd_convert(argc - 1, argv + 1);
    }

    if (strcmp(cmd, "extract") == 0) {
        return cmd_extract(argc - 1, argv + 1);
    }

    if (strcmp(cmd, "info") == 0) {
        return cmd_info(argc - 1, argv + 1);
    }

    /* Unknown command */
    fprintf(stderr, "Error: Unknown command '%s'\n", cmd);
    fprintf(stderr, "Run '%s --help' for usage.\n", argv[0]);
    return 1;
}
