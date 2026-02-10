/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD VFS Usage Example - libsacd
 * Demonstrates how to use the virtual filesystem layer to access SACD ISO
 * contents as DSF files with on-the-fly conversion.
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

#include "sacd_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <libsautil/mem.h>
#include <libsautil/compat.h>

/* Callback context for directory listing */
typedef struct {
    int count;
    int verbose;
} readdir_context_t;

/* Callback function for directory listing */
static int readdir_callback(const sacd_vfs_entry_t *entry, void *userdata)
{
    readdir_context_t *ctx = (readdir_context_t *)userdata;
    const char *type_str = (entry->type == SACD_VFS_ENTRY_DIRECTORY) ? "DIR " : "FILE";

    if (ctx->verbose) {
        if (entry->type == SACD_VFS_ENTRY_FILE) {
            printf("  [%s] %s (%" PRIu64 " bytes, track %u)\n",
                   type_str, entry->name, entry->size, entry->track_num);
        } else {
            printf("  [%s] %s/\n", type_str, entry->name);
        }
    }
    ctx->count++;
    return 0; /* Continue listing */
}

/* Print file information */
static void print_file_info(const sacd_vfs_file_info_t *info)
{
    printf("\n  File Information:\n");
    printf("    Total size:      %" PRIu64 " bytes\n", info->total_size);
    printf("    Header size:     %" PRIu64 " bytes\n", info->header_size);
    printf("    Audio data size: %" PRIu64 " bytes\n", info->audio_data_size);
    printf("    Channels:        %u\n", info->channel_count);
    printf("    Sample rate:     %u Hz\n", info->sample_rate);
    printf("    Sample count:    %" PRIu64 "\n", info->sample_count);
    printf("    Duration:        %.2f seconds\n", info->duration_seconds);
    printf("    Frame format:    %s\n",
           info->frame_format == SACD_VFS_FRAME_DST ? "DST (compressed)" : "DSD (raw)");
    if (info->metadata_offset > 0) {
        printf("    ID3 metadata:    %" PRIu64 " bytes at offset %" PRIu64 "\n",
               info->metadata_size, info->metadata_offset);
    } else {
        printf("    ID3 metadata:    None\n");
    }
}

/* Demonstrate reading and seeking within a virtual DSF file */
static int demonstrate_file_operations(sacd_vfs_ctx_t *ctx, const char *filepath)
{
    sacd_vfs_file_t *file = NULL;
    sacd_vfs_file_info_t info;
    uint8_t buffer[4096];
    size_t bytes_read;
    uint64_t position;
    int result;

    printf("\nOpening virtual file: %s\n", filepath);

    /* Open the virtual DSF file */
    result = sacd_vfs_file_open(ctx, filepath, &file);
    if (result != SACD_VFS_OK) {
        fprintf(stderr, "Error opening file: %s\n", sacd_vfs_error_string(result));
        return result;
    }

    /* Get file information */
    result = sacd_vfs_file_get_info(file, &info);
    if (result != SACD_VFS_OK) {
        fprintf(stderr, "Error getting file info: %s\n", sacd_vfs_error_string(result));
        sacd_vfs_file_close(file);
        return result;
    }
    print_file_info(&info);

    /* Read DSF header (first 92 bytes) */
    printf("\n  Reading DSF header...\n");
    result = sacd_vfs_file_read(file, buffer, DSF_DSD_CHUNK_SIZE + DSF_FMT_CHUNK_SIZE, &bytes_read);
    if (result == SACD_VFS_OK && bytes_read >= 4) {
        printf("    DSF magic: %.4s\n", buffer);
        printf("    Read %zu header bytes\n", bytes_read);
    }

    /* Get current position */
    sacd_vfs_file_tell(file, &position);
    printf("    Current position: %" PRIu64 "\n", position);

    /* Seek to audio data start and read a sample */
    printf("\n  Seeking to audio data (offset %d)...\n", DSF_AUDIO_DATA_OFFSET + DSF_DATA_CHUNK_HEADER_SIZE);
    result = sacd_vfs_file_seek(file, DSF_AUDIO_DATA_OFFSET + DSF_DATA_CHUNK_HEADER_SIZE, SEEK_SET);
    if (result == SACD_VFS_OK) {
        result = sacd_vfs_file_read(file, buffer, 64, &bytes_read);
        if (result == SACD_VFS_OK) {
            printf("    Read %zu bytes of audio data\n", bytes_read);
            printf("    First 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                   buffer[0], buffer[1], buffer[2], buffer[3],
                   buffer[4], buffer[5], buffer[6], buffer[7]);
        }
    }

    /* If there's ID3 metadata, seek to it and read */
    if (info.metadata_offset > 0 && info.metadata_size > 0) {
        printf("\n  Seeking to ID3 metadata (offset %" PRIu64 ")...\n", info.metadata_offset);
        result = sacd_vfs_file_seek(file, (int64_t)info.metadata_offset, SEEK_SET);
        if (result == SACD_VFS_OK) {
            size_t to_read = (info.metadata_size < sizeof(buffer)) ? info.metadata_size : sizeof(buffer);
            result = sacd_vfs_file_read(file, buffer, to_read, &bytes_read);
            if (result == SACD_VFS_OK && bytes_read >= 10) {
                printf("    ID3 header: %.3s (version 2.%d.%d)\n",
                       buffer, buffer[3], buffer[4]);
                printf("    Read %zu bytes of ID3 data\n", bytes_read);
            }
        }
    }

    /* Demonstrate SEEK_END (seek from end of file) */
    printf("\n  Seeking to end of file...\n");
    result = sacd_vfs_file_seek(file, 0, SEEK_END);
    if (result == SACD_VFS_OK) {
        sacd_vfs_file_tell(file, &position);
        printf("    Position at end: %" PRIu64 " (file size: %" PRIu64 ")\n",
               position, info.total_size);
    }

    /* Seek back 100 bytes and read */
    printf("\n  Seeking back 100 bytes from end...\n");
    result = sacd_vfs_file_seek(file, -100, SEEK_END);
    if (result == SACD_VFS_OK) {
        sacd_vfs_file_tell(file, &position);
        printf("    Position: %" PRIu64 "\n", position);
        result = sacd_vfs_file_read(file, buffer, 100, &bytes_read);
        if (result == SACD_VFS_OK) {
            printf("    Read %zu bytes from near end of file\n", bytes_read);
        }
    }

    /* Close file */
    sacd_vfs_file_close(file);
    printf("\n  File closed successfully\n");

    return SACD_VFS_OK;
}

