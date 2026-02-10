/*
 * Copyright (c) 2026 Alexander Wichers
 * All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the BSD_LICENSE file.
 *
 * @file sg_cmds_ps3.c
 * @brief PS3 BluRay drive SCSI command implementations
 *
 * This file implements PS3-specific SCSI commands for BluRay drive
 * authentication, key exchange, and disc structure reading. These commands
 * are used for:
 * - BD drive authentication (SEND KEY / REPORT KEY with key_class 0xE0)
 * - SAC (Secure Authenticated Channel) key exchange
 * - E0/E1 proprietary commands for session establishment
 * - D7 command for drive state flags
 * - Disc structure reading
 * - Mode page operations for buffer write enable
 *
 * The commands follow the MMC specification with PS3-specific extensions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include "sg_lib.h"
#include "sg_cmds_basic.h"
#include "sg_cmds_ps3.h"
#include "sg_pt.h"
#include "sg_unaligned.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Needs to be after config.h */
#ifdef SG_LIB_LINUX
#include <errno.h>
#endif

/** Sense buffer length for SCSI commands */
#define SENSE_BUFF_LEN 64

/** Default pass-through timeout in seconds */
#define DEF_PT_TIMEOUT 120

/** D7 command timeout in seconds (shorter for proprietary commands) */
#define D7_TIMEOUT 20

/* SCSI command opcodes */
#define SEND_KEY_OUT_CMD        0xa3    /**< SEND KEY command opcode */
#define SEND_KEY_OUT_CMDLEN     12      /**< SEND KEY CDB length */
#define REPORT_KEY_IN_CMD       0xa4    /**< REPORT KEY command opcode */
#define REPORT_KEY_IN_CMDLEN    12      /**< REPORT KEY CDB length */
#define E1_KEY_OUT_CMD          0xe1    /**< PS3 E1 SEND KEY command opcode */
#define E1_KEY_OUT_CMDLEN       12      /**< E1 CDB length */
#define E0_KEY_OUT_CMD          0xe0    /**< PS3 E0 REPORT KEY command opcode */
#define E0_KEY_OUT_CMDLEN       12      /**< E0 CDB length */
#define D7_KEY_IN_CMD           0xd7    /**< PS3 D7 command opcode */
#define D7_KEY_IN_CMDLEN        12      /**< D7 CDB length */
#define READ_DISC_STRUCTURE_CMD     0xad    /**< READ DISC STRUCTURE opcode */
#define READ_DISC_STRUCTURE_CMDLEN  12      /**< READ DISC STRUCTURE CDB length */
#define MODE_SELECT10_CMD       0x55    /**< MODE SELECT (10) command opcode */
#define MODE_SELECT10_CMDLEN    10      /**< MODE SELECT (10) CDB length */
#define READ12_CMD              0xa8    /**< READ (12) command opcode */
#define READ12_CMDLEN           12      /**< READ (12) CDB length */
#define GET_EVENT_STATUS_NOTIFICATION_CMD       0x4a    /**< GET EVENT STATUS NOTIFICATION opcode */
#define GET_EVENT_STATUS_NOTIFICATION_CMDLEN    10      /**< GET EVENT STATUS NOTIFICATION CDB length */

#if defined(__GNUC__) || defined(__clang__)
static int pr2ws(const char * fmt, ...)
__attribute__((format(printf, 1, 2)));
#else
static int pr2ws(const char * fmt, ...);
#endif


/**
 * @brief Print formatted message to warnings stream
 * @param fmt Printf-style format string
 * @return Number of characters printed
 */
static int
pr2ws(const char * fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = vfprintf(sg_warnings_strm ? sg_warnings_strm : stderr, fmt, args);
    va_end(args);
    return n;
}

/**
 * @brief Send authentication key to PS3 BluRay drive
 *
 * The SEND KEY command provides data necessary for authentication and for
 * generating a Bus Key for protected data transfers between the Host and Drive.
 * This command, in conjunction with REPORT KEY command, is intended to perform
 * authentication for Drives that conform to a specified Content Protection
 * scheme and to generate a Bus Key as the result of authentication.
 *
 * CDB Format (12 bytes):
 *   [0]     Opcode (0xA3)
 *   [1]     Reserved
 *   [2-5]   Reserved
 *   [6]     VCPS Function
 *   [7]     Key Class
 *   [8-9]   Parameter List Length
 *   [10]    AGID (bits 7-6), Key Format (bits 5-0)
 *   [11]    Control
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param paramp        Pointer to parameter data to send
 * @param param_list_len Length of parameter data in bytes
 * @param vcps_fun      VCPS function code
 * @param key_class     Key class (0xE0 for PS3 BD auth)
 * @param agid          Authentication Grant ID (2 bits)
 * @param key_fmt       Key format (6 bits)
 * @param ctrl          Control byte
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_send_key(int sg_fd,
                   void * paramp, int param_list_len,
                   unsigned char vcps_fun, unsigned char key_class,
                   unsigned char agid, unsigned char key_fmt,
                   unsigned char ctrl, int noisy, int verbose)
{
    int res, ret, sense_cat;
    unsigned char rtpgCmdBlk[SEND_KEY_OUT_CMDLEN] =
        {SEND_KEY_OUT_CMD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;

    rtpgCmdBlk[6] = vcps_fun;
    rtpgCmdBlk[7] = key_class;
    rtpgCmdBlk[8] = (param_list_len >> 8) & 0xff;
    rtpgCmdBlk[9] = param_list_len & 0xff;
    rtpgCmdBlk[10] = ((agid & 0x3) << 6) | (key_fmt & 0x3f);
    rtpgCmdBlk[11] = ctrl;

    if (NULL == sg_warnings_strm)
        sg_warnings_strm = stderr;

    if (verbose) {
        int k;
        pr2ws("    send key cdb: ");
        for (k = 0; k < SEND_KEY_OUT_CMDLEN; ++k)
            pr2ws("%02x ", rtpgCmdBlk[k]);
        pr2ws("\n");
        if ((verbose > 1) && paramp && param_list_len) {
            pr2ws("    send key parameter data:\n");
            dStrHexErr((const char *)paramp, param_list_len, -1);
        }
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2ws("send key: out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, rtpgCmdBlk, sizeof(rtpgCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_out(ptvp, (unsigned char *)paramp, param_list_len);
    res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
    /* Note: For DATA_OUT commands, mx_di_len should be 0 */
    ret = sg_cmds_process_resp(ptvp, "send key", res, 0,
                               sense_b, noisy, verbose, &sense_cat);
    if (-1 == ret) {
        /* Transport error */
        ;
    } else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
            ret = 0;
            break;
        default:
            ret = sense_cat;
            break;
        }
    } else {
        if ((verbose > 2) && (ret > 0)) {
            pr2ws("    send key: response%s\n",
                  (ret > 256 ? ", first 256 bytes" : ""));
            dStrHexErr((const char *)paramp, (ret > 256 ? 256 : ret), -1);
        }
        ret = 0;
    }
    destruct_scsi_pt_obj(ptvp);
    return ret;
}

/**
 * @brief Request authentication key from PS3 BluRay drive
 *
 * The REPORT KEY command requests the start of the authentication process and
 * provides data necessary for authentication and for generating a Bus Key for
 * protected transfers between the Host and Drive. This command, in conjunction
 * with the SEND KEY command, is intended to perform authentication for Drives
 * that conform to specified Content Protection schemes, and generates a Bus Key
 * as the result of that authentication.
 *
 * CDB Format (12 bytes):
 *   [0]     Opcode (0xA4)
 *   [1]     Reserved
 *   [2-5]   Start LBA (for block-based key formats)
 *   [6]     Block Count
 *   [7]     Key Class
 *   [8-9]   Allocation Length
 *   [10]    AGID (bits 7-6), Key Format (bits 5-0)
 *   [11]    Control
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param start_llba    Starting logical block address
 * @param block_cnt     Block count
 * @param resp          Buffer to receive response data
 * @param resp_len      Size of response buffer in bytes
 * @param key_class     Key class (0xE0 for PS3 BD auth)
 * @param agid          Authentication Grant ID (2 bits)
 * @param key_fmt       Key format (6 bits)
 * @param ctrl          Control byte
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_report_key(int sg_fd, unsigned int start_llba, unsigned char block_cnt,
                     void * resp, int resp_len, unsigned char key_class,
                     unsigned char agid, unsigned char key_fmt,
                     unsigned char ctrl, int noisy, int verbose)
{
    int res, ret, sense_cat;
    unsigned char stpgCmdBlk[REPORT_KEY_IN_CMDLEN] =
        {REPORT_KEY_IN_CMD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;

    stpgCmdBlk[2] = (start_llba >> 24) & 0xff;
    stpgCmdBlk[3] = (start_llba >> 16) & 0xff;
    stpgCmdBlk[4] = (start_llba >> 8) & 0xff;
    stpgCmdBlk[5] = start_llba & 0xff;
    stpgCmdBlk[6] = block_cnt;
    stpgCmdBlk[7] = key_class;
    stpgCmdBlk[8] = (resp_len >> 8) & 0xff;
    stpgCmdBlk[9] = resp_len & 0xff;
    stpgCmdBlk[10] = ((agid & 0x3) << 6) | (key_fmt & 0x3f);
    stpgCmdBlk[11] = ctrl;

    if (NULL == sg_warnings_strm)
        sg_warnings_strm = stderr;

    if (verbose) {
        int k;
        pr2ws("    report key cdb: ");
        for (k = 0; k < REPORT_KEY_IN_CMDLEN; ++k)
            pr2ws("%02x ", stpgCmdBlk[k]);
        pr2ws("\n");
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2ws("report key: out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, stpgCmdBlk, sizeof(stpgCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_in(ptvp, (unsigned char *)resp, resp_len);
    res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
    /* Note: For DATA_IN commands, mx_di_len must be non-zero to process response */
    ret = sg_cmds_process_resp(ptvp, "report key", res, resp_len,
                               sense_b, noisy, verbose, &sense_cat);
    if (-1 == ret) {
        /* Transport error */
        ;
    } else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
            ret = 0;
            break;
        default:
            ret = sense_cat;
            break;
        }
    } else {
        if ((verbose > 2) && (ret > 0) && resp) {
            pr2ws("    report key: response%s\n",
                  (ret > 256 ? ", first 256 bytes" : ""));
            dStrHexErr((const char *)resp, (ret > 256 ? 256 : ret), -1);
        }
        ret = 0;
    }
    destruct_scsi_pt_obj(ptvp);
    return ret;
}

