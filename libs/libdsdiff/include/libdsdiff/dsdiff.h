/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSDIFF API - Main public interface
 * This module provides the main public API for reading and writing DSDIFF
 * audio files. It provides:
 * - File lifecycle operations (create, open, close, finalize)
 * - Audio data I/O (DSD and DST compressed formats)
 * - Metadata access (comments, markers, channel configuration)
 * - Format properties (sample rate, channel count, file size)
 * - DST frame-based operations with optional CRC and indexing
 * The API is designed for both streaming and random-access operations.
 * DST-compressed files support frame-based seeking when an index is present.
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

#ifndef LIBDSDIFF_DSDIFF_H
#define LIBDSDIFF_DSDIFF_H

#include <stdint.h>
#include <libdsdiff/dsdiff_export.h>
#include <stddef.h>  /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Forward Declarations
 * ===========================================================================*/

/** Opaque file handle type */
typedef struct dsdiff_s dsdiff_t;

/* =============================================================================
 * Error Codes
 * ===========================================================================*/

/**
 * @brief DSDIFF error codes
 *
 * All functions return 0 on success, or a negative error code on failure.
 */
typedef enum {
    DSDIFF_SUCCESS = 0,

    /* File state errors (-1 to -9) */
    DSDIFF_ERROR_ALREADY_OPEN     = -1,
    DSDIFF_ERROR_NOT_OPEN         = -2,
    DSDIFF_ERROR_MODE_READ_ONLY   = -3,
    DSDIFF_ERROR_MODE_WRITE_ONLY  = -4,

    /* File format errors (-10 to -19) */
    DSDIFF_ERROR_INVALID_FILE     = -10,
    DSDIFF_ERROR_INVALID_VERSION  = -11,
    DSDIFF_ERROR_UNSUPPORTED_COMPRESSION = -12,
    DSDIFF_ERROR_UNEXPECTED_EOF   = -13,
    DSDIFF_ERROR_INVALID_CHUNK    = -14,

    /* I/O errors (-20 to -29) */
    DSDIFF_ERROR_READ_FAILED      = -20,
    DSDIFF_ERROR_WRITE_FAILED     = -21,
    DSDIFF_ERROR_SEEK_FAILED      = -22,
    DSDIFF_ERROR_END_OF_DATA      = -23,
    DSDIFF_ERROR_MAX_FILE_SIZE    = -24,
    DSDIFF_ERROR_FILE_NOT_FOUND   = -25,
    DSDIFF_ERROR_FILE_CREATE_FAILED = -26,

    /* Missing metadata errors (-30 to -39) */
    DSDIFF_ERROR_NO_CHANNEL_INFO  = -30,
    DSDIFF_ERROR_NO_TIMECODE      = -31,
    DSDIFF_ERROR_NO_LSCONFIG      = -32,
    DSDIFF_ERROR_NO_COMMENT       = -33,
    DSDIFF_ERROR_NO_EMID          = -34,
    DSDIFF_ERROR_NO_ARTIST        = -35,
    DSDIFF_ERROR_NO_TITLE         = -36,
    DSDIFF_ERROR_NO_MARKER        = -37,
    DSDIFF_ERROR_NO_CRC           = -38,
    DSDIFF_ERROR_NO_MANUFACTURER  = -39,

    /* Validation errors (-40 to -49) */
    DSDIFF_ERROR_INVALID_ARG      = -40,
    DSDIFF_ERROR_OUT_OF_MEMORY    = -41,
    DSDIFF_ERROR_INVALID_CHANNELS = -42,
    DSDIFF_ERROR_INVALID_TIMECODE = -43,
    DSDIFF_ERROR_INVALID_MODE     = -44,
    DSDIFF_ERROR_BUFFER_TOO_SMALL = -45,

    /* Operation errors (-50 to -59) */
    DSDIFF_ERROR_POST_CREATE_FORBIDDEN  = -50,
    DSDIFF_ERROR_CHUNK_LOCKED           = -51,

    /* Format mismatch errors (-60 to -69) */
    DSDIFF_ERROR_REQUIRES_DSD     = -60,
    DSDIFF_ERROR_REQUIRES_DST     = -61,
    DSDIFF_ERROR_CRC_ALREADY_PRESENT = -62,
    DSDIFF_ERROR_NO_DST_INDEX     = -63,

    /* Per-track ID3 errors (-70 to -79) */
    DSDIFF_ERROR_TRACK_INDEX_INVALID = -70,
    DSDIFF_ERROR_NO_TRACK_ID3        = -71,

} dsdiff_error_t;

/* =============================================================================
 * Constants
 * ===========================================================================*/

/** DSD sample frequency: 1FS (44.1 kHz) */
#define DSDIFF_SAMPLE_FREQ_1FS (44100)

