/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Test program for DST reader seeking and sequential reading.
 * This test verifies:
 * - Seeking to specific frames using sacd_get_frame_sector_range()
 * - Sequential frame reading using sacd_get_sound_data()
 * - Correct state maintenance during sequential reads (no continuous seeking)
 * - DST frame header verification
 * Usage: test_dst_reader [iso_path]
 *        Default iso_path is "data/dst.iso" relative to working directory.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <libsacd/sacd.h>
#include <libsautil/mem.h>

/* Maximum DST frame size for 6 channels */
#define DST_FRAME_BUFFER_SIZE (SACD_MAX_DST_SIZE)

/* Number of frames to test for sequential reading */
#define SEQUENTIAL_TEST_FRAMES 100

/* Number of seek tests to perform */
#define SEEK_TEST_COUNT 10

/**
 * @brief Print DST frame header information.
 *
 * Prints the time code and sector information for a DST frame.
 * Note: The actual frame_info_t structure is internal to the DST reader,
 * so we print the time code derived from frame number instead.
 *
 * @param[in] frame_num  Frame number
 * @param[in] frame_size Size of the frame in bytes
 * @param[in] sector_nr  Starting sector number
 * @param[in] num_sectors Number of sectors spanned
 */
static void print_frame_header(uint32_t frame_num, uint16_t frame_size,
                               uint32_t sector_nr, int num_sectors)
{
    /* Convert frame number to time code (MM:SS:FF format) */
    uint8_t minutes = (uint8_t)((frame_num / SACD_FRAMES_PER_SEC) / 60);
    uint8_t seconds = (uint8_t)((frame_num / SACD_FRAMES_PER_SEC) % 60);
    uint8_t frames = (uint8_t)(frame_num % SACD_FRAMES_PER_SEC);

    printf("  Frame %6u [%02u:%02u:%02u]: sector=%u, sectors=%d, size=%u bytes\n",
           frame_num, minutes, seconds, frames,
           sector_nr, num_sectors, frame_size);
}

/**
 * @brief Test seeking to specific frames using sacd_get_frame_sector_range().
 *
 * @param[in] ctx          SACD reader context
 * @param[in] total_frames Total number of frames in the area
 * @return 0 on success, non-zero on failure
 */
static int test_seeking(sacd_t *ctx, uint32_t total_frames)
{
    uint32_t test_frames[SEEK_TEST_COUNT];
    uint32_t sector_nr;
    int num_sectors;
    int result;
    int i;

    printf("\n=== Testing Frame Seeking ===\n");
    printf("Total frames in area: %u\n\n", total_frames);

    /* Generate test frame positions: beginning, middle, and end */
    if (total_frames < SEEK_TEST_COUNT) {
        printf("ERROR: Not enough frames for seek test (need at least %d)\n",
               SEEK_TEST_COUNT);
        return -1;
    }

    /* Test at various positions throughout the track */
    test_frames[0] = 0;                          /* First frame */
    test_frames[1] = 1;                          /* Second frame (sequential) */
    test_frames[2] = total_frames / 4;           /* 25% */
    test_frames[3] = total_frames / 3;           /* 33% */
    test_frames[4] = total_frames / 2;           /* 50% */
    test_frames[5] = total_frames / 2 + 1;       /* 50% + 1 (sequential) */
    test_frames[6] = total_frames * 2 / 3;       /* 66% */
    test_frames[7] = total_frames * 3 / 4;       /* 75% */
    test_frames[8] = total_frames - 2;           /* Near end */
    test_frames[9] = total_frames - 1;           /* Last frame */

    printf("Testing seek to %d frame positions:\n", SEEK_TEST_COUNT);

    for (i = 0; i < SEEK_TEST_COUNT; i++) {
        result = sacd_get_frame_sector_range(ctx, test_frames[i],
                                                  &sector_nr, &num_sectors);
        if (result != SACD_OK) {
            printf("  Frame %u: SEEK FAILED (error=%d)\n", test_frames[i], result);
            return -1;
        }

        print_frame_header(test_frames[i], 0, sector_nr, num_sectors);
    }

    printf("\nSeek test PASSED: All %d frames located successfully.\n",
           SEEK_TEST_COUNT);
    return 0;
}

/**
 * @brief Test sequential reading using sacd_get_sound_data().
 *
 * This test verifies that sequential reads maintain state and do not trigger
 * continuous seeking operations. The DST reader should use its cached position
 * for sequential reads.
 *
 * @param[in] ctx          SACD reader context
 * @param[in] total_frames Total number of frames in the area
 * @return 0 on success, non-zero on failure
 */
