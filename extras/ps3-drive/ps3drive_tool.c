/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief PS3 BluRay Drive Utility Tool.
 * A command-line utility for PS3 BluRay drive operations:
 *   - Drive information and detection
 *   - BD authentication
 *   - SAC key exchange
 *   - Drive pairing (P-Block, S-Block, HRL)
 *   - Firmware update
 * Usage:
 *   ps3drive-tool info <device>          - Show drive information
 *   ps3drive-tool auth <device>          - Authenticate drive
 *   ps3drive-tool keys <device>          - Perform SAC key exchange
 *   ps3drive-tool sacd <device> <0|1>    - Set SACD mode
 *   ps3drive-tool pair <device>          - Pair drive with default data
 *   ps3drive-tool fw <device> <firmware> - Update firmware
 *   ps3drive-tool eject <device>         - Eject disc
 *   ps3drive-tool detect                 - Scan for PS3 drives
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

#include <libps3drive/ps3drive.h>

#include <libsautil/getopt.h>
#include <libsautil/compat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define PROGRAM_NAME    "ps3drive-tool"
#define PROGRAM_VERSION "1.0.0"

/* ============================================================================
 * Global Options
 * ============================================================================ */

static int g_verbose = 0;
static bool g_force = false;

/* Long options for getopt_long */
static struct option long_options[] = {
    {"verbose", no_argument,       NULL, 'v'},
    {"force",   no_argument,       NULL, 'f'},
    {"help",    no_argument,       NULL, 'h'},
    {"version", no_argument,       NULL, 'V'},
    {NULL,      0,                 NULL, 0}
};

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

static void print_usage(void)
{
    fprintf(stderr,
        PROGRAM_NAME " v" PROGRAM_VERSION " - PS3 BluRay Drive Utility\n"
        "Using libps3drive v%s\n"
        "\n"
        "Usage: " PROGRAM_NAME " [options] <command> <device> [args...]\n"
        "\n"
        "Commands:\n"
        "  info <device>              Show drive information\n"
        "  auth <device>              Authenticate with drive\n"
        "  keys <device>              Perform SAC key exchange (derives AES key/IV)\n"
        "  sacd <device> <0|1>        Set SACD mode (0=disable, 1=enable)\n"
        "  pair <device>              Pair drive with default P-Block/S-Block/HRL\n"
        "  fw <device> <fw_file>      Update drive firmware\n"
        "  eject <device>             Eject disc from drive\n"
        "  detect                     Detect PS3 drives on the system\n"
        "\n"
        "Options:\n"
        "  -v, --verbose              Increase verbosity (can be used multiple times)\n"
        "  -f, --force                Force operation (skip confirmations)\n"
        "  -h, --help                 Show this help message\n"
        "  -V, --version              Show version information\n"
        "\n"
        "Device paths:\n"
#ifdef _WIN32
        "  Windows: D:, E:, \\\\.\\D:, \\\\.\\CdRom0\n"
#else
        "  Linux:   /dev/sr0, /dev/sg0\n"
        "  macOS:   /dev/disk1\n"
#endif
        "\n"
        "Examples:\n"
#ifdef _WIN32
        "  " PROGRAM_NAME " info D:\n"
        "  " PROGRAM_NAME " -v auth D:\n"
        "  " PROGRAM_NAME " keys D:\n"
        "  " PROGRAM_NAME " -f pair D:\n"
#else
        "  " PROGRAM_NAME " info /dev/sr0\n"
        "  " PROGRAM_NAME " -v auth /dev/sr0\n"
        "  " PROGRAM_NAME " keys /dev/sr0\n"
        "  " PROGRAM_NAME " -f pair /dev/sr0\n"
#endif
        "\n"
        "WARNING: The 'pair' and 'fw' commands can permanently damage your drive\n"
        "         if used incorrectly. Use at your own risk!\n",
        ps3drive_version());
}

static void print_version(void)
{
    printf(PROGRAM_NAME " v" PROGRAM_VERSION "\n");
    printf("Using libps3drive v%s\n", ps3drive_version());
}

