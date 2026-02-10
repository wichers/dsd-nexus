/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief DSDIFF chunk I/O operations
 * This module handles reading and writing of all DSDIFF chunk types.
 * DSDIFF uses a hierarchical chunk structure similar to AIFF/RIFF but
 * with 8-byte chunk sizes.
 * Chunk Hierarchy:
 * - FRM8 (top-level container)
 *   - FVER (format version)
 *   - PROP (properties container)
 *     - FS (sample rate)
 *     - CHNL (channels)
 *     - CMPR (compression)
 *     - ABSS (absolute start time, optional)
 *     - LSCO (loudspeaker config, optional)
 *   - SND/DST (audio data container)
 *     - FRTE (DST frame rate, if DST)
 *     - DSTF/DSTC (DST frames and CRCs)
 *   - DSTI (DST index, optional)
 *   - COMT (comments, optional)
 *   - DIIN (detailed info container, optional)
 *     - EMID (edited master ID)
 *     - DSDMARK (DSD markers)
 *     - DIAR (disc artist)
 *     - DITI (disc title)
 *   - MANF (manufacturer specific, optional, must follow sound data)
 * References:
 * - DSDIFF_1.5_file_format_specification.pdf
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

#ifndef LIBDSDIFF_DSDIFF_CHUNKS_H
#define LIBDSDIFF_DSDIFF_CHUNKS_H

#include "dsdiff_types.h"
#include "dsdiff_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque chunk file handle (internal use)
 */
typedef dsdiff_io_t dsdiff_chunk_t;

/**
 * @brief Open file for writing
 *
 * @param chunk Chunk file handle
 * @param filename File path
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_file_open_write(dsdiff_chunk_t **chunk, const char *filename);

/**
 * @brief Open file for reading
 *
 * @param chunk Chunk file handle
 * @param filename File path
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_file_open_read(dsdiff_chunk_t **chunk, const char *filename);

/**
 * @brief Open file for modification (read/write metadata)
 *
 * @param chunk Chunk file handle
 * @param filename File path
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_file_open_modify(dsdiff_chunk_t **chunk, const char *filename);

/* =============================================================================
 * Chunk Operations
 * ===========================================================================*/

/**
 * @brief Skip current chunk
 *
 * Advances file position past the current chunk (including padding).
 *
 * @param chunk Chunk file handle
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_skip(dsdiff_chunk_t *chunk);

/**
 * @brief Read chunk header
 *
 * Reads the chunk ID and size, identifying the chunk type.
 *
 * @param chunk Chunk file handle
 * @param chunk_type Pointer to receive chunk type
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_header(dsdiff_chunk_t *chunk,
                              dsdiff_chunk_type_t *chunk_type);

/* =============================================================================
 * Top-Level Chunks
 * ===========================================================================*/

/**
 * @brief Read FRM8 chunk (Form DSD Chunk)
 *
 * The top-level container chunk. Must be first in file.
 *
 * @param chunk Chunk file handle
 * @param chunk_size Pointer to receive chunk data size
 * @param file_type Pointer to receive audio type (DSDIFF_AUDIO_DSD or DSDIFF_AUDIO_DST)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_frm8_header(dsdiff_chunk_t *chunk, uint64_t *chunk_size,
                            dsdiff_audio_type_t *file_type);

/**
 * @brief Write FRM8 chunk header
 *
 * @param chunk Chunk file handle
 * @param chunk_size Chunk data size (0 = unknown, will update later)
 * @param is_dst_format DST flag (1=DST, 0=DSD)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_frm8_header(dsdiff_chunk_t *chunk, uint64_t chunk_size,
                             dsdiff_audio_type_t audio_type);

/**
 * @brief Read FVER chunk (Format Version)
 *
 * Contains the DSDIFF format version number.
 *
 * @param chunk Chunk file handle
 * @param version Pointer to receive version (e.g., 0x01050000 = v1.5.0.0)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_fver(dsdiff_chunk_t *chunk, uint32_t *version);

/**
 * @brief Write FVER chunk
 *
 * @param chunk Chunk file handle
 * @param version Version number
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_fver(dsdiff_chunk_t *chunk, uint32_t version);

/* =============================================================================
 * Property Chunks (inside PROP container)
 * ===========================================================================*/