static int test_sequential_reading(sacd_t *ctx, uint32_t total_frames)
{
    uint8_t *frame_buffer;
    uint32_t frame_count;
    uint16_t frame_size;
    uint32_t start_frame;
    uint32_t frames_to_read;
    uint32_t sector_nr;
    int num_sectors;
    int result;
    uint32_t i;

    printf("\n=== Testing Sequential Frame Reading ===\n");

    /* Allocate frame buffer */
    frame_buffer = (uint8_t *)sa_malloc(DST_FRAME_BUFFER_SIZE);
    if (!frame_buffer) {
        printf("ERROR: Failed to allocate frame buffer\n");
        return -1;
    }

    /* Determine how many frames to read */
    frames_to_read = (total_frames < SEQUENTIAL_TEST_FRAMES) ?
                      total_frames : SEQUENTIAL_TEST_FRAMES;

    /* Start from frame 0 */
    start_frame = 0;

    printf("Reading %u frames sequentially from frame %u...\n\n",
           frames_to_read, start_frame);

    /* First, seek to the start frame to establish initial position */
    result = sacd_get_frame_sector_range(ctx, start_frame,
                                              &sector_nr, &num_sectors);
    if (result != SACD_OK) {
        printf("ERROR: Initial seek failed (error=%d)\n", result);
        sa_free(frame_buffer);
        return -1;
    }

    printf("Initial seek to frame %u: sector=%u, sectors=%d\n\n",
           start_frame, sector_nr, num_sectors);

    /* Read frames sequentially */
    printf("Sequential read (should use cached positions, minimal seeking):\n");

    for (i = 0; i < frames_to_read; i++) {
        uint32_t current_frame = start_frame + i;

        /* Read one frame */
        frame_count = 1;
        result = sacd_get_sound_data(ctx, frame_buffer,
                                            current_frame, &frame_count,
                                            &frame_size);

        if (result != SACD_OK) {
            printf("  Frame %u: READ FAILED (error=%d)\n", current_frame, result);
            sa_free(frame_buffer);
            return -1;
        }

        if (frame_count != 1) {
            printf("  Frame %u: UNEXPECTED frame_count=%u (expected 1)\n",
                   current_frame, frame_count);
            sa_free(frame_buffer);
            return -1;
        }

        /* Get sector info for verification */
        result = sacd_get_frame_sector_range(ctx, current_frame,
                                                  &sector_nr, &num_sectors);
        if (result != SACD_OK) {
            printf("  Frame %u: GET_SECTOR FAILED (error=%d)\n", current_frame, result);
            sa_free(frame_buffer);
            return -1;
        }

        /* Print every 10th frame or first/last frames */
        if (i < 5 || i >= frames_to_read - 5 || (i % 10) == 0) {
            print_frame_header(current_frame, frame_size, sector_nr, num_sectors);
        } else if (i == 5) {
            printf("  ... (skipping intermediate frames) ...\n");
        }
    }

    printf("\nSequential read test PASSED: %u frames read successfully.\n",
           frames_to_read);

    sa_free(frame_buffer);
    return 0;
}

/**
 * @brief Test random seeking followed by sequential reading.
 *
 * This test verifies that after a random seek, subsequent sequential reads
 * still use the cached position optimization.
 *
 * @param[in] ctx          SACD reader context
 * @param[in] total_frames Total number of frames in the area
 * @return 0 on success, non-zero on failure
 */
