/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief PS3 BluRay Drive Interface for SACD Reading.
 * This library provides access to PS3 BluRay drives for reading SACD discs.
 * It handles drive authentication, SAC key exchange, and sector decryption.
 * Typical usage for SACD reading:
 *   1. Open drive with ps3drive_open()
 *   2. Authenticate with ps3drive_authenticate()
 *   3. Exchange keys with ps3drive_sac_key_exchange()
 *   4. Read sectors with ps3drive_read_sectors()
 *   5. Decrypt data with ps3drive_decrypt()
 *   6. Close drive with ps3drive_close()
 * For operations that don't require authentication (pairing, eject, etc.):
 *   1. Open drive with ps3drive_open()
 *   2. Perform operation (e.g., ps3drive_eject(), ps3drive_pair())
 *   3. Close drive with ps3drive_close()
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

#ifndef LIBPS3DRIVE_PS3DRIVE_H
#define LIBPS3DRIVE_PS3DRIVE_H

#include <libps3drive/ps3drive_types.h>
#include <libps3drive/ps3drive_error.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Version Information
 * ============================================================================ */

#define PS3DRIVE_VERSION_MAJOR  1
#define PS3DRIVE_VERSION_MINOR  0
#define PS3DRIVE_VERSION_PATCH  0

/**
 * @brief Get library version string.
 * @return Version string (e.g., "1.0.0")
 */
PS3DRIVE_API const char *ps3drive_version(void);

/* ============================================================================
 * Drive Management
 * ============================================================================ */

/**
 * @brief Open a PS3 BluRay drive.
 *
 * Opens the specified device and validates that it is a PS3 BluRay drive.
 * The returned handle must be released with ps3drive_close().
 *
 * @param[in]  device_path  Device path (e.g., "/dev/sr0" on Linux, "D:" on Windows)
 * @param[out] handle       Receives drive handle on success
 * @return PS3DRIVE_OK on success, error code on failure
 *
 * @note On Windows, paths like "D:", "D:\\", "\\\\.\\D:", or "\\\\.\\CdRom0" are accepted.
 * @note On Linux/Unix, paths like "/dev/sr0" or "/dev/sg0" are accepted.
 */
PS3DRIVE_API ps3drive_error_t ps3drive_open(const char *device_path,
                                             ps3drive_t **handle);

/**
 * @brief Close a PS3 drive and free resources.
 *
 * Releases all resources associated with the drive handle.
 * The handle becomes invalid after this call.
 *
 * @param[in] handle  Drive handle to close (may be NULL)
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_close(ps3drive_t *handle);

/**
 * @brief Eject the disc from the drive.
 *
 * @param[in] handle  Drive handle
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_eject(ps3drive_t *handle);

/* ============================================================================
 * Drive Information
 * ============================================================================ */

/**
 * @brief Get drive information (INQUIRY data).
 *
 * Retrieves identification and capability information from the drive.
 *
 * @param[in]  handle  Drive handle
 * @param[out] info    Receives drive information
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_get_info(ps3drive_t *handle,
                                                 ps3drive_info_t *info);

/**
 * @brief Check if a PS3 drive is present at the given path.
 *
 * This is a quick check that doesn't require a full open/authenticate cycle.
 *
 * @param[in]  device_path  Device path to check
 * @param[out] is_ps3       Receives true if PS3 drive, false otherwise
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_is_ps3_drive(const char *device_path,
                                                     bool *is_ps3);

/**
 * @brief Get total sector count of the disc.
 *
 * @param[in]  handle        Drive handle
 * @param[out] total_sectors Receives total sector count
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_get_total_sectors(ps3drive_t *handle,
                                                          uint32_t *total_sectors);

/**
 * @brief Print drive features to stdout.
 *
 * Prints detailed information about drive capabilities and features.
 *
 * @param[in] handle   Drive handle
 * @param[in] verbose  Verbose output level (0=minimal, 1=normal, 2=detailed)
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_print_features(ps3drive_t *handle,
                                                       int verbose);

/**
 * @brief Get the drive type identifier.
 *
 * @param[in]  handle      Drive handle
 * @param[out] drive_type  Receives drive type
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_get_type(ps3drive_t *handle,
                                                 ps3drive_type_t *drive_type);

/* ============================================================================
 * SAC Key Exchange
 * ============================================================================ */

