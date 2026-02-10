/*
 * Copyright (c) 2026 Alexander Wichers
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the BSD_LICENSE file.
 *
 * PS3 BluRay drive SCSI command declarations for:
 * - BD drive authentication (SEND KEY / REPORT KEY)
 * - SAC (Secure Authenticated Channel) key exchange
 * - E0/E1 proprietary session establishment commands
 * - D7 drive state flag commands
 * - Disc structure reading
 * - Mode page operations for buffer write enable
 */

#ifndef SG_CMDS_PS3_H
#define SG_CMDS_PS3_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Drive Feature Detection Functions
 *============================================================================*/

/** Feature flag: Hybrid disc detected (dual-layer DVD/SACD) */
#define FEATURE_HYBRID_DISC (1 << 0)

/** Feature flag: SACD feature 1 (0xFF40) is current */
#define FEATURE_SACD_1      (1 << 1)

/** Feature flag: SACD feature 2 (0xFF41) is current */
#define FEATURE_SACD_2      (1 << 2)

/**
 * @brief Get human-readable profile name
 *
 * Converts a disc profile number (from GET CONFIGURATION response) to
 * a human-readable string. Common profiles include:
 *   - 0x10: DVD-ROM
 *   - 0x40: BD-ROM
 *   - 0xff71: PS3 BD-ROM
 *
 * @param profile_num  Profile number from drive response
 * @param buff         Buffer to receive profile string (min 64 bytes)
 * @return Pointer to buff containing the profile description
 */
const char *sg_get_profile_str(int profile_num, char *buff);

/**
 * @brief Get human-readable feature name
 *
 * Converts a feature code (from GET CONFIGURATION response) to
 * a human-readable string. PS3-specific features include:
 *   - 0xFF00: PS3 drive
 *   - 0xFF40: SACD feature 1
 *   - 0xFF41: SACD feature 2
 *
 * @param feature_num  Feature code from drive response
 * @param buff         Buffer to receive feature string (min 64 bytes)
 * @return Pointer to buff containing the feature description
 */
const char *sg_get_feature_str(int feature_num, char *buff);

/**
 * @brief Decode and print a single feature descriptor
 *
 * Parses and prints detailed information about a feature descriptor
 * from the GET CONFIGURATION response. Handles standard MMC features
 * as well as PS3-proprietary features (0xFFxx).
 *
 * @param feature  Feature code
 * @param ucp      Pointer to feature descriptor data
 * @param len      Length of feature descriptor in bytes
 */
void sg_decode_feature(int feature, unsigned char *ucp, int len);

/**
 * @brief Decode and print complete GET CONFIGURATION response
 *
 * Parses the entire GET CONFIGURATION response, printing the current
 * profile and all feature descriptors. Supports brief mode (feature
 * names only) and hex dump mode.
 *
 * @param resp          Pointer to response buffer
 * @param max_resp_len  Size of response buffer in bytes
 * @param len           Actual response length from drive
 * @param brief         If non-zero, print only feature names
 * @param inner_hex     If non-zero, hex dump each feature instead of decoding
 */
void sg_decode_config(unsigned char *resp, int max_resp_len, int len,
                      int brief, int inner_hex);

/**
 * @brief Check for SACD-related features in GET CONFIGURATION response
 *
 * Scans the GET CONFIGURATION response for PS3-specific features that
 * indicate SACD disc capability. Only examines DVD profile (0x10) responses.
 *
 * @param resp          Pointer to response buffer
 * @param max_resp_len  Size of response buffer in bytes
 * @param len           Actual response length from drive
 * @return Bit mask of detected features:
 *         - FEATURE_HYBRID_DISC (0x01): Hybrid disc feature is current
 *         - FEATURE_SACD_1 (0x02): SACD feature 1 (0xFF40) is current
 *         - FEATURE_SACD_2 (0x04): SACD feature 2 (0xFF41) is current
 */
int sg_decode_config_set(unsigned char *resp, int max_resp_len, int len);

/*============================================================================
 * SCSI Command Functions
 *============================================================================*/

