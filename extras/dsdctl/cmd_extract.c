/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Extract command implementation
 * Extracts a raw SACD ISO image from a PS3 BluRay drive or PS3 network
 * streaming server. Uses sacd which wraps sacd_input for transparent
 * authentication, key exchange, and sector decryption.
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

#include "cmd_extract.h"
#include "cli_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include <libsacd/sacd.h>
#include <libsautil/mem.h>
#include <libsautil/compat.h>
#include <libsautil/time.h>

/* Sector constants */
#define SACD_SECTOR_SIZE     2048
#define SECTORS_PER_READ     256     /* 512 KB buffer per read */

/* ==========================================================================
 * Extract options
 * ========================================================================== */

typedef struct {
    const char *device_path;
    const char *network_addr;
    const char *output_path;
    bool show_progress;
    bool verbose;
} extract_opts_t;

/* ==========================================================================
 * Help
 * ========================================================================== */

static void print_extract_help(void)
{
    printf("Usage: dsdctl extract [options]\n\n");
    printf("Extract a raw SACD ISO image from a PS3 BluRay drive or network server.\n\n");

    printf("Input Source (one required):\n");
    printf("  -d, --device <path>     PS3 drive device path\n");
    printf("                          Linux: /dev/sr0, /dev/sg0\n");
    printf("                          Windows: D:, \\\\.\\D:, \\\\.\\CdRom0\n");
    printf("  -n, --network <addr>    PS3 network address (host:port)\n");
    printf("                          Example: 192.168.1.100:2002\n\n");

    printf("Output:\n");
    printf("  -o, --output <path>     Output ISO file path (required)\n\n");

    printf("Options:\n");
    printf("  --no-progress           Disable progress display\n");
    printf("  -v, --verbose           Verbose output\n");
    printf("  -h, --help              Show this help message\n\n");

    printf("Examples:\n");
    printf("  dsdctl extract -d /dev/sr0 -o album.iso\n");
    printf("  dsdctl extract -d D: -o album.iso\n");
    printf("  dsdctl extract -n 192.168.1.100:2002 -o album.iso\n");
}

/* ==========================================================================
 * Progress display for sector reading
 * ========================================================================== */

static void print_extract_progress(uint32_t current, uint32_t total,
                                   double speed_mbs)
{
    if (total == 0) {
        return;
    }
    int percent = (int)((uint64_t)current * 100 / total);
    printf("\rExtracting: %3d%% (%u / %u sectors) @ %.2f MB/s",
           percent, current, total, speed_mbs);
    fflush(stdout);
}

/* ==========================================================================
 * Extract implementation
 * ========================================================================== */

