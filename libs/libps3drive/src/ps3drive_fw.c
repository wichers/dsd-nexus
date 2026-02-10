/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Firmware update implementation.
 * This file implements firmware update operations for PS3 BluRay drives.
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
#include "ps3drive_keys.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <libsautil/time.h>

#include <sg_lib.h>
#include <sg_cmds_basic.h>
#include <sg_cmds_extra.h>
#include <sg_cmds_ps3.h>

/* ============================================================================
 * Sense Code Messages
 * ============================================================================ */

const char *ps3drive_sense_message(unsigned int req_sense)
{
    size_t i;

    for (i = 0; i < PS3DRIVE_SENSE_TABLE_SIZE; i++) {
        if (PS3DRIVE_SENSE_TABLE[i].req_sense == req_sense) {
            return PS3DRIVE_SENSE_TABLE[i].message;
        }
    }

    return "unknown";
}

/* ============================================================================
 * Firmware Update
 * ============================================================================ */

ps3drive_error_t ps3drive_update_firmware(ps3drive_t *handle,
                                           const uint8_t *firmware,
                                           size_t firmware_len,
                                           uint64_t h_id,
                                           int timeout_sec)
{
    unsigned int offset;
    unsigned int req_sense;
    unsigned int timeout_usec;
    int ret;
    size_t chunk_size;

    if (handle == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    if (firmware == NULL || firmware_len == 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_INVALID_ARG,
                           "Invalid firmware specified");
        return PS3DRIVE_ERR_INVALID_ARG;
    }

    /* Use default timeout if not specified */
    if (timeout_sec <= 0) {
        timeout_sec = 60;
    }

    /* If h_id not specified, use drive type */
    if (h_id == 0) {
        h_id = handle->drive_type;
    }

    ps3drive_debug(handle, 1, "Firmware update:\n");
    ps3drive_debug(handle, 1, "  Length: %zu bytes\n", firmware_len);
    ps3drive_debug(handle, 1, "  H_ID:   0x%016llx\n", (unsigned long long)h_id);
    ps3drive_debug(handle, 1, "  Timeout: %d seconds\n", timeout_sec);

    /* ========================================================================
     * Step 1: INQUIRY to verify drive
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== INQUIRY (0x12) ===\n");

    ret = ps3drive_inquiry_internal(handle);
    if (ret != PS3DRIVE_OK) {
        return ret;
    }

    ps3drive_debug(handle, 1, "  Vendor:  %s\n", handle->info.vendor_id);
    ps3drive_debug(handle, 1, "  Product: %s\n", handle->info.product_id);
    ps3drive_debug(handle, 1, "  Rev:     %s\n", handle->info.revision);

    /* ========================================================================
     * Step 2: Eject medium using START STOP UNIT
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== START STOP UNIT (0x1B) - Eject ===\n");

    /* START STOP UNIT: immed=0, fl_num=0, power_cond=0, noflush=0, loej=1, start=0 */
    ret = sg_ll_start_stop_unit(handle->sg_fd,
                                 0,    /* immed */
                                 0,    /* fl_num (power condition modifier) */
                                 0,    /* power_cond */
                                 0,    /* noflush__fl */
                                 1,    /* loej (load/eject) */
                                 0,    /* start (0=eject) */
                                 handle->noisy,
                                 handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_FW_UPDATE,
                           "START STOP UNIT (eject) failed: %d", ret);
        return PS3DRIVE_ERR_FW_UPDATE;
    }

    /* ========================================================================
     * Step 3: TEST UNIT READY - verify drive is ready (ignore medium not present)
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== TEST UNIT READY (0x00) - Pre-check ===\n");

    req_sense = 0;
    ret = sg_ll_ps3_test_unit_ready(handle->sg_fd, &req_sense,
                                     handle->noisy, handle->verbose);
    if (ret != 0 && req_sense != 0x23a00) {
        /* 0x23a00 = medium not present, which is expected after eject */
        ps3drive_set_error(handle, PS3DRIVE_ERR_FW_UPDATE,
                           "TEST UNIT READY failed: req_sense=0x%06x", req_sense);
        return PS3DRIVE_ERR_FW_UPDATE;
    }

    /* ========================================================================
     * Step 4: Write firmware in chunks using WRITE BUFFER
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== WRITE BUFFER (0x3B) ===\n");

    offset = 0;
    while (offset < firmware_len) {
        chunk_size = firmware_len - offset;
        if (chunk_size > PS3DRIVE_MAX_WRITE_LEN) {
            chunk_size = PS3DRIVE_MAX_WRITE_LEN;
        }

        ps3drive_debug(handle, 2, "Writing offset 0x%08x, size 0x%04zx\n",
                       offset, chunk_size);

        /* mode 0x7: download microcode with offsets and save */
        ret = sg_ll_write_buffer(handle->sg_fd,
                                  7,    /* mode: download microcode with offsets */
                                  0,    /* buffer id */
                                  offset,
                                  (void *)(firmware + offset),
                                  (int)chunk_size,
                                  handle->noisy,
                                  handle->verbose);
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_FW_UPDATE,
                               "WRITE BUFFER failed at offset 0x%08x: %d",
                               offset, ret);
            return PS3DRIVE_ERR_FW_UPDATE;
        }

        offset += (unsigned int)chunk_size;

        /* Progress indication */
        if (handle->verbose >= 1) {
            fprintf(stderr, "\rProgress: %3u%%",
                    (unsigned int)(100 * offset / firmware_len));
            fflush(stderr);
        }
    }

    if (handle->verbose >= 1) {
        fprintf(stderr, "\rProgress: 100%%\n");
    }

    /* ========================================================================
     * Step 5: Poll with TEST UNIT READY until complete or timeout
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== TEST UNIT READY (0x00) - Polling ===\n");

    timeout_usec = (unsigned int)timeout_sec * 1000 * 1000;

    while (timeout_usec > 0) {
        ps3drive_debug(handle, 2, "=== TEST UNIT READY (0x00) ===\n");

        req_sense = 0;
        ret = sg_ll_ps3_test_unit_ready(handle->sg_fd, &req_sense,
                                         handle->noisy, handle->verbose);

        ps3drive_debug(handle, 1, "req_sense 0x%06x (%s)\n",
                       req_sense, ps3drive_sense_message(req_sense));

        /* Check for success: sense code 0x23a00 means medium not present (success) */
        if (ret != 0 && req_sense == 0x23a00) {
            break;
        }

        /* Check for fatal errors */
        if (req_sense == 0x43e01 || req_sense == 0x52400 ||
            req_sense == 0x52600) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_FW_UPDATE,
                               "Firmware update failed: %s",
                               ps3drive_sense_message(req_sense));
            return PS3DRIVE_ERR_FW_UPDATE;
        }

        /* Sleep 100ms and decrement timeout */
        sa_usleep(100000);
        timeout_usec -= 100000;
    }

    if (timeout_usec == 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_FW_UPDATE,
                           "Firmware update timed out after %d seconds",
                           timeout_sec);
        return PS3DRIVE_ERR_FW_UPDATE;
    }

    /* ========================================================================
     * Step 6: Verify update by re-reading INQUIRY
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== INQUIRY (0x12) - Post Update ===\n");

    ret = ps3drive_inquiry_internal(handle);
    if (ret != PS3DRIVE_OK) {
        ps3drive_debug(handle, 1, "Post-update INQUIRY failed\n");
    } else {
        ps3drive_debug(handle, 1, "\nFirmware update complete:\n");
        ps3drive_debug(handle, 1, "  Vendor:  %s\n", handle->info.vendor_id);
        ps3drive_debug(handle, 1, "  Product: %s\n", handle->info.product_id);
        ps3drive_debug(handle, 1, "  Rev:     %s\n", handle->info.revision);
    }

    ps3drive_debug(handle, 1, "Firmware update completed successfully\n");
    return PS3DRIVE_OK;
}