static void print_hex(const char *label, const uint8_t *data, size_t len)
{
    size_t i;
    printf("%s: ", label);
    for (i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

static uint8_t *read_file(const char *path, size_t *out_len)
{
    FILE *fp = NULL;
    uint8_t *data = NULL;
    int64_t file_size;
    size_t bytes_read;

    fp = sa_fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file: %s\n", path);
        return NULL;
    }

    if (sa_fseek64(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: Cannot seek in file: %s\n", path);
        fclose(fp);
        return NULL;
    }

    file_size = sa_ftell64(fp);
    if (file_size < 0) {
        fprintf(stderr, "Error: Cannot get file size: %s\n", path);
        fclose(fp);
        return NULL;
    }

    if (sa_fseek64(fp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Error: Cannot seek in file: %s\n", path);
        fclose(fp);
        return NULL;
    }

    data = (uint8_t *)malloc((size_t)file_size);
    if (data == NULL) {
        fprintf(stderr, "Error: Out of memory\n");
        fclose(fp);
        return NULL;
    }

    bytes_read = fread(data, 1, (size_t)file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size) {
        fprintf(stderr, "Error: Failed to read file: %s\n", path);
        free(data);
        return NULL;
    }

    *out_len = (size_t)file_size;
    return data;
}

static bool confirm_action(const char *action)
{
    char response[16];

    if (g_force) {
        return true;
    }

    printf("WARNING: %s\n", action);
    printf("This operation can damage your drive. Continue? [y/N]: ");
    fflush(stdout);

    if (fgets(response, sizeof(response), stdin) == NULL) {
        return false;
    }

    return (response[0] == 'y' || response[0] == 'Y');
}

/* ============================================================================
 * Command: info
 * ============================================================================ */

static int cmd_info(const char *device)
{
    ps3drive_t *handle = NULL;
    ps3drive_info_t info;
    ps3drive_type_t drive_type;
    ps3drive_error_t err;
    uint32_t total_sectors;
    bool is_hybrid;

    printf("Opening device: %s\n", device);

    err = ps3drive_open(device, &handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Failed to open device: %s\n",
                ps3drive_error_string(err));
        return 1;
    }

    ps3drive_set_verbose(handle, g_verbose);

    /* Authenticate to access drive info */
    printf("Authenticating...\n");
    err = ps3drive_authenticate(handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Warning: BD authentication failed: %s\n",
                ps3drive_error_string(err));
        /* Continue anyway - some info may still be available */
    }

    /* Get drive info */
    err = ps3drive_get_info(handle, &info);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Failed to get drive info: %s\n",
                ps3drive_error_string(err));
        ps3drive_close(handle);
        return 1;
    }

    /* Get drive type */
    err = ps3drive_get_type(handle, &drive_type);
    if (err != PS3DRIVE_OK) {
        drive_type = PS3DRIVE_TYPE_UNKNOWN;
    }

    printf("\n");
    printf("=== Drive Information ===\n");
    printf("Vendor:       %s\n", info.vendor_id);
    printf("Product:      %s\n", info.product_id);
    printf("Revision:     %s\n", info.revision);
    printf("Drive Type:   %s (0x%016llx)\n",
           ps3drive_type_string(drive_type),
           (unsigned long long)info.drive_type);
    printf("SACD Feature: %s\n", info.has_sacd_feature ? "Yes" : "No");
    printf("Hybrid:       %s\n", info.has_hybrid_support ? "Yes" : "No");

    /* Get total sectors if disc present */
    err = ps3drive_get_total_sectors(handle, &total_sectors);
    if (err == PS3DRIVE_OK) {
        printf("Total Sectors: %u (%.2f GB)\n", total_sectors,
               (double)total_sectors * PS3DRIVE_SECTOR_SIZE / (1024.0 * 1024.0 * 1024.0));
    } else {
        printf("Total Sectors: N/A (no disc or error)\n");
    }

    /* Check hybrid disc */
    err = ps3drive_is_hybrid_disc(handle, &is_hybrid);
    if (err == PS3DRIVE_OK && is_hybrid) {
        printf("Disc Type:    Hybrid SACD\n");
    }

    printf("\n");

    /* Print detailed features if verbose */
    if (g_verbose > 0) {
        printf("=== Drive Features ===\n");
        ps3drive_print_features(handle, g_verbose);
        printf("\n");
    }

    ps3drive_close(handle);
    return 0;
}

/* ============================================================================
 * Command: auth
 * ============================================================================ */

