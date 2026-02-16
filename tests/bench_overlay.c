/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD Overlay VFS - Performance Benchmark
 * Exercises the overlay API to measure per-operation latency.
 * Usage: bench_overlay <source_dir> [iterations]
 */

#include <libsacdvfs/sacd_overlay.h>
#include <libsautil/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static double get_time_ms(void)
{
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart * 1000.0;
}
#else
static double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}
#endif

/* =============================================================================
 * Collected directory entries
 * ===========================================================================*/

#define MAX_ENTRIES 4096
#define MAX_PATHS  4096

typedef struct {
    sacd_overlay_entry_t entries[MAX_ENTRIES];
    int count;
} entry_list_t;

static int collect_entry(const sacd_overlay_entry_t *entry, void *userdata)
{
    entry_list_t *list = (entry_list_t *)userdata;
    if (list->count < MAX_ENTRIES) {
        list->entries[list->count++] = *entry;
    }
    return 0;
}

/* =============================================================================
 * Benchmark: readdir root
 * ===========================================================================*/

static void bench_readdir_root(sacd_overlay_ctx_t *ctx, int iterations,
                                entry_list_t *root_entries)
{
    double total = 0;
    int entry_count = 0;

    for (int i = 0; i < iterations; i++) {
        entry_list_t entries;
        entries.count = 0;

        double t0 = get_time_ms();
        int result = sacd_overlay_readdir(ctx, "/", collect_entry, &entries);
        double t1 = get_time_ms();

        total += (t1 - t0);
        entry_count = entries.count;

        if (i == 0 && root_entries) {
            *root_entries = entries;
        }

        if (result < 0) {
            fprintf(stderr, "  readdir / failed: %s\n",
                    sacd_overlay_error_string(result));
            break;
        }
    }

    printf("  readdir /           : %7.1f ms avg (%d entries, %d iters)\n",
           total / iterations, entry_count, iterations);
}

/* =============================================================================
 * Benchmark: stat on all root entries
 * ===========================================================================*/

static void bench_stat_root_entries(sacd_overlay_ctx_t *ctx, int iterations,
                                     const entry_list_t *root_entries)
{
    double total = 0;
    int stat_count = 0;

    for (int i = 0; i < iterations; i++) {
        double t0 = get_time_ms();

        for (int j = 0; j < root_entries->count; j++) {
            char path[SACD_OVERLAY_MAX_PATH];
            snprintf(path, sizeof(path), "/%s", root_entries->entries[j].name);

            sacd_overlay_entry_t entry;
            sacd_overlay_stat(ctx, path, &entry);
            stat_count++;
        }

        double t1 = get_time_ms();
        total += (t1 - t0);
    }

    printf("  stat all root       : %7.1f ms avg (%d stats/iter, %d iters)\n",
           total / iterations, root_entries->count, iterations);
}

/* =============================================================================
 * Benchmark: readdir inside ISOs
 * ===========================================================================*/

typedef struct {
    char paths[MAX_PATHS][SACD_OVERLAY_MAX_PATH];
    sacd_overlay_entry_type_t types[MAX_PATHS];
    uint64_t sizes[MAX_PATHS];
    int count;
} path_list_t;