/**
 * @brief Send SAC authentication key to PS3 BluRay drive
 *
 * Variant of SEND KEY used for SAC (Secure Authenticated Channel) protocol.
 * The parameter list length is placed in bytes [2-5] instead of [8-9].
 *
 * CDB Format (12 bytes):
 *   [0]     Opcode (0xA3)
 *   [1]     Reserved
 *   [2-5]   Parameter List Length (32-bit, big-endian)
 *   [6]     VCPS Function
 *   [7]     Key Class
 *   [8-9]   Reserved
 *   [10]    AGID (bits 7-6), Key Format (bits 5-0)
 *   [11]    Control
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param paramp        Pointer to parameter data to send
 * @param param_list_len Length of parameter data in bytes
 * @param vcps_fun      VCPS function code
 * @param key_class     Key class
 * @param agid          Authentication Grant ID (2 bits)
 * @param key_fmt       Key format (6 bits)
 * @param ctrl          Control byte
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_sac_send_key(int sg_fd,
                       void * paramp, int param_list_len,
                       unsigned char vcps_fun, unsigned char key_class,
                       unsigned char agid, unsigned char key_fmt,
                       unsigned char ctrl, int noisy, int verbose)
{
	int k, res, ret, sense_cat;
	unsigned char rtpgCmdBlk[SEND_KEY_OUT_CMDLEN] =
	{SEND_KEY_OUT_CMD, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	unsigned char sense_b[SENSE_BUFF_LEN];
	struct sg_pt_base * ptvp;

	rtpgCmdBlk[2] = (param_list_len >> 24) & 0xff;
	rtpgCmdBlk[3] = (param_list_len >> 16) & 0xff;
	rtpgCmdBlk[4] = (param_list_len >> 8) & 0xff;
	rtpgCmdBlk[5] = param_list_len & 0xff; 
	rtpgCmdBlk[6] = vcps_fun;
	rtpgCmdBlk[7] = key_class;
	rtpgCmdBlk[10] = ((agid & 0x3) << 6) | (key_fmt & 0x3f);
	rtpgCmdBlk[11] = ctrl;
	if (NULL == sg_warnings_strm)
		sg_warnings_strm = stderr;
	if (verbose) {
		pr2ws("    send key: ");
		for (k = 0; k < SEND_KEY_OUT_CMDLEN; ++k)
			pr2ws("%02x ", rtpgCmdBlk[k]);
		pr2ws("\n");
	}

	ptvp = construct_scsi_pt_obj();
	if (NULL == ptvp) {
		pr2ws("send key: out of "
			"memory\n");
		return -1;
	}
	set_scsi_pt_cdb(ptvp, rtpgCmdBlk, sizeof(rtpgCmdBlk));
	set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
	set_scsi_pt_data_out(ptvp, (unsigned char *)paramp, param_list_len);
	res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
	ret = sg_cmds_process_resp(ptvp, "send key", res,
		param_list_len, sense_b, noisy, verbose,
		&sense_cat);
	if (-1 == ret)
		;
	else if (-2 == ret) {
		switch (sense_cat) {
		case SG_LIB_CAT_RECOVERED:
		case SG_LIB_CAT_NO_SENSE:
			ret = 0;
			break;
		default:
			ret = sense_cat;
			break;
		}
	} else {
		if ((verbose > 2) && (ret > 0)) {
			pr2ws("    send key: "
				"response%s\n", (ret > 256 ? ", first 256 bytes" : ""));
			dStrHexErr((const char *)paramp, (ret > 256 ? 256 : ret), -1);
		}
		ret = 0;
	}
	destruct_scsi_pt_obj(ptvp);
	return ret;
} 

/**
 * @brief Request SAC authentication key from PS3 BluRay drive
 *
 * Variant of REPORT KEY used for SAC (Secure Authenticated Channel) protocol.
 * The allocation length is placed in bytes [2-5] instead of [8-9].
 *
 * CDB Format (12 bytes):
 *   [0]     Opcode (0xA4)
 *   [1]     Reserved
 *   [2-5]   Allocation Length (32-bit, big-endian)
 *   [6]     Block Count
 *   [7]     Key Class
 *   [8-9]   Reserved
 *   [10]    AGID (bits 7-6), Key Format (bits 5-0)
 *   [11]    Control
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param block_cnt     Block count
 * @param resp          Buffer to receive response data
 * @param resp_len      Size of response buffer in bytes
 * @param key_class     Key class
 * @param agid          Authentication Grant ID (2 bits)
 * @param key_fmt       Key format (6 bits)
 * @param ctrl          Control byte
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_sac_report_key(int sg_fd, unsigned char block_cnt,
                         void * resp, int resp_len, unsigned char key_class,
                         unsigned char agid, unsigned char key_fmt,
                         unsigned char ctrl, int noisy, int verbose)
{
    int k, res, ret, sense_cat;
	unsigned char stpgCmdBlk[REPORT_KEY_IN_CMDLEN] =
	{REPORT_KEY_IN_CMD, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	unsigned char sense_b[SENSE_BUFF_LEN];
	struct sg_pt_base * ptvp;

	stpgCmdBlk[2] = (resp_len >> 24) & 0xff;
	stpgCmdBlk[3] = (resp_len >> 16) & 0xff;
	stpgCmdBlk[4] = (resp_len >> 8) & 0xff;
	stpgCmdBlk[5] = resp_len & 0xff; 
	stpgCmdBlk[6] = block_cnt;
	stpgCmdBlk[7] = key_class;
	stpgCmdBlk[10] = ((agid & 0x3) << 6) | (key_fmt & 0x3f);
	stpgCmdBlk[11] = ctrl;
	if (NULL == sg_warnings_strm)
		sg_warnings_strm = stderr;
	if (verbose) {
		pr2ws("    report key: ");
		for (k = 0; k < REPORT_KEY_IN_CMDLEN; ++k)
			pr2ws("%02x ", stpgCmdBlk[k]);
		pr2ws("\n");
		if ((verbose > 1) && resp && resp_len) {
			pr2ws("    report key "
				"parameter list:\n");
			dStrHexErr((const char *)resp, resp_len, -1);
		}
	}

	ptvp = construct_scsi_pt_obj();
	if (NULL == ptvp) {
		pr2ws("report key: out of "
			"memory\n");
		return -1;
	}
	set_scsi_pt_cdb(ptvp, stpgCmdBlk, sizeof(stpgCmdBlk));
	set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
	set_scsi_pt_data_in(ptvp, (unsigned char *)resp, resp_len);
	res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
	ret = sg_cmds_process_resp(ptvp, "report key", res, 0,
		sense_b, noisy, verbose, &sense_cat);
	if (-1 == ret)
		;
	else if (-2 == ret) {
		switch (sense_cat) {
		case SG_LIB_CAT_RECOVERED:
		case SG_LIB_CAT_NO_SENSE:
			ret = 0;
			break;
		default:
			ret = sense_cat;
			break;
		}
	} else
		ret = 0;
	destruct_scsi_pt_obj(ptvp);
	return ret;
} 

/**
 * @brief PS3 E0 command - Report key for session establishment
 *
 * Proprietary PS3 command (opcode 0xE0) used during session establishment.
 * The command receives encrypted challenge/response data from the drive.
 *
 * CDB Format (12 bytes):
 *   [0]     Opcode (0xE0)
 *   [1]     Reserved (0x00)
 *   [2]     Allocation Length (8-bit)
 *   [3]     Reserved (0x00)
 *   [4-11]  Command-specific data (from cdb parameter)
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param resp          Buffer to receive response data
 * @param mx_resp_len   Size of response buffer in bytes
 * @param cdb           8 bytes of command-specific data for CDB[4-11]
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_e0_report_key(int sg_fd, void * resp, int mx_resp_len,
                        const unsigned char cdb[8], int noisy, int verbose)
{
    int res, ret, sense_cat;
    unsigned char rtpgCmdBlk[E0_KEY_OUT_CMDLEN] =
        {E0_KEY_OUT_CMD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;

    rtpgCmdBlk[1] = 0x00;
    rtpgCmdBlk[2] = mx_resp_len & 0xff;
    rtpgCmdBlk[3] = 0x00;
    memcpy(rtpgCmdBlk + 4, cdb, 8);

    if (NULL == sg_warnings_strm)
        sg_warnings_strm = stderr;

    if (verbose) {
        int k;
        pr2ws("    e0 report key cdb: ");
        for (k = 0; k < E0_KEY_OUT_CMDLEN; ++k)
            pr2ws("%02x ", rtpgCmdBlk[k]);
        pr2ws("\n");
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2ws("e0 report key: out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, rtpgCmdBlk, sizeof(rtpgCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_in(ptvp, (unsigned char *)resp, mx_resp_len);
    res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
    ret = sg_cmds_process_resp(ptvp, "e0 report key", res,
                               mx_resp_len, sense_b, noisy, verbose,
                               &sense_cat);
    if (-1 == ret) {
        /* Transport error */
        ;
    } else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
            ret = 0;
            break;
        default:
            ret = sense_cat;
            break;
        }
    } else {
        if ((verbose > 2) && (ret > 0) && resp) {
            pr2ws("    e0 report key: response%s\n",
                  (ret > 256 ? ", first 256 bytes" : ""));
            dStrHexErr((const char *)resp, (ret > 256 ? 256 : ret), -1);
        }
        ret = 0;
    }
    destruct_scsi_pt_obj(ptvp);
    return ret;
}

