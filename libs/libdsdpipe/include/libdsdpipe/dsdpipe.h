/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSD Audio Pipeline Library - Main Public API
 * libdsdpipe provides a simple pipeline for processing DSD audio with:
 * - Multiple source types (SACD ISO, DSDIFF, DSF)
 * - Multiple sink types (WAV, FLAC, DSDIFF, DSF)
 * - Transparent transformations (DST decoding, DSD-to-PCM conversion)
 * - Reference-counted buffer management for multi-sink scenarios
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

#ifndef LIBDSDPIPE_DSDPIPE_H
#define LIBDSDPIPE_DSDPIPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <libdsdpipe/dsdpipe_export.h>

/*============================================================================
 * Forward Declarations
 *============================================================================*/

/* Opaque type for flexible metadata tag storage (see metadata_tags.h) */
typedef struct metadata_tags_s metadata_tags_t;

/*============================================================================
 * Version Information
 *============================================================================*/

#include <libdsdpipe/version.h>

/*============================================================================
 * Error Codes
 *============================================================================*/

/**
 * @brief Error codes returned by libdsdpipe functions
 */
typedef enum dsdpipe_error_e {
    DSDPIPE_OK                     =  0,   /**< Success */
    DSDPIPE_ERROR_INVALID_ARG      = -1,   /**< Invalid argument */
    DSDPIPE_ERROR_OUT_OF_MEMORY    = -2,   /**< Memory allocation failed */
    DSDPIPE_ERROR_NOT_CONFIGURED   = -3,   /**< Pipeline not fully configured */
    DSDPIPE_ERROR_ALREADY_RUNNING  = -4,   /**< Pipeline already running */
    DSDPIPE_ERROR_SOURCE_OPEN      = -5,   /**< Failed to open source */
    DSDPIPE_ERROR_SINK_OPEN        = -6,   /**< Failed to open sink */
    DSDPIPE_ERROR_READ             = -7,   /**< Read error from source */
    DSDPIPE_ERROR_WRITE            = -8,   /**< Write error to sink */
    DSDPIPE_ERROR_DST_DECODE       = -9,   /**< DST decoding error */
    DSDPIPE_ERROR_PCM_CONVERT      = -10,  /**< DSD-to-PCM conversion error */
    DSDPIPE_ERROR_CANCELLED        = -11,  /**< Operation cancelled by user */
    DSDPIPE_ERROR_NO_SOURCE        = -12,  /**< No source configured */
    DSDPIPE_ERROR_NO_SINKS         = -13,  /**< No sinks configured */
    DSDPIPE_ERROR_TRACK_NOT_FOUND  = -14,  /**< Requested track not found */
    DSDPIPE_ERROR_UNSUPPORTED      = -15,  /**< Unsupported operation */
    DSDPIPE_ERROR_INTERNAL         = -16,  /**< Internal error */
    DSDPIPE_ERROR_FLAC_UNAVAILABLE = -17,  /**< FLAC support not compiled in */
    DSDPIPE_ERROR_INVALID_TRACK_SPEC = -18, /**< Invalid track specification string */
    DSDPIPE_ERROR_PATH_TOO_LONG    = -19,  /**< Output path exceeds maximum length */
    DSDPIPE_ERROR_FILE_CREATE      = -20,  /**< Failed to create output file */
    DSDPIPE_ERROR_FILE_WRITE       = -21,  /**< Failed to write to output file */
    DSDPIPE_ERROR_INVALID_STATE    = -22   /**< Invalid operation for current state */
} dsdpipe_error_t;

/*============================================================================
 * Audio Format Types
 *============================================================================*/

/**
 * @brief Audio data format types
 */
typedef enum dsdpipe_audio_format_e {
    DSDPIPE_FORMAT_UNKNOWN = 0,    /**< Unknown format */
    DSDPIPE_FORMAT_DSD_RAW,        /**< Raw DSD (byte-interleaved, MSB first) */
    DSDPIPE_FORMAT_DST,            /**< DST compressed DSD */
    DSDPIPE_FORMAT_PCM_INT16,      /**< 16-bit signed integer PCM */
    DSDPIPE_FORMAT_PCM_INT24,      /**< 24-bit signed integer PCM */
    DSDPIPE_FORMAT_PCM_INT32,      /**< 32-bit signed integer PCM */
    DSDPIPE_FORMAT_PCM_FLOAT32,    /**< 32-bit float PCM */
    DSDPIPE_FORMAT_PCM_FLOAT64     /**< 64-bit float PCM */
} dsdpipe_audio_format_t;