static void bench_readdir_isos(sacd_overlay_ctx_t *ctx, int iterations,
                                const entry_list_t *root_entries,
                                path_list_t *all_files)
{
    double total = 0;
    int iso_count = 0;
    int total_tracks = 0;

    if (all_files) {
        all_files->count = 0;
    }

    for (int i = 0; i < iterations; i++) {
        double t0 = get_time_ms();

        for (int j = 0; j < root_entries->count; j++) {
            if (root_entries->entries[j].type != SACD_OVERLAY_ENTRY_ISO_FOLDER &&
                root_entries->entries[j].type != SACD_OVERLAY_ENTRY_DIRECTORY) {
                continue;
            }

            char dir_path[SACD_OVERLAY_MAX_PATH];
            snprintf(dir_path, sizeof(dir_path), "/%s",
                     root_entries->entries[j].name);

            /* Read ISO root dir (e.g., /AlbumName) */
            entry_list_t iso_entries;
            iso_entries.count = 0;
            int result = sacd_overlay_readdir(ctx, dir_path,
                                              collect_entry, &iso_entries);
            if (result < 0) continue;

            if (i == 0) iso_count++;

            /* Read subdirs (Stereo/, Multi-channel/) */
            for (int k = 0; k < iso_entries.count; k++) {
                if (iso_entries.entries[k].type == SACD_OVERLAY_ENTRY_DIRECTORY) {
                    char sub_path[SACD_OVERLAY_MAX_PATH];
                    snprintf(sub_path, sizeof(sub_path), "%s/%s",
                             dir_path, iso_entries.entries[k].name);

                    entry_list_t track_entries;
                    track_entries.count = 0;
                    result = sacd_overlay_readdir(ctx, sub_path,
                                                  collect_entry, &track_entries);
                    if (result < 0) continue;

                    if (i == 0) {
                        total_tracks += track_entries.count;

                        /* Collect file paths for read benchmark */
                        if (all_files) {
                            for (int t = 0; t < track_entries.count; t++) {
                                if (all_files->count < MAX_PATHS &&
                                    track_entries.entries[t].type ==
                                        SACD_OVERLAY_ENTRY_FILE) {
                                    snprintf(
                                        all_files->paths[all_files->count],
                                        SACD_OVERLAY_MAX_PATH, "%s/%s",
                                        sub_path,
                                        track_entries.entries[t].name);
                                    all_files->types[all_files->count] =
                                        track_entries.entries[t].type;
                                    all_files->sizes[all_files->count] =
                                        track_entries.entries[t].size;
                                    all_files->count++;
                                }
                            }
                        }
                    }
                }
            }
        }

        double t1 = get_time_ms();
        total += (t1 - t0);
    }

    printf("  readdir ISOs        : %7.1f ms avg (%d ISOs, %d tracks, %d iters)\n",
           total / iterations, iso_count, total_tracks, iterations);
}

/* =============================================================================
 * Benchmark: open + stat + read header + close for each virtual file
 * ===========================================================================*/

static void bench_open_read_close(sacd_overlay_ctx_t *ctx, int iterations,
                                   const path_list_t *files)
{
    double total_open = 0;
    double total_read = 0;
    double total_close = 0;
    int file_count = files->count;
    char buf[4096];

    for (int i = 0; i < iterations; i++) {
        double t_open_sum = 0;
        double t_read_sum = 0;
        double t_close_sum = 0;

        for (int j = 0; j < file_count; j++) {
            sacd_overlay_file_t *file = NULL;

            /* Open */
            double t0 = get_time_ms();
            int result = sacd_overlay_open(ctx, files->paths[j],
                                            SACD_OVERLAY_OPEN_READ, &file);
            double t1 = get_time_ms();
            t_open_sum += (t1 - t0);

            if (result != SACD_OVERLAY_OK || !file) continue;

            /* Read first 4KB (DSF header) */
            size_t bytes_read = 0;
            double t2 = get_time_ms();
            sacd_overlay_read(file, buf, sizeof(buf), 0, &bytes_read);
            double t3 = get_time_ms();
            t_read_sum += (t3 - t2);

            /* Close */
            double t4 = get_time_ms();
            sacd_overlay_close(file);
            double t5 = get_time_ms();
            t_close_sum += (t5 - t4);
        }

        total_open += t_open_sum;
        total_read += t_read_sum;
        total_close += t_close_sum;
    }

    if (file_count > 0) {
        printf("  open  (per file)    : %7.3f ms avg (%d files, %d iters)\n",
               total_open / (iterations * file_count), file_count, iterations);
        printf("  read 4KB (per file) : %7.3f ms avg\n",
               total_read / (iterations * file_count));
        printf("  close (per file)    : %7.3f ms avg\n",
               total_close / (iterations * file_count));
        printf("  open+read+close tot : %7.1f ms avg (all %d files)\n",
               (total_open + total_read + total_close) / iterations, file_count);
    }
}

/* =============================================================================
 * Benchmark: sequential 1MB read from each file
 * ===========================================================================*/