/**
 * @brief PS3 E1 command - Send key for session establishment
 *
 * Proprietary PS3 command (opcode 0xE1) used during session establishment.
 * The command sends encrypted challenge/response data to the drive.
 *
 * Note: This command may return unrecognized sense data (SG_LIB_CAT_SENSE)
 * which is treated as success, matching the behavior of working implementations.
 *
 * CDB Format (12 bytes):
 *   [0]     Opcode (0xE1)
 *   [1]     Reserved (0x00)
 *   [2]     Parameter Length (8-bit)
 *   [3]     Reserved (0x00)
 *   [4-11]  Command-specific data (from cdb parameter)
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param paramp        Pointer to parameter data to send
 * @param param_len     Length of parameter data in bytes
 * @param cdb           8 bytes of command-specific data for CDB[4-11]
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_e1_send_key(int sg_fd, void * paramp, int param_len,
                      const unsigned char cdb[8], int noisy, int verbose)
{
    int res, ret, sense_cat;
    unsigned char stpgCmdBlk[E1_KEY_OUT_CMDLEN] =
        {E1_KEY_OUT_CMD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;

    stpgCmdBlk[1] = 0x00;
    stpgCmdBlk[2] = param_len & 0xff;
    stpgCmdBlk[3] = 0x00;
    memcpy(stpgCmdBlk + 4, cdb, 8);

    if (NULL == sg_warnings_strm)
        sg_warnings_strm = stderr;

    if (verbose) {
        int k;
        pr2ws("    e1 send key cdb: ");
        for (k = 0; k < E1_KEY_OUT_CMDLEN; ++k)
            pr2ws("%02x ", stpgCmdBlk[k]);
        pr2ws("\n");
        if ((verbose > 1) && paramp && param_len) {
            pr2ws("    e1 send key parameter data:\n");
            dStrHexErr((const char *)paramp, param_len, -1);
        }
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2ws("e1 send key: out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, stpgCmdBlk, sizeof(stpgCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_out(ptvp, (unsigned char *)paramp, param_len);
    res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
    /* Note: For DATA_OUT commands, mx_di_len should be 0 */
    ret = sg_cmds_process_resp(ptvp, "e1 send key", res, 0,
                               sense_b, noisy, verbose, &sense_cat);
    if (-1 == ret) {
        /* Transport error */
        ;
    } else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
        case SG_LIB_CAT_SENSE:
            /*
             * E1 command may return unrecognized sense data with empty
             * sense buffer. Working implementations don't check SCSI status
             * for this command, so we treat SG_LIB_CAT_SENSE as success.
             */
            ret = 0;
            break;
        default:
            ret = sense_cat;
            break;
        }
    } else {
        ret = 0;
    }
    destruct_scsi_pt_obj(ptvp);
    return ret;
}

/**
 * @brief PS3 D7 command - Set drive state flag
 *
 * Proprietary PS3 command (opcode 0xD7) used to set a drive state flag.
 * This is a DATA_IN command that reads back drive state after setting.
 *
 * CDB Format (12 bytes):
 *   [0]     Opcode (0xD7)
 *   [1]     Subcommand (0x1A for set)
 *   [2]     Mode page code high (0x0E)
 *   [3]     Mode page code low (0x0F)
 *   [4-5]   Reserved
 *   [6]     Reserved (0x06)
 *   [7]     Allocation length (0x72)
 *   [8-10]  Reserved
 *   [11]    Flag value to set
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param flag          Flag value to set
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_d7_set(int sg_fd, unsigned char flag, int noisy, int verbose)
{
    int res, ret, sense_cat;
    unsigned char paramp[0x72];
    unsigned char stpgCmdBlk[D7_KEY_IN_CMDLEN] =
        {D7_KEY_IN_CMD, 0x1a, 0x0e, 0x0f, 0, 0, 0x06, 0x72, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;
    int param_len = sizeof(paramp);

    /* Initialize buffer to zero (fix: was uninitialized) */
    memset(paramp, 0, sizeof(paramp));

    stpgCmdBlk[11] = flag;

    if (NULL == sg_warnings_strm)
        sg_warnings_strm = stderr;

    if (verbose) {
        int k;
        pr2ws("    d7 set cdb: ");
        for (k = 0; k < D7_KEY_IN_CMDLEN; ++k)
            pr2ws("%02x ", stpgCmdBlk[k]);
        pr2ws("\n");
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2ws("d7 set: out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, stpgCmdBlk, sizeof(stpgCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_in(ptvp, paramp, param_len);
    res = do_scsi_pt(ptvp, sg_fd, D7_TIMEOUT, verbose);
    /* Fix: Use param_len instead of 0 for DATA_IN command */
    ret = sg_cmds_process_resp(ptvp, "d7 set", res, param_len,
                               sense_b, noisy, verbose, &sense_cat);
    if (-1 == ret) {
        /* Transport error */
        ;
    } else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
            ret = 0;
            break;
        default:
            ret = sense_cat;
            break;
        }
    } else {
        ret = 0;
    }
    destruct_scsi_pt_obj(ptvp);
    return ret;
}

/**
 * @brief PS3 D7 command - Get drive state flag
 *
 * Proprietary PS3 command (opcode 0xD7) used to read a drive state flag.
 *
 * CDB Format (12 bytes):
 *   [0]     Opcode (0xD7)
 *   [1]     Subcommand (0x1A for get)
 *   [2]     Mode page code high (0x0F)
 *   [3]     Mode page code low (0x0F)
 *   [4-5]   Reserved
 *   [6]     Reserved (0x06)
 *   [7]     Allocation length (0x72)
 *   [8-11]  Reserved
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param flag          Pointer to receive flag value (must not be NULL)
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_d7_get(int sg_fd, unsigned char *flag, int noisy, int verbose)
{
    int res, ret, sense_cat;
    unsigned char paramp[0x72];
    unsigned char stpgCmdBlk[D7_KEY_IN_CMDLEN] =
        {D7_KEY_IN_CMD, 0x1a, 0x0f, 0x0f, 0, 0, 0x06, 0x72, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;
    int param_len = sizeof(paramp);

    /* Validate output parameter */
    if (NULL == flag) {
        pr2ws("d7 get: flag pointer is NULL\n");
        return -1;
    }

    /* Initialize buffer to zero */
    memset(paramp, 0, sizeof(paramp));

    if (NULL == sg_warnings_strm)
        sg_warnings_strm = stderr;

    if (verbose) {
        int k;
        pr2ws("    d7 get cdb: ");
        for (k = 0; k < D7_KEY_IN_CMDLEN; ++k)
            pr2ws("%02x ", stpgCmdBlk[k]);
        pr2ws("\n");
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2ws("d7 get: out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, stpgCmdBlk, sizeof(stpgCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_in(ptvp, paramp, param_len);
    /* Use shorter D7_TIMEOUT matching original implementation */
    res = do_scsi_pt(ptvp, sg_fd, D7_TIMEOUT, verbose);
    /* Fix: Use param_len instead of 0 for DATA_IN command */
    ret = sg_cmds_process_resp(ptvp, "d7 get", res, param_len,
                               sense_b, noisy, verbose, &sense_cat);
    if (-1 == ret) {
        /* Transport error */
        ;
    } else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
            ret = 0;
            *flag = paramp[11];
            break;
        default:
            ret = sense_cat;
            break;
        }
    } else {
        ret = 0;
        *flag = paramp[11];
    }

    destruct_scsi_pt_obj(ptvp);
    return ret;
}

