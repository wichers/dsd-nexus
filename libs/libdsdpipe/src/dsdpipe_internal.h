/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Internal structures and functions for libdsdpipe
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

#ifndef LIBDSDPIPE_DSDPIPE_INTERNAL_H
#define LIBDSDPIPE_DSDPIPE_INTERNAL_H

#include <libdsdpipe/dsdpipe.h>
#include <libsautil/buffer.h>

#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

#define DSDPIPE_MAX_SINKS          8       /**< Maximum simultaneous sinks */
#define DSDPIPE_MAX_TRACKS         255     /**< Maximum tracks to select */
#define DSDPIPE_MAX_CHANNELS       6       /**< Maximum audio channels */
#define DSDPIPE_ERROR_MSG_SIZE     512     /**< Size of error message buffer */

#define DSDPIPE_DSD_FRAME_SIZE     4704    /**< DSD frame size (588 samples * 8 bits) */
#define DSDPIPE_MAX_DSD_SIZE       28224   /**< Max DSD data per frame (6ch * 4704) */
#define DSDPIPE_MAX_DST_SIZE       28224   /**< Max DST compressed frame size */
#define DSDPIPE_BUFFER_POOL_LIMIT  200     /**< Max buffers in pool (supports async reader) */

/*============================================================================
 * Buffer Flags
 *============================================================================*/

#define DSDPIPE_BUF_FLAG_TRACK_START   (1U << 0)   /**< First frame of track */
#define DSDPIPE_BUF_FLAG_TRACK_END     (1U << 1)   /**< Last frame of track */
#define DSDPIPE_BUF_FLAG_EOF           (1U << 2)   /**< End of file/source */
#define DSDPIPE_BUF_FLAG_DISCONTINUITY (1U << 3)   /**< Discontinuity in stream */

/*============================================================================
 * Internal Buffer Structure
 *============================================================================*/

/**
 * @brief Internal buffer wrapper around sa_buffer_pool_t
 */
typedef struct dsdpipe_buffer_s {
    sa_buffer_ref_t *ref;           /**< Underlying pool buffer reference */
    uint8_t *data;                  /**< Pointer to audio data (== ref->data) */
    size_t size;                    /**< Size of valid data */
    size_t capacity;                /**< Total buffer capacity */
    uint64_t frame_number;          /**< Frame number in source */
    uint64_t sample_offset;         /**< Sample offset from start */
    uint8_t track_number;           /**< Track number (1-based) */
    uint32_t flags;                 /**< DSDPIPE_BUF_FLAG_* */
    dsdpipe_format_t format;       /**< Audio format of this buffer */
} dsdpipe_buffer_t;

/*============================================================================
 * Track Selection State
 *============================================================================*/

/**
 * @brief Track selection state
 */
typedef struct dsdpipe_track_selection_s {
    uint8_t *tracks;                /**< Array of selected track numbers (1-based) */
    size_t count;                   /**< Number of selected tracks */
    size_t capacity;                /**< Allocated capacity */
    size_t current_idx;             /**< Current index in tracks array */
} dsdpipe_track_selection_t;

/*============================================================================
 * Source Interface (Virtual)
 *============================================================================*/

/**
 * @brief Source operations interface
 */