static int do_extract(const extract_opts_t *opts)
{
    sacd_t *reader = NULL;
    const char *input_path = NULL;
    uint32_t total_sectors = 0;
    uint32_t sectors_read_total = 0;
    uint8_t *buffer = NULL;
    FILE *output_file = NULL;
    uint64_t start_ms, last_update_ms;
    uint64_t last_bytes = 0;
    double current_speed = 0.0;
    int ret = 0;

    /* Determine input path */
    if (opts->device_path) {
        input_path = opts->device_path;
        printf("Source:  PS3 Drive (%s)\n", input_path);
    } else {
        input_path = opts->network_addr;
        printf("Source:  PS3 Network (%s)\n", input_path);
    }
    printf("Output:  %s\n", opts->output_path);

    /* Install signal handler */
    cli_install_signal_handler();

    /* Create and initialize SACD reader */
    reader = sacd_create();
    if (!reader) {
        cli_error("Failed to create SACD reader");
        return 1;
    }

    printf("Connecting and authenticating...\n");

    if (sacd_init(reader, input_path, 1, 1) != SACD_OK) {
        cli_error("Failed to initialize SACD reader for: %s", input_path);
        sacd_destroy(reader);
        return 1;
    }

    /* Get total disc size */
    if (sacd_get_total_sectors(reader, &total_sectors) != SACD_OK ||
        total_sectors == 0) {
        cli_error("Failed to get disc sector count");
        sacd_close(reader);
        sacd_destroy(reader);
        return 1;
    }

    {
        double total_mb = (double)total_sectors * SACD_SECTOR_SIZE / (1024.0 * 1024.0);
        printf("Disc:    %u sectors (%.1f MB)\n\n", total_sectors, total_mb);
    }

    /* Allocate read buffer */
    buffer = (uint8_t *)sa_malloc(SECTORS_PER_READ * SACD_SECTOR_SIZE);
    if (!buffer) {
        cli_error("Failed to allocate read buffer");
        sacd_close(reader);
        sacd_destroy(reader);
        return 1;
    }

    /* Open output file */
    output_file = sa_fopen(opts->output_path, "wb");
    if (!output_file) {
        cli_error("Failed to create output file: %s", opts->output_path);
        sa_free(buffer);
        sacd_close(reader);
        sacd_destroy(reader);
        return 1;
    }

    /* Extraction loop */
    start_ms = (uint64_t)(sa_gettime_relative() / 1000);
    last_update_ms = start_ms;
    sectors_read_total = 0;

    while (sectors_read_total < total_sectors && !cli_is_interrupted()) {
        uint32_t remaining = total_sectors - sectors_read_total;
        uint32_t to_read = (remaining > SECTORS_PER_READ)
                            ? SECTORS_PER_READ : remaining;
        uint32_t sectors_read = 0;

        int read_result = sacd_read_raw_sectors(
            reader, sectors_read_total, to_read, buffer, &sectors_read);

        if (read_result != SACD_OK || sectors_read == 0) {
            cli_error("Read error at sector %u", sectors_read_total);
            ret = 1;
            break;
        }

        size_t written = fwrite(buffer, SACD_SECTOR_SIZE, sectors_read, output_file);
        if (written != sectors_read) {
            cli_error("Write error at sector %u", sectors_read_total);
            ret = 1;
            break;
        }

        sectors_read_total += sectors_read;

        /* Update progress display */
        if (opts->show_progress) {
            uint64_t now_ms = (uint64_t)(sa_gettime_relative() / 1000);
            double elapsed = (double)(now_ms - last_update_ms) / 1000.0;
            if (elapsed >= 0.5) {
                uint64_t current_bytes = (uint64_t)sectors_read_total * SACD_SECTOR_SIZE;
                uint64_t bytes_delta = current_bytes - last_bytes;
                current_speed = (double)bytes_delta / (1024.0 * 1024.0) / elapsed;
                last_bytes = current_bytes;
                last_update_ms = now_ms;
            }
            print_extract_progress(sectors_read_total, total_sectors, current_speed);
        }
    }

    if (opts->show_progress) {
        cli_progress_clear();
    }

    /* Close output file */
    fclose(output_file);

    /* Handle result */
    if (cli_is_interrupted()) {
        printf("Extraction cancelled by user.\n");
        /* Remove incomplete file */
        remove(opts->output_path);
        ret = 1;
    } else if (ret != 0) {
        /* Remove incomplete file on error */
        remove(opts->output_path);
    } else {
        uint64_t end_ms = (uint64_t)(sa_gettime_relative() / 1000);
        uint64_t total_bytes = (uint64_t)sectors_read_total * SACD_SECTOR_SIZE;
        cli_print_statistics(start_ms, end_ms, total_bytes);
        printf("Extraction complete: %s\n", opts->output_path);
    }

    /* Cleanup */
    sa_free(buffer);
    sacd_close(reader);
    sacd_destroy(reader);

    return ret;
}

/* ==========================================================================
 * Command entry point
 * ========================================================================== */

int cmd_extract(int argc, char *argv[])
{
    extract_opts_t opts = {
        .device_path = NULL,
        .network_addr = NULL,
        .output_path = NULL,
        .show_progress = true,
        .verbose = false
    };

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (cli_match_option(arg, "-h", "--help")) {
            print_extract_help();
            return 0;
        }

        if (cli_match_option(arg, "-d", "--device")) {
            opts.device_path = cli_get_option_value(argc, argv, &i);
            if (!opts.device_path) {
                cli_error("Missing value for --device");
                return 1;
            }
            continue;
        }

        if (cli_match_option(arg, "-n", "--network")) {
            opts.network_addr = cli_get_option_value(argc, argv, &i);
            if (!opts.network_addr) {
                cli_error("Missing value for --network");
                return 1;
            }
            continue;
        }

        if (cli_match_option(arg, "-o", "--output")) {
            opts.output_path = cli_get_option_value(argc, argv, &i);
            if (!opts.output_path) {
                cli_error("Missing value for --output");
                return 1;
            }
            continue;
        }

        if (strcmp(arg, "--no-progress") == 0) {
            opts.show_progress = false;
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

        /* Positional argument: auto-detect device vs network */
        if (!opts.device_path && !opts.network_addr) {
            cli_input_type_t type = cli_detect_input_type(arg);
            if (type == CLI_INPUT_NETWORK) {
                opts.network_addr = arg;
            } else if (type == CLI_INPUT_PS3_DEVICE) {
                opts.device_path = arg;
            } else {
                cli_error("Cannot determine input type for: %s\n"
                          "  Use -d for device or -n for network.", arg);
                return 1;
            }
        } else if (!opts.output_path) {
            opts.output_path = arg;
        } else {
            cli_error("Unexpected argument: %s", arg);
            return 1;
        }
    }

    /* Validate */
    if (!opts.device_path && !opts.network_addr) {
        cli_error("Input source required (-d/--device or -n/--network)");
        fprintf(stderr, "Run 'dsdctl extract --help' for usage.\n");
        return 1;
    }

    if (opts.device_path && opts.network_addr) {
        cli_error("Specify only one input source: device or network, not both");
        return 1;
    }

    if (!opts.output_path) {
        cli_error("Output path required (-o/--output)");
        fprintf(stderr, "Run 'dsdctl extract --help' for usage.\n");
        return 1;
    }

    return do_extract(&opts);
}