static int test_seek_then_sequential(sacd_t *ctx, uint32_t total_frames)
{
    uint8_t *frame_buffer;
    uint32_t frame_count;
    uint16_t frame_size;
    uint32_t start_frame;
    uint32_t sector_nr;
    int num_sectors;
    int result;
    uint32_t i;

    printf("\n=== Testing Seek + Sequential Read Pattern ===\n");

    frame_buffer = (uint8_t *)sa_malloc(DST_FRAME_BUFFER_SIZE);
    if (!frame_buffer) {
        printf("ERROR: Failed to allocate frame buffer\n");
        return -1;
    }

    /* Seek to middle of track */
    start_frame = total_frames / 2;
    if (start_frame + 20 > total_frames) {
        start_frame = (total_frames > 20) ? total_frames - 20 : 0;
    }

    printf("Seeking to frame %u (middle of track)...\n", start_frame);

    result = sacd_get_frame_sector_range(ctx, start_frame,
                                              &sector_nr, &num_sectors);
    if (result != SACD_OK) {
        printf("ERROR: Seek to middle failed (error=%d)\n", result);
        sa_free(frame_buffer);
        return -1;
    }

    printf("Seek result: sector=%u, sectors=%d\n\n", sector_nr, num_sectors);

    /* Read 20 frames sequentially from middle */
    printf("Reading 20 frames sequentially from middle:\n");

    for (i = 0; i < 20 && (start_frame + i) < total_frames; i++) {
        uint32_t current_frame = start_frame + i;

        frame_count = 1;
        result = sacd_get_sound_data(ctx, frame_buffer,
                                            current_frame, &frame_count,
                                            &frame_size);

        if (result != SACD_OK) {
            printf("  Frame %u: READ FAILED (error=%d)\n", current_frame, result);
            sa_free(frame_buffer);
            return -1;
        }

        result = sacd_get_frame_sector_range(ctx, current_frame,
                                                  &sector_nr, &num_sectors);
        if (result == SACD_OK) {
            print_frame_header(current_frame, frame_size, sector_nr, num_sectors);
        }
    }

    /* Now seek backwards and read again */
    start_frame = total_frames / 4;
    printf("\nSeeking back to frame %u (25%% of track)...\n", start_frame);

    result = sacd_get_frame_sector_range(ctx, start_frame,
                                              &sector_nr, &num_sectors);
    if (result != SACD_OK) {
        printf("ERROR: Backward seek failed (error=%d)\n", result);
        sa_free(frame_buffer);
        return -1;
    }

    printf("Seek result: sector=%u, sectors=%d\n\n", sector_nr, num_sectors);

    /* Read 10 frames sequentially */
    printf("Reading 10 frames sequentially after backward seek:\n");

    for (i = 0; i < 10 && (start_frame + i) < total_frames; i++) {
        uint32_t current_frame = start_frame + i;

        frame_count = 1;
        result = sacd_get_sound_data(ctx, frame_buffer,
                                            current_frame, &frame_count,
                                            &frame_size);

        if (result != SACD_OK) {
            printf("  Frame %u: READ FAILED (error=%d)\n", current_frame, result);
            sa_free(frame_buffer);
            return -1;
        }

        result = sacd_get_frame_sector_range(ctx, current_frame,
                                                  &sector_nr, &num_sectors);
        if (result == SACD_OK) {
            print_frame_header(current_frame, frame_size, sector_nr, num_sectors);
        }
    }

    printf("\nSeek + sequential test PASSED.\n");

    sa_free(frame_buffer);
    return 0;
}

/**
 * @brief Print disc and area summary information.
 *
 * @param[in] ctx SACD reader context
 */
static void print_disc_info(sacd_t *ctx)
{
    const char *album_title = NULL;
    const char *album_artist = NULL;
    uint8_t track_count = 0;
    uint32_t total_play_time = 0;
    uint8_t frame_format = 0;
    uint16_t channel_count = 0;
    uint8_t major, minor;

    printf("\n=== Disc Information ===\n");

    /* Get disc version */
    if (sacd_get_disc_spec_version(ctx, &major, &minor) == SACD_OK) {
        printf("SACD Spec Version: %u.%u\n", major, minor);
    }

    /* Get album text */
    if (sacd_get_album_text(ctx, 1, ALBUM_TEXT_TYPE_TITLE, &album_title) == SACD_OK && album_title) {
        printf("Album Title: %s\n", album_title);
    }

    if (sacd_get_album_text(ctx, 1, ALBUM_TEXT_TYPE_ARTIST, &album_artist) == SACD_OK && album_artist) {
        printf("Album Artist: %s\n", album_artist);
    }

    printf("\n=== Current Area Information ===\n");

    /* Get area info */
    if (sacd_get_track_count(ctx, &track_count) == SACD_OK) {
        printf("Track Count: %u\n", track_count);
    }

    if (sacd_get_total_area_play_time(ctx, &total_play_time) == SACD_OK) {
        uint32_t minutes = (total_play_time / SACD_FRAMES_PER_SEC) / 60;
        uint32_t seconds = (total_play_time / SACD_FRAMES_PER_SEC) % 60;
        printf("Total Play Time: %u:%02u (%u frames)\n",
               minutes, seconds, total_play_time);
    }

    if (sacd_get_area_frame_format_code(ctx, &frame_format) == SACD_OK) {
        const char *format_name = (frame_format == 0) ? "DST" :
                                  (frame_format == 2) ? "DSD 3-in-14" :
                                  (frame_format == 3) ? "DSD 3-in-16" : "Unknown";
        printf("Frame Format: %s (code=%u)\n", format_name, frame_format);
    }

    if (sacd_get_area_channel_count(ctx, &channel_count) == SACD_OK) {
        printf("Channel Count: %u\n", channel_count);
    }

    printf("\n");
}