/**
 * @brief Read PROP chunk header (Property Container)
 *
 * @param chunk Chunk file handle
 * @param chunk_size Pointer to receive chunk data size
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_prop_header(dsdiff_chunk_t *chunk, uint64_t *chunk_size);

/**
 * @brief Write PROP chunk header
 *
 * @param chunk Chunk file handle
 * @param chunk_size Chunk data size (0 = unknown)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_prop_header(dsdiff_chunk_t *chunk, uint64_t chunk_size);

/**
 * @brief Read FS chunk (Sample Rate)
 *
 * @param chunk Chunk file handle
 * @param sample_rate Pointer to receive sample rate in Hz
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_fs(dsdiff_chunk_t *chunk, uint32_t *sample_rate);

/**
 * @brief Write FS chunk
 *
 * @param chunk Chunk file handle
 * @param sample_rate Sample rate in Hz (e.g., 2822400 = 64×44.1kHz)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_fs(dsdiff_chunk_t *chunk, uint32_t sample_rate);

/**
 * @brief Read CHNL chunk (Channels)
 *
 * Reads channel count and channel IDs.
 * The channel_ids array is allocated and must be freed by caller.
 *
 * @param chunk Chunk file handle
 * @param channel_count Pointer to receive channel count
 * @param channel_ids Pointer to receive allocated channel ID array
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_chnl(dsdiff_chunk_t *chunk, uint16_t *channel_count,
                            dsdiff_channel_id_t **channel_ids);

/**
 * @brief Write CHNL chunk
 *
 * @param chunk Chunk file handle
 * @param channel_count Number of channels
 * @param channel_ids Array of channel IDs
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_chnl(dsdiff_chunk_t *chunk, uint16_t channel_count,
                             const dsdiff_channel_id_t *channel_ids);

/**
 * @brief Read CMPR chunk (Compression Type)
 *
 * @param chunk Chunk file handle
 * @param compression_type Pointer to receive compression type
 * @param compression_name Pointer to receive allocated compression name string
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_cmpr(dsdiff_chunk_t *chunk,
                            dsdiff_audio_type_t *compression_type,
                            char *compression_name, size_t name_buffer_size);

/**
 * @brief Write CMPR chunk
 *
 * @param chunk Chunk file handle
 * @param compression_type Compression type
 * @param compression_name Compression name string
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_cmpr(dsdiff_chunk_t *chunk,
                             dsdiff_audio_type_t compression_type,
                             const char *compression_name);

/**
 * @brief Read ABSS chunk (Absolute Start Time)
 *
 * @param chunk Chunk file handle
 * @param timecode Pointer to receive start timecode
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_abss(dsdiff_chunk_t *chunk,
                            dsdiff_timecode_t *timecode);

/**
 * @brief Write ABSS chunk
 *
 * @param chunk Chunk file handle
 * @param timecode Start timecode
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_abss(dsdiff_chunk_t *chunk,
                             const dsdiff_timecode_t *timecode);

/**
 * @brief Read LSCO chunk (Loudspeaker Configuration)
 *
 * @param chunk Chunk file handle
 * @param loudspeaker_config Pointer to receive loudspeaker configuration
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_lsco(dsdiff_chunk_t *chunk,
                            dsdiff_loudspeaker_config_t *loudspeaker_config);

/**
 * @brief Write LSCO chunk
 *
 * @param chunk Chunk file handle
 * @param loudspeaker_config Loudspeaker configuration
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_lsco(dsdiff_chunk_t *chunk,
                             dsdiff_loudspeaker_config_t loudspeaker_config);

/* =============================================================================
 * Sound Data Chunks
 * ===========================================================================*/