/** DSD sample frequency: 64FS (2.8224 MHz) */
#define DSDIFF_SAMPLE_FREQ_64FS (64 * DSDIFF_SAMPLE_FREQ_1FS)

/** DSD sample frequency: 128FS (5.6448 MHz) */
#define DSDIFF_SAMPLE_FREQ_128FS (128 * DSDIFF_SAMPLE_FREQ_1FS)

/** DSD sample frequency: 256FS (11.2896 MHz) */
#define DSDIFF_SAMPLE_FREQ_256FS (256 * DSDIFF_SAMPLE_FREQ_1FS)

/* =============================================================================
 * Basic Enumerations
 * ===========================================================================*/

/**
 * @brief Audio file type
 */
typedef enum {
    DSDIFF_AUDIO_DSD     = 0,  /**< Uncompressed DSD */
    DSDIFF_AUDIO_DST     = 1,  /**< DST compressed */
    DSDIFF_AUDIO_UNKNOWN = 2,  /**< Unknown format */

    /* Aliases for compatibility */
    DSDIFF_AUDIO_PCM_DSDIFF = DSDIFF_AUDIO_DSD,
    DSDIFF_AUDIO_DSDIFF_DST = DSDIFF_AUDIO_DST
} dsdiff_audio_type_t;

/**
 * @brief File open mode
 */
typedef enum {
    DSDIFF_FILE_MODE_CLOSED = 0,  /**< File is closed */
    DSDIFF_FILE_MODE_READ   = 1,  /**< File open for reading */
    DSDIFF_FILE_MODE_WRITE  = 2,  /**< File open for writing */
    DSDIFF_FILE_MODE_MODIFY = 3   /**< File open for modification (metadata) */
} dsdiff_file_mode_t;

/**
 * @brief Seek direction
 */
typedef enum {
    DSDIFF_SEEK_SET = 0,  /**< Seek from beginning */
    DSDIFF_SEEK_CUR = 1,  /**< Seek from current position */
    DSDIFF_SEEK_END = 2   /**< Seek from end */
} dsdiff_seek_dir_t;

/* =============================================================================
 * Channel Configuration Types
 * ===========================================================================*/

/**
 * @brief Loudspeaker configuration
 *
 * Defines standard loudspeaker configurations per ITU-R BS.775-1.
 */
typedef enum {
    DSDIFF_LS_CONFIG_STEREO  = 0,      /**< 2-channel stereo */
    DSDIFF_LS_CONFIG_MULTI5  = 3,      /**< 5-channel (per ITU) */
    DSDIFF_LS_CONFIG_MULTI6  = 4,      /**< 6-channel (5.1 configuration) */
    DSDIFF_LS_CONFIG_INVALID = 65535   /**< Undefined configuration */
} dsdiff_loudspeaker_config_t;

/* =============================================================================
 * Timecode Types
 * ===========================================================================*/

/**
 * @brief Timecode structure
 *
 * Represents absolute time with sample-accurate positioning.
 * Resolution is determined by the sample rate.
 */
typedef struct {
    uint16_t hours;    /**< Hours (0-23) */
    uint8_t  minutes;  /**< Minutes (0-59) */
    uint8_t  seconds;  /**< Seconds (0-59) */
    uint32_t samples;  /**< Sample offset within second (0 to sample_rate-1) */
} dsdiff_timecode_t;

/* =============================================================================
 * Marker Types
 * ===========================================================================*/

/**
 * @brief Marker types
 *
 * Defines marker types for track and program identification.
 * See DSDIFF specification section 3.11.2.
 */
typedef enum {
    DSDIFF_MARK_TRACK_START   = 0,  /**< Track start entry point */
    DSDIFF_MARK_TRACK_STOP    = 1,  /**< Track stop entry point */
    DSDIFF_MARK_PROGRAM_START = 2,  /**< Program start (2-channel or multi-channel area) */
    DSDIFF_MARK_INDEX         = 4   /**< Index entry point */
} dsdiff_mark_type_t;

/**
 * @brief Marker channel identification
 */
typedef enum {
    DSDIFF_MARK_CHANNEL_ALL = 0  /**< All channels */
    /* DSDIFF_MARK_CHANNEL_ALL + X = channel X (1-based) */
} dsdiff_mark_channel_t;

/**
 * @brief Track flags
 *
 * Flags for 5 or 6 channel Edited Master files.
 * Sound must be digital silence when using these flags.
 */
typedef enum {
    DSDIFF_TRACK_FLAG_NONE      = 0x000000,  /**< No flags */
    DSDIFF_TRACK_FLAG_TMF4_MUTE = 0x000001,  /**< 4th channel muted (6-channel only) */
    DSDIFF_TRACK_FLAG_TMF1_MUTE = 0x000002,  /**< Channels 1-2 muted */
    DSDIFF_TRACK_FLAG_TMF2_MUTE = 0x000004,  /**< Last 2 channels muted */
    DSDIFF_TRACK_FLAG_TMF3_MUTE = 0x000008,  /**< 3rd channel muted */
    DSDIFF_TRACK_FLAG_LFE_MUTE  = 0x000001   /**< LFE mute (obsolete, same as TMF4) */
} dsdiff_track_flags_t;

