/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Internal declarations for PS3 drive library.
 * This file contains internal structure definitions and function
 * declarations shared between the library source files.
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

#ifndef LIBPS3DRIVE_PS3DRIVE_INTERNAL_H
#define LIBPS3DRIVE_PS3DRIVE_INTERNAL_H

#include <libps3drive/ps3drive.h>
#include <libps3drive/ps3drive_types.h>
#include <libps3drive/ps3drive_error.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Internal Constants
 * ============================================================================ */

/** Maximum error message length */
#define PS3DRIVE_ERROR_MSG_SIZE  256

/** Sense buffer length for SCSI commands */
#define PS3DRIVE_SENSE_LEN       64

/** Default SCSI command timeout in seconds */
#define PS3DRIVE_TIMEOUT_SEC     120

/** SACD feature code */
#define PS3DRIVE_SACD_FEATURE    0xFF41

/* ============================================================================
 * Internal Handle Structure
 * ============================================================================ */

/**
 * @brief Internal PS3 drive handle structure.
 *
 * Contains all state needed for drive operations including the SCSI
 * file descriptor, authentication state, and derived encryption keys.
 */
struct ps3drive_s {
    /** SCSI generic file descriptor */
    int sg_fd;

    /** Derived AES key from SAC key exchange */
    uint8_t aes_key[16];

    /** Derived AES IV from SAC key exchange */
    uint8_t aes_iv[16];

    /** BD authentication completed flag */
    bool authenticated;

    /** SAC key exchange completed flag */
    bool sac_exchanged;

    /** Verbosity level (0=silent, 1=errors, 2=verbose, 3=debug) */
    int verbose;

    /** Noisy flag for SCSI commands (show SCSI errors) */
    int noisy;

    /** Cached drive information */
    ps3drive_info_t info;

    /** Drive type identifier */
    uint64_t drive_type;

    /** Last error code */
    ps3drive_error_t last_error;

    /** Error message buffer */
    char error_msg[PS3DRIVE_ERROR_MSG_SIZE];

    /** Total sectors cached from READ CAPACITY */
    uint32_t total_sectors;

    /** Sectors cached flag */
    bool total_sectors_valid;

    /** Is hybrid disc flag */
    bool is_hybrid;

    /** Hybrid disc checked flag */
    bool hybrid_checked;
};

/* ============================================================================
 * Pairing Context Structure
 * ============================================================================ */

/**
 * @brief Internal pairing context structure.
 *
 * Contains decrypted P-Block, S-Block, and HRL data for drive pairing.
 */
struct ps3drive_pairing_ctx {
    /** Decrypted P-Block data */
    uint8_t pblock[PS3DRIVE_PBLOCK_SIZE];

    /** Decrypted S-Block data */
    uint8_t sblock[PS3DRIVE_SBLOCK_SIZE];

    /** HRL data (padded to full size) */
    uint8_t hrl[PS3DRIVE_HRL_SIZE];

    /** Actual HRL data length before padding */
    size_t hrl_len;

    /** P-Block data is valid */
    bool pblock_valid;

    /** S-Block data is valid */
    bool sblock_valid;

    /** HRL data is valid */
    bool hrl_valid;
};

/* ============================================================================
 * Error Handling Helpers
 * ============================================================================ */

/**
 * @brief Set error state on handle.
 *
 * @param[in] handle  Drive handle
 * @param[in] err     Error code to set
 * @param[in] fmt     Format string for error message
 * @param[in] ...     Format arguments
 */
void ps3drive_set_error(ps3drive_t *handle, ps3drive_error_t err,
                         const char *fmt, ...);

/**
 * @brief Set error state with va_list.
 *
 * @param[in] handle  Drive handle
 * @param[in] err     Error code to set
 * @param[in] fmt     Format string for error message
 * @param[in] args    Format arguments
 */
void ps3drive_set_error_v(ps3drive_t *handle, ps3drive_error_t err,
                           const char *fmt, va_list args);

/**
 * @brief Clear error state on handle.
 *
 * @param[in] handle  Drive handle
 */
void ps3drive_clear_error(ps3drive_t *handle);

/* ============================================================================
 * Authentication Functions (ps3drive_auth.c)
 * ============================================================================ */

/**
 * @brief Perform BD drive authentication.
 *
 * Internal function that implements the BD authentication protocol.
 *
 * @param[in] handle  Drive handle
 * @param[in] key1    16-byte authentication key 1
 * @param[in] key2    16-byte authentication key 2
 * @return PS3DRIVE_OK on success, error code on failure
 */