/**
 * @brief Read disc structure from PS3 BluRay drive
 *
 * The READ DISC STRUCTURE command retrieves disc structure information
 * such as physical format info, copyright info, and disc key.
 *
 * CDB Format (12 bytes):
 *   [0]     Opcode (0xAD)
 *   [1]     Media Type (bits 3-0)
 *   [2-5]   Address (LBA)
 *   [6]     Layer Number
 *   [7]     Format
 *   [8-9]   Allocation Length
 *   [10]    AGID (bits 7-6)
 *   [11]    Control
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param media_type    Media type (0 = DVD, 1 = BD)
 * @param address       LBA address (format-dependent)
 * @param layer_nr      Layer number (0 or 1)
 * @param fmt           Structure format code
 * @param agid          Authentication Grant ID (2 bits)
 * @param ctrl          Control byte
 * @param resp          Buffer to receive response data
 * @param mx_resp_len   Size of response buffer in bytes
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_read_disc_structure(int sg_fd, int media_type, unsigned int address,
                              int layer_nr, int fmt, int agid,
                              unsigned char ctrl, void * resp,
                              int mx_resp_len, int noisy, int verbose)
{
    int res, ret, sense_cat;
    unsigned char gpCmdBlk[READ_DISC_STRUCTURE_CMDLEN] =
        {READ_DISC_STRUCTURE_CMD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;

    if (NULL == sg_warnings_strm)
        sg_warnings_strm = stderr;

    gpCmdBlk[1] = (media_type & 0xf);
    gpCmdBlk[2] = (unsigned char)((address >> 24) & 0xff);
    gpCmdBlk[3] = (unsigned char)((address >> 16) & 0xff);
    gpCmdBlk[4] = (unsigned char)((address >> 8) & 0xff);
    gpCmdBlk[5] = (unsigned char)(address & 0xff);
    gpCmdBlk[6] = (unsigned char)layer_nr;
    gpCmdBlk[7] = (unsigned char)fmt;
    gpCmdBlk[8] = (unsigned char)((mx_resp_len >> 8) & 0xff);
    gpCmdBlk[9] = (unsigned char)(mx_resp_len & 0xff);
    gpCmdBlk[10] = (agid & 0x3) << 6;
    gpCmdBlk[11] = ctrl;

    if (verbose) {
        int k;
        pr2ws("    read disc structure cdb: ");
        for (k = 0; k < READ_DISC_STRUCTURE_CMDLEN; ++k)
            pr2ws("%02x ", gpCmdBlk[k]);
        pr2ws("\n");
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2ws("read disc structure: out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, gpCmdBlk, sizeof(gpCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_in(ptvp, (unsigned char *)resp, mx_resp_len);
    res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
    ret = sg_cmds_process_resp(ptvp, "read disc structure", res, mx_resp_len,
                               sense_b, noisy, verbose, &sense_cat);
    if (-1 == ret) {
        /* Transport error */
        ;
    } else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_INVALID_OP:
        case SG_LIB_CAT_ILLEGAL_REQ:
        case SG_LIB_CAT_UNIT_ATTENTION:
        case SG_LIB_CAT_ABORTED_COMMAND:
            ret = sense_cat;
            break;
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
            ret = 0;
            break;
        default:
            ret = -1;
            break;
        }
    } else {
        if ((verbose > 2) && (ret > 3) && resp) {
            unsigned char * ucp;
            int len;

            ucp = (unsigned char *)resp;
            len = (ucp[0] << 24) + (ucp[1] << 16) + (ucp[2] << 8) + ucp[3] + 4;
            if (len < 0)
                len = 0;
            len = (ret < len) ? ret : len;
            pr2ws("    read disc structure: response%s\n",
                  (len > 256 ? ", first 256 bytes" : ""));
            dStrHexErr((const char *)resp, (len > 256 ? 256 : len), -1);
        }
        ret = 0;
    }
    destruct_scsi_pt_obj(ptvp);
    return ret;
}

/* SCSI command opcodes for TEST UNIT READY */
#define TUR_CMD         0x00    /**< TEST UNIT READY command opcode */
#define TUR_CMDLEN      6       /**< TEST UNIT READY CDB length */

/**
 * @brief PS3-specific TEST UNIT READY with sense code extraction
 *
 * Invokes a SCSI TEST UNIT READY command and returns the full sense code
 * (sense_key << 16 | ASC << 8 | ASCQ) in the req_sense output parameter.
 * This matches the original bd_util.c implementation which returns sense
 * codes in a 24-bit format for firmware update status checking.
 *
 * CDB Format (6 bytes):
 *   [0]     Opcode (0x00)
 *   [1-5]   Reserved
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param req_sense     Pointer to receive sense code (sense_key<<16|ASC<<8|ASCQ)
 *                      Must not be NULL
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, -1 on transport error, -2 on command failure with sense
 */
int
sg_ll_ps3_test_unit_ready(int sg_fd, unsigned int *req_sense,
                          int noisy, int verbose)
{
    int k, res, ret, sense_cat;
    unsigned char turCmdBlk[TUR_CMDLEN] = {TUR_CMD, 0, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;

    if (NULL == req_sense) {
        pr2ws("test unit ready: req_sense pointer is NULL\n");
        return -1;
    }

    *req_sense = 0;

    if (NULL == sg_warnings_strm)
        sg_warnings_strm = stderr;

    if (verbose) {
        pr2ws("    test unit ready cdb: ");
        for (k = 0; k < TUR_CMDLEN; ++k)
            pr2ws("%02x ", turCmdBlk[k]);
        pr2ws("\n");
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2ws("test unit ready: out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, turCmdBlk, sizeof(turCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    res = do_scsi_pt(ptvp, sg_fd, D7_TIMEOUT, verbose);
    ret = sg_cmds_process_resp(ptvp, "test unit ready", res, 0,
                               sense_b, noisy, verbose, &sense_cat);
    if (-1 == ret) {
        /* Transport error */
        destruct_scsi_pt_obj(ptvp);
        return -1;
    } else if (-2 == ret) {
        /* Command completed with sense data - extract sense key/ASC/ASCQ */
        int slen = get_scsi_pt_sense_len(ptvp);
        if (slen >= 14) {
            /* Fixed format sense: sense_key in byte 2, ASC in byte 12, ASCQ in byte 13 */
            *req_sense = ((sense_b[2] & 0x0F) << 16) |
                         (sense_b[12] << 8) |
                         sense_b[13];
        } else if (slen >= 3) {
            /* Minimal sense data */
            *req_sense = (sense_b[2] & 0x0F) << 16;
        }
        destruct_scsi_pt_obj(ptvp);
        return -2;
    } else {
        /* Success - no sense data */
        destruct_scsi_pt_obj(ptvp);
        return 0;
    }
}

/**
 * @brief Set drive buffer mode via MODE SELECT (10)
 *
 * Writes a mode page (0x2D) to enable buffer write operations on the drive.
 * This must be called before using WRITE BUFFER commands.
 *
 * Mode Page 0x2D Format (6 bytes):
 *   [0]     Page code (0x2D)
 *   [1]     Page length (0x06)
 *   [2]     Buffer ID
 *   [3-5]   Reserved
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param buffer_id     Buffer ID to enable for writing
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_write_mode(int sg_fd, int buffer_id, int noisy, int verbose)
{
    unsigned char ref_md[16];
    int res;

    memset(ref_md, 0, sizeof(ref_md));

    /* Mode parameter header (8 bytes) + mode page (8 bytes) */
    ref_md[1]  = 0x0e;              /* Mode data length (14 bytes following) */
    ref_md[8]  = 0x2d;              /* Mode page code */
    ref_md[9]  = 0x06;              /* Mode page length */
    ref_md[10] = buffer_id & 0xff;  /* Buffer ID to enable */

    res = sg_ll_mode_select10(sg_fd, 1 /* PF */, 0 /* save */, ref_md,
                              sizeof(ref_md), noisy, verbose);

    return res;
}

/**
 * @brief PS3-specific MODE SELECT (10) command
 *
 * Extended MODE SELECT (10) command with additional PS3-specific parameters.
 * This variant includes reserved, NACA, and flag fields not present in
 * standard MODE SELECT.
 *
 * CDB Format (10 bytes):
 *   [0]     Opcode (0x55)
 *   [1]     PF (bit 4), Reserved (bits 3-1), SP (bit 0)
 *   [2-6]   Reserved
 *   [7-8]   Parameter List Length
 *   [9]     NACA (bit 2), Flag (bit 1)
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param pf            Page Format bit (1 = page format, 0 = vendor-specific)
 * @param reserved      Reserved field value (3 bits)
 * @param sp            Save Pages bit
 * @param naca          Normal ACA bit
 * @param flag          PS3-specific flag bit
 * @param paramp        Pointer to mode page data
 * @param param_len     Length of mode page data in bytes
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_mode_select10(int sg_fd, int pf, int reserved, int sp, int naca,
                        int flag, void * paramp, int param_len,
                        int noisy, int verbose)
{
    int res, ret, sense_cat;
    unsigned char modesCmdBlk[MODE_SELECT10_CMDLEN] =
        {MODE_SELECT10_CMD, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base * ptvp;

    modesCmdBlk[1] = (unsigned char)(((pf << 4) & 0x10) |
                                     ((reserved << 1) & 0xE) |
                                     (sp & 0x1));
    sg_put_unaligned_be16((int16_t)param_len, modesCmdBlk + 7);
    modesCmdBlk[9] = (unsigned char)(((naca << 2) & 0x4) |
                                     ((flag << 1) & 0x2));

    if (param_len > 0xffff) {
        pr2ws("mode select (10): param_len too big\n");
        return -1;
    }

    if (verbose) {
        int k;
        pr2ws("    mode select (10) cdb: ");
        for (k = 0; k < MODE_SELECT10_CMDLEN; ++k)
            pr2ws("%02x ", modesCmdBlk[k]);
        pr2ws("\n");
    }
    if (verbose > 1) {
        pr2ws("    mode select (10) parameter list\n");
        dStrHexErr((const char *)paramp, param_len, -1);
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2ws("mode select (10): out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, modesCmdBlk, sizeof(modesCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_out(ptvp, (unsigned char *)paramp, param_len);
    res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
    /* Note: For DATA_OUT commands, mx_di_len should be 0 */
    ret = sg_cmds_process_resp(ptvp, "mode select (10)", res, 0, sense_b,
                               noisy, verbose, &sense_cat);
    if (-1 == ret) {
        /* Transport error */
        ;
    } else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
            ret = 0;
            break;
        default:
            ret = sense_cat;
            break;
        }
    } else {
        ret = 0;
    }

    destruct_scsi_pt_obj(ptvp);
    return ret;
}