/* List directory contents */
static int list_directory(sacd_vfs_ctx_t *ctx, const char *path, int verbose)
{
    readdir_context_t rd_ctx = { 0, verbose };
    int result;

    printf("\nListing directory: %s\n", path);

    result = sacd_vfs_readdir(ctx, path, readdir_callback, &rd_ctx);
    if (result < 0) {
        fprintf(stderr, "Error listing directory: %s\n", sacd_vfs_error_string(result));
        return result;
    }

    printf("  Found %d entries\n", rd_ctx.count);
    return SACD_VFS_OK;
}

#define DUMP_CHUNK_SIZE (1024 * 1024)  /* 1 MB */

/* Write buffer to file */
static int write_dump_file(const char *filename, const uint8_t *data, size_t size)
{
    FILE *fp = sa_fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot create file '%s'\n", filename);
        return -1;
    }

    size_t written = fwrite(data, 1, size, fp);
    fclose(fp);

    if (written != size) {
        fprintf(stderr, "Error: Only wrote %zu of %zu bytes\n", written, size);
        return -1;
    }

    printf("    Wrote %zu bytes to '%s'\n", written, filename);
    return 0;
}

/* Dump track to disc with seek demonstration:
 * 1. Dump first 1MB
 * 2. Seek to end and dump last 1MB
 * 3. Seek back to 1MB and dump the middle
 */