/**
 * @brief Source type enumeration
 */
typedef enum dsdpipe_source_type_e {
    DSDPIPE_SOURCE_NONE = 0,       /**< No source configured */
    DSDPIPE_SOURCE_SACD,           /**< SACD ISO image via libsacd */
    DSDPIPE_SOURCE_DSDIFF,         /**< DSDIFF file via libdsdiff */
    DSDPIPE_SOURCE_DSF             /**< DSF file via libdsf */
} dsdpipe_source_type_t;

/**
 * @brief Sink type enumeration
 */
typedef enum dsdpipe_sink_type_e {
    DSDPIPE_SINK_DSF = 0,          /**< Sony DSF format */
    DSDPIPE_SINK_DSDIFF,           /**< DSDIFF format (per-track files) */
    DSDPIPE_SINK_DSDIFF_EDIT,      /**< DSDIFF Edit Master (single file with markers) */
    DSDPIPE_SINK_WAV,              /**< PCM WAV format */
    DSDPIPE_SINK_FLAC,             /**< PCM FLAC format */
    DSDPIPE_SINK_PRINT,            /**< Human-readable text output */
    DSDPIPE_SINK_XML,              /**< XML metadata export */
    DSDPIPE_SINK_CUE,              /**< CUE sheet generation */
    DSDPIPE_SINK_ID3               /**< ID3v2.4 tag file */
} dsdpipe_sink_type_t;

/**
 * @brief Channel type for SACD source selection
 */
typedef enum dsdpipe_channel_type_e {
    DSDPIPE_CHANNEL_STEREO = 0,    /**< 2-channel stereo area */
    DSDPIPE_CHANNEL_MULTICHANNEL   /**< Multi-channel (surround) area */
} dsdpipe_channel_type_t;

/**
 * @brief DSD-to-PCM conversion quality setting
 */
typedef enum dsdpipe_pcm_quality_e {
    DSDPIPE_PCM_QUALITY_FAST = 0,  /**< Fast lookup-table conversion */
    DSDPIPE_PCM_QUALITY_NORMAL,    /**< Normal quality (multistage filter) */
    DSDPIPE_PCM_QUALITY_HIGH       /**< High quality (direct 64x filter) */
} dsdpipe_pcm_quality_t;

/**
 * @brief Track filename format options
 *
 * Controls how track filenames are generated from metadata.
 */
typedef enum dsdpipe_track_format_e {
    DSDPIPE_TRACK_NUM_ONLY = 0,    /**< Track number only: "01" */
    DSDPIPE_TRACK_NUM_TITLE,       /**< Number and title: "01 - Title" */
    DSDPIPE_TRACK_NUM_ARTIST_TITLE /**< Number, artist, title: "01 - Artist - Title" */
} dsdpipe_track_format_t;

/*============================================================================
 * Audio Format Descriptor
 *============================================================================*/

/**
 * @brief Audio format descriptor
 */
typedef struct dsdpipe_format_s {
    dsdpipe_audio_format_t type;   /**< Audio data format */
    uint32_t sample_rate;           /**< Sample rate in Hz */
    uint16_t channel_count;         /**< Number of channels (1-6) */
    uint16_t bits_per_sample;       /**< Bits per sample (for PCM) */
    uint32_t frame_rate;            /**< Frames per second (75 for SACD) */
} dsdpipe_format_t;

/*============================================================================
 * Metadata Structure
 *============================================================================*/

/**
 * @brief Metadata container for album and track information
 *
 * All string fields are dynamically allocated and must be freed
 * using dsdpipe_metadata_free().
 */