static void bench_sequential_read(sacd_overlay_ctx_t *ctx, int iterations,
                                   const path_list_t *files)
{
    double total = 0;
    uint64_t total_bytes = 0;
    int file_count = files->count;
    size_t read_size = 256 * 1024;  /* 256KB chunks */
    size_t max_per_file = 1024 * 1024;  /* Read up to 1MB per file */
    char *buf = (char *)malloc(read_size);
    if (!buf) return;

    for (int i = 0; i < iterations; i++) {
        double t0 = get_time_ms();
        uint64_t iter_bytes = 0;

        for (int j = 0; j < file_count; j++) {
            sacd_overlay_file_t *file = NULL;
            int result = sacd_overlay_open(ctx, files->paths[j],
                                            SACD_OVERLAY_OPEN_READ, &file);
            if (result != SACD_OVERLAY_OK || !file) continue;

            uint64_t offset = 0;
            size_t total_read_this_file = 0;
            while (total_read_this_file < max_per_file) {
                size_t bytes_read = 0;
                result = sacd_overlay_read(file, buf, read_size, offset,
                                            &bytes_read);
                if (result != SACD_OVERLAY_OK || bytes_read == 0) break;
                offset += bytes_read;
                total_read_this_file += bytes_read;
                iter_bytes += bytes_read;
            }

            sacd_overlay_close(file);
        }

        double t1 = get_time_ms();
        total += (t1 - t0);
        if (i == 0) total_bytes = iter_bytes;
    }

    double avg_ms = total / iterations;
    double mb = (double)total_bytes / (1024.0 * 1024.0);
    double throughput = (avg_ms > 0) ? (mb / (avg_ms / 1000.0)) : 0;

    printf("  seq read 1MB/file   : %7.1f ms avg (%.1f MB, %.1f MB/s, %d iters)\n",
           avg_ms, mb, throughput, iterations);

    free(buf);
}

/* =============================================================================
 * Benchmark: full Roon-like scan simulation
 * ===========================================================================*/

static void bench_roon_scan(sacd_overlay_ctx_t *ctx, int iterations)
{
    double total = 0;
    int total_entries = 0;
    int total_files = 0;
    char buf[4096];

    for (int i = 0; i < iterations; i++) {
        double t0 = get_time_ms();
        int iter_entries = 0;
        int iter_files = 0;

        /* Step 1: readdir root */
        entry_list_t root;
        root.count = 0;
        sacd_overlay_readdir(ctx, "/", collect_entry, &root);

        /* Step 2: For each entry, stat + readdir + open/read/close tracks */
        for (int j = 0; j < root.count; j++) {
            char path[SACD_OVERLAY_MAX_PATH];
            snprintf(path, sizeof(path), "/%s", root.entries[j].name);

            /* Stat the entry */
            sacd_overlay_entry_t entry;
            sacd_overlay_stat(ctx, path, &entry);
            iter_entries++;

            if (entry.type != SACD_OVERLAY_ENTRY_DIRECTORY &&
                entry.type != SACD_OVERLAY_ENTRY_ISO_FOLDER) {
                continue;
            }

            /* Readdir the ISO/directory */
            entry_list_t sub;
            sub.count = 0;
            sacd_overlay_readdir(ctx, path, collect_entry, &sub);

            for (int k = 0; k < sub.count; k++) {
                char sub_path[SACD_OVERLAY_MAX_PATH];
                snprintf(sub_path, sizeof(sub_path), "%s/%s",
                         path, sub.entries[k].name);

                sacd_overlay_stat(ctx, sub_path, &entry);
                iter_entries++;

                if (entry.type == SACD_OVERLAY_ENTRY_DIRECTORY) {
                    /* Readdir area (Stereo/Multi-channel) */
                    entry_list_t tracks;
                    tracks.count = 0;
                    sacd_overlay_readdir(ctx, sub_path, collect_entry, &tracks);

                    /* Open, read header, close each track */
                    for (int t = 0; t < tracks.count; t++) {
                        if (tracks.entries[t].type != SACD_OVERLAY_ENTRY_FILE)
                            continue;

                        char file_path[SACD_OVERLAY_MAX_PATH];
                        snprintf(file_path, sizeof(file_path), "%s/%s",
                                 sub_path, tracks.entries[t].name);

                        sacd_overlay_file_t *file = NULL;
                        int r = sacd_overlay_open(ctx, file_path,
                                                   SACD_OVERLAY_OPEN_READ,
                                                   &file);
                        if (r == SACD_OVERLAY_OK && file) {
                            size_t bytes_read = 0;
                            sacd_overlay_read(file, buf, sizeof(buf), 0,
                                              &bytes_read);
                            sacd_overlay_close(file);
                            iter_files++;
                        }
                    }
                } else if (entry.type == SACD_OVERLAY_ENTRY_FILE) {
                    /* Passthrough file - just stat is enough */
                    iter_files++;
                }
            }
        }

        double t1 = get_time_ms();
        total += (t1 - t0);
        if (i == 0) {
            total_entries = iter_entries;
            total_files = iter_files;
        }
    }

    printf("  FULL SCAN (Roon)    : %7.1f ms avg (%d entries, %d files, %d iters)\n",
           total / iterations, total_entries, total_files, iterations);
    if (total_files > 0) {
        printf("  per-album avg       : %7.1f ms\n",
               total / iterations / ((total_entries > 0) ?
                   (double)total_entries : 1.0));
    }
}