typedef struct dsdpipe_source_ops_s {
    /**
     * @brief Open the source
     * @param ctx Source context
     * @param path Path to source file
     * @return DSDPIPE_OK on success
     */
    int (*open)(void *ctx, const char *path);

    /**
     * @brief Close the source
     * @param ctx Source context
     */
    void (*close)(void *ctx);

    /**
     * @brief Get number of tracks
     * @param ctx Source context
     * @param count Output track count
     * @return DSDPIPE_OK on success
     */
    int (*get_track_count)(void *ctx, uint8_t *count);

    /**
     * @brief Get audio format
     * @param ctx Source context
     * @param format Output format
     * @return DSDPIPE_OK on success
     */
    int (*get_format)(void *ctx, dsdpipe_format_t *format);

    /**
     * @brief Seek to track
     * @param ctx Source context
     * @param track_number Track number (1-based)
     * @return DSDPIPE_OK on success
     */
    int (*seek_track)(void *ctx, uint8_t track_number);

    /**
     * @brief Read next frame
     * @param ctx Source context
     * @param buffer Output buffer
     * @return DSDPIPE_OK on success, or error code
     */
    int (*read_frame)(void *ctx, dsdpipe_buffer_t *buffer);

    /**
     * @brief Get album metadata
     * @param ctx Source context
     * @param metadata Output metadata
     * @return DSDPIPE_OK on success
     */
    int (*get_album_metadata)(void *ctx, dsdpipe_metadata_t *metadata);

    /**
     * @brief Get track metadata
     * @param ctx Source context
     * @param track_number Track number (1-based)
     * @param metadata Output metadata
     * @return DSDPIPE_OK on success
     */
    int (*get_track_metadata)(void *ctx, uint8_t track_number,
                              dsdpipe_metadata_t *metadata);

    /**
     * @brief Get total frames for track
     * @param ctx Source context
     * @param track_number Track number (1-based)
     * @param frames Output frame count
     * @return DSDPIPE_OK on success
     */
    int (*get_track_frames)(void *ctx, uint8_t track_number, uint64_t *frames);

    /**
     * @brief Destroy source context
     * @param ctx Source context
     */
    void (*destroy)(void *ctx);
} dsdpipe_source_ops_t;

/**
 * @brief Source instance
 */
typedef struct dsdpipe_source_s {
    dsdpipe_source_type_t type;    /**< Source type */
    const dsdpipe_source_ops_t *ops; /**< Operations */
    void *ctx;                      /**< Implementation context */
    dsdpipe_format_t format;       /**< Cached format */
    bool is_open;                   /**< Open state */
} dsdpipe_source_t;

/*============================================================================
 * Sink Interface (Virtual)
 *============================================================================*/

/**
 * @brief Sink capabilities flags
 */
typedef enum dsdpipe_sink_caps_e {
    DSDPIPE_SINK_CAP_DSD           = (1 << 0),   /**< Accepts DSD data */
    DSDPIPE_SINK_CAP_DST           = (1 << 1),   /**< Accepts DST data */
    DSDPIPE_SINK_CAP_PCM           = (1 << 2),   /**< Accepts PCM data */
    DSDPIPE_SINK_CAP_METADATA      = (1 << 3),   /**< Supports metadata */
    DSDPIPE_SINK_CAP_MARKERS       = (1 << 4),   /**< Supports track markers */
    DSDPIPE_SINK_CAP_MULTI_TRACK   = (1 << 5)    /**< Single file, multiple tracks */
} dsdpipe_sink_caps_t;

/**
 * @brief Sink operations interface
 */
typedef struct dsdpipe_sink_ops_s {
    /**
     * @brief Open sink for writing
     * @param ctx Sink context
     * @param path Output path
     * @param format Audio format
     * @param metadata Album metadata (may be NULL)
     * @return DSDPIPE_OK on success
     */
    int (*open)(void *ctx, const char *path, const dsdpipe_format_t *format,
                const dsdpipe_metadata_t *metadata);

    /**
     * @brief Close sink
     * @param ctx Sink context
     */
    void (*close)(void *ctx);

    /**
     * @brief Called at track start
     * @param ctx Sink context
     * @param track_number Track number (1-based)
     * @param metadata Track metadata (may be NULL)
     * @return DSDPIPE_OK on success
     */
    int (*track_start)(void *ctx, uint8_t track_number,
                       const dsdpipe_metadata_t *metadata);

    /**
     * @brief Called at track end
     * @param ctx Sink context
     * @param track_number Track number (1-based)
     * @return DSDPIPE_OK on success
     */
    int (*track_end)(void *ctx, uint8_t track_number);

    /**
     * @brief Write audio frame
     * @param ctx Sink context
     * @param buffer Buffer to write
     * @return DSDPIPE_OK on success
     */
    int (*write_frame)(void *ctx, const dsdpipe_buffer_t *buffer);

    /**
     * @brief Finalize output (flush, update headers)
     * @param ctx Sink context
     * @return DSDPIPE_OK on success
     */
    int (*finalize)(void *ctx);

    /**
     * @brief Get sink capabilities
     * @param ctx Sink context
     * @return Capability flags
     */
    uint32_t (*get_capabilities)(void *ctx);

    /**
     * @brief Destroy sink context
     * @param ctx Sink context
     */
    void (*destroy)(void *ctx);
} dsdpipe_sink_ops_t;