/* Invokes a PS3 SEND KEY command for BD drive authentication.
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_send_key(int sg_fd, void * paramp, int param_list_len,
                       unsigned char vcps_fun, unsigned char key_class,
                       unsigned char agid, unsigned char key_fmt,
                       unsigned char ctrl, int noisy, int verbose);

/* Invokes a PS3 REPORT KEY command for BD drive authentication.
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_report_key(int sg_fd, unsigned int start_llba,
                         unsigned char block_cnt, void * resp, int resp_len,
                         unsigned char key_class, unsigned char agid,
                         unsigned char key_fmt, unsigned char ctrl,
                         int noisy, int verbose);

/* Invokes a PS3 SAC SEND KEY command for Secure Authenticated Channel.
 * SAC variant places parameter length in CDB bytes [2-5] instead of [8-9].
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_sac_send_key(int sg_fd, void * paramp, int param_list_len,
                           unsigned char vcps_fun, unsigned char key_class,
                           unsigned char agid, unsigned char key_fmt,
                           unsigned char ctrl, int noisy, int verbose);

/* Invokes a PS3 SAC REPORT KEY command for Secure Authenticated Channel.
 * SAC variant places allocation length in CDB bytes [2-5] instead of [8-9].
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_sac_report_key(int sg_fd, unsigned char block_cnt,
                             void * resp, int resp_len,
                             unsigned char key_class, unsigned char agid,
                             unsigned char key_fmt, unsigned char ctrl,
                             int noisy, int verbose);

/* Invokes a PS3 E0 command to receive session establishment data.
 * Proprietary command (opcode 0xE0) used during drive session setup.
 * The cdb parameter contains 8 bytes of command-specific data.
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_e0_report_key(int sg_fd, void * resp, int mx_resp_len,
                            const unsigned char cdb[8], int noisy, int verbose);

/* Invokes a PS3 E1 command to send session establishment data.
 * Proprietary command (opcode 0xE1) used during drive session setup.
 * The cdb parameter contains 8 bytes of command-specific data.
 * Note: This command may return SG_LIB_CAT_SENSE which is treated as success.
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_e1_send_key(int sg_fd, void * paramp, int param_len,
                          const unsigned char cdb[8], int noisy, int verbose);

/* Invokes a PS3 D7 command to set a drive state flag.
 * Proprietary command (opcode 0xD7) with subcommand 0x1A.
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_d7_set(int sg_fd, unsigned char flag, int noisy, int verbose);

/* Invokes a PS3 D7 command to get a drive state flag.
 * Proprietary command (opcode 0xD7) with subcommand 0x1A.
 * The flag parameter must not be NULL.
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_d7_get(int sg_fd, unsigned char *flag, int noisy, int verbose);

/* Invokes a PS3 READ DISC STRUCTURE command (MMC).
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_read_disc_structure(int sg_fd, int media_type,
                                  unsigned int address, int layer_nr,
                                  int fmt, int agid, unsigned char ctrl,
                                  void * resp, int mx_resp_len,
                                  int noisy, int verbose);

/* Invokes MODE SELECT (10) to enable buffer write mode (page 0x2D).
 * Must be called before using WRITE BUFFER commands on PS3 drives.
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_write_mode(int sg_fd, int buffer_id, int noisy, int verbose);

/* Invokes a PS3-specific MODE SELECT (10) command with extended fields.
 * Includes reserved, NACA, and flag fields for PS3-specific operations.
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_mode_select10(int sg_fd, int pf, int reserved, int sp, int naca,
                            int flag, void * paramp, int param_len,
                            int noisy, int verbose);

/* Invokes a PS3-specific TEST UNIT READY command with sense code extraction.
 * Returns sense code (sense_key<<16 | ASC<<8 | ASCQ) in req_sense parameter.
 * This matches the original bd_util.c implementation for firmware update
 * status checking. The req_sense parameter must not be NULL.
 * Returns 0 on success, -1 on transport error, -2 on command failure with sense.
 * Common sense codes for firmware update:
 *   0x23a00 - success (medium not present)
 *   0x43e01 - failure erasing or writing flash
 *   0x52400 - invalid data length or continuous error
 *   0x52600 - invalid firmware combination or hash error */
int sg_ll_ps3_test_unit_ready(int sg_fd, unsigned int *req_sense,
                              int noisy, int verbose);

/* Invokes a READ (12) command to read sectors from the drive.
 * This is a standard MMC READ (12) command (opcode 0xA8).
 * The buffer must be large enough to hold (num_sectors * sector_size) bytes.
 * Typically sector_size is 2048 bytes for SACD/BD.
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_read12(int sg_fd, uint32_t lba, uint32_t num_sectors,
                     void *buffer, int sector_size, int noisy, int verbose);

/* Invokes a GET EVENT STATUS NOTIFICATION command (opcode 0x4A).
 * This MMC command retrieves asynchronous event status from the drive.
 * The polled parameter should be 1 for polled operation (immediate return).
 * The notification_class_request specifies which event classes to query
 * (0x10 = media class events for disc insertion/removal detection).
 * The resp buffer receives the event status notification header and data.
 * Returns 0 when successful, SG_LIB_CAT_INVALID_OP if command not
 * supported, SG_LIB_CAT_ILLEGAL_REQ if field in cdb not supported,
 * SG_LIB_CAT_UNIT_ATTENTION, SG_LIB_CAT_ABORTED_COMMAND, else -1 */
int sg_ll_ps3_get_event_status_notification(int sg_fd, int polled,
                                             int notification_class_request,
                                             void *resp, int resp_len,
                                             int noisy, int verbose);

#ifdef __cplusplus
}
#endif

#endif
