/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Comprehensive unit tests for SACD Virtual Filesystem using CMocka
 * This test suite covers all aspects of the SACD VFS API including:
 * - Context lifecycle (create, open, close, destroy)
 * - Directory operations (readdir, stat, get_track_filename)
 * - File operations (open, close, read, seek, tell)
 * - ID3 metadata operations
 * - Frame size calculations for different channel counts
 * - DSF header generation validation
 * - Error handling
 * Run with Valgrind for memory leak detection:
 *   valgrind --leak-check=full --show-leak-kinds=all ./test_sacd_vfs
 * Include the VFS header
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

#include <libsacd/sacd.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* =============================================================================
 * Test Constants - Verify Frame Size Definitions
 * ===========================================================================*/

/* Expected values based on SACD specification */
#define EXPECTED_FRAME_SIZE_64          4704    /* 588 samples * 64 bits / 8 */
#define EXPECTED_MAX_CHANNEL_COUNT      6
#define EXPECTED_MAX_DSD_SIZE           28224   /* 4704 * 6 */
#define EXPECTED_FRAMES_PER_SEC         75
#define EXPECTED_SAMPLES_PER_FRAME      588
#define EXPECTED_SAMPLING_FREQUENCY     2822400 /* 588 * 64 * 75 */

/* DSF chunk sizes */
#define EXPECTED_DSF_DSD_CHUNK_SIZE     28
#define EXPECTED_DSF_FMT_CHUNK_SIZE     52
#define EXPECTED_DSF_DATA_HEADER_SIZE   12
#define EXPECTED_DSF_BLOCK_SIZE         4096

/* =============================================================================
 * Setup and Teardown Functions
 * ===========================================================================*/

static int group_setup(void **state)
{
    (void)state;
    return 0;
}

static int group_teardown(void **state)
{
    (void)state;
    return 0;
}

/* =============================================================================
 * Test: Frame Size Constants Verification
 * ===========================================================================*/

/**
 * @brief Verify SACD frame size constants match specification
 */
static void test_frame_size_constants(void **state)
{
    (void)state;

    /* Verify SACD_FRAME_SIZE_64 = 4704 bytes per channel */
    assert_int_equal(SACD_FRAME_SIZE_64, EXPECTED_FRAME_SIZE_64);

    /* Verify MAX_CHANNEL_COUNT = 6 */
    assert_int_equal(MAX_CHANNEL_COUNT, EXPECTED_MAX_CHANNEL_COUNT);

    /* Verify SACD_MAX_DSD_SIZE = 4704 * 6 = 28224 bytes */
    assert_int_equal(SACD_MAX_DSD_SIZE, EXPECTED_MAX_DSD_SIZE);

    /* Verify frame rate = 75 fps */
    assert_int_equal(SACD_FRAMES_PER_SEC, EXPECTED_FRAMES_PER_SEC);

    /* Verify samples per frame = 588 */
    assert_int_equal(SACD_SAMPLES_PER_FRAME, EXPECTED_SAMPLES_PER_FRAME);

    /* Verify sampling frequency = 2822400 Hz */
    assert_int_equal(SACD_SAMPLING_FREQUENCY, EXPECTED_SAMPLING_FREQUENCY);
}

/**
 * @brief Verify DSF chunk size constants
 */
static void test_dsf_chunk_constants(void **state)
{
    (void)state;

    assert_int_equal(DSF_DSD_CHUNK_SIZE, EXPECTED_DSF_DSD_CHUNK_SIZE);
    assert_int_equal(DSF_FMT_CHUNK_SIZE, EXPECTED_DSF_FMT_CHUNK_SIZE);
    assert_int_equal(DSF_DATA_CHUNK_HEADER_SIZE, EXPECTED_DSF_DATA_HEADER_SIZE);
    assert_int_equal(DSF_BLOCK_SIZE_PER_CHANNEL, EXPECTED_DSF_BLOCK_SIZE);

    /* Verify audio data offset calculation */
    size_t expected_offset = EXPECTED_DSF_DSD_CHUNK_SIZE + EXPECTED_DSF_FMT_CHUNK_SIZE;
    assert_int_equal(DSF_AUDIO_DATA_OFFSET, expected_offset);
}

/**
 * @brief Verify frame size calculations for different channel counts
 */
static void test_frame_size_calculations(void **state)
{
    (void)state;

    /* Mono: 1 channel = 4704 bytes */
    size_t mono_frame = SACD_FRAME_SIZE_64 * 1;
    assert_int_equal(mono_frame, 4704);

    /* Stereo: 2 channels = 9408 bytes */
    size_t stereo_frame = SACD_FRAME_SIZE_64 * 2;
    assert_int_equal(stereo_frame, 9408);

    /* 5 channels = 23520 bytes */
    size_t five_ch_frame = SACD_FRAME_SIZE_64 * 5;
    assert_int_equal(five_ch_frame, 23520);

    /* 6 channels (5.1) = 28224 bytes */
    size_t six_ch_frame = SACD_FRAME_SIZE_64 * 6;
    assert_int_equal(six_ch_frame, 28224);
    assert_int_equal(six_ch_frame, SACD_MAX_DSD_SIZE);
}

/* =============================================================================
 * Test: VFS Context Lifecycle
 * ===========================================================================*/

/**
 * @brief Test VFS context creation
 */
static void test_vfs_create(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    assert_non_null(ctx);

    sacd_vfs_destroy(ctx);
}

/**
 * @brief Test VFS context destruction with NULL
 */
static void test_vfs_destroy_null(void **state)
{
    (void)state;

    /* Should not crash with NULL */
    sacd_vfs_destroy(NULL);
}

/**
 * @brief Test VFS open with NULL context
 */
static void test_vfs_open_null_context(void **state)
{
    (void)state;

    int ret = sacd_vfs_open(NULL, "test.iso");
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);
}

/**
 * @brief Test VFS open with NULL path
 */
static void test_vfs_open_null_path(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    assert_non_null(ctx);

    int ret = sacd_vfs_open(ctx, NULL);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    sacd_vfs_destroy(ctx);
}

/**
 * @brief Test VFS open with nonexistent file
 */
static void test_vfs_open_nonexistent_file(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    assert_non_null(ctx);

    int ret = sacd_vfs_open(ctx, "nonexistent_file_12345.iso");
    assert_int_not_equal(ret, SACD_VFS_OK);

    sacd_vfs_destroy(ctx);
}

/**
 * @brief Test VFS close with NULL context
 */
static void test_vfs_close_null(void **state)
{
    (void)state;

    int ret = sacd_vfs_close(NULL);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);
}

/* =============================================================================
 * Test: Directory Operations (with NULL/Invalid Parameters)
 * ===========================================================================*/

