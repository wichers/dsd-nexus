/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Drive pairing (P-Block, S-Block, HRL) implementation.
 * This file implements the drive pairing operations that can restore
 * a PS3 BluRay drive's ability to play BD movies after corruption.
 * Pairing sequence (order is critical):
 *   1. Write P-Block to buffer 2
 *   2. Authenticate drive with Storage Manager
 *   3. Write S-Block to buffer 3
 *   4. Write HRL to buffer 4
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

#include <sg_lib.h>
#include <sg_cmds_basic.h>
#include <sg_cmds_extra.h>
#include <sg_cmds_ps3.h>

/* ============================================================================
 * Pairing Context Management
 * ============================================================================ */

ps3drive_error_t ps3drive_pairing_create_default(ps3drive_pairing_ctx_t **ctx)
{
    ps3drive_pairing_ctx_t *new_ctx;

    if (ctx == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    *ctx = NULL;

    new_ctx = (ps3drive_pairing_ctx_t *)calloc(1, sizeof(ps3drive_pairing_ctx_t));
    if (new_ctx == NULL) {
        return PS3DRIVE_ERR_OUT_OF_MEMORY;
    }

    /* Set P-Block from embedded data */
    memcpy(new_ctx->pblock, PS3DRIVE_PBLOCK_KEY, PS3DRIVE_PBLOCK_SIZE);
    new_ctx->pblock_valid = true;

    /* Set S-Block from embedded data */
    memcpy(new_ctx->sblock, PS3DRIVE_SBLOCK_KEY, PS3DRIVE_SBLOCK_SIZE);
    new_ctx->sblock_valid = true;

    /* Set default HRL - 84 byte header, rest zeros, total 32768 bytes */
    memset(new_ctx->hrl, 0, PS3DRIVE_HRL_SIZE);
    memcpy(new_ctx->hrl, PS3DRIVE_DEFAULT_HRL, sizeof(PS3DRIVE_DEFAULT_HRL));
    new_ctx->hrl_len = PS3DRIVE_DEFAULT_HRL_SIZE;
    new_ctx->hrl_valid = true;

    *ctx = new_ctx;
    return PS3DRIVE_OK;
}

void ps3drive_pairing_free(ps3drive_pairing_ctx_t *ctx)
{
    if (ctx != NULL) {
        ps3drive_secure_zero(ctx, sizeof(*ctx));
        free(ctx);
    }
}

/* ============================================================================
 * Buffer Operations
 * ============================================================================ */

ps3drive_error_t ps3drive_enable_buffer_write(ps3drive_t *handle, int buffer_id)
{
    int ret;

    if (handle == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    /* Use PS3-specific write mode to enable buffer */
    ret = sg_ll_ps3_write_mode(handle->sg_fd, buffer_id,
                                handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_BUFFER_WRITE,
                           "Failed to enable buffer %d write: %d",
                           buffer_id, ret);
        return PS3DRIVE_ERR_BUFFER_WRITE;
    }

    ps3drive_debug(handle, 2, "Enabled write for buffer %d\n", buffer_id);
    return PS3DRIVE_OK;
}

ps3drive_error_t ps3drive_write_buffer_internal(ps3drive_t *handle,
                                                 int buffer_id,
                                                 const uint8_t *data,
                                                 size_t len)
{
    int ret;
    size_t offset = 0;
    size_t chunk_size;

    if (handle == NULL || data == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    if (len == 0) {
        return PS3DRIVE_OK;
    }

    /* Write in chunks of max 32KB */
    while (offset < len) {
        chunk_size = len - offset;
        if (chunk_size > PS3DRIVE_MAX_WRITE_LEN) {
            chunk_size = PS3DRIVE_MAX_WRITE_LEN;
        }

        ret = sg_ll_write_buffer(handle->sg_fd,
                                  5,         /* mode: download microcode */
                                  buffer_id, /* buffer id */
                                  (unsigned int)offset,
                                  (void *)(data + offset),
                                  (int)chunk_size,
                                  handle->noisy,
                                  handle->verbose);
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_BUFFER_WRITE,
                               "WRITE BUFFER failed at offset %zu: %d",
                               offset, ret);
            return PS3DRIVE_ERR_BUFFER_WRITE;
        }

        offset += chunk_size;
    }

    ps3drive_debug(handle, 2, "Wrote %zu bytes to buffer %d\n", len, buffer_id);
    return PS3DRIVE_OK;
}

