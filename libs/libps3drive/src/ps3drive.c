/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Core PS3 drive interface implementation.
 * This file implements the main public API functions for opening,
 * closing, reading, and decrypting data from PS3 BluRay drives.
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

#include "ps3drive_internal.h"
#include "ps3drive_crypto.h"
#include "ps3drive_keys.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <sg_pt.h>
#include <sg_lib.h>
#include <sg_unaligned.h>
#include <sg_cmds_basic.h>
#include <sg_cmds_mmc.h>
#include <sg_cmds_ps3.h>

/* ============================================================================
 * Version Information
 * ============================================================================ */

static const char PS3DRIVE_VERSION_STRING[] = "1.0.0";

const char *ps3drive_version(void)
{
    return PS3DRIVE_VERSION_STRING;
}

/* ============================================================================
 * Error Handling
 * ============================================================================ */

/** Error message strings */
static const char *error_strings[] = {
    "Success",                           /* PS3DRIVE_OK */
    "NULL pointer argument",             /* PS3DRIVE_ERR_NULL_PTR */
    "Failed to open device",             /* PS3DRIVE_ERR_OPEN_FAILED */
    "Device is not a PS3 drive",         /* PS3DRIVE_ERR_NOT_PS3_DRIVE */
    "BD authentication failed",          /* PS3DRIVE_ERR_AUTH_FAILED */
    "SAC key exchange failed",           /* PS3DRIVE_ERR_SAC_FAILED */
    "Decryption failed",                 /* PS3DRIVE_ERR_DECRYPT_FAILED */
    "Read operation failed",             /* PS3DRIVE_ERR_READ_FAILED */
    "SCSI command failed",               /* PS3DRIVE_ERR_SCSI_FAILED */
    "Cryptographic operation failed",    /* PS3DRIVE_ERR_CRYPTO_FAILED */
    "Drive lacks SACD feature",          /* PS3DRIVE_ERR_NO_SACD_FEATURE */
    "Disc is not a hybrid disc",         /* PS3DRIVE_ERR_NOT_HYBRID */
    "Failed to select layer",            /* PS3DRIVE_ERR_LAYER_SELECT */
    "Drive pairing failed",              /* PS3DRIVE_ERR_PAIRING_FAILED */
    "Firmware update failed",            /* PS3DRIVE_ERR_FW_UPDATE */
    "Memory allocation failed",          /* PS3DRIVE_ERR_OUT_OF_MEMORY */
    "Invalid argument",                  /* PS3DRIVE_ERR_INVALID_ARG */
    "Operation timed out",               /* PS3DRIVE_ERR_TIMEOUT */
    "Not authenticated yet",             /* PS3DRIVE_ERR_NOT_AUTHENTICATED */
    "Drive not ready",                   /* PS3DRIVE_ERR_NOT_READY */
    "No disc in drive",                  /* PS3DRIVE_ERR_NO_DISC */
    "Invalid EID2 data",                 /* PS3DRIVE_ERR_INVALID_EID2 */
    "Buffer write failed",               /* PS3DRIVE_ERR_BUFFER_WRITE */
    "Seek operation failed",             /* PS3DRIVE_ERR_SEEK_FAILED */
};

#define ERROR_STRING_COUNT \
    (sizeof(error_strings) / sizeof(error_strings[0]))

const char *ps3drive_error_string(ps3drive_error_t error)
{
    int idx = -error;
    if (idx >= 0 && idx < (int)ERROR_STRING_COUNT) {
        return error_strings[idx];
    }
    return "Unknown error";
}

const char *ps3drive_get_error(ps3drive_t *handle)
{
    if (handle == NULL) {
        return "NULL handle";
    }
    if (handle->error_msg[0] != '\0') {
        return handle->error_msg;
    }
    return ps3drive_error_string(handle->last_error);
}

void ps3drive_set_error(ps3drive_t *handle, ps3drive_error_t err,
                         const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ps3drive_set_error_v(handle, err, fmt, args);
    va_end(args);
}

void ps3drive_set_error_v(ps3drive_t *handle, ps3drive_error_t err,
                           const char *fmt, va_list args)
{
    if (handle == NULL) {
        return;
    }

    handle->last_error = err;

    if (fmt != NULL) {
        vsnprintf(handle->error_msg, sizeof(handle->error_msg), fmt, args);
    } else {
        handle->error_msg[0] = '\0';
    }
}

