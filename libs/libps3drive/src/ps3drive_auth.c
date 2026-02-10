/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief BD drive authentication implementation.
 * This file implements the BD authentication protocol that establishes
 * a session with the PS3 BluRay drive before SAC key exchange can occur.
 * Protocol overview:
 *   1. TEST UNIT READY
 *   2. SEND KEY (security check)
 *   3. Generate host random, encrypt with key1, send
 *   4. REPORT KEY (receive encrypted host+drive randoms)
 *   5. Decrypt, verify host random, extract drive random
 *   6. Encrypt drive random with key1, send
 *   7. Derive session keys (key7, key8) from randoms
 *   8. Send E1 command with encrypted data
 *   9. Re-establish session with key5/key6
 *   10. Derive final session keys
 *   11. Send E0 command and receive response
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

#include <string.h>
#include <stdio.h>

#include <sg_lib.h>
#include <sg_cmds_basic.h>
#include <sg_cmds_ps3.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * @brief Fill buffer with pseudo-random data.
 *
 * Uses the cryptographic RNG if available, falls back to less secure method.
 */
static void fill_random(uint8_t *buf, size_t len)
{
    if (ps3drive_random_bytes(buf, len) != 0) {
        /* Fallback to less secure method if crypto RNG fails */
        for (size_t i = 0; i < len; i++) {
            buf[i] = (uint8_t)(i * 17 + 0x42);  /* Deterministic fallback */
        }
    }
}

/* ============================================================================
 * BD Authentication Protocol
 * ============================================================================ */

