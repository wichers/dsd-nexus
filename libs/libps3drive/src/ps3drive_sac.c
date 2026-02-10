/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD Authentication Channel (SAC) key exchange implementation.
 * This file implements the 6-command SAC key exchange protocol that
 * derives the AES key and IV used for SACD sector decryption.
 * Protocol flow verified against SacModule.spu.self emulator:
 *   CMD 0: Get key format from drive
 *   CMD 2: Generate Key 1 - Send host_random + RSA-175 public key blob
 *   CMD 3: Validate Key 1 - Receive and verify drive certificate, extract drive pubkey
 *   CMD 4: Generate Key 2 - RSA-1024 sign (drive_response + host_session_random)
 *   CMD 5: Validate Key 2 - Nested RSA: outer=drive_pubkey, inner=host_privkey
 *   CMD 6: Derive final disc key using session_key = SHA1(host_session_random || drive_session_random)[:16]
 * Key insight from emulator analysis:
 *   - Session key = SHA1(host_session_random || drive_session_random)[:16]
 *   - Session IV is STATIC: 0x00000010 0x00000000 0x00000000 0x00000000
 *   - Disc IV is STATIC: decrypted during CMD0 from internal keys
 *   - Final disc key = AES_CBC_decrypt(drive_response, session_key, PS3DRIVE_SESSION_IV)[0x20:0x30]
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

#include <libsautil/macros.h>

#include <string.h>
#include <stdio.h>

#include <sg_lib.h>
#include <sg_unaligned.h>
#include <sg_cmds_ps3.h>
#include <mbedtls/sha1.h>

/* ============================================================================
 * Comprehensive Logging (for comparison with emulator)
 * ============================================================================ */
#define SAC_VERBOSE_LOGGING 0

#if SAC_VERBOSE_LOGGING
static void sac_hexdump(const char *label, const uint8_t *data, size_t len)
{
    size_t i;
    fprintf(stdout, "[SAC] %s (%zu bytes):\n", label, len);
    for (i = 0; i < len; i++) {
        if (i % 16 == 0) fprintf(stdout, "  %04zx: ", i);
        fprintf(stdout, "%02x ", data[i]);
        if ((i + 1) % 16 == 0) fprintf(stdout, "\n");
    }
    if (len % 16 != 0) fprintf(stdout, "\n");
}

static void sac_hexline(const char *label, const uint8_t *data, size_t len)
{
    size_t i;
    fprintf(stdout, "[SAC] %s: ", label);
    for (i = 0; i < len; i++) {
        fprintf(stdout, "%02x", data[i]);
    }
    fprintf(stdout, "\n");
}
#else
#define sac_hexdump(label, data, len) ((void)0)
#define sac_hexline(label, data, len) ((void)0)
#endif

/*
 * Calculate SCSI buffer send size with 4-byte alignment.
 * The drive expects: length_field(4) + payload + padding_to_4byte_boundary
 *
 * Formula from working libsacd implementation:
 *   send_size = payload + 4 + ((payload & 3) ? (~(payload & 3) + 5) & 3 : 0)
 */
#define SAC_SEND_SIZE(payload) \
    ((payload) + 4 + (((payload) & 3) != 0 ? (~((payload) & 3) + 4 + 1) & 3 : 0))

/* ============================================================================
 * Work Buffer Layout (mirrors SPU's 0xD290)
 * ============================================================================ */
typedef struct {
    uint8_t flags[8];              /* 0x00: flags */
    uint8_t cert_id_2[8];          /* 0x08: drive certificate ID */
    uint8_t host_random[16];       /* 0x10: host random (from CMD2) */
    uint8_t drive_response[16];    /* 0x20: drive challenge response */
    uint8_t host_session_random[16]; /* 0x30: host session random (CMD4) */
    uint8_t drive_session_random[16]; /* 0x40: drive session random (CMD5) */
    uint8_t drive_pubkey[128];     /* 0x50: drive public key (derived from cert) */
} sac_work_buffer_t;

/* ============================================================================
 * SAC Key Exchange Protocol
 * ============================================================================ */