/**
 * @brief Read SND chunk header (DSD Sound Data)
 *
 * @param chunk Chunk file handle
 * @param sound_data_size Pointer to receive sound data size
 * @param data_start_pos Pointer to receive start position of data
 * @param data_stop_pos Pointer to receive stop position of data
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_snd_header(dsdiff_chunk_t *chunk,
                                  uint64_t *sound_data_size,
                                  uint64_t *data_start_pos,
                                  uint64_t *data_stop_pos);

/**
 * @brief Write SND chunk header
 *
 * @param chunk Chunk file handle
 * @param sound_data_size Sound data size (0 = unknown)
 * @param data_start_pos Pointer to receive start position
 * @param data_stop_pos Pointer to receive stop position
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_snd_header(dsdiff_chunk_t *chunk,
                                   uint64_t sound_data_size,
                                   uint64_t *data_start_pos,
                                   uint64_t *data_stop_pos);

/**
 * @brief Read DST chunk header (DST Sound Data Container)
 *
 * @param chunk Chunk file handle
 * @param chunk_data_size Pointer to receive chunk data size
 * @param data_start_pos Pointer to receive start position
 * @param data_stop_pos Pointer to receive stop position
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_dst_header(dsdiff_chunk_t *chunk,
                                  uint64_t *chunk_data_size,
                                  uint64_t *data_start_pos,
                                  uint64_t *data_stop_pos);

/**
 * @brief Write DST chunk header
 *
 * @param chunk Chunk file handle
 * @param chunk_data_size Chunk data size (0 = unknown)
 * @param data_start_pos Pointer to receive start position
 * @param data_stop_pos Pointer to receive stop position
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_dst_header(dsdiff_chunk_t *chunk,
                                   uint64_t chunk_data_size,
                                   uint64_t *data_start_pos,
                                   uint64_t *data_stop_pos);

/**
 * @brief Read FRTE chunk (DST Frame Information)
 *
 * @param chunk Chunk file handle
 * @param num_frames Pointer to receive number of DST frames
 * @param frame_rate Pointer to receive frame rate (typically 75)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_frte(dsdiff_chunk_t *chunk, uint32_t *frame_count,
                            uint16_t *frame_rate);

/**
 * @brief Write FRTE chunk
 *
 * @param chunk Chunk file handle
 * @param num_frames Number of DST frames
 * @param frame_rate Frame rate (typically 75 fps)
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_frte(dsdiff_chunk_t *chunk, uint32_t frame_count,
                             uint16_t frame_rate);

/**
 * @brief Read DSTF chunk (DST Frame Data)
 *
 * @param chunk Chunk file handle
 * @param num_bytes Pointer to receive frame size in bytes
 * @param sound_data Buffer to receive frame data
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_dstf(dsdiff_chunk_t *chunk, size_t *frame_size,
                            uint8_t *frame_data);

/**
 * @brief Write DSTF chunk
 *
 * @param chunk Chunk file handle
 * @param num_bytes Frame size in bytes
 * @param sound_data Frame data
 * @param cur_file_pos Pointer to receive current file position after write
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_dstf(dsdiff_chunk_t *chunk, size_t frame_size,
                             const uint8_t *frame_data, uint64_t *position);

/**
 * @brief Read DSTC chunk (DST Frame CRC)
 *
 * @param chunk Chunk file handle
 * @param num_bytes Pointer to receive CRC size in bytes
 * @param crc_data Buffer to receive CRC data
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_dstc(dsdiff_chunk_t *chunk, size_t *crc_size,
                            uint8_t *crc_data);

/**
 * @brief Read DSTC chunk size only
 *
 * @param chunk Chunk file handle
 * @param num_bytes Pointer to receive CRC size in bytes
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_get_dstc_size(dsdiff_chunk_t *chunk,
                                 uint64_t *crc_size);

/**
 * @brief Write DSTC chunk
 *
 * @param chunk Chunk file handle
 * @param num_bytes CRC size in bytes
 * @param crc_data CRC data
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_dstc(dsdiff_chunk_t *chunk, size_t crc_size,
                             const uint8_t *crc_data);

/**
 * @brief Read DSTI chunk header (DST Sound Index)
 *
 * @param chunk Chunk file handle
 * @param chunk_data_size Pointer to receive chunk data size
 * @param data_start_pos Pointer to receive start position
 * @param data_stop_pos Pointer to receive stop position
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_dsti_header(dsdiff_chunk_t *chunk,
                                   uint64_t *chunk_data_size,
                                   uint64_t *data_start_pos,
                                   uint64_t *data_stop_pos);

/**
 * @brief Read DSTI chunk contents
 *
 * @param chunk Chunk file handle
 * @param file_pos File position of index data
 * @param num_frames Number of frames in index
 * @param indexes Array to receive index entries
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_dsti_contents(dsdiff_chunk_t *chunk,
                                     uint64_t offset, uint64_t frame_count,
                                     dsdiff_index_t *indexes);

/**
 * @brief Write DSTI chunk contents
 *
 * @param chunk Chunk file handle
 * @param num_frames Number of frames in index
 * @param indexes Array of index entries
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_dsti_contents(dsdiff_chunk_t *chunk,
                                      uint64_t frame_count,
                                      const dsdiff_index_t *indexes);

/**
 * @brief Read raw chunk contents at file position
 *
 * @param chunk Chunk file handle
 * @param file_pos File position to read from
 * @param byte_count Number of bytes to read
 * @param data Buffer to receive data
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_contents(dsdiff_chunk_t *chunk, uint64_t file_pos,
                               size_t byte_count, uint8_t *data);

/* =============================================================================
 * Comment Chunk
 * ===========================================================================*/

/**
 * @brief Read COMT chunk (Comments)
 *
 * Reads all comments from the chunk.
 * The comments array is allocated and must be freed by caller.
 *
 * @param chunk Chunk file handle
 * @param comment_count Pointer to receive number of comments
 * @param comments Pointer to receive allocated comments array
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_comt(dsdiff_chunk_t *chunk, uint16_t *comment_count,
                            dsdiff_comment_t **comments);

/**
 * @brief Write COMT chunk
 *
 * @param chunk Chunk file handle
 * @param comment_count Number of comments
 * @param comments Array of comments
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_comt(dsdiff_chunk_t *chunk, uint16_t comment_count,
                             const dsdiff_comment_t *comments);

/* =============================================================================
 * Detailed Information Chunks (inside DIIN container)
 * ===========================================================================*/