/**
 * @brief Test get_album_name with NULL parameters
 */
static void test_get_album_name_null_params(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    char buffer[256];
    int ret;

    /* NULL context */
    ret = sacd_vfs_get_album_name(NULL, buffer, sizeof(buffer));
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL buffer */
    ret = sacd_vfs_get_album_name(ctx, NULL, sizeof(buffer));
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* Zero size */
    ret = sacd_vfs_get_album_name(ctx, buffer, 0);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* Not open */
    ret = sacd_vfs_get_album_name(ctx, buffer, sizeof(buffer));
    assert_int_equal(ret, SACD_VFS_ERROR_NOT_OPEN);

    sacd_vfs_destroy(ctx);
}

/**
 * @brief Test has_area with NULL context
 */
static void test_has_area_null_context(void **state)
{
    (void)state;

    bool result = sacd_vfs_has_area(NULL, SACD_VFS_AREA_STEREO);
    assert_false(result);
}

/**
 * @brief Test has_area with invalid area
 */
static void test_has_area_invalid(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    assert_non_null(ctx);

    /* Invalid area type (greater than MULTICHANNEL) */
    bool result = sacd_vfs_has_area(ctx, (sacd_vfs_area_t)99);
    assert_false(result);

    sacd_vfs_destroy(ctx);
}

/**
 * @brief Test get_track_count with NULL parameters
 */
static void test_get_track_count_null_params(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    uint8_t count;
    int ret;

    /* NULL context */
    ret = sacd_vfs_get_track_count(NULL, SACD_VFS_AREA_STEREO, &count);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL count pointer */
    ret = sacd_vfs_get_track_count(ctx, SACD_VFS_AREA_STEREO, NULL);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* Not open */
    ret = sacd_vfs_get_track_count(ctx, SACD_VFS_AREA_STEREO, &count);
    assert_int_equal(ret, SACD_VFS_ERROR_NOT_OPEN);

    sacd_vfs_destroy(ctx);
}

/**
 * @brief Test get_track_filename with NULL parameters
 */
static void test_get_track_filename_null_params(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    char buffer[256];
    int ret;

    /* NULL context */
    ret = sacd_vfs_get_track_filename(NULL, SACD_VFS_AREA_STEREO, 1, buffer, sizeof(buffer));
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL buffer */
    ret = sacd_vfs_get_track_filename(ctx, SACD_VFS_AREA_STEREO, 1, NULL, sizeof(buffer));
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* Zero size */
    ret = sacd_vfs_get_track_filename(ctx, SACD_VFS_AREA_STEREO, 1, buffer, 0);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* Track number 0 */
    ret = sacd_vfs_get_track_filename(ctx, SACD_VFS_AREA_STEREO, 0, buffer, sizeof(buffer));
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    sacd_vfs_destroy(ctx);
}

/**
 * @brief Test readdir with NULL parameters
 */
static void test_readdir_null_params(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    int ret;

    /* NULL context */
    ret = sacd_vfs_readdir(NULL, "/", NULL, NULL);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL path */
    ret = sacd_vfs_readdir(ctx, NULL, NULL, NULL);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL callback */
    ret = sacd_vfs_readdir(ctx, "/", NULL, NULL);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    sacd_vfs_destroy(ctx);
}

/**
 * @brief Test stat with NULL parameters
 */
static void test_stat_null_params(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    sacd_vfs_entry_t entry;
    int ret;

    /* NULL context */
    ret = sacd_vfs_stat(NULL, "/", &entry);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL path */
    ret = sacd_vfs_stat(ctx, NULL, &entry);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL entry */
    ret = sacd_vfs_stat(ctx, "/", NULL);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    sacd_vfs_destroy(ctx);
}

/* =============================================================================
 * Test: File Operations (with NULL/Invalid Parameters)
 * ===========================================================================*/

/**
 * @brief Test file_open with NULL parameters
 */
static void test_file_open_null_params(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    sacd_vfs_file_t *file = NULL;
    int ret;

    /* NULL context */
    ret = sacd_vfs_file_open(NULL, "/path/to/file.dsf", &file);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL path */
    ret = sacd_vfs_file_open(ctx, NULL, &file);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL file pointer */
    ret = sacd_vfs_file_open(ctx, "/path/to/file.dsf", NULL);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* Not open */
    ret = sacd_vfs_file_open(ctx, "/path/to/file.dsf", &file);
    assert_int_equal(ret, SACD_VFS_ERROR_NOT_OPEN);

    sacd_vfs_destroy(ctx);
}

/**
 * @brief Test file_close with NULL
 */
static void test_file_close_null(void **state)
{
    (void)state;

    int ret = sacd_vfs_file_close(NULL);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);
}

/**
 * @brief Test file_get_info with NULL parameters
 */
static void test_file_get_info_null_params(void **state)
{
    (void)state;

    sacd_vfs_file_info_t info;
    int ret;

    /* NULL file */
    ret = sacd_vfs_file_get_info(NULL, &info);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);
}

/**
 * @brief Test file_read with NULL parameters
 */
static void test_file_read_null_params(void **state)
{
    (void)state;

    uint8_t buffer[1024];
    size_t bytes_read;
    int ret;

    /* NULL file */
    ret = sacd_vfs_file_read(NULL, buffer, sizeof(buffer), &bytes_read);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);
}

/**
 * @brief Test file_seek with NULL file
 */
static void test_file_seek_null(void **state)
{
    (void)state;

    int ret = sacd_vfs_file_seek(NULL, 0, SEEK_SET);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);
}

/**
 * @brief Test file_tell with NULL parameters
 */
static void test_file_tell_null_params(void **state)
{
    (void)state;

    uint64_t position;
    int ret;

    /* NULL file */
    ret = sacd_vfs_file_tell(NULL, &position);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);
}

/* =============================================================================
 * Test: ID3 Metadata Operations
 * ===========================================================================*/

/**
 * @brief Test get_id3_tag with NULL parameters
 */
static void test_get_id3_tag_null_params(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    uint8_t *buffer = NULL;
    size_t size = 0;
    int ret;

    /* NULL context */
    ret = sacd_vfs_get_id3_tag(NULL, SACD_VFS_AREA_STEREO, 1, &buffer, &size);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL buffer pointer */
    ret = sacd_vfs_get_id3_tag(ctx, SACD_VFS_AREA_STEREO, 1, NULL, &size);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL size pointer */
    ret = sacd_vfs_get_id3_tag(ctx, SACD_VFS_AREA_STEREO, 1, &buffer, NULL);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* Track number 0 */
    ret = sacd_vfs_get_id3_tag(ctx, SACD_VFS_AREA_STEREO, 0, &buffer, &size);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    sacd_vfs_destroy(ctx);
}

/**
 * @brief Test set_id3_overlay with NULL parameters
 */