/**
 * @brief DSD marker structure
 *
 * Markers with offset support for precise positioning.
 * See DSDIFF specification section 3.11.2.
 */
typedef struct {
    dsdiff_timecode_t     time;         /**< Marker time position */
    dsdiff_mark_type_t    mark_type;    /**< Type of marker */
    dsdiff_mark_channel_t mark_channel; /**< Channel identification */
    dsdiff_track_flags_t  track_flags;  /**< Track flags */
    int32_t               offset;       /**< Offset in samples */
    uint32_t              text_length;  /**< Marker text length (excluding null) */
    char                  *marker_text; /**< Marker description (dynamically allocated) */
} dsdiff_marker_t;

/* =============================================================================
 * Comment Types
 * ===========================================================================*/

/**
 * @brief Comment category types
 *
 * Defines the category of a comment.
 * See DSDIFF specification section 3.10.
 */
typedef enum {
    DSDIFF_COMMENT_TYPE_GENERAL      = 0,  /**< General comment */
    DSDIFF_COMMENT_TYPE_CHANNEL      = 1,  /**< Channel-specific comment */
    DSDIFF_COMMENT_TYPE_SOUND_SOURCE = 2,  /**< Sound source comment */
    DSDIFF_COMMENT_TYPE_FILE_HISTORY = 3   /**< File history comment */
} dsdiff_comment_type_t;

/**
 * @brief Sound source reference types
 *
 * Reference values for SOUND_SOURCE comment types.
 */
typedef enum {
    DSDIFF_SOURCE_DSD_RECORDING    = 0,  /**< Source is a DSD recording */
    DSDIFF_SOURCE_ANALOG_RECORDING = 1,  /**< Source is an analog recording */
    DSDIFF_SOURCE_PCM_RECORDING    = 2   /**< Source is a PCM recording */
} dsdiff_source_reference_t;

/**
 * @brief File history reference types
 *
 * Reference values for FILE_HISTORY comment types.
 */
typedef enum {
    DSDIFF_HISTORY_REMARK         = 0,  /**< General remark */
    DSDIFF_HISTORY_OPERATOR       = 1,  /**< Name of operator */
    DSDIFF_HISTORY_CREATE_MACHINE = 2,  /**< Name of creating machine */
    DSDIFF_HISTORY_PLACE_ZONE     = 3,  /**< Place or zone information */
    DSDIFF_HISTORY_REVISION       = 4   /**< Revision number */
} dsdiff_history_reference_t;

/**
 * @brief Comment reference types (combined for backwards compatibility)
 *
 * Reference values for SOUND_SOURCE and FILE_HISTORY comment types.
 */
typedef enum {
    /* Sound source references */
    DSDIFF_REF_SOURCE_DSD_RECORDING    = 0,  /**< Source is a DSD recording */
    DSDIFF_REF_SOURCE_ANALOG_RECORDING = 1,  /**< Source is an analog recording */
    DSDIFF_REF_SOURCE_PCM_RECORDING    = 2,  /**< Source is a PCM recording */

    /* File history references */
    DSDIFF_REF_HISTORY_REMARK         = 0,  /**< General remark */
    DSDIFF_REF_HISTORY_OPERATOR       = 1,  /**< Name of operator */
    DSDIFF_REF_HISTORY_CREATE_MACHINE = 2,  /**< Name of creating machine */
    DSDIFF_REF_HISTORY_PLACE_ZONE     = 3,  /**< Place or zone information */
    DSDIFF_REF_HISTORY_REVISION       = 4   /**< Revision number */
} dsdiff_comment_reference_t;

/**
 * @brief Comment structure
 *
 * Comments with timestamp and categorization.
 * See DSDIFF specification section 3.10.
 */
typedef struct {
    uint16_t year;         /**< Creation year (0-65535) */
    uint8_t  month;        /**< Creation month (0-12) */
    uint8_t  day;          /**< Creation day (0-31) */
    uint8_t  hour;         /**< Creation hour (0-23) */
    uint8_t  minute;       /**< Creation minute (0-59) */
    uint16_t comment_type; /**< Comment type (see dsdiff_comment_type_t) */
    uint16_t comment_ref;  /**< Comment reference (see dsdiff_comment_reference_t) */
    uint32_t text_length;  /**< Text length in bytes (excluding null terminator) */
    char     *text;        /**< Comment text (dynamically allocated) */
} dsdiff_comment_t;

/* =============================================================================
 * Manufacturer Types
 * ===========================================================================*/

/**
 * @brief Manufacturer specific data structure
 *
 * Contains manufacturer identification and custom data.
 * See DSDIFF specification section 3.12.
 */
