/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief libdsdpcm - DSD to PCM conversion library (C API)
 * This library provides DSD-to-PCM conversion with multiple modes:
 * - Multistage conversion (best quality)
 * - Direct conversion (30kHz lowpass)
 * - User-defined FIR filter with configurable decimation
 * Supports both 32-bit float and 64-bit double precision.
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

#ifndef LIBDSDPCM_DSDPCM_H
#define LIBDSDPCM_DSDPCM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Version Information
 * ========================================================================== */

#define DSDPCM_VERSION_MAJOR 1
#define DSDPCM_VERSION_MINOR 0
#define DSDPCM_VERSION_PATCH 0

/* ==========================================================================
 * Export/Import Macros
 * ========================================================================== */

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef DSDPCM_BUILDING_DLL
        #define DSDPCM_API __declspec(dllexport)
    #elif defined(DSDPCM_USING_DLL)
        #define DSDPCM_API __declspec(dllimport)
    #else
        #define DSDPCM_API
    #endif
#else
    #if __GNUC__ >= 4
        #define DSDPCM_API __attribute__((visibility("default")))
    #else
        #define DSDPCM_API
    #endif
#endif

/* ==========================================================================
 * Audio Sample Types
 * ========================================================================== */

/**
 * @brief 32-bit float audio sample
 */
typedef float dsdpcm_sample32_t;

/**
 * @brief 64-bit double audio sample
 */
typedef double dsdpcm_sample64_t;

/**
 * @brief Platform-dependent audio sample type
 *
 * On 64-bit platforms (x64, arm64): double
 * On 32-bit platforms: float
 */
#if defined(_M_X64) || defined(_M_ARM64) || defined(__x86_64__) || defined(__aarch64__) || defined(__LP64__)
typedef double dsdpcm_sample_t;
#define DSDPCM_DEFAULT_FP64 1
#else
typedef float dsdpcm_sample_t;
#define DSDPCM_DEFAULT_FP64 0
#endif

/* ==========================================================================
 * Conversion Type Enumeration
 * ========================================================================== */

/**
 * @brief Conversion algorithm types
 */
typedef enum dsdpcm_conv_type_e {
    DSDPCM_CONV_UNKNOWN    = -1,  /**< Unknown/invalid type */
    DSDPCM_CONV_MULTISTAGE = 0,   /**< Multi-stage decimation (best quality) */
    DSDPCM_CONV_DIRECT     = 1,   /**< Direct conversion (30kHz lowpass) */
    DSDPCM_CONV_USER       = 2    /**< User-defined FIR filter */
} dsdpcm_conv_type_t;

/**
 * @brief Precision mode
 */
typedef enum dsdpcm_precision_e {
    DSDPCM_PRECISION_FP32 = 0,    /**< 32-bit float precision */
    DSDPCM_PRECISION_FP64 = 1     /**< 64-bit double precision */
} dsdpcm_precision_t;

/**
 * @brief FIR decimation factors for user-defined filters
 */
typedef enum dsdpcm_decimation_e {
    DSDPCM_DECIMATION_AUTO = 0,   /**< Auto-detect based on sample rates */
    DSDPCM_DECIMATION_8    = 8,
    DSDPCM_DECIMATION_16   = 16,
    DSDPCM_DECIMATION_32   = 32,
    DSDPCM_DECIMATION_64   = 64,
    DSDPCM_DECIMATION_128  = 128,
    DSDPCM_DECIMATION_256  = 256,
    DSDPCM_DECIMATION_512  = 512,
    DSDPCM_DECIMATION_1024 = 1024
} dsdpcm_decimation_t;

/* ==========================================================================
 * Error Codes
 * ========================================================================== */

/**
 * @brief Error codes returned by library functions
 */