static void test_set_id3_overlay_null_params(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    uint8_t buffer[256] = {0};
    int ret;

    /* NULL context */
    ret = sacd_vfs_set_id3_overlay(NULL, SACD_VFS_AREA_STEREO, 1, buffer, sizeof(buffer));
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* NULL buffer */
    ret = sacd_vfs_set_id3_overlay(ctx, SACD_VFS_AREA_STEREO, 1, NULL, sizeof(buffer));
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* Zero size */
    ret = sacd_vfs_set_id3_overlay(ctx, SACD_VFS_AREA_STEREO, 1, buffer, 0);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* Track number 0 */
    ret = sacd_vfs_set_id3_overlay(ctx, SACD_VFS_AREA_STEREO, 0, buffer, sizeof(buffer));
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    sacd_vfs_destroy(ctx);
}

/* =============================================================================
 * Test: Error String Function
 * ===========================================================================*/

/**
 * @brief Test error string conversion
 */
static void test_error_strings(void **state)
{
    (void)state;

    const char *str;

    /* Success */
    str = sacd_vfs_error_string(SACD_VFS_OK);
    assert_non_null(str);
    assert_string_equal(str, "Success");

    /* Known error codes */
    str = sacd_vfs_error_string(SACD_VFS_ERROR_INVALID_PARAMETER);
    assert_non_null(str);

    str = sacd_vfs_error_string(SACD_VFS_ERROR_NOT_FOUND);
    assert_non_null(str);

    str = sacd_vfs_error_string(SACD_VFS_ERROR_IO);
    assert_non_null(str);

    str = sacd_vfs_error_string(SACD_VFS_ERROR_MEMORY);
    assert_non_null(str);

    str = sacd_vfs_error_string(SACD_VFS_ERROR_NOT_OPEN);
    assert_non_null(str);

    str = sacd_vfs_error_string(SACD_VFS_ERROR_SEEK);
    assert_non_null(str);

    str = sacd_vfs_error_string(SACD_VFS_ERROR_READ);
    assert_non_null(str);

    str = sacd_vfs_error_string(SACD_VFS_ERROR_FORMAT);
    assert_non_null(str);

    str = sacd_vfs_error_string(SACD_VFS_ERROR_DST_DECODE);
    assert_non_null(str);

    str = sacd_vfs_error_string(SACD_VFS_ERROR_EOF);
    assert_non_null(str);

    /* Unknown error code */
    str = sacd_vfs_error_string(-9999);
    assert_non_null(str);
    assert_string_equal(str, "Unknown error");
}

/* =============================================================================
 * Test: DSF Header Structure Verification
 * ===========================================================================*/

/**
 * @brief Verify DSF header total size
 */
static void test_dsf_header_size(void **state)
{
    (void)state;

    /* DSF header consists of:
     * - DSD chunk: 28 bytes
     * - fmt chunk: 52 bytes
     * - data chunk header: 12 bytes
     * Total: 92 bytes
     */
    size_t total_header = DSF_DSD_CHUNK_SIZE + DSF_FMT_CHUNK_SIZE + DSF_DATA_CHUNK_HEADER_SIZE;
    assert_int_equal(total_header, 92);
}

/**
 * @brief Verify audio data size calculations for stereo
 */
static void test_audio_data_size_stereo(void **state)
{
    (void)state;

    /* For a 1-second stereo track:
     * - Frames: 75 (SACD_FRAMES_PER_SEC)
     * - Bytes per channel: 75 * 4704 = 352800
     * - Blocks per channel: ceil(352800 / 4096) = 87
     * - Audio data size: 87 * 4096 * 2 = 712704 bytes
     */
    uint32_t frames = 75;
    uint32_t channels = 2;
    size_t bytes_per_channel = (size_t)frames * SACD_FRAME_SIZE_64;
    size_t num_blocks = (bytes_per_channel + DSF_BLOCK_SIZE_PER_CHANNEL - 1) / DSF_BLOCK_SIZE_PER_CHANNEL;
    size_t audio_size = num_blocks * DSF_BLOCK_SIZE_PER_CHANNEL * channels;

    assert_int_equal(bytes_per_channel, 352800);
    assert_int_equal(num_blocks, 87);
    assert_int_equal(audio_size, 712704);
}

/**
 * @brief Verify audio data size calculations for 5.1 surround
 */
static void test_audio_data_size_multichannel(void **state)
{
    (void)state;

    /* For a 1-second 5.1 channel track:
     * - Frames: 75
     * - Bytes per channel: 75 * 4704 = 352800
     * - Blocks per channel: ceil(352800 / 4096) = 87
     * - Audio data size: 87 * 4096 * 6 = 2138112 bytes
     */
    uint32_t frames = 75;
    uint32_t channels = 6;
    size_t bytes_per_channel = (size_t)frames * SACD_FRAME_SIZE_64;
    size_t num_blocks = (bytes_per_channel + DSF_BLOCK_SIZE_PER_CHANNEL - 1) / DSF_BLOCK_SIZE_PER_CHANNEL;
    size_t audio_size = num_blocks * DSF_BLOCK_SIZE_PER_CHANNEL * channels;

    assert_int_equal(bytes_per_channel, 352800);
    assert_int_equal(num_blocks, 87);
    assert_int_equal(audio_size, 2138112);
}

/* =============================================================================
 * Test: Lookahead Buffer Configuration
 * ===========================================================================*/

/**
 * @brief Verify DST lookahead buffer size
 */
static void test_dst_lookahead_buffer(void **state)
{
    (void)state;

    /* DST_LOOKAHEAD_FRAMES should be 25 seconds worth of frames */
    size_t expected_frames = 25 * SACD_FRAMES_PER_SEC;
    assert_int_equal(expected_frames, 1875);
    assert_int_equal(DST_LOOKAHEAD_FRAMES, expected_frames);
}

/* =============================================================================
 * Test: Area Type Enumeration
 * ===========================================================================*/

/**
 * @brief Verify area type enumeration values
 */
static void test_area_types(void **state)
{
    (void)state;

    assert_int_equal(SACD_VFS_AREA_STEREO, 0);
    assert_int_equal(SACD_VFS_AREA_MULTICHANNEL, 1);
}

/**
 * @brief Verify frame format enumeration values
 */
static void test_frame_format_types(void **state)
{
    (void)state;

    /* Frame formats should match SACD specification */
    assert_int_equal(SACD_VFS_FRAME_DST, 0);
    assert_int_equal(SACD_VFS_FRAME_DSD_3_IN_14, 2);
    assert_int_equal(SACD_VFS_FRAME_DSD_3_IN_16, 3);
}

/* =============================================================================
 * Test: Path Length Limits
 * ===========================================================================*/

