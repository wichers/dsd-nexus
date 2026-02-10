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

#include <libdsf/dsf.h>

#include "dsf_io.h"

#include <libsautil/compat.h>
#include <libsautil/sastring.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/stat.h>

/* =============================================================================
 * File Operations
 * ===========================================================================*/

int dsf_io_open_write(dsf_io_t *io, const char *filename) {
    if (!io || !filename) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    sa_strlcpy(io->filename, filename, sizeof(io->filename));
    io->mode = DSF_FILE_MODE_WRITE;

    io->file = sa_fopen(filename, "wb");
    if (io->file == NULL) {
        return DSF_ERROR_GENERIC;
    }

    return DSF_SUCCESS;
}

int dsf_io_open_read(dsf_io_t *io, const char *filename) {
    if (!io || !filename) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    sa_strlcpy(io->filename, filename, sizeof(io->filename));
    io->mode = DSF_FILE_MODE_READ;

    io->file = sa_fopen(filename, "rb");
    if (io->file == NULL) {
        return DSF_ERROR_GENERIC;
    }

    return DSF_SUCCESS;
}

int dsf_io_open_modify(dsf_io_t *io, const char *filename) {
    if (!io || !filename) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    sa_strlcpy(io->filename, filename, sizeof(io->filename));
    io->mode = DSF_FILE_MODE_MODIFY;

    io->file = sa_fopen(filename, "r+b");
    if (io->file == NULL) {
        return DSF_ERROR_GENERIC;
    }

    return DSF_SUCCESS;
}

int dsf_io_close(dsf_io_t *io) {
    uint64_t current_pos;
    int ret;

    if (!io) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    /* Special handling for WRITE and MODIFY modes - truncate file to current position */
    if (io->mode == DSF_FILE_MODE_WRITE || io->mode == DSF_FILE_MODE_MODIFY) {
        if (io->file) {
            /* Get current file position before closing */
            int64_t pos = sa_ftell64(io->file);
            if (pos < 0) {
                fclose(io->file);
                return DSF_ERROR_GENERIC;
            }
            current_pos = (uint64_t)pos;

            ret = sa_chsize(sa_fileno(io->file), (int64_t)current_pos);
            if (ret != 0) {
                fclose(io->file);
                return DSF_ERROR_GENERIC;
            }

            /* Close the file */
            fclose(io->file);
            io->file = NULL;
        }
    } else {
        /* For READ mode, just close normally */
        if (io->file) {
            fclose(io->file);
        }
    }

    return DSF_SUCCESS;
}

int dsf_io_remove_file(dsf_io_t *io) {
    char filename[DSF_MAX_STR_SIZE];
    int ret;

    if (!io) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    sa_strlcpy(filename, io->filename, sizeof(filename));

    ret = dsf_io_close(io);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    if (sa_unlink(filename) != 0) {
        return DSF_ERROR_GENERIC;
    }

    return DSF_SUCCESS;
}

int dsf_io_get_filename(dsf_io_t *io, char *filename, size_t buffer_size) {
    if (!io || !filename || buffer_size == 0) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    sa_strlcpy(filename, io->filename, buffer_size);
    return DSF_SUCCESS;
}

int dsf_io_is_file_open(dsf_io_t *io, int *is_open) {
    if (!io || !is_open) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    *is_open = (io->file != NULL) ? 1 : 0;
    return DSF_SUCCESS;
}

/* =============================================================================
 * Position Operations
 * ===========================================================================*/

int dsf_io_seek(dsf_io_t *io, int64_t offset, dsf_seek_dir_t origin,
                   uint64_t *new_pos) {
    if (!io || !io->file) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (sa_fseek64(io->file, offset, origin) != 0) {
        return DSF_ERROR_GENERIC;
    }

    if (new_pos) {
        int64_t pos = sa_ftell64(io->file);
        if (pos < 0) {
            return DSF_ERROR_GENERIC;
        }
        *new_pos = (uint64_t)pos;
    }

    return DSF_SUCCESS;
}

int dsf_io_get_position(dsf_io_t *io, uint64_t *position) {
    if (!io || !io->file || !position) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    int64_t pos = sa_ftell64(io->file);
    if (pos < 0) {
        return DSF_ERROR_GENERIC;
    }
    *position = (uint64_t)pos;
    return DSF_SUCCESS;
}

int dsf_io_set_position(dsf_io_t *io, uint64_t position) {
    if (!io || !io->file) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    if (sa_fseek64(io->file, (int64_t)position, SEEK_SET) != 0) {
        return DSF_ERROR_GENERIC;
    }
    return DSF_SUCCESS;
}

int dsf_io_get_file_size(dsf_io_t *io, uint64_t *size) {
    if (!io || !io->file || !size) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    struct sa_stat_s buf;
    if (sa_fstat(sa_fileno(io->file), &buf) != 0) {
        return DSF_ERROR_GENERIC;
    }
    *size = (uint64_t)buf.st_size;

    return DSF_SUCCESS;
}

int dsf_io_claim_extra_size(dsf_io_t *io, uint64_t extra_bytes) {
    uint64_t current_size;
    int64_t position;
    int ret;

    if (!io || !io->file) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    struct sa_stat_s st;
    if (sa_fstat(sa_fileno(io->file), &st) != 0) {
        return DSF_ERROR_GENERIC;
    }
    current_size = (uint64_t)st.st_size;

    position = sa_ftell64(io->file);
    if (position < 0) {
        return DSF_ERROR_GENERIC;
    }

    ret = sa_chsize(sa_fileno(io->file), (int64_t)(current_size + extra_bytes));
    if (ret != 0) {
        return DSF_ERROR_GENERIC;
    }

    if (sa_fseek64(io->file, position, SEEK_SET) != 0) {
        return DSF_ERROR_GENERIC;
    }
    return DSF_SUCCESS;
}