void ps3drive_clear_error(ps3drive_t *handle)
{
    if (handle != NULL) {
        handle->last_error = PS3DRIVE_OK;
        handle->error_msg[0] = '\0';
    }
}

/* ============================================================================
 * Debug Output
 * ============================================================================ */

void ps3drive_debug(ps3drive_t *handle, int level, const char *fmt, ...)
{
    va_list args;

    if (handle == NULL || handle->verbose < level) {
        return;
    }

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void ps3drive_debug_hex(ps3drive_t *handle, const char *prefix,
                         const uint8_t *data, size_t len)
{
    size_t i;

    if (handle == NULL || handle->verbose < 3 || data == NULL) {
        return;
    }

    fprintf(stderr, "%s (%zu bytes):\n", prefix, len);
    for (i = 0; i < len; i++) {
        if (i > 0 && (i % 16) == 0) {
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "%02x ", data[i]);
    }
    fprintf(stderr, "\n");
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

uint8_t ps3drive_checksum(const uint8_t *data, int len)
{
    unsigned short sum = 0;
    int i;

    if (data == NULL) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        sum += data[i];
    }

    /* One's complement (NOT two's complement!) */
    return (uint8_t)(~sum);
}

/* ============================================================================
 * Drive Type Lookup
 * ============================================================================ */

const char *ps3drive_type_string(ps3drive_type_t drive_type)
{
    size_t i;

    for (i = 0; i < PS3DRIVE_ID_TABLE_SIZE; i++) {
        if (PS3DRIVE_ID_TABLE[i].type_id == (uint64_t)drive_type) {
            return PS3DRIVE_ID_TABLE[i].product_id;
        }
    }

    return "Unknown PS3 Drive";
}

uint64_t ps3drive_lookup_type(const char *product_id)
{
    size_t i;

    if (product_id == NULL) {
        return 0;
    }

    for (i = 0; i < PS3DRIVE_ID_TABLE_SIZE; i++) {
        if (strncmp(PS3DRIVE_ID_TABLE[i].product_id, product_id,
                    strlen(PS3DRIVE_ID_TABLE[i].product_id)) == 0) {
            return PS3DRIVE_ID_TABLE[i].type_id;
        }
    }

    return 0;
}

/* ============================================================================
 * Drive Management
 * ============================================================================ */

ps3drive_error_t ps3drive_open(const char *device_path, ps3drive_t **handle)
{
    ps3drive_t *dev = NULL;
    ps3drive_error_t ret;

    if (device_path == NULL || handle == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    *handle = NULL;

    /* Allocate handle */
    dev = (ps3drive_t *)calloc(1, sizeof(ps3drive_t));
    if (dev == NULL) {
        return PS3DRIVE_ERR_OUT_OF_MEMORY;
    }

    dev->sg_fd = -1;
    dev->verbose = 0;
    dev->noisy = 0;

#ifdef _WIN32
    /*
     * Use SPT indirect (double-buffered) interface on Windows.
     * The direct interface (SPTD) can have issues with vendor-specific
     * commands on some systems. The indirect interface is more compatible.
     */
    scsi_pt_win32_direct(1);
#endif

    /* Open the device */
    dev->sg_fd = scsi_pt_open_device(device_path, 0 /* rw */, dev->verbose);
    if (dev->sg_fd < 0) {
        ps3drive_set_error(dev, PS3DRIVE_ERR_OPEN_FAILED,
                           "Failed to open device '%s': %s",
                           device_path, safe_strerror(-dev->sg_fd));
        ret = PS3DRIVE_ERR_OPEN_FAILED;
        goto error;
    }

    *handle = dev;
    return PS3DRIVE_OK;

error:
    if (dev != NULL) {
        if (dev->sg_fd >= 0) {
            scsi_pt_close_device(dev->sg_fd);
        }
        ps3drive_secure_zero(dev, sizeof(*dev));
        free(dev);
    }
    return ret;
}

ps3drive_error_t ps3drive_close(ps3drive_t *handle)
{
    if (handle == NULL) {
        return PS3DRIVE_OK;
    }

    /* Close SCSI device */
    if (handle->sg_fd >= 0) {
        scsi_pt_close_device(handle->sg_fd);
        handle->sg_fd = -1;
    }

    /* Clear authentication state */
    handle->authenticated = false;
    handle->sac_exchanged = false;

    /* Cleanup crypto if initialized */
    ps3drive_crypto_cleanup();

    /* Securely zero sensitive data */
    ps3drive_secure_zero(handle->aes_key, sizeof(handle->aes_key));
    ps3drive_secure_zero(handle->aes_iv, sizeof(handle->aes_iv));
    ps3drive_secure_zero(handle, sizeof(*handle));

    free(handle);

    return PS3DRIVE_OK;
}

ps3drive_error_t ps3drive_eject(ps3drive_t *handle)
{
    int ret;

    if (handle == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    /* START STOP UNIT with LoEj=1, Start=0 (eject) */
    ret = sg_ll_start_stop_unit(handle->sg_fd,
                                 0,    /* immed */
                                 0,    /* fl_num (power condition) */
                                 0,    /* power_cond */
                                 0,    /* fl (format layer number) */
                                 1,    /* loej (load/eject) */
                                 0,    /* start */
                                 handle->noisy,
                                 handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_SCSI_FAILED,
                           "START STOP UNIT (eject) failed: %d", ret);
        return PS3DRIVE_ERR_SCSI_FAILED;
    }

    return PS3DRIVE_OK;
}

/* ============================================================================
 * Drive Information
 * ============================================================================ */

ps3drive_error_t ps3drive_get_info(ps3drive_t *handle, ps3drive_info_t *info)
{
    if (handle == NULL || info == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    memcpy(info, &handle->info, sizeof(*info));
    return PS3DRIVE_OK;
}

ps3drive_error_t ps3drive_is_ps3_drive(const char *device_path, bool *is_ps3)
{
    ps3drive_t *handle = NULL;
    ps3drive_error_t ret;

    if (device_path == NULL || is_ps3 == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    *is_ps3 = false;

    ret = ps3drive_open(device_path, &handle);
    if (ret == PS3DRIVE_OK) {
        *is_ps3 = true;
        ps3drive_close(handle);
    } else if (ret == PS3DRIVE_ERR_NOT_PS3_DRIVE) {
        /* Not an error, just not a PS3 drive */
        ret = PS3DRIVE_OK;
    }

    return ret;
}

ps3drive_error_t ps3drive_get_type(ps3drive_t *handle, ps3drive_type_t *drive_type)
{
    if (handle == NULL || drive_type == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    *drive_type = (ps3drive_type_t)handle->drive_type;
    return PS3DRIVE_OK;
}

ps3drive_error_t ps3drive_get_total_sectors(ps3drive_t *handle,
                                             uint32_t *total_sectors)
{
    int ret;
    uint8_t resp[8];
    uint32_t last_lba;

    if (handle == NULL || total_sectors == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    if (handle->total_sectors_valid) {
        *total_sectors = handle->total_sectors;
        return PS3DRIVE_OK;
    }

    /* READ CAPACITY 10 returns 8 bytes: 4 bytes LBA + 4 bytes block size */
    ret = sg_ll_readcap_10(handle->sg_fd, 0 /* pmi */, 0 /* lba */,
                            resp, sizeof(resp),
                            handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_SCSI_FAILED,
                           "READ CAPACITY failed: %d", ret);
        return PS3DRIVE_ERR_SCSI_FAILED;
    }

    /* Parse response: first 4 bytes = last LBA (big-endian) */
    last_lba = sg_get_unaligned_be32(resp);

    /* last_lba is the LBA of the last sector, total = last_lba + 1 */
    handle->total_sectors = last_lba + 1;
    handle->total_sectors_valid = true;

    *total_sectors = handle->total_sectors;
    return PS3DRIVE_OK;
}

void ps3drive_set_verbose(ps3drive_t *handle, int level)
{
    if (handle != NULL) {
        handle->verbose = level;
        handle->noisy = (level >= 1) ? 1 : 0;
    }
}

ps3drive_error_t ps3drive_get_disc_presence(ps3drive_t *handle,
                                             bool *disc_present)
{
    int ret;
    uint8_t buffer[8];

    if (handle == NULL || disc_present == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    *disc_present = false;

    ret = sg_ll_ps3_get_event_status_notification(handle->sg_fd,
                                                   1,    /* polled */
                                                   0x10, /* media class */
                                                   buffer, sizeof(buffer),
                                                   handle->noisy,
                                                   handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_SCSI_FAILED,
                           "GET EVENT STATUS NOTIFICATION failed: %d", ret);
        return PS3DRIVE_ERR_SCSI_FAILED;
    }

    /*
     * Response buffer[5] contains the media event code:
     *   0x00 = No change / No event
     *   0x01 = Eject requested
     *   0x02 = New media (disc present)
     *   0x03 = Media removal
     *   0x04 = Media changed
     */
    *disc_present = (buffer[5] == 0x02);

    return PS3DRIVE_OK;
}

/* ============================================================================
 * Authentication
 * ============================================================================ */

ps3drive_error_t ps3drive_authenticate(ps3drive_t *handle)
{
    ps3drive_error_t ret;

    if (handle == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    if (handle->authenticated) {
        ps3drive_debug(handle, 2, "Drive already authenticated\n");
        return PS3DRIVE_OK;
    }

    ps3drive_debug(handle, 1, "Starting BD authentication...\n");

    ret = ps3drive_auth_bd_internal(handle, PS3DRIVE_AUTH_KEY1,
                                     PS3DRIVE_AUTH_KEY2);
    if (ret != PS3DRIVE_OK) {
        return ret;
    }

    handle->authenticated = true;
    ps3drive_debug(handle, 1, "BD authentication successful\n");

    return PS3DRIVE_OK;
}

/* ============================================================================
 * SAC Key Exchange
 * ============================================================================ */

ps3drive_error_t ps3drive_sac_key_exchange(ps3drive_t *handle,
                                            uint8_t *aes_key,
                                            uint8_t *aes_iv)
{
    ps3drive_error_t ret;

    if (handle == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    if (handle->sac_exchanged) {
        /* Already done, just return the keys if requested */
        if (aes_key != NULL) {
            memcpy(aes_key, handle->aes_key, 16);
        }
        if (aes_iv != NULL) {
            memcpy(aes_iv, handle->aes_iv, 16);
        }
        return PS3DRIVE_OK;
    }

    /*
     * SACD initialization sequence (must be in this exact order):
     * 1. BD authentication (caller must call ps3drive_authenticate() first)
     * 2. Set CD speed
     * 3. Select SACD layer (for hybrid discs)
     * 4. INQUIRY (validate it's a PS3 drive)
     * 5. GET CONFIGURATION (check if SACD feature is enabled)
     * 6. SAC key exchange
     */

    /* 1. Verify BD authentication was done */
    if (!handle->authenticated) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_NOT_AUTHENTICATED,
                           "BD authentication required before SAC key exchange");
        return PS3DRIVE_ERR_NOT_AUTHENTICATED;
    }

    /* 2. Set CD speed to maximum */
    sg_ll_set_cd_speed(handle->sg_fd, 0, 0xffff, 0, handle->noisy,
                       handle->verbose);

    /* 3. Select SACD layer (for hybrid discs) */
    ret = ps3drive_select_sacd_layer(handle);
    if (ret != PS3DRIVE_OK && ret != PS3DRIVE_ERR_NOT_HYBRID) {
        return ret;
    }

    /* 4. Get drive info via INQUIRY */
    ret = ps3drive_inquiry_internal(handle);
    if (ret != PS3DRIVE_OK) {
        return ret;
    }

    /* Check if this is a PS3 drive */
    handle->drive_type = ps3drive_lookup_type(handle->info.product_id);
    if (handle->drive_type == 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_NOT_PS3_DRIVE,
                           "'%s' is not a recognized PS3 drive",
                           handle->info.product_id);
        return PS3DRIVE_ERR_NOT_PS3_DRIVE;
    }

    /* 5. Check if SACD feature is enabled (GET CONFIGURATION) */
    ret = ps3drive_check_sacd_feature_internal(handle);
    if (ret != PS3DRIVE_OK) {
        return ret;
    }

    /* 6. Perform SAC key exchange */
    ret = ps3drive_sac_exchange_internal(handle, handle->aes_key,
                                          handle->aes_iv);
    if (ret != PS3DRIVE_OK) {
        return ret;
    }

    handle->sac_exchanged = true;

    /* Copy keys out if requested */
    if (aes_key != NULL) {
        memcpy(aes_key, handle->aes_key, 16);
    }
    if (aes_iv != NULL) {
        memcpy(aes_iv, handle->aes_iv, 16);
    }

    return PS3DRIVE_OK;
}

ps3drive_error_t ps3drive_is_authenticated(ps3drive_t *handle,
                                            bool *authenticated)
{
    if (handle == NULL || authenticated == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    *authenticated = handle->authenticated;
    return PS3DRIVE_OK;
}

/* ============================================================================
 * Layer Selection
 * ============================================================================ */

ps3drive_error_t ps3drive_select_sacd_layer(ps3drive_t *handle)
{
    uint8_t resp[16];
    int ret;
    int i, layer_count;
    uint16_t layer_type;
    bool found_sacd = false;

    if (handle == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    /* Read disc structure format 0x90 = Returns the list of recognized Format-layers
     * This is the correct format for hybrid disc detection */
    ret = sg_ll_ps3_read_disc_structure(handle->sg_fd,
                                         0,    /* media_type */
                                         0,    /* address */
                                         0,    /* layer_nr */
                                         0x90, /* format (format-layers list) */
                                         0,    /* agid */
                                         0,    /* ctrl */
                                         resp, sizeof(resp),
                                         1, handle->verbose);
    if (ret == 0) {
        int len = sg_get_unaligned_be16(resp);
        if (len > 5) {
            layer_count = resp[4];

            ps3drive_debug(handle, 2, "Disc has %d layers\n", layer_count);

            if (layer_count < 2) {
                /* Not a hybrid disc */
                ps3drive_debug(handle, 2, "Not a hybrid disc (single layer)\n");
                handle->is_hybrid = false;
                handle->hybrid_checked = true;
                return PS3DRIVE_OK;
            }

            /* Iterate through layers looking for SACD layer (0x10) */
            for (i = 0; i < layer_count; i++) {
                layer_type = sg_get_unaligned_be16(resp + 6 + i * sizeof(uint16_t));

                ps3drive_debug(handle, 2, "Layer %d: type=0x%04x\n", i, layer_type);

                if (layer_type == 0x10) {
                    /* Found SACD layer - select it */
                    found_sacd = true;

                    /* START STOP UNIT to select this layer
                     * Parameters from original: immed=0, fl_num=i, power_cond=0,
                     * fl=1, loej=1, start=1 */
                    ret = sg_ll_start_stop_unit(handle->sg_fd,
                                                 0,    /* immed */
                                                 i,    /* fl_num (layer number) */
                                                 0,    /* power_cond */
                                                 1,    /* fl (select layer) */
                                                 1,    /* loej */
                                                 1,    /* start */
                                                 handle->noisy, handle->verbose);
                    if (ret != 0) {
                        ps3drive_set_error(handle, PS3DRIVE_ERR_LAYER_SELECT,
                                           "Failed to select SACD layer %d", i);
                        return PS3DRIVE_ERR_LAYER_SELECT;
                    }

                    ps3drive_debug(handle, 1, "Selected SACD layer %d\n", i);
                    break;
                }
            }

            handle->is_hybrid = found_sacd;
            handle->hybrid_checked = true;
            return PS3DRIVE_OK;
        }
    }

    /* READ DISC STRUCTURE failed or returned unexpected data
     * This may happen on non-hybrid discs - not an error */
    ps3drive_debug(handle, 2, "No hybrid layers found (format 0x90 failed or empty)\n");
    handle->is_hybrid = false;
    handle->hybrid_checked = true;

    return PS3DRIVE_OK;
}

ps3drive_error_t ps3drive_is_hybrid_disc(ps3drive_t *handle, bool *is_hybrid)
{
    ps3drive_error_t ret;

    if (handle == NULL || is_hybrid == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    if (!handle->hybrid_checked) {
        /* Need to check first */
        ret = ps3drive_select_sacd_layer(handle);
        if (ret != PS3DRIVE_OK && ret != PS3DRIVE_ERR_NOT_HYBRID) {
            return ret;
        }
    }

    *is_hybrid = handle->is_hybrid;
    return PS3DRIVE_OK;
}

/* SACD flag values for D7 command */
#define PS3DRIVE_SACD_FLAG_ENABLE   0xFF
#define PS3DRIVE_SACD_FLAG_DISABLE  0x53

ps3drive_error_t ps3drive_enable_sacd_mode(ps3drive_t *handle, bool enable)
{
    int ret;
    uint8_t flag;

    if (handle == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    flag = enable ? PS3DRIVE_SACD_FLAG_ENABLE : PS3DRIVE_SACD_FLAG_DISABLE;

    ps3drive_debug(handle, 2, "Setting SACD flag to 0x%02x\n", flag);

    /*
     * Use the proprietary PS3 D7 command to set drive state flag.
     * Flag values:
     *   0xFF = Enable SACD mode
     *   0x53 = Disable SACD mode
     */
    ret = sg_ll_ps3_d7_set(handle->sg_fd, flag,
                            handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_SCSI_FAILED,
                           "D7 SET command failed: %d", ret);
        return PS3DRIVE_ERR_SCSI_FAILED;
    }

    ps3drive_debug(handle, 1, "SACD mode %s (flag=0x%02x)\n",
                   enable ? "enabled" : "disabled", flag);

    return PS3DRIVE_OK;
}

/* ============================================================================
 * Reading and Decryption
 * ============================================================================ */

uint32_t ps3drive_read_sectors(ps3drive_t *handle,
                                uint32_t start_sector,
                                uint32_t num_sectors,
                                void *buffer)
{
    int ret;
    uint32_t sectors_read = 0;
    uint8_t *buf_ptr = (uint8_t *)buffer;

    if (handle == NULL || buffer == NULL || num_sectors == 0) {
        return 0;
    }

    /* READ12 supports a maximum of 32 sectors per command */
    while (sectors_read < num_sectors) {
        uint32_t chunk = num_sectors - sectors_read;
        if (chunk > 32) {
            chunk = 32;
        }

        ret = sg_ll_ps3_read12(handle->sg_fd, start_sector + sectors_read,
                               chunk, buf_ptr, PS3DRIVE_SECTOR_SIZE,
                               handle->noisy, handle->verbose);
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_READ_FAILED,
                               "Read failed at sector %u: %d",
                               start_sector + sectors_read, ret);
            return sectors_read;
        }

        buf_ptr += chunk * PS3DRIVE_SECTOR_SIZE;
        sectors_read += chunk;
    }

    return sectors_read;
}

ps3drive_error_t ps3drive_decrypt(ps3drive_t *handle,
                                   uint8_t *buffer,
                                   uint32_t num_sectors)
{
    uint32_t i;
    int ret;

    if (handle == NULL || buffer == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    if (num_sectors == 0) {
        return PS3DRIVE_OK;
    }

    if (!handle->sac_exchanged) {
        return PS3DRIVE_ERR_NOT_AUTHENTICATED;
    }

    /* Decrypt each sector */
    for (i = 0; i < num_sectors; i++) {
        ret = ps3drive_aes128_cbc_decrypt(handle->aes_key,
                                           handle->aes_iv,
                                           buffer + (i * PS3DRIVE_SECTOR_SIZE),
                                           buffer + (i * PS3DRIVE_SECTOR_SIZE),
                                           PS3DRIVE_SECTOR_SIZE);
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_DECRYPT_FAILED,
                               "Decryption failed for sector %u", i);
            return PS3DRIVE_ERR_DECRYPT_FAILED;
        }
    }

    return PS3DRIVE_OK;
}

/* ============================================================================
 * Print Features (stub - implemented in ps3drive_info.c)
 * ============================================================================ */

ps3drive_error_t ps3drive_print_features(ps3drive_t *handle, int verbose)
{
    if (handle == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    printf("PS3 Drive Information:\n");
    printf("  Vendor:   %s\n", handle->info.vendor_id);
    printf("  Product:  %s\n", handle->info.product_id);
    printf("  Revision: %s\n", handle->info.revision);
    printf("  Type:     %s (0x%016llx)\n",
           ps3drive_type_string((ps3drive_type_t)handle->drive_type),
           (unsigned long long)handle->drive_type);
    printf("  SACD:     %s\n",
           handle->info.has_sacd_feature ? "Yes" : "No");
    printf("  Hybrid:   %s\n",
           handle->info.has_hybrid_support ? "Yes" : "No");

    (void)verbose;
    return PS3DRIVE_OK;
}
