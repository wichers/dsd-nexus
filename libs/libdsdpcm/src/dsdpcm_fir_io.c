/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief FIR coefficient file I/O implementation
 * Supports two file formats:
 * - Text format: Human-readable, one coefficient per line, comments with '#'
 * - Binary format: Compact format with header and double array
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <libdsdpcm/dsdpcm.h>
#include "dsdpcm_internal.h"

/* UTF-8 compatible file I/O from libsautil */
#include <libsautil/compat.h>

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/**
 * @brief Trim leading and trailing whitespace from a string
 */
static char *trim_whitespace(char *str)
{
    char *end;

    /* Trim leading space */
    while (isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    /* Trim trailing space */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }

    end[1] = '\0';
    return str;
}

/**
 * @brief Parse decimation value from string
 */
static dsdpcm_decimation_t parse_decimation(const char *str)
{
    char *endptr = NULL;
    long val;

    if (!str || *str == '\0') {
        return DSDPCM_DECIMATION_AUTO;
    }

    val = strtol(str, &endptr, 10);

    if (endptr == str || *endptr != '\0') {
        /* Not a valid number, check for "auto" */
        if (strcmp(str, "auto") == 0 || strcmp(str, "AUTO") == 0) {
            return DSDPCM_DECIMATION_AUTO;
        }
        return DSDPCM_DECIMATION_AUTO;
    }

    switch (val) {
        case 8:    return DSDPCM_DECIMATION_8;
        case 16:   return DSDPCM_DECIMATION_16;
        case 32:   return DSDPCM_DECIMATION_32;
        case 64:   return DSDPCM_DECIMATION_64;
        case 128:  return DSDPCM_DECIMATION_128;
        case 256:  return DSDPCM_DECIMATION_256;
        case 512:  return DSDPCM_DECIMATION_512;
        case 1024: return DSDPCM_DECIMATION_1024;
        default:   return DSDPCM_DECIMATION_AUTO;
    }
}

/* ==========================================================================
 * Binary Format Detection
 * ========================================================================== */

int dsdpcm_fir_is_binary(const char *filename)
{
    FILE *fp;
    char magic[DSDPCM_FIR_MAGIC_SIZE];
    size_t read_count;

    if (!filename) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    fp = sa_fopen(filename, "rb");
    if (!fp) {
        return DSDPCM_ERR_FILE_OPEN;
    }

    read_count = fread(magic, 1, DSDPCM_FIR_MAGIC_SIZE, fp);
    fclose(fp);

    if (read_count != DSDPCM_FIR_MAGIC_SIZE) {
        return 0; /* Assume text if file is too short */
    }

    return (memcmp(magic, DSDPCM_FIR_MAGIC, DSDPCM_FIR_MAGIC_SIZE) == 0) ? 1 : 0;
}

/* ==========================================================================
 * Text Format I/O
 * ========================================================================== */