typedef struct dsdpipe_metadata_s {
    /* Album information */
    char *album_title;              /**< Album title */
    char *album_artist;             /**< Album artist */
    char *album_publisher;          /**< Publisher/label */
    char *album_copyright;          /**< Copyright notice */
    char *catalog_number;           /**< Catalog number */
    uint16_t year;                  /**< Release year (0 if unknown) */
    uint8_t month;                  /**< Release month (0 if unknown) */
    uint8_t day;                    /**< Release day (0 if unknown) */
    char *genre;                    /**< Genre string */

    /* Track information */
    char *track_title;              /**< Track title */
    char *track_performer;          /**< Track performer */
    char *track_composer;           /**< Track composer */
    char *track_arranger;           /**< Track arranger */
    char *track_songwriter;         /**< Track songwriter */
    char *track_message;            /**< Track comment/message */
    char isrc[13];                  /**< ISRC code (12 chars + null) */
    uint8_t track_number;           /**< Track number (1-based) */
    uint8_t track_total;            /**< Total tracks */

    /* Disc set information */
    uint16_t disc_number;           /**< Disc number in set */
    uint16_t disc_total;            /**< Total discs in set */

    /* Timing */
    uint32_t start_frame;           /**< Start position in SACD frames (1/75 sec) */
    uint32_t duration_frames;       /**< Duration in SACD frames (1/75 sec) */
    double duration_seconds;        /**< Duration in seconds */

    /* Flexible tag storage for arbitrary metadata (ID3 frames, custom fields) */
    metadata_tags_t *tags;          /**< Key-value tag storage (may be NULL) */
} dsdpipe_metadata_t;

/*============================================================================
 * Progress Information
 *============================================================================*/

/**
 * @brief Progress information passed to callback
 */
typedef struct dsdpipe_progress_s {
    uint8_t track_number;           /**< Current track number (1-based) */
    uint8_t track_total;            /**< Total tracks being processed */
    uint64_t frames_done;           /**< Frames processed in current track */
    uint64_t frames_total;          /**< Total frames in current track */
    uint64_t bytes_written;         /**< Total bytes written to sinks */
    float track_percent;            /**< Track progress (0.0 - 100.0) */
    float total_percent;            /**< Overall progress (0.0 - 100.0) */
    const char *track_title;        /**< Current track title (may be NULL) */
    const char *current_sink;       /**< Name of sink currently writing */
} dsdpipe_progress_t;

/**
 * @brief Progress callback function type
 *
 * @param progress Progress information
 * @param userdata User-provided context pointer
 * @return 0 to continue, non-zero to cancel the pipeline
 */
typedef int (*dsdpipe_progress_cb)(const dsdpipe_progress_t *progress,
                                    void *userdata);

/*============================================================================
 * Opaque Pipeline Handle
 *============================================================================*/

/**
 * @brief Opaque pipeline handle
 */
typedef struct dsdpipe_s dsdpipe_t;

/*============================================================================
 * Pipeline Lifecycle Functions
 *============================================================================*/

/**
 * @brief Create a new pipeline instance
 *
 * @return New pipeline handle, or NULL on allocation failure
 */
DSDPIPE_API dsdpipe_t *dsdpipe_create(void);

/**
 * @brief Destroy a pipeline instance and free all resources
 *
 * @param pipe Pipeline handle (may be NULL)
 */
void DSDPIPE_API dsdpipe_destroy(dsdpipe_t *pipe);

/**
 * @brief Reset pipeline to initial state (keeps source, clears sinks and tracks)
 *
 * Useful for processing multiple track selections from the same source.
 *
 * @param pipe Pipeline handle
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_reset(dsdpipe_t *pipe);

/*============================================================================
 * Source Configuration Functions
 *============================================================================*/

/**
 * @brief Set SACD ISO image as source
 *
 * @param pipe Pipeline handle
 * @param iso_path Path to SACD ISO file
 * @param channel_type Channel type to extract (stereo or multichannel)
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_set_source_sacd(dsdpipe_t *pipe,
                             const char *iso_path,
                             dsdpipe_channel_type_t channel_type);

/**
 * @brief Set DSDIFF file as source
 *
 * Supports both single-track files and Edit Master files with markers.
 *
 * @param pipe Pipeline handle
 * @param path Path to DSDIFF file
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_set_source_dsdiff(dsdpipe_t *pipe, const char *path);

/**
 * @brief Set DSF file as source
 *
 * @param pipe Pipeline handle
 * @param path Path to DSF file
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_set_source_dsf(dsdpipe_t *pipe, const char *path);

/**
 * @brief Get the currently configured source type
 *
 * @param pipe Pipeline handle
 * @return Source type, or DSDPIPE_SOURCE_NONE if not configured
 */
