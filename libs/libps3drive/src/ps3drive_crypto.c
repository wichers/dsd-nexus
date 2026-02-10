/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Cryptographic operations using mbedtls.
 * This file implements cryptographic wrappers around mbedtls for
 * AES, DES, SHA1, and RSA operations needed by the PS3 drive library.
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

#include "ps3drive_crypto.h"
#include "ps3drive_keys.h"

#include <string.h>
#include <mbedtls/aes.h>
#include <mbedtls/des.h>
#include <mbedtls/sha1.h>
#include <mbedtls/bignum.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/platform_util.h>

/* ============================================================================
 * Random Number Generator State
 * ============================================================================ */

typedef struct {
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    int                      initialized;
} ps3drive_rng_ctx_t;

static ps3drive_rng_ctx_t g_rng = {0};

/* ============================================================================
 * Initialization / Cleanup
 * ============================================================================ */

int ps3drive_crypto_init(void)
{
    int ret;
    const char *personalization = "ps3drive_crypto";

    if (g_rng.initialized) {
        return 0;
    }

    mbedtls_entropy_init(&g_rng.entropy);
    mbedtls_ctr_drbg_init(&g_rng.ctr_drbg);

    ret = mbedtls_ctr_drbg_seed(&g_rng.ctr_drbg,
                                 mbedtls_entropy_func,
                                 &g_rng.entropy,
                                 (const unsigned char *)personalization,
                                 strlen(personalization));
    if (ret != 0) {
        mbedtls_ctr_drbg_free(&g_rng.ctr_drbg);
        mbedtls_entropy_free(&g_rng.entropy);
        return ret;
    }

    g_rng.initialized = 1;
    return 0;
}

void ps3drive_crypto_cleanup(void)
{
    if (g_rng.initialized) {
        mbedtls_ctr_drbg_free(&g_rng.ctr_drbg);
        mbedtls_entropy_free(&g_rng.entropy);
        g_rng.initialized = 0;
    }
}

/* ============================================================================
 * AES Operations
 * ============================================================================ */

int ps3drive_aes128_cbc_encrypt(const uint8_t *key, const uint8_t *iv,
                                 const uint8_t *input, uint8_t *output,
                                 size_t len)
{
    mbedtls_aes_context aes;
    uint8_t iv_copy[16];
    int ret;

    if (key == NULL || iv == NULL || input == NULL || output == NULL) {
        return -1;
    }
    if (len == 0 || (len % 16) != 0) {
        return -1;
    }

    /* mbedtls modifies IV in place, so make a copy */
    memcpy(iv_copy, iv, 16);

    mbedtls_aes_init(&aes);

    ret = mbedtls_aes_setkey_enc(&aes, key, 128);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return ret;
    }

    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, len, iv_copy,
                                 input, output);

    mbedtls_aes_free(&aes);
    ps3drive_secure_zero(iv_copy, sizeof(iv_copy));

    return ret;
}

int ps3drive_aes128_cbc_decrypt(const uint8_t *key, const uint8_t *iv,
                                 const uint8_t *input, uint8_t *output,
                                 size_t len)
{
    mbedtls_aes_context aes;
    uint8_t iv_copy[16];
    int ret;

    if (key == NULL || iv == NULL || input == NULL || output == NULL) {
        return -1;
    }
    if (len == 0 || (len % 16) != 0) {
        return -1;
    }

    /* mbedtls modifies IV in place, so make a copy */
    memcpy(iv_copy, iv, 16);

    mbedtls_aes_init(&aes);

    ret = mbedtls_aes_setkey_dec(&aes, key, 128);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return ret;
    }

    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len, iv_copy,
                                 input, output);

    mbedtls_aes_free(&aes);
    ps3drive_secure_zero(iv_copy, sizeof(iv_copy));

    return ret;
}

int ps3drive_aes256_cbc_decrypt(const uint8_t *key, const uint8_t *iv,
                                 const uint8_t *input, uint8_t *output,
                                 size_t len)
{
    mbedtls_aes_context aes;
    uint8_t iv_copy[16];
    int ret;

    if (key == NULL || iv == NULL || input == NULL || output == NULL) {
        return -1;
    }
    if (len == 0 || (len % 16) != 0) {
        return -1;
    }

    /* mbedtls modifies IV in place, so make a copy */
    memcpy(iv_copy, iv, 16);

    mbedtls_aes_init(&aes);

    ret = mbedtls_aes_setkey_dec(&aes, key, 256);
    if (ret != 0) {
        mbedtls_aes_free(&aes);
        return ret;
    }

    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len, iv_copy,
                                 input, output);

    mbedtls_aes_free(&aes);
    ps3drive_secure_zero(iv_copy, sizeof(iv_copy));

    return ret;
}