typedef struct {
    uint8_t  man_id[4];    /**< Manufacturer ID (4 characters) */
    uint32_t data_size;    /**< Size of manufacturer data in bytes */
    uint8_t  *data;        /**< Manufacturer specific data (dynamically allocated) */
} dsdiff_manufacturer_t;


/* =============================================================================
 * File Lifecycle Operations
 * ===========================================================================*/

int DSDIFF_API dsdiff_new(dsdiff_t **handle);

/**
 * @brief Create new DSDIFF file for writing
 *
 * Creates a new DSDIFF file and initializes it with basic properties.
 * The file is opened in write mode.
 *
 * @param handle Pointer to receive file handle (allocated on success)
 * @param filename File path (UTF-8 encoded)
 * @param sample_rate Sample frequency in Hz (e.g., 2822400 for DSD64)
 * @param channel_count Number of audio channels (1-6)
 * @param channel_ids Array of channel IDs (length = channel_count)
 * @param compression_type Compression type (DSDIFF_COMPRESSION_DSD or DSDIFF_AUDIO_DST)
 * @param compression_name Compression name (e.g., "not compressed" or "DST")
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_create(dsdiff_t *handle,
                  const char *filename,
                  dsdiff_audio_type_t file_type,
                  uint16_t channel_count,
                  uint16_t sample_bits,
                  uint32_t sample_rate);
  /**
 * @brief Open existing DSDIFF file for reading
 *
 * Opens an existing DSDIFF file and parses its structure.
 * The file is opened in read-only mode.
 *
 * @param handle Pointer to receive file handle (allocated on success)
 * @param filename File path (UTF-8 encoded)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_open(dsdiff_t *handle, const char *filename);

 /**
 * @brief Open existing DSDIFF file for modification
 *
 * Opens an existing DSDIFF file for reading and allows metadata modification.
 * Audio data cannot be modified, but comments, markers, and other metadata
 * can be updated.
 *
 * @param handle Pointer to receive file handle (allocated on success)
 * @param filename File path (UTF-8 encoded)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_modify(dsdiff_t *handle, const char *filename);

/**
 * @brief Finalize DSDIFF file
 *
 * Finalizes the file by writing all pending metadata chunks (comments,
 * markers, DIIN hierarchy) and updating chunk sizes. Must be called before
 * closing when writing a new file.
 *
 * @param handle File handle
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_finalize(dsdiff_t *handle);

/**
 * @brief Close DSDIFF file
 *
 * Closes the file and frees all resources. The handle becomes invalid
 * after this call.
 *
 * @param handle File handle
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_close(dsdiff_t *handle);

/* =============================================================================
 * File Properties (Read-only)
 * ===========================================================================*/

/**
 * @brief Get file open mode
 *
 * @param handle File handle
 * @param mode Pointer to receive mode
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_open_mode(dsdiff_t *handle, dsdiff_file_mode_t *mode);

/**
 * @brief Get file type
 *
 * @param handle File handle
 * @param file_type Pointer to receive file type
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_audio_type(dsdiff_t *handle, dsdiff_audio_type_t *file_type);

/**
 * @brief Get filename
 *
 * @param handle File handle
 * @param filename Buffer to receive filename (UTF-8)
 * @param buffer_size Size of buffer
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_filename(dsdiff_t *handle, char *filename, size_t buffer_size);

/**
 * @brief Get number of channels
 *
 * @param handle File handle
 * @param channel_count Pointer to receive number of channels
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_channel_count(dsdiff_t *handle, uint16_t *channel_count);

/**
 * @brief Get sample bit depth (always 1 for DSD)
 *
 * @param handle File handle
 * @param sample_bits Pointer to receive sample bits (always 1)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_sample_bits(dsdiff_t *handle, uint16_t *sample_bits);

/**
 * @brief Get sample frequency
 *
 * @param handle File handle
 * @param sample_rate Pointer to receive sample frequency in Hz
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_sample_rate(dsdiff_t *handle, uint32_t *sample_rate);

/**
 * @brief Get number of sample frames
 *
 * @param handle File handle
 * @param sample_frame_count Pointer to receive number of sample frames
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_sample_frame_count(dsdiff_t *handle, uint64_t *sample_frame_count);

/**
 * @brief Get sound data size in bytes
 *
 * @param handle File handle
 * @param data_size Pointer to receive sound data size
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_dsd_data_size(dsdiff_t *handle, uint64_t *data_size);

/**
 * @brief Get format version
 *
 * @param handle File handle
 * @param major_version Pointer to receive high version byte
 * @param minor_version Pointer to receive low version byte
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_format_version(dsdiff_t *handle,
                               uint8_t *major_version,
                               uint8_t *minor_version);

/* =============================================================================
 * Audio Data I/O (DSD Format)
 * ===========================================================================*/