/**
 * @brief Execute SACD key exchange.
 *
 * Performs the SAC (SACD Authentication Channel) 6-command key exchange
 * protocol to derive the disc encryption keys. This function handles
 * the SACD initialization sequence:
 *   1. Set CD speed to maximum
 *   2. Select SACD layer (for hybrid discs)
 *   3. INQUIRY (validate drive type)
 *   4. GET CONFIGURATION (check SACD feature)
 *   5. SAC key exchange
 *
 * @note ps3drive_authenticate() MUST be called before this function.
 *       Returns PS3DRIVE_ERR_NOT_AUTHENTICATED if not authenticated.
 *
 * @param[in]  handle   Drive handle
 * @param[out] aes_key  Receives 16-byte AES key (optional, can be NULL)
 * @param[out] aes_iv   Receives 16-byte AES IV (optional, can be NULL)
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_sac_key_exchange(ps3drive_t *handle,
                                                         uint8_t *aes_key,
                                                         uint8_t *aes_iv);

/**
 * @brief Authenticate with the PS3 BD drive.
 *
 * Performs BD drive authentication protocol. This must be called before
 * operations that require authentication:
 *   - ps3drive_sac_key_exchange() - for SACD reading
 *   - ps3drive_enable_sacd_mode() / ps3drive_get_sacd_mode() - D7 commands
 *   - ps3drive_update_firmware() - firmware flashing
 *
 * Operations that do NOT require authentication:
 *   - ps3drive_eject()
 *   - ps3drive_pair()
 *   - ps3drive_get_disc_presence()
 *
 * @param[in] handle  Drive handle
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_authenticate(ps3drive_t *handle);

/**
 * @brief Check if drive has been authenticated.
 *
 * @param[in]  handle         Drive handle
 * @param[out] authenticated  Receives authentication status
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_is_authenticated(ps3drive_t *handle,
                                                         bool *authenticated);

/* ============================================================================
 * Layer Selection (Hybrid Discs)
 * ============================================================================ */

/**
 * @brief Select the SACD layer on a hybrid disc.
 *
 * For hybrid discs that have both CD/DVD and SACD layers,
 * this function selects the SACD layer for reading using
 * START STOP UNIT command.
 *
 * @param[in] handle  Drive handle
 * @return PS3DRIVE_OK on success, PS3DRIVE_ERR_NOT_HYBRID if not hybrid
 */
PS3DRIVE_API ps3drive_error_t ps3drive_select_sacd_layer(ps3drive_t *handle);

/**
 * @brief Enable SACD mode on the drive using D7 command.
 *
 * Uses the proprietary PS3 D7 command to set the drive state flag
 * for SACD mode.
 *
 * The enable parameter controls the drive behavior:
 *   - false: Set flag to 0xFF (disable SACD)
 *   - true:  Set flag to 0x53 (enable SACD)
 *
 * @param[in] handle  Drive handle
 * @param[in] enable  true to enable SACD, false to disable
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_enable_sacd_mode(ps3drive_t *handle,
                                                         bool enable);

/**
 * @brief Check if the current disc is a hybrid disc.
 *
 * @param[in]  handle    Drive handle
 * @param[out] is_hybrid Receives true if hybrid, false otherwise
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_is_hybrid_disc(ps3drive_t *handle,
                                                       bool *is_hybrid);

/* ============================================================================
 * Reading and Decryption
 * ============================================================================ */

/**
 * @brief Read sectors from the disc.
 *
 * Reads raw (encrypted) sectors from the disc. Use ps3drive_decrypt()
 * to decrypt the data after reading.
 *
 * @param[in]  handle       Drive handle
 * @param[in]  start_sector Starting sector number (0-based)
 * @param[in]  num_sectors  Number of sectors to read
 * @param[out] buffer       Buffer to receive data (must be num_sectors * 2048 bytes)
 * @return Number of sectors successfully read, or 0 on error
 */
PS3DRIVE_API uint32_t ps3drive_read_sectors(ps3drive_t *handle,
                                             uint32_t start_sector,
                                             uint32_t num_sectors,
                                             void *buffer);

/**
 * @brief Decrypt sector data using the exchanged keys.
 *
 * Decrypts data in-place using the AES key and IV obtained from
 * the SAC key exchange. The key exchange must have been completed
 * with ps3drive_sac_key_exchange() before calling this function.
 *
 * @param[in]     handle      Drive handle
 * @param[in,out] buffer      Data to decrypt (in-place)
 * @param[in]     num_sectors Number of 2048-byte sectors
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_decrypt(ps3drive_t *handle,
                                                uint8_t *buffer,
                                                uint32_t num_sectors);

/* ============================================================================
 * Drive Pairing (Advanced)
 * ============================================================================ */