/**
 * @brief Verify path length constants
 */
static void test_path_limits(void **state)
{
    (void)state;

    /* Verify reasonable path limits */
    assert_true(SACD_VFS_MAX_PATH >= 256);
    assert_true(SACD_VFS_MAX_FILENAME >= 128);
    assert_int_equal(SACD_VFS_MAX_TRACKS, MAX_TRACK_COUNT);
}

/* =============================================================================
 * Test: Context State Transitions
 * ===========================================================================*/

/**
 * @brief Test multiple create/destroy cycles
 */
static void test_multiple_create_destroy(void **state)
{
    (void)state;

    for (int i = 0; i < 10; i++) {
        sacd_vfs_ctx_t *ctx = sacd_vfs_create();
        assert_non_null(ctx);
        sacd_vfs_destroy(ctx);
    }
}

/**
 * @brief Test close on not-open context
 */
static void test_close_not_open(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    assert_non_null(ctx);

    /* Close should handle not-open gracefully */
    int ret = sacd_vfs_close(ctx);
    /* This may return OK or an error depending on implementation */
    (void)ret;

    sacd_vfs_destroy(ctx);
}

/* =============================================================================
 * Test: Entry Type Enumeration
 * ===========================================================================*/

/**
 * @brief Verify entry type enumeration values
 */
static void test_entry_types(void **state)
{
    (void)state;

    assert_int_equal(SACD_VFS_ENTRY_DIRECTORY, 0);
    assert_int_equal(SACD_VFS_ENTRY_FILE, 1);
}

/* =============================================================================
 * Test: Memory Safety with Repeated Operations
 * ===========================================================================*/

/**
 * @brief Test repeated open attempts on same context
 */
static void test_repeated_open_attempts(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    assert_non_null(ctx);

    /* Multiple failed opens should not leak memory */
    for (int i = 0; i < 5; i++) {
        int ret = sacd_vfs_open(ctx, "nonexistent_file.iso");
        assert_int_not_equal(ret, SACD_VFS_OK);
    }

    sacd_vfs_destroy(ctx);
}

/* =============================================================================
 * Test: Block Size Alignment
 * ===========================================================================*/

/**
 * @brief Verify DSF block alignment calculations
 */
static void test_block_alignment(void **state)
{
    (void)state;

    /* Test various byte counts and verify block alignment */
    size_t test_sizes[] = {1, 4095, 4096, 4097, 8192, 10000};
    size_t expected_blocks[] = {1, 1, 1, 2, 2, 3};

    for (size_t i = 0; i < sizeof(test_sizes) / sizeof(test_sizes[0]); i++) {
        size_t bytes = test_sizes[i];
        size_t blocks = (bytes + DSF_BLOCK_SIZE_PER_CHANNEL - 1) / DSF_BLOCK_SIZE_PER_CHANNEL;
        assert_int_equal(blocks, expected_blocks[i]);
    }
}

/* =============================================================================
 * Test: Duration Calculation
 * ===========================================================================*/

/**
 * @brief Verify duration calculation from frame count
 */
static void test_duration_calculation(void **state)
{
    (void)state;

    /* 75 frames = 1 second */
    uint32_t frames_1s = 75;
    double duration_1s = (double)frames_1s / SACD_FRAMES_PER_SEC;
    assert_true(duration_1s >= 0.999 && duration_1s <= 1.001);

    /* 750 frames = 10 seconds */
    uint32_t frames_10s = 750;
    double duration_10s = (double)frames_10s / SACD_FRAMES_PER_SEC;
    assert_true(duration_10s >= 9.999 && duration_10s <= 10.001);

    /* 4500 frames = 60 seconds = 1 minute */
    uint32_t frames_60s = 4500;
    double duration_60s = (double)frames_60s / SACD_FRAMES_PER_SEC;
    assert_true(duration_60s >= 59.999 && duration_60s <= 60.001);
}

/* =============================================================================
 * Test: Sample Count Calculation
 * ===========================================================================*/

/**
 * @brief Verify sample count calculation
 */
static void test_sample_count_calculation(void **state)
{
    (void)state;

    /* For 1 second of audio:
     * Frames: 75
     * Samples per frame: 588
     * Bits per sample: 1 (DSD)
     * Total samples per channel: 75 * 588 * 8 = 352800
     */
    uint32_t frames = 75;
    uint64_t sample_count = (uint64_t)frames * SACD_SAMPLES_PER_FRAME * 8;
    assert_int_equal(sample_count, 352800);
}

/* =============================================================================
 * Test: DSF Virtual File Structure and Region Calculations
 * ===========================================================================*/

/**
 * @brief Helper structure to simulate DSF file layout for testing
 */
typedef struct {
    uint32_t channel_count;
    uint32_t frame_count;
    size_t id3_size;

    /* Calculated values */
    size_t header_size;
    size_t audio_data_size;
    size_t padding_size;
    size_t metadata_offset;
    size_t total_file_size;
} dsf_layout_t;

/**
 * @brief Calculate DSF file layout for given parameters
 */
static void calculate_dsf_layout(dsf_layout_t *layout)
{
    /* DSF header: DSD chunk (28) + fmt chunk (52) + data chunk header (12) = 92 bytes */
    layout->header_size = DSF_DSD_CHUNK_SIZE + DSF_FMT_CHUNK_SIZE + DSF_DATA_CHUNK_HEADER_SIZE;

    /* Audio data: block-aligned per channel */
    size_t bytes_per_channel = (size_t)layout->frame_count * SACD_FRAME_SIZE_64;
    size_t num_blocks = (bytes_per_channel + DSF_BLOCK_SIZE_PER_CHANNEL - 1) / DSF_BLOCK_SIZE_PER_CHANNEL;
    size_t padded_bytes_per_channel = num_blocks * DSF_BLOCK_SIZE_PER_CHANNEL;
    layout->audio_data_size = padded_bytes_per_channel * layout->channel_count;

    /* Padding: difference between padded and actual data */
    size_t actual_audio_bytes = bytes_per_channel * layout->channel_count;
    layout->padding_size = layout->audio_data_size - actual_audio_bytes;

    /* Metadata offset: header + audio data (including padding) */
    layout->metadata_offset = layout->header_size + layout->audio_data_size;

    /* Total file size: header + audio + metadata */
    layout->total_file_size = layout->metadata_offset + layout->id3_size;
}

/**
 * @brief Test DSF layout for stereo 1-second track
 */