/**
 * @brief Read DSD sound data
 *
 * Reads raw DSD audio data from the file. Data is interleaved by channel.
 *
 * @param handle File handle
 * @param buffer Buffer to receive data
 * @param byte_count Number of bytes to read
 * @param bytes_read Pointer to receive actual bytes read (may be NULL)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_read_dsd_data(dsdiff_t *handle,
                         uint8_t *buffer,
                         uint32_t byte_count,
                         uint32_t *bytes_read);

/**
 * @brief Write DSD sound data
 *
 * Writes raw DSD audio data to the file. Data must be interleaved by channel.
 *
 * @param handle File handle
 * @param buffer Buffer containing data to write
 * @param byte_count Number of bytes to write
 * @param bytes_written Pointer to receive actual bytes written (may be NULL)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_write_dsd_data(dsdiff_t *handle,
                          const uint8_t *buffer,
                          uint32_t byte_count,
                          uint32_t *bytes_written);

/**
 * @brief Seek to beginning of DSD data
 *
 * Seeks to the start of the audio data chunk.
 *
 * @param handle File handle
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_seek_dsd_start(dsdiff_t *handle);

/**
 * @brief Skip sound data (relative seek forward)
 *
 * Skips forward by the specified number of sample frames.
 *
 * @param handle File handle
 * @param skip_count Number of sample frames to skip
 * @param skipped_count Pointer to receive actual frames skipped (may be NULL)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_skip_dsd_data(dsdiff_t *handle,
                         uint32_t skip_count,
                         uint32_t *skipped_count);

/**
 * @brief Seek sound data (signed offset, relative to origin)
 *
 * Seeks to a position using signed offset and origin (can go backward).
 *
 * @param handle File handle
 * @param frame_offset Signed offset in sample frames
 * @param origin Seek origin (DSDIFF_SEEK_SET, DSDIFF_SEEK_CUR, DSDIFF_SEEK_END)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_seek_dsd_data(dsdiff_t *handle,
                          int64_t frame_offset,
                          dsdiff_seek_dir_t origin);

/* =============================================================================
 * Audio Data I/O (DST Compressed Format)
 * ===========================================================================*/