ps3drive_error_t ps3drive_sac_exchange_internal(ps3drive_t *handle,
                                                 uint8_t *aes_key,
                                                 uint8_t *aes_iv)
{
    int ret;
    uint8_t key_fmt;
    uint8_t ioctl_buffer[256];
    uint32_t ioctl_buffer_size;

    /* Session state using proper work buffer structure */
    sac_work_buffer_t work_buf;
    uint8_t session_key[16];

    if (handle == NULL || aes_key == NULL || aes_iv == NULL) {
        return PS3DRIVE_ERR_NULL_PTR;
    }

    memset(&work_buf, 0, sizeof(work_buf));
    memset(ioctl_buffer, 0, sizeof(ioctl_buffer));

    /* Initialize crypto RNG */
    ret = ps3drive_crypto_init();
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                           "Failed to initialize RNG: %d", ret);
        return PS3DRIVE_ERR_CRYPTO_FAILED;
    }

    /* ========================================================================
     * CMD 0: Get key format from drive
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== SAC CMD 0: Get Key Format ===\n");

    ret = sg_ll_ps3_sac_report_key(handle->sg_fd, 0, ioctl_buffer, 8,
                                    16, 0, 0, 0,
                                    handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                           "SAC CMD 0 failed: %d", ret);
        goto stop;
    }

    key_fmt = ioctl_buffer[7];
    ps3drive_debug(handle, 2, "Key format: 0x%02x\n", key_fmt);

    /* ========================================================================
     * CMD 2: Generate Key 1
     *
     * Packet structure (208 bytes total, verified from SPU emulator):
     *   [0-3]:    Length = 0xC9 (201)
     *   [4-19]:   Host random (16 bytes)
     *   [20-23]:  Reserved zeros (4 bytes)
     *   [24-27]:  Host cert ID = 0x00000001 (4 bytes)
     *   [28-29]:  Marker = 0x0099 (2 bytes)
     *   [30-204]: RSA-175 public key blob (175 bytes)
     *
     * NOTE: This is NOT encrypted - we send the raw RSA-175 public key blob.
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== SAC CMD 2: Generate Key 1 ===\n");
    {
        uint8_t challenge[220];
        uint32_t payload_size;
        uint32_t send_size;

        memset(challenge, 0, sizeof(challenge));

        /* Generate 16-byte host random */
        ps3drive_random_bytes(work_buf.host_random, 16);
        sac_hexline("host_random", work_buf.host_random, 16);

        /* Build packet - payload size is 201 (0xC9) bytes */
        payload_size = 201;  /* 0xC9 = 16 + 4 + 4 + 2 + 175 */
        sg_put_unaligned_be32(payload_size, challenge);   /* [0-3]: Length = 201 */
        memcpy(challenge + 4, work_buf.host_random, 16);  /* [4-19]: Host random */
        /* [20-23]: Zeros (already set) */
        sg_put_unaligned_be32(0x00000001, challenge + 24); /* [24-27]: Host cert ID */
        challenge[28] = 0x00;                              /* [28-29]: Marker 0x0099 */
        challenge[29] = 0x99;
        memcpy(challenge + 30, PS3DRIVE_RSA175_BLOB, 175); /* [30-204]: RSA-175 blob */

        ps3drive_debug_hex(handle, "Host random", work_buf.host_random, 16);

        /* Calculate send size with 4-byte alignment padding */
        send_size = SAC_SEND_SIZE(payload_size);
        /* For payload_size=201: 201+4=205, +3 padding = 208 */

        sac_hexdump("CMD 2 packet (to drive)", challenge, send_size);

        ret = sg_ll_ps3_sac_send_key(handle->sg_fd, challenge, send_size,
                                      2, 16, 0, key_fmt, 0,
                                      handle->noisy, handle->verbose);
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                               "SAC CMD 2 failed: %d", ret);
            goto stop;
        }
    }

    /* ========================================================================
     * CMD 3: Validate Key 1 (receive drive certificate)
     *
     * Input structure (197 bytes = 0xC5):
     *   [0-15]:   Challenge response (16 bytes)
     *   [16-23]:  Drive certificate ID (8 bytes)
     *   [24]:     Reserved
     *   [25]:     Certificate type marker (must be 0x95)
     *   [26-196]: Certificate body (171 bytes):
     *             - [26-153]: RSA modulus (128 bytes)
     *             - [154-192]: Padding (39 bytes)
     *             - [193-196]: RSA exponent (4 bytes, typically 65537)
     *
     * We store the certificate data in work_buffer for CMD5.
     * NOTE: drive_pubkey is derived from certificate chain verification,
     * not directly copied. For simplicity, we use the cert RSA modulus.
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== SAC CMD 3: Validate Key 1 ===\n");

    ret = sg_ll_ps3_sac_report_key(handle->sg_fd, 2, ioctl_buffer, 208,
                                    16, 0, key_fmt, 0,
                                    handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                           "SAC CMD 3 failed: %d", ret);
        goto stop;
    }

    ioctl_buffer_size = sg_get_unaligned_be32(ioctl_buffer);
    sac_hexdump("CMD 3 raw response", ioctl_buffer, SAMIN(ioctl_buffer_size + 4, 208));

    {
        uint8_t *response = ioctl_buffer + 4;

        /* Verify certificate type marker */
        if (response[25] != 0x95) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                               "Invalid certificate type: 0x%02x", response[25]);
            goto stop;
        }

        /* Store challenge response */
        memcpy(work_buf.drive_response, response, 16);
        sac_hexline("drive_response", work_buf.drive_response, 16);

        /* Store drive certificate ID */
        memcpy(work_buf.cert_id_2, response + 16, 8);
        sac_hexline("cert_id_2", work_buf.cert_id_2, 8);

        /* ================================================================
         * Derive drive_pubkey from certificate using CA root key
         *
         * The certificate modulus is RSA-signed by Sony's CA root key.
         * We perform the RSA public operation (decrypt/verify) to extract
         * the actual drive public key.
         *
         * Formula:
         *   decrypted = cert_modulus^65537 mod CA_ROOT_N
         *   drive_pubkey[0:89]   = decrypted[18:107]
         *   drive_pubkey[89:128] = cert_body[128:167] (padding after modulus)
         * ================================================================ */
        {
            uint8_t rsa_decrypted[128];
            const uint8_t *cert_modulus = response + 26;      /* 128 bytes */
            const uint8_t *cert_padding = response + 26 + 128; /* 39 bytes */

            sac_hexdump("cert_modulus (input)", cert_modulus, 128);

            /* RSA decrypt: decrypted = cert_modulus^65537 mod CA_ROOT_N */
            ret = ps3drive_rsa1024_public_op(PS3DRIVE_CA_ROOT_N, cert_modulus, rsa_decrypted);
            if (ret != 0) {
                ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                                   "Certificate RSA verification failed: %d", ret);
                goto stop;
            }

            sac_hexdump("RSA decrypted result", rsa_decrypted, 128);

            /* Verify ISO 9796-2 signature format marker */
            if (rsa_decrypted[0] != 0x6a) {
                printf("[SAC] WARNING: Unexpected signature format: 0x%02x (expected 0x6a)\n", rsa_decrypted[0]);
                goto stop;
            }

            /* Extract drive_pubkey using verified formula:
             *   drive_pubkey[0:89]   = decrypted[18:107] (89 bytes from RSA result)
             *   drive_pubkey[89:128] = cert_padding[0:39] (39 bytes from certificate)
             */
            memcpy(work_buf.drive_pubkey, rsa_decrypted + 18, 89);
            memcpy(work_buf.drive_pubkey + 89, cert_padding, 39);

            sac_hexdump("Derived drive_pubkey", work_buf.drive_pubkey, 128);
        }

        /* Set flags */
        work_buf.flags[7] = 0x01;

        ps3drive_debug_hex(handle, "Drive cert ID", work_buf.cert_id_2, 8);
        ps3drive_debug_hex(handle, "Drive response", work_buf.drive_response, 16);
    }

    /* ========================================================================
     * CMD 4: Generate Key 2 (RSA-1024 session message)
     *
     * Protocol (reverse engineered from SacModule.spu.self trace):
     *   1. Generate host_session_random (16 bytes)
     *   2. RSA encrypt session_random with drive's public key (e=65537)
     *      -> produces 128-byte encrypted_session
     *   3. Build message to sign:
     *      - drive_response (16 bytes)
     *      - cert_id_2 (8 bytes, from drive certificate)
     *      - encrypted_session[0:82] (first 82 bytes of RSA result)
     *      Total: 106 bytes, fits in PKCS#1 v1.5 padding (128 - 11 = 117 max)
     *   4. PKCS#1 v1.5 sign message with host's private key -> 128-byte signature
     *   5. Build output (174 bytes total):
     *      - signature (128 bytes)
     *      - encrypted_session[82:128] (46 bytes)
     *
     * Output structure (180 bytes with length prefix):
     *   [0-3]:    Length = 0xAE (174)
     *   [4-131]:  RSA signature (128 bytes)
     *   [132-177]: Tail of encrypted session (46 bytes)
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== SAC CMD 4: Generate Key 2 ===\n");

    {
        uint8_t session_msg[180];
        uint8_t encrypted_session[128];  /* session_random encrypted with drive pubkey */
        uint8_t sign_input[128];         /* Message to be signed (with PKCS#1 padding) */
        uint8_t rsa_signature[128];      /* Final signature */
        uint32_t payload_size;
        uint32_t send_size;

        memset(session_msg, 0, sizeof(session_msg));
        memset(encrypted_session, 0, sizeof(encrypted_session));
        memset(sign_input, 0, sizeof(sign_input));

        /* Step 1: Generate host session random */
        ps3drive_random_bytes(work_buf.host_session_random, 16);
        sac_hexline("host_session_random", work_buf.host_session_random, 16);
        ps3drive_debug_hex(handle, "Host session random", work_buf.host_session_random, 16);

        /* Step 2: RSA encrypt (host_cert_id + session_random) with drive's public key
         *
         * From SPU emulator trace analysis:
         * - Data to encrypt: host_cert_id(8) + session_random(16) = 24 bytes
         * - Uses PKCS#1 v1.5 type 2 padding
         * - Format: 00 02 [random padding] 00 [host_cert_id(8)] [session_random(16)]
         * - Padding length = 128 - 3 - 24 = 101 bytes
         * - SPU padding allows zeros (uses SHA1-based MGF)
         *
         * Note: host_cert_id is always 0x00000001 for the host (8-byte big-endian)
         */
        {
            uint8_t padded_session[128];
            uint8_t *p = padded_session;
            /* Data is 24 bytes: 8 bytes host_cert_id + 16 bytes session_random */
            int data_len = 8 + 16;  /* 24 bytes */
            int pad_len = 128 - 3 - data_len;  /* 101 bytes of random padding */
            int i;
            /* Host certificate ID (0x00000001 in big-endian, padded to 8 bytes) */
            static const uint8_t host_cert_id[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };

            *p++ = 0x00;
            *p++ = 0x02;

            /* Generate random non-zero padding bytes */
            ps3drive_random_bytes(p, pad_len);
            for (i = 0; i < pad_len; i++) {
                /* Ensure no zero bytes in padding (PKCS#1 requirement) */
                while (p[i] == 0) {
                    ps3drive_random_bytes(&p[i], 1);
                }
            }
            p += pad_len;

            *p++ = 0x00;
            /* Copy host_cert_id (8 bytes) then session_random (16 bytes) */
            memcpy(p, host_cert_id, 8);
            p += 8;
            memcpy(p, work_buf.host_session_random, 16);

            sac_hexdump("PKCS#1 type 2 padded input", padded_session, 128);
            ps3drive_debug_hex(handle, "PKCS#1 type 2 padded input", padded_session, 128);

            ret = ps3drive_rsa1024_public_op(work_buf.drive_pubkey, padded_session, encrypted_session);
            if (ret != 0) {
                ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                                   "RSA encrypt session_random failed: %d", ret);
                goto stop;
            }
            ps3drive_secure_zero(padded_session, sizeof(padded_session));
        }
        sac_hexdump("encrypted_session", encrypted_session, 128);
        ps3drive_debug_hex(handle, "Encrypted session", encrypted_session, 128);

        /* Step 3: Build message to sign with 0x6a format (ISO 9796-2 style)
         *
         * CRITICAL: The SPU does NOT use standard PKCS#1 v1.5 Type 1 padding!
         * Instead it uses a custom 0x6a format (ISO/IEC 9796-2 Digital Signature Scheme 1).
         *
         * Format (128 bytes total):
         *   Byte 0:       0x6a (header marker)
         *   Bytes 1-106:  data = drive_response(16) + cert_id_2(8) + encrypted_session[0:82]
         *   Bytes 107-126: SHA1 hash (20 bytes)
         *   Byte 127:     0xBC (trailer marker)
         *
         * IMPORTANT: The SHA1 hash is computed over the FULL 152 bytes:
         *   drive_response(16) + cert_id_2(8) + encrypted_session[ALL 128 bytes]
         *
         * This differs from the message body which only contains encrypted_session[0:82].
         * The remaining encrypted_session[82:128] is sent separately in the CMD4 output.
         */
        {
            uint8_t data_to_hash[152];  /* Full 152 bytes for SHA1 */
            uint8_t sha1_hash[20];
            uint8_t *p = sign_input;

            /* Build full SHA1 input (152 bytes) */
            memcpy(data_to_hash, work_buf.drive_response, 16);
            memcpy(data_to_hash + 16, work_buf.cert_id_2, 8);
            memcpy(data_to_hash + 24, encrypted_session, 128);  /* FULL 128 bytes */

            /* Compute SHA1 hash of full 152-byte data */
            mbedtls_sha1(data_to_hash, 152, sha1_hash);
            sac_hexline("SHA1(152-byte data)", sha1_hash, 20);

            /* Build 128-byte sign input in 0x6a format */
            *p++ = 0x6a;                              /* Byte 0: header */
            memcpy(p, work_buf.drive_response, 16);   /* Bytes 1-16: drive_response */
            p += 16;
            memcpy(p, work_buf.cert_id_2, 8);         /* Bytes 17-24: cert_id_2 */
            p += 8;
            memcpy(p, encrypted_session, 82);         /* Bytes 25-106: encrypted_session[0:82] */
            p += 82;
            memcpy(p, sha1_hash, 20);                 /* Bytes 107-126: SHA1 hash */
            p += 20;
            *p = 0xBC;                                /* Byte 127: trailer */
        }
        sac_hexdump("sign_input (0x6a format)", sign_input, 128);
        ps3drive_debug_hex(handle, "Sign input (0x6a format)", sign_input, 128);

        /* Step 4: RSA sign with host's private key */
        ret = ps3drive_rsa1024_private_op(sign_input, rsa_signature);
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                               "RSA-1024 sign failed: %d", ret);
            goto stop;
        }
        sac_hexdump("rsa_signature", rsa_signature, 128);
        ps3drive_debug_hex(handle, "RSA signature", rsa_signature, 128);

        /* Step 5: Build output packet
         * payload = signature(128) + encrypted_session[82:128](46) = 174 bytes
         */
        payload_size = 174;  /* 0xAE */
        sg_put_unaligned_be32(payload_size, session_msg);
        memcpy(session_msg + 4, rsa_signature, 128);
        memcpy(session_msg + 4 + 128, encrypted_session + 82, 46);

        /* Calculate send size with 4-byte alignment padding */
        send_size = SAC_SEND_SIZE(payload_size);
        /* For payload_size=174: 174+4=178, +2 padding = 180 */

        sac_hexdump("CMD 4 packet (to drive)", session_msg, send_size);

        ret = sg_ll_ps3_sac_send_key(handle->sg_fd, session_msg, send_size,
                                      3, 16, 0, key_fmt, 0,
                                      handle->noisy, handle->verbose);
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                               "SAC CMD 4 failed: %d", ret);
            goto stop;
        }

        ps3drive_secure_zero(sign_input, sizeof(sign_input));
        ps3drive_secure_zero(encrypted_session, sizeof(encrypted_session));
    }

    /* ========================================================================
     * CMD 5: Validate Key 2 (nested RSA decryption)
     *
     * Input structure (174 bytes):
     *   [0-127]:   RSA-1024 encrypted outer layer
     *   [128-173]: Continuation of inner RSA ciphertext (46 bytes)
     *
     * Protocol flow (Nested RSA):
     * 1. Outer RSA decrypt with drive's public key (e=65537)
     *    Output: marker(1) + host_random_echo(16) + cert_id_1(8) + inner_part(103)
     * 2. Reconstruct inner ciphertext (128 bytes):
     *    inner[0:64]  = outer[25:89]
     *    inner[64:128] = outer[89:107] + input[128:174]
     * 3. Inner RSA decrypt with host's private key
     *    Output: PKCS#1 v1.5 padded: 00 02 [padding] 00 [cert_id_2(8)] [drive_session_random(16)]
     * 4. Derive session key: SHA1(host_session_random || drive_session_random)[:16]
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== SAC CMD 5: Validate Key 2 ===\n");

    ret = sg_ll_ps3_sac_report_key(handle->sg_fd, 3, ioctl_buffer, 180,
                                    16, 0, key_fmt, 0,
                                    handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                           "SAC CMD 5 failed: %d", ret);
        goto stop;
    }

    {
        uint8_t *response = ioctl_buffer + 4;
        uint8_t outer_decrypted[128];
        uint8_t inner_ciphertext[128];
        uint8_t inner_decrypted[128];
        uint8_t kdf_input[32];
        uint8_t sha_hash[20];
        int i;

        ioctl_buffer_size = sg_get_unaligned_be32(ioctl_buffer);
        ps3drive_debug(handle, 2, "CMD 5 response size: %u\n", ioctl_buffer_size);

        /* Step 1: Outer RSA decryption with drive's public key */
        ret = ps3drive_rsa1024_public_op(work_buf.drive_pubkey, response, outer_decrypted);
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                               "Outer RSA decrypt failed: %d", ret);
            goto stop;
        }

        ps3drive_debug(handle, 2, "Outer RSA marker: 0x%02x\n", outer_decrypted[0]);

        /* Verify marker (should be 0x6a) */
        if (outer_decrypted[0] != 0x6a) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                               "Invalid outer RSA marker: 0x%02x", outer_decrypted[0]);
            goto stop;
        }

        /* Verify host random echo (bytes 1-16) */
        if (memcmp(outer_decrypted + 1, work_buf.host_random, 16) != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                               "Host random mismatch in CMD5");
            goto stop;
        }
        ps3drive_debug(handle, 2, "Host random verified OK\n");

        /* Step 2: Reconstruct inner ciphertext */
        /* First half (64 bytes): outer[25:89] */
        memcpy(inner_ciphertext, outer_decrypted + 25, 64);
        /* Second half (64 bytes): outer[89:107] (18 bytes) + response[128:174] (46 bytes) */
        memcpy(inner_ciphertext + 64, outer_decrypted + 89, 18);
        memcpy(inner_ciphertext + 64 + 18, response + 128, 46);

        /* Step 3: Inner RSA decryption with host's private key */
        ret = ps3drive_rsa1024_private_op(inner_ciphertext, inner_decrypted);
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                               "Inner RSA decrypt failed: %d", ret);
            goto stop;
        }

        /* Verify PKCS#1 v1.5 type 2 header */
        if (inner_decrypted[0] != 0x00 || inner_decrypted[1] != 0x02) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                               "Invalid PKCS#1 header: %02x %02x",
                               inner_decrypted[0], inner_decrypted[1]);
            goto stop;
        }

        /* Find 0x00 separator after padding */
        for (i = 2; i < 128; i++) {
            if (inner_decrypted[i] == 0x00) break;
        }
        if (i >= 128 - 24) {  /* Need at least 24 bytes for cert_id_2 + random */
            ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                               "PKCS#1 separator not found");
            goto stop;
        }

        /* Extract cert_id_2 (8 bytes) and drive_session_random (16 bytes) */
        i++;  /* Skip the 0x00 separator */

        /* Verify cert_id_2 matches */
        if (memcmp(inner_decrypted + i, work_buf.cert_id_2, 8) != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                               "Certificate ID mismatch in CMD5");
            goto stop;
        }
        ps3drive_debug(handle, 2, "Certificate ID verified OK\n");

        /* Extract drive_session_random */
        memcpy(work_buf.drive_session_random, inner_decrypted + i + 8, 16);

        ps3drive_debug_hex(handle, "Drive session random", work_buf.drive_session_random, 16);

        /* Step 4: Derive session key = SHA1(host_session_random || drive_session_random)[:16] */
        memcpy(kdf_input, work_buf.host_session_random, 16);
        memcpy(kdf_input + 16, work_buf.drive_session_random, 16);

        ps3drive_sha1(kdf_input, 32, sha_hash);
        memcpy(session_key, sha_hash, 16);

        ps3drive_debug_hex(handle, "Session key", session_key, 16);

        ps3drive_secure_zero(outer_decrypted, sizeof(outer_decrypted));
        ps3drive_secure_zero(inner_ciphertext, sizeof(inner_ciphertext));
        ps3drive_secure_zero(inner_decrypted, sizeof(inner_decrypted));
        ps3drive_secure_zero(kdf_input, sizeof(kdf_input));
    }

    /* ========================================================================
     * CMD 6: Get key blob and derive final disc key
     *
     * The drive sends 48 bytes of AES-encrypted key material.
     * Decrypt with session_key and PS3DRIVE_SESSION_IV.
     * Final disc key = decrypted[0x20:0x30]
     * Final disc IV = static (from CMD0 decryption) = decrypted_iv_bin
     * ======================================================================== */
    ps3drive_debug(handle, 2, "=== SAC CMD 6: Derive Disc Key ===\n");

    ret = sg_ll_ps3_sac_report_key(handle->sg_fd, 4, ioctl_buffer, 52,
                                    16, 0, key_fmt, 0,
                                    handle->noisy, handle->verbose);
    if (ret != 0) {
        ps3drive_set_error(handle, PS3DRIVE_ERR_SAC_FAILED,
                           "SAC CMD 6 failed: %d", ret);
        goto stop;
    }

    {
        uint8_t encrypted_blob[48];
        uint8_t decrypted[48];

        memcpy(encrypted_blob, ioctl_buffer + 4, 48);

        ps3drive_debug_hex(handle, "Encrypted blob", encrypted_blob, 48);
        ps3drive_debug_hex(handle, "Session IV", PS3DRIVE_SESSION_IV, 16);

        /* AES-CBC-128 decrypt the 48-byte blob */
        ret = ps3drive_aes128_cbc_decrypt(session_key, PS3DRIVE_SESSION_IV,
                                           encrypted_blob, decrypted, 48);
        if (ret != 0) {
            ps3drive_set_error(handle, PS3DRIVE_ERR_CRYPTO_FAILED,
                               "AES decryption failed: %d", ret);
            goto stop;
        }

        ps3drive_debug_hex(handle, "Decrypted blob", decrypted, 48);

        /*
         * Decrypted structure (verified from emulator):
         *   [0x00-0x0F]: Relates to disc IV / padding
         *   [0x10-0x1F]: Metadata / intermediate data
         *   [0x20-0x2F]: *** FINAL DISC AES KEY ***
         */

        /* Extract final AES key from offset 0x20 */
        memcpy(aes_key, decrypted + 0x20, 16);

        /* Disc IV is STATIC - comes from CMD0 internal decryption */
        memcpy(aes_iv, PS3DRIVE_DISC_IV, 16);

        ps3drive_debug_hex(handle, "Final AES Key", aes_key, 16);
        ps3drive_debug_hex(handle, "Final AES IV", aes_iv, 16);

        ps3drive_secure_zero(decrypted, sizeof(decrypted));
    }

    ps3drive_debug(handle, 1, "SAC key exchange completed successfully\n");
    ret = 0;

stop:
    /* Cleanup: notify drive that we're done */
    {
        uint8_t cleanup_fmt = 1;
        sg_ll_ps3_sac_report_key(handle->sg_fd, 255, NULL, 0,
                                  16, 0, cleanup_fmt, 0, 0, 0);
    }

    /* Securely zero sensitive data */
    ps3drive_secure_zero(&work_buf, sizeof(work_buf));
    ps3drive_secure_zero(session_key, sizeof(session_key));

    if (ret != 0) {
        return PS3DRIVE_ERR_SAC_FAILED;
    }

    return PS3DRIVE_OK;
}