/* ============================================================================
 * DES Operations
 * ============================================================================ */

int ps3drive_3des_cbc_encrypt(const uint8_t *key, const uint8_t *iv,
                               const uint8_t *input, uint8_t *output,
                               size_t len)
{
    mbedtls_des3_context des3;
    uint8_t iv_copy[8];
    int ret;

    if (key == NULL || iv == NULL || input == NULL || output == NULL) {
        return -1;
    }
    if (len == 0 || (len % 8) != 0) {
        return -1;
    }

    /* mbedtls modifies IV in place, so make a copy */
    memcpy(iv_copy, iv, 8);

    mbedtls_des3_init(&des3);

    ret = mbedtls_des3_set3key_enc(&des3, key);
    if (ret != 0) {
        mbedtls_des3_free(&des3);
        return ret;
    }

    ret = mbedtls_des3_crypt_cbc(&des3, MBEDTLS_DES_ENCRYPT, len, iv_copy,
                                  input, output);

    mbedtls_des3_free(&des3);
    ps3drive_secure_zero(iv_copy, sizeof(iv_copy));

    return ret;
}

int ps3drive_des_cbc_decrypt(const uint8_t *key, const uint8_t *iv,
                              const uint8_t *input, uint8_t *output,
                              size_t len)
{
    mbedtls_des_context des;
    uint8_t iv_copy[8];
    int ret;

    if (key == NULL || iv == NULL || input == NULL || output == NULL) {
        return -1;
    }
    if (len == 0 || (len % 8) != 0) {
        return -1;
    }

    /* mbedtls modifies IV in place, so make a copy */
    memcpy(iv_copy, iv, 8);

    mbedtls_des_init(&des);

    ret = mbedtls_des_setkey_dec(&des, key);
    if (ret != 0) {
        mbedtls_des_free(&des);
        return ret;
    }

    ret = mbedtls_des_crypt_cbc(&des, MBEDTLS_DES_DECRYPT, len, iv_copy,
                                 input, output);

    mbedtls_des_free(&des);
    ps3drive_secure_zero(iv_copy, sizeof(iv_copy));

    return ret;
}

/* ============================================================================
 * Hash Operations
 * ============================================================================ */

void ps3drive_sha1(const uint8_t *data, size_t len, uint8_t *output)
{
    mbedtls_sha1_context sha1;

    if (data == NULL || output == NULL) {
        return;
    }

    mbedtls_sha1_init(&sha1);
    mbedtls_sha1_starts(&sha1);
    mbedtls_sha1_update(&sha1, data, len);
    mbedtls_sha1_finish(&sha1, output);
    mbedtls_sha1_free(&sha1);
}

void ps3drive_sha1_kdf(uint8_t *output, const uint8_t *plain,
                        const uint8_t *crypt)
{
    uint8_t combined[40];
    uint8_t hash[20];
    int i;

    if (output == NULL || plain == NULL || crypt == NULL) {
        return;
    }

    /* Combine: first 20 bytes = plain XOR crypt, next 20 bytes = crypt */
    memcpy(combined, plain, 20);
    memcpy(combined + 20, crypt, 20);
    for (i = 0; i < 20; i++) {
        combined[i] ^= combined[i + 20];
    }

    /* SHA1 hash of combined data */
    ps3drive_sha1(combined, 40, hash);
    memcpy(output, hash, 20);

    ps3drive_secure_zero(combined, sizeof(combined));
    ps3drive_secure_zero(hash, sizeof(hash));
}

/* ============================================================================
 * RSA Operations
 * ============================================================================ */

/**
 * @brief Raw RSA private key operation: output = input^d mod n
 */
static int rsa_raw_private_op(const uint8_t *n_bytes, size_t n_len,
                               const uint8_t *d_bytes, size_t d_len,
                               const uint8_t *input, uint8_t *output,
                               size_t io_len)
{
    mbedtls_mpi N, D, M, C;
    int ret = 0;
    size_t olen;

    mbedtls_mpi_init(&N);
    mbedtls_mpi_init(&D);
    mbedtls_mpi_init(&M);
    mbedtls_mpi_init(&C);

    /* Load N (modulus) */
    ret = mbedtls_mpi_read_binary(&N, n_bytes, n_len);
    if (ret != 0) {
        goto cleanup;
    }

    /* Load D (private exponent) */
    ret = mbedtls_mpi_read_binary(&D, d_bytes, d_len);
    if (ret != 0) {
        goto cleanup;
    }

    /* Load M (input message) */
    ret = mbedtls_mpi_read_binary(&M, input, io_len);
    if (ret != 0) {
        goto cleanup;
    }

    /* C = M^D mod N */
    ret = mbedtls_mpi_exp_mod(&C, &M, &D, &N, NULL);
    if (ret != 0) {
        goto cleanup;
    }

    /* Write output with leading zeros if needed */
    memset(output, 0, io_len);
    olen = mbedtls_mpi_size(&C);
    if (olen > io_len) {
        ret = -1;
        goto cleanup;
    }
    ret = mbedtls_mpi_write_binary(&C, output + (io_len - olen), olen);

cleanup:
    mbedtls_mpi_free(&N);
    mbedtls_mpi_free(&D);
    mbedtls_mpi_free(&M);
    mbedtls_mpi_free(&C);

    return ret;
}