static int dump_track_to_disc(sacd_vfs_ctx_t *ctx, const char *filepath, uint8_t track_num)
{
    sacd_vfs_file_t *file = NULL;
    sacd_vfs_file_info_t info;
    uint8_t *buffer = NULL;
    size_t bytes_read;
    size_t total_read;
    uint64_t position;
    int result;
    char output_filename[512];

    printf("\n=== Dumping Track %u to Disc ===\n", track_num);
    printf("Opening virtual file: %s\n", filepath);

    /* Open the virtual DSF file */
    result = sacd_vfs_file_open(ctx, filepath, &file);
    if (result != SACD_VFS_OK) {
        fprintf(stderr, "Error opening file: %s\n", sacd_vfs_error_string(result));
        return result;
    }

    /* Get file information */
    result = sacd_vfs_file_get_info(file, &info);
    if (result != SACD_VFS_OK) {
        fprintf(stderr, "Error getting file info: %s\n", sacd_vfs_error_string(result));
        sacd_vfs_file_close(file);
        return result;
    }

    printf("File size: %" PRIu64 " bytes (%.2f MB)\n",
           info.total_size, (double)info.total_size / (1024.0 * 1024.0));

    /* Allocate read buffer */
    buffer = (uint8_t *)sa_malloc(DUMP_CHUNK_SIZE);
    if (!buffer) {
        fprintf(stderr, "Error: Cannot allocate buffer\n");
        sacd_vfs_file_close(file);
        return SACD_VFS_ERROR_MEMORY;
    }

    /* Determine actual chunk sizes based on file size */
    size_t first_chunk = (info.total_size < DUMP_CHUNK_SIZE) ? (size_t)info.total_size : DUMP_CHUNK_SIZE;
    size_t last_chunk = first_chunk;
    size_t middle_start = first_chunk;
    size_t middle_size = 0;

    if (info.total_size > 2 * DUMP_CHUNK_SIZE) {
        middle_size = (size_t)(info.total_size - 2 * DUMP_CHUNK_SIZE);
        if (middle_size > DUMP_CHUNK_SIZE) {
            middle_size = DUMP_CHUNK_SIZE;  /* Cap middle dump at 1MB too */
        }
    }

    /* ===== STEP 1: Dump first 1MB ===== */
    printf("\n[Step 1] Reading first %zu bytes from start...\n", first_chunk);

    result = sacd_vfs_file_seek(file, 0, SEEK_SET);
    if (result != SACD_VFS_OK) {
        fprintf(stderr, "Error seeking to start: %s\n", sacd_vfs_error_string(result));
        sa_free(buffer);
        sacd_vfs_file_close(file);
        return result;
    }

    total_read = 0;
    while (total_read < first_chunk) {
        size_t to_read = first_chunk - total_read;
        if (to_read > 65536) to_read = 65536;  /* Read in 64KB chunks */

        result = sacd_vfs_file_read(file, buffer + total_read, to_read, &bytes_read);
        if (result != SACD_VFS_OK && result != SACD_VFS_ERROR_EOF) {
            fprintf(stderr, "Error reading: %s\n", sacd_vfs_error_string(result));
            break;
        }
        if (bytes_read == 0) break;
        total_read += bytes_read;

        /* Progress indicator */
        if (total_read % (256 * 1024) == 0 || total_read >= first_chunk) {
            printf("\r    Progress: %zu / %zu bytes (%.1f%%)",
                   total_read, first_chunk, 100.0 * total_read / first_chunk);
            fflush(stdout);
        }
    }
    printf("\n");

    sacd_vfs_file_tell(file, &position);
    printf("    Position after read: %" PRIu64 "\n", position);

    snprintf(output_filename, sizeof(output_filename), "track%02u_first_1mb.bin", track_num);
    write_dump_file(output_filename, buffer, total_read);

    /* Verify DSF header in first chunk */
    if (total_read >= 4 && memcmp(buffer, "DSD ", 4) == 0) {
        printf("    DSF header verified: %.4s\n", buffer);
    }

    /* ===== STEP 2: Seek to end and dump last 1MB ===== */
    printf("\n[Step 2] Seeking to end and reading last %zu bytes...\n", last_chunk);

    int64_t seek_offset = -(int64_t)last_chunk;
    result = sacd_vfs_file_seek(file, seek_offset, SEEK_END);
    if (result != SACD_VFS_OK) {
        fprintf(stderr, "Error seeking to end: %s\n", sacd_vfs_error_string(result));
        sa_free(buffer);
        sacd_vfs_file_close(file);
        return result;
    }

    sacd_vfs_file_tell(file, &position);
    printf("    Position after SEEK_END(%+" PRId64 "): %" PRIu64 "\n", seek_offset, position);

    total_read = 0;
    while (total_read < last_chunk) {
        size_t to_read = last_chunk - total_read;
        if (to_read > 65536) to_read = 65536;

        result = sacd_vfs_file_read(file, buffer + total_read, to_read, &bytes_read);
        if (result != SACD_VFS_OK && result != SACD_VFS_ERROR_EOF) {
            fprintf(stderr, "Error reading: %s\n", sacd_vfs_error_string(result));
            break;
        }
        if (bytes_read == 0) break;
        total_read += bytes_read;

        if (total_read % (256 * 1024) == 0 || total_read >= last_chunk) {
            printf("\r    Progress: %zu / %zu bytes (%.1f%%)",
                   total_read, last_chunk, 100.0 * total_read / last_chunk);
            fflush(stdout);
        }
    }
    printf("\n");

    sacd_vfs_file_tell(file, &position);
    printf("    Position after read: %" PRIu64 " (EOF: %" PRIu64 ")\n", position, info.total_size);

    snprintf(output_filename, sizeof(output_filename), "track%02u_last_1mb.bin", track_num);
    write_dump_file(output_filename, buffer, total_read);

    /* Check for ID3 tag at end */
    if (total_read >= 10 && info.metadata_size > 0) {
        /* ID3 should be at the end of the file */
        size_t id3_offset_in_buffer = 0;
        if (info.total_size - last_chunk < info.metadata_offset) {
            id3_offset_in_buffer = (size_t)(info.metadata_offset - (info.total_size - last_chunk));
        }
        if (id3_offset_in_buffer < total_read &&
            memcmp(buffer + id3_offset_in_buffer, "ID3", 3) == 0) {
            printf("    ID3 tag found at buffer offset %zu: ID3v2.%d.%d\n",
                   id3_offset_in_buffer, buffer[id3_offset_in_buffer + 3],
                   buffer[id3_offset_in_buffer + 4]);
        }
    }

    /* Check for padding bytes (0x69) before ID3 */
    if (total_read > 0 && info.metadata_offset > 0) {
        /* Find where padding should be */
        size_t check_offset = 0;
        if (info.total_size - last_chunk < info.metadata_offset) {
            check_offset = (size_t)(info.metadata_offset - (info.total_size - last_chunk));
            if (check_offset > 10) {
                /* Check bytes just before ID3 for padding */
                int padding_count = 0;
                for (size_t i = check_offset - 1; i > 0 && i > check_offset - 100; i--) {
                    if (buffer[i] == 0x69) padding_count++;
                    else break;
                }
                if (padding_count > 0) {
                    printf("    Found %d padding bytes (0x69) before ID3 tag\n", padding_count);
                }
            }
        }
    }

    /* ===== STEP 3: Seek back to 1MB and dump middle ===== */
    if (middle_size > 0) {
        printf("\n[Step 3] Seeking back to offset %zu and reading middle %zu bytes...\n",
               middle_start, middle_size);

        result = sacd_vfs_file_seek(file, (int64_t)middle_start, SEEK_SET);
        if (result != SACD_VFS_OK) {
            fprintf(stderr, "Error seeking to middle: %s\n", sacd_vfs_error_string(result));
            sa_free(buffer);
            sacd_vfs_file_close(file);
            return result;
        }

        sacd_vfs_file_tell(file, &position);
        printf("    Position after SEEK_SET(%" PRIu64 "): %" PRIu64 "\n",
               (uint64_t)middle_start, position);

        total_read = 0;
        while (total_read < middle_size) {
            size_t to_read = middle_size - total_read;
            if (to_read > 65536) to_read = 65536;

            result = sacd_vfs_file_read(file, buffer + total_read, to_read, &bytes_read);
            if (result != SACD_VFS_OK && result != SACD_VFS_ERROR_EOF) {
                fprintf(stderr, "Error reading: %s\n", sacd_vfs_error_string(result));
                break;
            }
            if (bytes_read == 0) break;
            total_read += bytes_read;

            if (total_read % (256 * 1024) == 0 || total_read >= middle_size) {
                printf("\r    Progress: %zu / %zu bytes (%.1f%%)",
                       total_read, middle_size, 100.0 * total_read / middle_size);
                fflush(stdout);
            }
        }
        printf("\n");

        sacd_vfs_file_tell(file, &position);
        printf("    Position after read: %" PRIu64 "\n", position);

        snprintf(output_filename, sizeof(output_filename), "track%02u_middle_1mb.bin", track_num);
        write_dump_file(output_filename, buffer, total_read);

        /* Show sample of middle data */
        if (total_read >= 16) {
            printf("    First 16 bytes of middle: ");
            for (int i = 0; i < 16; i++) {
                printf("%02X ", buffer[i]);
            }
            printf("\n");
        }
    } else {
        printf("\n[Step 3] Skipped - file too small for middle dump\n");
    }

    /* ===== STEP 4: Write entire file to disk ===== */
    printf("\n[Step 4] Writing entire file to disk...\n");

    /* Seek back to start */
    result = sacd_vfs_file_seek(file, 0, SEEK_SET);
    if (result != SACD_VFS_OK) {
        fprintf(stderr, "Error seeking to start for full dump: %s\n", sacd_vfs_error_string(result));
        sa_free(buffer);
        sacd_vfs_file_close(file);
        return result;
    }

    snprintf(output_filename, sizeof(output_filename), "track%02u_full.dsf", track_num);
    FILE *full_fp = sa_fopen(output_filename, "wb");
    if (!full_fp) {
        fprintf(stderr, "Error: Cannot create file '%s'\n", output_filename);
        sa_free(buffer);
        sacd_vfs_file_close(file);
        return -1;
    }

    uint64_t total_written = 0;
    uint64_t file_size = info.total_size;
    printf("    Writing %" PRIu64 " bytes (%.2f MB)...\n",
           file_size, (double)file_size / (1024.0 * 1024.0));

    while (total_written < file_size) {
        size_t to_read = DUMP_CHUNK_SIZE;
        if (file_size - total_written < to_read) {
            to_read = (size_t)(file_size - total_written);
        }

        /* Read in chunks */
        size_t chunk_read = 0;
        while (chunk_read < to_read) {
            size_t request = to_read - chunk_read;
            if (request > 65536) request = 65536;

            result = sacd_vfs_file_read(file, buffer + chunk_read, request, &bytes_read);
            if (result != SACD_VFS_OK && result != SACD_VFS_ERROR_EOF) {
                fprintf(stderr, "\nError reading at offset %" PRIu64 ": %s\n",
                        total_written + chunk_read, sacd_vfs_error_string(result));
                break;
            }
            if (bytes_read == 0) break;
            chunk_read += bytes_read;
        }

        if (chunk_read == 0) break;

        /* Write to file */
        size_t written = fwrite(buffer, 1, chunk_read, full_fp);
        if (written != chunk_read) {
            fprintf(stderr, "\nError writing to file\n");
            break;
        }

        total_written += chunk_read;

        /* Progress indicator every 1MB or at completion */
        if (total_written % (1024 * 1024) == 0 || total_written >= file_size) {
            double percent = 100.0 * total_written / file_size;
            printf("\r    Progress: %" PRIu64 " / %" PRIu64 " bytes (%.1f%%)",
                   total_written, file_size, percent);
            fflush(stdout);
        }
    }
    printf("\n");

    fclose(full_fp);

    if (total_written == file_size) {
        printf("    Successfully wrote %" PRIu64 " bytes to '%s'\n", total_written, output_filename);
    } else {
        printf("    Warning: Only wrote %" PRIu64 " of %" PRIu64 " bytes\n",
               total_written, file_size);
    }

    /* Verify the written file */
    printf("\n    Verifying written file...\n");
    FILE *verify_fp = sa_fopen(output_filename, "rb");
    if (verify_fp) {
        uint8_t verify_buf[16];

        /* Check DSF header */
        if (fread(verify_buf, 1, 4, verify_fp) == 4) {
            if (memcmp(verify_buf, "DSD ", 4) == 0) {
                printf("    DSF header: OK (DSD )\n");
            } else {
                printf("    DSF header: MISMATCH (%.4s)\n", verify_buf);
            }
        }

        /* Check ID3 tag at end if present */
        if (info.metadata_size > 0 && info.metadata_offset > 0) {
            if (sa_fseek64(verify_fp, (int64_t)info.metadata_offset, SEEK_SET) == 0) {
                if (fread(verify_buf, 1, 10, verify_fp) >= 3) {
                    if (memcmp(verify_buf, "ID3", 3) == 0) {
                        printf("    ID3 tag at offset %" PRIu64 ": OK (ID3v2.%d.%d)\n",
                               info.metadata_offset, verify_buf[3], verify_buf[4]);
                    } else {
                        printf("    ID3 tag: MISMATCH at offset %" PRIu64 "\n",
                               info.metadata_offset);
                    }
                }
            }
        }

        /* Check file size */
        sa_fseek64(verify_fp, 0, SEEK_END);
        int64_t actual_size = sa_ftell64(verify_fp);
        if ((uint64_t)actual_size == file_size) {
            printf("    File size: OK (%" PRIu64 " bytes)\n", file_size);
        } else {
            printf("    File size: MISMATCH (expected %" PRIu64 ", got %" PRId64 ")\n",
                   file_size, actual_size);
        }

        fclose(verify_fp);
    }

    /* Summary */
    printf("\n=== Dump Summary ===\n");
    printf("Created output files:\n");
    printf("  - track%02u_first_1mb.bin  (start of file)\n", track_num);
    printf("  - track%02u_last_1mb.bin   (end of file)\n", track_num);
    if (middle_size > 0) {
        printf("  - track%02u_middle_1mb.bin (middle of file)\n", track_num);
    }
    printf("  - track%02u_full.dsf       (complete DSF file)\n", track_num);

    sa_free(buffer);
    sacd_vfs_file_close(file);
    return SACD_VFS_OK;
}