static int cmd_auth(const char *device)
{
    ps3drive_t *handle = NULL;
    ps3drive_error_t err;
    bool authenticated;

    printf("Opening device: %s\n", device);

    err = ps3drive_open(device, &handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Failed to open device: %s\n",
                ps3drive_error_string(err));
        return 1;
    }

    ps3drive_set_verbose(handle, g_verbose);

    /* Perform BD authentication */
    printf("Performing BD authentication...\n");
    err = ps3drive_authenticate(handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: BD authentication failed: %s\n",
                ps3drive_error_string(err));
        if (g_verbose > 0) {
            fprintf(stderr, "Detail: %s\n", ps3drive_get_error(handle));
        }
        ps3drive_close(handle);
        return 1;
    }

    /* Verify authentication */
    err = ps3drive_is_authenticated(handle, &authenticated);
    if (err == PS3DRIVE_OK && authenticated) {
        printf("BD authentication successful!\n");
    } else {
        printf("Authentication completed (unable to verify status).\n");
    }

    ps3drive_close(handle);
    return 0;
}

/* ============================================================================
 * Command: keys
 * ============================================================================ */

static int cmd_keys(const char *device)
{
    ps3drive_t *handle = NULL;
    ps3drive_error_t err;
    uint8_t aes_key[PS3DRIVE_AES_KEY_SIZE];
    uint8_t aes_iv[PS3DRIVE_AES_IV_SIZE];

    printf("Opening device: %s\n", device);

    err = ps3drive_open(device, &handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Failed to open device: %s\n",
                ps3drive_error_string(err));
        return 1;
    }

    ps3drive_set_verbose(handle, g_verbose);

    /* BD authentication must be done before SAC key exchange */
    printf("Authenticating...\n");
    err = ps3drive_authenticate(handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: BD authentication failed: %s\n",
                ps3drive_error_string(err));
        if (g_verbose > 0) {
            fprintf(stderr, "Detail: %s\n", ps3drive_get_error(handle));
        }
        ps3drive_close(handle);
        return 1;
    }

    /* Perform SAC key exchange */
    printf("Performing SAC key exchange...\n");
    err = ps3drive_sac_key_exchange(handle, aes_key, aes_iv);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: SAC key exchange failed: %s\n",
                ps3drive_error_string(err));
        if (g_verbose > 0) {
            fprintf(stderr, "Detail: %s\n", ps3drive_get_error(handle));
        }
        ps3drive_close(handle);
        return 1;
    }

    printf("\nSAC key exchange successful!\n\n");
    print_hex("AES Key", aes_key, PS3DRIVE_AES_KEY_SIZE);
    print_hex("AES IV ", aes_iv, PS3DRIVE_AES_IV_SIZE);
    printf("\n");

    /* Try to select SACD layer for hybrid discs */
    err = ps3drive_select_sacd_layer(handle);
    if (err == PS3DRIVE_OK) {
        printf("SACD layer selected (hybrid disc detected).\n");
    } else if (err == PS3DRIVE_ERR_NOT_HYBRID) {
        printf("Single-layer SACD disc (not hybrid).\n");
    }

    ps3drive_close(handle);
    return 0;
}

/* ============================================================================
 * Command: sacd (set SACD mode)
 * ============================================================================ */

static int cmd_sacd(const char *device, int mode)
{
    ps3drive_t *handle = NULL;
    ps3drive_error_t err;

    if (!confirm_action("You are about to change the drive SACD mode.")) {
        printf("Operation cancelled.\n");
        return 1;
    }

    printf("Opening device: %s\n", device);

    err = ps3drive_open(device, &handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Failed to open device: %s\n",
                ps3drive_error_string(err));
        return 1;
    }

    ps3drive_set_verbose(handle, g_verbose);

    /* Eject disc before changing SACD mode */
    printf("Ejecting disc...\n");
    err = ps3drive_eject(handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Warning: Eject failed: %s\n",
                ps3drive_error_string(err));
        /* Continue anyway - disc may already be ejected */
    }

    /* D7 commands require BD authentication */
    printf("Authenticating...\n");
    err = ps3drive_authenticate(handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: BD authentication failed: %s\n",
                ps3drive_error_string(err));
        if (g_verbose > 0) {
            fprintf(stderr, "Detail: %s\n", ps3drive_get_error(handle));
        }
        ps3drive_close(handle);
        return 1;
    }

    /* Set the SACD mode - this command will hang until drive is power cycled */
    printf("%s SACD mode...\n", mode ? "Enabling" : "Disabling");
    printf("NOTE: The drive will stop responding after this command.\n");
    printf("      Power cycle the drive to complete the change.\n\n");

    ps3drive_enable_sacd_mode(handle, mode ? true : false);
    ps3drive_close(handle);
    return 0;
}

