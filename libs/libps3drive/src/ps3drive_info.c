/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Drive information and feature detection.
 * This file implements INQUIRY, GET CONFIGURATION, and feature
 * detection functions for PS3 BluRay drives.
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

#include <string.h>
#include <stdio.h>

#include <sg_lib.h>
#include <sg_cmds_basic.h>
#include <sg_cmds_mmc.h>
#include <sg_cmds_ps3.h>

/* ============================================================================
 * INQUIRY
 * ============================================================================ */

ps3drive_error_t ps3drive_inquiry_internal(ps3drive_t *handle)
{
    struct sg_simple_inquiry_resp inq_resp;
    int ret;

    if (handle == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    memset(&inq_resp, 0, sizeof(inq_resp));

    ret = sg_simple_inquiry(handle->sg_fd, &inq_resp,
                             handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_SCSI_FAILED,
                           "INQUIRY failed: %d", ret);
        return PS3DRIVE_ERR_SCSI_FAILED;
    }

    /* Copy vendor ID (8 bytes max) */
    memset(handle->info.vendor_id, 0, sizeof(handle->info.vendor_id));
    strncpy(handle->info.vendor_id, inq_resp.vendor,
            sizeof(handle->info.vendor_id) - 1);

    /* Trim trailing spaces */
    for (int i = (int)strlen(handle->info.vendor_id) - 1; i >= 0; i--) {
        if (handle->info.vendor_id[i] == ' ') {
            handle->info.vendor_id[i] = '\0';
        } else {
            break;
        }
    }

    /* Copy product ID (16 bytes max) */
    memset(handle->info.product_id, 0, sizeof(handle->info.product_id));
    strncpy(handle->info.product_id, inq_resp.product,
            sizeof(handle->info.product_id) - 1);

    /* Trim trailing spaces */
    for (int i = (int)strlen(handle->info.product_id) - 1; i >= 0; i--) {
        if (handle->info.product_id[i] == ' ') {
            handle->info.product_id[i] = '\0';
        } else {
            break;
        }
    }

    /* Copy revision (4 bytes max) */
    memset(handle->info.revision, 0, sizeof(handle->info.revision));
    strncpy(handle->info.revision, inq_resp.revision,
            sizeof(handle->info.revision) - 1);

    ps3drive_debug(handle, 2, "INQUIRY: Vendor='%s' Product='%s' Rev='%s'\n",
                   handle->info.vendor_id, handle->info.product_id,
                   handle->info.revision);

    return PS3DRIVE_OK;
}

/* ============================================================================
 * Feature Detection
 * ============================================================================ */

ps3drive_error_t ps3drive_check_sacd_feature_internal(ps3drive_t *handle)
{
    uint8_t resp[2051];
    int ret;

    if (handle == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    /* GET CONFIGURATION for specific feature 0xFF41 (SACD)
     * RT=0: all features starting from the specified feature
     * This matches the original implementation */
    ret = sg_ll_get_config(handle->sg_fd,
                            0,              /* RT: all features from starting */
                            PS3DRIVE_SACD_FEATURE,  /* starting feature 0xFF41 */
                            resp, sizeof(resp),
                            handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_NO_SACD_FEATURE,
                           "SACD feature check failed: %d", ret);
        return PS3DRIVE_ERR_NO_SACD_FEATURE;
    }

    /* Check response format from original:
     * (resp[0] & 1) == 0 : check header flags
     * (resp[10] & 1) != 0 : feature is current (active)
     *
     * If SACD feature is recognized and current, we need to configure
     * the drive with MODE SENSE/SELECT sequence */
    if ((resp[0] & 1) == 0 && (resp[10] & 1) != 0) {
        /* SACD feature is recognized, now configure it */
        ps3drive_debug(handle, 1, "SACD feature detected, configuring mode...\n");

        /* MODE SENSE (10) - read mode page 0x03 */
        ret = sg_ll_mode_sense10(handle->sg_fd,
                                  0,      /* llbaa */
                                  1,      /* dbd: disable block descriptors */
                                  0,      /* pc: current values */
                                  0x03,   /* page code */
                                  0,      /* sub-page code */
                                  resp, 16,
                                  handle->noisy, handle->verbose);
        if (ret != 0) {
            ps3drive_debug(handle, 1, "MODE SENSE failed: %d\n", ret);
            ps3drive_set_error(handle, PS3DRIVE_ERR_NO_SACD_FEATURE,
                               "MODE SENSE for SACD failed: %d", ret);
            return PS3DRIVE_ERR_NO_SACD_FEATURE;
        }

        /* Debug: show mode sense response */
        ps3drive_debug(handle, 1, "MODE SENSE resp[11] = 0x%02x (need 0x02 for MODE SELECT)\n", resp[11]);

        /* Check if mode page indicates SACD mode needs configuration (resp[11] == 2)
         * If resp[11] == 3, SACD mode is already active - no MODE SELECT needed */
        if (resp[11] == 2) {
            ps3drive_debug(handle, 1, "SACD mode available, sending MODE SELECT...\n");

            /* MODE SELECT (10) - configure SACD mode
             * Parameters from original:
             * pf=0, reserved=7, sp=0, naca=1, flag=1, param_len=2051 */
            ret = sg_ll_ps3_mode_select10(handle->sg_fd,
                                           0,       /* pf: page format */
                                           7,       /* reserved */
                                           0,       /* sp: save pages */
                                           1,       /* naca */
                                           1,       /* flag */
                                           resp, 2051,
                                           handle->noisy, handle->verbose);
            if (ret != 0) {
                ps3drive_debug(handle, 1, "MODE SELECT failed: %d\n", ret);
                ps3drive_set_error(handle, PS3DRIVE_ERR_NO_SACD_FEATURE,
                                   "MODE SELECT for SACD failed: %d", ret);
                return PS3DRIVE_ERR_NO_SACD_FEATURE;
            }

            ps3drive_debug(handle, 1, "SACD mode configured successfully\n");
        } else {
            ps3drive_debug(handle, 1, "SACD mode already active (resp[11]=0x%02x)\n", resp[11]);
        }

        handle->info.has_sacd_feature = 1;
        ps3drive_debug(handle, 1, "SACD feature is active\n");
        return PS3DRIVE_OK;
    }

    ps3drive_set_error(handle, PS3DRIVE_ERR_NO_SACD_FEATURE,
                       "SACD feature not available or not current");
    return PS3DRIVE_ERR_NO_SACD_FEATURE;
}
