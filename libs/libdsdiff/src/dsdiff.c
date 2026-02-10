/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
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


#include <libdsdiff/dsdiff.h>

#include "dsdiff_chunks.h"
#include "dsdiff_markers.h"
#include "dsdiff_io.h"

#include <libsautil/mem.h>

#include <stdlib.h>
#include <string.h>

/**
 * @brief Internal DSDIFF file handle structure
 *
 * This structure maintains all state for reading/writing DSDIFF files.
 * Fields are grouped by functional area for clarity.
 */
struct dsdiff_s {

    /* =========================================================================
     * File State
     * ======================================================================= */

    dsdiff_chunk_t *io;                 /**< I/O handle for file operations */
    dsdiff_file_mode_t mode;            /**< Current file mode (read/write/modify) */
    uint32_t format_version;            /**< DSDIFF format version (e.g., 0x01050000) */
    uint64_t file_size_after_finalize;  /**< Final file size after close */

    /* =========================================================================
     * Audio Format (PROP/SND chunk)
     * ======================================================================= */

    uint16_t channel_count;             /**< Number of audio channels */
    dsdiff_channel_id_t *channel_ids;   /**< Array of channel identifiers */
    uint32_t sample_rate;               /**< Sample rate in Hz (e.g., 2822400) */
    char *compression_name;             /**< Compression type name string */

    uint64_t sample_frame_count;        /**< Current number of sample frames written */
    uint64_t sample_frame_capacity;     /**< Pre-allocated capacity for sample frames */

    /* =========================================================================
     * Sound Data Positions
     * ======================================================================= */

    uint64_t prop_chunk_size;           /**< Size of PROP chunk */
    uint64_t sound_data_size;           /**< Total size of sound data */
    uint64_t sound_data_start_pos;      /**< File position: start of sound data */
    uint64_t sound_data_end_pos;        /**< File position: end of sound data */

    /* =========================================================================
     * Optional Metadata: Timecode (ABSS chunk)
     * ======================================================================= */

    int has_timecode;                   /**< Flag: timecode present */
    dsdiff_timecode_t start_timecode;   /**< Absolute start time */
    uint64_t timecode_chunk_pos;        /**< File position: ABSS chunk */

    /* =========================================================================
     * Optional Metadata: Loudspeaker Config (LSCO chunk)
     * ======================================================================= */

    int has_loudspeaker_config;         /**< Flag: loudspeaker config present */
    dsdiff_loudspeaker_config_t loudspeaker_config; /**< Speaker configuration */
    uint64_t loudspeaker_chunk_pos;     /**< File position: LSCO chunk */

    /* =========================================================================
     * Optional Metadata: Comments (COMT chunk)
     * ======================================================================= */

    uint16_t comment_count;             /**< Number of comments */
    dsdiff_comment_t *comments;         /**< Array of comment structures */
    uint64_t comment_chunk_pos;         /**< File position: COMT chunk */

    /* =========================================================================
     * Optional Metadata: ID3 Tags
     * ======================================================================= */

    uint32_t id3_tag_size;              /**< Size of ID3 tag data */
    uint8_t *id3_tag;                   /**< Raw ID3 tag data */
    uint64_t id3_chunk_pos;             /**< File position: ID3 chunk */

    /* =========================================================================
     * Per-Track ID3 Tags (Edit Master mode)
     * ======================================================================= */

    uint32_t track_id3_count;           /**< Number of per-track ID3 tags */
    uint8_t **track_id3_tags;           /**< Array of raw ID3 tag data */
    uint32_t *track_id3_sizes;          /**< Array of ID3 tag sizes */

    /* =========================================================================
     * Optional Metadata: Manufacturer Data (MANF chunk)
     * ======================================================================= */

    int has_manufacturer;               /**< Flag: manufacturer data present */
    uint8_t manufacturer_id[4];         /**< 4-byte manufacturer identifier */
    uint32_t manufacturer_data_size;    /**< Size of manufacturer data */
    uint8_t *manufacturer_data;         /**< Raw manufacturer data */
    uint64_t manufacturer_chunk_pos;    /**< File position: MANF chunk */

    /* =========================================================================
     * Optional Metadata: Disc Info (DIIN container)
     * ======================================================================= */

    int has_disc_artist;                /**< Flag: disc artist present */
    char *disc_artist;                  /**< Disc artist string (DIAR) */
    int has_disc_title;                 /**< Flag: disc title present */
    char *disc_title;                   /**< Disc title string (DITI) */
    int has_emid;                       /**< Flag: EMID present */
    char *emid;                         /**< Edited Master ID string */

    uint64_t diin_chunk_pos;            /**< File position: DIIN chunk header */
    uint64_t diin_file_start;           /**< File position: DIIN data start */
    uint64_t diin_file_end;             /**< File position: DIIN data end */

    /* =========================================================================
     * Markers (MARK chunks inside DIIN)
     * ======================================================================= */

    dsdiff_marker_list_t markers;       /**< List of DSD markers */

    /* =========================================================================
     * DST Compression (DST container)
     * ======================================================================= */

    int is_dst_format;                  /**< Flag: file uses DST compression */
    uint32_t dst_frame_count;           /**< Number of DST frames */
    uint16_t dst_frame_rate;            /**< DST frame rate (typically 75 fps) */
    uint64_t dst_chunk_size;            /**< Size of DST chunk */
    uint64_t dst_data_end;              /**< File position: end of DST data */

    int has_crc;                        /**< Flag: CRC data present */
    uint32_t crc_size;                  /**< Size of CRC data */

    /* =========================================================================
     * DST Index (DSTI chunk)
     * ======================================================================= */

    int has_index;                      /**< Flag: index chunk present */
    uint32_t reserved_index_count;      /**< Pre-allocated index entry count */
    dsdiff_index_t *indexes;            /**< Array of frame index entries */

    uint64_t index_file_start;          /**< File position: index data start */
    uint64_t index_file_end;            /**< File position: index data end */
    uint64_t index_file_size;           /**< Size of index data */

    /* =========================================================================
     * Chunk Position Cache (for modification)
     * ======================================================================= */

    uint64_t channel_chunk_pos;         /**< File position: CHNL chunk */
};

/* Initialization / Cleanup */
static void dsdiff_init_handle(dsdiff_t *handle);
static void dsdiff_cleanup_handle(dsdiff_t *handle);

/* Parsing (Read) */
static int dsdiff_parse_file(dsdiff_t *handle);
static int dsdiff_parse_frm8(dsdiff_t *handle, uint64_t chunk_size);
static int dsdiff_parse_prop(dsdiff_t *handle, uint64_t chunk_size);
static int dsdiff_parse_dst(dsdiff_t *handle);
static int dsdiff_parse_diin(dsdiff_t *handle, uint64_t chunk_size);
static int dsdiff_read_index(dsdiff_t *handle);

/* Writing */
static int dsdiff_write_diin(dsdiff_t *handle);
static int dsdiff_write_index(dsdiff_t *handle);
static int dsdiff_write_dst_frame_internal(dsdiff_t *handle,
                                           const uint8_t *data,
                                           uint32_t size);

/* Validation */
static int dsdiff_validate_version(dsdiff_t *handle);
static int dsdiff_is_chunk_writable(dsdiff_t *handle, uint64_t position);
static int dsdiff_verify_write_position(dsdiff_t *handle, uint64_t position);

/* Utilities */
static void dsdiff_timecode_normalize(dsdiff_timecode_t *timecode,
                                      uint32_t sample_rate);

/**
 * @brief Set default channel IDs based on channel count
 *
 * Sets standard channel configurations for common layouts:
 * - 2 channels: Stereo (SLFT, SRGT)
 * - 5 channels: 5.0 surround (MLFT, MRGT, C, LS, RS)
 * - 6 channels: 5.1 surround (MLFT, MRGT, C, LFE, LS, RS)
 *
 * For other channel counts, uses generic channel IDs.
 *
 * @param handle File handle
 * @param channel_count Number of channels
 * @return 0 on success, negative error code on failure
 */
