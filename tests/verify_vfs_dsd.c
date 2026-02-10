/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD VFS Verification Tool
 * This tool compares DSF files extracted by sacd-extract (known working)
 * with the virtual DSF files produced by sacd_vfs to identify corruption.
 * Usage: verify_vfs_dsd <reference.dsf> <source.iso> <track_num>
 * The tool performs:
 * 1. Sequential comparison of all bytes
 * 2. Reports the first byte offset where data differs
 * 3. Provides context around any corruption
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

#include <libsacdvfs/sacd_vfs.h>
#include <libsautil/sa_tpool.h>
#include <libsautil/compat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#define COMPARE_BUFFER_SIZE 4096
#define BENCHMARK_READ_SIZE (256 * 1024)  /* 256 KB reads for benchmark */
#define CONTEXT_BYTES 32

/* Color codes for terminal output */
#ifdef _WIN32
#define COLOR_RED ""
#define COLOR_GREEN ""
#define COLOR_YELLOW ""
#define COLOR_RESET ""
#else
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RESET "\033[0m"
#endif

/* =============================================================================
 * High-Resolution Timer
 * ===========================================================================*/

static double get_time_seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

static void print_hex_dump(const uint8_t *data, size_t len, uint64_t offset)
{
    for (size_t i = 0; i < len; i += 16) {
        printf("  %08llx: ", (unsigned long long)(offset + i));
        for (size_t j = 0; j < 16 && (i + j) < len; j++) {
            printf("%02x ", data[i + j]);
        }
        printf("\n");
    }
}