/**
 * @brief Sink configuration
 */
typedef struct dsdpipe_sink_config_s {
    dsdpipe_sink_type_t type;      /**< Sink type */
    char *path;                     /**< Output path */
    dsdpipe_track_format_t track_filename_format; /**< Track filename format */
    union {
        struct {
            bool write_id3;         /**< Write ID3 tag */
        } dsf;
        struct {
            bool write_dst;         /**< Keep DST compression */
            bool edit_master;       /**< Create edit master */
            bool write_id3;         /**< Write ID3 tag */
            uint8_t track_selection_count; /**< Track count for edit master renumbering */
        } dsdiff;
        struct {
            int bit_depth;          /**< PCM bit depth */
            int sample_rate;        /**< Output sample rate */
        } wav;
        struct {
            int bit_depth;          /**< PCM bit depth */
            int compression;        /**< FLAC compression level */
        } flac;
    } opts;
} dsdpipe_sink_config_t;

/**
 * @brief Sink instance
 */
typedef struct dsdpipe_sink_s {
    dsdpipe_sink_type_t type;      /**< Sink type */
    const dsdpipe_sink_ops_t *ops; /**< Operations */
    void *ctx;                      /**< Implementation context */
    dsdpipe_sink_config_t config;  /**< Configuration */
    uint32_t caps;                  /**< Cached capabilities */
    bool is_open;                   /**< Open state */
} dsdpipe_sink_t;

/*============================================================================
 * Transform Interface (Virtual)
 *============================================================================*/

/**
 * @brief Transform operations interface
 */
typedef struct dsdpipe_transform_ops_s {
    /**
     * @brief Initialize transform
     * @param ctx Transform context
     * @param input_format Input audio format
     * @param output_format Output format (filled by transform)
     * @return DSDPIPE_OK on success
     */
    int (*init)(void *ctx, const dsdpipe_format_t *input_format,
                dsdpipe_format_t *output_format);

    /**
     * @brief Process a frame
     * @param ctx Transform context
     * @param input Input buffer
     * @param output Output buffer
     * @return DSDPIPE_OK on success
     */
    int (*process)(void *ctx, const dsdpipe_buffer_t *input,
                   dsdpipe_buffer_t *output);

    /**
     * @brief Process multiple frames in parallel (batch API)
     * @param ctx Transform context
     * @param inputs Array of input data pointers
     * @param input_sizes Array of input sizes
     * @param outputs Array of output data pointers
     * @param output_sizes Array of output sizes (filled by transform)
     * @param count Number of frames
     * @return DSDPIPE_OK on success
     *
     * @note Optional - may be NULL if not supported
     */
    int (*process_batch)(void *ctx,
                         const uint8_t *inputs[], const size_t input_sizes[],
                         uint8_t *outputs[], size_t output_sizes[],
                         size_t count);

    /**
     * @brief Flush any pending output
     * @param ctx Transform context
     * @param output Output buffer (may be NULL if no pending data)
     * @return DSDPIPE_OK on success, or positive value if more data pending
     */
    int (*flush)(void *ctx, dsdpipe_buffer_t *output);

    /**
     * @brief Reset transform state
     * @param ctx Transform context
     */
    void (*reset)(void *ctx);

    /**
     * @brief Destroy transform context
     * @param ctx Transform context
     */
    void (*destroy)(void *ctx);
} dsdpipe_transform_ops_t;

/**
 * @brief Transform instance
 */
typedef struct dsdpipe_transform_s {
    const dsdpipe_transform_ops_t *ops; /**< Operations */
    void *ctx;                      /**< Implementation context */
    dsdpipe_format_t input_format; /**< Input format */
    dsdpipe_format_t output_format; /**< Output format */
    bool is_initialized;            /**< Init state */
} dsdpipe_transform_t;