/* ============================================================================
 * Drive Pairing
 * ============================================================================ */

ps3drive_error_t ps3drive_pair(ps3drive_t *handle, ps3drive_pairing_ctx_t *ctx)
{
    ps3drive_error_t ret;

    if (handle == NULL || ctx == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    /* ========================================================================
     * Step 1: Write P-Block to buffer 2
     * ======================================================================== */
    if (ctx->pblock_valid) {
        ps3drive_debug(handle, 1, "Writing P-Block...\n");

        ret = ps3drive_enable_buffer_write(handle, PS3DRIVE_BUFFER_PBLOCK);
        if (ret != PS3DRIVE_OK) {
            return ret;
        }

        ret = ps3drive_write_buffer_internal(handle, PS3DRIVE_BUFFER_PBLOCK,
                                              ctx->pblock, PS3DRIVE_PBLOCK_SIZE);
        if (ret != PS3DRIVE_OK) {
            return ret;
        }
    }

    /* ========================================================================
     * Step 2: Authenticate drive with Storage Manager
     *
     * CRITICAL: Authentication MUST happen AFTER P-Block write but BEFORE
     * S-Block write. This is the exact sequence from the original pair.cmd:
     *   1. bd_enable_buffer_write -b 2
     *   2. bd_write_buffer -b 2 -i pblockdec.bin
     *   3. bd_auth  <-- Authentication happens HERE
     *   4. bd_enable_buffer_write -b 3
     *   5. bd_write_buffer -b 3 -i sblockdec.bin
     *   ...
     * ======================================================================== */
    ps3drive_debug(handle, 1, "Authenticating drive...\n");

    ret = ps3drive_auth_bd_internal(handle, PS3DRIVE_AUTH_KEY1, PS3DRIVE_AUTH_KEY2);
    if (ret != PS3DRIVE_OK) {
        ps3drive_set_error(handle, ret, "BD authentication failed during pairing");
        return ret;
    }
    handle->authenticated = true;
    ps3drive_debug(handle, 1, "BD authentication successful\n");

    /* ========================================================================
     * Step 3: Write S-Block to buffer 3
     * ======================================================================== */
    if (ctx->sblock_valid) {
        ps3drive_debug(handle, 1, "Writing S-Block...\n");

        ret = ps3drive_enable_buffer_write(handle, PS3DRIVE_BUFFER_SBLOCK);
        if (ret != PS3DRIVE_OK) {
            return ret;
        }

        ret = ps3drive_write_buffer_internal(handle, PS3DRIVE_BUFFER_SBLOCK,
                                              ctx->sblock, PS3DRIVE_SBLOCK_SIZE);
        if (ret != PS3DRIVE_OK) {
            return ret;
        }
    }

    /* ========================================================================
     * Step 4: Write HRL to buffer 4
     * ======================================================================== */
    if (ctx->hrl_valid) {
        ps3drive_debug(handle, 1, "Writing HRL...\n");

        ret = ps3drive_enable_buffer_write(handle, PS3DRIVE_BUFFER_HRL);
        if (ret != PS3DRIVE_OK) {
            return ret;
        }

        ret = ps3drive_write_buffer_internal(handle, PS3DRIVE_BUFFER_HRL,
                                              ctx->hrl, PS3DRIVE_HRL_SIZE);
        if (ret != PS3DRIVE_OK) {
            return ret;
        }
    }

    ps3drive_debug(handle, 1, "Drive pairing completed successfully\n");
    return PS3DRIVE_OK;
}