/**
 * @brief Read sectors from drive using READ (12) command
 *
 * The READ (12) command reads one or more logical blocks from the medium.
 * This is the standard MMC READ (12) command used for reading sectors
 * from optical media including SACD discs.
 *
 * CDB Format (12 bytes):
 *   [0]     Opcode (0xA8)
 *   [1]     Reserved
 *   [2-5]   Logical Block Address (big-endian)
 *   [6-9]   Transfer Length in blocks (big-endian)
 *   [10]    Reserved
 *   [11]    Control
 *
 * @param sg_fd         SCSI generic file descriptor
 * @param lba           Starting logical block address
 * @param num_sectors   Number of sectors to read
 * @param buffer        Buffer to receive data (must be num_sectors * sector_size bytes)
 * @param sector_size   Size of each sector in bytes (typically 2048)
 * @param noisy         If non-zero, print error messages
 * @param verbose       Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_read12(int sg_fd, uint32_t lba, uint32_t num_sectors,
                 void *buffer, int sector_size, int noisy, int verbose)
{
    int res, ret, sense_cat;
    int transfer_len;
    unsigned char readCmdBlk[READ12_CMDLEN] =
        {READ12_CMD, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base *ptvp;

    if (NULL == buffer) {
        pr2ws("read12: buffer pointer is NULL\n");
        return -1;
    }

    if (num_sectors == 0) {
        return 0;
    }

    transfer_len = (int)(num_sectors * (uint32_t)sector_size);

    /* Build CDB: LBA in bytes 2-5, transfer length in bytes 6-9 */
    readCmdBlk[2] = (unsigned char)((lba >> 24) & 0xff);
    readCmdBlk[3] = (unsigned char)((lba >> 16) & 0xff);
    readCmdBlk[4] = (unsigned char)((lba >> 8) & 0xff);
    readCmdBlk[5] = (unsigned char)(lba & 0xff);

    readCmdBlk[6] = (unsigned char)((num_sectors >> 24) & 0xff);
    readCmdBlk[7] = (unsigned char)((num_sectors >> 16) & 0xff);
    readCmdBlk[8] = (unsigned char)((num_sectors >> 8) & 0xff);
    readCmdBlk[9] = (unsigned char)(num_sectors & 0xff);

    if (NULL == sg_warnings_strm)
        sg_warnings_strm = stderr;

    if (verbose) {
        int k;
        pr2ws("    read12 cdb: ");
        for (k = 0; k < READ12_CMDLEN; ++k)
            pr2ws("%02x ", readCmdBlk[k]);
        pr2ws("\n");
        pr2ws("    lba=0x%08x, num_sectors=%u, transfer_len=%d\n",
              lba, num_sectors, transfer_len);
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2ws("read12: out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, readCmdBlk, sizeof(readCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_in(ptvp, (unsigned char *)buffer, transfer_len);
    res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
    ret = sg_cmds_process_resp(ptvp, "read12", res, transfer_len,
                               sense_b, noisy, verbose, &sense_cat);
    if (-1 == ret) {
        /* Transport error */
        ;
    } else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
            ret = 0;
            break;
        default:
            ret = sense_cat;
            break;
        }
    } else {
        if ((verbose > 2) && (ret > 0) && buffer) {
            pr2ws("    read12: read %d bytes\n", ret);
        }
        ret = 0;
    }
    destruct_scsi_pt_obj(ptvp);
    return ret;
}

/**
 * @brief Get event status notification from drive
 *
 * The GET EVENT STATUS NOTIFICATION command retrieves asynchronous event
 * status from the drive. This is commonly used to detect media changes
 * (disc insertion/removal) and other drive events.
 *
 * CDB Format (10 bytes):
 *   [0]     Opcode (0x4A)
 *   [1]     Polled (bit 0): 1 = polled, 0 = asynchronous
 *   [2-3]   Reserved
 *   [4]     Notification Class Request (bit mask)
 *   [5-6]   Reserved
 *   [7-8]   Allocation Length (big-endian)
 *   [9]     Control
 *
 * Notification Class Request values:
 *   0x01 = Operational Change
 *   0x02 = Power Management
 *   0x04 = External Request
 *   0x10 = Media (disc insertion/removal)
 *   0x20 = Multi-Initiator
 *   0x40 = Device Busy
 *
 * @param sg_fd                      SCSI generic file descriptor
 * @param polled                     1 for polled operation, 0 for async
 * @param notification_class_request Bit mask of event classes to query
 * @param resp                       Buffer to receive response data
 * @param resp_len                   Size of response buffer in bytes
 * @param noisy                      If non-zero, print error messages
 * @param verbose                    Verbosity level for debug output
 * @return 0 on success, SG_LIB_CAT_* error code or -1 on failure
 */
int
sg_ll_ps3_get_event_status_notification(int sg_fd, int polled,
                                         int notification_class_request,
                                         void *resp, int resp_len,
                                         int noisy, int verbose)
{
    int res, ret, sense_cat;
    unsigned char gesnCmdBlk[GET_EVENT_STATUS_NOTIFICATION_CMDLEN] =
        {GET_EVENT_STATUS_NOTIFICATION_CMD, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    unsigned char sense_b[SENSE_BUFF_LEN];
    struct sg_pt_base *ptvp;

    if (NULL == resp) {
        pr2ws("get event status notification: resp pointer is NULL\n");
        return -1;
    }

    /* Build CDB */
    gesnCmdBlk[1] = polled ? 0x01 : 0x00;
    gesnCmdBlk[4] = (unsigned char)(notification_class_request & 0xff);
    gesnCmdBlk[7] = (unsigned char)((resp_len >> 8) & 0xff);
    gesnCmdBlk[8] = (unsigned char)(resp_len & 0xff);

    if (NULL == sg_warnings_strm)
        sg_warnings_strm = stderr;

    if (verbose) {
        int k;
        pr2ws("    get event status notification cdb: ");
        for (k = 0; k < GET_EVENT_STATUS_NOTIFICATION_CMDLEN; ++k)
            pr2ws("%02x ", gesnCmdBlk[k]);
        pr2ws("\n");
    }

    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        pr2ws("get event status notification: out of memory\n");
        return -1;
    }
    set_scsi_pt_cdb(ptvp, gesnCmdBlk, sizeof(gesnCmdBlk));
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_in(ptvp, (unsigned char *)resp, resp_len);
    res = do_scsi_pt(ptvp, sg_fd, DEF_PT_TIMEOUT, verbose);
    ret = sg_cmds_process_resp(ptvp, "get event status notification", res,
                               resp_len, sense_b, noisy, verbose, &sense_cat);
    if (-1 == ret) {
        /* Transport error */
        ;
    } else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
            ret = 0;
            break;
        default:
            ret = sense_cat;
            break;
        }
    } else {
        if ((verbose > 2) && (ret > 0) && resp) {
            pr2ws("    get event status notification: received %d bytes\n", ret);
            dStrHexErr((const char *)resp, (ret > 64 ? 64 : ret), -1);
        }
        ret = 0;
    }
    destruct_scsi_pt_obj(ptvp);
    return ret;
}

/*============================================================================
 * Drive Feature Detection Implementation
 *============================================================================*/

/**
 * @brief Value-description pair for profile and feature lookups
 *
 * Used internally to map numeric codes to human-readable strings.
 */
struct val_desc_t {
    int val;            /**< Numeric value (profile or feature code) */
    const char *desc;   /**< Human-readable description */
};

/**
 * @brief Profile descriptions table
 *
 * Maps disc profile numbers to their descriptions. Profile numbers are
 * returned in the GET CONFIGURATION response (bytes 6-7) and indicate
 * the current disc type.
 *
 * Standard profiles (MMC):
 *   0x00-0x05: Legacy formats
 *   0x08-0x0a: CD formats
 *   0x10-0x1b: DVD formats
 *   0x40-0x43: BD formats
 *   0x50-0x5a: HD DVD formats
 *
 * Sony PS3-specific profiles (0xFFxx):
 *   0xff50: PSX CD-ROM
 *   0xff60-0xff61: PS2 formats
 *   0xff70-0xff71: PS3 formats
 */
static struct val_desc_t profile_desc_arr[] = {
    { 0x0, "No current profile" },
    { 0x1, "Non-removable disk (obs)" },
    { 0x2, "Removable disk" },
    { 0x3, "Magneto optical erasable" },
    { 0x4, "Optical write once" },
    { 0x5, "AS-MO" },
    { 0x8, "CD-ROM" },
    { 0x9, "CD-R" },
    { 0xa, "CD-RW" },
    { 0x10, "DVD-ROM" },
    { 0x11, "DVD-R sequential recording" },
    { 0x12, "DVD-RAM" },
    { 0x13, "DVD-RW restricted overwrite" },
    { 0x14, "DVD-RW sequential recording" },
    { 0x15, "DVD-R dual layer sequental recording" },
    { 0x16, "DVD-R dual layer jump recording" },
    { 0x17, "DVD-RW dual layer" },
    { 0x18, "DVD-Download disc recording" },
    { 0x1a, "DVD+RW" },
    { 0x1b, "DVD+R" },
    { 0x20, "DDCD-ROM" },
    { 0x21, "DDCD-R" },
    { 0x22, "DDCD-RW" },
    { 0x2a, "DVD+RW dual layer" },
    { 0x2b, "DVD+R dual layer" },
    { 0x40, "BD-ROM" },
    { 0x41, "BD-R SRM" },
    { 0x42, "BD-R RRM" },
    { 0x43, "BD-RE" },
    { 0x50, "HD DVD-ROM" },
    { 0x51, "HD DVD-R" },
    { 0x52, "HD DVD-RAM" },
    { 0x53, "HD DVD-RW" },
    { 0x58, "HD DVD-R dual layer" },
    { 0x5a, "HD DVD-RW dual layer" },
    { 0xff50, "PSX CD-ROM" },
    { 0xff60, "PS2 CD-ROM" },
    { 0xff61, "PS2 DVD-ROM" },
    { 0xff70, "PS3 DVD-ROM" },
    { 0xff71, "PS3 BD-ROM" },
    { 0xffff, "Non-conforming profile" },
    { -1, NULL },
};