/**
 * @brief Get number of DST frames
 *
 * @param handle File handle
 * @param frame_count Pointer to receive number of DST frames
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_dst_frame_count(dsdiff_t *handle, uint32_t *frame_count);

/**
 * @brief Set DST frame rate
 *
 * Sets the number of DST frames per second. Must be called before writing
 * DST data to a new file.
 *
 * @param handle File handle
 * @param frame_rate DST frame rate (typically 75)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_set_dst_frame_rate(dsdiff_t *handle, uint16_t frame_rate);

/**
 * @brief Get DST frame rate
 *
 * @param handle File handle
 * @param frame_rate Pointer to receive frame rate
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_dst_frame_rate(dsdiff_t *handle, uint16_t *frame_rate);

/**
 * @brief Get maximum DST frame size
 *
 * Returns the size of the largest DST frame in the file.
 *
 * @param handle File handle
 * @param frame_size Pointer to receive max frame size in bytes
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_dst_max_frame_size(dsdiff_t *handle, uint32_t *frame_size);

/**
 * @brief Check if CRC is present in file
 *
 * @param handle File handle
 * @param has_crc Pointer to receive result (1=yes, 0=no)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_has_dst_crc(dsdiff_t *handle, int *has_crc);

/**
 * @brief Get CRC size
 *
 * Returns the total size of CRC data in the file.
 *
 * @param handle File handle
 * @param crc_size Pointer to receive CRC size in bytes
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_dst_crc_size(dsdiff_t *handle, uint32_t *crc_size);

/**
 * @brief Write DST frame (without CRC)
 *
 * Writes a single DST frame to the file.
 *
 * @param handle File handle
 * @param dst_data Buffer containing DST frame data
 * @param frame_size Size of DST frame in bytes
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_write_dst_frame(dsdiff_t *handle,
                           const uint8_t *dst_data,
                           uint32_t frame_size);

/**
 * @brief Write DST frame with CRC
 *
 * Writes a single DST frame with CRC to the file.
 *
 * @param handle File handle
 * @param dst_data Buffer containing DST frame data
 * @param frame_size Size of DST frame in bytes
 * @param crc_data CRC data for the frame
 * @param crc_size Size of CRC data
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_write_dst_frame_with_crc(dsdiff_t *handle,
                                    const uint8_t *dst_data,
                                    uint32_t frame_size,
                                    const uint8_t *crc_data,
                                    uint32_t crc_size);

/**
 * @brief Read DST frame (without CRC)
 *
 * Reads a single DST frame from the file (sequential access).
 *
 * @param handle File handle
 * @param dst_data Buffer to receive DST frame data
 * @param max_frame_size Maximum buffer size
 * @param frame_size Pointer to receive actual frame size
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_read_dst_frame(dsdiff_t *handle,
                          uint8_t *dst_data,
                          uint32_t max_frame_size,
                          uint32_t *frame_size);

/**
 * @brief Read DST frame with CRC
 *
 * Reads a single DST frame with CRC from the file (sequential access).
 *
 * @param handle File handle
 * @param dst_data Buffer to receive DST frame data
 * @param max_frame_size Maximum buffer size for frame
 * @param frame_size Pointer to receive actual frame size
 * @param crc_data Buffer to receive CRC data
 * @param max_crc_size Maximum buffer size for CRC
 * @param crc_size Pointer to receive actual CRC size
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_read_dst_frame_with_crc(dsdiff_t *handle,
                                   uint8_t *dst_data,
                                   uint32_t max_frame_size,
                                   uint32_t *frame_size,
                                   uint8_t *crc_data,
                                   uint32_t max_crc_size,
                                   uint32_t *crc_size);

/**
 * @brief Check if file is indexed
 *
 * Checks if the file contains a DST index (DSTI chunk) for random access.
 *
 * @param handle File handle
 * @param has_index Pointer to receive result (1=yes, 0=no)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_has_dst_index(dsdiff_t *handle, int *has_index);

/**
 * @brief Go to DST frame by index
 *
 * Seeks to the specified DST frame using the index.
 *
 * @param handle File handle
 * @param frame_index Frame index number (0-based)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_seek_dst_frame(dsdiff_t *handle, uint32_t frame_index);

/**
 * @brief Read DST frame at index (without CRC)
 *
 * Reads a specific DST frame by index (random access).
 *
 * @param handle File handle
 * @param frame_index Frame index number (0-based)
 * @param dst_data Buffer to receive DST frame data
 * @param max_frame_size Maximum buffer size
 * @param frame_size Pointer to receive actual frame size
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_read_dst_frame_at_index(dsdiff_t *handle,
                                   uint32_t frame_index,
                                   uint8_t *dst_data,
                                   uint32_t max_frame_size,
                                   uint32_t *frame_size);

/**
 * @brief Read DST frame at index with CRC
 *
 * Reads a specific DST frame with CRC by index (random access).
 *
 * @param handle File handle
 * @param frame_index Frame index number (0-based)
 * @param dst_data Buffer to receive DST frame data
 * @param max_frame_size Maximum buffer size for frame
 * @param frame_size Pointer to receive actual frame size
 * @param crc_data Buffer to receive CRC data
 * @param max_crc_size Maximum buffer size for CRC
 * @param crc_size Pointer to receive actual CRC size
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_read_dst_frame_at_index_with_crc(dsdiff_t *handle,
                                            uint32_t frame_index,
                                            uint8_t *dst_data,
                                            uint32_t max_frame_size,
                                            uint32_t *frame_size,
                                            uint8_t *crc_data,
                                            uint32_t max_crc_size,
                                            uint32_t *crc_size);

/**
 * @brief Get DST frame size at index
 *
 * Returns the size of a specific DST frame without reading it.
 *
 * @param handle File handle
 * @param frame_index Frame index number (0-based)
 * @param frame_size Pointer to receive frame size in bytes
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_dst_frame_size(dsdiff_t *handle,
                              uint32_t frame_index,
                              uint32_t *frame_size);

/* =============================================================================
 * Loudspeaker Configuration
 * ===========================================================================*/

/**
 * @brief Check if loudspeaker config is present
 *
 * @param handle File handle
 * @param has_config Pointer to receive result (1=yes, 0=no)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_has_loudspeaker_config(dsdiff_t *handle, int *has_config);

/**
 * @brief Set loudspeaker configuration
 *
 * @param handle File handle
 * @param loudspeaker_config Loudspeaker configuration
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_set_loudspeaker_config(dsdiff_t *handle,
                                  dsdiff_loudspeaker_config_t loudspeaker_config);

/**
 * @brief Get loudspeaker configuration
 *
 * @param handle File handle
 * @param loudspeaker_config Pointer to receive loudspeaker configuration
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_loudspeaker_config(dsdiff_t *handle,
                                  dsdiff_loudspeaker_config_t *loudspeaker_config);

/* =============================================================================
 * Timecode
 * ===========================================================================*/

/**
 * @brief Get start timecode
 *
 * @param handle File handle
 * @param start_timecode Pointer to receive start timecode
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_start_timecode(dsdiff_t *handle,
                              dsdiff_timecode_t *start_timecode);

/**
 * @brief Set start timecode
 *
 * @param handle File handle
 * @param start_timecode Start timecode to set
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_set_start_timecode(dsdiff_t *handle,
                              const dsdiff_timecode_t *start_timecode);

/**
 * @brief Check if start timecode is present
 *
 * @param handle File handle
 * @param has_timecode Pointer to receive result (1=yes, 0=no)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_has_start_timecode(dsdiff_t *handle, int *has_timecode);

/* =============================================================================
 * Comments
 * ===========================================================================*/