/* ============================================================================
 * Public RSA Operations (wrappers using fixed keys)
 * ============================================================================ */

int ps3drive_rsa1024_private_op(const uint8_t *input, uint8_t *output)
{
    return rsa_raw_private_op(PS3DRIVE_RSA1024_N, 128,
                               PS3DRIVE_RSA1024_D, 128,
                               input, output, 128);
}

/**
 * @brief RSA-1024 public key operation with arbitrary modulus.
 *
 * Computes: output = input^65537 mod n
 *
 * Uses square-and-multiply algorithm since mbedtls_mpi_exp_mod requires
 * odd modulus (Montgomery multiplication), but some drive certificates
 * have even moduli.
 *
 * For e = 65537 = 2^16 + 1:
 *   M^65537 = M^(2^16) * M = ((M^2)^2)^...^2 * M  (16 squarings, then multiply by M)
 *
 * @param[in]  n       128-byte RSA modulus
 * @param[in]  input   128-byte input
 * @param[out] output  128-byte output
 * @return 0 on success, non-zero on error
 */
int ps3drive_rsa1024_public_op(const uint8_t *n, const uint8_t *input, uint8_t *output)
{
    mbedtls_mpi N, M, C, T;
    int ret = 0;
    size_t olen;
    int i;

    mbedtls_mpi_init(&N);
    mbedtls_mpi_init(&M);
    mbedtls_mpi_init(&C);
    mbedtls_mpi_init(&T);

    ret = mbedtls_mpi_read_binary(&N, n, 128);
    if (ret != 0) {
        goto cleanup;
    }

    ret = mbedtls_mpi_read_binary(&M, input, 128);
    if (ret != 0) {
        goto cleanup;
    }

    /* Compute M^65537 mod N using square-and-multiply
     * 65537 = 0x10001 = 2^16 + 1
     * So: M^65537 = (M^(2^16)) * M
     *
     * Algorithm:
     * 1. C = M
     * 2. Square C 16 times: C = C^2 mod N (16 times)
     * 3. Multiply by M: C = C * M mod N
     */

    /* C = M */
    ret = mbedtls_mpi_copy(&C, &M);
    if (ret != 0) goto cleanup;

    /* Square 16 times: C = M^(2^16) mod N */
    for (i = 0; i < 16; i++) {
        /* T = C * C */
        ret = mbedtls_mpi_mul_mpi(&T, &C, &C);
        if (ret != 0) goto cleanup;

        /* C = T mod N */
        ret = mbedtls_mpi_mod_mpi(&C, &T, &N);
        if (ret != 0) goto cleanup;
    }

    /* C = C * M mod N  (this gives M^(2^16 + 1) = M^65537) */
    ret = mbedtls_mpi_mul_mpi(&T, &C, &M);
    if (ret != 0) goto cleanup;

    ret = mbedtls_mpi_mod_mpi(&C, &T, &N);
    if (ret != 0) goto cleanup;

    /* Write result */
    memset(output, 0, 128);
    olen = mbedtls_mpi_size(&C);
    if (olen > 128) { ret = -1; goto cleanup; }
    ret = mbedtls_mpi_write_binary(&C, output + (128 - olen), olen);

cleanup:
    mbedtls_mpi_free(&N);
    mbedtls_mpi_free(&M);
    mbedtls_mpi_free(&C);
    mbedtls_mpi_free(&T);
    return ret;
}

/* ============================================================================
 * Random Number Generation
 * ============================================================================ */

int ps3drive_random_bytes(uint8_t *output, size_t len)
{
    int ret;
    if (output == NULL || len == 0) {
        return -1;
    }

    if (!g_rng.initialized) {
        ret = ps3drive_crypto_init();
        if (ret != 0) {
            return ret;
        }
    }

    return mbedtls_ctr_drbg_random(&g_rng.ctr_drbg, output, len);
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

void ps3drive_secure_zero(void *ptr, size_t len)
{
    if (ptr != NULL && len > 0) {
        mbedtls_platform_zeroize(ptr, len);
    }
}