dsdpipe_source_type_t DSDPIPE_API dsdpipe_get_source_type(dsdpipe_t *pipe);

/**
 * @brief Get the audio format of the current source
 *
 * @param pipe Pipeline handle
 * @param format Output format descriptor
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_get_source_format(dsdpipe_t *pipe, dsdpipe_format_t *format);

/*============================================================================
 * Track Selection Functions
 *============================================================================*/

/**
 * @brief Get the number of tracks available from the source
 *
 * For DSDIFF Edit Master files, tracks are determined by markers.
 * For single-track DSF/DSDIFF files, returns 1.
 *
 * @param pipe Pipeline handle
 * @param count Output track count
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_get_track_count(dsdpipe_t *pipe, uint8_t *count);

/**
 * @brief Select specific tracks by number
 *
 * @param pipe Pipeline handle
 * @param track_numbers Array of 1-based track numbers
 * @param count Number of tracks in array
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_select_tracks(dsdpipe_t *pipe,
                           const uint8_t *track_numbers,
                           size_t count);

/**
 * @brief Select tracks using a specification string
 *
 * Supported formats:
 * - "all" - All tracks
 * - "1" - Single track
 * - "1,3,5" - Specific tracks
 * - "1-5" - Range of tracks
 * - "1-3,5,7-9" - Combination
 *
 * @param pipe Pipeline handle
 * @param selection Track selection string
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_select_tracks_str(dsdpipe_t *pipe, const char *selection);

/**
 * @brief Select all available tracks
 *
 * @param pipe Pipeline handle
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_select_all_tracks(dsdpipe_t *pipe);

/**
 * @brief Get the list of currently selected tracks
 *
 * @param pipe Pipeline handle
 * @param tracks Output array (caller-allocated)
 * @param max_count Maximum tracks to return
 * @param count Output actual count of selected tracks
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_get_selected_tracks(dsdpipe_t *pipe,
                                 uint8_t *tracks,
                                 size_t max_count,
                                 size_t *count);

/*============================================================================
 * Sink Configuration Functions
 *============================================================================*/

/**
 * @brief Add DSF output sink
 *
 * For multiple tracks, creates separate files with track numbers appended.
 *
 * @param pipe Pipeline handle
 * @param output_path Output path (base name for multiple tracks)
 * @param write_id3 Whether to write ID3v2 metadata tag
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_add_sink_dsf(dsdpipe_t *pipe,
                          const char *output_path,
                          bool write_id3);

/**
 * @brief Add DSDIFF output sink
 *
 * @param pipe Pipeline handle
 * @param output_path Output path
 * @param write_dst Keep DST compression if source is DST (false = decode to DSD)
 * @param edit_master Create Edit Master with markers (true) or per-track files (false)
 * @param write_id3 Write ID3v2 metadata tag
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_add_sink_dsdiff(dsdpipe_t *pipe,
                             const char *output_path,
                             bool write_dst,
                             bool edit_master,
                             bool write_id3);

/**
 * @brief Add WAV output sink (requires DSD-to-PCM conversion)
 *
 * @param pipe Pipeline handle
 * @param output_path Output path
 * @param bit_depth PCM bit depth (16, 24, or 32)
 * @param sample_rate Output sample rate in Hz (0 = auto, typically 88200 or 176400)
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_add_sink_wav(dsdpipe_t *pipe,
                          const char *output_path,
                          int bit_depth,
                          int sample_rate);

/**
 * @brief Add FLAC output sink (requires DSD-to-PCM conversion)
 *
 * @param pipe Pipeline handle
 * @param output_path Output path
 * @param bit_depth PCM bit depth (16 or 24)
 * @param compression FLAC compression level (0-8, default 5)
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_add_sink_flac(dsdpipe_t *pipe,
                           const char *output_path,
                           int bit_depth,
                           int compression);

/**
 * @brief Add a human-readable text metadata sink
 *
 * Outputs formatted text containing all metadata fields.
 * If output_path is NULL, writes to stdout.
 *
 * @param pipe Pipeline handle
 * @param output_path Output file path (or NULL for stdout)
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_add_sink_print(dsdpipe_t *pipe, const char *output_path);

/**
 * @brief Add an XML metadata sink
 *
 * Generates an XML file containing all album and track metadata.
 * Designed as a companion file for DSDIFF edit masters.
 *
 * @param pipe Pipeline handle
 * @param output_path Output XML file path
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_add_sink_xml(dsdpipe_t *pipe, const char *output_path);

/**
 * @brief Add a CUE sheet sink
 *
 * Generates a CUE sheet file containing track listings with timing.
 * Designed as a companion file for DSDIFF edit masters.
 *
 * @param pipe Pipeline handle
 * @param output_path Output CUE file path
 * @param audio_filename Referenced audio file name in CUE sheet
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_add_sink_cue(dsdpipe_t *pipe,
                          const char *output_path,
                          const char *audio_filename);

/**
 * @brief Add an ID3v2.4 tag sink
 *
 * Generates ID3v2.4 tag files that can be embedded in DSF/DSDIFF files.
 * In per_track mode, generates one .id3 file per track.
 *
 * @param pipe Pipeline handle
 * @param output_path Output path (file for single mode, directory for per-track)
 * @param per_track Generate per-track ID3 files
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_add_sink_id3(dsdpipe_t *pipe,
                          const char *output_path,
                          bool per_track);

/**
 * @brief Render ID3v2.4 tag to buffer
 *
 * Creates an ID3v2.4 tag from metadata that can be embedded directly
 * in DSF or DSDIFF files.
 *
 * @param metadata Source metadata
 * @param track_number Track number for the tag
 * @param out_data Output buffer (caller must free with free())
 * @param out_size Output size in bytes
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_id3_render(const dsdpipe_metadata_t *metadata,
                        uint8_t track_number,
                        uint8_t **out_data,
                        size_t *out_size);

/**
 * @brief Get the number of configured sinks
 *
 * @param pipe Pipeline handle
 * @return Number of configured sinks
 */