/* =============================================================================
 * Benchmark: cleanup_idle cycle
 * ===========================================================================*/

static void bench_cleanup(sacd_overlay_ctx_t *ctx, int iterations)
{
    double total = 0;

    for (int i = 0; i < iterations; i++) {
        double t0 = get_time_ms();
        int cleaned = sacd_overlay_cleanup_idle(ctx);
        double t1 = get_time_ms();
        total += (t1 - t0);

        if (i == 0) {
            printf("  cleanup_idle        : %7.1f ms (cleaned %d)\n",
                   t1 - t0, cleaned);
        }
    }

    if (iterations > 1) {
        printf("  cleanup_idle avg    : %7.1f ms (%d iters)\n",
               total / iterations, iterations);
    }
}

/* =============================================================================
 * Main
 * ===========================================================================*/

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source_dir> [iterations]\n", argv[0]);
        fprintf(stderr, "\nRuns overlay API benchmarks against the given source directory.\n");
        return 1;
    }

    const char *source_dir = argv[1];
    int iterations = (argc > 2) ? atoi(argv[2]) : 5;
    if (iterations < 1) iterations = 1;

    printf("=== SACD Overlay Benchmark ===\n");
    printf("Source: %s\n", source_dir);
    printf("Iterations: %d\n\n", iterations);

    /* Enable debug logging */
    sa_log_set_level(SA_LOG_WARNING);

    /* Create overlay context */
    sacd_overlay_config_t config;
    sacd_overlay_config_init(&config);
    config.source_dir = source_dir;
    config.cache_timeout_seconds = 0;  /* No timeout for benchmarks */

    double t0 = get_time_ms();
    sacd_overlay_ctx_t *ctx = sacd_overlay_create(&config);
    double t1 = get_time_ms();

    if (!ctx) {
        fprintf(stderr, "Error: Failed to create overlay context for: %s\n",
                source_dir);
        return 1;
    }

    printf("  create context      : %7.1f ms\n", t1 - t0);

    /* Collect root entries */
    entry_list_t root_entries;
    root_entries.count = 0;

    printf("\n--- First access (cold) ---\n");
    bench_readdir_root(ctx, 1, &root_entries);
    bench_stat_root_entries(ctx, 1, &root_entries);

    path_list_t *all_files = (path_list_t *)calloc(1, sizeof(path_list_t));
    if (!all_files) {
        fprintf(stderr, "Error: Out of memory\n");
        sacd_overlay_destroy(ctx);
        return 1;
    }

    bench_readdir_isos(ctx, 1, &root_entries, all_files);

    printf("  mounted ISOs        : %d\n",
           sacd_overlay_get_mounted_iso_count(ctx));
    printf("  virtual files found : %d\n", all_files->count);

    if (all_files->count > 0) {
        bench_open_read_close(ctx, 1, all_files);
    }

    printf("\n--- Warm cache (%d iterations) ---\n", iterations);
    bench_readdir_root(ctx, iterations, NULL);
    bench_stat_root_entries(ctx, iterations, &root_entries);
    bench_readdir_isos(ctx, iterations, &root_entries, NULL);

    if (all_files->count > 0) {
        bench_open_read_close(ctx, iterations, all_files);
        bench_sequential_read(ctx, iterations > 3 ? 3 : iterations, all_files);
    }

    printf("\n--- Full Roon-like scan ---\n");
    bench_roon_scan(ctx, iterations);

    printf("\n--- Cleanup ---\n");
    bench_cleanup(ctx, iterations);

    /* Destroy */
    t0 = get_time_ms();
    sacd_overlay_destroy(ctx);
    t1 = get_time_ms();
    printf("  destroy context     : %7.1f ms\n\n", t1 - t0);

    free(all_files);

    return 0;
}