/*============================================================================
 * Main Pipeline Structure
 *============================================================================*/

/**
 * @brief Pipeline state enumeration
 */
typedef enum dsdpipe_state_e {
    DSDPIPE_STATE_CREATED = 0,     /**< Just created */
    DSDPIPE_STATE_CONFIGURED,      /**< Configured with source and sinks */
    DSDPIPE_STATE_RUNNING,         /**< Currently running */
    DSDPIPE_STATE_FINISHED,        /**< Finished successfully */
    DSDPIPE_STATE_ERROR            /**< Finished with error */
} dsdpipe_state_t;

/**
 * @brief Main pipeline structure
 */
struct dsdpipe_s {
    /* State */
    dsdpipe_state_t state;         /**< Current state */
    atomic_bool cancelled; /**< Cancellation flag (thread-safe) */

    /* Error handling */
    dsdpipe_error_t last_error;    /**< Last error code */
    char error_message[DSDPIPE_ERROR_MSG_SIZE]; /**< Last error message */

    /* Source */
    dsdpipe_source_t source;       /**< Input source */

    /* Track selection */
    dsdpipe_track_selection_t tracks; /**< Selected tracks */

    /* Sinks */
    dsdpipe_sink_t *sinks[DSDPIPE_MAX_SINKS]; /**< Output sinks */
    int sink_count;                 /**< Number of configured sinks */

    /* Transforms */
    dsdpipe_transform_t *dst_decoder;   /**< DST decoder (auto-inserted) */
    dsdpipe_transform_t *dsd2pcm;       /**< DSD-to-PCM converter (auto-inserted) */

    /* Conversion settings */
    dsdpipe_pcm_quality_t pcm_quality;  /**< PCM conversion quality */
    bool pcm_use_fp64;              /**< Use double precision for PCM */

    /* Filename generation settings */
    dsdpipe_track_format_t track_filename_format;  /**< Track filename format */

    /* Buffer pools */
    sa_buffer_pool_t *dsd_pool;     /**< Pool for DSD/DST buffers */
    sa_buffer_pool_t *pcm_pool;     /**< Pool for PCM buffers */
    bool pools_initialized;         /**< Pool init state */

    /* Progress */
    dsdpipe_progress_cb progress_callback; /**< Progress callback */
    void *progress_userdata;        /**< Progress callback userdata */
    dsdpipe_progress_t progress;   /**< Current progress state */
};

/*============================================================================
 * Internal Helper Functions
 *============================================================================*/

/**
 * @brief Set error message
 */
void dsdpipe_set_error(dsdpipe_t *pipe, dsdpipe_error_t error,
                        const char *format, ...);

/**
 * @brief Initialize buffer pools
 */
int dsdpipe_init_pools(dsdpipe_t *pipe);

/**
 * @brief Free buffer pools
 */
void dsdpipe_free_pools(dsdpipe_t *pipe);

/**
 * @brief Allocate a buffer from the DSD pool
 */
dsdpipe_buffer_t *dsdpipe_buffer_alloc_dsd(dsdpipe_t *pipe);

/**
 * @brief Allocate a buffer from the PCM pool
 */
dsdpipe_buffer_t *dsdpipe_buffer_alloc_pcm(dsdpipe_t *pipe);

/**
 * @brief Decrement buffer reference count (returns to pool when zero)
 */
void dsdpipe_buffer_unref(dsdpipe_buffer_t *buffer);

/*============================================================================
 * Track Selection Functions
 *============================================================================*/

/**
 * @brief Initialize track selection
 */
int dsdpipe_track_selection_init(dsdpipe_track_selection_t *sel);

/**
 * @brief Free track selection
 */
void dsdpipe_track_selection_free(dsdpipe_track_selection_t *sel);

/**
 * @brief Clear track selection
 */
void dsdpipe_track_selection_clear(dsdpipe_track_selection_t *sel);