/**
 * @brief Feature descriptions table
 *
 * Maps feature codes to their descriptions. Feature codes are found in
 * the feature descriptor headers of GET CONFIGURATION responses.
 *
 * Standard features (MMC):
 *   0x00-0x04: Core capabilities
 *   0x10-0x52: Read/write capabilities
 *   0x80: Hybrid disc
 *   0x100-0x142: Extended capabilities
 *
 * Sony PS3-specific features (0xFFxx):
 *   0xff00: PS3 drive identifier
 *   0xff10-0xff31: PlayStation decryption features
 *   0xff40-0xff41: SACD features
 */
static struct val_desc_t feature_desc_arr[] = {
    { 0x0, "Profile list" },
    { 0x1, "Core" },
    { 0x2, "Morphing" },
    { 0x3, "Removable media" },
    { 0x4, "Write Protect" },
    { 0x10, "Random readable" },
    { 0x1d, "Multi-read" },
    { 0x1e, "CD read" },
    { 0x1f, "DVD read" },
    { 0x20, "Random writable" },
    { 0x21, "Incremental streaming writable" },
    { 0x22, "Sector erasable" },
    { 0x23, "Formattable" },
    { 0x24, "Hardware defect management" },
    { 0x25, "Write once" },
    { 0x26, "Restricted overwrite" },
    { 0x27, "CD-RW CAV write" },
    { 0x28, "MRW" },          /* Mount Rainier reWritable */
    { 0x29, "Enhanced defect reporting" },
    { 0x2a, "DVD+RW" },
    { 0x2b, "DVD+R" },
    { 0x2c, "Rigid restricted overwrite" },
    { 0x2d, "CD track-at-once" },
    { 0x2e, "CD mastering (session at once)" },
    { 0x2f, "DVD-R/-RW write" },
    { 0x30, "Double density CD read" },
    { 0x31, "Double density CD-R write" },
    { 0x32, "Double density CD-RW write" },
    { 0x33, "Layer jump recording" },
    { 0x34, "LJ rigid restricted oberwrite" },
    { 0x35, "Stop long operation" },
    { 0x37, "CD-RW media write support" },
    { 0x38, "BD-R POW" },
    { 0x3a, "DVD+RW dual layer" },
    { 0x3b, "DVD+R dual layer" },
    { 0x40, "BD read" },
    { 0x41, "BD write" },
    { 0x42, "TSR (timely safe recording)" },
    { 0x50, "HD DVD read" },
    { 0x51, "HD DVD write" },
    { 0x52, "HD DVD-RW fragment recording" },
    { 0x80, "Hybrid disc" },
    { 0x100, "Power management" },
    { 0x101, "SMART" },
    { 0x102, "Embedded changer" },
    { 0x103, "CD audio external play" },
    { 0x104, "Microcode upgrade" },
    { 0x105, "Timeout" },
    { 0x106, "DVD CSS" },
    { 0x107, "Real time streaming" },
    { 0x108, "Drive serial number" },
    { 0x109, "Media serial number" },
    { 0x10a, "Disc control blocks" },
    { 0x10b, "DVD CPRM" },
    { 0x10c, "Firmware information" },
    { 0x10d, "AACS" },
    { 0x10e, "DVD CSS managed recording" },
    { 0x110, "VCPS" },
    { 0x113, "SecurDisc" },
    { 0x120, "BD CPS" },
    { 0x142, "OSSC" },
    { 0xff00, "PS3 drive" },
    { 0xff10, "PSX CD decryption" },
    { 0xff20, "PS2 CD decryption" },
    { 0xff21, "PS2 DVD decryption" },
    { 0xff30, "PS3 DVD decryption" },
    { 0xff31, "PS3 BD decryption" },
    { 0xff40, "SACD feature 1" },
    { 0xff41, "SACD feature 2" },
    { -1, NULL },
};

/**
 * @brief Look up profile description by number
 *
 * Searches the profile_desc_arr table for a matching profile number.
 * If not found, formats the number as hexadecimal.
 *
 * @param profile_num  Profile number to look up
 * @param buff         Buffer for result (must be at least 64 bytes)
 * @return Pointer to buff containing the description
 */
const char *
sg_get_profile_str(int profile_num, char *buff)
{
    const struct val_desc_t *pdp;

    for (pdp = profile_desc_arr; pdp->desc; ++pdp) {
        if (pdp->val == profile_num) {
            strcpy(buff, pdp->desc);
            return buff;
        }
    }
    snprintf(buff, 64, "0x%x", profile_num);
    return buff;
}

/**
 * @brief Look up feature description by code
 *
 * Searches the feature_desc_arr table for a matching feature code.
 * If not found, formats the code as hexadecimal.
 *
 * @param feature_num  Feature code to look up
 * @param buff         Buffer for result (must be at least 64 bytes)
 * @return Pointer to buff containing the description
 */
const char *
sg_get_feature_str(int feature_num, char *buff)
{
    int k, num;

    num = sizeof(feature_desc_arr) / sizeof(feature_desc_arr[0]);
    for (k = 0; k < num; ++k) {
        if (feature_desc_arr[k].val == feature_num) {
            strcpy(buff, feature_desc_arr[k].desc);
            return buff;
        }
    }
    snprintf(buff, 64, "0x%x", feature_num);
    return buff;
}

/**
 * @brief Decode and print detailed feature information
 *
 * Parses feature descriptor data and prints human-readable output.
 * Handles all standard MMC features as well as PS3-proprietary features.
 *
 * Feature Descriptor Format:
 *   [0-1]  Feature code (big-endian)
 *   [2]    Version (bits 5-2), Persistent (bit 1), Current (bit 0)
 *   [3]    Additional length
 *   [4+]   Feature-specific data
 *
 * @param feature  Feature code
 * @param ucp      Pointer to feature descriptor
 * @param len      Total length of feature descriptor
 */
