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

#include "dsdiff_io.h"

#include <libsautil/compat.h>
#include <libsautil/sastring.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

/* =============================================================================
 * File Open/Close Operations
 * ===========================================================================*/

int dsdiff_io_open_write(dsdiff_io_t *io, const char *filename) {
    if (!io || !filename) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    sa_strlcpy(io->filename, filename, sizeof(io->filename));
    io->mode = DSDIFF_FILE_MODE_WRITE;

    io->file = sa_fopen(filename, "wb");
    if (io->file == NULL) {
        return DSDIFF_ERROR_FILE_CREATE_FAILED;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_io_open_read(dsdiff_io_t *io, const char *filename) {
    if (!io || !filename) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    sa_strlcpy(io->filename, filename, sizeof(io->filename));
    io->mode = DSDIFF_FILE_MODE_READ;

    io->file = sa_fopen(filename, "rb");
    if (io->file == NULL) {
        return DSDIFF_ERROR_FILE_NOT_FOUND;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_io_open_modify(dsdiff_io_t *io, const char *filename) {
    if (!io || !filename) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    sa_strlcpy(io->filename, filename, sizeof(io->filename));
    io->mode = DSDIFF_FILE_MODE_MODIFY;

    io->file = sa_fopen(filename, "r+b");
    if (io->file == NULL) {
        return DSDIFF_ERROR_FILE_NOT_FOUND;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_io_close(dsdiff_io_t *io) {
    uint64_t current_pos;
    int ret;

    if (!io) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (io->mode == DSDIFF_FILE_MODE_WRITE || io->mode == DSDIFF_FILE_MODE_MODIFY) {
        if (io->file) {
            int64_t pos = sa_ftell64(io->file);
            if (pos < 0) {
                fclose(io->file);
                return DSDIFF_ERROR_SEEK_FAILED;
            }
            current_pos = (uint64_t)pos;

            ret = sa_chsize(sa_fileno(io->file), (int64_t)current_pos);
            if (ret != 0) {
                fclose(io->file);
                return DSDIFF_ERROR_WRITE_FAILED;
            }

            fclose(io->file);
            io->file = NULL;
        }
    } else {
        if (io->file) {
            fclose(io->file);
        }
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * File State Query Functions
 * ===========================================================================*/

int dsdiff_io_get_filename(dsdiff_io_t *io, char *filename, size_t buffer_size) {
    if (!io || !filename || buffer_size == 0) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    sa_strlcpy(filename, io->filename, buffer_size);
    return DSDIFF_SUCCESS;
}

int dsdiff_io_is_open(dsdiff_io_t *io, int *is_open) {
    if (!io || !is_open) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    *is_open = (io->file != NULL) ? 1 : 0;
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Position and Size Operations
 * ===========================================================================*/

int dsdiff_io_seek(dsdiff_io_t *io, int64_t offset, dsdiff_seek_dir_t origin,
                   uint64_t *new_pos) {
    if (!io || !io->file) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (sa_fseek64(io->file, offset, origin) != 0) {
        return DSDIFF_ERROR_SEEK_FAILED;
    }

    if (new_pos) {
        int64_t pos = sa_ftell64(io->file);
        if (pos < 0) {
            return DSDIFF_ERROR_SEEK_FAILED;
        }
        *new_pos = (uint64_t)pos;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_io_get_position(dsdiff_io_t *io, uint64_t *position) {
    if (!io || !io->file || !position) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    int64_t pos = sa_ftell64(io->file);
    if (pos < 0) {
        return DSDIFF_ERROR_SEEK_FAILED;
    }
    *position = (uint64_t)pos;
    return DSDIFF_SUCCESS;
}

int dsdiff_io_set_position(dsdiff_io_t *io, uint64_t position) {
    if (!io || !io->file) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (sa_fseek64(io->file, (int64_t)position, SEEK_SET) != 0) {
        return DSDIFF_ERROR_SEEK_FAILED;
    }
    return DSDIFF_SUCCESS;
}

int dsdiff_io_get_file_size(dsdiff_io_t *io, uint64_t *size) {
    if (!io || !io->file || !size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    struct sa_stat_s buf;
    if (sa_fstat(sa_fileno(io->file), &buf) != 0) {
        return DSDIFF_ERROR_READ_FAILED;
    }
    *size = (uint64_t)buf.st_size;

    return DSDIFF_SUCCESS;
}

int dsdiff_io_preallocate(dsdiff_io_t *io, uint64_t extra_bytes) {
    uint64_t current_size;
    int64_t position;
    int ret;

    if (!io || !io->file) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    struct sa_stat_s st;
    if (sa_fstat(sa_fileno(io->file), &st) != 0) {
        return DSDIFF_ERROR_READ_FAILED;
    }
    current_size = (uint64_t)st.st_size;

    position = sa_ftell64(io->file);
    if (position < 0) {
        return DSDIFF_ERROR_SEEK_FAILED;
    }

    ret = sa_chsize(sa_fileno(io->file), (int64_t)(current_size + extra_bytes));
    if (ret != 0) {
        return DSDIFF_ERROR_WRITE_FAILED;
    }

    if (sa_fseek64(io->file, position, SEEK_SET) != 0) {
        return DSDIFF_ERROR_SEEK_FAILED;
    }
    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Chunk ID Operations
 * ===========================================================================*/

int dsdiff_io_read_chunk_id(dsdiff_io_t *io, uint32_t *chunk_id) {
    size_t bytes_read;
    uint32_t id;
    int ret;

    if (!io || !chunk_id) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_read_bytes(io, (uint8_t *) &id, 4, &bytes_read);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    *chunk_id = id;
    return DSDIFF_SUCCESS;
}

int dsdiff_io_write_chunk_id(dsdiff_io_t *io, uint32_t chunk_id) {
    if (!io) {
        return DSDIFF_ERROR_INVALID_ARG;
    }
    size_t written;

    return dsdiff_io_write_bytes(io, (uint8_t *) &chunk_id, 4, &written);
}

/* =============================================================================
 * Integer I/O Operations (Big-Endian)
 *
 * DSDIFF uses big-endian byte order for all multi-byte integers.
 * These functions handle the byte-swapping on little-endian systems.
 * ===========================================================================*/

int dsdiff_io_read_uint8(dsdiff_io_t *io, uint8_t *data) {
    size_t bytes_read;

    if (!io || !io->file || !data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    bytes_read = fread(data, 1, 1, io->file);
    if (bytes_read != 1) {
        return DSDIFF_ERROR_READ_FAILED;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_io_write_uint8(dsdiff_io_t *io, uint8_t data) {
    size_t bytes_written;

    if (!io || !io->file) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    bytes_written = fwrite(&data, 1, 1, io->file);
    if (bytes_written != 1) {
        return DSDIFF_ERROR_WRITE_FAILED;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_io_read_uint16_be(dsdiff_io_t *io, uint16_t *data) {
    uint16_t value;
    size_t bytes_read;

    if (!io || !io->file || !data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    bytes_read = fread(&value, 1, 2, io->file);
    if (bytes_read != 2) {
        return DSDIFF_ERROR_READ_FAILED;
    }

    *data = sa_be2ne16(value);
    return DSDIFF_SUCCESS;
}

int dsdiff_io_write_uint16_be(dsdiff_io_t *io, uint16_t data) {
    uint16_t value;
    size_t bytes_written;

    if (!io || !io->file) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    value = sa_be2ne16(data);

    bytes_written = fwrite(&value, 1, 2, io->file);
    if (bytes_written != 2) {
        return DSDIFF_ERROR_WRITE_FAILED;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_io_read_uint32_be(dsdiff_io_t *io, uint32_t *data) {
    uint32_t value;
    size_t bytes_read;

    if (!io || !io->file || !data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    bytes_read = fread(&value, 1, 4, io->file);
    if (bytes_read != 4) {
        return DSDIFF_ERROR_READ_FAILED;
    }

    *data = sa_be2ne32(value);
    return DSDIFF_SUCCESS;
}

int dsdiff_io_write_uint32_be(dsdiff_io_t *io, uint32_t data) {
    uint32_t value;
    size_t bytes_written;

    if (!io || !io->file) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    value = sa_be2ne32(data);

    bytes_written = fwrite(&value, 1, 4, io->file);
    if (bytes_written != 4) {
        return DSDIFF_ERROR_WRITE_FAILED;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_io_read_int32_be(dsdiff_io_t *io, int32_t *data) {
    int32_t value;
    size_t bytes_read;

    if (!io || !io->file || !data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    bytes_read = fread(&value, 1, 4, io->file);
    if (bytes_read != 4) {
        return DSDIFF_ERROR_READ_FAILED;
    }

    *data = (int32_t)sa_be2ne32(value);
    return DSDIFF_SUCCESS;
}

int dsdiff_io_write_int32_be(dsdiff_io_t *io, int32_t data) {
    int32_t value;
    size_t bytes_written;

    if (!io || !io->file) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    value = (int32_t)sa_be2ne32(data);

    bytes_written = fwrite(&value, 1, 4, io->file);
    if (bytes_written != 4) {
        return DSDIFF_ERROR_WRITE_FAILED;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_io_read_uint64_be(dsdiff_io_t *io, uint64_t *data) {
    uint64_t value;
    size_t bytes_read;

    if (!io || !io->file || !data) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    bytes_read = fread(&value, 1, 8, io->file);
    if (bytes_read != 8) {
        return DSDIFF_ERROR_READ_FAILED;
    }

    *data = sa_be2ne64(value);
    return DSDIFF_SUCCESS;
}

int dsdiff_io_write_uint64_be(dsdiff_io_t *io, uint64_t data) {
    uint64_t value;
    size_t bytes_written;

    if (!io || !io->file) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    value = sa_be2ne64(data);

    bytes_written = fwrite(&value, 1, 8, io->file);
    if (bytes_written != 8) {
        return DSDIFF_ERROR_WRITE_FAILED;
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Padding Operations
 * ===========================================================================*/

int dsdiff_io_read_pad_byte(dsdiff_io_t *io) {
    uint8_t pad;
    return dsdiff_io_read_uint8(io, &pad);
}

int dsdiff_io_write_pad_byte(dsdiff_io_t *io) {
    return dsdiff_io_write_uint8(io, 0);
}

/* =============================================================================
 * String I/O Operations
 *
 * DSDIFF uses Pascal-style strings (pstrings) with a length byte prefix.
 * Strings are padded to even length for word alignment.
 * ===========================================================================*/

int dsdiff_io_read_pstring(dsdiff_io_t *io, uint16_t *length,
                            char *string, size_t buffer_size) {
    uint8_t len;
    uint16_t str_len;
    int ret;

    if (!io || !length || !string) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    ret = dsdiff_io_read_uint8(io, &len);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    str_len = len;

    if (str_len > *length) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (str_len >= buffer_size) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (str_len > 0) {
        ret = dsdiff_io_read_bytes(io, (uint8_t *)string, str_len, NULL);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    string[str_len] = '\0';

    str_len++;

    if (str_len % 2 != 0) {
        ret = dsdiff_io_read_pad_byte(io);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
        str_len++;
    }

    *length = str_len;

    return DSDIFF_SUCCESS;
}

int dsdiff_io_write_pstring(dsdiff_io_t *io, uint16_t length,
                             const char *string) {
    uint8_t str_len;
    uint16_t total_len;
    int ret;
    size_t actual_len;

    (void)length;

    if (!io || !string) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    actual_len = strlen(string);
    str_len = (actual_len > 255) ? 255 : (uint8_t)actual_len;

    ret = dsdiff_io_write_uint8(io, str_len);
    if (ret != DSDIFF_SUCCESS) {
        return ret;
    }

    if (str_len > 0) {
        ret = dsdiff_io_write_bytes(io, (const uint8_t *)string, str_len, NULL);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    total_len = str_len + 1;

    if (total_len % 2 != 0) {
        ret = dsdiff_io_write_pad_byte(io);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_io_read_string(dsdiff_io_t *io, size_t length,
                           char *string) {
    int ret;

    if (!io || !string) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (length > 0) {
        ret = dsdiff_io_read_bytes(io, (uint8_t *)string, length, NULL);
        if (ret != DSDIFF_SUCCESS) {
            return ret;
        }
    }

    string[length] = '\0';

    return DSDIFF_SUCCESS;
}

int dsdiff_io_write_string(dsdiff_io_t *io, size_t length,
                            const char *string) {
    if (!io || !string) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    if (length > 0) {
        return dsdiff_io_write_bytes(io, (const uint8_t *)string, length, NULL);
    }

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Raw Byte Operations
 * ===========================================================================*/

int dsdiff_io_read_bytes(dsdiff_io_t *io, uint8_t *buffer, size_t byte_count,
                         size_t *out_bytes_read) {
    size_t actual_read;

    if (!io || !io->file || !buffer) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    actual_read = fread(buffer, 1, byte_count, io->file);
    if (actual_read != byte_count) {
        return DSDIFF_ERROR_READ_FAILED;
    }

    if (out_bytes_read) {
        *out_bytes_read = actual_read;
    }

    return DSDIFF_SUCCESS;
}

int dsdiff_io_write_bytes(dsdiff_io_t *io, const uint8_t *buffer,
                          size_t byte_count, size_t *out_bytes_written) {
    size_t actual_written;

    if (!io || !io->file || !buffer) {
        return DSDIFF_ERROR_INVALID_ARG;
    }

    actual_written = fwrite(buffer, 1, byte_count, io->file);
    if (actual_written != byte_count) {
        return DSDIFF_ERROR_WRITE_FAILED;
    }

    if (out_bytes_written) {
        *out_bytes_written = actual_written;
    }

    return DSDIFF_SUCCESS;
}