/* =============================================================================
 * Chunk ID Operations
 * ===========================================================================*/

int dsf_io_read_chunk_id(dsf_io_t *io, uint32_t *chunk_id) {
    uint32_t id;
    int ret;

    if (!io || !chunk_id) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    ret = dsf_io_read_uint32_le(io, &id);
    if (ret != DSF_SUCCESS) {
        return ret;
    }

    *chunk_id = id;
    return DSF_SUCCESS;
}

int dsf_io_write_chunk_id(dsf_io_t *io, uint32_t chunk_id) {
    if (!io) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    return dsf_io_write_uint32_le(io, chunk_id);
}

/* =============================================================================
 * Integer I/O Operations
 * ===========================================================================*/

int dsf_io_read_uint8(dsf_io_t *io, uint8_t *data) {
    size_t bytes_read;

    if (!io || !io->file || !data) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    bytes_read = fread(data, 1, 1, io->file);
    if (bytes_read != 1) {
        return DSF_ERROR_READ;
    }

    return DSF_SUCCESS;
}

int dsf_io_write_uint8(dsf_io_t *io, uint8_t data) {
    size_t bytes_written;

    if (!io || !io->file) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    bytes_written = fwrite(&data, 1, 1, io->file);
    if (bytes_written != 1) {
        return DSF_ERROR_WRITE;
    }

    return DSF_SUCCESS;
}

int dsf_io_read_uint16_le(dsf_io_t *io, uint16_t *data) {
    uint16_t value;
    size_t bytes_read;

    if (!io || !io->file || !data) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    bytes_read = fread(&value, 1, 2, io->file);
    if (bytes_read != 2) {
        return DSF_ERROR_READ;
    }

    *data = sa_le2ne16(value);
    return DSF_SUCCESS;
    
}
int dsf_io_write_uint16_le(dsf_io_t *io, uint16_t data) {
    uint16_t value;
    size_t bytes_written;

    if (!io || !io->file) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    value = sa_le2ne16(data);

    bytes_written = fwrite(&value, 1, 2, io->file);
    if (bytes_written != 2) {
        return DSF_ERROR_WRITE;
    }

    return DSF_SUCCESS;
}

int dsf_io_read_uint32_le(dsf_io_t *io, uint32_t *data) {
    uint32_t value;
    size_t bytes_read;

    if (!io || !io->file || !data) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    bytes_read = fread(&value, 1, 4, io->file);
    if (bytes_read != 4) {
        return DSF_ERROR_READ;
    }

    *data = sa_le2ne32(value);
    return DSF_SUCCESS;
}

int dsf_io_write_uint32_le(dsf_io_t *io, uint32_t data) {
    uint32_t value;
    size_t bytes_written;

    if (!io || !io->file) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    value = sa_le2ne32(data);

    bytes_written = fwrite(&value, 1, 4, io->file);
    if (bytes_written != 4) {
        return DSF_ERROR_WRITE;
    }

    return DSF_SUCCESS;
}

int dsf_io_read_int32_le(dsf_io_t *io, int32_t *data) {
    int32_t value;
    size_t bytes_read;

    if (!io || !io->file || !data) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    bytes_read = fread(&value, 1, 4, io->file);
    if (bytes_read != 4) {
        return DSF_ERROR_READ;
    }

    *data = (int32_t)sa_le2ne32(value);
    return DSF_SUCCESS;
}

int dsf_io_write_int32_le(dsf_io_t *io, int32_t data) {
  int32_t value;
  size_t bytes_written;

  if (!io || !io->file) {
    return DSF_ERROR_INVALID_PARAMETER;
  }

  value = (int32_t) sa_le2ne32(data);

  bytes_written = fwrite(&value, 1, 4, io->file);
  if (bytes_written != 4) {
    return DSF_ERROR_WRITE;
  }

  return DSF_SUCCESS;
}

int dsf_io_read_uint64_le(dsf_io_t *io, uint64_t *data) {
    uint64_t value;
    size_t bytes_read;

    if (!io || !io->file || !data) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    bytes_read = fread(&value, 1, 8, io->file);
    if (bytes_read != 8) {
        return DSF_ERROR_READ;
    }

    *data = sa_le2ne64(value);
    return DSF_SUCCESS;
}

int dsf_io_write_uint64_le(dsf_io_t *io, uint64_t data) {
    uint64_t value;
    size_t bytes_written;

    if (!io || !io->file) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    value = sa_le2ne64(data);

    bytes_written = fwrite(&value, 1, 8, io->file);
    if (bytes_written != 8) {
        return DSF_ERROR_WRITE;
    }

    return DSF_SUCCESS;
}

/* =============================================================================
 * Raw Byte Operations
 * ===========================================================================*/

int dsf_io_read_bytes(dsf_io_t *io, uint8_t *buffer, size_t num_bytes,
                          size_t *bytes_read) {
    size_t read;

    if (!io || !io->file || !buffer) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    read = fread(buffer, 1, num_bytes, io->file);
    if (read != num_bytes) {
        return DSF_ERROR_READ;
    }
    *bytes_read = read;

    return DSF_SUCCESS;
}

int dsf_io_write_bytes(dsf_io_t *io, const uint8_t *buffer,
                           size_t num_bytes, size_t *bytes_written) {
    size_t written;

    if (!io || !io->file || !buffer) {
        return DSF_ERROR_INVALID_PARAMETER;
    }

    written = fwrite(buffer, 1, num_bytes, io->file);
    if (written != num_bytes) {
        return DSF_ERROR_WRITE;
    }
    *bytes_written = written;

    return DSF_SUCCESS;
}