int dsdpcm_fir_load_text(dsdpcm_fir_t *fir, const char *filename)
{
    FILE *fp = NULL;
    char line[1024];
    double *coefficients = NULL;
    size_t capacity = 256;
    size_t count = 0;
    char *name = NULL;
    dsdpcm_decimation_t decimation = DSDPCM_DECIMATION_AUTO;
    int result = DSDPCM_OK;

    if (!fir || !filename) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    fp = sa_fopen(filename, "r");
    if (!fp) {
        return DSDPCM_ERR_FILE_OPEN;
    }

    /* Initial allocation */
    coefficients = (double *)malloc(capacity * sizeof(double));
    if (!coefficients) {
        fclose(fp);
        return DSDPCM_ERR_ALLOC_FAILED;
    }

    /* Read file line by line */
    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim_whitespace(line);

        /* Skip empty lines */
        if (*trimmed == '\0') {
            continue;
        }

        /* Parse comment lines for metadata */
        if (*trimmed == '#') {
            char *value = NULL;

            /* Check for "# Name:" header */
            if (strncmp(trimmed, "# Name:", 7) == 0) {
                value = trim_whitespace(trimmed + 7);
                if (*value && !name) {
                    size_t len = strlen(value);
                    if (len > DSDPCM_FIR_MAX_NAME_LENGTH) {
                        len = DSDPCM_FIR_MAX_NAME_LENGTH;
                    }
                    name = (char *)malloc(len + 1);
                    if (name) {
                        memcpy(name, value, len);
                        name[len] = '\0';
                    }
                }
            }
            /* Check for "# Decimation:" header */
            else if (strncmp(trimmed, "# Decimation:", 13) == 0) {
                value = trim_whitespace(trimmed + 13);
                decimation = parse_decimation(value);
            }
            /* Ignore other comments */
            continue;
        }

        /* Parse coefficient */
        {
            char *endptr = NULL;
            double coef;

            errno = 0;
            coef = strtod(trimmed, &endptr);

            if (endptr == trimmed || errno != 0) {
                /* Skip invalid lines */
                continue;
            }

            /* Grow buffer if needed */
            if (count >= capacity) {
                size_t new_capacity = capacity * 2;
                double *new_buf;

                if (new_capacity > DSDPCM_FIR_MAX_COEFFICIENTS) {
                    new_capacity = DSDPCM_FIR_MAX_COEFFICIENTS;
                }

                if (count >= new_capacity) {
                    result = DSDPCM_ERR_BUFFER_TOO_SMALL;
                    goto cleanup;
                }

                new_buf = (double *)realloc(coefficients, new_capacity * sizeof(double));
                if (!new_buf) {
                    result = DSDPCM_ERR_ALLOC_FAILED;
                    goto cleanup;
                }
                coefficients = new_buf;
                capacity = new_capacity;
            }

            coefficients[count++] = coef;
        }
    }

    if (ferror(fp)) {
        result = DSDPCM_ERR_FILE_READ;
        goto cleanup;
    }

    if (count == 0) {
        result = DSDPCM_ERR_FILE_FORMAT;
        goto cleanup;
    }

    /* Update FIR structure */
    free(fir->coefficients);
    free(fir->name);

    fir->coefficients = coefficients;
    fir->count = count;
    fir->decimation = decimation;
    fir->name = name;

    coefficients = NULL; /* Ownership transferred */
    name = NULL;

cleanup:
    if (fp) {
        fclose(fp);
    }
    free(coefficients);
    free(name);
    return result;
}

int dsdpcm_fir_save_text(const dsdpcm_fir_t *fir, const char *filename)
{
    FILE *fp = NULL;
    size_t i;

    if (!fir || !filename) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    if (!fir->coefficients || fir->count == 0) {
        return DSDPCM_ERR_INVALID_PARAM;
    }

    fp = sa_fopen(filename, "w");
    if (!fp) {
        return DSDPCM_ERR_FILE_OPEN;
    }

    /* Write header comments */
    fprintf(fp, "# FIR Filter Coefficients\n");
    fprintf(fp, "# Generated by libdsdpcm %s\n", dsdpcm_version_string());
    fprintf(fp, "#\n");

    if (fir->name) {
        fprintf(fp, "# Name: %s\n", fir->name);
    }

    if (fir->decimation != DSDPCM_DECIMATION_AUTO) {
        fprintf(fp, "# Decimation: %d\n", (int)fir->decimation);
    } else {
        fprintf(fp, "# Decimation: auto\n");
    }

    fprintf(fp, "# Count: %zu\n", fir->count);
    fprintf(fp, "#\n");

    /* Write coefficients */
    for (i = 0; i < fir->count; i++) {
        fprintf(fp, "%.17g\n", fir->coefficients[i]);
    }

    if (ferror(fp)) {
        fclose(fp);
        return DSDPCM_ERR_FILE_WRITE;
    }

    fclose(fp);
    return DSDPCM_OK;
}

/* ==========================================================================
 * Binary Format I/O
 * ========================================================================== */