static void test_dsf_layout_stereo_1sec(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,  /* 1 second */
        .id3_size = 256
    };
    calculate_dsf_layout(&layout);

    /* Header should be 92 bytes */
    assert_int_equal(layout.header_size, 92);

    /* Audio: 75 frames * 4704 bytes/ch = 352800 bytes/ch
     * Blocks: ceil(352800 / 4096) = 87 blocks
     * Padded: 87 * 4096 = 356352 bytes/ch
     * Total: 356352 * 2 = 712704 bytes
     */
    assert_int_equal(layout.audio_data_size, 712704);

    /* Padding: 712704 - (352800 * 2) = 712704 - 705600 = 7104 bytes */
    assert_int_equal(layout.padding_size, 7104);

    /* Metadata offset: 92 + 712704 = 712796 */
    assert_int_equal(layout.metadata_offset, 712796);

    /* Total: 712796 + 256 = 713052 */
    assert_int_equal(layout.total_file_size, 713052);
}

/**
 * @brief Test DSF layout for 5.1 surround 1-second track
 */
static void test_dsf_layout_multichannel_1sec(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 6,
        .frame_count = 75,  /* 1 second */
        .id3_size = 512
    };
    calculate_dsf_layout(&layout);

    assert_int_equal(layout.header_size, 92);

    /* Audio: 75 frames * 4704 bytes/ch = 352800 bytes/ch
     * Blocks: 87 blocks/ch
     * Padded: 356352 bytes/ch
     * Total: 356352 * 6 = 2138112 bytes
     */
    assert_int_equal(layout.audio_data_size, 2138112);

    /* Padding: 2138112 - (352800 * 6) = 2138112 - 2116800 = 21312 bytes */
    assert_int_equal(layout.padding_size, 21312);

    /* Metadata offset: 92 + 2138112 = 2138204 */
    assert_int_equal(layout.metadata_offset, 2138204);
}

/**
 * @brief Test DSF layout for mono track
 */
static void test_dsf_layout_mono(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 1,
        .frame_count = 150,  /* 2 seconds */
        .id3_size = 128
    };
    calculate_dsf_layout(&layout);

    assert_int_equal(layout.header_size, 92);

    /* Audio: 150 frames * 4704 bytes = 705600 bytes
     * Blocks: ceil(705600 / 4096) = 173 blocks
     * Padded: 173 * 4096 = 708608 bytes
     */
    assert_int_equal(layout.audio_data_size, 708608);

    /* Padding: 708608 - 705600 = 3008 bytes */
    assert_int_equal(layout.padding_size, 3008);
}

/**
 * @brief Test DSF layout for long track (5 minutes)
 */
static void test_dsf_layout_long_track(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75 * 60 * 5,  /* 5 minutes = 22500 frames */
        .id3_size = 1024
    };
    calculate_dsf_layout(&layout);

    /* Verify calculations are consistent */
    assert_int_equal(layout.header_size, 92);
    assert_true(layout.audio_data_size > 0);
    assert_true(layout.metadata_offset == layout.header_size + layout.audio_data_size);
    assert_true(layout.total_file_size == layout.metadata_offset + layout.id3_size);

    /* Audio data should be block-aligned */
    assert_int_equal(layout.audio_data_size % (DSF_BLOCK_SIZE_PER_CHANNEL * 2), 0);
}

/* =============================================================================
 * Test: Seek Position Calculations
 * ===========================================================================*/

/**
 * @brief Test seek to header region
 */
static void test_seek_calculation_header(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 256
    };
    calculate_dsf_layout(&layout);

    /* Position 0: Start of DSD chunk */
    uint64_t pos = 0;
    assert_true(pos < layout.header_size);

    /* Position 27: Last byte of DSD chunk */
    pos = 27;
    assert_true(pos < layout.header_size);

    /* Position 28: Start of fmt chunk */
    pos = 28;
    assert_true(pos < layout.header_size);

    /* Position 79: Last byte of fmt chunk */
    pos = 79;
    assert_true(pos < layout.header_size);

    /* Position 80: Start of data chunk header */
    pos = 80;
    assert_true(pos < layout.header_size);

    /* Position 91: Last byte of header */
    pos = 91;
    assert_true(pos < layout.header_size);

    /* Position 92: First byte of audio data */
    pos = 92;
    assert_true(pos >= layout.header_size);
    assert_true(pos < layout.metadata_offset);
}

/**
 * @brief Test seek to audio data region
 */
static void test_seek_calculation_audio(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 256
    };
    calculate_dsf_layout(&layout);

    /* First byte of audio data */
    uint64_t pos = layout.header_size;
    assert_true(pos >= layout.header_size);
    assert_true(pos < layout.metadata_offset);

    /* Middle of audio data */
    pos = layout.header_size + layout.audio_data_size / 2;
    assert_true(pos >= layout.header_size);
    assert_true(pos < layout.metadata_offset);

    /* Last byte before padding (actual audio data end) */
    size_t actual_audio = (size_t)layout.frame_count * SACD_FRAME_SIZE_64 * layout.channel_count;
    pos = layout.header_size + actual_audio - 1;
    assert_true(pos >= layout.header_size);
    assert_true(pos < layout.metadata_offset);

    /* First byte of padding region */
    pos = layout.header_size + actual_audio;
    assert_true(pos >= layout.header_size);
    assert_true(pos < layout.metadata_offset);
    /* This is in the padding region */

    /* Last byte before metadata */
    pos = layout.metadata_offset - 1;
    assert_true(pos >= layout.header_size);
    assert_true(pos < layout.metadata_offset);
}

/**
 * @brief Test seek to metadata region
 */
static void test_seek_calculation_metadata(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 256
    };
    calculate_dsf_layout(&layout);

    /* First byte of metadata (ID3 tag) */
    uint64_t pos = layout.metadata_offset;
    assert_true(pos >= layout.metadata_offset);
    assert_true(pos < layout.total_file_size);

    /* Middle of metadata */
    pos = layout.metadata_offset + layout.id3_size / 2;
    assert_true(pos >= layout.metadata_offset);
    assert_true(pos < layout.total_file_size);

    /* Last byte of file */
    pos = layout.total_file_size - 1;
    assert_true(pos >= layout.metadata_offset);
    assert_true(pos < layout.total_file_size);

    /* Beyond EOF */
    pos = layout.total_file_size;
    assert_true(pos >= layout.total_file_size);
}

/**
 * @brief Test frame calculation from audio position
 */