int DSDPIPE_API dsdpipe_get_sink_count(dsdpipe_t *pipe);

/**
 * @brief Clear all configured sinks
 *
 * @param pipe Pipeline handle
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_clear_sinks(dsdpipe_t *pipe);

/*============================================================================
 * Transformation Configuration
 *============================================================================*/

/**
 * @brief Set DSD-to-PCM conversion quality
 *
 * Only affects sinks that require PCM (WAV, FLAC).
 *
 * @param pipe Pipeline handle
 * @param quality Conversion quality setting
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_set_pcm_quality(dsdpipe_t *pipe, dsdpipe_pcm_quality_t quality);

/**
 * @brief Enable/disable double precision for DSD-to-PCM conversion
 *
 * @param pipe Pipeline handle
 * @param use_fp64 Use 64-bit float precision
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_set_pcm_use_fp64(dsdpipe_t *pipe, bool use_fp64);

/**
 * @brief Set track filename format for output sinks
 *
 * Controls how track filenames are generated from metadata.
 * Default is DSDPIPE_TRACK_NUM_TITLE.
 *
 * @param pipe Pipeline handle
 * @param format Track filename format
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_set_track_filename_format(dsdpipe_t *pipe,
                                       dsdpipe_track_format_t format);

/**
 * @brief Get current track filename format
 *
 * @param pipe Pipeline handle
 * @return Current track filename format
 */
dsdpipe_track_format_t DSDPIPE_API dsdpipe_get_track_filename_format(dsdpipe_t *pipe);

/*============================================================================
 * Progress and Execution
 *============================================================================*/

/**
 * @brief Set progress callback function
 *
 * @param pipe Pipeline handle
 * @param callback Callback function (NULL to disable)
 * @param userdata User-provided context pointer
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_set_progress_callback(dsdpipe_t *pipe,
                                   dsdpipe_progress_cb callback,
                                   void *userdata);

/**
 * @brief Run the pipeline synchronously
 *
 * Blocks until all tracks are processed or an error occurs.
 *
 * @param pipe Pipeline handle
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_run(dsdpipe_t *pipe);

/**
 * @brief Cancel a running pipeline
 *
 * Thread-safe. Can be called from progress callback or another thread.
 *
 * @param pipe Pipeline handle
 */
void DSDPIPE_API dsdpipe_cancel(dsdpipe_t *pipe);