typedef enum dsdpcm_error_e {
    DSDPCM_OK                    =  0,   /**< Success */
    DSDPCM_ERR_NULL_POINTER      = -1,   /**< Null pointer argument */
    DSDPCM_ERR_INVALID_PARAM     = -2,   /**< Invalid parameter value */
    DSDPCM_ERR_ALLOC_FAILED      = -3,   /**< Memory allocation failed */
    DSDPCM_ERR_NOT_INITIALIZED   = -4,   /**< Decoder not initialized */
    DSDPCM_ERR_UNSUPPORTED       = -5,   /**< Unsupported operation */
    DSDPCM_ERR_FIR_REQUIRED      = -6,   /**< FIR data required for USER mode */
    DSDPCM_ERR_PRECISION_MISMATCH = -7,  /**< Precision mismatch */
    DSDPCM_ERR_FILE_OPEN         = -10,  /**< File open failed */
    DSDPCM_ERR_FILE_READ         = -11,  /**< File read failed */
    DSDPCM_ERR_FILE_WRITE        = -12,  /**< File write failed */
    DSDPCM_ERR_FILE_FORMAT       = -13,  /**< Invalid file format */
    DSDPCM_ERR_BUFFER_TOO_SMALL  = -14   /**< Output buffer too small */
} dsdpcm_error_t;

/* ==========================================================================
 * Opaque Types
 * ========================================================================== */

/**
 * @brief Opaque decoder handle
 */
typedef struct dsdpcm_decoder_s dsdpcm_decoder_t;

/**
 * @brief FIR coefficient data structure
 */
typedef struct dsdpcm_fir_s {
    double              *coefficients;   /**< FIR filter coefficients (owned) */
    size_t               count;          /**< Number of coefficients */
    dsdpcm_decimation_t  decimation;     /**< Decimation factor */
    char                *name;           /**< Filter name (optional, owned) */
} dsdpcm_fir_t;

/* ==========================================================================
 * Decoder Lifecycle Functions
 * ========================================================================== */

/**
 * @brief Create a new DSD-to-PCM decoder instance
 *
 * @return Pointer to decoder, or NULL on allocation failure
 */
DSDPCM_API dsdpcm_decoder_t *dsdpcm_create(void);

/**
 * @brief Destroy a decoder instance and free all resources
 *
 * @param decoder Decoder instance (may be NULL)
 */
DSDPCM_API void dsdpcm_destroy(dsdpcm_decoder_t *decoder);

/* ==========================================================================
 * Initialization Functions
 * ========================================================================== */

/**
 * @brief Initialize decoder with multistage conversion (best quality)
 *
 * Uses multi-stage decimation with optimized FIR filters for
 * the highest quality conversion.
 *
 * @param decoder         Decoder instance
 * @param channels        Number of audio channels
 * @param framerate       Frame rate (e.g., 75 for SACD)
 * @param dsd_samplerate  DSD sample rate (e.g., 2822400 for DSD64)
 * @param pcm_samplerate  Target PCM sample rate (e.g., 44100, 88200, 176400)
 * @param precision       Floating point precision (FP32 or FP64)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_init_multistage(dsdpcm_decoder_t *decoder,
                                      size_t channels,
                                      size_t framerate,
                                      size_t dsd_samplerate,
                                      size_t pcm_samplerate,
                                      dsdpcm_precision_t precision);

/**
 * @brief Initialize decoder with direct conversion (30kHz lowpass)
 *
 * Uses a single powerful FIR filter with up to 64x decimation.
 * Applies a 30kHz lowpass filter.
 *
 * @param decoder         Decoder instance
 * @param channels        Number of audio channels
 * @param framerate       Frame rate (e.g., 75 for SACD)
 * @param dsd_samplerate  DSD sample rate
 * @param pcm_samplerate  Target PCM sample rate
 * @param precision       Floating point precision (FP32 or FP64)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_init_direct(dsdpcm_decoder_t *decoder,
                                  size_t channels,
                                  size_t framerate,
                                  size_t dsd_samplerate,
                                  size_t pcm_samplerate,
                                  dsdpcm_precision_t precision);

/**
 * @brief Initialize decoder with user-defined FIR filter
 *
 * Allows using custom FIR filter coefficients with configurable
 * decimation factor.
 *
 * @param decoder         Decoder instance
 * @param channels        Number of audio channels
 * @param framerate       Frame rate (e.g., 75 for SACD)
 * @param dsd_samplerate  DSD sample rate
 * @param pcm_samplerate  Target PCM sample rate
 * @param precision       Floating point precision (FP32 or FP64)
 * @param fir             FIR coefficient data (must remain valid during decoding)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_init_user_fir(dsdpcm_decoder_t *decoder,
                                    size_t channels,
                                    size_t framerate,
                                    size_t dsd_samplerate,
                                    size_t pcm_samplerate,
                                    dsdpcm_precision_t precision,
                                    const dsdpcm_fir_t *fir);

/**
 * @brief Generic initialization function
 *
 * @param decoder         Decoder instance
 * @param channels        Number of audio channels
 * @param framerate       Frame rate
 * @param dsd_samplerate  DSD sample rate
 * @param pcm_samplerate  Target PCM sample rate
 * @param conv_type       Conversion type
 * @param precision       Floating point precision
 * @param fir             FIR data (required for USER type, NULL otherwise)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_init(dsdpcm_decoder_t *decoder,
                           size_t channels,
                           size_t framerate,
                           size_t dsd_samplerate,
                           size_t pcm_samplerate,
                           dsdpcm_conv_type_t conv_type,
                           dsdpcm_precision_t precision,
                           const dsdpcm_fir_t *fir);

/**
 * @brief Free decoder internal resources without destroying
 *
 * The decoder can be re-initialized after calling this.
 *
 * @param decoder Decoder instance
 */