/**
 * @brief Get number of comments
 *
 * @param handle File handle
 * @param comment_count Pointer to receive number of comments
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_comment_count(dsdiff_t *handle, int *comment_count);

/**
 * @brief Get comment by index
 *
 * Retrieves a comment at the specified index (0-based).
 * The caller is responsible for freeing comment->comment_text.
 *
 * @param handle File handle
 * @param comment_index Comment index (0-based)
 * @param comment Pointer to receive comment data
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_comment(dsdiff_t *handle,
                       int comment_index,
                       dsdiff_comment_t *comment);

/**
 * @brief Set/add comment
 *
 * Adds a new comment to the file or updates an existing one.
 *
 * @param handle File handle
 * @param comment Comment to add/update
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_add_comment(dsdiff_t *handle, const dsdiff_comment_t *comment);

/**
 * @brief Delete comment
 *
 * Removes a comment at the specified index (0-based).
 *
 * @param handle File handle
 * @param comment_index Comment index (0-based)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_delete_comment(dsdiff_t *handle, int comment_index);

/**
 * @brief Get ID3 tag data
 *
 * Retrieves the ID3 tag from the DSDIFF file. The returned pointer points
 * to internal memory and must not be freed by the caller.
 *
 * @param handle File handle
 * @param tag_data Pointer to receive ID3 tag data (do not free)
 * @param tag_size Pointer to receive ID3 tag size in bytes
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_id3_tag(dsdiff_t *handle,
                       uint8_t **tag_data, uint32_t *tag_size);

/**
 * @brief Set ID3 tag data
 *
 * Sets the ID3 tag for the DSDIFF file. The data is copied internally.
 *
 * @param handle File handle
 * @param tag_data ID3 tag data to set
 * @param tag_size Size of ID3 tag data in bytes
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_set_id3_tag(dsdiff_t *handle, const uint8_t *tag_data, uint32_t tag_size);

/* =============================================================================
 * Per-Track ID3 Tags (Edit Master Mode)
 * ===========================================================================*/

/**
 * @brief Get number of per-track ID3 tags
 *
 * Returns the count of per-track ID3 tags stored for Edit Master files.
 * Each track in an Edit Master file can have its own ID3 tag.
 *
 * @param handle File handle
 * @param count Pointer to receive the count
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_track_id3_count(dsdiff_t *handle, uint32_t *count);

/**
 * @brief Get per-track ID3 tag raw data
 *
 * Retrieves the raw ID3 tag data for a specific track. The caller must
 * free the returned data using sa_free() or free().
 *
 * @param handle File handle
 * @param track_index Track index (0-based)
 * @param tag_data Pointer to receive allocated tag data (caller must free)
 * @param tag_size Pointer to receive tag size in bytes
 * @return 0 on success, DSDIFF_ERROR_TRACK_INDEX_INVALID if index out of range,
 *         DSDIFF_ERROR_NO_TRACK_ID3 if no ID3 tag for this track
 */
int DSDIFF_API dsdiff_get_track_id3_tag(dsdiff_t *handle, uint32_t track_index,
                              uint8_t **tag_data, uint32_t *tag_size);

/**
 * @brief Set per-track ID3 tag raw data
 *
 * Associates raw ID3 tag data with a specific track (0-based index).
 * The library makes an internal copy of the data. Setting a track index
 * beyond the current count will grow the internal array as needed.
 *
 * @param handle File handle
 * @param track_index Track index (0-based)
 * @param tag_data Raw ID3 tag binary data
 * @param tag_size Size of tag data in bytes
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_set_track_id3_tag(dsdiff_t *handle, uint32_t track_index,
                              const uint8_t *tag_data, uint32_t tag_size);

/**
 * @brief Clear per-track ID3 tag
 *
 * Removes the ID3 tag data for a specific track. The track index remains
 * valid but will have no associated ID3 data.
 *
 * @param handle File handle
 * @param track_index Track index (0-based)
 * @return 0 on success, DSDIFF_ERROR_TRACK_INDEX_INVALID if index out of range
 */
int DSDIFF_API dsdiff_clear_track_id3_tag(dsdiff_t *handle, uint32_t track_index);

/* =============================================================================
 * Manufacturer Specific Data
 * ===========================================================================*/