/* ============================================================================
 * Command: pair
 * ============================================================================ */

static int cmd_pair(const char *device)
{
    ps3drive_t *handle = NULL;
    ps3drive_pairing_ctx_t *ctx = NULL;
    ps3drive_error_t err;

    if (!confirm_action("You are about to pair this drive with default P-Block/S-Block/HRL data.")) {
        printf("Operation cancelled.\n");
        return 1;
    }

    /* Create default pairing context (P-Block, S-Block, HRL) */
    printf("Creating pairing context with default data...\n");
    err = ps3drive_pairing_create_default(&ctx);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Failed to create pairing context: %s\n",
                ps3drive_error_string(err));
        return 1;
    }

    /* Open drive */
    printf("Opening device: %s\n", device);
    err = ps3drive_open(device, &handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Failed to open device: %s\n",
                ps3drive_error_string(err));
        ps3drive_pairing_free(ctx);
        return 1;
    }

    ps3drive_set_verbose(handle, g_verbose);

    /* Perform pairing */
    printf("Pairing drive...\n");
    printf("  Step 1: Writing P-Block to buffer 2...\n");
    printf("  Step 2: Authenticating drive...\n");
    printf("  Step 3: Writing S-Block to buffer 3...\n");
    printf("  Step 4: Writing HRL to buffer 4...\n");

    err = ps3drive_pair(handle, ctx);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Pairing failed: %s\n",
                ps3drive_error_string(err));
        if (g_verbose > 0) {
            fprintf(stderr, "Detail: %s\n", ps3drive_get_error(handle));
        }
        ps3drive_close(handle);
        ps3drive_pairing_free(ctx);
        return 1;
    }

    printf("Drive pairing completed successfully!\n");
    printf("The drive should now be able to play BD movies.\n");

    ps3drive_close(handle);
    ps3drive_pairing_free(ctx);
    return 0;
}

/* ============================================================================
 * Command: fw
 * ============================================================================ */

static int cmd_fw(const char *device, const char *fw_path)
{
    ps3drive_t *handle = NULL;
    ps3drive_error_t err;
    uint8_t *fw_data = NULL;
    size_t fw_len = 0;

    if (!confirm_action("You are about to update the drive firmware.")) {
        printf("Operation cancelled.\n");
        return 1;
    }

    /* Read firmware file */
    printf("Reading firmware file: %s\n", fw_path);
    fw_data = read_file(fw_path, &fw_len);
    if (fw_data == NULL) {
        return 1;
    }

    printf("Firmware size: %zu bytes\n", fw_len);

    /* Open drive */
    printf("Opening device: %s\n", device);
    err = ps3drive_open(device, &handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Failed to open device: %s\n",
                ps3drive_error_string(err));
        free(fw_data);
        return 1;
    }

    ps3drive_set_verbose(handle, g_verbose);

    /*
     * NOTE: Firmware update does NOT require BD authentication.
     * The original bd_update_fw.c does not authenticate before
     * updating firmware. Authentication may interfere with the
     * firmware update process.
     */

    /* Update firmware */
    printf("Updating firmware (this may take a while)...\n");
    err = ps3drive_update_firmware(handle, fw_data, fw_len, 0, 300);
    free(fw_data);

    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Firmware update failed: %s\n",
                ps3drive_error_string(err));
        if (g_verbose > 0) {
            fprintf(stderr, "Detail: %s\n", ps3drive_get_error(handle));
        }
        ps3drive_close(handle);
        return 1;
    }

    printf("Firmware update completed successfully!\n");
    printf("Please power cycle the drive to apply the new firmware.\n");

    ps3drive_close(handle);
    return 0;
}

/* ============================================================================
 * Command: eject
 * ============================================================================ */

static int cmd_eject(const char *device)
{
    ps3drive_t *handle = NULL;
    ps3drive_error_t err;

    printf("Opening device: %s\n", device);

    err = ps3drive_open(device, &handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Failed to open device: %s\n",
                ps3drive_error_string(err));
        return 1;
    }

    ps3drive_set_verbose(handle, g_verbose);

    printf("Ejecting disc...\n");
    err = ps3drive_eject(handle);
    if (err != PS3DRIVE_OK) {
        fprintf(stderr, "Error: Eject failed: %s\n",
                ps3drive_error_string(err));
        ps3drive_close(handle);
        return 1;
    }

    printf("Disc ejected.\n");

    ps3drive_close(handle);
    return 0;
}