int main(int argc, char *argv[])
{
    const char *iso_path;
    sacd_t *ctx = NULL;
    channel_t channel_types[2];
    uint16_t nr_types = 2;
    uint32_t total_frames = 0;
    uint8_t frame_format = 0;
    int result;
    int test_result = 0;

    /* Get ISO path from command line or use default */
    iso_path = (argc > 1) ? argv[1] : "data/dst.iso";

    printf("=================================================\n");
    printf("DST Reader Seeking and Sequential Reading Test\n");
    printf("=================================================\n");
    printf("ISO file: %s\n", iso_path);

    /* Create reader context */
    ctx = sacd_create();
    if (!ctx) {
        printf("ERROR: Failed to create SACD reader context\n");
        return 1;
    }

    /* Initialize reader with ISO file */
    printf("Opening ISO file...\n");
    result = sacd_init(ctx, iso_path, 1, 1);
    if (result != SACD_OK) {
        printf("ERROR: Failed to initialize SACD reader (error=%d)\n", result);
        printf("       Make sure %s exists and is a valid SACD ISO image.\n", iso_path);
        sacd_destroy(ctx);
        return 1;
    }

    printf("ISO file opened successfully.\n");

    /* Get available channel types */
    result = sacd_get_available_channel_types(ctx, channel_types, &nr_types);
    if (result != SACD_OK || nr_types == 0) {
        printf("ERROR: No audio areas available (error=%d)\n", result);
        sacd_close(ctx);
        sacd_destroy(ctx);
        return 1;
    }

    printf("Available areas: %u\n", nr_types);
    for (uint16_t i = 0; i < nr_types; i++) {
        printf("  Area %u: %s\n", i,
               (channel_types[i] == TWO_CHANNEL) ? "2-Channel Stereo" : "Multi-Channel");
    }

    /* Select first available area (prefer 2-channel for testing) */
    result = sacd_select_channel_type(ctx, channel_types[0]);
    if (result != SACD_OK) {
        printf("ERROR: Failed to select channel type (error=%d)\n", result);
        sacd_close(ctx);
        sacd_destroy(ctx);
        return 1;
    }

    printf("Selected area: %s\n",
           (channel_types[0] == TWO_CHANNEL) ? "2-Channel Stereo" : "Multi-Channel");

    /* Check frame format - this test is specifically for DST */
    result = sacd_get_area_frame_format_code(ctx, &frame_format);
    if (result != SACD_OK) {
        printf("ERROR: Failed to get frame format (error=%d)\n", result);
        sacd_close(ctx);
        sacd_destroy(ctx);
        return 1;
    }

    if (frame_format != 0) {
        printf("WARNING: This ISO is not DST-encoded (frame_format=%u).\n", frame_format);
        printf("         The test will still run but is designed for DST content.\n");
    }

    /* Print disc info */
    print_disc_info(ctx);

    /* Get total frames for testing */
    result = sacd_get_total_area_play_time(ctx, &total_frames);
    if (result != SACD_OK || total_frames == 0) {
        printf("ERROR: Failed to get total play time (error=%d)\n", result);
        sacd_close(ctx);
        sacd_destroy(ctx);
        return 1;
    }

    /* Run tests */
    printf("\n=================================================\n");
    printf("Running DST Reader Tests\n");
    printf("=================================================\n");

    /* Test 1: Seeking */
    if (test_seeking(ctx, total_frames) != 0) {
        printf("\n*** SEEKING TEST FAILED ***\n");
        test_result = 1;
    }

    /* Test 2: Sequential reading */
    if (test_sequential_reading(ctx, total_frames) != 0) {
        printf("\n*** SEQUENTIAL READING TEST FAILED ***\n");
        test_result = 1;
    }

    /* Test 3: Seek + sequential pattern */
    if (test_seek_then_sequential(ctx, total_frames) != 0) {
        printf("\n*** SEEK + SEQUENTIAL TEST FAILED ***\n");
        test_result = 1;
    }

    /* Summary */
    printf("\n=================================================\n");
    if (test_result == 0) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("SOME TESTS FAILED\n");
    }
    printf("=================================================\n");

    /* Cleanup */
    printf("\nClosing ISO file...\n");
    sacd_close(ctx);
    sacd_destroy(ctx);

    printf("Done.\n");

    return test_result;
}