/**
 * @brief Check if manufacturer data is present
 *
 * @param handle File handle
 * @param has_manufacturer Pointer to receive result (1=yes, 0=no)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_has_manufacturer(dsdiff_t *handle, int *has_manufacturer);

/**
 * @brief Get manufacturer data
 *
 * Retrieves the manufacturer-specific data. The returned data pointer
 * points to internal memory and must not be freed by the caller.
 *
 * @param handle File handle
 * @param manufacturer Pointer to receive manufacturer data
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_manufacturer(dsdiff_t *handle, dsdiff_manufacturer_t *manufacturer);

/**
 * @brief Set manufacturer data
 *
 * Sets the manufacturer-specific data. The data is copied internally.
 *
 * @param handle File handle
 * @param manufacturer Manufacturer data to set
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_set_manufacturer(dsdiff_t *handle, const dsdiff_manufacturer_t *manufacturer);

/* =============================================================================
 * DSD Markers (native DSDIFF markers)
 * ===========================================================================*/

/**
 * @brief Get number of DSD markers
 *
 * @param handle File handle
 * @param marker_count Pointer to receive number of markers
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_dsd_marker_count(dsdiff_t *handle, int *marker_count);

/**
 * @brief Get DSD marker by index
 *
 * Retrieves a DSD marker at the specified index (0-based).
 * The caller is responsible for freeing marker->marker_text.
 *
 * @param handle File handle
 * @param marker_index Marker index (0-based)
 * @param marker Pointer to receive marker data
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_dsd_marker(dsdiff_t *handle,
                          int marker_index,
                          dsdiff_marker_t *marker);

/**
 * @brief Set/add DSD marker
 *
 * Adds a new DSD marker to the file.
 *
 * @param handle File handle
 * @param marker Marker to add
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_add_dsd_marker(dsdiff_t *handle, const dsdiff_marker_t *marker);

/**
 * @brief Delete DSD marker
 *
 * Removes a DSD marker at the specified index (0-based).
 *
 * @param handle File handle
 * @param marker_index Marker index (0-based)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_delete_dsd_marker(dsdiff_t *handle, int marker_index);

/**
 * @brief Sort DSD markers
 *
 * Sorts DSD markers by timestamp.
 *
 * @param handle File handle
 * @param sort_type Sort type (reserved, use 0)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_sort_dsd_markers(dsdiff_t *handle, int sort_type);

/* =============================================================================
 * Detailed Information (DIIN Hierarchy)
 * ===========================================================================*/

/**
 * @brief Check if EMID (Edition/Modification ID) is present
 *
 * @param handle File handle
 * @param has_emid Pointer to receive result (1=yes, 0=no)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_has_emid(dsdiff_t *handle, int *has_emid);

/**
 * @brief Get EMID (Edition/Modification ID)
 *
 * Retrieves the EMID string. The caller must allocate the buffer.
 *
 * @param handle File handle
 * @param length Pointer to receive string length
 * @param emid Buffer to receive EMID string (UTF-8)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_emid(dsdiff_t *handle,
                    uint32_t *length,
                    char *emid);

/**
 * @brief Set EMID (Edition/Modification ID)
 *
 * @param handle File handle
 * @param emid EMID string (UTF-8)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_set_emid(dsdiff_t *handle, const char *emid);

/**
 * @brief Check if disc artist is present
 *
 * @param handle File handle
 * @param has_artist Pointer to receive result (1=yes, 0=no)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_has_disc_artist(dsdiff_t *handle, int *has_artist);

/**
 * @brief Get disc artist
 *
 * Retrieves the disc artist string. The caller must allocate the buffer.
 *
 * @param handle File handle
 * @param length Pointer to receive string length
 * @param disc_artist Buffer to receive artist string (UTF-8)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_disc_artist(dsdiff_t *handle,
                           uint32_t *length,
                           char *disc_artist);

/**
 * @brief Set disc artist
 *
 * @param handle File handle
 * @param disc_artist Artist string (UTF-8)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_set_disc_artist(dsdiff_t *handle, const char *disc_artist);

/**
 * @brief Check if disc title is present
 *
 * @param handle File handle
 * @param has_title Pointer to receive result (1=yes, 0=no)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_has_disc_title(dsdiff_t *handle, int *has_title);

/**
 * @brief Get disc title
 *
 * Retrieves the disc title string. The caller must allocate the buffer.
 *
 * @param handle File handle
 * @param length Pointer to receive string length
 * @param disc_title Buffer to receive title string (UTF-8)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_get_disc_title(dsdiff_t *handle,
                          uint32_t *length,
                          char *disc_title);

/**
 * @brief Set disc title
 *
 * @param handle File handle
 * @param disc_title Title string (UTF-8)
 * @return 0 on success, negative error code on failure
 */
int DSDIFF_API dsdiff_set_disc_title(dsdiff_t *handle, const char *disc_title);

/* =============================================================================
 * Error Handling
 * ===========================================================================*/

/**
 * @brief Get human-readable error string
 *
 * Converts a DSDIFF error code to a descriptive string.
 *
 * @param error_code Error code returned by DSDIFF functions
 * @return Pointer to static string describing the error
 */
DSDIFF_API const char *dsdiff_error_string(int error_code);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDIFF_DSDIFF_H */