/* ============================================================================
 * Command: detect
 * ============================================================================ */

static int cmd_detect(void)
{
    bool found = false;
    bool is_ps3;
    ps3drive_error_t err;
    int i;

    printf("Scanning for PS3 BluRay drives...\n\n");

#ifdef _WIN32
    /* Scan drive letters A-Z */
    for (i = 0; i < 26; i++) {
        char device[8];
        sprintf_s(device, sizeof(device), "%c:", 'A' + i);

        /* Check if it's a CD-ROM drive */
        if (GetDriveTypeA(device) != DRIVE_CDROM) {
            continue;
        }

        err = ps3drive_is_ps3_drive(device, &is_ps3);
        if (err == PS3DRIVE_OK && is_ps3) {
            printf("  Found PS3 drive: %s\n", device);
            found = true;
        } else if (g_verbose > 1) {
            printf("  Checked %s: not a PS3 drive\n", device);
        }
    }
#else
    /* Scan /dev/sr* devices */
    for (i = 0; i < 10; i++) {
        char device[32];
        snprintf(device, sizeof(device), "/dev/sr%d", i);

        err = ps3drive_is_ps3_drive(device, &is_ps3);
        if (err == PS3DRIVE_OK && is_ps3) {
            printf("  Found PS3 drive: %s\n", device);
            found = true;
        } else if (err == PS3DRIVE_OK && g_verbose > 1) {
            printf("  Checked %s: not a PS3 drive\n", device);
        }
    }

    /* Also scan /dev/sg* devices */
    for (i = 0; i < 10; i++) {
        char device[32];
        snprintf(device, sizeof(device), "/dev/sg%d", i);

        err = ps3drive_is_ps3_drive(device, &is_ps3);
        if (err == PS3DRIVE_OK && is_ps3) {
            printf("  Found PS3 drive: %s\n", device);
            found = true;
        } else if (err == PS3DRIVE_OK && g_verbose > 1) {
            printf("  Checked %s: not a PS3 drive\n", device);
        }
    }
#endif

    if (!found) {
        printf("No PS3 BluRay drives found.\n");
    }

    printf("\n");
    return found ? 0 : 1;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char *argv[])
{
    int opt;
    int option_index = 0;
    const char *command;
    const char *device;

    /* Parse options using getopt_long */
    while ((opt = getopt_long(argc, argv, "vfhV", long_options, &option_index)) != -1) {
        switch (opt) {
        case 'v':
            g_verbose++;
            break;
        case 'f':
            g_force = true;
            break;
        case 'h':
            print_usage();
            return 0;
        case 'V':
            print_version();
            return 0;
        default:
            print_usage();
            return 1;
        }
    }

    /* Check for command */
    if (optind >= argc) {
        fprintf(stderr, "Error: No command specified.\n\n");
        print_usage();
        return 1;
    }

    command = argv[optind++];

    /* Handle 'detect' command (no device needed) */
    if (strcmp(command, "detect") == 0) {
        return cmd_detect();
    }

    /* All other commands require a device */
    if (optind >= argc) {
        fprintf(stderr, "Error: No device specified.\n\n");
        print_usage();
        return 1;
    }

    device = argv[optind++];

    /* Dispatch commands */
    if (strcmp(command, "info") == 0) {
        return cmd_info(device);
    } else if (strcmp(command, "auth") == 0) {
        return cmd_auth(device);
    } else if (strcmp(command, "keys") == 0) {
        return cmd_keys(device);
    } else if (strcmp(command, "sacd") == 0) {
        int mode;
        if (optind >= argc) {
            fprintf(stderr, "Error: 'sacd' command requires mode (0 or 1).\n");
            return 1;
        }
        mode = atoi(argv[optind]);
        if (mode != 0 && mode != 1) {
            fprintf(stderr, "Error: Invalid mode '%s'. Must be 0 or 1.\n",
                    argv[optind]);
            return 1;
        }
        return cmd_sacd(device, mode);
    } else if (strcmp(command, "pair") == 0) {
        return cmd_pair(device);
    } else if (strcmp(command, "fw") == 0) {
        if (optind >= argc) {
            fprintf(stderr, "Error: 'fw' command requires firmware file path.\n");
            return 1;
        }
        return cmd_fw(device, argv[optind]);
    } else if (strcmp(command, "eject") == 0) {
        return cmd_eject(device);
    } else {
        fprintf(stderr, "Error: Unknown command: %s\n\n", command);
        print_usage();
        return 1;
    }
}