static int dsdiff_set_default_channel_ids(dsdiff_t *handle, uint16_t channel_count) {
    int i;

    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    /* Free existing channel IDs if present */
    if (handle->channel_ids != NULL) {
        sa_free(handle->channel_ids);
        handle->channel_ids = NULL;
    }

    handle->channel_count = channel_count;
    handle->channel_ids = (dsdiff_channel_id_t *)sa_malloc(
        channel_count * sizeof(dsdiff_channel_id_t));
    if (handle->channel_ids == NULL) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    /* Set default channel IDs based on channel count */
    switch (channel_count) {
        case 2:
            /* Stereo */
            handle->channel_ids[0] = DSDIFF_CHAN_SLFT;
            handle->channel_ids[1] = DSDIFF_CHAN_SRGT;
            break;
        case 5:
            /* 5.0 surround */
            handle->channel_ids[0] = DSDIFF_CHAN_MLFT;
            handle->channel_ids[1] = DSDIFF_CHAN_MRGT;
            handle->channel_ids[2] = DSDIFF_CHAN_C;
            handle->channel_ids[3] = DSDIFF_CHAN_LS;
            handle->channel_ids[4] = DSDIFF_CHAN_RS;
            break;
        case 6:
            /* 5.1 surround */
            handle->channel_ids[0] = DSDIFF_CHAN_MLFT;
            handle->channel_ids[1] = DSDIFF_CHAN_MRGT;
            handle->channel_ids[2] = DSDIFF_CHAN_C;
            handle->channel_ids[3] = DSDIFF_CHAN_LFE;
            handle->channel_ids[4] = DSDIFF_CHAN_LS;
            handle->channel_ids[5] = DSDIFF_CHAN_RS;
            break;
        default:
            /* For other channel counts, use generic mono channel IDs */
            for (i = 0; i < channel_count; i++) {
                handle->channel_ids[i] = DSDIFF_CHAN_C;
            }
            break;
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * File Lifecycle Operations
 * ===========================================================================*/

int dsdiff_new(dsdiff_t **handle) {
    dsdiff_t *new_handle;

    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    new_handle = (dsdiff_t *)sa_malloc(sizeof(dsdiff_t));
    if (!new_handle) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    memset(new_handle, 0, sizeof(dsdiff_t));
    dsdiff_init_handle(new_handle);
    *handle = new_handle;

    return DSDIFF_SUCCESS;
}

int dsdiff_create(dsdiff_t *handle,
                  const char *filename,
                  dsdiff_audio_type_t file_type,
                  uint16_t channel_count,
                  uint16_t sample_bits,
                  uint32_t sample_rate) {
    int ret;
    uint64_t start_chunk_pos = 0;
    uint64_t end_chunk_pos = 0;

    /*
     * Parameter validation
     */
    if (!handle || !filename) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if ((channel_count < 1) || (channel_count > 1000)) {
        return DSDIFF_ERROR_INVALID_CHANNELS;
    }

    if (sample_bits != 1) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (strlen(filename) <= 0) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode != DSDIFF_FILE_MODE_CLOSED) {
        sa_free(handle);
        return DSDIFF_ERROR_ALREADY_OPEN;
    }

    /*
     * Initialize handle state for new file creation
     */
    handle->mode = DSDIFF_FILE_MODE_CLOSED;
    handle->sample_frame_count = 0;
    handle->prop_chunk_size = 0;
    handle->sound_data_size = 0;
    handle->sound_data_start_pos = 0;
    handle->sound_data_end_pos = 0;
    handle->dst_frame_count = 0;
    handle->is_dst_format = (file_type != DSDIFF_AUDIO_DSD);

    handle->sample_rate = sample_rate;
    handle->sample_frame_capacity = 0;

    /*
     * Set default channel IDs based on channel count.
     * This sets up standard speaker configurations for common layouts.
     */
    ret = dsdiff_set_default_channel_ids(handle, channel_count);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    /*
     * Create file for writing.
     */
    ret = dsdiff_chunk_file_open_write(&handle->io, filename);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    handle->mode = DSDIFF_FILE_MODE_WRITE;

    /*
     * Write FRM8 container header (placeholder size, updated on finalize).
     * Form type is "DSD " for uncompressed or "DST " for DST-encoded files.
     */
    ret = dsdiff_chunk_write_frm8_header(handle->io, 0, handle->is_dst_format);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    /* Format version chunk - identifies DSDIFF specification version */
    ret = dsdiff_chunk_write_fver(handle->io, handle->format_version);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    /*
     * Write PROP (Property) chunk containing audio format properties.
     * Sub-chunks: FS (sample rate), CHNL (channels), CMPR (compression),
     *             ABSS (timecode, optional), LSCO (loudspeaker config, optional)
     */
    ret = dsdiff_io_get_position(handle->io, &start_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    ret = dsdiff_chunk_write_prop_header(handle->io, 0);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    ret = dsdiff_chunk_write_fs(handle->io, sample_rate);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    /* Save channel chunk position for potential later modification */
    ret = dsdiff_io_get_position(handle->io, &handle->channel_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    ret = dsdiff_chunk_write_chnl(handle->io, channel_count, handle->channel_ids);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    /* Compression type determines audio data format (DSD raw vs DST encoded) */
    if (file_type == DSDIFF_AUDIO_DSD) {
        ret = dsdiff_chunk_write_cmpr(handle->io, DSDIFF_AUDIO_DSD, "not compressed");
    } else if (file_type == DSDIFF_AUDIO_DST) {
        ret = dsdiff_chunk_write_cmpr(handle->io, DSDIFF_AUDIO_DST, "DST Encoded");
    } else {
        ret = DSDIFF_ERROR_INVALID_ARG;
    }
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    /* Optional ABSS chunk for absolute start time (timecode) */
    ret = dsdiff_io_get_position(handle->io, &handle->timecode_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    if (handle->has_timecode) {
        ret = dsdiff_chunk_write_abss(handle->io, &handle->start_timecode);
        if (ret != DSDIFF_SUCCESS) {
            goto cleanup;
        }
    } else {
        handle->timecode_chunk_pos = 0;
    }

    /* Optional LSCO chunk for loudspeaker configuration */
    ret = dsdiff_io_get_position(handle->io, &handle->loudspeaker_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    if (handle->has_loudspeaker_config) {
        ret = dsdiff_chunk_write_lsco(handle->io, handle->loudspeaker_config);
        if (ret != DSDIFF_SUCCESS) {
            goto cleanup;
        }
    } else {
        handle->loudspeaker_chunk_pos = 0;
    }

    /* Calculate PROP chunk size (excludes chunk ID and size field: 12 bytes) */
    ret = dsdiff_io_get_position(handle->io, &end_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }
    handle->prop_chunk_size = end_chunk_pos - start_chunk_pos - 12;

    /*
     * Write sound data container (DSD or DST).
     * For DST, also write FRTE (frame table entry) with initial frame count.
     * Initial size is 0; actual size is updated when file is finalized.
     */
    if (file_type == DSDIFF_AUDIO_DSD) {
        ret = dsdiff_chunk_write_snd_header(handle->io, 0,
                                            &handle->sound_data_start_pos,
                                            &handle->sound_data_end_pos);
    } else if (file_type == DSDIFF_AUDIO_DST) {
        handle->sound_data_size += (4 + 8 + 4 + 2);  /* DST header overhead */
        ret = dsdiff_chunk_write_dst_header(handle->io, 0,
                                            &handle->sound_data_start_pos,
                                            &handle->sound_data_end_pos);
        if (ret == DSDIFF_SUCCESS) {
            ret = dsdiff_chunk_write_frte(handle->io, handle->dst_frame_count, handle->dst_frame_rate);
        }
    } else {
        ret = DSDIFF_ERROR_INVALID_ARG;
    }
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    /* Set end position to start - audio data not yet written */
    handle->sound_data_end_pos = handle->sound_data_start_pos;

    return DSDIFF_SUCCESS;

cleanup:
    /* Release all resources on failure */
    if (handle->mode != DSDIFF_FILE_MODE_CLOSED) {
        dsdiff_io_close(handle->io);
    }
    if (handle->channel_ids) {
        sa_free(handle->channel_ids);
    }
    dsdiff_marker_list_free(&handle->markers);
    return ret;
}

int dsdiff_open(dsdiff_t *handle, const char *filename) {
    int ret;

    if (!handle || !filename || strlen(filename) <= 0) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->io == NULL) {
    }

    ret = dsdiff_chunk_file_open_read(&handle->io, filename);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    handle->mode = DSDIFF_FILE_MODE_READ;

    ret = dsdiff_parse_file(handle);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    ret = dsdiff_seek_dsd_start(handle);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    return DSDIFF_SUCCESS;

cleanup:
    if (handle->mode != DSDIFF_FILE_MODE_CLOSED) {
        handle->mode = DSDIFF_FILE_MODE_CLOSED;
        dsdiff_io_close(handle->io);
    }
    dsdiff_cleanup_handle(handle);
    sa_free(handle);
    return ret;
}

int dsdiff_modify(dsdiff_t *handle, const char *filename) {
    int ret;
    uint64_t file_pos;

    if (!handle || !filename || strlen(filename) <= 0) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->io == NULL) {
    }

    ret = dsdiff_chunk_file_open_modify(&handle->io, filename);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    handle->mode = DSDIFF_FILE_MODE_MODIFY;

    ret = dsdiff_parse_file(handle);
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    if (handle->is_dst_format) {
        ret = dsdiff_io_seek(handle->io, handle->dst_data_end, DSDIFF_SEEK_SET, &file_pos);
    } else {
        ret = dsdiff_io_seek(handle->io, handle->sound_data_end_pos, DSDIFF_SEEK_SET, &file_pos);
    }
    if (ret != DSDIFF_SUCCESS) {
        goto cleanup;
    }

    return DSDIFF_SUCCESS;

cleanup:
    if (handle->mode != DSDIFF_FILE_MODE_CLOSED) {
        handle->mode = DSDIFF_FILE_MODE_CLOSED;
        dsdiff_io_close(handle->io);
    }
    dsdiff_cleanup_handle(handle);
    sa_free(handle);
    return ret;
}

int dsdiff_finalize(dsdiff_t *handle) {
    int ret = DSDIFF_SUCCESS;
    uint64_t file_pos = 0;
    uint64_t end_file_pos = 0;
    uint64_t file_size = 0;
    uint64_t seek_pos;

    /*
     * Parameter validation
     */
    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    if ((handle->mode == DSDIFF_FILE_MODE_WRITE) || (handle->mode == DSDIFF_FILE_MODE_MODIFY)) {
        dsdiff_validate_version(handle);

        /*
         * Ensure sound data chunk is word-aligned (DSDIFF requirement).
         * Add padding byte if sound data size is odd.
         */
        uint64_t sound_chunk_size = handle->sound_data_size;
        if ((sound_chunk_size % 2) != 0) {
            ret = dsdiff_io_write_pad_byte(handle->io);
            if (ret != DSDIFF_SUCCESS) {
                return ret;
            }
        }

        /*
         * Write optional metadata chunks at end of file.
         * Only writes if chunk position is writable (not in middle of existing data).
         */

        /* DIIN (Disc Information) chunk with EMID, artist, title */
        if (dsdiff_is_chunk_writable(handle, handle->diin_chunk_pos)) {
            ret = dsdiff_write_diin(handle);
            if (ret != DSDIFF_SUCCESS) {
                return ret;
            }
        }

        /* INDEX chunk for DST frame positions (DST format only) */
        ret = dsdiff_write_index(handle);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        /* COMT (Comments) chunk */
        if (handle->comment_count != 0) {
            if (dsdiff_is_chunk_writable(handle, handle->comment_chunk_pos)) {
                ret = dsdiff_chunk_write_comt(handle->io, handle->comment_count, handle->comments);
                if (ret != DSDIFF_SUCCESS) {
                    return ret;
                }
            }
        }

        /* ID3v2 metadata chunk (file-level) */
        if (handle->id3_tag_size != 0) {
            if (dsdiff_is_chunk_writable(handle, handle->id3_chunk_pos)) {
                ret = dsdiff_chunk_write_id3(handle->io, handle->id3_tag, handle->id3_tag_size);
                if (ret != DSDIFF_SUCCESS) {
                    return ret;
                }
            }
        }

        /* Per-track ID3v2 metadata chunks (Edit Master mode) */
        for (uint32_t i = 0; i < handle->track_id3_count; i++) {
            if (handle->track_id3_tags[i] != NULL && handle->track_id3_sizes[i] != 0) {
                ret = dsdiff_chunk_write_id3(handle->io,
                                             handle->track_id3_tags[i],
                                             handle->track_id3_sizes[i]);
                if (ret != DSDIFF_SUCCESS) {
                    return ret;
                }
            }
        }

        /* MANF (Manufacturer-specific) chunk */
        if (handle->has_manufacturer) {
            if (dsdiff_is_chunk_writable(handle, handle->manufacturer_chunk_pos)) {
                ret = dsdiff_chunk_write_manf(handle->io,
                                              handle->manufacturer_id,
                                              handle->manufacturer_data,
                                              handle->manufacturer_data_size);
                if (ret != DSDIFF_SUCCESS) {
                    return ret;
                }
            }
        }

        /*
         * Calculate final file size and update header chunks.
         * FRM8 size excludes 12-byte header (4-byte ID + 8-byte size field).
         */
        ret = dsdiff_io_get_position(handle->io, &end_file_pos);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        handle->file_size_after_finalize = end_file_pos;
        file_size = (uint64_t)end_file_pos - 12;

        /*
         * Rewrite file header with correct sizes.
         * Seek to beginning and update FRM8, FVER, PROP headers.
         */
        ret = dsdiff_io_seek(handle->io, 0, DSDIFF_SEEK_SET, &file_pos);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        ret = dsdiff_chunk_write_frm8_header(handle->io, file_size, handle->is_dst_format);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        ret = dsdiff_chunk_write_fver(handle->io, handle->format_version);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        ret = dsdiff_io_get_position(handle->io, &file_pos);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        ret = dsdiff_chunk_write_prop_header(handle->io, handle->prop_chunk_size);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        /* Update channel configuration (may have changed via set_channel_ids) */
        ret = dsdiff_io_seek(handle->io, handle->channel_chunk_pos, DSDIFF_SEEK_SET, &seek_pos);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        ret = dsdiff_chunk_write_chnl(handle->io, handle->channel_count, handle->channel_ids);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        /* Update timecode if present (normalize to valid H:M:S:samples format) */
        ret = dsdiff_io_seek(handle->io, handle->timecode_chunk_pos, DSDIFF_SEEK_SET, &seek_pos);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        if (handle->has_timecode) {
            dsdiff_timecode_normalize(&handle->start_timecode, handle->sample_rate);
            ret = dsdiff_chunk_write_abss(handle->io, &handle->start_timecode);
            if (ret != DSDIFF_SUCCESS) {
                return ret;
            }
        }

        /*
         * Update sound data header with final size (WRITE mode only).
         * MODIFY mode preserves original sound data header position.
         */
        if (handle->mode == DSDIFF_FILE_MODE_WRITE) {
            ret = dsdiff_io_seek(handle->io, file_pos + 12 + handle->prop_chunk_size, DSDIFF_SEEK_SET, &file_pos);
            if (ret != DSDIFF_SUCCESS) {
                return ret;
            }

            if (!handle->is_dst_format) {
                ret = dsdiff_chunk_write_snd_header(handle->io, handle->sound_data_size,
                                                    &handle->sound_data_start_pos,
                                                    &handle->sound_data_end_pos);
                if (ret != DSDIFF_SUCCESS) {
                    return ret;
                }
            } else {
                ret = dsdiff_chunk_write_dst_header(handle->io, handle->sound_data_size,
                                                    &handle->sound_data_start_pos,
                                                    &handle->sound_data_end_pos);
                if (ret != DSDIFF_SUCCESS) {
                    return ret;
                }

                /* Update DST frame table entry count */
                ret = dsdiff_chunk_write_frte(handle->io, handle->dst_frame_count, handle->dst_frame_rate);
                if (ret != DSDIFF_SUCCESS) {
                    return ret;
                }
            }
        }

        /* Seek to end of file for proper truncation on close */
        ret = dsdiff_io_seek(handle->io, handle->file_size_after_finalize, DSDIFF_SEEK_SET, &seek_pos);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_close(dsdiff_t *handle) {
    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    if (handle->io) {
        dsdiff_io_close(handle->io);
    }

    if (handle->channel_ids) {
        sa_free(handle->channel_ids);
    }

    if (handle->compression_name) {
        sa_free(handle->compression_name);
    }

    if (handle->indexes) {
        sa_free(handle->indexes);
    }

    if (handle->comments) {
        uint16_t i;
        for (i = 0; i < handle->comment_count; i++) {
            if (handle->comments[i].text) {
                sa_free(handle->comments[i].text);
            }
        }
        sa_free(handle->comments);
    }

    dsdiff_marker_list_free(&handle->markers);

    if (handle->emid) {
        sa_free(handle->emid);
    }

    if (handle->disc_artist) {
        sa_free(handle->disc_artist);
    }

    if (handle->disc_title) {
        sa_free(handle->disc_title);
    }

    sa_free(handle);
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * File Properties (Read-only)
 * ===========================================================================*/

int dsdiff_get_open_mode(dsdiff_t *handle, dsdiff_file_mode_t *mode) {
    if (!handle || !mode) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *mode = handle->mode;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_audio_type(dsdiff_t *handle, dsdiff_audio_type_t *audio_type) {
    if (!handle || !audio_type) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    *audio_type = handle->is_dst_format ? DSDIFF_AUDIO_DST : DSDIFF_AUDIO_DSD;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_filename(dsdiff_t *handle, char *filename, size_t buffer_size) {
    if (!handle || !filename || buffer_size == 0) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    dsdiff_io_get_filename(handle->io, filename, buffer_size);
    return DSDIFF_SUCCESS;
}

int dsdiff_get_channel_count(dsdiff_t *handle, uint16_t *channel_count) {
    if (!handle || !channel_count) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    *channel_count = handle->channel_count;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_sample_bits(dsdiff_t *handle, uint16_t *sample_bits) {
    if (!handle || !sample_bits) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    *sample_bits = 1;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_sample_rate(dsdiff_t *handle, uint32_t *sample_rate) {
    if (!handle || !sample_rate) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    *sample_rate = handle->sample_rate;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_sample_frame_count(dsdiff_t *handle, uint64_t *num_sample_frames) {
    if (!handle || !num_sample_frames) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    *num_sample_frames = handle->sample_frame_count;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_dsd_data_size(dsdiff_t *handle, uint64_t *data_size) {
    if (!handle || !data_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    *data_size = handle->sound_data_size;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_format_version(dsdiff_t *handle,
                               uint8_t *major_version,
                               uint8_t *minor_version) {
    if (!handle || !major_version || !minor_version) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *major_version = (handle->format_version >> 24) & 0xFF;
    *minor_version = (handle->format_version >> 16) & 0xFF;
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Audio Data I/O (DSD Format)
 * ===========================================================================*/

int dsdiff_read_dsd_data(dsdiff_t *handle,
                         uint8_t *buffer,
                         uint32_t byte_count,
                         uint32_t *bytes_read_out) {
    int ret = DSDIFF_SUCCESS;
    uint64_t file_pos;
    size_t transfer_count;
    size_t bytes_read;

    if (!handle || !buffer || !bytes_read_out) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *bytes_read_out = 0;

    if (handle->is_dst_format) {
        return DSDIFF_SUCCESS;
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    if (handle->mode != DSDIFF_FILE_MODE_READ) {
        return DSDIFF_ERROR_MODE_WRITE_ONLY;
    }

    if (byte_count > 0) {
        transfer_count = byte_count;

        ret = dsdiff_io_get_position(handle->io, &file_pos);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        if (file_pos < handle->sound_data_end_pos) {
            if ((file_pos + transfer_count) > handle->sound_data_end_pos) {
                transfer_count = (size_t)(handle->sound_data_end_pos - file_pos);
            }

            ret = dsdiff_io_read_bytes(handle->io, buffer, transfer_count, &bytes_read);
            if (ret == DSDIFF_SUCCESS) {
                *bytes_read_out = (uint32_t)bytes_read;
            }
        } else {
            ret = DSDIFF_ERROR_END_OF_DATA;
        }
    }

    return ret;
}

int dsdiff_write_dsd_data(dsdiff_t *handle,
                          const uint8_t *buffer,
                          uint32_t byte_count,
                          uint32_t *bytes_written_out) {
    int ret;
    size_t transfer_count;
    size_t bytes_written;

    if (!handle || !buffer || !bytes_written_out) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *bytes_written_out = 0;

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    if (handle->mode != DSDIFF_FILE_MODE_WRITE) {
        return DSDIFF_ERROR_MODE_READ_ONLY;
    }

    if (handle->is_dst_format) {
        return DSDIFF_ERROR_REQUIRES_DSD;
    }

    if (byte_count > 0) {
        transfer_count = byte_count;

        if (DSDIFF_MAX_DATA_SIZE >= (handle->sound_data_size + transfer_count)) {
            ret = dsdiff_io_write_bytes(handle->io, buffer, transfer_count, &bytes_written);

            if (ret != DSDIFF_SUCCESS) {
                return DSDIFF_ERROR_WRITE_FAILED;
            }

            handle->sample_frame_count += (uint32_t)(bytes_written / handle->channel_count);
            handle->sound_data_size += (uint64_t)bytes_written;
            handle->sound_data_end_pos += (uint64_t)bytes_written;
            *bytes_written_out = (uint32_t)bytes_written;
        } else {
            return DSDIFF_ERROR_MAX_FILE_SIZE;
        }
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_skip_dsd_data(dsdiff_t *handle,
                         uint32_t skip_count,
                         uint32_t *skipped_count) {
    int ret = DSDIFF_SUCCESS;
    uint64_t file_pos;
    uint64_t offset;

    if (!handle || !skipped_count) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *skipped_count = 0;

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    if ((handle->mode != DSDIFF_FILE_MODE_READ) && (handle->mode != DSDIFF_FILE_MODE_MODIFY)) {
        return DSDIFF_ERROR_MODE_WRITE_ONLY;
    }

    if (skip_count > 0) {
        offset = (uint64_t)skip_count * (uint64_t)handle->channel_count;

        ret = dsdiff_io_get_position(handle->io, &file_pos);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        if (file_pos < handle->sound_data_end_pos) {
            if ((file_pos + offset) > handle->sound_data_end_pos) {
                offset = handle->sound_data_end_pos - file_pos;
            }

            ret = dsdiff_io_seek(handle->io, offset, DSDIFF_SEEK_CUR, &file_pos);
            if (ret == DSDIFF_SUCCESS) {
                *skipped_count = (uint32_t)(offset / handle->channel_count);
            }
        } else {
            ret = DSDIFF_ERROR_END_OF_DATA;
        }
    }

    return ret;
}

int dsdiff_seek_dsd_data(dsdiff_t *handle,
                          int64_t frame_offset,
                          dsdiff_seek_dir_t origin) {
    int ret = DSDIFF_SUCCESS;
    uint64_t file_pos = 0;
    int64_t byte_offset;
    uint64_t new_pos = 0;

    /*
     * Parameter validation
     */
    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    /* DST format uses frame-based seeking, not byte-based */
    if (handle->is_dst_format) {
        return DSDIFF_SUCCESS;
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    ret = dsdiff_io_get_position(handle->io, &file_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    /*
     * Convert sample frame offset to byte offset.
     * Each sample frame contains one byte per channel.
     */
    byte_offset = frame_offset * (int64_t)handle->channel_count;

    /*
     * Calculate target position based on seek origin.
     * Handles negative offsets safely to prevent underflow.
     */
    switch (origin) {
    case DSDIFF_SEEK_SET:
        /* From start of sound data */
        if (byte_offset >= 0) {
            new_pos = handle->sound_data_start_pos + (uint64_t)byte_offset;
        } else {
            new_pos = handle->sound_data_start_pos;
        }
        break;
    case DSDIFF_SEEK_CUR:
        /* From current file position */
        if (byte_offset >= 0) {
            new_pos = file_pos + (uint64_t)byte_offset;
        } else {
            if ((uint64_t)(-byte_offset) > file_pos) {
                new_pos = 0;
            } else {
                new_pos = file_pos - (uint64_t)(-byte_offset);
            }
        }
        break;
    case DSDIFF_SEEK_END:
        /* From end of written sound data */
        if (byte_offset >= 0) {
            new_pos = handle->sound_data_end_pos + (uint64_t)byte_offset;
        } else {
            if ((uint64_t)(-byte_offset) > handle->sound_data_end_pos) {
                new_pos = 0;
            } else {
                new_pos = handle->sound_data_end_pos - (uint64_t)(-byte_offset);
            }
        }
        break;
    }

    /* Clamp to sound data boundaries */
    if (new_pos < handle->sound_data_start_pos) {
        new_pos = handle->sound_data_start_pos;
    }

    /*
     * Boundary behavior differs by mode:
     * - READ: clamp to existing data end
     * - WRITE/MODIFY: extend data end if seeking beyond current end
     */
    if (handle->mode == DSDIFF_FILE_MODE_READ) {
        if (new_pos > handle->sound_data_end_pos) {
            new_pos = handle->sound_data_end_pos;
        }
    } else {
        if (new_pos > handle->sound_data_end_pos) {
            handle->sound_data_end_pos = new_pos;
        }
    }

    ret = dsdiff_io_seek(handle->io, new_pos, DSDIFF_SEEK_SET, &file_pos);

    return ret;
}

int dsdiff_seek_dsd_start(dsdiff_t *handle) {
    int ret = DSDIFF_SUCCESS;
    uint64_t file_pos;

    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (!handle->is_dst_format) {
        return dsdiff_seek_dsd_data(handle, 0, DSDIFF_SEEK_SET);
    }

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    if ((handle->mode != DSDIFF_FILE_MODE_READ) && (handle->mode != DSDIFF_FILE_MODE_MODIFY)) {
        return DSDIFF_ERROR_MODE_WRITE_ONLY;
    }

    ret = dsdiff_io_seek(handle->io, handle->sound_data_start_pos, DSDIFF_SEEK_SET, &file_pos);

    return ret;
}

/* =============================================================================
 * Audio Data I/O (DST Compressed Format)
 * ===========================================================================*/

int dsdiff_get_dst_frame_count(dsdiff_t *handle, uint32_t *num_frames) {
    if (!handle || !num_frames) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *num_frames = handle->dst_frame_count;
    return DSDIFF_SUCCESS;
}

int dsdiff_set_dst_frame_rate(dsdiff_t *handle, uint16_t frame_rate) {
    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode != DSDIFF_FILE_MODE_WRITE && handle->mode != DSDIFF_FILE_MODE_MODIFY) {
        return DSDIFF_ERROR_INVALID_MODE;
    }

    handle->dst_frame_rate = frame_rate;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_dst_frame_rate(dsdiff_t *handle, uint16_t *frame_rate) {
    if (!handle || !frame_rate) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *frame_rate = handle->dst_frame_rate;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_dst_max_frame_size(dsdiff_t *handle, uint32_t *frame_size) {
    if (!handle || !frame_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *frame_size = (handle->channel_count * ((handle->sample_rate / handle->dst_frame_rate) / 8)) + 1;
    return DSDIFF_SUCCESS;
}

int dsdiff_has_dst_crc(dsdiff_t *handle, int *has_crc) {
    if (!handle || !has_crc) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *has_crc = handle->has_crc;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_dst_crc_size(dsdiff_t *handle, uint32_t *crc_size) {
    if (!handle || !crc_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *crc_size = handle->crc_size;
    return DSDIFF_SUCCESS;
}

static int dsdiff_write_dst_frame_internal(dsdiff_t *handle,
                                       const uint8_t *dst_data,
                                       uint32_t frame_size) {
    int ret;
    uint64_t cur_file_pos;
    uint64_t chunk_size;

    /*
     * Mode validation - DST frames can only be written in WRITE mode
     */
    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    if (handle->mode != DSDIFF_FILE_MODE_WRITE) {
        return DSDIFF_ERROR_MODE_READ_ONLY;
    }

    if (!handle->is_dst_format) {
        return DSDIFF_ERROR_REQUIRES_DST;
    }

    if (frame_size == 0) {
        return DSDIFF_SUCCESS;
    }

    if ((handle->sound_data_size + frame_size) > DSDIFF_MAX_DATA_SIZE) {
        return DSDIFF_ERROR_MAX_FILE_SIZE;
    }

    /* Write DSTF chunk containing the compressed frame data */
    ret = dsdiff_chunk_write_dstf(handle->io, frame_size, dst_data, &cur_file_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    handle->dst_frame_count++;

    /*
     * Update total sound data size.
     * Chunk overhead: 4-byte ID + 8-byte size = 12 bytes.
     * Add padding if chunk size is odd (DSDIFF word alignment).
     */
    chunk_size = frame_size + 12;
    if ((chunk_size % 2) == 1) {
        chunk_size++;
    }
    handle->sound_data_size += chunk_size;

    /*
     * Dynamically grow frame index array for random access playback.
     * Allocates in batches of 1000 entries to reduce reallocation frequency.
     */
    if (handle->dst_frame_count >= handle->reserved_index_count) {
        uint32_t new_capacity = handle->reserved_index_count + 1000;
        dsdiff_index_t *new_indexes = (dsdiff_index_t *)sa_realloc(
            handle->indexes,
            new_capacity * sizeof(dsdiff_index_t)
        );
        if (!new_indexes) {
            return DSDIFF_ERROR_OUT_OF_MEMORY;
        }
        handle->indexes = new_indexes;
        handle->reserved_index_count = new_capacity;
    }

    /* Record frame position and size for INDEX chunk written on finalize */
    handle->indexes[handle->dst_frame_count - 1].offset = cur_file_pos;
    handle->indexes[handle->dst_frame_count - 1].length = frame_size;

    return DSDIFF_SUCCESS;
}

int dsdiff_write_dst_frame(dsdiff_t *handle,
                           const uint8_t *dst_data,
                           uint32_t frame_size) {
    if (!handle || !dst_data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->has_crc) {
        return DSDIFF_ERROR_CRC_ALREADY_PRESENT;
    }

    return dsdiff_write_dst_frame_internal(handle, dst_data, frame_size);
}

int dsdiff_write_dst_frame_with_crc(dsdiff_t *handle,
                                    const uint8_t *dst_data,
                                    uint32_t frame_size,
                                    const uint8_t *crc_data,
                                    uint32_t crc_size) {
    int ret;
    uint64_t crc_chunk_size;

    if (!handle || !dst_data || !crc_data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->dst_frame_count == 0) {
        handle->has_crc = 1;
    }

    ret = dsdiff_write_dst_frame_internal(handle, dst_data, frame_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (handle->has_crc && crc_size > 0) {
        ret = dsdiff_chunk_write_dstc(handle->io, crc_size, crc_data);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        crc_chunk_size = crc_size + 12;
        if ((crc_chunk_size % 2) == 1) {
            crc_chunk_size++;
        }
        handle->sound_data_size += crc_chunk_size;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_read_dst_frame(dsdiff_t *handle,
                          uint8_t *dst_data,
                          uint32_t max_frame_size,
                          uint32_t *frame_size) {
    int ret;
    uint64_t chunk_data_size;
    dsdiff_chunk_type_t chunk_id;

    (void)max_frame_size;

    if (!handle || !dst_data || !frame_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *frame_size = 0;

    ret = dsdiff_chunk_read_dstf(handle->io, &chunk_data_size, dst_data);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }
    *frame_size = (uint32_t)chunk_data_size;

    ret = dsdiff_chunk_read_header(handle->io, &chunk_id);

    if (ret == DSDIFF_SUCCESS && chunk_id == DSDIFF_CHUNK_DSTC) {
        ret = dsdiff_chunk_skip(handle->io);
        if (ret == DSDIFF_SUCCESS) {
            ret = dsdiff_chunk_read_header(handle->io, &chunk_id);
        }
    }

    if (chunk_id != DSDIFF_CHUNK_DSTF) {
        uint64_t new_pos;
        dsdiff_io_seek(handle->io, handle->sound_data_start_pos, DSDIFF_SEEK_SET, &new_pos);
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_read_dst_frame_with_crc(dsdiff_t *handle,
                                   uint8_t *dst_data,
                                   uint32_t max_frame_size,
                                   uint32_t *frame_size,
                                   uint8_t *crc_data,
                                   uint32_t max_crc_size,
                                   uint32_t *crc_size) {
    int ret;
    uint64_t chunk_data_size;
    dsdiff_chunk_type_t chunk_id;

    (void)max_frame_size;
    (void)max_crc_size;

    if (!handle || !dst_data || !frame_size || !crc_data || !crc_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *frame_size = 0;
    *crc_size = 0;

    ret = dsdiff_chunk_read_dstf(handle->io, &chunk_data_size, dst_data);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }
    *frame_size = (uint32_t)chunk_data_size;

    ret = dsdiff_chunk_read_header(handle->io, &chunk_id);

    if (ret == DSDIFF_SUCCESS && chunk_id == DSDIFF_CHUNK_DSTC) {
        ret = dsdiff_chunk_read_dstc(handle->io, &chunk_data_size, crc_data);
        if (ret == DSDIFF_SUCCESS) {
            *crc_size = (uint32_t)chunk_data_size;
            ret = dsdiff_chunk_read_header(handle->io, &chunk_id);
        }
    }

    if (chunk_id != DSDIFF_CHUNK_DSTF) {
        uint64_t new_pos;
        dsdiff_io_seek(handle->io, handle->sound_data_start_pos, DSDIFF_SEEK_SET, &new_pos);
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_has_dst_index(dsdiff_t *handle, int *has_index) {
    if (!handle || !has_index) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *has_index = handle->has_index;
    return DSDIFF_SUCCESS;
}

int dsdiff_seek_dst_frame(dsdiff_t *handle, uint32_t frame_index) {
    uint64_t new_pos;

    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (!handle->has_index) {
        return DSDIFF_ERROR_NO_DST_INDEX;
    }

    if (frame_index >= handle->dst_frame_count) {
        return DSDIFF_ERROR_END_OF_DATA;
    }

    return dsdiff_io_seek(handle->io, handle->indexes[frame_index].offset,
                         DSDIFF_SEEK_SET, &new_pos);
}

int dsdiff_read_dst_frame_at_index(dsdiff_t *handle,
                                   uint32_t frame_index,
                                   uint8_t *dst_data,
                                   uint32_t max_frame_size,
                                   uint32_t *frame_size) {
    int ret;
    uint64_t frame_offset;
    uint32_t frame_length;

    if (!handle || !dst_data || !frame_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *frame_size = 0;

    ret = dsdiff_read_index(handle);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (!handle->has_index) {
        return DSDIFF_ERROR_NO_DST_INDEX;
    }

    if (frame_index >= handle->dst_frame_count) {
        return DSDIFF_ERROR_END_OF_DATA;
    }

    frame_offset = handle->indexes[frame_index].offset;
    frame_length = handle->indexes[frame_index].length;

    if (frame_length > max_frame_size) {
        return DSDIFF_ERROR_BUFFER_TOO_SMALL;
    }

    ret = dsdiff_chunk_read_contents(handle->io, frame_offset, frame_length, dst_data);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    *frame_size = (uint32_t)frame_length;

    return DSDIFF_SUCCESS;
}

int dsdiff_read_dst_frame_at_index_with_crc(dsdiff_t *handle,
                                            uint32_t frame_index,
                                            uint8_t *dst_data,
                                            uint32_t max_frame_size,
                                            uint32_t *frame_size,
                                            uint8_t *crc_data,
                                            uint32_t max_crc_size,
                                            uint32_t *crc_size) {
    int ret;
    uint64_t frame_offset;
    uint64_t frame_length;
    uint64_t crc_file_pos;

    if (!handle || !dst_data || !frame_size || !crc_data || !crc_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *frame_size = 0;
    *crc_size = 0;

    ret = dsdiff_read_dst_frame_at_index(handle, frame_index, dst_data, max_frame_size, frame_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (handle->has_crc) {
        if (handle->crc_size > max_crc_size) {
            return DSDIFF_ERROR_BUFFER_TOO_SMALL;
        }

        frame_offset = handle->indexes[frame_index].offset;
        frame_length = handle->indexes[frame_index].length;

        crc_file_pos = frame_offset + frame_length;

        if (frame_length % 2) {
            crc_file_pos++;
        }

        crc_file_pos += 12;

        ret = dsdiff_chunk_read_contents(handle->io, crc_file_pos,
                                         handle->crc_size, crc_data);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        *crc_size = handle->crc_size;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_get_dst_frame_size(dsdiff_t *handle,
                              uint32_t frame_index,
                              uint32_t *frame_size) {
    int ret;

    if (!handle || !frame_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *frame_size = 0;

    ret = dsdiff_read_index(handle);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (!handle->has_index) {
        return DSDIFF_ERROR_NO_DST_INDEX;
    }

    if (frame_index > handle->reserved_index_count) {
        return DSDIFF_SUCCESS;
    }

    *frame_size = handle->indexes[frame_index].length;

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Channel Configuration
 * ===========================================================================*/

int dsdiff_get_channel_ids(dsdiff_t *handle, dsdiff_channel_id_t *channel_ids) {
    int channel;

    if (!handle || !channel_ids) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    memset(channel_ids, 0, sizeof(*channel_ids));

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    if (handle->channel_ids == NULL) {
        return DSDIFF_ERROR_NO_CHANNEL_INFO;
    }

    for (channel = 0; channel < handle->channel_count; channel++) {
        channel_ids[channel] = handle->channel_ids[channel];
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_set_channel_ids(dsdiff_t *handle, const dsdiff_channel_id_t *channel_ids, uint16_t channel_count) {
    int channel;

    if (!handle || !channel_ids) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    /* MODIFY mode: channel count must match original file */
    if (handle->mode == DSDIFF_FILE_MODE_MODIFY) {
        if (handle->channel_count != channel_count) {
            return DSDIFF_ERROR_INVALID_CHANNELS;
        }
    }

    /* Free existing channel IDs before reallocating */
    if (handle->channel_ids != NULL) {
        sa_free(handle->channel_ids);
    }

    handle->channel_count = channel_count;
    handle->channel_ids = (dsdiff_channel_id_t *)sa_malloc(
        handle->channel_count * sizeof(dsdiff_channel_id_t));
    if (handle->channel_ids == NULL) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    /*
     * Validate channel configuration for standard speaker layouts.
     * For recognized layouts (stereo, 5.0, 5.1), enforce proper channel ordering
     * or reject invalid combinations where all expected channels are present
     * but in wrong order (which would indicate user error rather than custom layout).
     */

    /* Stereo (2.0): must be SLFT, SRGT in that order if both are stereo channels */
    if (channel_count == 2) {
        if (channel_ids[0] == DSDIFF_CHAN_SLFT && channel_ids[1] == DSDIFF_CHAN_SRGT) {
            /* Valid stereo configuration */
        } else if ((channel_ids[0] == DSDIFF_CHAN_SLFT || channel_ids[0] == DSDIFF_CHAN_SRGT) &&
                   (channel_ids[1] == DSDIFF_CHAN_SLFT || channel_ids[1] == DSDIFF_CHAN_SRGT)) {
            /* Both channels are stereo but wrong order (e.g., SRGT, SLFT) */
            return DSDIFF_ERROR_INVALID_CHANNELS;
        }
        /* Other 2-channel configs (e.g., custom) are allowed */
    } else if (channel_count == 5) {
        /* 5.0 surround: MLFT, MRGT, C, LS, RS in that order */
        if (channel_ids[0] == DSDIFF_CHAN_MLFT && channel_ids[1] == DSDIFF_CHAN_MRGT &&
            channel_ids[2] == DSDIFF_CHAN_C && channel_ids[3] == DSDIFF_CHAN_LS &&
            channel_ids[4] == DSDIFF_CHAN_RS) {
            /* Valid 5.0 configuration */
        } else {
            /* Check if all 5.0 channels present but in wrong order */
            int has_mlft = 0, has_mrgt = 0, has_c = 0, has_ls = 0, has_rs = 0;
            for (channel = 0; channel < channel_count; channel++) {
                if (channel_ids[channel] == DSDIFF_CHAN_MLFT) has_mlft = 1;
                else if (channel_ids[channel] == DSDIFF_CHAN_MRGT) has_mrgt = 1;
                else if (channel_ids[channel] == DSDIFF_CHAN_C) has_c = 1;
                else if (channel_ids[channel] == DSDIFF_CHAN_LS) has_ls = 1;
                else if (channel_ids[channel] == DSDIFF_CHAN_RS) has_rs = 1;
            }
            if (has_mlft && has_mrgt && has_c && has_ls && has_rs) {
                /* Has all 5.0 channels but wrong order - reject */
                return DSDIFF_ERROR_INVALID_CHANNELS;
            }
            /* Other 5-channel configs allowed (custom layouts) */
        }
    } else if (channel_count == 6) {
        /* 5.1 surround: MLFT, MRGT, C, LFE, LS, RS in that order */
        if (channel_ids[0] == DSDIFF_CHAN_MLFT && channel_ids[1] == DSDIFF_CHAN_MRGT &&
            channel_ids[2] == DSDIFF_CHAN_C && channel_ids[3] == DSDIFF_CHAN_LFE &&
            channel_ids[4] == DSDIFF_CHAN_LS && channel_ids[5] == DSDIFF_CHAN_RS) {
            /* Valid 5.1 configuration */
        } else {
            /* Check if all 5.1 channels present but in wrong order */
            int has_mlft = 0, has_mrgt = 0, has_c = 0, has_lfe = 0, has_ls = 0, has_rs = 0;
            for (channel = 0; channel < channel_count; channel++) {
                if (channel_ids[channel] == DSDIFF_CHAN_MLFT) has_mlft = 1;
                else if (channel_ids[channel] == DSDIFF_CHAN_MRGT) has_mrgt = 1;
                else if (channel_ids[channel] == DSDIFF_CHAN_C) has_c = 1;
                else if (channel_ids[channel] == DSDIFF_CHAN_LFE) has_lfe = 1;
                else if (channel_ids[channel] == DSDIFF_CHAN_LS) has_ls = 1;
                else if (channel_ids[channel] == DSDIFF_CHAN_RS) has_rs = 1;
            }
            if (has_mlft && has_mrgt && has_c && has_lfe && has_ls && has_rs) {
                /* Has all 5.1 channels but wrong order - reject */
                return DSDIFF_ERROR_INVALID_CHANNELS;
            }
            /* Other 6-channel configs allowed (custom layouts) */
        }
    }

    /* Copy channel IDs to handle */
    for (channel = 0; channel < handle->channel_count; channel++) {
        handle->channel_ids[channel] = channel_ids[channel];
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_has_loudspeaker_config(dsdiff_t *handle, int *has_config) {
    if (!handle || !has_config) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *has_config = handle->has_loudspeaker_config;
    return DSDIFF_SUCCESS;
}

int dsdiff_set_loudspeaker_config(dsdiff_t *handle,
                                  dsdiff_loudspeaker_config_t loudspeaker_config) {
    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if ((handle->mode == DSDIFF_FILE_MODE_MODIFY) &&
        (handle->has_loudspeaker_config == 0)) {
        return DSDIFF_ERROR_NO_LSCONFIG;
    }

    handle->loudspeaker_config = loudspeaker_config;
    handle->has_loudspeaker_config = 1;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_loudspeaker_config(dsdiff_t *handle,
                                  dsdiff_loudspeaker_config_t *loudspeaker_config) {
    if (!handle || !loudspeaker_config) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->has_loudspeaker_config == 0) {
        return DSDIFF_ERROR_NO_LSCONFIG;
    }

    *loudspeaker_config = handle->loudspeaker_config;
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Timecode
 * ===========================================================================*/

int dsdiff_get_start_timecode(dsdiff_t *handle,
                              dsdiff_timecode_t *start_timecode) {
    if (!handle || !start_timecode) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    memset(start_timecode, 0, sizeof(*start_timecode));

    if (handle->mode == DSDIFF_FILE_MODE_CLOSED) {
        return DSDIFF_ERROR_NOT_OPEN;
    }

    if (!handle->has_timecode) {
        return DSDIFF_ERROR_NO_TIMECODE;
    }

    *start_timecode = handle->start_timecode;
    return DSDIFF_SUCCESS;
}

int dsdiff_set_start_timecode(dsdiff_t *handle,
                              const dsdiff_timecode_t *start_timecode) {
    if (!handle || !start_timecode) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if ((handle->mode != DSDIFF_FILE_MODE_CLOSED) &&
        (handle->mode != DSDIFF_FILE_MODE_MODIFY)) {
        return DSDIFF_ERROR_POST_CREATE_FORBIDDEN;
    }

    if ((handle->mode == DSDIFF_FILE_MODE_MODIFY) &&
        (!handle->has_timecode)) {
        return DSDIFF_ERROR_NO_TIMECODE;
    }

    handle->start_timecode = *start_timecode;
    handle->has_timecode = 1;
    return DSDIFF_SUCCESS;
}

int dsdiff_has_start_timecode(dsdiff_t *handle, int *has_timecode) {
    if (!handle || !has_timecode) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *has_timecode = handle->has_timecode;
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Comments
 * ===========================================================================*/

int dsdiff_get_comment_count(dsdiff_t *handle, int *comment_count) {
    if (!handle || !comment_count) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *comment_count = handle->comment_count;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_comment(dsdiff_t *handle,
                       int comment_index,
                       dsdiff_comment_t *comment) {
    if (!handle || !comment) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (comment_index < 0 || comment_index >= handle->comment_count) {
        return DSDIFF_ERROR_NO_COMMENT;
    }

    memcpy(comment, &handle->comments[comment_index], sizeof(dsdiff_comment_t));

    return DSDIFF_SUCCESS;
}

int dsdiff_add_comment(dsdiff_t *handle, const dsdiff_comment_t *comment) {
    dsdiff_comment_t *new_comments;
    dsdiff_comment_t *last_comment;
    int ret;

    if (!handle || !comment) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode != DSDIFF_FILE_MODE_WRITE &&
        handle->mode != DSDIFF_FILE_MODE_MODIFY) {
        return DSDIFF_ERROR_MODE_READ_ONLY;
    }

    ret = dsdiff_verify_write_position(handle, handle->comment_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    new_comments = (dsdiff_comment_t *)sa_malloc(
        (handle->comment_count + 1) * sizeof(dsdiff_comment_t));
    if (!new_comments) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    if (handle->comment_count > 0) {
        memcpy(new_comments, handle->comments,
               handle->comment_count * sizeof(dsdiff_comment_t));

        memcpy(&new_comments[handle->comment_count], comment, sizeof(dsdiff_comment_t));

        sa_free(handle->comments);
        handle->comments = new_comments;

        last_comment = &handle->comments[handle->comment_count];

        if (comment->text && comment->text_length > 0) {
            last_comment->text = (char *)sa_malloc(comment->text_length + 1);
            if (last_comment->text) {
                memcpy(last_comment->text, comment->text, comment->text_length);
                last_comment->text[comment->text_length] = '\0';
            } else {
                return DSDIFF_ERROR_OUT_OF_MEMORY;
            }
        } else {
            last_comment->text = NULL;
        }
    }
    else {
        memcpy(new_comments, comment, sizeof(dsdiff_comment_t));
        handle->comments = new_comments;

        if (comment->text && comment->text_length > 0) {
            handle->comments[0].text = (char *)sa_malloc(comment->text_length + 1);
            if (handle->comments[0].text) {
                memcpy(handle->comments[0].text, comment->text, comment->text_length);
                handle->comments[0].text[comment->text_length] = '\0';
            } else {
                return DSDIFF_ERROR_OUT_OF_MEMORY;
            }
        } else {
            handle->comments[0].text = NULL;
        }
    }

    handle->comment_count++;

    return DSDIFF_SUCCESS;
}

int dsdiff_delete_comment(dsdiff_t *handle, int comment_index) {
    dsdiff_comment_t *new_comments = NULL;
    int remaining_count;

    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (comment_index >= handle->comment_count || comment_index < 0) {
        return DSDIFF_SUCCESS;
    }

    if (handle->comment_count > 1) {
        new_comments = (dsdiff_comment_t *)sa_malloc(
            (handle->comment_count - 1) * sizeof(dsdiff_comment_t));

        if (!new_comments) {
            return DSDIFF_ERROR_OUT_OF_MEMORY;
        }

        if (comment_index > 0) {
            memcpy(new_comments, handle->comments,
                   comment_index * sizeof(dsdiff_comment_t));
        }

        remaining_count = handle->comment_count - (comment_index + 1);
        if (remaining_count > 0) {
            memcpy(new_comments + comment_index,
                   handle->comments + (comment_index + 1),
                   remaining_count * sizeof(dsdiff_comment_t));
        }
    }

    if (handle->comments[comment_index].text != NULL) {
        sa_free(handle->comments[comment_index].text);
        handle->comments[comment_index].text = NULL;
    }

    if (handle->comments != NULL) {
        sa_free(handle->comments);
    }

    handle->comments = new_comments;
    handle->comment_count--;

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * ID3 Tags
 * ===========================================================================*/

int dsdiff_get_id3_tag(dsdiff_t *handle,
                       uint8_t **tag_data, uint32_t *tag_size) {
    if (!handle || !tag_data || !tag_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *tag_data = (uint8_t *)sa_malloc(handle->id3_tag_size);
    if (!*tag_data) {
      return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    memcpy(*tag_data, handle->id3_tag, handle->id3_tag_size);
    *tag_size = handle->id3_tag_size;

    return DSDIFF_SUCCESS;
}

int dsdiff_set_id3_tag(dsdiff_t *handle, const uint8_t *tag_data, uint32_t tag_size) {
    int ret;

    if (!handle || !tag_data || tag_size == 0) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode != DSDIFF_FILE_MODE_WRITE &&
        handle->mode != DSDIFF_FILE_MODE_MODIFY) {
        return DSDIFF_ERROR_MODE_READ_ONLY;
    }

    ret = dsdiff_verify_write_position(handle, handle->id3_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    handle->id3_tag = (uint8_t *)sa_malloc(tag_size);
    if (handle->id3_tag) {
        memcpy(handle->id3_tag, tag_data, tag_size);
    } else {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }
    handle->id3_tag_size = tag_size;

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Per-Track ID3 Tags (Edit Master Mode)
 * ===========================================================================*/

int dsdiff_get_track_id3_count(dsdiff_t *handle, uint32_t *count) {
    if (!handle || !count) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *count = handle->track_id3_count;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_track_id3_tag(dsdiff_t *handle, uint32_t track_index,
                              uint8_t **tag_data, uint32_t *tag_size) {
    if (!handle || !tag_data || !tag_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (track_index >= handle->track_id3_count) {
        return DSDIFF_ERROR_TRACK_INDEX_INVALID;
    }

    if (handle->track_id3_tags[track_index] == NULL ||
        handle->track_id3_sizes[track_index] == 0) {
        return DSDIFF_ERROR_NO_TRACK_ID3;
    }

    uint32_t size = handle->track_id3_sizes[track_index];
    *tag_data = (uint8_t *)sa_malloc(size);
    if (!*tag_data) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    memcpy(*tag_data, handle->track_id3_tags[track_index], size);
    *tag_size = size;

    return DSDIFF_SUCCESS;
}

int dsdiff_set_track_id3_tag(dsdiff_t *handle, uint32_t track_index,
                              const uint8_t *tag_data, uint32_t tag_size) {
    if (!handle || !tag_data || tag_size == 0) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode != DSDIFF_FILE_MODE_WRITE &&
        handle->mode != DSDIFF_FILE_MODE_MODIFY) {
        return DSDIFF_ERROR_MODE_READ_ONLY;
    }

    /* Grow arrays if needed */
    uint32_t new_count = track_index + 1;
    if (new_count > handle->track_id3_count) {
        uint8_t **new_tags = (uint8_t **)sa_realloc(
            handle->track_id3_tags, new_count * sizeof(uint8_t *));
        if (!new_tags) {
            return DSDIFF_ERROR_OUT_OF_MEMORY;
        }

        uint32_t *new_sizes = (uint32_t *)sa_realloc(
            handle->track_id3_sizes, new_count * sizeof(uint32_t));
        if (!new_sizes) {
            /* Rollback tags realloc - but we can't shrink it back easily,
             * so just keep the larger array */
            handle->track_id3_tags = new_tags;
            return DSDIFF_ERROR_OUT_OF_MEMORY;
        }

        /* Zero-initialize new entries */
        for (uint32_t i = handle->track_id3_count; i < new_count; i++) {
            new_tags[i] = NULL;
            new_sizes[i] = 0;
        }

        handle->track_id3_tags = new_tags;
        handle->track_id3_sizes = new_sizes;
        handle->track_id3_count = new_count;
    }

    /* Free existing data if present */
    if (handle->track_id3_tags[track_index] != NULL) {
        sa_free(handle->track_id3_tags[track_index]);
    }

    /* Allocate and copy new data */
    handle->track_id3_tags[track_index] = (uint8_t *)sa_malloc(tag_size);
    if (!handle->track_id3_tags[track_index]) {
        handle->track_id3_sizes[track_index] = 0;
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    memcpy(handle->track_id3_tags[track_index], tag_data, tag_size);
    handle->track_id3_sizes[track_index] = tag_size;

    return DSDIFF_SUCCESS;
}

int dsdiff_clear_track_id3_tag(dsdiff_t *handle, uint32_t track_index) {
    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (track_index >= handle->track_id3_count) {
        return DSDIFF_ERROR_TRACK_INDEX_INVALID;
    }

    if (handle->track_id3_tags[track_index] != NULL) {
        sa_free(handle->track_id3_tags[track_index]);
        handle->track_id3_tags[track_index] = NULL;
    }
    handle->track_id3_sizes[track_index] = 0;

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Manufacturer Specific Data
 * ===========================================================================*/

int dsdiff_has_manufacturer(dsdiff_t *handle, int *has_manufacturer)
{
    if (!handle || !has_manufacturer) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *has_manufacturer = handle->has_manufacturer;
    return DSDIFF_SUCCESS;
}

int dsdiff_get_manufacturer(dsdiff_t *handle, dsdiff_manufacturer_t *manufacturer)
{
    if (!handle || !manufacturer) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (!handle->has_manufacturer) {
        return DSDIFF_ERROR_NO_MANUFACTURER;
    }

    memcpy(manufacturer->man_id, handle->manufacturer_id, 4);
    manufacturer->data_size = handle->manufacturer_data_size;
    manufacturer->data = handle->manufacturer_data;

    return DSDIFF_SUCCESS;
}

int dsdiff_set_manufacturer(dsdiff_t *handle, const dsdiff_manufacturer_t *manufacturer)
{
    int ret;
    uint8_t *new_data = NULL;

    if (!handle || !manufacturer) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode != DSDIFF_FILE_MODE_WRITE &&
        handle->mode != DSDIFF_FILE_MODE_MODIFY) {
        return DSDIFF_ERROR_MODE_READ_ONLY;
    }

    ret = dsdiff_verify_write_position(handle, handle->manufacturer_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    /* Allocate and copy new data */
    if (manufacturer->data_size > 0 && manufacturer->data != NULL) {
        new_data = (uint8_t *)sa_malloc(manufacturer->data_size);
        if (!new_data) {
            return DSDIFF_ERROR_OUT_OF_MEMORY;
        }
        memcpy(new_data, manufacturer->data, manufacturer->data_size);
    }

    /* Free old data */
    if (handle->manufacturer_data != NULL) {
        sa_free(handle->manufacturer_data);
    }

    /* Set new values */
    memcpy(handle->manufacturer_id, manufacturer->man_id, 4);
    handle->manufacturer_data = new_data;
    handle->manufacturer_data_size = manufacturer->data_size;
    handle->has_manufacturer = 1;

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * DSD Markers
 * ===========================================================================*/

int dsdiff_get_dsd_marker_count(dsdiff_t *handle, int *marker_count) {
    if (!handle || !marker_count) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *marker_count = (int)dsdiff_marker_list_get_count(&handle->markers);
    return DSDIFF_SUCCESS;
}

int dsdiff_get_dsd_marker(dsdiff_t *handle,
                          int marker_index,
                          dsdiff_marker_t *marker) {
    int marker_count;

    if (!handle || !marker) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    marker_count = (int)dsdiff_marker_list_get_count(&handle->markers);

    if (marker_index < marker_count) {
        return dsdiff_marker_list_get(&handle->markers, (uint32_t)marker_index,
                                      marker, NULL);
    } else {
        return DSDIFF_ERROR_NO_MARKER;
    }
}

int dsdiff_add_dsd_marker(dsdiff_t *handle, const dsdiff_marker_t *marker) {
    int ret;

    if (!handle || !marker) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode != DSDIFF_FILE_MODE_WRITE &&
        handle->mode != DSDIFF_FILE_MODE_MODIFY) {
        return DSDIFF_ERROR_MODE_READ_ONLY;
    }

    ret = dsdiff_verify_write_position(handle, handle->diin_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return dsdiff_marker_list_add(&handle->markers, marker, handle->sample_rate);
}

int dsdiff_delete_dsd_marker(dsdiff_t *handle, int marker_index) {
    int marker_count;

    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    marker_count = (int)dsdiff_marker_list_get_count(&handle->markers);
    if (marker_index >= marker_count || marker_index < 0) {
        return DSDIFF_SUCCESS;
    }

    return dsdiff_marker_list_remove(&handle->markers, (uint32_t)marker_index);
}

int dsdiff_sort_dsd_markers(dsdiff_t *handle, int sort_type) {
    int marker_count;

    (void)sort_type;

    if (!handle) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    marker_count = (int)dsdiff_marker_list_get_count(&handle->markers);
    if (marker_count > 1) {
        dsdiff_marker_list_sort(&handle->markers);

        if (handle->mode == DSDIFF_FILE_MODE_MODIFY) {
            handle->diin_chunk_pos = 0;
        }
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Detailed Information (DIIN Hierarchy)
 * ===========================================================================*/

int dsdiff_has_emid(dsdiff_t *handle, int *has_emid) {
    if (!handle || !has_emid) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *has_emid = 0;

    if (handle->has_emid && handle->emid != NULL) {
        *has_emid = (int)(strlen(handle->emid) + 1);
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_get_emid(dsdiff_t *handle,
                    uint32_t *length,
                    char *emid) {
    uint32_t copy_size;

    if (!handle || !length || !emid) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->has_emid && handle->emid != NULL) {
        copy_size = (uint32_t)(strlen(handle->emid) + 1);

        if (copy_size > *length) {
            copy_size = *length;
        }

        memcpy(emid, handle->emid, copy_size);

        *length = copy_size;
    } else {
        return DSDIFF_ERROR_NO_EMID;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_set_emid(dsdiff_t *handle, const char *emid) {
    int ret;
    uint32_t copy_size;

    if (!handle || !emid) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode != DSDIFF_FILE_MODE_WRITE) {
        if (handle->mode != DSDIFF_FILE_MODE_MODIFY) {
            return DSDIFF_ERROR_MODE_READ_ONLY;
        }
    }

    ret = dsdiff_verify_write_position(handle, handle->diin_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (handle->has_emid && handle->emid != NULL) {
        sa_free(handle->emid);
        handle->emid = NULL;
    }

    copy_size = (uint32_t)(strlen(emid) + 1);
    handle->emid = (char *)sa_malloc(copy_size);
    if (handle->emid == NULL) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    memcpy(handle->emid, emid, copy_size);
    handle->has_emid = 1;

    return DSDIFF_SUCCESS;
}

int dsdiff_has_disc_artist(dsdiff_t *handle, int *has_artist) {
    if (!handle || !has_artist) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *has_artist = 0;

    if (handle->has_disc_artist && handle->disc_artist != NULL) {
        *has_artist = (int)(strlen(handle->disc_artist) + 1);
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_get_disc_artist(dsdiff_t *handle,
                           uint32_t *length,
                           char *disc_artist) {
    uint32_t copy_size;

    if (!handle || !length || !disc_artist) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->has_disc_artist && handle->disc_artist != NULL) {
        copy_size = (uint32_t)(strlen(handle->disc_artist) + 1);

        if (copy_size > *length) {
            copy_size = *length;
        }

        memcpy(disc_artist, handle->disc_artist, copy_size);

        *length = copy_size;
    } else {
        return DSDIFF_ERROR_NO_ARTIST;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_set_disc_artist(dsdiff_t *handle, const char *disc_artist) {
    uint32_t copy_size;
    int ret;

    if (!handle || !disc_artist) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode != DSDIFF_FILE_MODE_WRITE) {
        if (handle->mode != DSDIFF_FILE_MODE_MODIFY) {
            return DSDIFF_ERROR_MODE_READ_ONLY;
        }
    }

    ret = dsdiff_verify_write_position(handle, handle->diin_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (handle->has_disc_artist && handle->disc_artist != NULL) {
        sa_free(handle->disc_artist);
        handle->disc_artist = NULL;
    }

    copy_size = (uint32_t)(strlen(disc_artist) + 1);
    handle->disc_artist = (char *)sa_malloc(copy_size);
    if (handle->disc_artist == NULL) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    memcpy(handle->disc_artist, disc_artist, copy_size);
    handle->has_disc_artist = 1;

    return DSDIFF_SUCCESS;
}

int dsdiff_has_disc_title(dsdiff_t *handle, int *has_title) {
    if (!handle || !has_title) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *has_title = 0;

    if (handle->has_disc_title && handle->disc_title != NULL) {
        *has_title = (int)(strlen(handle->disc_title) + 1);
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_get_disc_title(dsdiff_t *handle,
                          uint32_t *length,
                          char *disc_title) {
    uint32_t copy_size;

    if (!handle || !length || !disc_title) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->has_disc_title && handle->disc_title != NULL) {
        copy_size = (uint32_t)(strlen(handle->disc_title) + 1);

        if (copy_size > *length) {
            copy_size = *length;
        }

        memcpy(disc_title, handle->disc_title, copy_size);

        *length = copy_size;
    } else {
        return DSDIFF_ERROR_NO_TITLE;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_set_disc_title(dsdiff_t *handle, const char *disc_title) {
    uint32_t copy_size;
    int ret;

    if (!handle || !disc_title) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (handle->mode != DSDIFF_FILE_MODE_WRITE) {
        if (handle->mode != DSDIFF_FILE_MODE_MODIFY) {
            return DSDIFF_ERROR_MODE_READ_ONLY;
        }
    }

    ret = dsdiff_verify_write_position(handle, handle->diin_chunk_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (handle->has_disc_title && handle->disc_title != NULL) {
        sa_free(handle->disc_title);
        handle->disc_title = NULL;
    }

    copy_size = (uint32_t)(strlen(disc_title) + 1);
    handle->disc_title = (char *)sa_malloc(copy_size);
    if (handle->disc_title == NULL) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    memcpy(handle->disc_title, disc_title, copy_size);
    handle->has_disc_title = 1;

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Internal: Handle Initialization
 * ===========================================================================*/

static void dsdiff_init_handle(dsdiff_t *handle) {
    if (!handle) {
        return;
    }

    /* File state */
    handle->mode = DSDIFF_FILE_MODE_CLOSED;
    handle->format_version = DSDIFF_FILE_VERSION_15;

    /* Audio format */
    handle->channel_count = 1;
    if (handle->channel_ids != NULL) {
        sa_free(handle->channel_ids);
        handle->channel_ids = NULL;
    }
    handle->channel_chunk_pos = 0;
    handle->sample_rate = 0;
    handle->is_dst_format = 0;

    /* Audio data tracking */
    handle->sample_frame_count = 0;
    handle->sample_frame_capacity = 0;
    handle->prop_chunk_size = 0;
    handle->sound_data_size = 0;
    handle->sound_data_start_pos = 0;
    handle->sound_data_end_pos = 0;

    /* Timecode (ABSS chunk) */
    handle->has_timecode = 0;
    handle->timecode_chunk_pos = 0;

    /* Loudspeaker config (LSCO chunk) */
    handle->has_loudspeaker_config = 0;
    handle->loudspeaker_config = DSDIFF_LS_CONFIG_STEREO;
    handle->loudspeaker_chunk_pos = 0;

    /* DST compressed format */
    handle->dst_frame_rate = 75;
    handle->has_crc = 0;
    handle->crc_size = 0;
    handle->dst_data_end = 0;
    handle->reserved_index_count = 0;
    handle->indexes = NULL;
    handle->has_index = 0;

    /* Comments (COMT chunk) */
    handle->comment_count = 0;
    handle->comments = NULL;
    handle->comment_chunk_pos = 0;

    /* ID3 tags */
    handle->id3_tag_size = 0;
    handle->id3_tag = NULL;
    handle->id3_chunk_pos = 0;

    /* Manufacturer data (MANF chunk) */
    handle->has_manufacturer = 0;
    memset(handle->manufacturer_id, 0, sizeof(handle->manufacturer_id));
    handle->manufacturer_data_size = 0;
    handle->manufacturer_data = NULL;
    handle->manufacturer_chunk_pos = 0;

    /* Disc info (DIIN chunk) */
    handle->diin_chunk_pos = 0;
    handle->diin_file_start = 0;
    handle->diin_file_end = 0;
    handle->has_disc_artist = 0;
    handle->disc_artist = NULL;
    handle->has_disc_title = 0;
    handle->disc_title = NULL;
    handle->has_emid = 0;
    handle->emid = NULL;

    /* Markers */
    dsdiff_marker_list_init(&handle->markers);

    /* Finalization state */
    handle->file_size_after_finalize = 0;
}

static void dsdiff_cleanup_handle(dsdiff_t *handle) {
    if (!handle) {
        return;
    }

    if (handle->channel_ids != NULL) {
        sa_free(handle->channel_ids);
        handle->channel_ids = NULL;
    }

    if (handle->indexes != NULL) {
        sa_free(handle->indexes);
        handle->indexes = NULL;
    }

    if (handle->comments != NULL) {
        for (int i = 0; i < handle->comment_count; i++) {
            if (handle->comments[i].text != NULL) {
                sa_free(handle->comments[i].text);
            }
        }
        sa_free(handle->comments);
        handle->comments = NULL;
    }

    if (handle->disc_artist != NULL) {
        sa_free(handle->disc_artist);
        handle->disc_artist = NULL;
    }
    if (handle->disc_title != NULL) {
        sa_free(handle->disc_title);
        handle->disc_title = NULL;
    }
    if (handle->emid != NULL) {
        sa_free(handle->emid);
        handle->emid = NULL;
    }

    if (handle->manufacturer_data != NULL) {
        sa_free(handle->manufacturer_data);
        handle->manufacturer_data = NULL;
    }

    /* Free per-track ID3 tags */
    if (handle->track_id3_tags != NULL) {
        for (uint32_t i = 0; i < handle->track_id3_count; i++) {
            if (handle->track_id3_tags[i] != NULL) {
                sa_free(handle->track_id3_tags[i]);
            }
        }
        sa_free(handle->track_id3_tags);
        handle->track_id3_tags = NULL;
    }
    if (handle->track_id3_sizes != NULL) {
        sa_free(handle->track_id3_sizes);
        handle->track_id3_sizes = NULL;
    }
    handle->track_id3_count = 0;

    dsdiff_marker_list_free(&handle->markers);

    dsdiff_init_handle(handle);
}

/* =============================================================================
 * Internal: Validation Helpers
 * ===========================================================================*/

static int dsdiff_is_chunk_writable(dsdiff_t *handle, uint64_t position) {
    if (position == 0) {
        return 1;
    }

    if (handle->mode == DSDIFF_FILE_MODE_MODIFY) {
        if (handle->is_dst_format) {
            if (position < handle->dst_data_end) {
                return 0;
            }
        } else {
            if (position < handle->sound_data_end_pos) {
                return 0;
            }
        }
    }

    return 1;
}

static int dsdiff_verify_write_position(dsdiff_t *handle, uint64_t position) {
    if (dsdiff_is_chunk_writable(handle, position) == 0) {
        return DSDIFF_ERROR_CHUNK_LOCKED;
    }
    return DSDIFF_SUCCESS;
}

static int dsdiff_read_index(dsdiff_t *handle) {
    int ret;

    if (!handle->has_index ||
        handle->dst_frame_count == 0 ||
        handle->reserved_index_count != 0) {
        return DSDIFF_SUCCESS;
    }

    handle->indexes = (dsdiff_index_t *)sa_malloc(
        handle->dst_frame_count * sizeof(dsdiff_index_t));
    if (handle->indexes == NULL) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    handle->reserved_index_count = handle->dst_frame_count;

    ret = dsdiff_chunk_read_dsti_contents(handle->io,
                                          handle->index_file_start,
                                          handle->dst_frame_count,
                                          handle->indexes);
    if (ret != DSDIFF_SUCCESS) {
        sa_free(handle->indexes);
        handle->indexes = NULL;
        handle->reserved_index_count = 0;
        return ret;
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Internal: File Parsing
 * ===========================================================================*/

static int dsdiff_parse_file(dsdiff_t *handle) {
    int ret;
    uint64_t chunk_size;
    dsdiff_chunk_type_t chunk_id;
    dsdiff_audio_type_t file_type;
    uint64_t file_size;

    if (!handle || !handle->io) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_chunk_read_header(handle->io, &chunk_id);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (chunk_id != DSDIFF_CHUNK_FRM8) {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    ret = dsdiff_chunk_read_frm8_header(handle->io, &chunk_size, &file_type);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_get_file_size(handle->io, &file_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (file_size < chunk_size) {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    if (file_type == DSDIFF_AUDIO_DST) {
        handle->is_dst_format = 1;
    } else if (file_type == DSDIFF_AUDIO_DSD) {
        handle->is_dst_format = 0;
    } else {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    ret = dsdiff_parse_frm8(handle, chunk_size);

    return ret;
}

static int dsdiff_validate_version(dsdiff_t *handle) {
    (void)handle;
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Internal: File Writing / Finalization
 * ===========================================================================*/

static int dsdiff_write_index(dsdiff_t *handle) {
    int ret = DSDIFF_SUCCESS;

    if (handle->is_dst_format && handle->dst_frame_count && (handle->mode != DSDIFF_FILE_MODE_MODIFY)) {
        ret = dsdiff_chunk_write_dsti_contents(handle->io, handle->dst_frame_count, handle->indexes);
    }

    return ret;
}

static int dsdiff_write_diin(dsdiff_t *handle) {
    int ret = DSDIFF_SUCCESS;
    uint64_t cur_file_pos = 0;
    uint64_t end_file_pos = 0;
    uint64_t diin_size = 0;
    uint64_t diin_data_start, diin_data_end;
    uint64_t discard_pos;
    int write_diin;

    if ((handle->mode != DSDIFF_FILE_MODE_WRITE) && (handle->mode != DSDIFF_FILE_MODE_MODIFY)) {
        return DSDIFF_ERROR_WRITE_FAILED;
    }

    write_diin = (handle->has_emid || handle->has_disc_artist || handle->has_disc_title ||
                  (dsdiff_marker_list_get_count(&handle->markers) > 0));

    if (!write_diin) {
        return ret;
    }

    ret = dsdiff_io_get_position(handle->io, &cur_file_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_chunk_write_diin_header(handle->io, diin_size, &diin_data_start, &diin_data_end);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (handle->has_emid) {
        ret = dsdiff_chunk_write_emid(handle->io, strlen(handle->emid), handle->emid);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    uint32_t marker_count = dsdiff_marker_list_get_count(&handle->markers);
    for (uint32_t i = 0; i < marker_count; i++) {
        dsdiff_marker_t *marker = dsdiff_marker_create();
        if (marker && dsdiff_marker_list_get(&handle->markers, i, marker, 0) == 0) {
            ret = dsdiff_chunk_write_mark(handle->io, marker);
            dsdiff_marker_free(marker);
            if (ret != DSDIFF_SUCCESS) {
                return ret;
            }
        }
    }

    if (handle->has_disc_artist) {
        ret = dsdiff_chunk_write_diar(handle->io, strlen(handle->disc_artist), handle->disc_artist);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    if (handle->has_disc_title) {
        ret = dsdiff_chunk_write_diti(handle->io, strlen(handle->disc_title), handle->disc_title);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    ret = dsdiff_io_get_position(handle->io, &end_file_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    diin_size = end_file_pos - cur_file_pos - 12;

    ret = dsdiff_io_seek(handle->io, cur_file_pos, DSDIFF_SEEK_SET, &discard_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_chunk_write_diin_header(handle->io, diin_size, &diin_data_start, &diin_data_end);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_seek(handle->io, end_file_pos, DSDIFF_SEEK_SET, &discard_pos);

    return ret;
}

static int dsdiff_parse_frm8(dsdiff_t *handle, uint64_t chunk_size) {
    int ret = DSDIFF_SUCCESS;
    dsdiff_chunk_type_t chunk_id;
    uint64_t chunk_data_size;
    uint64_t file_pos;
    int fver_count = 0, prop_count = 0, dsd_count = 0, dst_count = 0, comt_count = 0, diin_count = 0, frm8_count = 0;
    uint32_t id3_count = 0;  /* Track number of ID3 chunks for per-track storage */

    /*
     * Parse top-level chunks within FRM8 container.
     * Expected structure:
     *   FVER - Format version (required, exactly 1)
     *   PROP - Properties chunk containing FS, CHNL, CMPR, etc.
     *   DSD  - Uncompressed audio data (mutually exclusive with DST)
     *   DST  - DST-compressed audio data (mutually exclusive with DSD)
     *   DIIN - Disc information (optional)
     *   COMT - Comments (optional)
     *   ID3  - ID3v2 metadata (optional)
     *   MANF - Manufacturer data (optional)
     */
    while (ret == DSDIFF_SUCCESS) {
        ret = dsdiff_io_get_position(handle->io, &file_pos);
        if (ret != DSDIFF_SUCCESS) {
            break;
        }

        /* Stop when we've read past the FRM8 chunk boundary */
        if (file_pos > chunk_size) {
            ret = DSDIFF_SUCCESS;
            break;
        }

        ret = dsdiff_chunk_read_header(handle->io, &chunk_id);
        if (ret != DSDIFF_SUCCESS) {
            /* EOF or read error at chunk boundary is OK - we're done */
            ret = DSDIFF_SUCCESS;
            break;
        }

        switch (chunk_id) {
            case DSDIFF_CHUNK_FRM8:
                /* Nested FRM8 not allowed at top level */
                frm8_count++;
                break;

            case DSDIFF_CHUNK_FVER:
                /* Format version - only DSDIFF 1.x is supported */
                ret = dsdiff_chunk_read_fver(handle->io, &handle->format_version);
                if (ret == DSDIFF_SUCCESS) {
                    uint8_t major_version, minor_version;
                    dsdiff_get_format_version(handle, &major_version, &minor_version);
                    if (major_version != 1) {
                        return DSDIFF_ERROR_INVALID_VERSION;
                    }
                }
                fver_count++;
                break;

            case DSDIFF_CHUNK_PROP:
                /* Properties - delegates to dsdiff_parse_prop for sub-chunks */
                ret = dsdiff_chunk_read_prop_header(handle->io, &chunk_data_size);
                if (ret == DSDIFF_SUCCESS) {
                    ret = dsdiff_parse_prop(handle, chunk_data_size);
                }
                prop_count++;
                break;

            case DSDIFF_CHUNK_DIIN:
                /* Disc information - contains EMID, DIAR, DITI */
                {
                    uint64_t diin_data_start, diin_data_end;
                    ret = dsdiff_io_get_position(handle->io, &handle->diin_chunk_pos);
                    if (ret == DSDIFF_SUCCESS) {
                        handle->diin_chunk_pos -= 4;
                        ret = dsdiff_chunk_read_diin_header(handle->io, &chunk_data_size, &diin_data_start, &diin_data_end);
                        if (ret == DSDIFF_SUCCESS) {
                            ret = dsdiff_parse_diin(handle, chunk_data_size);
                        }
                    }
                    diin_count++;
                }
                break;

            case DSDIFF_CHUNK_DSD:
                /* Uncompressed DSD audio data */
                ret = dsdiff_chunk_read_snd_header(
                    handle->io, &handle->sound_data_size,
                    &handle->sound_data_start_pos, &handle->sound_data_end_pos);
                if (ret == DSDIFF_SUCCESS) {
                    handle->sample_frame_count = handle->sound_data_size / handle->channel_count;
                }
                dsd_count++;
                break;

            case DSDIFF_CHUNK_DST:
                /* DST-compressed audio - delegates to dsdiff_parse_dst */
                ret = dsdiff_parse_dst(handle);
                if (ret == DSDIFF_SUCCESS) {
                    ret = dsdiff_io_get_position(handle->io, &handle->dst_data_end);
                }
                dst_count++;
                break;

            case DSDIFF_CHUNK_DSTI:
                /* DST frame index - enables random access for DST files */
                ret = dsdiff_chunk_read_dsti_header(handle->io, &handle->index_file_size,
                                                    &handle->index_file_start, &handle->index_file_end);
                if (ret == DSDIFF_SUCCESS) {
                    ret = dsdiff_io_get_position(handle->io, &handle->dst_data_end);
                    if (ret == DSDIFF_SUCCESS) {
                        handle->has_index = 1;
                    }
                }
                dst_count++;
                break;

            case DSDIFF_CHUNK_COMT:
                /* Comments chunk */
                ret = dsdiff_io_get_position(handle->io, &handle->comment_chunk_pos);
                if (ret == DSDIFF_SUCCESS) {
                    handle->comment_chunk_pos -= 4;
                    ret = dsdiff_chunk_read_comt(
                        handle->io, &handle->comment_count, &handle->comments);
                }
                comt_count++;
                break;

            case DSDIFF_CHUNK_ID3:
                /* ID3v2 metadata tag - first goes to file-level, rest to per-track */
                if (id3_count == 0) {
                    /* First ID3 chunk: store as file-level ID3 */
                    ret = dsdiff_io_get_position(handle->io, &handle->id3_chunk_pos);
                    if (ret == DSDIFF_SUCCESS) {
                        handle->id3_chunk_pos -= 4;
                        ret = dsdiff_chunk_read_id3(
                            handle->io, &handle->id3_tag, &handle->id3_tag_size);
                    }
                } else {
                    /* Subsequent ID3 chunks: store as per-track ID3 */
                    uint8_t *track_tag = NULL;
                    uint32_t track_tag_size = 0;
                    ret = dsdiff_chunk_read_id3(handle->io, &track_tag, &track_tag_size);
                    if (ret == DSDIFF_SUCCESS && track_tag != NULL) {
                        uint32_t track_idx = id3_count - 1;
                        /* Grow arrays if needed */
                        if (track_idx >= handle->track_id3_count) {
                            uint32_t new_count = track_idx + 1;
                            uint8_t **new_tags = (uint8_t **)sa_realloc(
                                handle->track_id3_tags, new_count * sizeof(uint8_t *));
                            uint32_t *new_sizes = (uint32_t *)sa_realloc(
                                handle->track_id3_sizes, new_count * sizeof(uint32_t));
                            if (new_tags && new_sizes) {
                                for (uint32_t j = handle->track_id3_count; j < new_count; j++) {
                                    new_tags[j] = NULL;
                                    new_sizes[j] = 0;
                                }
                                handle->track_id3_tags = new_tags;
                                handle->track_id3_sizes = new_sizes;
                                handle->track_id3_count = new_count;
                            } else {
                                sa_free(track_tag);
                                if (new_tags) handle->track_id3_tags = new_tags;
                                ret = DSDIFF_ERROR_OUT_OF_MEMORY;
                            }
                        }
                        if (ret == DSDIFF_SUCCESS) {
                            handle->track_id3_tags[track_idx] = track_tag;
                            handle->track_id3_sizes[track_idx] = track_tag_size;
                        }
                    }
                }
                id3_count++;
                break;

            case DSDIFF_CHUNK_MANF:
                /* Manufacturer-specific data */
                ret = dsdiff_io_get_position(handle->io, &handle->manufacturer_chunk_pos);
                if (ret == DSDIFF_SUCCESS) {
                    handle->manufacturer_chunk_pos -= 4;
                    ret = dsdiff_chunk_read_manf(handle->io,
                                                 handle->manufacturer_id,
                                                 &handle->manufacturer_data,
                                                 &handle->manufacturer_data_size);
                    if (ret == DSDIFF_SUCCESS) {
                        handle->has_manufacturer = 1;
                    }
                }
                break;

            default:
                /* Unknown chunk - skip it */
                ret = dsdiff_chunk_skip(handle->io);
                break;
        }

    }

    /*
     * Validate chunk counts according to DSDIFF specification.
     * - Nested FRM8 not allowed
     * - Exactly one FVER required
     * - At most one of: PROP, COMT, DIIN
     * - Either DSD or DST but not both
     */
    if (frm8_count != 0) {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    if (fver_count != 1) {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    if (prop_count > 1 || comt_count > 1 || diin_count > 1) {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    /* DSD and DST are mutually exclusive */
    if (!handle->is_dst_format) {
        if (dsd_count != 1 || dst_count != 0) {
            return DSDIFF_ERROR_INVALID_FILE;
        }
    } else {
        if (dst_count != 1 || dsd_count != 0) {
            return DSDIFF_ERROR_INVALID_FILE;
        }
    }

    return DSDIFF_SUCCESS;
}

static int dsdiff_parse_prop(dsdiff_t *handle, uint64_t chunk_size) {
    int ret;
    dsdiff_chunk_type_t chunk_id;
    int fs_count = 0, chnl_count = 0, cmpr_count = 0, abss_count = 0, lsco_count = 0;
    uint64_t start_pos, current_pos, end_pos;
    dsdiff_audio_type_t compression_type;
    char compression_name[256];

    handle->prop_chunk_size = chunk_size;

    ret = dsdiff_io_get_position(handle->io, &start_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    end_pos = start_pos + chunk_size - 4;

    while (1) {
        ret = dsdiff_io_get_position(handle->io, &current_pos);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        if (current_pos >= end_pos) {
            return (current_pos == end_pos) ? DSDIFF_SUCCESS : DSDIFF_ERROR_UNEXPECTED_EOF;
        }

        ret = dsdiff_chunk_read_header(handle->io, &chunk_id);
        if (ret != DSDIFF_SUCCESS) {
            break;
        }

        switch (chunk_id) {
            case DSDIFF_CHUNK_FS:
                ret = dsdiff_chunk_read_fs(handle->io, &handle->sample_rate);
                fs_count++;
                break;

            case DSDIFF_CHUNK_CHNL:
                ret = dsdiff_io_get_position(handle->io, &handle->channel_chunk_pos);
                if (ret == DSDIFF_SUCCESS) {
                    handle->channel_chunk_pos -= 4;
                    ret = dsdiff_chunk_read_chnl(handle->io, &handle->channel_count, &handle->channel_ids);
                }
                chnl_count++;
                break;

            case DSDIFF_CHUNK_CMPR:
                ret = dsdiff_chunk_read_cmpr(handle->io, &compression_type, compression_name, sizeof(compression_name));
                if (ret == DSDIFF_SUCCESS) {
                    if (compression_type != DSDIFF_AUDIO_DSD &&
                        compression_type != DSDIFF_AUDIO_DST) {
                        return DSDIFF_ERROR_UNSUPPORTED_COMPRESSION;
                    }
                    handle->is_dst_format = (compression_type == DSDIFF_AUDIO_DST);
                }
                cmpr_count++;
                break;

            case DSDIFF_CHUNK_ABSS:
                ret = dsdiff_io_get_position(handle->io, &handle->timecode_chunk_pos);
                if (ret == DSDIFF_SUCCESS) {
                    handle->timecode_chunk_pos -= 4;
                    ret = dsdiff_chunk_read_abss(handle->io, &handle->start_timecode);
                    if (ret == DSDIFF_SUCCESS) {
                        handle->has_timecode = 1;
                    }
                }
                abss_count++;
                break;

            case DSDIFF_CHUNK_LSCO:
                ret = dsdiff_io_get_position(handle->io, &handle->loudspeaker_chunk_pos);
                if (ret == DSDIFF_SUCCESS) {
                    handle->loudspeaker_chunk_pos -= 4;
                    ret = dsdiff_chunk_read_lsco(handle->io, &handle->loudspeaker_config);
                    if (ret == DSDIFF_SUCCESS) {
                        handle->has_loudspeaker_config = 1;
                    }
                }
                lsco_count++;
                break;

            default:
                ret = dsdiff_chunk_skip(handle->io);
                break;
        }

        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    if (fs_count != 1 || chnl_count != 1 || cmpr_count != 1) {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    if (abss_count > 1 || lsco_count > 1) {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    return DSDIFF_SUCCESS;
}

static int dsdiff_parse_dst(dsdiff_t *handle) {
    int ret;
    dsdiff_chunk_type_t chunk_id;
    uint64_t dst_data_size, start_pos, end_pos;

    ret = dsdiff_chunk_read_dst_header(handle->io, &dst_data_size, &start_pos, &end_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    handle->sound_data_start_pos = start_pos + 22;

    ret = dsdiff_chunk_read_header(handle->io, &chunk_id);
    if (ret != DSDIFF_SUCCESS || chunk_id != DSDIFF_CHUNK_FRTE) {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    ret = dsdiff_chunk_read_frte(handle->io, &handle->dst_frame_count, &handle->dst_frame_rate);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (handle->dst_frame_count < 2) {
        return DSDIFF_SUCCESS;
    }

    ret = dsdiff_chunk_read_header(handle->io, &chunk_id);
    if (ret != DSDIFF_SUCCESS || chunk_id != DSDIFF_CHUNK_DSTF) {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    ret = dsdiff_chunk_skip(handle->io);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_chunk_read_header(handle->io, &chunk_id);
    if (ret == DSDIFF_SUCCESS && chunk_id == DSDIFF_CHUNK_DSTC) {
        uint64_t dstc_size;
        handle->has_crc = 1;
        ret = dsdiff_chunk_get_dstc_size(handle->io, &dstc_size);
        if (ret == DSDIFF_SUCCESS) {
            handle->crc_size = (uint32_t)dstc_size;
            ret = dsdiff_chunk_skip(handle->io);
            if (ret != DSDIFF_SUCCESS) {
                return ret;
            }
        }
    }

    return DSDIFF_SUCCESS;
}

static int dsdiff_parse_diin(dsdiff_t *handle, uint64_t chunk_size) {
    int ret;
    dsdiff_chunk_type_t chunk_id;
    uint64_t start_pos, current_pos;
    uint64_t end_pos;

    ret = dsdiff_io_get_position(handle->io, &start_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    end_pos = start_pos + chunk_size;

    while (1) {
        ret = dsdiff_io_get_position(handle->io, &current_pos);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        if (current_pos >= end_pos) {
            return (current_pos == end_pos) ? DSDIFF_SUCCESS : DSDIFF_ERROR_UNEXPECTED_EOF;
        }

        ret = dsdiff_chunk_read_header(handle->io, &chunk_id);
        if (ret != DSDIFF_SUCCESS) {
            return DSDIFF_SUCCESS;
        }

        switch (chunk_id) {
            case DSDIFF_CHUNK_EMID:
                ret = dsdiff_chunk_read_emid(handle->io, &handle->emid);
                if (ret == DSDIFF_SUCCESS) {
                    handle->has_emid = 1;
                }
                break;

            case DSDIFF_CHUNK_DIAR:
                ret = dsdiff_chunk_read_diar(handle->io, &handle->disc_artist);
                if (ret == DSDIFF_SUCCESS) {
                    handle->has_disc_artist = 1;
                }
                break;

            case DSDIFF_CHUNK_DITI:
                ret = dsdiff_chunk_read_diti(handle->io, &handle->disc_title);
                if (ret == DSDIFF_SUCCESS) {
                    handle->has_disc_title = 1;
                }
                break;

            case DSDIFF_CHUNK_MARK:
                {
                    dsdiff_marker_t *marker = dsdiff_marker_create();
                    if (marker) {
                        ret = dsdiff_chunk_read_mark(handle->io, marker);
                        if (ret == DSDIFF_SUCCESS) {
                            dsdiff_marker_list_add(&handle->markers, marker, handle->sample_rate);
                        }
                        dsdiff_marker_free(marker);
                    }
                }
                break;

            default:
                ret = dsdiff_chunk_skip(handle->io);
                break;
        }

        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }
}

/* =============================================================================
 * Internal: Utility Functions
 * ===========================================================================*/

static void dsdiff_timecode_normalize(dsdiff_timecode_t *timecode, uint32_t sample_rate) {
    uint64_t total_seconds, total_minutes, total_hours;

    if (!timecode || sample_rate == 0) {
        return;
    }

    total_seconds = (timecode->samples / sample_rate);
    timecode->samples = (timecode->samples % sample_rate);

    total_seconds += timecode->seconds;
    total_minutes = total_seconds / 60;
    timecode->seconds = (uint8_t)(total_seconds % 60);

    total_minutes += timecode->minutes;
    total_hours = total_minutes / 60;
    timecode->minutes = (uint8_t)(total_minutes % 60);

    total_hours += timecode->hours;
    timecode->hours = (uint16_t)total_hours;
}

/* =============================================================================
 * Error Handling
 * ===========================================================================*/

const char *dsdiff_error_string(int error_code) {
    switch (error_code) {
        case DSDIFF_SUCCESS:
            return "Success";

        /* File state errors */
        case DSDIFF_ERROR_ALREADY_OPEN:
            return "File already open";
        case DSDIFF_ERROR_NOT_OPEN:
            return "File not open";
        case DSDIFF_ERROR_MODE_READ_ONLY:
            return "File is open for reading only";
        case DSDIFF_ERROR_MODE_WRITE_ONLY:
            return "File is open for writing only";

        /* File format errors */
        case DSDIFF_ERROR_INVALID_FILE:
            return "Invalid DSDIFF file";
        case DSDIFF_ERROR_INVALID_VERSION:
            return "Invalid DSDIFF version";
        case DSDIFF_ERROR_UNSUPPORTED_COMPRESSION:
            return "Unsupported compression type";
        case DSDIFF_ERROR_UNEXPECTED_EOF:
            return "Unexpected end of file";
        case DSDIFF_ERROR_INVALID_CHUNK:
            return "Invalid chunk structure";

        /* I/O errors */
        case DSDIFF_ERROR_READ_FAILED:
            return "Read error";
        case DSDIFF_ERROR_WRITE_FAILED:
            return "Write error";
        case DSDIFF_ERROR_SEEK_FAILED:
            return "Seek error";
        case DSDIFF_ERROR_END_OF_DATA:
            return "End of sound data reached";
        case DSDIFF_ERROR_MAX_FILE_SIZE:
            return "Maximum file size exceeded";
        case DSDIFF_ERROR_FILE_NOT_FOUND:
            return "File not found";
        case DSDIFF_ERROR_FILE_CREATE_FAILED:
            return "Cannot create file";

        /* Missing metadata errors */
        case DSDIFF_ERROR_NO_CHANNEL_INFO:
            return "No channel information";
        case DSDIFF_ERROR_NO_TIMECODE:
            return "No timecode information";
        case DSDIFF_ERROR_NO_LSCONFIG:
            return "No loudspeaker configuration";
        case DSDIFF_ERROR_NO_COMMENT:
            return "No comment at index";
        case DSDIFF_ERROR_NO_EMID:
            return "No edited master ID";
        case DSDIFF_ERROR_NO_ARTIST:
            return "No disc artist";
        case DSDIFF_ERROR_NO_TITLE:
            return "No disc title";
        case DSDIFF_ERROR_NO_MARKER:
            return "No marker at index";
        case DSDIFF_ERROR_NO_CRC:
            return "No CRC data";
        case DSDIFF_ERROR_NO_MANUFACTURER:
            return "No manufacturer data";

        /* Validation errors */
        case DSDIFF_ERROR_INVALID_ARG:
            return "Invalid argument";
        case DSDIFF_ERROR_OUT_OF_MEMORY:
            return "Out of memory";
        case DSDIFF_ERROR_INVALID_CHANNELS:
            return "Invalid channel configuration";
        case DSDIFF_ERROR_INVALID_TIMECODE:
            return "Invalid timecode";
        case DSDIFF_ERROR_INVALID_MODE:
            return "Invalid file mode";
        case DSDIFF_ERROR_BUFFER_TOO_SMALL:
            return "Buffer too small";

        /* Operation errors */
        case DSDIFF_ERROR_POST_CREATE_FORBIDDEN:
            return "Operation not allowed after file creation";
        case DSDIFF_ERROR_CHUNK_LOCKED:
            return "Chunk is locked and cannot be modified";

        /* Format mismatch errors */
        case DSDIFF_ERROR_REQUIRES_DSD:
            return "Operation requires DSD format";
        case DSDIFF_ERROR_REQUIRES_DST:
            return "Operation requires DST format";
        case DSDIFF_ERROR_CRC_ALREADY_PRESENT:
            return "CRC data already present";
        case DSDIFF_ERROR_NO_DST_INDEX:
            return "No DST frame index available";

        default:
            return "Unknown error";
    }
}