static int compare_files(FILE *ref_file, sacd_vfs_file_t *vfs_file,
                         uint64_t ref_size, uint64_t vfs_size)
{
    uint8_t ref_buf[COMPARE_BUFFER_SIZE];
    uint8_t vfs_buf[COMPARE_BUFFER_SIZE];
    uint64_t position = 0;
    uint64_t compare_size = (ref_size < vfs_size) ? ref_size : vfs_size;
    uint64_t first_diff = UINT64_MAX;
    uint64_t diff_count = 0;
    int result = 0;

    printf("Comparing %llu bytes...\n", (unsigned long long)compare_size);

    /* Seek both files to start */
    sa_fseek64(ref_file, 0, SEEK_SET);
    sacd_vfs_file_seek(vfs_file, 0, SEEK_SET);

    while (position < compare_size) {
        size_t to_read = COMPARE_BUFFER_SIZE;
        if (position + to_read > compare_size) {
            to_read = (size_t)(compare_size - position);
        }

        /* Read from reference file */
        size_t ref_read = fread(ref_buf, 1, to_read, ref_file);
        if (ref_read == 0 && !feof(ref_file)) {
            printf("Error reading reference file at offset %llu\n",
                   (unsigned long long)position);
            return -1;
        }

        /* Read from VFS */
        size_t vfs_read = 0;
        int err = sacd_vfs_file_read(vfs_file, vfs_buf, to_read, &vfs_read);
        if (err != SACD_VFS_OK && err != SACD_VFS_ERROR_EOF) {
            printf("Error reading VFS file at offset %llu: %s\n",
                   (unsigned long long)position, sacd_vfs_error_string(err));
            return -1;
        }

        /* Compare read sizes */
        if (ref_read != vfs_read) {
            printf("Read size mismatch at offset %llu: ref=%zu, vfs=%zu\n",
                   (unsigned long long)position, ref_read, vfs_read);
        }

        size_t cmp_size = (ref_read < vfs_read) ? ref_read : vfs_read;

        /* Compare data */
        for (size_t i = 0; i < cmp_size; i++) {
            if (ref_buf[i] != vfs_buf[i]) {
                diff_count++;
                if (first_diff == UINT64_MAX) {
                    first_diff = position + i;
                    result = -1;

                    printf("\n%s=== FIRST DIFFERENCE at offset %llu (0x%llx) ===%s\n",
                           COLOR_RED, (unsigned long long)first_diff,
                           (unsigned long long)first_diff, COLOR_RESET);
                    printf("Expected (reference): 0x%02x\n", ref_buf[i]);
                    printf("Got (VFS):            0x%02x\n", vfs_buf[i]);

                    /* Show context */
                    size_t ctx_start = (i >= CONTEXT_BYTES) ? i - CONTEXT_BYTES : 0;
                    size_t ctx_end = i + CONTEXT_BYTES;
                    if (ctx_end > cmp_size) ctx_end = cmp_size;

                    printf("\nReference context:\n");
                    print_hex_dump(ref_buf + ctx_start, ctx_end - ctx_start,
                                   position + ctx_start);

                    printf("\nVFS context:\n");
                    print_hex_dump(vfs_buf + ctx_start, ctx_end - ctx_start,
                                   position + ctx_start);

                    /* Report position in DSF structure */
                    uint64_t offset = first_diff;
                    if (offset < 28) {
                        printf("\nLocation: DSD chunk (header)\n");
                    } else if (offset < 80) {
                        printf("\nLocation: fmt chunk (format info)\n");
                    } else if (offset < 92) {
                        printf("\nLocation: data chunk header\n");
                    } else {
                        printf("\nLocation: Audio data region (offset %llu into audio)\n",
                               (unsigned long long)(offset - 92));

                        /* Calculate which DSF block this is in */
                        uint64_t audio_offset = offset - 92;
                        uint64_t block_group = audio_offset / (4096 * 2);  /* 2 channels */
                        uint64_t byte_in_group = audio_offset % (4096 * 2);
                        uint32_t channel = (uint32_t)(byte_in_group / 4096);
                        uint64_t byte_in_block = byte_in_group % 4096;

                        printf("  Block group: %llu\n", (unsigned long long)block_group);
                        printf("  Channel: %u\n", channel);
                        printf("  Byte in block: %llu\n", (unsigned long long)byte_in_block);
                    }
                }
            }
        }

        position += cmp_size;

        /* Progress indicator - every 100 MB */
        if ((position % (100 * 1024 * 1024)) == 0) {
            printf("  Compared %llu MB...\n", (unsigned long long)(position / (1024 * 1024)));
        }
    }

    if (diff_count > 0) {
        printf("\n%sTotal differences: %llu bytes%s\n",
               COLOR_RED, (unsigned long long)diff_count, COLOR_RESET);
    } else {
        printf("\n%sNo differences found - data matches perfectly!%s\n",
               COLOR_GREEN, COLOR_RESET);
    }

    return result;
}

static void compare_at_random_offsets(FILE *ref_file, sacd_vfs_file_t *vfs_file,
                                      uint64_t file_size, int num_tests)
{
    uint8_t ref_buf[256];
    uint8_t vfs_buf[256];
    int failed = 0;

    printf("\n=== Random Seek Test ===\n");

    for (int i = 0; i < num_tests; i++) {
        /* Generate random offset */
        uint64_t offset = ((uint64_t)rand() << 32 | rand()) % file_size;
        size_t to_read = 256;
        if (offset + to_read > file_size) {
            to_read = (size_t)(file_size - offset);
        }

        /* Seek and read from reference */
        sa_fseek64(ref_file, (int64_t)offset, SEEK_SET);
        size_t ref_read = fread(ref_buf, 1, to_read, ref_file);

        /* Seek and read from VFS */
        int seek_err = sacd_vfs_file_seek(vfs_file, offset, SEEK_SET);
        size_t vfs_read = 0;
        int read_err = sacd_vfs_file_read(vfs_file, vfs_buf, to_read, &vfs_read);

        if (ref_read != vfs_read || memcmp(ref_buf, vfs_buf, ref_read) != 0) {
            printf("  FAIL at offset %llu: ref_read=%zu, vfs_read=%zu, seek_err=%d, read_err=%d\n",
                   (unsigned long long)offset, ref_read, vfs_read, seek_err, read_err);
            if (vfs_read > 0 && ref_read > 0) {
                /* Show first difference */
                for (size_t j = 0; j < ref_read && j < vfs_read; j++) {
                    if (ref_buf[j] != vfs_buf[j]) {
                        printf("    First diff at byte %zu: ref=0x%02x, vfs=0x%02x\n",
                               j, ref_buf[j], vfs_buf[j]);
                        break;
                    }
                }
            }
            failed++;
        } else {
            printf("  OK at offset %llu (%zu bytes)\n",
                   (unsigned long long)offset, ref_read);
        }
    }

    printf("Random seek test: %d/%d passed\n", num_tests - failed, num_tests);
}