void
sg_decode_feature(int feature, unsigned char *ucp, int len)
{
    int k, num, n, profile;
    char buff[128];
    const char *cp;

    cp = "";
    switch (feature) {
    case 0:     /* Profile list */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 2), !!(ucp[2] & 1),
            feature);
        printf("    available profiles [more recent typically higher "
            "in list]:\n");
        for (k = 4; k < len; k += 4) {
            profile = sg_get_unaligned_be16(ucp + k);
            printf("      profile: %s , currentP=%d\n",
                sg_get_profile_str(profile, buff), !!(ucp[k + 2] & 1));
        }
        break;
    case 1:     /* Core */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 2), !!(ucp[2] & 1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        num = sg_get_unaligned_be32(ucp + 4);
        switch (num) {
        case 0: cp = "unspecified"; break;
        case 1: cp = "SCSI family"; break;
        case 2: cp = "ATAPI"; break;
        case 3: cp = "IEEE 1394 - 1995"; break;
        case 4: cp = "IEEE 1394A"; break;
        case 5: cp = "Fibre channel"; break;
        case 6: cp = "IEEE 1394B"; break;
        case 7: cp = "Serial ATAPI"; break;
        case 8: cp = "USB (both 1 and 2)"; break;
        case 0xffff: cp = "vendor unique"; break;
        default:
            snprintf(buff, sizeof(buff), "[0x%x]", num);
            cp = buff;
            break;
        }
        printf("      Physical interface standard: %s", cp);
        if (len > 8)
            printf(", INQ2=%d, DBE=%d\n", !!(ucp[8] & 2), !!(ucp[8] & 1));
        else
            printf("\n");
        break;
    case 2:     /* Morphing */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 2), !!(ucp[2] & 1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      OCEvent=%d, ASYNC=%d\n", !!(ucp[4] & 2),
            !!(ucp[4] & 1));
        break;
    case 3:     /* Removable medium */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 2), !!(ucp[2] & 1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        num = (ucp[4] >> 5) & 0x7;
        switch (num) {
        case 0: cp = "Caddy/slot type"; break;
        case 1: cp = "Tray type"; break;
        case 2: cp = "Pop-up type"; break;
        case 4: cp = "Embedded changer with individually changeable discs";
            break;
        case 5: cp = "Embedded changer using a magazine"; break;
        default:
            snprintf(buff, sizeof(buff), "[0x%x]", num);
            cp = buff;
            break;
        }
        printf("      Loading mechanism: %s\n", cp);
        printf("      Load=%d, Eject=%d, Prevent jumper=%d, Lock=%d\n",
            !!(ucp[4] & 0x10), !!(ucp[4] & 0x8), !!(ucp[4] & 0x4),
            !!(ucp[4] & 0x1));
        break;
    case 4:     /* Write protect */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      DWP=%d, WDCB=%d, SPWP=%d, SSWPP=%d\n", !!(ucp[4] & 0x8),
            !!(ucp[4] & 0x4), !!(ucp[4] & 0x2), !!(ucp[4] & 0x1));
        break;
    case 0x10:     /* Random readable */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 12) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        num = sg_get_unaligned_be32(ucp + 4);
        printf("      Logical block size=0x%x, blocking=0x%x, PP=%d\n",
            num, sg_get_unaligned_be16(ucp + 8), !!(ucp[10] & 0x1));
        break;
    case 0x1d:     /* Multi-read */
    case 0x22:     /* Sector erasable */
    case 0x26:     /* Restricted overwrite */
    case 0x27:     /* CDRW CAV write */
    case 0x35:     /* Stop long operation */
    case 0x38:     /* BD-R pseudo-overwrite (POW) */
    case 0x42:     /* TSR (timely safe recording) */
    case 0x100:    /* Power management */
    case 0x109:    /* Media serial number */
    case 0x110:    /* VCPS */
    case 0x113:    /* SecurDisc */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        break;
    case 0x1e:     /* CD read */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      DAP=%d, C2 flags=%d, CD-Text=%d\n", !!(ucp[4] & 0x80),
            !!(ucp[4] & 0x2), !!(ucp[4] & 0x1));
        break;
    case 0x1f:     /* DVD read */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len > 7)
            printf("      MULTI110=%d, Dual-RW=%d, Dual-R=%d\n",
                !!(ucp[4] & 0x1), !!(ucp[6] & 0x2), !!(ucp[6] & 0x1));
        break;
    case 0x20:     /* Random writable */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 16) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        num = sg_get_unaligned_be32(ucp + 4);
        n = sg_get_unaligned_be32(ucp + 8);
        printf("      Last lba=0x%x, Logical block size=0x%x, blocking=0x%x,"
            " PP=%d\n", num, n, sg_get_unaligned_be16(ucp + 12),
            !!(ucp[14] & 0x1));
        break;
    case 0x21:     /* Incremental streaming writable */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Data block types supported=0x%x, TRIO=%d, ARSV=%d, "
            "BUF=%d\n", sg_get_unaligned_be16(ucp + 4), !!(ucp[6] & 0x4),
            !!(ucp[6] & 0x2), !!(ucp[6] & 0x1));
        num = ucp[7];
        printf("      Number of link sizes=%d\n", num);
        for (k = 0; k < num; ++k)
            printf("        %d\n", ucp[8 + k]);
        break;
    case 0x23:     /* Formattable */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len > 4)
            printf("      BD-RE: RENoSA=%d, Expand=%d, QCert=%d, Cert=%d, "
                "FRF=%d\n", !!(ucp[4] & 0x8), !!(ucp[4] & 0x4),
                !!(ucp[4] & 0x2), !!(ucp[4] & 0x1), !!(ucp[5] & 0x80));
        if (len > 8)
            printf("      BD-R: RRM=%d\n", !!(ucp[8] & 0x1));
        break;
    case 0x24:     /* Hardware defect management */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len > 4)
            printf("      SSA=%d\n", !!(ucp[4] & 0x80));
        break;
    case 0x25:     /* Write once */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 12) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        num = sg_get_unaligned_be16(ucp + 4);
        printf("      Logical block size=0x%x, blocking=0x%x, PP=%d\n",
            num, sg_get_unaligned_be16(ucp + 8), !!(ucp[10] & 0x1));
        break;
    case 0x28:     /* MRW (Mount Rainier reWriteable) */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len > 4)
            printf("      DVD+Write=%d, DVD+Read=%d, Write=%d\n",
                !!(ucp[4] & 0x4), !!(ucp[4] & 0x2), !!(ucp[4] & 0x1));
        break;
    case 0x29:     /* Enhanced defect reporting */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      DRT-DM=%d, number of DBI cache zones=0x%x, number of "
            "entries=0x%x\n", !!(ucp[4] & 0x1), ucp[5],
            sg_get_unaligned_be16(ucp + 6));
        break;
    case 0x2a:     /* DVD+RW */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Write=%d, Quick start=%d, Close only=%d\n",
            !!(ucp[4] & 0x1), !!(ucp[5] & 0x2), !!(ucp[5] & 0x1));
        break;
    case 0x2b:     /* DVD+R */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Write=%d\n", !!(ucp[4] & 0x1));
        break;
    case 0x2c:     /* Rigid restricted overwrite */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      DSDG=%d, DSDR=%d, Intermediate=%d, Blank=%d\n",
            !!(ucp[4] & 0x8), !!(ucp[4] & 0x4), !!(ucp[4] & 0x2),
            !!(ucp[4] & 0x1));
        break;
    case 0x2d:     /* CD Track at once */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      BUF=%d, R-W raw=%d, R-W pack=%d, Test write=%d\n",
            !!(ucp[4] & 0x40), !!(ucp[4] & 0x10), !!(ucp[4] & 0x8),
            !!(ucp[4] & 0x4));
        printf("      CD-RW=%d, R-W sub-code=%d, Data type supported=%d\n",
            !!(ucp[4] & 0x2), !!(ucp[4] & 0x1),
            sg_get_unaligned_be16(ucp + 6));
        break;
    case 0x2e:     /* CD mastering (session at once) */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      BUF=%d, SAO=%d, Raw MS=%d, Raw=%d\n",
            !!(ucp[4] & 0x40), !!(ucp[4] & 0x20), !!(ucp[4] & 0x10),
            !!(ucp[4] & 0x8));
        printf("      Test write=%d, CD-RW=%d, R-W=%d\n",
            !!(ucp[4] & 0x4), !!(ucp[4] & 0x2), !!(ucp[4] & 0x1));
        printf("      Maximum cue sheet length=0x%x\n",
            sg_get_unaligned_be24(ucp + 5));
        break;
    case 0x2f:     /* DVD-R/-RW write */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      BUF=%d, RDL=%d, Test write=%d, DVD-RW SL=%d\n",
            !!(ucp[4] & 0x40), !!(ucp[4] & 0x8), !!(ucp[4] & 0x4),
            !!(ucp[4] & 0x2));
        break;
    case 0x33:     /* Layer jump recording */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        num = ucp[7];
        printf("      Number of link sizes=%d\n", num);
        for (k = 0; k < num; ++k)
            printf("        %d\n", ucp[8 + k]);
        break;
    case 0x34:     /* Layer jump rigid restricted overwrite */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      CLJB=%d\n", !!(ucp[4] & 0x1));
        printf("      Buffer block size=%d\n", ucp[7]);
        break;
    case 0x37:     /* CD-RW media write support */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      CD-RW media sub-type support (bitmask)=0x%x\n", ucp[5]);
        break;
    case 0x3a:     /* DVD+RW dual layer */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      write=%d, quick_start=%d, close_only=%d\n",
            !!(ucp[4] & 0x1), !!(ucp[5] & 0x2), !!(ucp[5] & 0x1));
        break;
    case 0x3b:     /* DVD+R dual layer */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      write=%d\n", !!(ucp[4] & 0x1));
        break;
    case 0x40:     /* BD Read */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 32) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Bitmaps for BD-RE read support:\n");
        printf("        Class 0=0x%x, Class 1=0x%x, Class 2=0x%x, "
            "Class 3=0x%x\n", sg_get_unaligned_be16(ucp + 8),
            sg_get_unaligned_be16(ucp + 10),
            sg_get_unaligned_be16(ucp + 12),
            sg_get_unaligned_be16(ucp + 14));
        printf("      Bitmaps for BD-R read support:\n");
        printf("        Class 0=0x%x, Class 1=0x%x, Class 2=0x%x, "
            "Class 3=0x%x\n", sg_get_unaligned_be16(ucp + 16),
            sg_get_unaligned_be16(ucp + 18),
            sg_get_unaligned_be16(ucp + 20),
            sg_get_unaligned_be16(ucp + 22));
        printf("      Bitmaps for BD-ROM read support:\n");
        printf("        Class 0=0x%x, Class 1=0x%x, Class 2=0x%x, "
            "Class 3=0x%x\n", sg_get_unaligned_be16(ucp + 24),
            sg_get_unaligned_be16(ucp + 26),
            sg_get_unaligned_be16(ucp + 28),
            sg_get_unaligned_be16(ucp + 30));
        break;
    case 0x41:     /* BD Write */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 32) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      SVNR=%d\n", !!(ucp[4] & 0x1));
        printf("      Bitmaps for BD-RE write support:\n");
        printf("        Class 0=0x%x, Class 1=0x%x, Class 2=0x%x, "
            "Class 3=0x%x\n", sg_get_unaligned_be16(ucp + 8),
            sg_get_unaligned_be16(ucp + 10),
            sg_get_unaligned_be16(ucp + 12),
            sg_get_unaligned_be16(ucp + 14));
        printf("      Bitmaps for BD-R write support:\n");
        printf("        Class 0=0x%x, Class 1=0x%x, Class 2=0x%x, "
            "Class 3=0x%x\n", sg_get_unaligned_be16(ucp + 16),
            sg_get_unaligned_be16(ucp + 18),
            sg_get_unaligned_be16(ucp + 20),
            sg_get_unaligned_be16(ucp + 22));
        printf("      Bitmaps for BD-ROM write support:\n");
        printf("        Class 0=0x%x, Class 1=0x%x, Class 2=0x%x, "
            "Class 3=0x%x\n", sg_get_unaligned_be16(ucp + 24),
            sg_get_unaligned_be16(ucp + 26),
            sg_get_unaligned_be16(ucp + 28),
            sg_get_unaligned_be16(ucp + 30));
        break;
    case 0x50:     /* HD DVD Read */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      HD DVD-R=%d, HD DVD-RAM=%d\n", !!(ucp[4] & 0x1),
            !!(ucp[6] & 0x1));
        break;
    case 0x51:     /* HD DVD Write */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      HD DVD-R=%d, HD DVD-RAM=%d\n", !!(ucp[4] & 0x1),
            !!(ucp[6] & 0x1));
        break;
    case 0x52:     /* HD DVD-RW fragment recording */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      BGP=%d\n", !!(ucp[4] & 0x1));
        break;
    case 0x80:     /* Hybrid disc */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      RI=%d\n", !!(ucp[4] & 0x1));
        break;
    case 0x101:    /* SMART */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      PP=%d\n", !!(ucp[4] & 0x1));
        break;
    case 0x102:    /* Embedded changer */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      SCC=%d, SDP=%d, highest slot number=%d\n",
            !!(ucp[4] & 0x10), !!(ucp[4] & 0x4), (ucp[7] & 0x1f));
        break;
    case 0x103:    /* CD audio external play (obsolete) */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Scan=%d, SCM=%d, SV=%d, number of volume levels=%d\n",
            !!(ucp[4] & 0x4), !!(ucp[4] & 0x2), !!(ucp[4] & 0x1),
            sg_get_unaligned_be16(ucp + 6));
        break;
    case 0x104:    /* Firmware upgrade */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 4) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        if (len > 4)
            printf("      M5=%d\n", !!(ucp[4] & 0x1));
        break;
    case 0x105:    /* Timeout */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len > 7) {
            printf("      Group 3=%d, unit length=%d\n",
                !!(ucp[4] & 0x1), sg_get_unaligned_be16(ucp + 6));
        }
        break;
    case 0x106:    /* DVD CSS */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      CSS version=%d\n", ucp[7]);
        break;
    case 0x107:    /* Real time streaming */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      RBCB=%d, SCS=%d, MP2A=%d, WSPD=%d, SW=%d\n",
            !!(ucp[4] & 0x10), !!(ucp[4] & 0x8), !!(ucp[4] & 0x4),
            !!(ucp[4] & 0x2), !!(ucp[4] & 0x1));
        break;
    case 0x108:    /* Drive serial number */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        num = len - 4;
        n = sizeof(buff) - 1;
        n = ((num < n) ? num : n);
        strncpy(buff, (const char *)(ucp + 4), n);
        buff[n] = '\0';
        printf("      Drive serial number: %s\n", buff);
        break;
    case 0x10a:    /* Disc control blocks */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        printf("      Disc control blocks:\n");
        for (k = 4; k < len; k += 4) {
            printf("        0x%x\n", sg_get_unaligned_be32(ucp + k));
        }
        break;
    case 0x10b:    /* DVD CPRM */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      CPRM version=%d\n", ucp[7]);
        break;
    case 0x10c:    /* Firmware information */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 20) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      %.2s%.2s/%.2s/%.2s %.2s:%.2s:%.2s\n", ucp + 4,
            ucp + 6, ucp + 8, ucp + 10, ucp + 12, ucp + 14, ucp + 16);
        break;
    case 0x10d:    /* AACS */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      BNG=%d, Block count for binding nonce=%d\n",
            !!(ucp[4] & 0x1), ucp[5]);
        printf("      Number of AGIDs=%d, AACS version=%d\n",
            (ucp[6] & 0xf), ucp[7]);
        break;
    case 0x10e:    /* DVD CSS managed recording */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Maximum number of scrambled extent information "
            "entries=%d\n", ucp[4]);
        break;
    case 0x120:    /* BD CPS */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      BD CPS major:minor version number=%d:%d, max open "
            "SACs=%d\n", ((ucp[5] >> 4) & 0xf), (ucp[5] & 0xf),
            ucp[6] & 0x3);
        break;
    case 0x142:    /* OSSC (Optical Security Subsystem Class) */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("    PSAU=%d, LOSPB=%d, ME=%d\n", !!(ucp[4] & 0x80),
            !!(ucp[4] & 0x40), !!(ucp[4] & 0x1));
        num = ucp[5];
        printf("      Profile numbers:\n");
        for (k = 6; (num > 0) && (k < len); --num, k += 2) {
            printf("        %u\n", sg_get_unaligned_be16(ucp + k));
        }
        break;
    case 0xff10:     /* PSX CD */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Write=%d\n", !!(ucp[4] & 0x1));
        break;
    case 0xff20:     /* PS2 CD */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Write=%d\n", !!(ucp[4] & 0x1));
        break;
    case 0xff21:     /* PS2 DVD */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Write=%d\n", !!(ucp[4] & 0x1));
        break;
    case 0xff30:     /* PS3 DVD */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Write=%d\n", !!(ucp[4] & 0x1));
        break;
    case 0xff31:     /* PS3 BD */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Write=%d\n", !!(ucp[4] & 0x1));
        break;
    case 0xff40:     /* SACD feature 1 */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      unkn1=%d, unkn2=%d\n",
            !!(ucp[4] & 0x1), ucp[5]);
        printf("      unkn3=%d, SACD version=%d\n",
            (ucp[6] & 0xf), ucp[7]);
        break;
    case 0xff41:     /* SACD feature 2 */
        printf("    version=%d, persist=%d, current=%d [0x%x]\n",
            ((ucp[2] >> 2) & 0xf), !!(ucp[2] & 0x2), !!(ucp[2] & 0x1),
            feature);
        if (len < 8) {
            printf("      additional length [%d] too short\n", len - 4);
            break;
        }
        printf("      Write=%d\n", !!(ucp[4] & 0x1));
        break;
    default:
        pr2ws("    Unknown feature [0x%x], version=%d persist=%d, "
            "current=%d\n", feature, ((ucp[2] >> 2) & 0xf),
            !!(ucp[2] & 0x2), !!(ucp[2] & 0x1));
        dStrHexErr((const char *)ucp, len, 1);
        break;
    }
}

