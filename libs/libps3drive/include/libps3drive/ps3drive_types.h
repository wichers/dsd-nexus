/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Type definitions for PS3 BluRay drive interface.
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

#ifndef LIBPS3DRIVE_PS3DRIVE_TYPES_H
#define LIBPS3DRIVE_PS3DRIVE_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Export Macros
 * ============================================================================ */

#ifdef _WIN32
    #ifdef PS3DRIVE_BUILDING_DLL
        #define PS3DRIVE_API __declspec(dllexport)
    #else
        #define PS3DRIVE_API
    #endif
#else
    #if defined(__GNUC__) && __GNUC__ >= 4
        #define PS3DRIVE_API __attribute__((visibility("default")))
    #else
        #define PS3DRIVE_API
    #endif
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

/** Sector size for SACD reading (2048 bytes) */
#define PS3DRIVE_SECTOR_SIZE    2048

/** AES key size in bytes */
#define PS3DRIVE_AES_KEY_SIZE   16

/** AES IV size in bytes */
#define PS3DRIVE_AES_IV_SIZE    16

/** Maximum vendor ID length (including null terminator) */
#define PS3DRIVE_VENDOR_ID_LEN  9

/** Maximum product ID length (including null terminator) */
#define PS3DRIVE_PRODUCT_ID_LEN 17

/** Maximum revision length (including null terminator) */
#define PS3DRIVE_REVISION_LEN   5

/* ============================================================================
 * Opaque Handle Types
 * ============================================================================ */

/**
 * @brief Opaque handle for PS3 drive operations.
 *
 * All drive operations require a valid handle obtained from ps3drive_open().
 * The handle must be released with ps3drive_close() when no longer needed.
 */
typedef struct ps3drive_s ps3drive_t;

/**
 * @brief Opaque handle for drive pairing context.
 *
 * Contains P-Block, S-Block, and HRL data for drive pairing.
 * Created with ps3drive_pairing_create_default() and freed with ps3drive_pairing_free().
 */
typedef struct ps3drive_pairing_ctx ps3drive_pairing_ctx_t;

/* ============================================================================
 * Drive Information
 * ============================================================================ */

/**
 * @brief Drive information structure.
 *
 * Contains identification and capability information retrieved from
 * the drive via INQUIRY and GET CONFIGURATION commands.
 */
typedef struct ps3drive_info {
    char     vendor_id[PS3DRIVE_VENDOR_ID_LEN];    /**< Vendor identification */
    char     product_id[PS3DRIVE_PRODUCT_ID_LEN];  /**< Product identification */
    char     revision[PS3DRIVE_REVISION_LEN];      /**< Firmware revision */
    uint64_t drive_type;                           /**< PS3 drive type identifier */
    int      has_sacd_feature;                     /**< Non-zero if SACD feature present */
    int      has_hybrid_support;                   /**< Non-zero if hybrid disc supported */
} ps3drive_info_t;

/* ============================================================================
 * Drive Type Identifiers
 * ============================================================================ */

/**
 * @brief PS3 drive type identifiers.
 *
 * These values identify different hardware revisions of PS3 BluRay drives.
 * Using defines instead of enum because values are 64-bit.
 */
typedef uint64_t ps3drive_type_t;

#define PS3DRIVE_TYPE_UNKNOWN   ((uint64_t)0)
#define PS3DRIVE_TYPE_300R      ((uint64_t)0x1200000000000001ULL)
#define PS3DRIVE_TYPE_301R      ((uint64_t)0x1200000000000002ULL)
#define PS3DRIVE_TYPE_302R      ((uint64_t)0x1200000000000003ULL)
#define PS3DRIVE_TYPE_303R      ((uint64_t)0x1200000000000004ULL)
#define PS3DRIVE_TYPE_304R      ((uint64_t)0x1200000000000005ULL)
#define PS3DRIVE_TYPE_306R      ((uint64_t)0x1200000000000007ULL)
#define PS3DRIVE_TYPE_308R      ((uint64_t)0x1200000000000008ULL)
#define PS3DRIVE_TYPE_310R      ((uint64_t)0x1200000000000009ULL)
#define PS3DRIVE_TYPE_312R      ((uint64_t)0x120000000000000aULL)
#define PS3DRIVE_TYPE_314R      ((uint64_t)0x120000000000000bULL)

/* ============================================================================
 * Buffer Identifiers (for pairing/firmware operations)
 * ============================================================================ */

/**
 * @brief BD drive buffer identifiers.
 */
typedef enum ps3drive_buffer_id {
    PS3DRIVE_BUFFER_MAIN   = 0,  /**< Main write buffer */
    PS3DRIVE_BUFFER_PBLOCK = 2,  /**< P-Block buffer (0x60 bytes) */
    PS3DRIVE_BUFFER_SBLOCK = 3,  /**< S-Block buffer (0x670 bytes) */
    PS3DRIVE_BUFFER_HRL    = 4   /**< HRL buffer (0x8000 bytes) */
} ps3drive_buffer_id_t;

/* ============================================================================
 * Buffer Sizes
 * ============================================================================ */

/** P-Block buffer size */
#define PS3DRIVE_PBLOCK_SIZE    0x60

/** S-Block buffer size */
#define PS3DRIVE_SBLOCK_SIZE    0x670

/** HRL buffer size */
#define PS3DRIVE_HRL_SIZE       0x8000

/** Maximum write buffer length */
#define PS3DRIVE_MAX_WRITE_LEN  0x8000

#ifdef __cplusplus
}
#endif

#endif /* LIBPS3DRIVE_PS3DRIVE_TYPES_H */