ps3drive_error_t ps3drive_auth_bd_internal(ps3drive_t *handle,
                                            const uint8_t *key1,
                                            const uint8_t *key2)
{
    uint8_t cdb[256];
    uint8_t buf[256];
    uint8_t rnd1[16];  /* Host random */
    uint8_t rnd2[16];  /* Drive random */
    uint8_t key7[16];  /* Derived session key 7 */
    uint8_t key8[16];  /* Derived session key 8 */
    unsigned int req_sense = 0;
    int ret;

    if (handle == NULL || key1 == NULL || key2 == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    /* ========================================================================
     * Step 1: TEST UNIT READY
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== TEST UNIT READY (0x00) ===\n");

    ret = sg_ll_ps3_test_unit_ready(handle->sg_fd, &req_sense,
                                     handle->noisy, handle->verbose);
    if (ret != 0 && req_sense != 0x23a00) {
        ps3drive_debug(handle, 1, "TEST UNIT READY: req_sense=0x%x\n", req_sense);
        /* Continue anyway - some drives return errors here */
    }

    /* ========================================================================
     * Step 2: Security Check (SEND KEY)
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== SEND KEY - Security Check ===\n");

    memset(buf, 0, 0x14);

    ret = sg_ll_ps3_send_key(handle->sg_fd, buf, 0x14,
                              0,    /* vcps_fun */
                              0xe0, /* key_class */
                              0,    /* agid */
                              0,    /* key_fmt */
                              0,    /* ctrl */
                              handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "SEND KEY (security check) failed: %d", ret);
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    /* ========================================================================
     * Step 3: Establish Session - Send Encrypted Host Random
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== SEND KEY - Host Random ===\n");

    memset(buf, 0, 0x14);
    buf[0] = 0x00;  /* Length MSB */
    buf[1] = 0x10;  /* Length LSB (16 bytes) */

    fill_random(rnd1, 16);
    memcpy(buf + 4, rnd1, 16);

    /* Encrypt host random with key1 using IV1 */
    ret = ps3drive_aes128_cbc_encrypt(key1, PS3DRIVE_AUTH_IV1,
                                       buf + 4, buf + 4, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "AES encryption failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    ret = sg_ll_ps3_send_key(handle->sg_fd, buf, 0x14,
                              0, 0xe0, 0, 0, 0,
                              handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "SEND KEY (host random) failed: %d", ret);
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    /* ========================================================================
     * Step 4: Receive Encrypted Host and Drive Randoms
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== REPORT KEY - Get Randoms ===\n");

    memset(buf, 0, 0x24);  /* Clear buffer before receiving data */
    ret = sg_ll_ps3_report_key(handle->sg_fd, 0, 0, buf, 0x24,
                                0xe0, 0, 0, 0,
                                handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "REPORT KEY (randoms) failed: %d", ret);
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    /* Decrypt received host and drive randoms separately */
    ret = ps3drive_aes128_cbc_decrypt(key2, PS3DRIVE_AUTH_IV1,
                                       buf + 4, buf + 4, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "AES decryption failed (host random)");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    ret = ps3drive_aes128_cbc_decrypt(key2, PS3DRIVE_AUTH_IV1,
                                       buf + 0x14, buf + 0x14, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "AES decryption failed (drive random)");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    /* Verify received host random matches what we sent */
    if (memcmp(rnd1, buf + 4, 16) != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "Host random mismatch");
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    /* Extract drive random */
    memcpy(rnd2, buf + 0x14, 16);

    ps3drive_debug_hex(handle, "Drive random", rnd2, 16);

    /* ========================================================================
     * Step 5: Send Encrypted Drive Random
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== SEND KEY - Drive Random ===\n");

    memset(buf, 0, 0x14);
    buf[0] = 0x00;
    buf[1] = 0x10;
    memcpy(buf + 4, rnd2, 16);

    ret = ps3drive_aes128_cbc_encrypt(key1, PS3DRIVE_AUTH_IV1,
                                       buf + 4, buf + 4, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "AES encryption failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    ret = sg_ll_ps3_send_key(handle->sg_fd, buf, 0x14,
                              0, 0xe0, 0, 0x2, 0,
                              handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "SEND KEY (drive random) failed: %d", ret);
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    /* ========================================================================
     * Step 6: Derive Session Keys from Randoms
     * ======================================================================== */

    /* key7 = AES-CBC-encrypt(key3, iv1, rnd1[0:8] || rnd2[8:16]) */
    memcpy(key7, rnd1, 8);
    memcpy(key7 + 8, rnd2 + 8, 8);
    ret = ps3drive_aes128_cbc_encrypt(PS3DRIVE_AUTH_KEY3, PS3DRIVE_AUTH_IV1,
                                       key7, key7, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "Session key derivation failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    /* key8 = AES-CBC-encrypt(key4, iv1, rnd1[8:16] || rnd2[0:8]) */
    memcpy(key8, rnd1 + 8, 8);
    memcpy(key8 + 8, rnd2, 8);
    ret = ps3drive_aes128_cbc_encrypt(PS3DRIVE_AUTH_KEY4, PS3DRIVE_AUTH_IV1,
                                       key8, key8, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "Session key derivation failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    ps3drive_debug_hex(handle, "Session key7", key7, 16);
    ps3drive_debug_hex(handle, "Session key8", key8, 16);

    /* ========================================================================
     * Step 7: Send E1 Command
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== E1 Command ===\n");

    /* Build CDB for E1 command */
    memset(cdb, 0, 8);
    cdb[6] = 0xe6;  /* Random byte */
    cdb[7] = ps3drive_checksum(cdb, 7);

    /* 3DES encrypt CDB */
    {
        uint8_t des_key[24];
        memcpy(des_key, key7, 16);
        memcpy(des_key + 16, key7, 8);  /* Extend to 24 bytes (K1||K2||K1) */

        ret = ps3drive_3des_cbc_encrypt(des_key, PS3DRIVE_AUTH_IV2,
                                         cdb, cdb, 8);
        ps3drive_secure_zero(des_key, sizeof(des_key));
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                               "3DES encryption failed");
            return PS3DRIVE_ERR_CRYPTO_FAILED;
        }
    }

    /* Build data buffer for E1 */
    memset(buf, 0, 0x54);
    buf[0] = 0x00;
    buf[1] = 0x50;  /* Length (80 bytes) */
    buf[5] = 0xee;  /* Random byte */
    memcpy(buf + 8, PS3DRIVE_CMD_4_14, sizeof(PS3DRIVE_CMD_4_14));
    buf[4] = ps3drive_checksum(buf + 5, 0x4f);

    /* AES encrypt buffer */
    ret = ps3drive_aes128_cbc_encrypt(key7, PS3DRIVE_AUTH_IV3,
                                       buf + 4, buf + 4, 0x50);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "AES encryption failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    ret = sg_ll_ps3_e1_send_key(handle->sg_fd, buf, 0x54, cdb,
                                 handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "E1 command failed: %d", ret);
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    /* ========================================================================
     * Step 8: Re-establish Session with key5/key6
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== Re-establish Session ===\n");

    memset(buf, 0, 0x14);
    buf[0] = 0x00;
    buf[1] = 0x10;
    memcpy(buf + 4, rnd1, 16);

    ret = ps3drive_aes128_cbc_encrypt(PS3DRIVE_AUTH_KEY5, PS3DRIVE_AUTH_IV1,
                                       buf + 4, buf + 4, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "AES encryption failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    ret = sg_ll_ps3_send_key(handle->sg_fd, buf, 0x14,
                              0, 0xe0, 0, 1, 0,
                              handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "SEND KEY (re-establish) failed: %d", ret);
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    /* Receive randoms again */
    memset(buf, 0, 0x24);  /* Clear buffer before receiving data */
    ret = sg_ll_ps3_report_key(handle->sg_fd, 0, 0, buf, 0x24,
                                0xe0, 0, 1, 0,
                                handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "REPORT KEY (re-establish) failed: %d", ret);
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    /* Decrypt with key6 */
    ret = ps3drive_aes128_cbc_decrypt(PS3DRIVE_AUTH_KEY6, PS3DRIVE_AUTH_IV1,
                                       buf + 4, buf + 4, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "AES decryption failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    ret = ps3drive_aes128_cbc_decrypt(PS3DRIVE_AUTH_KEY6, PS3DRIVE_AUTH_IV1,
                                       buf + 0x14, buf + 0x14, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "AES decryption failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    /* Verify host random */
    if (memcmp(rnd1, buf + 4, 16) != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "Host random mismatch (re-establish)");
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    memcpy(rnd2, buf + 0x14, 16);

    /* Send encrypted drive random */
    memset(buf, 0, 0x14);
    buf[0] = 0x00;
    buf[1] = 0x10;
    memcpy(buf + 4, rnd2, 16);

    ret = ps3drive_aes128_cbc_encrypt(PS3DRIVE_AUTH_KEY5, PS3DRIVE_AUTH_IV1,
                                       buf + 4, buf + 4, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "AES encryption failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    ret = sg_ll_ps3_send_key(handle->sg_fd, buf, 0x14,
                              0, 0xe0, 0, 0x3, 0,
                              handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "SEND KEY (drive random 2) failed: %d", ret);
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    /* ========================================================================
     * Step 9: Derive Final Session Keys
     * ======================================================================== */

    memcpy(key7, rnd1, 8);
    memcpy(key7 + 8, rnd2 + 8, 8);
    ret = ps3drive_aes128_cbc_encrypt(PS3DRIVE_AUTH_KEY3, PS3DRIVE_AUTH_IV1,
                                       key7, key7, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "Final key derivation failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    memcpy(key8, rnd1 + 8, 8);
    memcpy(key8 + 8, rnd2, 8);
    ret = ps3drive_aes128_cbc_encrypt(PS3DRIVE_AUTH_KEY4, PS3DRIVE_AUTH_IV1,
                                       key8, key8, 16);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "Final key derivation failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    /* ========================================================================
     * Step 10: E0 Command (Retrieve Response)
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== E0 Command ===\n");

    memset(cdb, 0, 8);
    cdb[0] = 0x04;  /* Random byte */
    cdb[6] = 0xe7;  /* Random byte */
    cdb[7] = ps3drive_checksum(cdb, 7);

    /* 3DES encrypt CDB */
    {
        uint8_t des_key[24];
        memcpy(des_key, key7, 16);
        memcpy(des_key + 16, key7, 8);

        ret = ps3drive_3des_cbc_encrypt(des_key, PS3DRIVE_AUTH_IV2,
                                         cdb, cdb, 8);
        ps3drive_secure_zero(des_key, sizeof(des_key));
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                               "3DES encryption failed");
            return PS3DRIVE_ERR_CRYPTO_FAILED;
        }
    }

    ret = sg_ll_ps3_e0_report_key(handle->sg_fd, buf, 0x54, cdb,
                                   handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "E0 command failed: %d", ret);
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    /* Decrypt response */
    ret = ps3drive_aes128_cbc_decrypt(key7, PS3DRIVE_AUTH_IV3,
                                       buf + 4, buf + 4, 0x50);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "AES decryption failed");
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    /* Verify checksum */
    if (buf[4] != ps3drive_checksum(buf + 5, 0x4f)) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_AUTH_FAILED,
                           "Response checksum mismatch");
        return PS3DRIVE_ERR_AUTH_FAILED;
    }

    ps3drive_debug_hex(handle, "Version info", buf + 6, 8);
    ps3drive_debug(handle, 1, "BD authentication completed successfully\n");

    /* Securely zero sensitive data */
    ps3drive_secure_zero(rnd1, sizeof(rnd1));
    ps3drive_secure_zero(rnd2, sizeof(rnd2));
    ps3drive_secure_zero(key7, sizeof(key7));
    ps3drive_secure_zero(key8, sizeof(key8));

    return PS3DRIVE_OK;
}