DSDPCM_API void dsdpcm_free(dsdpcm_decoder_t *decoder);

/* ==========================================================================
 * Query Functions
 * ========================================================================== */

/**
 * @brief Get filter delay in PCM samples
 *
 * @param decoder Decoder instance
 * @param delay   Pointer to receive delay value
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_get_delay(dsdpcm_decoder_t *decoder, double *delay);

/**
 * @brief Get current conversion type
 *
 * @param decoder   Decoder instance
 * @param conv_type Pointer to receive conversion type
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_get_conv_type(dsdpcm_decoder_t *decoder, dsdpcm_conv_type_t *conv_type);

/**
 * @brief Get current precision mode
 *
 * @param decoder   Decoder instance
 * @param precision Pointer to receive precision mode
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_get_precision(dsdpcm_decoder_t *decoder, dsdpcm_precision_t *precision);

/**
 * @brief Check if decoder is initialized
 *
 * @param decoder Decoder instance
 *
 * @return 1 if initialized, 0 otherwise
 */
DSDPCM_API int dsdpcm_is_initialized(dsdpcm_decoder_t *decoder);

/* ==========================================================================
 * Conversion Functions (Platform-dependent output type)
 * ========================================================================== */

/**
 * @brief Convert DSD data to PCM (platform-dependent precision)
 *
 * Output precision matches dsdpcm_sample_t (double on 64-bit, float on 32-bit).
 * The decoder must be initialized with matching precision.
 *
 * @param decoder     Decoder instance
 * @param dsd_data    Input DSD data (interleaved by channel)
 * @param dsd_size    Size of DSD data in bytes
 * @param pcm_data    Output PCM buffer (interleaved by channel)
 * @param pcm_samples Pointer to receive total samples written (all channels)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_convert(dsdpcm_decoder_t *decoder,
                              const uint8_t *dsd_data,
                              size_t dsd_size,
                              dsdpcm_sample_t *pcm_data,
                              size_t *pcm_samples);

/* ==========================================================================
 * Conversion Functions (Explicit precision)
 * ========================================================================== */

/**
 * @brief Convert DSD data to 32-bit float PCM
 *
 * The decoder must be initialized with DSDPCM_PRECISION_FP32.
 *
 * @param decoder     Decoder instance
 * @param dsd_data    Input DSD data (interleaved by channel)
 * @param dsd_size    Size of DSD data in bytes
 * @param pcm_data    Output PCM buffer (interleaved by channel)
 * @param pcm_samples Pointer to receive total samples written (all channels)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_convert_fp32(dsdpcm_decoder_t *decoder,
                                   const uint8_t *dsd_data,
                                   size_t dsd_size,
                                   dsdpcm_sample32_t *pcm_data,
                                   size_t *pcm_samples);

/**
 * @brief Convert DSD data to 64-bit double PCM
 *
 * The decoder must be initialized with DSDPCM_PRECISION_FP64.
 *
 * @param decoder     Decoder instance
 * @param dsd_data    Input DSD data (interleaved by channel)
 * @param dsd_size    Size of DSD data in bytes
 * @param pcm_data    Output PCM buffer (interleaved by channel)
 * @param pcm_samples Pointer to receive total samples written (all channels)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_convert_fp64(dsdpcm_decoder_t *decoder,
                                   const uint8_t *dsd_data,
                                   size_t dsd_size,
                                   dsdpcm_sample64_t *pcm_data,
                                   size_t *pcm_samples);

/* ==========================================================================
 * FIR Coefficient Management
 * ========================================================================== */