/**
 * @brief Check if the pipeline was cancelled
 *
 * @param pipe Pipeline handle
 * @return true if cancelled, false otherwise
 */
bool DSDPIPE_API dsdpipe_is_cancelled(dsdpipe_t *pipe);

/*============================================================================
 * Error Handling
 *============================================================================*/

/**
 * @brief Get the last error message
 *
 * @param pipe Pipeline handle
 * @return Error message string (valid until next API call)
 */
DSDPIPE_API const char *dsdpipe_get_error_message(dsdpipe_t *pipe);

/**
 * @brief Convert error code to string
 *
 * @param error Error code
 * @return Static string describing the error
 */
DSDPIPE_API const char *dsdpipe_error_string(dsdpipe_error_t error);

/*============================================================================
 * Metadata Functions
 *============================================================================*/

/**
 * @brief Get album-level metadata from source
 *
 * @param pipe Pipeline handle
 * @param metadata Output metadata structure (caller must call dsdpipe_metadata_free)
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_get_album_metadata(dsdpipe_t *pipe, dsdpipe_metadata_t *metadata);

/**
 * @brief Get track-level metadata from source
 *
 * @param pipe Pipeline handle
 * @param track_number Track number (1-based)
 * @param metadata Output metadata structure (caller must call dsdpipe_metadata_free)
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_get_track_metadata(dsdpipe_t *pipe,
                                uint8_t track_number,
                                dsdpipe_metadata_t *metadata);

/**
 * @brief Initialize metadata structure to zero/NULL values
 *
 * @param metadata Metadata structure to initialize
 */
void DSDPIPE_API dsdpipe_metadata_init(dsdpipe_metadata_t *metadata);

/**
 * @brief Free dynamically allocated strings in metadata structure
 *
 * @param metadata Metadata structure to free
 */
void DSDPIPE_API dsdpipe_metadata_free(dsdpipe_metadata_t *metadata);

/**
 * @brief Copy metadata from source to destination
 *
 * Performs deep copy of all string fields.
 *
 * @param dest Destination metadata structure
 * @param src Source metadata structure
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_metadata_copy(dsdpipe_metadata_t *dest,
                           const dsdpipe_metadata_t *src);

/**
 * @brief Set an arbitrary metadata tag
 *
 * Tags are stored in a flexible key-value structure that can hold
 * any metadata field (ID3 frames, custom fields, etc.).
 *
 * @param metadata Metadata structure
 * @param key Tag key (e.g., "TIT2", "custom_field")
 * @param value Tag value (UTF-8 string)
 * @return DSDPIPE_OK on success, error code otherwise
 */
int DSDPIPE_API dsdpipe_metadata_set_tag(dsdpipe_metadata_t *metadata,
                              const char *key,
                              const char *value);

/**
 * @brief Get an arbitrary metadata tag
 *
 * @param metadata Metadata structure
 * @param key Tag key to look up
 * @return Tag value (do not free), or NULL if not found
 */
DSDPIPE_API const char *dsdpipe_metadata_get_tag(const dsdpipe_metadata_t *metadata,
                                                 const char *key);

/**
 * @brief Callback function for metadata tag enumeration
 */
typedef int (*dsdpipe_tag_callback_t)(void *ctx, const char *key, const char *value);

/**
 * @brief Enumerate all metadata tags
 *
 * Calls the callback for each tag in sorted order by key.
 *
 * @param metadata Metadata structure
 * @param ctx User-provided context passed to callback
 * @param callback Function to call for each tag
 */
void DSDPIPE_API dsdpipe_metadata_enumerate_tags(const dsdpipe_metadata_t *metadata,
                                      void *ctx,
                                      dsdpipe_tag_callback_t callback);

/**
 * @brief Get the number of metadata tags
 *
 * @param metadata Metadata structure
 * @return Number of tags
 */
size_t DSDPIPE_API dsdpipe_metadata_tag_count(const dsdpipe_metadata_t *metadata);

