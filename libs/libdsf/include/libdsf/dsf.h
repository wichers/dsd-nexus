/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSF file API - Main public interface
 * This module provides the main public API for reading, writing, and modifying
 * DSF audio files. DSF is a simpler format than DSDIFF, consisting of:
 * - DSD chunk (file header)
 * - fmt chunk (format information)
 * - data chunk (DSD audio data)
 * - Optional ID3v2 metadata chunk
 * Features:
 * - File lifecycle operations (create, open, close, finalize)
 * - Audio data I/O (DSD format only - DSF doesn't support DST compression)
 * - Metadata access (ID3v2 tags)
 * - Format properties (sample rate, channel count, file size)
 * The API supports streaming and random-access operations.
 * References:
 * - DSF_file_format_specification_E.pdf
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

#ifndef LIBDSF_DSF_H
#define LIBDSF_DSF_H

#include <stddef.h>
#include <stdint.h>
#include <libdsf/dsf_export.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Enumerations
 * ===========================================================================*/

/**
 * @brief Format ID (from fmt chunk, byte 12-15)
 * Only DSD raw is currently supported
 */
typedef enum {
    DSF_FORMAT_DSD_RAW = 0  /**< DSD raw (uncompressed) */
} dsf_format_id_t;

/**
 * @brief Channel type (from fmt chunk, byte 16-19)
 * Maps number of channels to standard channel configurations
 */
typedef enum {
    DSF_CHANNEL_TYPE_MONO        = 1,  /**< 1 channel: mono */
    DSF_CHANNEL_TYPE_STEREO      = 2,  /**< 2 channels: stereo */
    DSF_CHANNEL_TYPE_3_CHANNELS  = 3,  /**< 3 channels */
    DSF_CHANNEL_TYPE_QUAD        = 4,  /**< 4 channels: quad */
    DSF_CHANNEL_TYPE_4_CHANNELS  = 5,  /**< 4 channels (alternate) */
    DSF_CHANNEL_TYPE_5_CHANNELS  = 6,  /**< 5 channels */
    DSF_CHANNEL_TYPE_5_1_CHANNELS = 7  /**< 5.1 channels (6 channels total) */
} dsf_channel_type_t;

/**
 * @brief Bits per sample (from fmt chunk, byte 28-31)
 */
typedef enum {
    DSF_BITS_PER_SAMPLE_1 = 1,  /**< 1 bit per sample (DSD) */
    DSF_BITS_PER_SAMPLE_8 = 8   /**< 8 bits per sample (converted) */
} dsf_bits_per_sample_t;

/**
 * @brief Seek direction
 */
typedef enum {
    DSF_SEEK_SET = 0,  /**< Seek from beginning */
    DSF_SEEK_CUR = 1,  /**< Seek from current position */
    DSF_SEEK_END = 2   /**< Seek from end */
} dsf_seek_dir_t;

/**
 * @brief File open mode
 */
typedef enum {
    DSF_FILE_MODE_CLOSED = 0,  /**< File is closed */
    DSF_FILE_MODE_READ   = 1,  /**< File open for reading */
    DSF_FILE_MODE_WRITE  = 2,  /**< File open for writing */
    DSF_FILE_MODE_MODIFY = 3   /**< File open for modification (metadata) */
} dsf_file_mode_t;

/** Maximum supported channels (per spec) */
#define DSF_MAX_CHANNELS 7

/* Standard DSD sample rates (in Hz) */
#define DSF_SAMPLE_FREQ_1FS     44100      /* 44.1 kHz base */
#define DSF_SAMPLE_FREQ_64FS    2822400    /* 64 * 44.1 kHz = 2.8224 MHz (DSD64) */
#define DSF_SAMPLE_FREQ_128FS   5644800    /* 128 * 44.1 kHz = 5.6448 MHz (DSD128) */
#define DSF_SAMPLE_FREQ_256FS   11289600   /* 256 * 44.1 kHz = 11.2896 MHz (DSD256) */
#define DSF_SAMPLE_FREQ_512FS   22579200   /* 512 * 44.1 kHz = 22.5792 MHz (DSD512) */

/** Block size per channel (fixed at 4096 bytes) */
#define DSF_BLOCK_SIZE_PER_CHANNEL 4096

/* =============================================================================
 * Error Codes
 * ===========================================================================*/

/**
 * @brief DSF error codes
 *
 * All functions return 0 on success, or a negative error code on failure.
 */
typedef enum {
    DSF_SUCCESS = 0,                    /**< Success */

    /* File state errors */
    DSF_ERROR_ALREADY_OPEN = -1,        /**< File already open */
    DSF_ERROR_NOT_OPEN = -2,            /**< File not open */
    DSF_ERROR_OPEN_READ = -3,           /**< File is open for reading */
    DSF_ERROR_OPEN_WRITE = -4,          /**< File is open for writing */

    /* File format errors */
    DSF_ERROR_INVALID_FILE = -10,       /**< Invalid DSF file */
    DSF_ERROR_INVALID_CHUNK = -11,      /**< Invalid chunk structure */
    DSF_ERROR_INVALID_DSF = -12,        /**< Invalid DSF format */
    DSF_ERROR_INVALID_VERSION = -13,    /**< Invalid DSF version */
    DSF_ERROR_UNSUPPORTED_COMPRESSION = -14,  /**< Unsupported compression */
    DSF_ERROR_UNEXPECTED_EOF = -15,     /**< Unexpected end of file */

    /* I/O errors */
    DSF_ERROR_READ = -20,               /**< Read error */
    DSF_ERROR_WRITE = -21,              /**< Write error */
    DSF_ERROR_SEEK = -22,               /**< Seek error */
    DSF_ERROR_END_OF_DATA = -23,        /**< End of sound data reached */
    DSF_ERROR_MAX_FILE_SIZE = -24,      /**< Maximum file size exceeded */
    DSF_ERROR_FILE_NOT_FOUND = -25,     /**< File not found */
    DSF_ERROR_CANNOT_CREATE_FILE = -26, /**< Cannot create file */
    DSF_ERROR_CONVERSION_FAILED = -27,  /**< String conversion failed */
    DSF_ERROR_GENERIC = -28,            /**< Generic/unknown error */

    /* Data errors */
    DSF_ERROR_NO_CHANNEL_INFO = -30,    /**< No channel information */
    DSF_ERROR_INVALID_CHANNELS = -31,   /**< Invalid number of channels */
    DSF_ERROR_CHANNELS_INCORRECT = -32, /**< Channel identifiers incorrect */
    DSF_ERROR_INVALID_SAMPLE_RATE = -33, /**< Invalid sample rate */
    DSF_ERROR_INVALID_BIT_DEPTH = -34,  /**< Invalid bits per sample */
    DSF_ERROR_INVALID_BLOCK_SIZE = -35, /**< Invalid block size */

    /* Operation errors */
    DSF_ERROR_INVALID_ARG = -40,        /**< Invalid argument */
    DSF_ERROR_OUT_OF_MEMORY = -41,      /**< Out of memory */
    DSF_ERROR_BUFFER_TOO_SMALL = -42,   /**< Buffer too small */
    DSF_ERROR_INVALID_MODE = -43,       /**< Invalid file mode */
    DSF_ERROR_OPERATION_NOT_ALLOWED = -44,  /**< Operation not allowed in current state */

    /* Metadata errors */
    DSF_ERROR_NO_METADATA = -50,        /**< No metadata block */
    DSF_ERROR_INVALID_METADATA = -51,   /**< Invalid metadata */

    /* Alias for compatibility */
    DSF_ERROR_INVALID_PARAMETER = DSF_ERROR_INVALID_ARG,
} dsf_error_t;

/* =============================================================================
 * Helper Macros
 * ===========================================================================*/

/**
 * @brief Check if error code indicates success
 */
#define DSF_IS_SUCCESS(err) ((err) == DSF_SUCCESS)

/**
 * @brief Check if error code indicates failure
 */
#define DSF_IS_ERROR(err) ((err) < 0)

/* =============================================================================
 * DSF File Information Structure
 * ===========================================================================*/

 /**
 * @brief DSF file information
 *
 * High-level information extracted from DSF file chunks.
 * This structure provides a convenient way to access file properties.
 */
typedef struct {
    /* From DSD Chunk */
    uint64_t file_size;         /**< Total file size in bytes */
    uint64_t metadata_offset;   /**< Offset to metadata chunk (0 if none) */

    /* From fmt Chunk */
    uint32_t format_version;    /**< DSF format version (1) */
    uint32_t format_id;         /**< Format ID (0 = DSD raw) */
    uint32_t channel_type;      /**< Channel type (1-7) */
    uint32_t channel_count;     /**< Number of channels (1-7) */
    uint32_t sampling_frequency;/**< Sampling frequency in Hz */
    uint32_t bits_per_sample;   /**< Bits per sample (1 or 8) */
    uint64_t sample_count;      /**< Total samples per channel */
    uint32_t block_size_per_channel; /**< Block size per channel (4096) */

    /* Derived information */
    uint64_t audio_data_size;   /**< Size of audio data in bytes */
    uint64_t audio_data_offset; /**< Offset to audio data in file */
    double duration_seconds;    /**< Duration in seconds */
    uint32_t bit_rate;          /**< Bit rate in bits per second */
} dsf_file_info_t;

/* =============================================================================
 * Opaque Handle Types
 * ===========================================================================*/

/**
 * @brief Opaque DSF file handle
 *
 * Internal structure is hidden from API users.
 * Create with dsf_create() or dsf_open().
 * Destroy with dsf_close().
 */
typedef struct dsf_s dsf_t;

/* =============================================================================
 * File Lifecycle Operations
 * ===========================================================================*/

/**
 * @brief Allocate DSF file handle
 *
 * Allocates memory for a DSF file handle. Must be paired with dsf_free().
 *
 * @param handle Pointer to receive file handle
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_alloc(dsf_t **handle);

/**
 * @brief Free DSF file handle
 *
 * Frees the memory allocated for a DSF file handle. The file should
 * already be closed with dsf_close() before calling this function.
 *
 * @param handle File handle to free
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_free(dsf_t *handle);

/**
 * @brief Create new DSF file for writing
 *
 * Creates a new DSF file and initializes it with basic properties.
 * The file is opened in write mode.
 *
 * @param handle Pointer to file handle (must be allocated with dsf_alloc)
 * @param filename File path (UTF-8 encoded)
 * @param sample_rate Sample frequency in Hz (e.g., 2822400 for DSD64)
 * @param channel_type Channel type (1-7)
 * @param channel_count Number of audio channels (1-7)
 * @param bits_per_sample Bits per sample (1 for DSD)
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_create(dsf_t *handle,
               const char *filename,
               uint32_t sample_rate,
               uint32_t channel_type,
               uint32_t channel_count,
               uint32_t bits_per_sample);

/**
 * @brief Open existing DSF file for reading
 *
 * Opens an existing DSF file and parses its structure.
 * The file is opened in read-only mode.
 *
 * @param handle Pointer to file handle (must be allocated with dsf_alloc)
 * @param filename File path (UTF-8 encoded)
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_open(dsf_t *handle,
             const char *filename);

/**
 * @brief Open existing DSF file for modification
 *
 * Opens an existing DSF file for reading and allows metadata modification.
 * Audio data cannot be modified, but ID3v2 metadata can be updated.
 *
 * @param handle Pointer to file handle (must be allocated with dsf_alloc)
 * @param filename File path (UTF-8 encoded)
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_open_modify(dsf_t *handle,
                    const char *filename);

/**
 * @brief Finalize DSF file
 *
 * Finalizes the file by updating chunk sizes and writing metadata.
 * Must be called before closing when writing a new file.
 *
 * @param handle File handle
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_finalize(dsf_t *handle);

/**
 * @brief Close DSF file
 *
 * Closes the file and frees internal resources. The handle remains valid
 * and can be reused with another dsf_open/dsf_create call, or freed with dsf_free().
 *
 * @param handle File handle
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_close(dsf_t *handle);

/**
 * @brief Remove DSF file
 *
 * Closes and deletes the file from disk.
 *
 * @param handle File handle
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_remove_file(dsf_t *handle);

/* =============================================================================
 * File Properties (Read-only)
 * ===========================================================================*/

/**
 * @brief Get file information
 *
 * Retrieves all file properties in a single structure.
 *
 * @param handle File handle
 * @param info Pointer to receive file information
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_file_info(dsf_t *handle, dsf_file_info_t *info);

/**
 * @brief Get file open mode
 *
 * @param handle File handle
 * @param mode Pointer to receive mode
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_file_mode(dsf_t *handle, dsf_file_mode_t *mode);

/**
 * @brief Get filename
 *
 * @param handle File handle
 * @param filename Buffer to receive filename (UTF-8)
 * @param buffer_size Size of buffer
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_filename(dsf_t *handle, char *filename, size_t buffer_size);

/**
 * @brief Get number of channels
 *
 * @param handle File handle
 * @param channel_count Pointer to receive number of channels
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_channel_count(dsf_t *handle, uint32_t *channel_count);

/**
 * @brief Get channel type
 *
 * @param handle File handle
 * @param channel_type Pointer to receive channel type (1-7)
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_channel_type(dsf_t *handle, uint32_t *channel_type);

/**
 * @brief Get bits per sample
 *
 * @param handle File handle
 * @param bits_per_sample Pointer to receive bits per sample (1 or 8)
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_bits_per_sample(dsf_t *handle, uint32_t *bits_per_sample);

/**
 * @brief Get sample frequency
 *
 * @param handle File handle
 * @param sample_rate Pointer to receive sample frequency in Hz
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_sample_rate(dsf_t *handle, uint32_t *sample_rate);

/**
 * @brief Get number of samples per channel
 *
 * @param handle File handle
 * @param sample_count Pointer to receive number of samples per channel
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_sample_count(dsf_t *handle, uint64_t *sample_count);

/**
 * @brief Get audio data size in bytes
 *
 * @param handle File handle
 * @param data_size Pointer to receive audio data size
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_audio_data_size(dsf_t *handle, uint64_t *data_size);

/**
 * @brief Get file size
 *
 * @param handle File handle
 * @param file_size Pointer to receive total file size
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_file_size(dsf_t *handle, uint64_t *file_size);

/**
 * @brief Get audio duration in seconds
 *
 * @param handle File handle
 * @param duration Pointer to receive duration
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_duration(dsf_t *handle, double *duration);

/* =============================================================================
 * Audio Data I/O
 * ===========================================================================*/

/**
 * @brief Read DSD audio data
 *
 * Reads DSD audio data from the file and returns it in DSDIFF
 * byte-interleaved format [L0][R0][L1][R1]... with MSB-first bit ordering.
 * The function handles conversion from DSF block-interleaved format internally.
 *
 * Data is buffered internally for efficient reading of arbitrary sizes.
 * Seeking invalidates the read buffer.
 *
 * @param handle File handle
 * @param buffer Buffer to receive byte-interleaved data
 * @param num_bytes Number of bytes to read
 * @param bytes_read Pointer to receive actual bytes read
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_read_audio_data(dsf_t *handle,
                        uint8_t *buffer,
                        size_t num_bytes,
                        size_t *bytes_read);

/**
 * @brief Write DSD audio data
 *
 * Writes DSD audio data to the file. Input data should be in DSDIFF
 * byte-interleaved format [L0][R0][L1][R1]... with MSB-first bit ordering.
 * The function handles conversion to DSF block-interleaved format internally.
 *
 * Data is buffered until complete 4096-byte blocks can be written.
 * Call dsf_flush_audio_data() or dsf_finalize() to write any remaining
 * partial blocks with zero padding.
 *
 * @param handle File handle
 * @param buffer Buffer containing DSDIFF byte-interleaved data
 * @param num_bytes Number of bytes to write (must be multiple of channel_count)
 * @param bytes_written Pointer to receive actual bytes written to file
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_write_audio_data(dsf_t *handle,
                         const uint8_t *buffer,
                         size_t num_bytes,
                         size_t *bytes_written);

/**
 * @brief Flush buffered audio data
 *
 * Writes any remaining buffered audio data with zero padding to complete
 * the final 4096-byte block per channel, as required by DSF specification.
 * This is automatically called by dsf_finalize(), but can be called
 * explicitly if needed.
 *
 * @param handle File handle
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_flush_audio_data(dsf_t *handle);

/**
 * @brief Seek to beginning of audio data
 *
 * Seeks to the start of the audio data chunk.
 *
 * @param handle File handle
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_seek_to_audio_start(dsf_t *handle);

/**
 * @brief Seek within audio data
 *
 * Seeks to a specific position within the audio data.
 *
 * @param handle File handle
 * @param byte_offset Offset in bytes from origin
 * @param origin Seek origin (DSF_SEEK_SET, DSF_SEEK_CUR, DSF_SEEK_END)
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_seek_audio_data(dsf_t *handle,
                        int64_t byte_offset,
                        dsf_seek_dir_t origin);

/**
 * @brief Get current position in audio data
 *
 * @param handle File handle
 * @param position Pointer to receive current position (bytes from start of audio data)
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_audio_position(dsf_t *handle, uint64_t *position);

/* =============================================================================
 * Metadata Operations (ID3v2)
 * ===========================================================================*/

/**
 * @brief Check if metadata is present
 *
 * @param handle File handle
 * @param has_metadata Pointer to receive result (1=yes, 0=no)
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_has_metadata(dsf_t *handle, int *has_metadata);

/**
 * @brief Get metadata size
 *
 * @param handle File handle
 * @param metadata_size Pointer to receive metadata size in bytes
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_get_metadata_size(dsf_t *handle, uint64_t *metadata_size);

/**
 * @brief Read metadata
 *
 * Reads the ID3v2 metadata chunk. The caller must free the buffer.
 *
 * @param handle File handle
 * @param buffer Pointer to receive allocated buffer
 * @param size Pointer to receive buffer size
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_read_metadata(dsf_t *handle, uint8_t **buffer, uint64_t *size);

/**
 * @brief Write metadata
 *
 * Writes ID3v2 metadata to the file. Can only be called when creating
 * a new file or modifying an existing file.
 *
 * @param handle File handle
 * @param buffer Metadata buffer
 * @param size Metadata size
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_write_metadata(dsf_t *handle, const uint8_t *buffer, uint64_t size);

/* =============================================================================
 * Utility Functions
 * ===========================================================================*/

/**
 * @brief Validate DSF file
 *
 * Validates the file structure and format parameters.
 *
 * @param handle File handle
 * @return 0 if valid, negative error code if invalid
 */
int DSF_API dsf_validate(dsf_t *handle);

/**
 * @brief Get error string
 *
 * Converts an error code to a human-readable string.
 *
 * @param error_code Error code
 * @return Error string (static, do not free)
 */
DSF_API const char *dsf_error_string(int error_code);

/* =============================================================================
 * Format Conversion Functions
 * ===========================================================================*/

/**
 * @brief Convert DSDIFF interleaved data to DSF block-interleaved format
 *
 * DSDIFF uses byte-interleaved format: [L0][R0][L1][R1]...
 * DSF uses block-interleaved format: [L0..L4095][R0..R4095]...
 *
 * Additionally, DSDIFF uses MSB-first bit ordering while DSF uses LSB-first,
 * so each byte is bit-reversed during conversion.
 *
 * @param dsdiff_data Source DSDIFF data (byte-interleaved, MSB-first)
 * @param dsf_data Destination DSF data buffer (block-interleaved, LSB-first)
 * @param dsdiff_size Size of source data in bytes (must be multiple of channel_count)
 * @param channel_count Number of audio channels
 * @param dsf_size Pointer to receive actual DSF output size (padded to block boundary)
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_convert_dsd_to_block_interleaved(const uint8_t *dsdiff_data,
                            uint8_t *dsf_data,
                            size_t dsdiff_size,
                            uint32_t channel_count,
                            size_t *dsf_size);

/**
 * @brief Convert DSF block-interleaved data to DSDIFF byte-interleaved format
 *
 * DSF uses block-interleaved format: [L0..L4095][R0..R4095]...
 * DSDIFF uses byte-interleaved format: [L0][R0][L1][R1]...
 *
 * Additionally, DSF uses LSB-first bit ordering while DSDIFF uses MSB-first,
 * so each byte is bit-reversed during conversion.
 *
 * @param dsf_data Source DSF data (block-interleaved, LSB-first)
 * @param dsdiff_data Destination DSDIFF data buffer (byte-interleaved, MSB-first)
 * @param dsf_size Size of source data in bytes (should be multiple of block_size * channel_count)
 * @param channel_count Number of audio channels
 * @param dsdiff_size Pointer to receive actual DSDIFF output size
 * @return 0 on success, negative error code on failure
 */
int DSF_API dsf_convert_dsd_to_byte_interleaved(const uint8_t *dsf_data,
                          uint8_t *dsdiff_data,
                          size_t dsf_size,
                          uint32_t channel_count,
                          size_t *dsdiff_size);

/**
 * @brief Convert sample rate to string representation
 */
static inline const char* dsf_sample_rate_to_string(uint32_t rate) {
    switch (rate) {
        case DSF_SAMPLE_FREQ_64FS:  return "2.8224 MHz (DSD64)";
        case DSF_SAMPLE_FREQ_128FS: return "5.6448 MHz (DSD128)";
        case DSF_SAMPLE_FREQ_256FS: return "11.2896 MHz (DSD256)";
        case DSF_SAMPLE_FREQ_512FS: return "22.5792 MHz (DSD512)";
        default: return "Unknown";
    }
}

/**
 * @brief Convert channel type to string representation
 */
static inline const char* dsf_channel_type_to_string(uint32_t type) {
    switch (type) {
        case DSF_CHANNEL_TYPE_MONO:        return "Mono";
        case DSF_CHANNEL_TYPE_STEREO:      return "Stereo";
        case DSF_CHANNEL_TYPE_3_CHANNELS:  return "3 Channels";
        case DSF_CHANNEL_TYPE_QUAD:        return "Quad";
        case DSF_CHANNEL_TYPE_4_CHANNELS:  return "4 Channels";
        case DSF_CHANNEL_TYPE_5_CHANNELS:  return "5 Channels";
        case DSF_CHANNEL_TYPE_5_1_CHANNELS: return "5.1 Channels";
        default: return "Unknown";
    }
}

/**
 * @brief Validate sample rate
 */
static inline int dsf_is_valid_sample_rate(uint32_t rate) {
    return (rate == DSF_SAMPLE_FREQ_64FS ||
            rate == DSF_SAMPLE_FREQ_128FS ||
            rate == DSF_SAMPLE_FREQ_256FS ||
            rate == DSF_SAMPLE_FREQ_512FS);
}

/**
 * @brief Validate channel type
 */
static inline int dsf_is_valid_channel_type(uint32_t type) {
    return (type >= DSF_CHANNEL_TYPE_MONO &&
            type <= DSF_CHANNEL_TYPE_5_1_CHANNELS);
}

/**
 * @brief Validate bits per sample
 */
static inline int dsf_is_valid_bits_per_sample(uint32_t bits) {
    return (bits == DSF_BITS_PER_SAMPLE_1 ||
            bits == DSF_BITS_PER_SAMPLE_8);
}

/**
 * @brief Safe uint64_t addition with overflow detection
 * @return 1 if overflow would occur, 0 if safe
 */
static inline int dsf_uint64_add_overflow(uint64_t a, uint64_t b, uint64_t *result) {
    if (a > UINT64_MAX - b) {
        return 1; /* Overflow */
    }
    *result = a + b;
    return 0;
}

/**
 * @brief Safe uint64_t multiplication with overflow detection
 * @return 1 if overflow would occur, 0 if safe
 */
static inline int dsf_uint64_mul_overflow(uint64_t a, uint64_t b, uint64_t *result) {
    if (a != 0 && b > UINT64_MAX / a) {
        return 1; /* Overflow */
    }
    *result = a * b;
    return 0;
}

/**
 * @brief Safe uint64_t subtraction with underflow detection
 * @return 1 if underflow would occur, 0 if safe
 */
static inline int dsf_uint64_sub_underflow(uint64_t a, uint64_t b, uint64_t *result) {
    if (b > a) {
        return 1; /* Underflow */
    }
    *result = a - b;
    return 0;
}

/**
 * @brief Safe cast from uint64_t to size_t with overflow detection
 * @return 1 if value doesn't fit in size_t, 0 if safe
 */
static inline int dsf_uint64_to_sizet(uint64_t value, size_t *result) {
    if (value > SIZE_MAX) {
        return 1; /* Doesn't fit */
    }
    *result = (size_t)value;
    return 0;
}

/**
 * @brief Calculate audio data size from format parameters
 */
static inline uint64_t dsf_calculate_audio_data_size(
    uint32_t channel_count,
    uint64_t sample_count,
    uint32_t bits_per_sample)
{
    if (bits_per_sample == 1) {
        /* DSD: samples are packed into bytes, then organized in 4096-byte blocks */
        uint64_t samples_per_byte = 8;
        uint64_t bytes_per_channel;
        uint64_t blocks;
        uint64_t block_bytes;
        uint64_t total_size;

        /* Safe calculation: (sample_count + 7) / 8 */
        if (sample_count > UINT64_MAX - 7) {
            /* Sample count too large */
            return 0;
        }
        bytes_per_channel = (sample_count + samples_per_byte - 1) / samples_per_byte;

        /* Round up to block boundary */
        if (bytes_per_channel > UINT64_MAX - DSF_BLOCK_SIZE_PER_CHANNEL + 1) {
            return 0;
        }
        blocks = (bytes_per_channel + DSF_BLOCK_SIZE_PER_CHANNEL - 1) /
                 DSF_BLOCK_SIZE_PER_CHANNEL;

        /* Safe multiplication: blocks * DSF_BLOCK_SIZE_PER_CHANNEL */
        if (dsf_uint64_mul_overflow(blocks, DSF_BLOCK_SIZE_PER_CHANNEL, &block_bytes)) {
            return 0;
        }

        /* Safe multiplication: block_bytes * channel_count */
        if (dsf_uint64_mul_overflow(block_bytes, channel_count, &total_size)) {
            return 0;
        }

        return total_size;
    } else {
        /* 8-bit: each sample is 1 byte */
        uint64_t total_size;
        if (dsf_uint64_mul_overflow(sample_count, channel_count, &total_size)) {
            return 0;
        }
        return total_size;
    }
}

/**
 * @brief Calculate duration in seconds
 */
static inline double dsf_calculate_duration(uint64_t sample_count, uint32_t sample_rate) {
    if (sample_rate == 0) return 0.0;
    return (double)sample_count / (double)sample_rate;
}

/**
 * @brief Calculate bit rate
 */
static inline uint32_t dsf_calculate_bit_rate(
    uint32_t channel_count,
    uint32_t sample_rate,
    uint32_t bits_per_sample)
{
    return channel_count * sample_rate * bits_per_sample;
}

#ifdef __cplusplus
}
#endif

#endif /* LIBDSF_DSF_H */