int main(int argc, char *argv[])
{
    sacd_vfs_ctx_t *ctx = NULL;
    char album_name[SACD_VFS_MAX_FILENAME];
    char track_filename[SACD_VFS_MAX_FILENAME];
    char filepath[SACD_VFS_MAX_PATH];
    uint8_t track_count;
    int result;
    int dump_track = 0;  /* 0 = no dump, >0 = track number to dump */

    /* Check command line arguments */
    if (argc < 2) {
        fprintf(stderr, "SACD VFS Usage Example\n");
        fprintf(stderr, "Usage: %s <sacd_iso_file> [track_number]\n\n", argv[0]);
        fprintf(stderr, "This example demonstrates:\n");
        fprintf(stderr, "  - Opening an SACD ISO as a virtual filesystem\n");
        fprintf(stderr, "  - Browsing virtual directories\n");
        fprintf(stderr, "  - Reading virtual DSF files with on-the-fly conversion\n");
        fprintf(stderr, "  - Seeking within virtual files\n");
        fprintf(stderr, "  - Accessing ID3 metadata\n");
        fprintf(stderr, "\nOptional track_number parameter:\n");
        fprintf(stderr, "  If specified, dumps the track to disc demonstrating seek:\n");
        fprintf(stderr, "    1. First 1MB from start\n");
        fprintf(stderr, "    2. Last 1MB from end (seek to end)\n");
        fprintf(stderr, "    3. Middle 1MB (seek back to offset 1MB)\n");
        return 1;
    }

    /* Parse optional track number */
    if (argc >= 3) {
        dump_track = atoi(argv[2]);
        if (dump_track < 1 || dump_track > 255) {
            fprintf(stderr, "Error: Track number must be between 1 and 255\n");
            return 1;
        }
    }

    printf("=== SACD Virtual Filesystem Example ===\n\n");

    /* Create VFS context */
    printf("Creating VFS context...\n");
    ctx = sacd_vfs_create();
    if (!ctx) {
        fprintf(stderr, "Error: Failed to create VFS context\n");
        return 1;
    }

    /* Open SACD ISO */
    printf("Opening SACD ISO: %s\n", argv[1]);
    result = sacd_vfs_open(ctx, argv[1]);
    if (result != SACD_VFS_OK) {
        fprintf(stderr, "Error: Failed to open SACD ISO: %s\n",
                sacd_vfs_error_string(result));
        sacd_vfs_destroy(ctx);
        return 1;
    }
    printf("SACD ISO opened successfully!\n");

    /* Get album name */
    result = sacd_vfs_get_album_name(ctx, album_name, sizeof(album_name));
    if (result == SACD_VFS_OK) {
        printf("\nAlbum: %s\n", album_name);
    }

    /* Check available areas and track counts */
    printf("\nAvailable areas:\n");
    if (sacd_vfs_has_area(ctx, SACD_VFS_AREA_STEREO)) {
        sacd_vfs_get_track_count(ctx, SACD_VFS_AREA_STEREO, &track_count);
        printf("  Stereo: %u tracks\n", track_count);
    }
    if (sacd_vfs_has_area(ctx, SACD_VFS_AREA_MULTICHANNEL)) {
        sacd_vfs_get_track_count(ctx, SACD_VFS_AREA_MULTICHANNEL, &track_count);
        printf("  Multi-channel: %u tracks\n", track_count);
    }

    /* List root directory */
    list_directory(ctx, "/", 1);

    /* List album directory */
    snprintf(filepath, sizeof(filepath), "/%s", album_name);
    list_directory(ctx, filepath, 1);

    /* List stereo directory if available */
    if (sacd_vfs_has_area(ctx, SACD_VFS_AREA_STEREO)) {
        snprintf(filepath, sizeof(filepath), "/%s/Stereo", album_name);
        list_directory(ctx, filepath, 1);

        /* Get first track filename */
        result = sacd_vfs_get_track_filename(ctx, SACD_VFS_AREA_STEREO, 1,
                                              track_filename, sizeof(track_filename));
        if (result == SACD_VFS_OK) {
            /* Open and demonstrate file operations on first stereo track */
            snprintf(filepath, sizeof(filepath), "/%s/Stereo/%s",
                     album_name, track_filename);
            demonstrate_file_operations(ctx, filepath);
        }
    }

    /* List multichannel directory if available */
    if (sacd_vfs_has_area(ctx, SACD_VFS_AREA_MULTICHANNEL)) {
        snprintf(filepath, sizeof(filepath), "/%s/Multi-channel", album_name);
        list_directory(ctx, filepath, 1);

        /* Get first track filename */
        result = sacd_vfs_get_track_filename(ctx, SACD_VFS_AREA_MULTICHANNEL, 1,
                                              track_filename, sizeof(track_filename));
        if (result == SACD_VFS_OK) {
            /* Open and demonstrate file operations on first multichannel track */
            snprintf(filepath, sizeof(filepath), "/%s/Multi-channel/%s",
                     album_name, track_filename);
            demonstrate_file_operations(ctx, filepath);
        }
    }

    /* Demonstrate ID3 tag extraction */
    if (sacd_vfs_has_area(ctx, SACD_VFS_AREA_STEREO)) {
        uint8_t *id3_buffer = NULL;
        size_t id3_size = 0;

        printf("\n=== ID3 Tag Extraction ===\n");
        result = sacd_vfs_get_id3_tag(ctx, SACD_VFS_AREA_STEREO, 1, &id3_buffer, &id3_size);
        if (result == SACD_VFS_OK && id3_buffer && id3_size > 0) {
            printf("Retrieved ID3 tag for track 1: %zu bytes\n", id3_size);
            if (id3_size >= 10) {
                printf("ID3 header: %.3s (version 2.%d.%d)\n",
                       id3_buffer, id3_buffer[3], id3_buffer[4]);
            }
            sa_free(id3_buffer);
        } else {
            printf("No ID3 tag available for track 1\n");
        }
    }

    /* Dump track to disc if requested */
    if (dump_track > 0) {
        /* Prefer stereo area, fall back to multichannel */
        sacd_vfs_area_t dump_area = SACD_VFS_AREA_STEREO;
        if (!sacd_vfs_has_area(ctx, SACD_VFS_AREA_STEREO)) {
            if (sacd_vfs_has_area(ctx, SACD_VFS_AREA_MULTICHANNEL)) {
                dump_area = SACD_VFS_AREA_MULTICHANNEL;
            } else {
                fprintf(stderr, "Error: No audio areas available\n");
                sacd_vfs_close(ctx);
                sacd_vfs_destroy(ctx);
                return 1;
            }
        }

        /* Verify track number is valid */
        sacd_vfs_get_track_count(ctx, dump_area, &track_count);
        if (dump_track > track_count) {
            fprintf(stderr, "Error: Track %d not found (only %u tracks in %s area)\n",
                    dump_track, track_count,
                    dump_area == SACD_VFS_AREA_STEREO ? "stereo" : "multichannel");
            sacd_vfs_close(ctx);
            sacd_vfs_destroy(ctx);
            return 1;
        }

        /* Get track filename and build path */
        result = sacd_vfs_get_track_filename(ctx, dump_area, (uint8_t)dump_track,
                                              track_filename, sizeof(track_filename));
        if (result == SACD_VFS_OK) {
            const char *area_name = (dump_area == SACD_VFS_AREA_STEREO) ? "Stereo" : "Multi-channel";
            snprintf(filepath, sizeof(filepath), "/%s/%s/%s",
                     album_name, area_name, track_filename);
            dump_track_to_disc(ctx, filepath, (uint8_t)dump_track);
        } else {
            fprintf(stderr, "Error: Cannot get filename for track %d\n", dump_track);
        }
    }

    /* Cleanup */
    printf("\n=== Cleanup ===\n");
    printf("Closing VFS...\n");
    sacd_vfs_close(ctx);
    sacd_vfs_destroy(ctx);
    printf("Done!\n");

    return 0;
}