/**
 * @brief Read DIIN chunk header (Edited Master Information Container)
 *
 * @param chunk Chunk file handle
 * @param chunk_data_size Pointer to receive chunk data size
 * @param data_start_pos Pointer to receive start position
 * @param data_stop_pos Pointer to receive stop position
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_diin_header(dsdiff_chunk_t *chunk,
                                   uint64_t *chunk_data_size,
                                   uint64_t *data_start_pos,
                                   uint64_t *data_stop_pos);

/**
 * @brief Write DIIN chunk header
 *
 * @param chunk Chunk file handle
 * @param chunk_data_size Chunk data size (0 = unknown)
 * @param data_start_pos Pointer to receive start position
 * @param data_stop_pos Pointer to receive stop position
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_diin_header(dsdiff_chunk_t *chunk,
                                    uint64_t chunk_data_size,
                                    uint64_t *data_start_pos,
                                    uint64_t *data_stop_pos);

/**
 * @brief Read EMID chunk (Edited Master ID)
 *
 * @param chunk Chunk file handle
 * @param emid Pointer to receive allocated EMID string
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_emid(dsdiff_chunk_t *chunk, char **emid);

/**
 * @brief Write EMID chunk
 *
 * @param chunk Chunk file handle
 * @param emid EMID string
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_emid(dsdiff_chunk_t *chunk, size_t text_length, const char *emid);

/**
 * @brief Read DSDMARK chunk (DSD Marker)
 *
 * @param chunk Chunk file handle
 * @param marker Pointer to receive marker
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_mark(dsdiff_chunk_t *chunk,
                               dsdiff_marker_t *marker);

/**
 * @brief Write DSDMARK chunk
 *
 * @param chunk Chunk file handle
 * @param marker Marker to write
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_mark(dsdiff_chunk_t *chunk,
                                const dsdiff_marker_t *marker);

/**
 * @brief Read DIAR chunk (Disc Artist)
 *
 * @param chunk Chunk file handle
 * @param artist Pointer to receive allocated artist string
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_diar(dsdiff_chunk_t *chunk, char **artist);

/**
 * @brief Write DIAR chunk
 *
 * @param chunk Chunk file handle
 * @param artist Artist string
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_diar(dsdiff_chunk_t *chunk, size_t text_length, const char *artist);

/**
 * @brief Read DITI chunk (Disc Title)
 *
 * @param chunk Chunk file handle
 * @param title Pointer to receive allocated title string
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_diti(dsdiff_chunk_t *chunk, char **title);

/**
 * @brief Write DITI chunk
 *
 * @param chunk Chunk file handle
 * @param title Title string
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_diti(dsdiff_chunk_t *chunk, size_t text_length, const char *title);

/**
 * @brief Write ID3 chunk
 *
 * @param chunk Chunk file handle
 * @param tag_data ID3 tag data
 * @param tag_size Size of ID3 tag in bytes
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_id3(dsdiff_chunk_t *chunk, uint8_t *tag_data, uint32_t tag_size);

/**
 * @brief Read ID3 chunk
 *
 * @param chunk Chunk file handle
 * @param out_tag_data Pointer to receive allocated ID3 tag data
 * @param out_tag_size Pointer to receive size of allocated ID3 tag
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_id3(dsdiff_chunk_t *chunk, uint8_t **out_tag_data, uint32_t *out_tag_size);

/* =============================================================================
 * Manufacturer Specific Chunk
 * ===========================================================================*/

/**
 * @brief Read MANF chunk (Manufacturer Specific)
 *
 * @param chunk Chunk file handle
 * @param man_id Buffer to receive 4-byte manufacturer ID
 * @param data Pointer to receive allocated data buffer
 * @param data_size Pointer to receive data size
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_read_manf(dsdiff_chunk_t *chunk, uint8_t *man_id,
                           uint8_t **data, uint32_t *data_size);

/**
 * @brief Write MANF chunk
 *
 * @param chunk Chunk file handle
 * @param man_id 4-byte manufacturer ID
 * @param data Manufacturer data
 * @param data_size Size of data in bytes
 * @return 0 on success, negative error code on failure
 */
int dsdiff_chunk_write_manf(dsdiff_chunk_t *chunk, const uint8_t *man_id,
                            const uint8_t *data, uint32_t data_size);


#ifdef __cplusplus
}
#endif

#endif /* LIBDSDIFF_DSDIFF_CHUNKS_H */
