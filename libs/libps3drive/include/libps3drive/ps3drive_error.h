/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Error codes for PS3 BluRay drive operations.
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

#ifndef LIBPS3DRIVE_PS3DRIVE_ERROR_H
#define LIBPS3DRIVE_PS3DRIVE_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Error codes for PS3 drive operations.
 *
 * All error codes are negative values except PS3DRIVE_OK (0).
 */
typedef enum ps3drive_error {
    PS3DRIVE_OK                    =   0,  /**< Success */
    PS3DRIVE_ERR_NULL_PTR          =  -1,  /**< NULL pointer argument */
    PS3DRIVE_ERR_OPEN_FAILED       =  -2,  /**< Failed to open device */
    PS3DRIVE_ERR_NOT_PS3_DRIVE     =  -3,  /**< Device is not a PS3 drive */
    PS3DRIVE_ERR_AUTH_FAILED       =  -4,  /**< BD authentication failed */
    PS3DRIVE_ERR_SAC_FAILED        =  -5,  /**< SAC key exchange failed */
    PS3DRIVE_ERR_DECRYPT_FAILED    =  -6,  /**< Decryption failed */
    PS3DRIVE_ERR_READ_FAILED       =  -7,  /**< Read operation failed */
    PS3DRIVE_ERR_SCSI_FAILED       =  -8,  /**< SCSI command failed */
    PS3DRIVE_ERR_CRYPTO_FAILED     =  -9,  /**< Cryptographic operation failed */
    PS3DRIVE_ERR_NO_SACD_FEATURE   = -10,  /**< Drive lacks SACD feature */
    PS3DRIVE_ERR_NOT_HYBRID        = -11,  /**< Disc is not a hybrid disc */
    PS3DRIVE_ERR_LAYER_SELECT      = -12,  /**< Failed to select layer */
    PS3DRIVE_ERR_PAIRING_FAILED    = -13,  /**< Drive pairing failed */
    PS3DRIVE_ERR_FW_UPDATE         = -14,  /**< Firmware update failed */
    PS3DRIVE_ERR_OUT_OF_MEMORY     = -15,  /**< Memory allocation failed */
    PS3DRIVE_ERR_INVALID_ARG       = -16,  /**< Invalid argument */
    PS3DRIVE_ERR_TIMEOUT           = -17,  /**< Operation timed out */
    PS3DRIVE_ERR_NOT_AUTHENTICATED = -18,  /**< Not authenticated yet */
    PS3DRIVE_ERR_NOT_READY         = -19,  /**< Drive not ready */
    PS3DRIVE_ERR_NO_DISC           = -20,  /**< No disc in drive */
    PS3DRIVE_ERR_INVALID_EID2      = -21,  /**< Invalid EID2 data */
    PS3DRIVE_ERR_BUFFER_WRITE      = -22,  /**< Buffer write failed */
    PS3DRIVE_ERR_SEEK_FAILED       = -23   /**< Seek operation failed */
} ps3drive_error_t;

#ifdef __cplusplus
}
#endif

#endif /* LIBPS3DRIVE_PS3DRIVE_ERROR_H */