/* =============================================================================
 * Benchmark: Timed Read of Entire Track
 * ===========================================================================*/

static int timed_read_track(sacd_vfs_ctx_t *ctx, const char *vfs_path,
                            sa_tpool *pool, const char *label,
                            double *elapsed_out, uint64_t *bytes_out)
{
    sacd_vfs_file_t *file = NULL;
    int ret;

    if (pool) {
        ret = sacd_vfs_file_open_mt(ctx, vfs_path, pool, &file);
    } else {
        ret = sacd_vfs_file_open(ctx, vfs_path, &file);
    }

    if (ret != SACD_VFS_OK) {
        printf("Error opening VFS file (%s): %s\n", label, sacd_vfs_error_string(ret));
        return -1;
    }

    sacd_vfs_file_info_t info;
    sacd_vfs_file_get_info(file, &info);

    uint8_t *buffer = (uint8_t *)malloc(BENCHMARK_READ_SIZE);
    if (!buffer) {
        printf("Error: Cannot allocate read buffer\n");
        sacd_vfs_file_close(file);
        return -1;
    }

    uint64_t total_read = 0;
    double start = get_time_seconds();

    while (total_read < info.total_size) {
        size_t to_read = BENCHMARK_READ_SIZE;
        if (total_read + to_read > info.total_size) {
            to_read = (size_t)(info.total_size - total_read);
        }

        size_t bytes_read = 0;
        ret = sacd_vfs_file_read(file, buffer, to_read, &bytes_read);
        if (ret != SACD_VFS_OK && ret != SACD_VFS_ERROR_EOF) {
            printf("Error reading VFS file (%s) at offset %llu: %s\n",
                   label, (unsigned long long)total_read, sacd_vfs_error_string(ret));
            free(buffer);
            sacd_vfs_file_close(file);
            return -1;
        }

        total_read += bytes_read;
        if (bytes_read == 0) break;
    }

    double end = get_time_seconds();
    *elapsed_out = end - start;
    *bytes_out = total_read;

    sacd_vfs_file_close(file);
    free(buffer);
    return 0;
}