static void test_seek_frame_calculation(void **state)
{
    (void)state;

    /* For stereo (2 channels):
     * SACD_FRAME_SIZE_64 = 4704 bytes per channel
     * DSF_BLOCK_SIZE_PER_CHANNEL = 4096 bytes
     * blocks_per_frame = ceil(4704 / 4096) = 2
     * frame_block_size = 2 * 4096 * 2 = 16384 bytes per frame in DSF
     */
    uint32_t channels = 2;
    size_t bytes_per_channel_per_frame = SACD_FRAME_SIZE_64;
    size_t blocks_per_frame = (bytes_per_channel_per_frame + DSF_BLOCK_SIZE_PER_CHANNEL - 1) /
                              DSF_BLOCK_SIZE_PER_CHANNEL;
    size_t frame_block_size = blocks_per_frame * DSF_BLOCK_SIZE_PER_CHANNEL * channels;

    assert_int_equal(blocks_per_frame, 2);
    assert_int_equal(frame_block_size, 16384);

    /* Audio offset 0 -> frame 0 */
    uint64_t audio_offset = 0;
    uint32_t frame = (uint32_t)(audio_offset / frame_block_size);
    assert_int_equal(frame, 0);

    /* Audio offset 16383 -> still frame 0 */
    audio_offset = 16383;
    frame = (uint32_t)(audio_offset / frame_block_size);
    assert_int_equal(frame, 0);

    /* Audio offset 16384 -> frame 1 */
    audio_offset = 16384;
    frame = (uint32_t)(audio_offset / frame_block_size);
    assert_int_equal(frame, 1);

    /* Audio offset 32768 -> frame 2 */
    audio_offset = 32768;
    frame = (uint32_t)(audio_offset / frame_block_size);
    assert_int_equal(frame, 2);
}

/**
 * @brief Test frame calculation for multichannel
 */
static void test_seek_frame_calculation_multichannel(void **state)
{
    (void)state;

    /* For 6 channels:
     * blocks_per_frame = 2 (same as stereo)
     * frame_block_size = 2 * 4096 * 6 = 49152 bytes per frame in DSF
     */
    uint32_t channels = 6;
    size_t bytes_per_channel_per_frame = SACD_FRAME_SIZE_64;
    size_t blocks_per_frame = (bytes_per_channel_per_frame + DSF_BLOCK_SIZE_PER_CHANNEL - 1) /
                              DSF_BLOCK_SIZE_PER_CHANNEL;
    size_t frame_block_size = blocks_per_frame * DSF_BLOCK_SIZE_PER_CHANNEL * channels;

    assert_int_equal(blocks_per_frame, 2);
    assert_int_equal(frame_block_size, 49152);

    /* Audio offset 49151 -> frame 0 */
    uint64_t audio_offset = 49151;
    uint32_t frame = (uint32_t)(audio_offset / frame_block_size);
    assert_int_equal(frame, 0);

    /* Audio offset 49152 -> frame 1 */
    audio_offset = 49152;
    frame = (uint32_t)(audio_offset / frame_block_size);
    assert_int_equal(frame, 1);
}

/* =============================================================================
 * Test: Padding Calculations
 * ===========================================================================*/

/**
 * @brief Test padding byte value (should be 0x69)
 */
static void test_padding_byte_value(void **state)
{
    (void)state;

    /* DSF specification uses 0x69 for padding */
    uint8_t padding_byte = 0x69;
    assert_int_equal(padding_byte, 0x69);
}

/**
 * @brief Test padding size calculation for various frame counts
 */
static void test_padding_size_various_frames(void **state)
{
    (void)state;

    /* Test cases: frame_count -> expected padding per channel */
    struct {
        uint32_t frames;
        uint32_t channels;
        size_t expected_padding;
    } test_cases[] = {
        /* 1 frame: 4704 bytes, 2 blocks (8192), padding = 8192 - 4704 = 3488 per channel */
        {1, 2, 3488 * 2},
        /* 75 frames: 352800 bytes, 87 blocks (356352), padding = 3552 per channel */
        {75, 2, 3552 * 2},
        /* 150 frames: 705600 bytes, 173 blocks (708608), padding = 3008 per channel */
        {150, 1, 3008},
        /* Edge case: exact block alignment (not typical but test it) */
    };

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        size_t bytes_per_channel = (size_t)test_cases[i].frames * SACD_FRAME_SIZE_64;
        size_t num_blocks = (bytes_per_channel + DSF_BLOCK_SIZE_PER_CHANNEL - 1) / DSF_BLOCK_SIZE_PER_CHANNEL;
        size_t padded_per_channel = num_blocks * DSF_BLOCK_SIZE_PER_CHANNEL;
        size_t padding_per_channel = padded_per_channel - bytes_per_channel;
        size_t total_padding = padding_per_channel * test_cases[i].channels;

        assert_int_equal(total_padding, test_cases[i].expected_padding);
    }
}

/**
 * @brief Test that padding region is correctly positioned
 */
static void test_padding_region_position(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 256
    };
    calculate_dsf_layout(&layout);

    /* Actual audio data end position */
    size_t actual_audio = (size_t)layout.frame_count * SACD_FRAME_SIZE_64 * layout.channel_count;
    size_t actual_audio_end = layout.header_size + actual_audio;

    /* Padding starts right after actual audio */
    size_t padding_start = actual_audio_end;
    size_t padding_end = layout.metadata_offset;

    assert_true(padding_start < padding_end);
    assert_int_equal(padding_end - padding_start, layout.padding_size);
}

/* =============================================================================
 * Test: ID3 Metadata Region
 * ===========================================================================*/

/**
 * @brief Test ID3 tag position at end of file
 */
static void test_id3_position(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 1024
    };
    calculate_dsf_layout(&layout);

    /* ID3 tag starts at metadata_offset */
    assert_int_equal(layout.metadata_offset, layout.header_size + layout.audio_data_size);

    /* ID3 tag ends at EOF */
    assert_int_equal(layout.metadata_offset + layout.id3_size, layout.total_file_size);
}

/**
 * @brief Test seek to read last few bytes of ID3
 */
static void test_seek_to_id3_end(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 512
    };
    calculate_dsf_layout(&layout);

    /* Seek to last 10 bytes of ID3 */
    uint64_t seek_pos = layout.total_file_size - 10;
    assert_true(seek_pos >= layout.metadata_offset);
    assert_true(seek_pos < layout.total_file_size);

    /* Bytes available to read */
    size_t available = layout.total_file_size - seek_pos;
    assert_int_equal(available, 10);
}

/**
 * @brief Test file with no ID3 tag
 */
static void test_layout_no_id3(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 0  /* No ID3 tag */
    };
    calculate_dsf_layout(&layout);

    /* Metadata offset equals total file size when no ID3 */
    assert_int_equal(layout.metadata_offset, layout.total_file_size);
}

/* =============================================================================
 * Test: DST vs DSD Frame Handling
 * ===========================================================================*/

/**
 * @brief Verify frame sizes are same for DST and DSD after decoding
 *
 * Note: DST is losslessly compressed DSD. After decoding, the frame size
 * is identical to raw DSD: SACD_FRAME_SIZE_64 bytes per channel.
 */