ps3drive_error_t ps3drive_auth_bd_internal(ps3drive_t *handle,
                                            const uint8_t *key1,
                                            const uint8_t *key2);

/* ============================================================================
 * SAC Key Exchange Functions (ps3drive_sac.c)
 * ============================================================================ */

/**
 * @brief Perform SAC key exchange protocol.
 *
 * Internal function that implements the 6-command SAC key exchange.
 *
 * @param[in]  handle   Drive handle
 * @param[out] aes_key  Receives 16-byte AES key
 * @param[out] aes_iv   Receives 16-byte AES IV
 * @return PS3DRIVE_OK on success, error code on failure
 */
ps3drive_error_t ps3drive_sac_exchange_internal(ps3drive_t *handle,
                                                 uint8_t *aes_key,
                                                 uint8_t *aes_iv);

/* ============================================================================
 * Info Functions (ps3drive_info.c)
 * ============================================================================ */

/**
 * @brief Perform INQUIRY and cache drive info.
 *
 * @param[in] handle  Drive handle
 * @return PS3DRIVE_OK on success, error code on failure
 */
ps3drive_error_t ps3drive_inquiry_internal(ps3drive_t *handle);

/**
 * @brief Check if SACD feature is enabled.
 *
 * @param[in] handle  Drive handle
 * @return PS3DRIVE_OK if SACD feature present, error code otherwise
 */
ps3drive_error_t ps3drive_check_sacd_feature_internal(ps3drive_t *handle);

/**
 * @brief Look up drive type from product ID.
 *
 * @param[in] product_id  Product ID string from INQUIRY
 * @return Drive type identifier, or 0 if not found
 */
uint64_t ps3drive_lookup_type(const char *product_id);

/* ============================================================================
 * Pairing Functions (ps3drive_pairing.c)
 * ============================================================================ */

/**
 * @brief Enable buffer write for specified buffer ID.
 *
 * @param[in] handle     Drive handle
 * @param[in] buffer_id  Buffer ID (2=P-Block, 3=S-Block, 4=HRL)
 * @return PS3DRIVE_OK on success, error code on failure
 */
ps3drive_error_t ps3drive_enable_buffer_write(ps3drive_t *handle,
                                               int buffer_id);

/**
 * @brief Write data to drive buffer.
 *
 * @param[in] handle     Drive handle
 * @param[in] buffer_id  Buffer ID
 * @param[in] data       Data to write
 * @param[in] len        Data length
 * @return PS3DRIVE_OK on success, error code on failure
 */
ps3drive_error_t ps3drive_write_buffer_internal(ps3drive_t *handle,
                                                 int buffer_id,
                                                 const uint8_t *data,
                                                 size_t len);

/* ============================================================================
 * Firmware Update Functions (ps3drive_fw.c)
 * ============================================================================ */

/**
 * @brief Get sense code message for firmware update.
 *
 * @param[in] req_sense  Request sense code
 * @return Message string, or "unknown" if not found
 */
const char *ps3drive_sense_message(unsigned int req_sense);

/* ============================================================================
 * SCSI Utility Functions
 * ============================================================================ */

/**
 * @brief Calculate simple checksum (one's complement).
 *
 * Used in authentication protocol for data validation.
 *
 * @param[in] data  Data to checksum
 * @param[in] len   Data length
 * @return 8-bit checksum
 */
uint8_t ps3drive_checksum(const uint8_t *data, int len);

/**
 * @brief Debug print hex dump.
 *
 * Prints hex dump of data to stderr if verbose level is high enough.
 *
 * @param[in] handle  Drive handle (for verbose level)
 * @param[in] prefix  Prefix string
 * @param[in] data    Data to dump
 * @param[in] len     Data length
 */
void ps3drive_debug_hex(ps3drive_t *handle, const char *prefix,
                         const uint8_t *data, size_t len);

/**
 * @brief Debug print message.
 *
 * Prints message to stderr if verbose level is high enough.
 *
 * @param[in] handle  Drive handle (for verbose level)
 * @param[in] level   Required verbose level (1, 2, or 3)
 * @param[in] fmt     Format string
 * @param[in] ...     Format arguments
 */
void ps3drive_debug(ps3drive_t *handle, int level, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* LIBPS3DRIVE_PS3DRIVE_INTERNAL_H */