/**
 * @brief Create an empty FIR structure
 *
 * @return Pointer to FIR structure, or NULL on allocation failure
 */
DSDPCM_API dsdpcm_fir_t *dsdpcm_fir_create(void);

/**
 * @brief Destroy a FIR structure and free all resources
 *
 * @param fir FIR structure (may be NULL)
 */
DSDPCM_API void dsdpcm_fir_destroy(dsdpcm_fir_t *fir);

/**
 * @brief Set FIR coefficients
 *
 * @param fir          FIR structure
 * @param coefficients Array of coefficients (copied internally)
 * @param count        Number of coefficients
 * @param decimation   Decimation factor (AUTO to auto-detect)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_fir_set_coefficients(dsdpcm_fir_t *fir,
                                           const double *coefficients,
                                           size_t count,
                                           dsdpcm_decimation_t decimation);

/**
 * @brief Get FIR coefficients
 *
 * @param fir          FIR structure
 * @param coefficients Pointer to receive coefficient array pointer (do not free)
 * @param count        Pointer to receive coefficient count
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_fir_get_coefficients(const dsdpcm_fir_t *fir,
                                           const double **coefficients,
                                           size_t *count);

/**
 * @brief Set FIR filter name
 *
 * @param fir  FIR structure
 * @param name Filter name (copied internally, may be NULL)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_fir_set_name(dsdpcm_fir_t *fir, const char *name);

/**
 * @brief Get FIR filter name
 *
 * @param fir FIR structure
 *
 * @return Filter name or NULL if not set
 */
DSDPCM_API const char *dsdpcm_fir_get_name(const dsdpcm_fir_t *fir);

/**
 * @brief Set FIR decimation factor
 *
 * @param fir        FIR structure
 * @param decimation Decimation factor
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_fir_set_decimation(dsdpcm_fir_t *fir, dsdpcm_decimation_t decimation);

/**
 * @brief Get FIR decimation factor
 *
 * @param fir FIR structure
 *
 * @return Decimation factor, or DSDPCM_DECIMATION_AUTO if not set
 */
DSDPCM_API dsdpcm_decimation_t dsdpcm_fir_get_decimation(const dsdpcm_fir_t *fir);

/**
 * @brief Load FIR coefficients from file
 *
 * Supported formats:
 * - Text format (.txt): Comments with '#', one coefficient per line
 * - Binary format (.fir): Magic header + coefficient array
 *
 * The file format is auto-detected from content.
 *
 * @param fir      FIR structure to populate
 * @param filename File path (UTF-8 encoded)
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_fir_load(dsdpcm_fir_t *fir, const char *filename);

/**
 * @brief Save FIR coefficients to file
 *
 * @param fir      FIR structure
 * @param filename File path (UTF-8 encoded)
 * @param binary   Save in binary format if non-zero
 *
 * @return DSDPCM_OK on success, negative error code on failure
 */
DSDPCM_API int dsdpcm_fir_save(const dsdpcm_fir_t *fir, const char *filename, int binary);

/* ==========================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * @brief Get error string for error code
 *
 * @param error Error code
 *
 * @return Pointer to static string describing the error
 */
DSDPCM_API const char *dsdpcm_error_string(int error);

/**
 * @brief Get library version string
 *
 * @return Version string (e.g., "1.0.0")
 */
DSDPCM_API const char *dsdpcm_version_string(void);

/**
 * @brief Calculate required PCM buffer size
 *
 * @param channels       Number of channels
 * @param framerate      Frame rate
 * @param dsd_samplerate DSD sample rate
 * @param pcm_samplerate Target PCM sample rate
 * @param dsd_bytes      DSD input size in bytes
 *
 * @return Required PCM buffer size in samples (total, all channels)
 */
DSDPCM_API size_t dsdpcm_calc_pcm_buffer_size(size_t channels,
                                              size_t framerate,
                                              size_t dsd_samplerate,
                                              size_t pcm_samplerate,
                                              size_t dsd_bytes);

/**
 * @brief Check if a decimation factor is valid
 *
 * @param decimation Decimation factor to check
 *
 * @return 1 if valid, 0 otherwise
 */
DSDPCM_API int dsdpcm_decimation_is_valid(dsdpcm_decimation_t decimation);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDPCM_DSDPCM_H */
