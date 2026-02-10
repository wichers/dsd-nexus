/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Internal cryptographic function declarations using mbedtls.
 * This file contains internal cryptographic wrappers around mbedtls
 * for AES, DES, SHA1, and RSA operations needed by the PS3 drive library.
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

#ifndef LIBPS3DRIVE_PS3DRIVE_CRYPTO_H
#define LIBPS3DRIVE_PS3DRIVE_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Initialization / Cleanup
 * ============================================================================ */

/**
 * @brief Initialize the cryptographic subsystem.
 *
 * Initializes the random number generator and other crypto state.
 * Called automatically by other crypto functions if needed.
 *
 * @return 0 on success, non-zero on error
 */
int ps3drive_crypto_init(void);

/**
 * @brief Clean up the cryptographic subsystem.
 *
 * Frees any resources allocated by ps3drive_crypto_init().
 * Should be called when the library is no longer needed.
 */
void ps3drive_crypto_cleanup(void);

/* ============================================================================
 * AES Operations
 * ============================================================================ */

/**
 * @brief AES-128-CBC encryption.
 *
 * @param[in]  key     16-byte AES key
 * @param[in]  iv      16-byte initialization vector (not modified)
 * @param[in]  input   Plaintext (must be multiple of 16 bytes)
 * @param[out] output  Ciphertext (may be same as input for in-place)
 * @param[in]  len     Length in bytes (must be multiple of 16)
 * @return 0 on success, non-zero on error
 */
int ps3drive_aes128_cbc_encrypt(const uint8_t *key, const uint8_t *iv,
                                 const uint8_t *input, uint8_t *output,
                                 size_t len);

/**
 * @brief AES-128-CBC decryption.
 *
 * @param[in]  key     16-byte AES key
 * @param[in]  iv      16-byte initialization vector (not modified)
 * @param[in]  input   Ciphertext (must be multiple of 16 bytes)
 * @param[out] output  Plaintext (may be same as input for in-place)
 * @param[in]  len     Length in bytes (must be multiple of 16)
 * @return 0 on success, non-zero on error
 */
int ps3drive_aes128_cbc_decrypt(const uint8_t *key, const uint8_t *iv,
                                 const uint8_t *input, uint8_t *output,
                                 size_t len);

/**
 * @brief AES-256-CBC decryption.
 *
 * Used for EID2 decryption in drive pairing.
 *
 * @param[in]  key     32-byte AES-256 key
 * @param[in]  iv      16-byte initialization vector (not modified)
 * @param[in]  input   Ciphertext (must be multiple of 16 bytes)
 * @param[out] output  Plaintext (may be same as input for in-place)
 * @param[in]  len     Length in bytes (must be multiple of 16)
 * @return 0 on success, non-zero on error
 */
int ps3drive_aes256_cbc_decrypt(const uint8_t *key, const uint8_t *iv,
                                 const uint8_t *input, uint8_t *output,
                                 size_t len);

/* ============================================================================
 * DES Operations
 * ============================================================================ */

/**
 * @brief Triple DES (3DES/TDES) CBC encryption.
 *
 * Uses EDE (Encrypt-Decrypt-Encrypt) mode with a 24-byte key.
 *
 * @param[in]  key     24-byte 3DES key (K1 || K2 || K3)
 * @param[in]  iv      8-byte initialization vector (not modified)
 * @param[in]  input   Plaintext (must be multiple of 8 bytes)
 * @param[out] output  Ciphertext (may be same as input for in-place)
 * @param[in]  len     Length in bytes (must be multiple of 8)
 * @return 0 on success, non-zero on error
 */
int ps3drive_3des_cbc_encrypt(const uint8_t *key, const uint8_t *iv,
                               const uint8_t *input, uint8_t *output,
                               size_t len);

/**
 * @brief Single DES CBC decryption.
 *
 * Used for P-Block/S-Block decryption in drive pairing.
 *
 * @param[in]  key     8-byte DES key
 * @param[in]  iv      8-byte initialization vector (not modified)
 * @param[in]  input   Ciphertext (must be multiple of 8 bytes)
 * @param[out] output  Plaintext (may be same as input for in-place)
 * @param[in]  len     Length in bytes (must be multiple of 8)
 * @return 0 on success, non-zero on error
 */
int ps3drive_des_cbc_decrypt(const uint8_t *key, const uint8_t *iv,
                              const uint8_t *input, uint8_t *output,
                              size_t len);

/* ============================================================================
 * Hash Operations
 * ============================================================================ */

/**
 * @brief SHA-1 hash.
 *
 * @param[in]  data    Data to hash
 * @param[in]  len     Length of data
 * @param[out] output  20-byte hash output
 */
void ps3drive_sha1(const uint8_t *data, size_t len, uint8_t *output);

/**
 * @brief SHA-1 Key Derivation Function (KDF).
 *
 * Specialized KDF used in SAC key exchange.
 * Computes: output = SHA1(plain XOR crypt || crypt)
 *
 * @param[out] output  20-byte derived key output
 * @param[in]  plain   20-byte plain random data
 * @param[in]  crypt   20-byte encrypted random data
 */
void ps3drive_sha1_kdf(uint8_t *output, const uint8_t *plain,
                        const uint8_t *crypt);

/* ============================================================================
 * RSA Operations
 * ============================================================================ */

/**
 * @brief RSA-175 raw private key operation.
 *
 * Computes: output = input^d mod n (without padding)
 * Uses the embedded RSA-175 private key.
 *
 * @param[in]  input   22-byte input
 * @param[out] output  22-byte output
 * @return 0 on success, non-zero on error
 */
int ps3drive_rsa175_private_op(const uint8_t *input, uint8_t *output);

/**
 * @brief RSA-1024 raw private key operation.
 *
 * Computes: output = input^d mod n (without padding)
 * Uses the embedded RSA-1024 private key.
 *
 * @param[in]  input   128-byte input
 * @param[out] output  128-byte output
 * @return 0 on success, non-zero on error
 */
int ps3drive_rsa1024_private_op(const uint8_t *input, uint8_t *output);

/**
 * @brief RSA-1024 public key operation with arbitrary modulus.
 *
 * Computes: output = input^65537 mod n
 *
 * Uses square-and-multiply algorithm to handle even moduli
 * (mbedtls_mpi_exp_mod requires odd modulus for Montgomery multiplication).
 *
 * @param[in]  n       128-byte RSA modulus
 * @param[in]  input   128-byte input
 * @param[out] output  128-byte output
 * @return 0 on success, non-zero on error
 */
int ps3drive_rsa1024_public_op(const uint8_t *n, const uint8_t *input, uint8_t *output);

/* ============================================================================
 * Random Number Generation
 * ============================================================================ */

/**
 * @brief Generate cryptographically secure random bytes.
 *
 * @param[out] output  Buffer to receive random bytes
 * @param[in]  len     Number of bytes to generate
 * @return 0 on success, non-zero on error
 */
int ps3drive_random_bytes(uint8_t *output, size_t len);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Securely zero memory.
 *
 * Guaranteed not to be optimized away by the compiler.
 *
 * @param[out] ptr  Memory to zero
 * @param[in]  len  Number of bytes to zero
 */
void ps3drive_secure_zero(void *ptr, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* LIBPS3DRIVE_PS3DRIVE_CRYPTO_H */
