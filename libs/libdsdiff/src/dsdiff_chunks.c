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
#include "dsdiff_io.h"
#include "dsdiff_types.h"

#include <libsautil/mem.h>

#include <stdlib.h>
#include <string.h>

/* =============================================================================
 * File Open Operations
 * ===========================================================================*/

int dsdiff_chunk_file_open_modify(dsdiff_chunk_t **chunk, const char *filename) {
    dsdiff_chunk_t *cf;
    int ret;

    if (!chunk) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    cf = (dsdiff_chunk_t *)sa_malloc(sizeof(dsdiff_chunk_t));
    if (!cf) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    ret = dsdiff_io_open_modify((dsdiff_io_t *)cf, filename);
    if (ret != DSDIFF_SUCCESS) {
        sa_free(cf);
        return ret;
    }

    *chunk = cf;
    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_file_open_read(dsdiff_chunk_t **chunk, const char *filename) {
    dsdiff_chunk_t *cf;
    int ret;

    if (!chunk) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    cf = (dsdiff_chunk_t *)sa_malloc(sizeof(dsdiff_chunk_t));
    if (!cf) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    ret = dsdiff_io_open_read((dsdiff_io_t *)cf, filename);
    if (ret != DSDIFF_SUCCESS) {
        sa_free(cf);
        return ret;
    }

    *chunk = cf;
    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_file_open_write(dsdiff_chunk_t **chunk, const char *filename) {
    dsdiff_chunk_t *cf;
    int ret;

    if (!chunk) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    cf = (dsdiff_chunk_t *)sa_malloc(sizeof(dsdiff_chunk_t));
    if (!cf) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    ret = dsdiff_io_open_write((dsdiff_io_t *)cf, filename);
    if (ret != DSDIFF_SUCCESS) {
        sa_free(cf);
        return ret;
    }

    *chunk = cf;
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Generic Chunk Operations
 * ===========================================================================*/

int dsdiff_chunk_read_contents(dsdiff_chunk_t *chunk, uint64_t file_pos,
                               size_t byte_count, uint8_t *data) {
    uint64_t saved_pos = 0;
    uint64_t new_pos = 0;
    size_t actual_read;
    int ret;

    if (!chunk || !data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_get_position(chunk, &saved_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_seek(chunk, file_pos, DSDIFF_SEEK_SET, &new_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_bytes(chunk, data, byte_count, &actual_read);
    if (ret != DSDIFF_SUCCESS) {
        dsdiff_io_seek(chunk, saved_pos, DSDIFF_SEEK_SET, &new_pos);
        return ret;
    }

    ret = dsdiff_io_seek(chunk, saved_pos, DSDIFF_SEEK_SET, &new_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_skip(dsdiff_chunk_t *chunk) {
    uint64_t chunk_size;
    int ret;

    if (!chunk) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_seek(chunk, chunk_size, DSDIFF_SEEK_CUR, NULL);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if ((chunk_size % 2) != 0) {
        ret = dsdiff_io_read_pad_byte(chunk);
    }

    return ret;
}

int dsdiff_chunk_read_header(dsdiff_chunk_t *chunk,
                              dsdiff_chunk_type_t *chunk_type) {
    uint32_t fourcc;
    int ret;

    if (!chunk || !chunk_type) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *chunk_type = DSDIFF_CHUNK_UNKNOWN;

    ret = dsdiff_io_read_chunk_id(chunk, &fourcc);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    switch (fourcc) {
#define AS_CASE(name, a, b, c, d, desc) \
    case name##_FOURCC:           \
      *chunk_type = DSDIFF_CHUNK_##name; \
      break;
      DSDIFF_CHUNK_LIST(AS_CASE)
#undef AS_CASE
        default:          *chunk_type = DSDIFF_CHUNK_UNKNOWN; break;
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * FRM8 Container Chunk (Form Container)
 * ===========================================================================*/

int dsdiff_chunk_read_frm8_header(dsdiff_chunk_t *chunk,
                            uint64_t *chunk_size,
                            dsdiff_audio_type_t *file_type) {
    dsdiff_chunk_type_t chunk_type;
    int ret;

    if (!chunk || !chunk_size || !file_type) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *chunk_size = 0;
    *file_type = DSDIFF_AUDIO_DSD;

    ret = dsdiff_io_read_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_chunk_read_header(chunk, &chunk_type);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (chunk_type == DSDIFF_CHUNK_DST) {
        *file_type = DSDIFF_AUDIO_DST;
    } else if (chunk_type == DSDIFF_CHUNK_DSD) {
        *file_type = DSDIFF_AUDIO_DSD;
    } else {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_write_frm8_header(dsdiff_chunk_t *chunk, uint64_t chunk_size,
                             dsdiff_audio_type_t audio_type) {
    int ret;

    if (!chunk) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_write_chunk_id(chunk, FRM8_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    /*
     * FRM8 form type is always "DSD " for DSDIFF files, regardless of
     * whether the audio is DST-compressed or uncompressed DSD.
     * The compression type is indicated in the CMPR chunk inside PROP.
     */
    (void)audio_type;  /* Unused - form type is always DSD */
    return dsdiff_io_write_chunk_id(chunk, DSD_FOURCC);
}

/* =============================================================================
 * FVER Chunk (Format Version)
 * ===========================================================================*/

int dsdiff_chunk_write_fver(dsdiff_chunk_t *chunk, uint32_t version) {
    int ret;

    if (!chunk) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_write_chunk_id(chunk, FVER_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, sizeof(version));
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return dsdiff_io_write_uint32_be(chunk, version);
}

int dsdiff_chunk_read_fver(dsdiff_chunk_t *chunk, uint32_t *version) {
    uint64_t chunk_size;
    int ret;

    if (!chunk || !version) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *version = 0;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (chunk_size != sizeof(uint32_t)) {
        return DSDIFF_ERROR_INVALID_CHUNK;
    }

    return dsdiff_io_read_uint32_be(chunk, version);
}

/* =============================================================================
 * PROP Chunk (Property Container)
 * ===========================================================================*/

int dsdiff_chunk_write_prop_header(dsdiff_chunk_t *chunk, uint64_t chunk_size) {
    int ret;

    if (!chunk) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_write_chunk_id(chunk, PROP_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return dsdiff_io_write_chunk_id(chunk, SND_FOURCC);
}

int dsdiff_chunk_read_prop_header(dsdiff_chunk_t *chunk,
                            uint64_t *chunk_size) {
    uint32_t prop_type;
    int ret;

    if (!chunk || !chunk_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *chunk_size = 0;

    ret = dsdiff_io_read_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_chunk_id(chunk, &prop_type);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (prop_type != SND_FOURCC) {
        return DSDIFF_ERROR_INVALID_FILE;
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * FS Chunk (Sample Rate)
 * ===========================================================================*/

int dsdiff_chunk_write_fs(dsdiff_chunk_t *chunk, uint32_t sample_rate) {
    int ret;

    if (!chunk) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_write_chunk_id(chunk, FS_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, sizeof(sample_rate));
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return dsdiff_io_write_uint32_be(chunk, sample_rate);
}

int dsdiff_chunk_read_fs(dsdiff_chunk_t *chunk, uint32_t *sample_rate) {
    uint64_t chunk_size;
    int ret;

    if (!chunk || !sample_rate) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *sample_rate = 0;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (chunk_size != sizeof(uint32_t)) {
        return DSDIFF_ERROR_INVALID_CHUNK;
    }

    return dsdiff_io_read_uint32_be(chunk, sample_rate);
}

/* =============================================================================
 * CHNL Chunk (Channel Configuration)
 * ===========================================================================*/

int dsdiff_chunk_write_chnl(dsdiff_chunk_t *chunk,
                             uint16_t channel_count,
                             const dsdiff_channel_id_t *channel_ids) {
    static const uint32_t channel_code_map[] = {
#define AS_ARRAY(name, a, b, c, d, id, desc) \
        [DSDIFF_CHAN_##name] = name##_FOURCC,
      DSDIFF_CHANNEL_LIST(AS_ARRAY)
#undef AS_ARRAY
        [DSDIFF_CHAN_INVALID] = 0xfeedfeed
    };
    uint64_t chunk_size;
    int ret;
    uint16_t i;

    if (!chunk || !channel_ids) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    chunk_size = 2 + (channel_count * 4);

    ret = dsdiff_io_write_chunk_id(chunk, CHNL_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint16_be(chunk, channel_count);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    for (i = 0; i < channel_count; i++) {
        uint32_t code;
        int ch_id = (int)channel_ids[i];

        if (ch_id >= 0 && ch_id < (int)(sizeof(channel_code_map) / sizeof(channel_code_map[0])) && channel_code_map[ch_id] != 0) {
            code = channel_code_map[ch_id];
        } else if (ch_id >= 0 && ch_id <= 999) {
            int d0 = ch_id / 100;
            int d1 = (ch_id % 100) / 10;
            int d2 = ch_id % 10;
            code = ('C' << 24) | ((d0 + '0') << 16) | ((d1 + '0') << 8) | (d2 + '0');
        } else {
            return DSDIFF_ERROR_INVALID_ARG;
        }

        ret = dsdiff_io_write_chunk_id(chunk, code);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_read_chnl(dsdiff_chunk_t *chunk,
                            uint16_t *channel_count,
                            dsdiff_channel_id_t **channel_ids) {
    uint64_t chunk_size;
    uint16_t num_ch;
    dsdiff_channel_id_t *ch_ids;
    int ret;
    uint16_t i;

    if (!chunk || !channel_count || !channel_ids) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *channel_count = 0;
    *channel_ids = NULL;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_uint16_be(chunk, &num_ch);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (num_ch <= 0 || num_ch > 1000 ||
        chunk_size != (sizeof(uint16_t) + sizeof(uint32_t) * num_ch)) {
        return DSDIFF_ERROR_INVALID_CHUNK;
    }

    ch_ids = (dsdiff_channel_id_t *)sa_malloc(num_ch * sizeof(dsdiff_channel_id_t));
    if (!ch_ids) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    for (i = 0; i < num_ch; i++) {
        uint32_t code;
        dsdiff_channel_id_t ch_id;

        ret = dsdiff_io_read_chunk_id(chunk, &code);
        if (ret != DSDIFF_SUCCESS) {
            sa_free(ch_ids);
            return ret;
        }

        switch (code) {
#define AS_CASE(name, a, b, c, d, id, desc)    \
            case name##_FOURCC: \
          ch_id = DSDIFF_CHAN_##name;       \
          break;
          DSDIFF_CHANNEL_LIST(AS_CASE)
#undef AS_CASE
            default: {
                uint8_t b0 = (code >> 24) & 0xFF;
                uint8_t b1 = (code >> 16) & 0xFF;
                uint8_t b2 = (code >> 8) & 0xFF;
                uint8_t b3 = code & 0xFF;

                if (b0 == 'C' && b1 >= '0' && b1 <= '9' &&
                    b2 >= '0' && b2 <= '9' && b3 >= '0' && b3 <= '9') {
                    int number = (b3 - '0') + 10 * (b2 - '0') + 100 * (b1 - '0');
                    ch_id = (dsdiff_channel_id_t)number;
                } else {
                    ch_id = DSDIFF_CHAN_INVALID;
                }
                break;
            }
        }

        ch_ids[i] = ch_id;
    }

    *channel_count = num_ch;
    *channel_ids = ch_ids;

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * CMPR Chunk (Compression Type)
 * ===========================================================================*/

int dsdiff_chunk_write_cmpr(dsdiff_chunk_t *chunk,
                             dsdiff_audio_type_t compression_type,
                             const char *compression_name) {
    uint32_t fourcc;
    size_t str_len;
    uint64_t chunk_size;
    int ret;

    if (!chunk || !compression_name) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    fourcc = (compression_type == DSDIFF_AUDIO_DST) ? DST_FOURCC : DSD_FOURCC;
    str_len = strlen(compression_name);

    chunk_size = 4;
    chunk_size += 1;
    chunk_size += str_len;
    if ((str_len % 2) == 0) {
        chunk_size += 1;
    }

    ret = dsdiff_io_write_chunk_id(chunk, CMPR_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_chunk_id(chunk, fourcc);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_pstring(chunk, (uint16_t)str_len, compression_name);

    return ret;
}

int dsdiff_chunk_read_cmpr(dsdiff_chunk_t *chunk,
                            dsdiff_audio_type_t *compression_type,
                            char *compression_name, size_t name_buffer_size) {
    uint64_t chunk_size;
    uint32_t fourcc;
    uint16_t name_len;
    int ret;

    if (!chunk || !compression_type || !compression_name) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_chunk_id(chunk, &fourcc);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    switch (fourcc) {
        case DSD_FOURCC:
            *compression_type = DSDIFF_AUDIO_DSD;
            break;
        case DST_FOURCC:
            *compression_type = DSDIFF_AUDIO_DST;
            break;
        default:
            return DSDIFF_ERROR_INVALID_CHUNK;
    }

    ret = dsdiff_io_read_pstring(chunk, &name_len, compression_name,
                                 name_buffer_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * ABSS Chunk (Absolute Start Time)
 * ===========================================================================*/

int dsdiff_chunk_write_abss(dsdiff_chunk_t *chunk,
                             const dsdiff_timecode_t *timecode) {
    int ret;

    if (!chunk || !timecode) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_write_chunk_id(chunk, ABSS_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, 8);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint16_be(chunk, timecode->hours);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint8(chunk, timecode->minutes);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint8(chunk, timecode->seconds);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return dsdiff_io_write_uint32_be(chunk, timecode->samples);
}

int dsdiff_chunk_read_abss(dsdiff_chunk_t *chunk,
                            dsdiff_timecode_t *timecode) {
    uint64_t chunk_size;
    int ret;

    if (!chunk || !timecode) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (chunk_size != 8) {
        return DSDIFF_ERROR_INVALID_CHUNK;
    }

    ret = dsdiff_io_read_uint16_be(chunk, &timecode->hours);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_uint8(chunk, &timecode->minutes);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_uint8(chunk, &timecode->seconds);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return dsdiff_io_read_uint32_be(chunk, &timecode->samples);
}

/* =============================================================================
 * LSCO Chunk (Loudspeaker Configuration)
 * ===========================================================================*/

int dsdiff_chunk_write_lsco(dsdiff_chunk_t *chunk,
                             dsdiff_loudspeaker_config_t config) {
    int ret;

    if (!chunk) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_write_chunk_id(chunk, LSCO_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, 2);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return dsdiff_io_write_uint16_be(chunk, config);
}

int dsdiff_chunk_read_lsco(dsdiff_chunk_t *chunk,
                            dsdiff_loudspeaker_config_t *config) {
    uint64_t chunk_size;
    uint16_t cfg;
    int ret;

    if (!chunk || !config) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (chunk_size != 2) {
        return DSDIFF_ERROR_INVALID_CHUNK;
    }

    ret = dsdiff_io_read_uint16_be(chunk, &cfg);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    *config = (dsdiff_loudspeaker_config_t)cfg;
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * DSD Sound Data Chunk
 * ===========================================================================*/

int dsdiff_chunk_write_snd_header(dsdiff_chunk_t *chunk,
                                   uint64_t sound_data_size,
                                   uint64_t *data_start_pos,
                                   uint64_t *data_stop_pos) {
    int ret;

    if (!chunk || !data_start_pos || !data_stop_pos) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *data_start_pos = 0;
    *data_stop_pos = 0;

    ret = dsdiff_io_write_chunk_id(chunk, DSD_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, sound_data_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_get_position(chunk, data_start_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    *data_stop_pos = *data_start_pos + sound_data_size;

    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_read_snd_header(dsdiff_chunk_t *chunk,
                                  uint64_t *sound_data_size,
                                  uint64_t *data_start_pos,
                                  uint64_t *data_stop_pos) {
    uint64_t chunk_size;
    int ret;

    if (!chunk || !sound_data_size ||
        !data_start_pos || !data_stop_pos) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *sound_data_size = 0;
    *data_start_pos = 0;
    *data_stop_pos = 0;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    *sound_data_size = chunk_size;

    ret = dsdiff_io_get_position(chunk, data_start_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_seek(chunk, chunk_size, DSDIFF_SEEK_CUR, data_stop_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if ((chunk_size % 2) != 0) {
        ret = dsdiff_io_read_pad_byte(chunk);
    }

    return ret;
}

/* =============================================================================
 * DST Sound Data Chunk (Compressed Audio)
 * ===========================================================================*/

int dsdiff_chunk_write_dst_header(dsdiff_chunk_t *chunk,
                                   uint64_t chunk_data_size,
                                   uint64_t *data_start_pos,
                                   uint64_t *data_stop_pos) {
    int ret;

    if (!chunk || !data_start_pos || !data_stop_pos) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *data_start_pos = 0;
    *data_stop_pos = 0;

    ret = dsdiff_io_write_chunk_id(chunk, DST_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_data_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_get_position(chunk, data_start_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    *data_stop_pos = *data_start_pos + chunk_data_size;

    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_read_dst_header(dsdiff_chunk_t *chunk,
                                  uint64_t *chunk_data_size,
                                  uint64_t *data_start_pos,
                                  uint64_t *data_stop_pos) {
    uint64_t chunk_size;
    int ret;

    if (!chunk || !chunk_data_size ||
        !data_start_pos || !data_stop_pos) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *chunk_data_size = 0;
    *data_start_pos = 0;
    *data_stop_pos = 0;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    *chunk_data_size = chunk_size;

    ret = dsdiff_io_get_position(chunk, data_start_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * FRTE Chunk (DST Frame Information)
 * ===========================================================================*/

int dsdiff_chunk_write_frte(dsdiff_chunk_t *chunk,
                             uint32_t frame_count,
                             uint16_t frame_rate) {
    int ret;

    if (!chunk) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_write_chunk_id(chunk, FRTE_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, 6);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint32_be(chunk, frame_count);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return dsdiff_io_write_uint16_be(chunk, frame_rate);
}

int dsdiff_chunk_read_frte(dsdiff_chunk_t *chunk,
                            uint32_t *frame_count,
                            uint16_t *frame_rate) {
    uint64_t chunk_size;
    int ret;

    if (!chunk || !frame_rate || !frame_count) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (chunk_size != 6) {
        return DSDIFF_ERROR_INVALID_CHUNK;
    }

    ret = dsdiff_io_read_uint32_be(chunk, frame_count);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return dsdiff_io_read_uint16_be(chunk, frame_rate);
}

/* =============================================================================
 * DSTF Chunk (DST Frame Data)
 * ===========================================================================*/

int dsdiff_chunk_write_dstf(dsdiff_chunk_t *chunk, size_t frame_size,
                            const uint8_t *frame_data, uint64_t *position) {
    int ret;
    size_t bytes_written;

    if (!chunk || !frame_data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_write_chunk_id(chunk, DSTF_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, frame_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (position) {
        ret = dsdiff_io_get_position(chunk, position);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    ret = dsdiff_io_write_bytes(chunk, frame_data, (uint32_t) frame_size, &bytes_written);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }
    if (bytes_written != frame_size) {
        return DSDIFF_ERROR_WRITE_FAILED;
    }

    if ((frame_size % 2) != 0) {
        ret = dsdiff_io_write_pad_byte(chunk);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_read_dstf(dsdiff_chunk_t *chunk, size_t *frame_size,
                           uint8_t *frame_data) {
    int ret;
    size_t bytes_read;

    if (!chunk || !frame_size || !frame_data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_read_uint64_be(chunk, frame_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_bytes(chunk, frame_data, (uint32_t) *frame_size, &bytes_read);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }
    if (bytes_read != *frame_size) {
        return DSDIFF_ERROR_UNEXPECTED_EOF;
    }

    if ((*frame_size % 2) != 0) {
        ret = dsdiff_io_read_pad_byte(chunk);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * DSTC Chunk (DST CRC Data)
 * ===========================================================================*/

int dsdiff_chunk_write_dstc(dsdiff_chunk_t *chunk, size_t crc_size,
                            const uint8_t *crc_data) {
    int ret;
    size_t bytes_written;

    if (!chunk || !crc_data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_write_chunk_id(chunk, DSTC_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, crc_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_bytes(chunk, crc_data, (size_t)crc_size, &bytes_written);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }
    if (bytes_written != (size_t)crc_size) {
        return DSDIFF_ERROR_WRITE_FAILED;
    }

    if ((crc_size % 2) != 0) {
        ret = dsdiff_io_write_pad_byte(chunk);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_get_dstc_size(dsdiff_chunk_t *chunk, uint64_t *crc_size) {
    if (!chunk || !crc_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    return dsdiff_io_read_uint64_be(chunk, crc_size);
}

int dsdiff_chunk_read_dstc(dsdiff_chunk_t *chunk, size_t *crc_size,
                           uint8_t *crc_data) {
    int ret;
    size_t bytes_read;

    if (!chunk || !crc_size || !crc_data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_read_uint64_be(chunk, crc_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_bytes(chunk, crc_data, (size_t)*crc_size, &bytes_read);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }
    if (bytes_read != (size_t)*crc_size) {
        return DSDIFF_ERROR_UNEXPECTED_EOF;
    }

    if ((*crc_size % 2) != 0) {
        ret = dsdiff_io_read_pad_byte(chunk);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * DSTI Chunk (DST Sound Index)
 * ===========================================================================*/

int dsdiff_chunk_write_dsti_contents(dsdiff_chunk_t *chunk,
                                      uint64_t frame_count,
                                      const dsdiff_index_t *indexes) {
    uint64_t chunk_size;
    uint64_t i;
    int ret;

    if (!chunk || !indexes) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    chunk_size = frame_count * 12;

    ret = dsdiff_io_write_chunk_id(chunk, DSTI_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    for (i = 0; i < frame_count; i++) {
        ret = dsdiff_io_write_uint64_be(chunk, indexes[i].offset);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }

        ret = dsdiff_io_write_uint32_be(chunk, indexes[i].length);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_read_dsti_header(dsdiff_chunk_t *chunk,
                                   uint64_t *chunk_data_size,
                                   uint64_t *data_start_pos,
                                   uint64_t *data_stop_pos) {
    uint64_t chunk_size;
    int ret;

    if (!chunk || !chunk_data_size ||
        !data_start_pos || !data_stop_pos) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *chunk_data_size = 0;
    *data_start_pos = 0;
    *data_stop_pos = 0;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    *chunk_data_size = chunk_size;

    ret = dsdiff_io_get_position(chunk, data_start_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_seek(chunk, chunk_size, DSDIFF_SEEK_CUR, data_stop_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if ((chunk_size % 2) != 0) {
        ret = dsdiff_io_read_pad_byte(chunk);
    }

    return ret;
}

int dsdiff_chunk_read_dsti_contents(dsdiff_chunk_t *chunk,
                                     uint64_t offset, uint64_t frame_count,
                                     dsdiff_index_t *indexes) {
    uint64_t cur_pos;
    uint64_t read_back_pos;
    uint64_t i;
    int ret;

    if (!chunk || !indexes) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_get_position(chunk, &cur_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_seek(chunk, offset, DSDIFF_SEEK_SET, &read_back_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    for (i = 0; i < frame_count; i++) {
        ret = dsdiff_io_read_uint64_be(chunk, &indexes[i].offset);
        if (ret != DSDIFF_SUCCESS) {
            dsdiff_io_seek(chunk, cur_pos, DSDIFF_SEEK_SET, &read_back_pos);
            return ret;
        }

        ret = dsdiff_io_read_uint32_be(chunk, &indexes[i].length);
        if (ret != DSDIFF_SUCCESS) {
            dsdiff_io_seek(chunk, cur_pos, DSDIFF_SEEK_SET, &read_back_pos);
            return ret;
        }
    }

    ret = dsdiff_io_seek(chunk, cur_pos, DSDIFF_SEEK_SET, &read_back_pos);

    return ret;
}

/* =============================================================================
 * COMT Chunk (Comments)
 * ===========================================================================*/

int dsdiff_chunk_write_comt(dsdiff_chunk_t *chunk,
                             uint16_t comment_count,
                             const dsdiff_comment_t *comments) {
    uint64_t chunk_size = 2;
    uint16_t i;
    int ret;

    if (!chunk || !comments) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    for (i = 0; i < comment_count; i++) {
        uint32_t text_len = comments[i].text ? (uint32_t)strlen(comments[i].text) : 0;
        chunk_size += 14;
        chunk_size += text_len;
        if ((text_len % 2) != 0) {
            chunk_size++;
        }
    }

    ret = dsdiff_io_write_chunk_id(chunk, COMT_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint16_be(chunk, comment_count);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    for (i = 0; i < comment_count; i++) {
        uint32_t text_len = comments[i].text ? (uint32_t)strlen(comments[i].text) : 0;

        ret = dsdiff_io_write_uint16_be(chunk, comments[i].year);
        if (ret != DSDIFF_SUCCESS) return ret;
        ret = dsdiff_io_write_uint8(chunk, comments[i].month);
        if (ret != DSDIFF_SUCCESS) return ret;
        ret = dsdiff_io_write_uint8(chunk, comments[i].day);
        if (ret != DSDIFF_SUCCESS) return ret;
        ret = dsdiff_io_write_uint8(chunk, comments[i].hour);
        if (ret != DSDIFF_SUCCESS) return ret;
        ret = dsdiff_io_write_uint8(chunk, comments[i].minute);
        if (ret != DSDIFF_SUCCESS) return ret;

        ret = dsdiff_io_write_uint16_be(chunk, comments[i].comment_type);
        if (ret != DSDIFF_SUCCESS) return ret;

        ret = dsdiff_io_write_uint16_be(chunk, comments[i].comment_ref);
        if (ret != DSDIFF_SUCCESS) return ret;

        ret = dsdiff_io_write_uint32_be(chunk, text_len);
        if (ret != DSDIFF_SUCCESS) return ret;

        if (text_len > 0 && comments[i].text) {
            ret = dsdiff_io_write_string(chunk, text_len, comments[i].text);
            if (ret != DSDIFF_SUCCESS) return ret;
        }

        if ((text_len % 2) != 0) {
            ret = dsdiff_io_write_pad_byte(chunk);
            if (ret != DSDIFF_SUCCESS) return ret;
        }
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_read_comt(dsdiff_chunk_t *chunk, uint16_t *comment_count,
                            dsdiff_comment_t **comments) {
    uint64_t chunk_size;
    uint16_t num;
    dsdiff_comment_t *comment_array = NULL;
    uint16_t i;
    int ret;

    if (!chunk || !comment_count || !comments) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *comment_count = 0;
    *comments = NULL;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_uint16_be(chunk, &num);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (num == 0) {
        return DSDIFF_SUCCESS;
    }

    comment_array = (dsdiff_comment_t *)sa_malloc(num * sizeof(dsdiff_comment_t));
    if (!comment_array) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }
    memset(comment_array, 0, num * sizeof(dsdiff_comment_t));

    for (i = 0; i < num; i++) {
        uint32_t text_len;

        ret = dsdiff_io_read_uint16_be(chunk, &comment_array[i].year);
        if (ret != DSDIFF_SUCCESS) goto error;
        ret = dsdiff_io_read_uint8(chunk, &comment_array[i].month);
        if (ret != DSDIFF_SUCCESS) goto error;
        ret = dsdiff_io_read_uint8(chunk, &comment_array[i].day);
        if (ret != DSDIFF_SUCCESS) goto error;
        ret = dsdiff_io_read_uint8(chunk, &comment_array[i].hour);
        if (ret != DSDIFF_SUCCESS) goto error;
        ret = dsdiff_io_read_uint8(chunk, &comment_array[i].minute);
        if (ret != DSDIFF_SUCCESS) goto error;

        ret = dsdiff_io_read_uint16_be(chunk, &comment_array[i].comment_type);
        if (ret != DSDIFF_SUCCESS) goto error;

        ret = dsdiff_io_read_uint16_be(chunk, &comment_array[i].comment_ref);
        if (ret != DSDIFF_SUCCESS) goto error;

        ret = dsdiff_io_read_uint32_be(chunk, &text_len);
        if (ret != DSDIFF_SUCCESS) goto error;

        comment_array[i].text_length = text_len;

        if (text_len > 0) {
            comment_array[i].text = (char *)sa_malloc(text_len + 1);
            if (!comment_array[i].text) {
                ret = DSDIFF_ERROR_OUT_OF_MEMORY;
                goto error;
            }

            ret = dsdiff_io_read_string(chunk, text_len, comment_array[i].text);
            if (ret != DSDIFF_SUCCESS) goto error;

            if ((text_len % 2) != 0) {
                ret = dsdiff_io_read_pad_byte(chunk);
                if (ret != DSDIFF_SUCCESS) goto error;
            }
        }
    }

    *comment_count = num;
    *comments = comment_array;
    return DSDIFF_SUCCESS;

error:
    if (comment_array) {
        for (i = 0; i < num; i++) {
            if (comment_array[i].text) {
                sa_free(comment_array[i].text);
            }
        }
        sa_free(comment_array);
    }
    *comment_count = 0;
    *comments = NULL;
    return ret;
}

/* =============================================================================
 * DIIN Chunk (Edited Master Information)
 * ===========================================================================*/

int dsdiff_chunk_write_diin_header(dsdiff_chunk_t *chunk,
                                    uint64_t chunk_data_size,
                                    uint64_t *data_start_pos,
                                    uint64_t *data_stop_pos) {
    int ret;

    if (!chunk || !data_start_pos || !data_stop_pos) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *data_start_pos = 0;
    *data_stop_pos = 0;

    ret = dsdiff_io_write_chunk_id(chunk, DIIN_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_data_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_get_position(chunk, data_start_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    *data_stop_pos = *data_start_pos + chunk_data_size;

    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_read_diin_header(dsdiff_chunk_t *chunk,
                                   uint64_t *chunk_data_size,
                                   uint64_t *data_start_pos,
                                   uint64_t *data_stop_pos) {
    uint64_t chunk_size;
    int ret;

    if (!chunk || !chunk_data_size ||
        !data_start_pos || !data_stop_pos) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *chunk_data_size = 0;
    *data_start_pos = 0;
    *data_stop_pos = 0;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    *chunk_data_size = chunk_size;

    ret = dsdiff_io_get_position(chunk, data_start_pos);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * EMID Chunk (Edited Master ID)
 * ===========================================================================*/

int dsdiff_chunk_write_emid(dsdiff_chunk_t *chunk, size_t text_length, const char *emid) {
    uint64_t chunk_size;
    int ret;

    if (!chunk || !emid) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    chunk_size = text_length;

    ret = dsdiff_io_write_chunk_id(chunk, EMID_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_string(chunk, text_length, emid);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if ((text_length % 2) != 0) {
        ret = dsdiff_io_write_pad_byte(chunk);
    }

    return ret;
}

int dsdiff_chunk_read_emid(dsdiff_chunk_t *chunk, char **emid) {
    uint64_t chunk_size;
    uint32_t num_chars;
    char *text;
    int ret;

    if (!chunk || !emid) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *emid = NULL;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    num_chars = (uint32_t)chunk_size;

    text = (char *)sa_malloc(num_chars + 1);
    if (!text) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    ret = dsdiff_io_read_string(chunk, num_chars, text);
    if (ret != DSDIFF_SUCCESS) {
        sa_free(text);
        return ret;
    }

    if ((num_chars % 2) != 0) {
        ret = dsdiff_io_read_pad_byte(chunk);
        if (ret != DSDIFF_SUCCESS) {
            sa_free(text);
            return ret;
        }
    }

    *emid = text;
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * MARK Chunk (Markers)
 * ===========================================================================*/

int dsdiff_chunk_write_mark(dsdiff_chunk_t *chunk,
                                const dsdiff_marker_t *marker) {
    uint64_t chunk_size;
    uint32_t text_len;
    int ret;

    if (!chunk || !marker) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    text_len = marker->marker_text ? (uint32_t)strlen(marker->marker_text) : 0;
    chunk_size = text_len + 22;

    if (chunk_size % 2 != 0) {
        chunk_size++;
    }

    ret = dsdiff_io_write_chunk_id(chunk, MARK_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint16_be(chunk, (uint16_t)marker->time.hours);
    if (ret != DSDIFF_SUCCESS) return ret;
    ret = dsdiff_io_write_uint8(chunk, marker->time.minutes);
    if (ret != DSDIFF_SUCCESS) return ret;
    ret = dsdiff_io_write_uint8(chunk, marker->time.seconds);
    if (ret != DSDIFF_SUCCESS) return ret;
    ret = dsdiff_io_write_uint32_be(chunk, marker->time.samples);
    if (ret != DSDIFF_SUCCESS) return ret;

    ret = dsdiff_io_write_int32_be(chunk, marker->offset);
    if (ret != DSDIFF_SUCCESS) return ret;

    ret = dsdiff_io_write_uint16_be(chunk, marker->mark_type);
    if (ret != DSDIFF_SUCCESS) return ret;

    ret = dsdiff_io_write_uint16_be(chunk, marker->mark_channel);
    if (ret != DSDIFF_SUCCESS) return ret;

    ret = dsdiff_io_write_uint16_be(chunk, marker->track_flags);
    if (ret != DSDIFF_SUCCESS) return ret;

    ret = dsdiff_io_write_uint32_be(chunk, text_len);
    if (ret != DSDIFF_SUCCESS) return ret;

    ret = dsdiff_io_write_string(chunk, text_len,
                                  marker->marker_text ? marker->marker_text : "");
    if (ret != DSDIFF_SUCCESS) return ret;

    if ((text_len + 22) % 2 != 0) {
        ret = dsdiff_io_write_pad_byte(chunk);
    }

    return ret;
}

int dsdiff_chunk_read_mark(dsdiff_chunk_t *chunk,
                               dsdiff_marker_t *marker) {
    uint64_t chunk_size;
    uint32_t size_read = 0;
    uint32_t text_len;
    char *text = NULL;
    int ret;

    if (!chunk || !marker) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    marker->marker_text = NULL;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_uint16_be(chunk, &marker->time.hours);
    if (ret != DSDIFF_SUCCESS) return ret;
    size_read += 2;

    ret = dsdiff_io_read_uint8(chunk, &marker->time.minutes);
    if (ret != DSDIFF_SUCCESS) return ret;
    size_read += 1;

    ret = dsdiff_io_read_uint8(chunk, &marker->time.seconds);
    if (ret != DSDIFF_SUCCESS) return ret;
    size_read += 1;

    ret = dsdiff_io_read_uint32_be(chunk, &marker->time.samples);
    if (ret != DSDIFF_SUCCESS) return ret;
    size_read += 4;

    ret = dsdiff_io_read_int32_be(chunk, &marker->offset);
    if (ret != DSDIFF_SUCCESS) return ret;
    size_read += 4;

    {
        uint16_t mark_type_temp;
        ret = dsdiff_io_read_uint16_be(chunk, &mark_type_temp);
        if (ret != DSDIFF_SUCCESS) return ret;
        marker->mark_type = (dsdiff_mark_type_t)mark_type_temp;
        size_read += 2;
    }

    {
        uint16_t mark_channel_temp;
        ret = dsdiff_io_read_uint16_be(chunk, &mark_channel_temp);
        if (ret != DSDIFF_SUCCESS) return ret;
        marker->mark_channel = (dsdiff_mark_channel_t)mark_channel_temp;
        size_read += 2;
    }

    {
        uint16_t track_flags_temp;
        ret = dsdiff_io_read_uint16_be(chunk, &track_flags_temp);
        if (ret != DSDIFF_SUCCESS) return ret;
        marker->track_flags = (dsdiff_track_flags_t)track_flags_temp;
        size_read += 2;
    }

    ret = dsdiff_io_read_uint32_be(chunk, &text_len);
    if (ret != DSDIFF_SUCCESS) return ret;
    size_read += 4;

    marker->text_length = text_len;

    if ((text_len + size_read) != chunk_size) {
        if ((text_len + size_read + 1) != chunk_size) {
            return DSDIFF_ERROR_INVALID_FILE;
        }
    }

    if (text_len > 0) {
      text = (char *)sa_malloc((text_len + 1) * sizeof(char));
        if (text == NULL) {
            return DSDIFF_ERROR_OUT_OF_MEMORY;
        }

        ret = dsdiff_io_read_string(chunk, text_len, text);
        if (ret != DSDIFF_SUCCESS) {
            sa_free(text);
            return ret;
        }

        marker->marker_text = text;
    }

    if (text_len % 2 != 0) {
        ret = dsdiff_io_read_pad_byte(chunk);
        if (ret != DSDIFF_SUCCESS) {
            if (marker->marker_text) {
                sa_free(marker->marker_text);
                marker->marker_text = NULL;
            }
            return ret;
        }
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * DIAR Chunk (Artist)
 * ===========================================================================*/

int dsdiff_chunk_write_diar(dsdiff_chunk_t *chunk, size_t text_length, const char *artist) {
    uint64_t chunk_size;
    uint32_t len32;
    int ret;

    if (!chunk || !artist) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (text_length > UINT32_MAX) {
        return DSDIFF_ERROR_INVALID_ARG;
    }
    len32 = (uint32_t)text_length;

    chunk_size = (uint64_t)len32 + 4;
    chunk_size += len32 % 2;

    ret = dsdiff_io_write_chunk_id(chunk, DIAR_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint32_be(chunk, len32);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_string(chunk, text_length, artist);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if ((len32 % 2) != 0) {
        ret = dsdiff_io_write_pad_byte(chunk);
    }

    return ret;
}

int dsdiff_chunk_read_diar(dsdiff_chunk_t *chunk, char **artist) {
    uint64_t chunk_size;
    uint32_t num_chars;
    char *text;
    int ret;

    if (!chunk || !artist) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *artist = NULL;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_uint32_be(chunk, &num_chars);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    text = (char *)sa_malloc(num_chars + 1);
    if (!text) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    ret = dsdiff_io_read_string(chunk, num_chars, text);
    if (ret != DSDIFF_SUCCESS) {
        sa_free(text);
        return ret;
    }

    if ((num_chars % 2) != 0) {
        ret = dsdiff_io_read_pad_byte(chunk);
        if (ret != DSDIFF_SUCCESS) {
            sa_free(text);
            return ret;
        }
    }

    *artist = text;
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * DITI Chunk (Title)
 * ===========================================================================*/

int dsdiff_chunk_write_diti(dsdiff_chunk_t *chunk, size_t text_length, const char *title) {
    uint64_t chunk_size;
    uint32_t len32;
    int ret;

    if (!chunk || !title) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (text_length > UINT32_MAX) {
        return DSDIFF_ERROR_INVALID_ARG;
    }
    len32 = (uint32_t)text_length;

    chunk_size = (uint64_t)len32 + 4;
    chunk_size += len32 % 2;

    ret = dsdiff_io_write_chunk_id(chunk, DITI_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint32_be(chunk, len32);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_string(chunk, text_length, title);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if ((len32 % 2) != 0) {
        ret = dsdiff_io_write_pad_byte(chunk);
    }

    return ret;
}

int dsdiff_chunk_read_diti(dsdiff_chunk_t *chunk, char **title) {
    uint64_t chunk_size;
    uint32_t num_chars;
    char *text;
    int ret;

    if (!chunk || !title) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *title = NULL;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_read_uint32_be(chunk, &num_chars);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    text = (char *)sa_malloc(num_chars + 1);
    if (!text) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    ret = dsdiff_io_read_string(chunk, num_chars, text);
    if (ret != DSDIFF_SUCCESS) {
        sa_free(text);
        return ret;
    }

    if ((num_chars % 2) != 0) {
        ret = dsdiff_io_read_pad_byte(chunk);
        if (ret != DSDIFF_SUCCESS) {
            sa_free(text);
            return ret;
        }
    }

    *title = text;
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * ID3 Chunk (ID3 Tag Data)
 * ===========================================================================*/

int dsdiff_chunk_write_id3(dsdiff_chunk_t *chunk, uint8_t *tag_data, uint32_t tag_size) {
    uint64_t chunk_data_size;
    size_t actual_written;
    int ret;

    if (!chunk || !tag_data || tag_size == 0) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    chunk_data_size = (uint64_t)tag_size;

    ret = dsdiff_io_write_chunk_id(chunk, ID3_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_uint64_be(chunk, chunk_data_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    ret = dsdiff_io_write_bytes(chunk, tag_data, tag_size, &actual_written);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if ((tag_size % 2) != 0) {
        ret = dsdiff_io_write_pad_byte(chunk);
    }

    return ret;
}

int dsdiff_chunk_read_id3(dsdiff_chunk_t *chunk, uint8_t **out_tag_data, uint32_t *out_tag_size) {
    uint64_t chunk_data_size;
    uint8_t *tag_buffer;
    size_t actual_read;
    int ret;

    if (!chunk || !out_tag_data || !out_tag_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *out_tag_data = NULL;
    *out_tag_size = 0;

    ret = dsdiff_io_read_uint64_be(chunk, &chunk_data_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (chunk_data_size == 0) {
        return DSDIFF_SUCCESS;
    }

    tag_buffer = (uint8_t *)sa_malloc((size_t)chunk_data_size);
    if (!tag_buffer) {
        return DSDIFF_ERROR_OUT_OF_MEMORY;
    }

    ret = dsdiff_io_read_bytes(chunk, tag_buffer, (size_t)chunk_data_size, &actual_read);
    if (ret != DSDIFF_SUCCESS) {
        sa_free(tag_buffer);
        return ret;
    }

    if ((chunk_data_size % 2) != 0) {
        ret = dsdiff_io_read_pad_byte(chunk);
        if (ret != DSDIFF_SUCCESS) {
            sa_free(tag_buffer);
            return ret;
        }
    }

    *out_tag_size = (uint32_t)chunk_data_size;
    *out_tag_data = tag_buffer;
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Manufacturer Specific Chunk
 * ===========================================================================*/

int dsdiff_chunk_read_manf(dsdiff_chunk_t *chunk, uint8_t *man_id,
                           uint8_t **data, uint32_t *data_size)
{
    uint64_t chunk_size;
    uint32_t man_data_size;
    uint8_t *man_data;
    size_t bytes_read;
    int ret;

    if (!chunk || !man_id || !data || !data_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *data = NULL;
    *data_size = 0;

    /* Read chunk size */
    ret = dsdiff_io_read_uint64_be(chunk, &chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (chunk_size < 4) {
        return DSDIFF_ERROR_INVALID_CHUNK;
    }

    /* Read manufacturer ID (4 bytes) */
    ret = dsdiff_io_read_bytes(chunk, man_id, 4, &bytes_read);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    /* Read manufacturer data */
    man_data_size = (uint32_t)(chunk_size - 4);
    if (man_data_size > 0) {
        man_data = (uint8_t *)sa_malloc(man_data_size);
        if (!man_data) {
            return DSDIFF_ERROR_OUT_OF_MEMORY;
        }

        ret = dsdiff_io_read_bytes(chunk, man_data, man_data_size, &bytes_read);
        if (ret != DSDIFF_SUCCESS) {
            sa_free(man_data);
            return ret;
        }

        *data = man_data;
        *data_size = man_data_size;
    }

    /* Handle padding byte */
    if ((chunk_size % 2) != 0) {
        ret = dsdiff_io_read_pad_byte(chunk);
        if (ret != DSDIFF_SUCCESS) {
            if (*data) {
                sa_free(*data);
                *data = NULL;
                *data_size = 0;
            }
            return ret;
        }
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_chunk_write_manf(dsdiff_chunk_t *chunk, const uint8_t *man_id,
                            const uint8_t *data, uint32_t data_size)
{
    uint64_t chunk_size;
    size_t bytes_written;
    int ret;

    if (!chunk || !man_id) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    /* chunk_size = manID (4) + manData[] */
    chunk_size = 4 + (uint64_t)data_size;

    /* Write chunk ID 'MANF' */
    ret = dsdiff_io_write_chunk_id(chunk, MANF_FOURCC);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    /* Write chunk size */
    ret = dsdiff_io_write_uint64_be(chunk, chunk_size);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    /* Write manufacturer ID */
    ret = dsdiff_io_write_bytes(chunk, man_id, 4, &bytes_written);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    /* Write manufacturer data */
    if (data_size > 0 && data != NULL) {
        ret = dsdiff_io_write_bytes(chunk, data, data_size, &bytes_written);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    /* Write padding if data size is odd */
    if ((data_size % 2) != 0) {
        ret = dsdiff_io_write_pad_byte(chunk);
    }

    return ret;
}