/**
 * @brief Decode and print complete GET CONFIGURATION response
 *
 * Parses the full response from a GET CONFIGURATION command. The response
 * format is:
 *   [0-3]  Data Length (excluding these 4 bytes)
 *   [4-5]  Reserved
 *   [6-7]  Current Profile
 *   [8+]   Feature Descriptors
 *
 * Each feature descriptor has:
 *   [0-1]  Feature Code
 *   [2]    Version/Persistent/Current flags
 *   [3]    Additional Length
 *   [4+]   Feature-specific data
 *
 * @param resp          Pointer to response buffer
 * @param max_resp_len  Size of response buffer
 * @param len           Actual response length (from Data Length field + 4)
 * @param brief         If non-zero, print only feature names
 * @param inner_hex     If non-zero, hex dump each feature
 */
void
sg_decode_config(unsigned char *resp, int max_resp_len, int len,
                 int brief, int inner_hex)
{
    int k, curr_profile, extra_len, feature;
    unsigned char *ucp;
    char buff[128];

    if (max_resp_len < len) {
        pr2ws("<<<warning: response too long for buffer, resp_len=%d>>>\n",
            len);
        len = max_resp_len;
    }
    if (len < 8) {
        pr2ws("response length too short: %d\n", len);
        return;
    }
    curr_profile = sg_get_unaligned_be16(resp + 6);
    if (0 == curr_profile)
        pr2ws("No current profile\n");
    else
        printf("Current profile: %s\n", sg_get_profile_str(curr_profile, buff));
    printf("Features%s:\n", (brief ? " (in brief)" : ""));
    ucp = resp + 8;
    len -= 8;
    for (k = 0; k < len; k += extra_len, ucp += extra_len) {
        extra_len = 4 + ucp[3];
        feature = sg_get_unaligned_be16(ucp + 0);
        printf("  %s feature\n", sg_get_feature_str(feature, buff));
        if (brief)
            continue;
        if (inner_hex) {
            dStrHex((const char *)ucp, extra_len, 1);
            continue;
        }
        if (0 != (extra_len % 4))
            printf("    additional length [%d] not a multiple of 4, ignore\n",
                extra_len - 4);
        else
            sg_decode_feature(feature, ucp, extra_len);
    }
}

/**
 * @brief Extract SACD-related feature flags from GET CONFIGURATION response
 *
 * Scans the response for PS3-specific features that indicate SACD capability.
 * This function only processes responses with DVD profile (0x10), as SACD
 * hybrid discs appear as DVD to the drive.
 *
 * Checks for:
 *   - Feature 0x80 (Hybrid disc): Indicates dual-layer DVD/SACD
 *   - Feature 0xFF40 (SACD feature 1): Primary SACD indicator
 *   - Feature 0xFF41 (SACD feature 2): Secondary SACD indicator
 *
 * @param resp          Pointer to GET CONFIGURATION response
 * @param max_resp_len  Size of response buffer
 * @param len           Actual response length
 * @return Bitmask of FEATURE_HYBRID_DISC, FEATURE_SACD_1, FEATURE_SACD_2
 */
int
sg_decode_config_set(unsigned char *resp, int max_resp_len, int len)
{
    int k, curr_profile, extra_len, feature;
    unsigned char *ucp;
    int ret = 0;

    if (max_resp_len < len) {
        pr2ws("<<<warning: response too long for buffer, resp_len=%d>>>\n",
            len);
        len = max_resp_len;
    }
    if (len < 8) {
        pr2ws("response length too short: %d\n", len);
        return 0;
    }
    curr_profile = sg_get_unaligned_be16(resp + 6);
    if (curr_profile == 0x10) { /* DVD profile */
        ucp = resp + 8;
        len -= 8;
        for (k = 0; k < len; k += extra_len, ucp += extra_len) {
            extra_len = 4 + ucp[3];
            feature = sg_get_unaligned_be16(ucp + 0);
            switch (feature)
            {
            case 0x80:     /* Hybrid disc */
                if ((ucp[2] & 0x1))
                    ret |= FEATURE_HYBRID_DISC;
                break;
            case 0xff40:     /* SACD feature 1 */
                if ((ucp[2] & 0x1))
                    ret |= FEATURE_SACD_1;
                break;
            case 0xff41:     /* SACD feature 2 */
                if ((ucp[2] & 0x1))
                    ret |= FEATURE_SACD_2;
                break;
            }
        }
    }
    return ret;
}
