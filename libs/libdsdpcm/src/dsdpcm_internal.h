/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Internal types and declarations for libdsdpcm C wrapper
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

#ifndef LIBDSDPCM_DSDPCM_INTERNAL_H
#define LIBDSDPCM_DSDPCM_INTERNAL_H

#include <libdsdpcm/dsdpcm.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * FIR File Format Constants
 * ========================================================================== */

/** Magic bytes for binary FIR file format */
#define DSDPCM_FIR_MAGIC "DFIR"
#define DSDPCM_FIR_MAGIC_SIZE 4

/** Current binary FIR file format version */
#define DSDPCM_FIR_VERSION 1

/** Maximum FIR filter name length */
#define DSDPCM_FIR_MAX_NAME_LENGTH 256

/** Maximum number of FIR coefficients */
#define DSDPCM_FIR_MAX_COEFFICIENTS 8192

/* ==========================================================================
 * Binary FIR File Header
 * ========================================================================== */

/**
 * @brief Binary FIR file header structure
 *
 * File layout:
 * - magic[4]        "DFIR"
 * - version         uint32_t (1)
 * - decimation      uint32_t
 * - coef_count      uint32_t
 * - name_length     uint32_t
 * - name[]          char[name_length] (UTF-8, no null)
 * - coefficients[]  double[coef_count]
 */
typedef struct dsdpcm_fir_header_s {
    char     magic[DSDPCM_FIR_MAGIC_SIZE];
    uint32_t version;
    uint32_t decimation;
    uint32_t coef_count;
    uint32_t name_length;
} dsdpcm_fir_header_t;

/* ==========================================================================
 * Internal FIR I/O Functions (implemented in dsdpcm_fir_io.c)
 * ========================================================================== */

/**
 * @brief Load FIR coefficients from text file
 *
 * @param fir      FIR structure to populate
 * @param filename File path (UTF-8 encoded)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
int dsdpcm_fir_load_text(dsdpcm_fir_t *fir, const char *filename);

/**
 * @brief Load FIR coefficients from binary file
 *
 * @param fir      FIR structure to populate
 * @param filename File path (UTF-8 encoded)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
int dsdpcm_fir_load_binary(dsdpcm_fir_t *fir, const char *filename);

/**
 * @brief Save FIR coefficients to text file
 *
 * @param fir      FIR structure
 * @param filename File path (UTF-8 encoded)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
int dsdpcm_fir_save_text(const dsdpcm_fir_t *fir, const char *filename);

/**
 * @brief Save FIR coefficients to binary file
 *
 * @param fir      FIR structure
 * @param filename File path (UTF-8 encoded)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
int dsdpcm_fir_save_binary(const dsdpcm_fir_t *fir, const char *filename);

/**
 * @brief Detect if file is binary FIR format
 *
 * @param filename File path (UTF-8 encoded)
 *
 * @return 1 if binary format, 0 if text format, negative on error
 */
int dsdpcm_fir_is_binary(const char *filename);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDPCM_DSDPCM_INTERNAL_H */