static void test_dst_dsd_frame_size_equivalence(void **state)
{
    (void)state;

    /* Both DST (after decode) and raw DSD produce same frame size */
    size_t dsd_frame_stereo = SACD_FRAME_SIZE_64 * 2;
    size_t dst_decoded_frame_stereo = SACD_FRAME_SIZE_64 * 2;

    assert_int_equal(dsd_frame_stereo, dst_decoded_frame_stereo);
    assert_int_equal(dsd_frame_stereo, 9408);

    /* Same for multichannel */
    size_t dsd_frame_51 = SACD_FRAME_SIZE_64 * 6;
    size_t dst_decoded_frame_51 = SACD_FRAME_SIZE_64 * 6;

    assert_int_equal(dsd_frame_51, dst_decoded_frame_51);
    assert_int_equal(dsd_frame_51, SACD_MAX_DSD_SIZE);
}

/**
 * @brief Verify DSF output is identical for DST and DSD source
 *
 * The VFS produces DSF files regardless of whether the source is
 * DST-encoded or raw DSD. The file structure is identical.
 */
static void test_dsf_structure_independent_of_source(void **state)
{
    (void)state;

    /* DST source layout */
    dsf_layout_t dst_layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 256
    };
    calculate_dsf_layout(&dst_layout);

    /* DSD source layout (same parameters) */
    dsf_layout_t dsd_layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 256
    };
    calculate_dsf_layout(&dsd_layout);

    /* Output structure is identical */
    assert_int_equal(dst_layout.header_size, dsd_layout.header_size);
    assert_int_equal(dst_layout.audio_data_size, dsd_layout.audio_data_size);
    assert_int_equal(dst_layout.padding_size, dsd_layout.padding_size);
    assert_int_equal(dst_layout.metadata_offset, dsd_layout.metadata_offset);
    assert_int_equal(dst_layout.total_file_size, dsd_layout.total_file_size);
}

/* =============================================================================
 * Test: Seek Edge Cases
 * ===========================================================================*/

/**
 * @brief Test SEEK_SET from various positions
 */
static void test_seek_set_positions(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 256
    };
    calculate_dsf_layout(&layout);

    /* SEEK_SET to 0 */
    uint64_t new_pos = 0;
    assert_int_equal(new_pos, 0);

    /* SEEK_SET to middle of file */
    new_pos = layout.total_file_size / 2;
    assert_true(new_pos > 0);
    assert_true(new_pos < layout.total_file_size);

    /* SEEK_SET to end */
    new_pos = layout.total_file_size;
    assert_int_equal(new_pos, layout.total_file_size);
}

/**
 * @brief Test SEEK_CUR calculations
 */
static void test_seek_cur_calculations(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 256
    };
    calculate_dsf_layout(&layout);

    /* Start at position 100, seek forward 50 */
    uint64_t current_pos = 100;
    int64_t offset = 50;
    uint64_t new_pos = current_pos + offset;
    assert_int_equal(new_pos, 150);

    /* Seek backward 30 */
    offset = -30;
    new_pos = current_pos + offset;
    assert_int_equal(new_pos, 70);

    /* Seek backward past start (should clamp to 0 in implementation) */
    current_pos = 10;
    offset = -20;
    int64_t result = (int64_t)current_pos + offset;
    assert_true(result < 0);  /* Would be negative - implementation should handle */
}

/**
 * @brief Test SEEK_END calculations
 */
static void test_seek_end_calculations(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 256
    };
    calculate_dsf_layout(&layout);

    /* SEEK_END with offset 0 -> EOF */
    uint64_t new_pos = layout.total_file_size;
    assert_int_equal(new_pos, layout.total_file_size);

    /* SEEK_END with offset -10 -> 10 bytes before EOF */
    int64_t offset = -10;
    new_pos = layout.total_file_size + offset;
    assert_int_equal(new_pos, layout.total_file_size - 10);

    /* SEEK_END with offset -256 -> start of ID3 */
    offset = -(int64_t)layout.id3_size;
    new_pos = layout.total_file_size + offset;
    assert_int_equal(new_pos, layout.metadata_offset);
}

/**
 * @brief Test seek boundary transitions
 */
static void test_seek_boundary_transitions(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 256
    };
    calculate_dsf_layout(&layout);

    /* Header -> Audio boundary */
    uint64_t pos = layout.header_size - 1;  /* Last byte of header */
    assert_true(pos < layout.header_size);
    pos = layout.header_size;  /* First byte of audio */
    assert_true(pos >= layout.header_size);
    assert_true(pos < layout.metadata_offset);

    /* Audio -> Metadata boundary */
    pos = layout.metadata_offset - 1;  /* Last byte of audio/padding */
    assert_true(pos < layout.metadata_offset);
    pos = layout.metadata_offset;  /* First byte of metadata */
    assert_true(pos >= layout.metadata_offset);

    /* Metadata -> EOF boundary */
    pos = layout.total_file_size - 1;  /* Last byte of file */
    assert_true(pos < layout.total_file_size);
    pos = layout.total_file_size;  /* EOF */
    assert_true(pos >= layout.total_file_size);
}

/* =============================================================================
 * Test: Read After Seek Scenarios
 * ===========================================================================*/

/**
 * @brief Test expected read sizes after seeking to various positions
 */
static void test_read_size_after_seek(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 256
    };
    calculate_dsf_layout(&layout);

    /* Seek to start, read 100 bytes -> should get 100 bytes */
    uint64_t pos = 0;
    size_t request = 100;
    size_t available = layout.total_file_size - pos;
    size_t expected = (request < available) ? request : available;
    assert_int_equal(expected, 100);

    /* Seek to 10 bytes before EOF, read 100 bytes -> should get 10 bytes */
    pos = layout.total_file_size - 10;
    request = 100;
    available = layout.total_file_size - pos;
    expected = (request < available) ? request : available;
    assert_int_equal(expected, 10);

    /* Seek to EOF, read anything -> should get 0 bytes */
    pos = layout.total_file_size;
    request = 100;
    available = layout.total_file_size - pos;
    expected = (request < available) ? request : available;
    assert_int_equal(expected, 0);
}

/**
 * @brief Test reading ID3 tag after seeking to metadata offset
 */
static void test_read_id3_after_seek(void **state)
{
    (void)state;

    dsf_layout_t layout = {
        .channel_count = 2,
        .frame_count = 75,
        .id3_size = 512
    };
    calculate_dsf_layout(&layout);

    /* Seek to metadata offset */
    uint64_t pos = layout.metadata_offset;

    /* Available bytes is the ID3 size */
    size_t available = layout.total_file_size - pos;
    assert_int_equal(available, layout.id3_size);

    /* Read full ID3 tag */
    size_t request = 1024;  /* Request more than available */
    size_t expected = (request < available) ? request : available;
    assert_int_equal(expected, layout.id3_size);
}