static int run_benchmark(const char *iso_path, int track_num, int num_threads)
{
    sacd_vfs_ctx_t *ctx = NULL;
    sa_tpool *pool = NULL;

    printf("=== SACD VFS Benchmark: ST vs MT DST Decompression ===\n");
    printf("ISO file: %s\n", iso_path);
    printf("Track: %d\n", track_num);
    printf("MT threads: %d\n\n", num_threads);

    /* --- Open ISO and resolve track path --- */
    ctx = sacd_vfs_create();
    if (!ctx) {
        printf("Error: Cannot create VFS context\n");
        return 1;
    }

    int ret = sacd_vfs_open(ctx, iso_path);
    if (ret != SACD_VFS_OK) {
        printf("Error: Cannot open ISO: %s\n", sacd_vfs_error_string(ret));
        sacd_vfs_destroy(ctx);
        return 1;
    }

    char album_name[256];
    sacd_vfs_get_album_name(ctx, album_name, sizeof(album_name));
    printf("Album: %s\n", album_name);

    if (!sacd_vfs_has_area(ctx, SACD_VFS_AREA_STEREO)) {
        printf("Error: No stereo area in this disc\n");
        sacd_vfs_close(ctx);
        sacd_vfs_destroy(ctx);
        return 1;
    }

    uint8_t track_count;
    sacd_vfs_get_track_count(ctx, SACD_VFS_AREA_STEREO, &track_count);
    if (track_num < 1 || track_num > track_count) {
        printf("Error: Invalid track number %d (valid: 1-%u)\n", track_num, track_count);
        sacd_vfs_close(ctx);
        sacd_vfs_destroy(ctx);
        return 1;
    }

    char track_filename[256];
    sacd_vfs_get_track_filename(ctx, SACD_VFS_AREA_STEREO, (uint8_t)track_num,
                                 track_filename, sizeof(track_filename));

    char vfs_path[512];
    snprintf(vfs_path, sizeof(vfs_path), "/Stereo/%s", track_filename);

    /* Get track info for display */
    {
        sacd_vfs_file_t *probe = NULL;
        ret = sacd_vfs_file_open(ctx, vfs_path, &probe);
        if (ret != SACD_VFS_OK) {
            printf("Error: Cannot open track: %s\n", sacd_vfs_error_string(ret));
            sacd_vfs_close(ctx);
            sacd_vfs_destroy(ctx);
            return 1;
        }
        sacd_vfs_file_info_t info;
        sacd_vfs_file_get_info(probe, &info);
        printf("Track: %s\n", track_filename);
        printf("  Size: %.2f MB\n", (double)info.total_size / (1024.0 * 1024.0));
        printf("  Duration: %.1f seconds\n", info.duration_seconds);
        printf("  Format: %s\n", info.frame_format == SACD_VFS_FRAME_DST ? "DST (compressed)" : "DSD (uncompressed)");
        printf("  Channels: %u, Sample rate: %u Hz\n", info.channel_count, info.sample_rate);

        if (info.frame_format != SACD_VFS_FRAME_DST) {
            printf("\nNote: This track uses DSD (uncompressed) format.\n");
            printf("MT decompression only benefits DST tracks.\n");
            printf("Running benchmark anyway for comparison...\n");
        }
        sacd_vfs_file_close(probe);
    }

    sacd_vfs_close(ctx);
    sacd_vfs_destroy(ctx);

    /* --- Run Single-Threaded benchmark --- */
    printf("\n--- Single-Threaded (ST) Read ---\n");

    ctx = sacd_vfs_create();
    if (!ctx) { printf("Error: Cannot create VFS context\n"); return 1; }
    ret = sacd_vfs_open(ctx, iso_path);
    if (ret != SACD_VFS_OK) { printf("Error: %s\n", sacd_vfs_error_string(ret)); sacd_vfs_destroy(ctx); return 1; }

    double st_elapsed = 0;
    uint64_t st_bytes = 0;
    ret = timed_read_track(ctx, vfs_path, NULL, "ST", &st_elapsed, &st_bytes);

    sacd_vfs_close(ctx);
    sacd_vfs_destroy(ctx);
    ctx = NULL;

    if (ret != 0) {
        printf("ST benchmark failed\n");
        return 1;
    }

    double st_mb = (double)st_bytes / (1024.0 * 1024.0);
    double st_throughput = st_mb / st_elapsed;
    printf("  Read %.2f MB in %.3f seconds (%.2f MB/s)\n", st_mb, st_elapsed, st_throughput);

    /* --- Run Multi-Threaded benchmark --- */
    printf("\n--- Multi-Threaded (MT, %d workers) Read ---\n", num_threads);

    pool = sa_tpool_init(num_threads);
    if (!pool) {
        printf("Error: Cannot create thread pool with %d threads\n", num_threads);
        return 1;
    }

    ctx = sacd_vfs_create();
    if (!ctx) { printf("Error: Cannot create VFS context\n"); sa_tpool_destroy(pool); return 1; }
    ret = sacd_vfs_open(ctx, iso_path);
    if (ret != SACD_VFS_OK) { printf("Error: %s\n", sacd_vfs_error_string(ret)); sacd_vfs_close(ctx); sacd_vfs_destroy(ctx); sa_tpool_destroy(pool); return 1; }

    double mt_elapsed = 0;
    uint64_t mt_bytes = 0;
    ret = timed_read_track(ctx, vfs_path, pool, "MT", &mt_elapsed, &mt_bytes);

    sacd_vfs_close(ctx);
    sacd_vfs_destroy(ctx);
    sa_tpool_destroy(pool);

    if (ret != 0) {
        printf("MT benchmark failed\n");
        return 1;
    }

    double mt_mb = (double)mt_bytes / (1024.0 * 1024.0);
    double mt_throughput = mt_mb / mt_elapsed;
    printf("  Read %.2f MB in %.3f seconds (%.2f MB/s)\n", mt_mb, mt_elapsed, mt_throughput);

    /* --- Results --- */
    printf("\n=== Benchmark Results ===\n");
    printf("  Single-threaded: %.3f s (%.2f MB/s)\n", st_elapsed, st_throughput);
    printf("  Multi-threaded:  %.3f s (%.2f MB/s) [%d workers]\n", mt_elapsed, mt_throughput, num_threads);

    if (st_bytes != mt_bytes) {
        printf("  %sWARNING: Byte count mismatch! ST=%llu, MT=%llu%s\n",
               COLOR_RED, (unsigned long long)st_bytes, (unsigned long long)mt_bytes, COLOR_RESET);
    }

    if (mt_elapsed > 0 && st_elapsed > 0) {
        double speedup = st_elapsed / mt_elapsed;
        if (speedup > 1.0) {
            printf("  %sSpeedup: %.2fx faster with MT%s\n", COLOR_GREEN, speedup, COLOR_RESET);
        } else if (speedup < 1.0) {
            printf("  %sMT was %.2fx slower than ST%s\n", COLOR_YELLOW, 1.0 / speedup, COLOR_RESET);
        } else {
            printf("  No significant difference\n");
        }
    }

    printf("\nNote: First run (ST) warms the OS disk cache.\n");
    printf("MT times primarily reflect CPU-bound DST decompression gains.\n");

    return 0;
}