int dsdpcm_fir_load_binary(dsdpcm_fir_t *fir, const char *filename)
{
    FILE *fp = NULL;
    dsdpcm_fir_header_t header;
    double *coefficients = NULL;
    char *name = NULL;
    int result = DSDPCM_OK;

    if (!fir || !filename) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    fp = sa_fopen(filename, "rb");
    if (!fp) {
        return DSDPCM_ERR_FILE_OPEN;
    }

    /* Read header */
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        result = DSDPCM_ERR_FILE_READ;
        goto cleanup;
    }

    /* Validate magic */
    if (memcmp(header.magic, DSDPCM_FIR_MAGIC, DSDPCM_FIR_MAGIC_SIZE) != 0) {
        result = DSDPCM_ERR_FILE_FORMAT;
        goto cleanup;
    }

    /* Validate version */
    if (header.version != DSDPCM_FIR_VERSION) {
        result = DSDPCM_ERR_FILE_FORMAT;
        goto cleanup;
    }

    /* Validate coefficient count */
    if (header.coef_count == 0 || header.coef_count > DSDPCM_FIR_MAX_COEFFICIENTS) {
        result = DSDPCM_ERR_FILE_FORMAT;
        goto cleanup;
    }

    /* Validate name length */
    if (header.name_length > DSDPCM_FIR_MAX_NAME_LENGTH) {
        result = DSDPCM_ERR_FILE_FORMAT;
        goto cleanup;
    }

    /* Read name if present */
    if (header.name_length > 0) {
        name = (char *)malloc(header.name_length + 1);
        if (!name) {
            result = DSDPCM_ERR_ALLOC_FAILED;
            goto cleanup;
        }

        if (fread(name, 1, header.name_length, fp) != header.name_length) {
            result = DSDPCM_ERR_FILE_READ;
            goto cleanup;
        }
        name[header.name_length] = '\0';
    }

    /* Allocate and read coefficients */
    coefficients = (double *)malloc(header.coef_count * sizeof(double));
    if (!coefficients) {
        result = DSDPCM_ERR_ALLOC_FAILED;
        goto cleanup;
    }

    if (fread(coefficients, sizeof(double), header.coef_count, fp) != header.coef_count) {
        result = DSDPCM_ERR_FILE_READ;
        goto cleanup;
    }

    /* Update FIR structure */
    free(fir->coefficients);
    free(fir->name);

    fir->coefficients = coefficients;
    fir->count = header.coef_count;
    fir->decimation = (dsdpcm_decimation_t)header.decimation;
    fir->name = name;

    coefficients = NULL; /* Ownership transferred */
    name = NULL;

cleanup:
    if (fp) {
        fclose(fp);
    }
    free(coefficients);
    free(name);
    return result;
}

int dsdpcm_fir_save_binary(const dsdpcm_fir_t *fir, const char *filename)
{
    FILE *fp = NULL;
    dsdpcm_fir_header_t header;
    size_t name_len;

    if (!fir || !filename) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    if (!fir->coefficients || fir->count == 0) {
        return DSDPCM_ERR_INVALID_PARAM;
    }

    fp = sa_fopen(filename, "wb");
    if (!fp) {
        return DSDPCM_ERR_FILE_OPEN;
    }

    /* Prepare header */
    memcpy(header.magic, DSDPCM_FIR_MAGIC, DSDPCM_FIR_MAGIC_SIZE);
    header.version = DSDPCM_FIR_VERSION;
    header.decimation = (uint32_t)fir->decimation;
    header.coef_count = (uint32_t)fir->count;

    name_len = fir->name ? strlen(fir->name) : 0;
    if (name_len > DSDPCM_FIR_MAX_NAME_LENGTH) {
        name_len = DSDPCM_FIR_MAX_NAME_LENGTH;
    }
    header.name_length = (uint32_t)name_len;

    /* Write header */
    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return DSDPCM_ERR_FILE_WRITE;
    }

    /* Write name */
    if (name_len > 0) {
        if (fwrite(fir->name, 1, name_len, fp) != name_len) {
            fclose(fp);
            return DSDPCM_ERR_FILE_WRITE;
        }
    }

    /* Write coefficients */
    if (fwrite(fir->coefficients, sizeof(double), fir->count, fp) != fir->count) {
        fclose(fp);
        return DSDPCM_ERR_FILE_WRITE;
    }

    fclose(fp);
    return DSDPCM_OK;
}

/* ==========================================================================
 * Public FIR I/O Functions (declared in dsdpcm.h)
 * ========================================================================== */

int dsdpcm_fir_load(dsdpcm_fir_t *fir, const char *filename)
{
    int is_binary;

    if (!fir || !filename) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    /* Auto-detect format */
    is_binary = dsdpcm_fir_is_binary(filename);
    if (is_binary < 0) {
        return is_binary; /* Error code */
    }

    if (is_binary) {
        return dsdpcm_fir_load_binary(fir, filename);
    } else {
        return dsdpcm_fir_load_text(fir, filename);
    }
}

int dsdpcm_fir_save(const dsdpcm_fir_t *fir, const char *filename, int binary)
{
    if (!fir || !filename) {
        return DSDPCM_ERR_NULL_POINTER;
    }

    if (binary) {
        return dsdpcm_fir_save_binary(fir, filename);
    } else {
        return dsdpcm_fir_save_text(fir, filename);
    }
}