/**
 * @brief Create a pairing context with default P-Block, S-Block, and HRL.
 *
 * Creates a pairing context using the embedded default pairing data.
 * This restores the drive to a working state for BD movie playback.
 *
 * @param[out] ctx  Receives pairing context
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_pairing_create_default(ps3drive_pairing_ctx_t **ctx);

/**
 * @brief Execute full drive pairing sequence.
 *
 * Performs the complete pairing sequence:
 *   1. Write P-Block to buffer 2
 *   2. Authenticate drive with Storage Manager
 *   3. Write S-Block to buffer 3
 *   4. Write HRL to buffer 4
 *
 * @param[in] handle  Drive handle
 * @param[in] ctx     Pairing context
 * @return PS3DRIVE_OK on success, error code on failure
 *
 * @warning This operation can corrupt the drive if performed incorrectly.
 *          Only use if you know what you're doing.
 */
PS3DRIVE_API ps3drive_error_t ps3drive_pair(ps3drive_t *handle,
                                             ps3drive_pairing_ctx_t *ctx);

/**
 * @brief Free pairing context.
 *
 * Releases memory and securely zeros sensitive data.
 *
 * @param[in] ctx  Pairing context to free (may be NULL)
 */
PS3DRIVE_API void ps3drive_pairing_free(ps3drive_pairing_ctx_t *ctx);

/* ============================================================================
 * Firmware Update (Advanced)
 * ============================================================================ */

/**
 * @brief Update drive firmware.
 *
 * Writes new firmware to the drive using WRITE BUFFER command.
 *
 * @param[in] handle       Drive handle
 * @param[in] firmware     Firmware data
 * @param[in] firmware_len Firmware length in bytes
 * @param[in] h_id         Hardware ID (use 0 for auto-detect from drive type)
 * @param[in] timeout_sec  Timeout in seconds (0 for default)
 * @return PS3DRIVE_OK on success, error code on failure
 *
 * @warning This operation can brick the drive if incorrect firmware is used.
 *          Only use if you know what you're doing.
 */
PS3DRIVE_API ps3drive_error_t ps3drive_update_firmware(ps3drive_t *handle,
                                                        const uint8_t *firmware,
                                                        size_t firmware_len,
                                                        uint64_t h_id,
                                                        int timeout_sec);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Get error message for error code.
 *
 * @param[in] error  Error code
 * @return Static string describing the error
 */
PS3DRIVE_API const char *ps3drive_error_string(ps3drive_error_t error);

/**
 * @brief Get the last error message from the handle.
 *
 * Returns a more detailed error message than ps3drive_error_string(),
 * which may include SCSI sense data or other context.
 *
 * @param[in] handle  Drive handle
 * @return Error message string (internal buffer, do not free)
 */
PS3DRIVE_API const char *ps3drive_get_error(ps3drive_t *handle);

/**
 * @brief Set verbosity level for debug output.
 *
 * @param[in] handle  Drive handle
 * @param[in] level   Verbosity (0=silent, 1=errors, 2=verbose, 3=debug)
 */
PS3DRIVE_API void ps3drive_set_verbose(ps3drive_t *handle, int level);

/**
 * @brief Check if a disc is present in the drive.
 *
 * Uses GET EVENT STATUS NOTIFICATION command to detect media presence.
 * This checks for "new media" event (buffer[5] == 0x02).
 *
 * @param[in]  handle       Drive handle
 * @param[out] disc_present Receives true if disc is present, false otherwise
 * @return PS3DRIVE_OK on success, error code on failure
 */
PS3DRIVE_API ps3drive_error_t ps3drive_get_disc_presence(ps3drive_t *handle,
                                                          bool *disc_present);

/**
 * @brief Get drive type name string.
 *
 * @param[in] drive_type  Drive type identifier
 * @return Static string with drive model name (e.g., "PS-SYSTEM 302R")
 */
PS3DRIVE_API const char *ps3drive_type_string(ps3drive_type_t drive_type);

#ifdef __cplusplus
}
#endif

#endif /* LIBPS3DRIVE_PS3DRIVE_H */