/**
 * @brief Add a track to selection
 */
int dsdpipe_track_selection_add(dsdpipe_track_selection_t *sel, uint8_t track);

/**
 * @brief Parse track selection string
 *
 * @param sel Track selection to populate
 * @param str Selection string ("all", "1,2,3", "1-5", "1-3,5,7-9")
 * @param max_track Maximum valid track number
 * @return DSDPIPE_OK on success
 */
int dsdpipe_track_selection_parse(dsdpipe_track_selection_t *sel,
                                   const char *str, uint8_t max_track);

/*============================================================================
 * Source Factory Functions
 *============================================================================*/

/**
 * @brief Create SACD source
 */
int dsdpipe_source_sacd_create(dsdpipe_source_t *source,
                                dsdpipe_channel_type_t channel_type);

/**
 * @brief Create DSDIFF source
 */
int dsdpipe_source_dsdiff_create(dsdpipe_source_t *source);

/**
 * @brief Create DSF source
 */
int dsdpipe_source_dsf_create(dsdpipe_source_t *source);

/**
 * @brief Destroy source (calls ops->destroy if set)
 */
void dsdpipe_source_destroy(dsdpipe_source_t *source);

/*============================================================================
 * Sink Factory Functions
 *============================================================================*/

/**
 * @brief Create DSF sink
 */
int dsdpipe_sink_dsf_create(dsdpipe_sink_t **sink,
                             const dsdpipe_sink_config_t *config);

/**
 * @brief Create DSDIFF sink
 */
int dsdpipe_sink_dsdiff_create(dsdpipe_sink_t **sink,
                                const dsdpipe_sink_config_t *config);

/**
 * @brief Set track selection count for DSDIFF edit master sink
 * @param ctx DSDIFF sink context
 * @param track_count Number of tracks in the selection (for ID3 renumbering)
 */
void dsdpipe_sink_dsdiff_set_track_count(void *ctx, uint8_t track_count);

/**
 * @brief Create WAV sink
 */
int dsdpipe_sink_wav_create(dsdpipe_sink_t **sink,
                             const dsdpipe_sink_config_t *config);

/**
 * @brief Create FLAC sink
 */
int dsdpipe_sink_flac_create(dsdpipe_sink_t **sink,
                              const dsdpipe_sink_config_t *config);

/**
 * @brief Create Print (text metadata) sink
 */
int dsdpipe_sink_print_create(dsdpipe_sink_t *sink);

/**
 * @brief Create XML metadata sink
 */
int dsdpipe_sink_xml_create(dsdpipe_sink_t *sink);

/**
 * @brief Create CUE sheet sink
 */
int dsdpipe_sink_cue_create(dsdpipe_sink_t *sink, const char *audio_filename);

/**
 * @brief Create ID3 tag file sink
 */
int dsdpipe_sink_id3_create(dsdpipe_sink_t *sink, bool per_track);

/**
 * @brief Destroy sink
 */
void dsdpipe_sink_destroy(dsdpipe_sink_t *sink);

/*============================================================================
 * Transform Factory Functions
 *============================================================================*/

/**
 * @brief Create DST decoder transform
 */
int dsdpipe_transform_dst_create(dsdpipe_transform_t **transform);

/**
 * @brief Create DSD-to-PCM converter transform
 */
int dsdpipe_transform_dsd2pcm_create(dsdpipe_transform_t **transform,
                                      dsdpipe_pcm_quality_t quality,
                                      bool use_fp64,
                                      int pcm_sample_rate);

/**
 * @brief Destroy transform
 */
void dsdpipe_transform_destroy(dsdpipe_transform_t *transform);

/*============================================================================
 * Metadata Helper Functions
 *============================================================================*/

/**
 * @brief Duplicate a string (returns NULL if src is NULL)
 */
char *dsdpipe_strdup(const char *src);

/**
 * @brief Set a metadata string field (frees old value)
 */
int dsdpipe_metadata_set_string(char **field, const char *value);

#ifdef __cplusplus
}
#endif

#endif /* LIBDSDPIPE_DSDPIPE_INTERNAL_H */