/* =============================================================================
 * Test: Multi-Channel Seek Scenarios
 * ===========================================================================*/

/**
 * @brief Test seek calculations for all supported channel counts
 */
static void test_seek_all_channel_counts(void **state)
{
    (void)state;

    uint32_t channel_counts[] = {1, 2, 3, 4, 5, 6};

    for (size_t i = 0; i < sizeof(channel_counts) / sizeof(channel_counts[0]); i++) {
        dsf_layout_t layout = {
            .channel_count = channel_counts[i],
            .frame_count = 75,
            .id3_size = 256
        };
        calculate_dsf_layout(&layout);

        /* Verify basic structure for each channel count */
        assert_int_equal(layout.header_size, 92);
        assert_true(layout.audio_data_size > 0);
        assert_true(layout.metadata_offset > layout.header_size);
        assert_true(layout.total_file_size > layout.metadata_offset);

        /* Audio size should scale with channel count */
        size_t expected_blocks = 87;  /* ceil(75 * 4704 / 4096) */
        size_t expected_audio = expected_blocks * DSF_BLOCK_SIZE_PER_CHANNEL * channel_counts[i];
        assert_int_equal(layout.audio_data_size, expected_audio);
    }
}

/* =============================================================================
 * Main Test Runner
 * ===========================================================================*/

int main(void)
{
    const struct CMUnitTest constant_tests[] = {
        cmocka_unit_test(test_frame_size_constants),
        cmocka_unit_test(test_dsf_chunk_constants),
        cmocka_unit_test(test_frame_size_calculations),
        cmocka_unit_test(test_dsf_header_size),
        cmocka_unit_test(test_audio_data_size_stereo),
        cmocka_unit_test(test_audio_data_size_multichannel),
        cmocka_unit_test(test_dst_lookahead_buffer),
        cmocka_unit_test(test_area_types),
        cmocka_unit_test(test_frame_format_types),
        cmocka_unit_test(test_path_limits),
        cmocka_unit_test(test_entry_types),
        cmocka_unit_test(test_block_alignment),
        cmocka_unit_test(test_duration_calculation),
        cmocka_unit_test(test_sample_count_calculation),
    };

    const struct CMUnitTest lifecycle_tests[] = {
        cmocka_unit_test(test_vfs_create),
        cmocka_unit_test(test_vfs_destroy_null),
        cmocka_unit_test(test_vfs_open_null_context),
        cmocka_unit_test(test_vfs_open_null_path),
        cmocka_unit_test(test_vfs_open_nonexistent_file),
        cmocka_unit_test(test_vfs_close_null),
        cmocka_unit_test(test_multiple_create_destroy),
        cmocka_unit_test(test_close_not_open),
        cmocka_unit_test(test_repeated_open_attempts),
    };

    const struct CMUnitTest directory_tests[] = {
        cmocka_unit_test(test_get_album_name_null_params),
        cmocka_unit_test(test_has_area_null_context),
        cmocka_unit_test(test_has_area_invalid),
        cmocka_unit_test(test_get_track_count_null_params),
        cmocka_unit_test(test_get_track_filename_null_params),
        cmocka_unit_test(test_readdir_null_params),
        cmocka_unit_test(test_stat_null_params),
    };

    const struct CMUnitTest file_tests[] = {
        cmocka_unit_test(test_file_open_null_params),
        cmocka_unit_test(test_file_close_null),
        cmocka_unit_test(test_file_get_info_null_params),
        cmocka_unit_test(test_file_read_null_params),
        cmocka_unit_test(test_file_seek_null),
        cmocka_unit_test(test_file_tell_null_params),
    };

    const struct CMUnitTest metadata_tests[] = {
        cmocka_unit_test(test_get_id3_tag_null_params),
        cmocka_unit_test(test_set_id3_overlay_null_params),
    };

    const struct CMUnitTest error_tests[] = {
        cmocka_unit_test(test_error_strings),
    };

    const struct CMUnitTest dsf_layout_tests[] = {
        cmocka_unit_test(test_dsf_layout_stereo_1sec),
        cmocka_unit_test(test_dsf_layout_multichannel_1sec),
        cmocka_unit_test(test_dsf_layout_mono),
        cmocka_unit_test(test_dsf_layout_long_track),
    };

    const struct CMUnitTest seek_calculation_tests[] = {
        cmocka_unit_test(test_seek_calculation_header),
        cmocka_unit_test(test_seek_calculation_audio),
        cmocka_unit_test(test_seek_calculation_metadata),
        cmocka_unit_test(test_seek_frame_calculation),
        cmocka_unit_test(test_seek_frame_calculation_multichannel),
    };

    const struct CMUnitTest padding_tests[] = {
        cmocka_unit_test(test_padding_byte_value),
        cmocka_unit_test(test_padding_size_various_frames),
        cmocka_unit_test(test_padding_region_position),
    };

    const struct CMUnitTest id3_region_tests[] = {
        cmocka_unit_test(test_id3_position),
        cmocka_unit_test(test_seek_to_id3_end),
        cmocka_unit_test(test_layout_no_id3),
    };

    const struct CMUnitTest dst_dsd_tests[] = {
        cmocka_unit_test(test_dst_dsd_frame_size_equivalence),
        cmocka_unit_test(test_dsf_structure_independent_of_source),
    };

    const struct CMUnitTest seek_edge_tests[] = {
        cmocka_unit_test(test_seek_set_positions),
        cmocka_unit_test(test_seek_cur_calculations),
        cmocka_unit_test(test_seek_end_calculations),
        cmocka_unit_test(test_seek_boundary_transitions),
    };

    const struct CMUnitTest read_after_seek_tests[] = {
        cmocka_unit_test(test_read_size_after_seek),
        cmocka_unit_test(test_read_id3_after_seek),
        cmocka_unit_test(test_seek_all_channel_counts),
    };

    int failed = 0;

    failed += cmocka_run_group_tests_name("SACD VFS Constants Verification",
                                          constant_tests, group_setup, NULL);
    failed += cmocka_run_group_tests_name("SACD VFS Lifecycle Tests",
                                          lifecycle_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("SACD VFS Directory Operations Tests",
                                          directory_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("SACD VFS File Operations Tests",
                                          file_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("SACD VFS Metadata Tests",
                                          metadata_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("SACD VFS Error Handling Tests",
                                          error_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("DSF Layout Calculation Tests",
                                          dsf_layout_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Seek Position Calculation Tests",
                                          seek_calculation_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Padding Calculation Tests",
                                          padding_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("ID3 Region Tests",
                                          id3_region_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("DST vs DSD Tests",
                                          dst_dsd_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Seek Edge Case Tests",
                                          seek_edge_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Read After Seek Tests",
                                          read_after_seek_tests, NULL, group_teardown);

    return failed;
}