/* =============================================================================
 * Main
 * ===========================================================================*/

int main(int argc, char *argv[])
{
    /* Check for benchmark mode */
    if (argc >= 2 && strcmp(argv[1], "--benchmark") == 0) {
        if (argc < 4) {
            printf("Usage: %s --benchmark <source.iso> <track_num> [threads]\n", argv[0]);
            printf("\nExample:\n");
            printf("  %s --benchmark data/DST.iso 1        # 4 threads (default)\n", argv[0]);
            printf("  %s --benchmark data/DST.iso 1 8      # 8 threads\n", argv[0]);
            return 1;
        }
        const char *iso_path = argv[2];
        int track_num = atoi(argv[3]);
        int num_threads = (argc > 4) ? atoi(argv[4]) : 4;
        if (num_threads < 1) num_threads = 1;
        return run_benchmark(iso_path, track_num, num_threads);
    }

    if (argc < 4) {
        printf("Usage: %s <reference.dsf> <source.iso> <track_num> [max_mb] [seed]\n", argv[0]);
        printf("\nExample:\n");
        printf("  %s data/01-STEREO-DSD.DSF data/DSD.ISO 1       # Full comparison\n", argv[0]);
        printf("  %s data/01-STEREO-DSD.DSF data/DSD.ISO 1 5     # Compare first 5 MB only\n", argv[0]);
        printf("  %s data/01-STEREO-DSD.DSF data/DSD.ISO 1 0     # Sampling mode (fast)\n", argv[0]);
        return 1;
    }

    const char *ref_path = argv[1];
    const char *iso_path = argv[2];
    int track_num = atoi(argv[3]);
    uint64_t max_compare_bytes = UINT64_MAX;  /* Default: compare all */
    int sampling_mode = 0;  /* 0 = full comparison, 1 = sampling mode */

    /* Optional max MB to compare */
    if (argc > 4) {
        int max_mb = atoi(argv[4]);
        if (max_mb > 0) {
            max_compare_bytes = (uint64_t)max_mb * 1024 * 1024;
            printf("Limiting comparison to first %d MB\n", max_mb);
        } else if (max_mb == 0) {
            sampling_mode = 1;
            printf("Using sampling mode (header + first/last MB + random samples)\n");
        }
    }

    /* Optional random seed */
    if (argc > 5) {
        srand((unsigned int)atoi(argv[5]));
    } else {
        srand(42);  /* Default seed for reproducibility */
    }

    printf("=== SACD VFS Verification Tool ===\n");
    printf("Reference file: %s\n", ref_path);
    printf("ISO file: %s\n", iso_path);
    printf("Track: %d\n\n", track_num);

    /* Open reference file */
    FILE *ref_file = sa_fopen(ref_path, "rb");
    if (!ref_file) {
        printf("Error: Cannot open reference file: %s\n", ref_path);
        return 1;
    }

    /* Get reference file size */
    sa_fseek64(ref_file, 0, SEEK_END);
    uint64_t ref_size = (uint64_t)sa_ftell64(ref_file);
    sa_fseek64(ref_file, 0, SEEK_SET);
    printf("Reference file size: %llu bytes\n", (unsigned long long)ref_size);

    /* Open SACD VFS */
    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    if (!ctx) {
        printf("Error: Cannot create VFS context\n");
        fclose(ref_file);
        return 1;
    }

    int ret = sacd_vfs_open(ctx, iso_path);
    if (ret != SACD_VFS_OK) {
        printf("Error: Cannot open ISO: %s\n", sacd_vfs_error_string(ret));
        sacd_vfs_destroy(ctx);
        fclose(ref_file);
        return 1;
    }

    /* Get album info */
    char album_name[256];
    sacd_vfs_get_album_name(ctx, album_name, sizeof(album_name));
    printf("Album: %s\n", album_name);

    /* Check for stereo area */
    if (!sacd_vfs_has_area(ctx, SACD_VFS_AREA_STEREO)) {
        printf("Error: No stereo area in this disc\n");
        sacd_vfs_close(ctx);
        sacd_vfs_destroy(ctx);
        fclose(ref_file);
        return 1;
    }

    /* Get track count */
    uint8_t track_count;
    sacd_vfs_get_track_count(ctx, SACD_VFS_AREA_STEREO, &track_count);
    printf("Track count: %u\n", track_count);

    if (track_num < 1 || track_num > track_count) {
        printf("Error: Invalid track number %d (valid: 1-%u)\n", track_num, track_count);
        sacd_vfs_close(ctx);
        sacd_vfs_destroy(ctx);
        fclose(ref_file);
        return 1;
    }

    /* Get track filename */
    char track_filename[256];
    sacd_vfs_get_track_filename(ctx, SACD_VFS_AREA_STEREO, (uint8_t)track_num,
                                 track_filename, sizeof(track_filename));
    printf("VFS track filename: %s\n", track_filename);

    /* Build virtual file path - VFS expects "Stereo" or "Multi-channel" in the path */
    char vfs_path[512];
    snprintf(vfs_path, sizeof(vfs_path), "/Stereo/%s", track_filename);
    printf("VFS path: %s\n", vfs_path);

    /* Open virtual file */
    sacd_vfs_file_t *vfs_file = NULL;
    ret = sacd_vfs_file_open(ctx, vfs_path, &vfs_file);
    if (ret != SACD_VFS_OK) {
        printf("Error: Cannot open VFS file: %s\n", sacd_vfs_error_string(ret));
        sacd_vfs_close(ctx);
        sacd_vfs_destroy(ctx);
        fclose(ref_file);
        return 1;
    }

    /* Get VFS file info */
    sacd_vfs_file_info_t info;
    sacd_vfs_file_get_info(vfs_file, &info);
    printf("\nVFS file info:\n");
    printf("  Total size: %llu bytes\n", (unsigned long long)info.total_size);
    printf("  Header size: %llu bytes\n", (unsigned long long)info.header_size);
    printf("  Audio data size: %llu bytes\n", (unsigned long long)info.audio_data_size);
    printf("  Metadata size: %llu bytes\n", (unsigned long long)info.metadata_size);
    printf("  Channel count: %u\n", info.channel_count);
    printf("  Sample rate: %u Hz\n", info.sample_rate);
    printf("  Frame format: %u\n", info.frame_format);

    /* Compare sizes */
    if (ref_size != info.total_size) {
        printf("\n%sWARNING: Size mismatch! Reference=%llu, VFS=%llu%s\n",
               COLOR_YELLOW, (unsigned long long)ref_size,
               (unsigned long long)info.total_size, COLOR_RESET);
    } else {
        printf("\n%sFile sizes match: %llu bytes%s\n",
               COLOR_GREEN, (unsigned long long)ref_size, COLOR_RESET);
    }

    int cmp_result = 0;
    uint64_t file_size = ref_size < info.total_size ? ref_size : info.total_size;

    if (sampling_mode) {
        /* Sampling mode: compare header + first 1MB + last 1MB + 20 random samples */
        printf("\n=== Sampling Comparison ===\n");

        /* Compare header (first 4KB) */
        printf("Checking header (first 4KB)...\n");
        cmp_result = compare_files(ref_file, vfs_file, 4096, 4096);
        if (cmp_result != 0) {
            printf("Header comparison FAILED\n");
        }

        /* Compare first 1MB of audio */
        if (cmp_result == 0 && file_size > 1024 * 1024) {
            printf("Checking first 1MB...\n");
            sa_fseek64(ref_file, 0, SEEK_SET);
            sacd_vfs_file_seek(vfs_file, 0, SEEK_SET);
            cmp_result = compare_files(ref_file, vfs_file, 1024 * 1024, 1024 * 1024);
        }

        /* Compare last 1MB - skip for DST since seeking in DST is slow */
        if (cmp_result == 0 && file_size > 2 * 1024 * 1024 && info.frame_format != 0) {
            printf("Checking last 1MB...\n");
            uint64_t last_mb_offset = file_size - 1024 * 1024;
            sa_fseek64(ref_file, (int64_t)last_mb_offset, SEEK_SET);
            sacd_vfs_file_seek(vfs_file, last_mb_offset, SEEK_SET);
            cmp_result = compare_files(ref_file, vfs_file, 1024 * 1024, 1024 * 1024);
        } else if (info.frame_format == 0) {
            printf("(Skipping last 1MB check for DST - seeking requires full decode)\n");
        }

        /* Random samples - skip for DST */
        if (cmp_result == 0 && info.frame_format != 0) {
            printf("Running 20 random seek tests...\n");
            compare_at_random_offsets(ref_file, vfs_file, file_size, 20);
        } else if (info.frame_format == 0) {
            printf("(Skipping random seek tests for DST - seeking requires full decode)\n");
        }
    } else {
        /* Full comparison mode */
        uint64_t actual_compare_size = file_size;
        if (max_compare_bytes < actual_compare_size) {
            actual_compare_size = max_compare_bytes;
            printf("(Limited to %llu bytes)\n", (unsigned long long)actual_compare_size);
        }

        printf("\n=== Byte-by-byte Comparison ===\n");
        cmp_result = compare_files(ref_file, vfs_file, actual_compare_size, actual_compare_size);

        /* Random offset tests - only if comparing full file */
        if (max_compare_bytes >= ref_size) {
            compare_at_random_offsets(ref_file, vfs_file, file_size, 10);
        } else {
            printf("\n(Random seek test skipped - partial comparison mode)\n");
        }
    }

    /* Cleanup */
    sacd_vfs_file_close(vfs_file);
    sacd_vfs_close(ctx);
    sacd_vfs_destroy(ctx);
    fclose(ref_file);

    return cmp_result;
}