/**
 * @brief Generate a track filename from metadata
 *
 * Creates a sanitized filename suitable for filesystem use.
 * Does not include file extension.
 *
 * This function uses:
 * - Track title from metadata
 * - Track performer, falling back to album artist if not set
 *
 * @param[in] metadata  Metadata containing track and album information
 * @param[in] format    Track filename format option
 *
 * @return Newly allocated string with the track filename, or NULL on error.
 *         Caller must free with sa_free().
 *
 * @note Track numbers are zero-padded to 2 digits.
 * @note Characters invalid for filenames are replaced with underscores.
 * @note Leading/trailing spaces and dots are trimmed.
 *
 * Example outputs:
 * - DSDPIPE_TRACK_NUM_TITLE: "01 - Track Title"
 * - DSDPIPE_TRACK_NUM_ARTIST_TITLE: "01 - Artist Name - Track Title"
 * - DSDPIPE_TRACK_NUM_ONLY: "01"
 */
DSDPIPE_API char *dsdpipe_get_track_filename(const dsdpipe_metadata_t *metadata,
                                             dsdpipe_track_format_t format);

/**
 * @brief Get the best available artist from metadata
 *
 * Returns track performer if available, otherwise falls back to album artist.
 *
 * @param[in] metadata Metadata to search
 * @return Artist string (do not free), or NULL if no artist found
 */
DSDPIPE_API const char *dsdpipe_get_best_artist(const dsdpipe_metadata_t *metadata);

/**
 * @brief Album directory format options
 *
 * Controls whether artist is included in album directory name.
 */
typedef enum dsdpipe_album_format_e {
    DSDPIPE_ALBUM_TITLE_ONLY = 0,  /**< Title only: "Album Title" */
    DSDPIPE_ALBUM_ARTIST_TITLE     /**< Artist and title: "Artist - Album Title" */
} dsdpipe_album_format_t;

/**
 * @brief Generate an album directory name from metadata
 *
 * Creates a sanitized directory name for album output.
 * For multi-disc sets, appends " (disc N-M)" suffix.
 *
 * @param[in] metadata  Album metadata
 * @param[in] format    Album directory format option
 *
 * @return Newly allocated string with the directory name, or NULL on error.
 *         Caller must free with sa_free().
 *
 * @note Characters invalid for filenames are replaced with underscores.
 *
 * Example outputs:
 * - Single disc: "Artist - Album Title"
 * - Multi-disc:  "Artist - Album Title (disc 1-3)"
 * - Title only:  "Album Title"
 */
DSDPIPE_API char *dsdpipe_get_album_dir(const dsdpipe_metadata_t *metadata,
                                        dsdpipe_album_format_t format);

/**
 * @brief Generate an album path with optional disc subdirectory
 *
 * Creates a sanitized path for album output. For multi-disc sets,
 * adds a "Disc N" subdirectory component.
 *
 * @param[in] metadata  Album metadata
 * @param[in] format    Album directory format option
 *
 * @return Newly allocated string with the path, or NULL on error.
 *         Caller must free with sa_free().
 *
 * Example outputs:
 * - Single disc: "Artist - Album Title"
 * - Multi-disc:  "Artist - Album Title/Disc 1"
 */
DSDPIPE_API char *dsdpipe_get_album_path(const dsdpipe_metadata_t *metadata,
                                         dsdpipe_album_format_t format);

/**
 * @brief Get speaker configuration string from format
 *
 * Returns a human-readable string describing the speaker configuration.
 *
 * @param[in] format Audio format descriptor
 * @return Static string describing speaker config ("Stereo", "5ch", "6ch", etc.)
 */
DSDPIPE_API const char *dsdpipe_get_speaker_config_string(const dsdpipe_format_t *format);

/**
 * @brief Get frame format string from audio format
 *
 * Returns a human-readable string describing the frame format.
 *
 * @param[in] format Audio format descriptor
 * @return Static string describing frame format ("DSD", "DST", "PCM", etc.)
 */
DSDPIPE_API const char *dsdpipe_get_frame_format_string(const dsdpipe_format_t *format);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * @brief Check if FLAC sink support is available
 *
 * @return true if FLAC support was compiled in
 */
bool DSDPIPE_API dsdpipe_has_flac_support(void);

/**
 * @brief Get library version string
 *
 * @return Version string (e.g., "1.0.0")
 */
DSDPIPE_API const char *dsdpipe_version_string(void);

/**
 * @brief Get library version as integer
 *
 * @return Version as (major << 16 | minor << 8 | patch)
 */
int DSDPIPE_API dsdpipe_version_int(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDPIPE_DSDPIPE_H */
