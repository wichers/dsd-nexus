/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Comprehensive unit tests for DSF file operations using CMocka
 * This test suite covers all aspects of the DSF file API including:
 * - File lifecycle (create, open, close, finalize)
 * - Audio data I/O operations
 * - Metadata (ID3v2) operations
 * - File properties and validation
 * - Error handling
 * Run with Valgrind for memory leak detection:
 *   valgrind --leak-check=full --show-leak-kinds=all ./test_dsf_file
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

#include "dsf_types.h"

#include <libsautil/mem.h>
#include <libsautil/compat.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

/* Test file paths */
#define TEST_FILE_STEREO        "test_dsf_stereo.dsf"
#define TEST_FILE_MONO          "test_dsf_mono.dsf"
#define TEST_FILE_MULTICHANNEL  "test_dsf_multichannel.dsf"
#define TEST_FILE_WITH_METADATA "test_dsf_metadata.dsf"
#define TEST_FILE_MODIFY        "test_dsf_modify.dsf"
#define TEST_FILE_AUDIO         "test_dsf_audio.dsf"
#define TEST_FILE_NO_METADATA   "test_dsf_no_metadata.dsf"
#define TEST_FILE_VALIDATE      "test_dsf_validate.dsf"
#define TEST_FILE_READ_MODE     "test_dsf_read_mode.dsf"
#define TEST_FILE_REMOVE        "test_dsf_remove.dsf"

/* =============================================================================
 * Setup and Teardown Functions
 * ===========================================================================*/

/**
 * @brief Group setup - runs once before all tests in a group
 */
static int group_setup(void **state)
{
    (void)state;
    return 0;
}

/**
 * @brief Group teardown - runs once after all tests in a group
 * Cleans up any test files that may have been left behind
 */
static int group_teardown(void **state)
{
    (void)state;

    /* Clean up test files */
    remove(TEST_FILE_STEREO);
    remove(TEST_FILE_MONO);
    remove(TEST_FILE_MULTICHANNEL);
    remove(TEST_FILE_WITH_METADATA);
    remove(TEST_FILE_MODIFY);
    remove(TEST_FILE_AUDIO);
    remove(TEST_FILE_NO_METADATA);
    remove(TEST_FILE_VALIDATE);
    remove(TEST_FILE_READ_MODE);
    remove(TEST_FILE_REMOVE);
    remove("test_invalid.dsf");

    return 0;
}

/* =============================================================================
 * Test: File Allocation and Deallocation
 * ===========================================================================*/

static void test_alloc_and_free(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    int ret;

    /* Test allocation */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_non_null(file);

    /* Test sa_free */
    ret = dsf_free(file);
    assert_int_equal(ret, DSF_SUCCESS);
}

static void test_alloc_null_pointer(void **state)
{
    (void)state;
    int ret;

    /* Test allocation with NULL pointer */
    ret = dsf_alloc(NULL);
    assert_int_equal(ret, DSF_ERROR_INVALID_PARAMETER);
}

static void test_free_null_pointer(void **state)
{
    (void)state;
    int ret;

    /* Test sa_free with NULL pointer */
    ret = dsf_free(NULL);
    assert_int_equal(ret, DSF_ERROR_INVALID_PARAMETER);
}

/* =============================================================================
 * Test: File Creation (Write Mode)
 * ===========================================================================*/

static void test_create_stereo_dsd64(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    int ret;

    /* Allocate */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Create stereo DSD64 file */
    ret = dsf_create(file, TEST_FILE_STEREO,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2,  /* channel count */
                     DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Finalize and close */
    ret = dsf_finalize(file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_close(file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_free(file);
    assert_int_equal(ret, DSF_SUCCESS);
}

static void test_create_mono_dsd128(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    int ret;

    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Create mono DSD128 file */
    ret = dsf_create(file, TEST_FILE_MONO,
                     DSF_SAMPLE_FREQ_128FS,
                     DSF_CHANNEL_TYPE_MONO,
                     1,  /* channel count */
                     DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_finalize(file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_close(file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_free(file);
    assert_int_equal(ret, DSF_SUCCESS);
}

static void test_create_multichannel_dsd256(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    int ret;

    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Create 5.1 channel DSD256 file */
    ret = dsf_create(file, TEST_FILE_MULTICHANNEL,
                     DSF_SAMPLE_FREQ_256FS,
                     DSF_CHANNEL_TYPE_5_1_CHANNELS,
                     6,  /* channel count (5.1) */
                     DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_finalize(file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_close(file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_free(file);
    assert_int_equal(ret, DSF_SUCCESS);
}

static void test_create_invalid_parameters(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    int ret;

    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Test invalid sample rate */
    ret = dsf_create(file, "test_invalid.dsf",
                     44100,  /* Invalid - should be DSD rate */
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_not_equal(ret, DSF_SUCCESS);

    /* Test invalid channel count */
    ret = dsf_create(file, "test_invalid.dsf",
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     0,  /* Invalid - no channels */
                     DSF_BITS_PER_SAMPLE_1);
    assert_int_not_equal(ret, DSF_SUCCESS);

    /* Test invalid bits per sample */
    ret = dsf_create(file, "test_invalid.dsf",
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, 16);  /* Invalid - should be 1 or 8 */
    assert_int_not_equal(ret, DSF_SUCCESS);

    ret = dsf_free(file);
    assert_int_equal(ret, DSF_SUCCESS);

    remove("test_invalid.dsf");
}

/* =============================================================================
 * Test: File Opening (Read Mode)
 * ===========================================================================*/

static void test_open_for_read(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    dsf_file_mode_t mode;
    int ret;

    /* First create a test file */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_STEREO,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    dsf_finalize(file);
    dsf_close(file);
    dsf_free(file);

    /* Now open for reading */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_open(file, TEST_FILE_STEREO);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Verify mode */
    ret = dsf_get_file_mode(file, &mode);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(mode, DSF_FILE_MODE_READ);

    ret = dsf_close(file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_free(file);
    assert_int_equal(ret, DSF_SUCCESS);
}

static void test_open_nonexistent_file(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    int ret;

    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_open(file, "nonexistent_file.dsf");
    assert_int_not_equal(ret, DSF_SUCCESS);

    ret = dsf_free(file);
    assert_int_equal(ret, DSF_SUCCESS);
}

/* =============================================================================
 * Test: File Opening (Modify Mode)
 * ===========================================================================*/

static void test_open_for_modify(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    dsf_file_mode_t mode;
    int ret;

    /* First create a test file */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_MODIFY,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    dsf_finalize(file);
    dsf_close(file);
    dsf_free(file);

    /* Now open for modification */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_open_modify(file, TEST_FILE_MODIFY);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Verify mode */
    ret = dsf_get_file_mode(file, &mode);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(mode, DSF_FILE_MODE_MODIFY);

    ret = dsf_finalize(file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_close(file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_free(file);
    assert_int_equal(ret, DSF_SUCCESS);
}

/* =============================================================================
 * Test: File Properties
 * ===========================================================================*/

static void test_get_file_properties(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    dsf_file_info_t info;
    uint32_t sample_rate, channel_count, channel_type, bits_per_sample;
    uint64_t sample_count, file_size, audio_size;
    double duration;
    char filename[256];
    int ret;

    /* Create test file */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_STEREO,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Get file info structure */
    ret = dsf_get_file_info(file, &info);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(info.sampling_frequency, DSF_SAMPLE_FREQ_64FS);
    assert_int_equal(info.channel_count, 2);
    assert_int_equal(info.channel_type, DSF_CHANNEL_TYPE_STEREO);
    assert_int_equal(info.bits_per_sample, DSF_BITS_PER_SAMPLE_1);

    /* Get individual properties */
    ret = dsf_get_sample_rate(file, &sample_rate);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(sample_rate, DSF_SAMPLE_FREQ_64FS);

    ret = dsf_get_channel_count(file, &channel_count);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(channel_count, 2);

    ret = dsf_get_channel_type(file, &channel_type);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(channel_type, DSF_CHANNEL_TYPE_STEREO);

    ret = dsf_get_bits_per_sample(file, &bits_per_sample);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(bits_per_sample, DSF_BITS_PER_SAMPLE_1);

    ret = dsf_get_sample_count(file, &sample_count);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_get_audio_data_size(file, &audio_size);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_get_duration(file, &duration);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_get_filename(file, filename, sizeof(filename));
    assert_int_equal(ret, DSF_SUCCESS);

    /* Finalize to update file size */
    ret = dsf_finalize(file);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Now get file size after finalize */
    ret = dsf_get_file_size(file, &file_size);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_true(file_size > 0);

    dsf_close(file);
    dsf_free(file);
}

/* =============================================================================
 * Test: Audio Data I/O
 * ===========================================================================*/

static void test_audio_write_and_read(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    uint8_t write_buffer[8192];
    uint8_t read_buffer[8192];
    size_t bytes_written, bytes_read;
    int ret;

    /* Initialize write buffer with test pattern */
    for (size_t i = 0; i < sizeof(write_buffer); i++) {
        write_buffer[i] = (uint8_t)(i & 0xFF);
    }

    /* Create file and write audio data */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_AUDIO,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Write audio data */
    ret = dsf_write_audio_data(file, write_buffer, sizeof(write_buffer), &bytes_written);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(bytes_written, sizeof(write_buffer));

    dsf_finalize(file);
    dsf_close(file);
    dsf_free(file);

    /* Open file and read audio data back */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_open(file, TEST_FILE_AUDIO);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Read audio data */
    memset(read_buffer, 0, sizeof(read_buffer));
    ret = dsf_read_audio_data(file, read_buffer, sizeof(read_buffer), &bytes_read);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(bytes_read, sizeof(read_buffer));

    /* Verify data matches */
    assert_memory_equal(write_buffer, read_buffer, sizeof(write_buffer));

    dsf_close(file);
    dsf_free(file);
}

static void test_audio_seek(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    uint8_t write_buffer[16384];
    uint8_t read_buffer[1024];
    size_t bytes_written, bytes_read;
    uint64_t position;
    int ret;

    /* Initialize write buffer */
    for (size_t i = 0; i < sizeof(write_buffer); i++) {
        write_buffer[i] = (uint8_t)(i & 0xFF);
    }

    /* Create file and write audio data */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_AUDIO,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_write_audio_data(file, write_buffer, sizeof(write_buffer), &bytes_written);
    assert_int_equal(ret, DSF_SUCCESS);

    dsf_finalize(file);
    dsf_close(file);
    dsf_free(file);

    /* Open file and test seeking */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_open(file, TEST_FILE_AUDIO);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Seek to offset 4096 from start */
    ret = dsf_seek_audio_data(file, 4096, DSF_SEEK_SET);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_get_audio_position(file, &position);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(position, 4096);

    /* Read data from this position */
    ret = dsf_read_audio_data(file, read_buffer, sizeof(read_buffer), &bytes_read);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(bytes_read, sizeof(read_buffer));

    /* Verify data matches expected offset in write buffer */
    assert_memory_equal(&write_buffer[4096], read_buffer, sizeof(read_buffer));

    /* Seek to start */
    ret = dsf_seek_to_audio_start(file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_get_audio_position(file, &position);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(position, 0);

    dsf_close(file);
    dsf_free(file);
}

/* =============================================================================
 * Test: Metadata Operations (ID3v2)
 * ===========================================================================*/

static void test_metadata_write_and_read(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    uint8_t metadata_in[256];
    uint8_t *metadata_out = NULL;
    uint64_t metadata_size_out;
    int has_metadata;
    int ret;

    /* Create test metadata (simple ID3v2 header) */
    memset(metadata_in, 0, sizeof(metadata_in));
    memcpy(metadata_in, "ID3", 3);  /* ID3v2 tag header */
    metadata_in[3] = 4;  /* Version 2.4 */
    metadata_in[4] = 0;  /* Revision 0 */
    metadata_in[5] = 0;  /* Flags */
    /* Size is stored in synchsafe integer format (7 bits per byte) */
    metadata_in[6] = 0;
    metadata_in[7] = 0;
    metadata_in[8] = 1;
    metadata_in[9] = 0;  /* Size = 128 bytes */

    /* Create file with metadata */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_WITH_METADATA,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Write metadata */
    ret = dsf_write_metadata(file, metadata_in, sizeof(metadata_in));
    assert_int_equal(ret, DSF_SUCCESS);

    dsf_finalize(file);
    dsf_close(file);
    dsf_free(file);

    /* Open file and read metadata */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_open(file, TEST_FILE_WITH_METADATA);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Check if metadata exists */
    ret = dsf_has_metadata(file, &has_metadata);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_true(has_metadata != 0);

    /* Get metadata size */
    ret = dsf_get_metadata_size(file, &metadata_size_out);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(metadata_size_out, sizeof(metadata_in));

    /* Read metadata */
    ret = dsf_read_metadata(file, &metadata_out, &metadata_size_out);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_non_null(metadata_out);
    assert_int_equal(metadata_size_out, sizeof(metadata_in));

    /* Verify metadata content */
    assert_memory_equal(metadata_in, metadata_out, sizeof(metadata_in));

    /* Free metadata buffer */
    sa_free(metadata_out);

    dsf_close(file);
    dsf_free(file);
}

static void test_metadata_modify(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    uint8_t metadata_v1[128];
    uint8_t metadata_v2[256];
    uint8_t *metadata_out = NULL;
    uint64_t metadata_size_out;
    int ret;

    /* Create initial metadata */
    memset(metadata_v1, 0xAA, sizeof(metadata_v1));
    memcpy(metadata_v1, "ID3", 3);

    /* Create file with initial metadata */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_WITH_METADATA,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_write_metadata(file, metadata_v1, sizeof(metadata_v1));
    assert_int_equal(ret, DSF_SUCCESS);

    dsf_finalize(file);
    dsf_close(file);
    dsf_free(file);

    /* Open for modification and update metadata */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_open_modify(file, TEST_FILE_WITH_METADATA);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Write new metadata */
    memset(metadata_v2, 0xBB, sizeof(metadata_v2));
    memcpy(metadata_v2, "ID3", 3);

    ret = dsf_write_metadata(file, metadata_v2, sizeof(metadata_v2));
    assert_int_equal(ret, DSF_SUCCESS);

    dsf_finalize(file);
    dsf_close(file);
    dsf_free(file);

    /* Read back and verify updated metadata */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_open(file, TEST_FILE_WITH_METADATA);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_read_metadata(file, &metadata_out, &metadata_size_out);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(metadata_size_out, sizeof(metadata_v2));

    /* Verify updated metadata content */
    assert_memory_equal(metadata_v2, metadata_out, sizeof(metadata_v2));

    sa_free(metadata_out);

    dsf_close(file);
    dsf_free(file);
}

static void test_file_without_metadata(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    int has_metadata;
    uint64_t metadata_size;
    int ret;

    /* Create file without metadata */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_NO_METADATA,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    dsf_finalize(file);
    dsf_close(file);
    dsf_free(file);

    /* Open and verify no metadata */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_open(file, TEST_FILE_NO_METADATA);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_has_metadata(file, &has_metadata);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(has_metadata, 0);

    ret = dsf_get_metadata_size(file, &metadata_size);
    assert_int_equal(ret, DSF_SUCCESS);
    assert_int_equal(metadata_size, 0);

    dsf_close(file);
    dsf_free(file);
}

/* =============================================================================
 * Test: File Validation
 * ===========================================================================*/

static void test_validate_valid_file(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    int ret;

    /* Create valid file */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_VALIDATE,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    dsf_finalize(file);
    dsf_close(file);
    dsf_free(file);

    /* Reopen for reading and validate */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_open(file, TEST_FILE_VALIDATE);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Validate */
    ret = dsf_validate(file);
    assert_int_equal(ret, DSF_SUCCESS);

    dsf_close(file);
    dsf_free(file);
}

/* =============================================================================
 * Test: Error Handling
 * ===========================================================================*/

static void test_error_strings(void **state)
{
    (void)state;
    const char *error_str;

    /* Test error string conversion */
    error_str = dsf_error_string(DSF_SUCCESS);
    assert_non_null(error_str);

    error_str = dsf_error_string(DSF_ERROR_INVALID_PARAMETER);
    assert_non_null(error_str);

    error_str = dsf_error_string(DSF_ERROR_READ);
    assert_non_null(error_str);

    error_str = dsf_error_string(DSF_ERROR_WRITE);
    assert_non_null(error_str);

    error_str = dsf_error_string(DSF_ERROR_INVALID_FILE);
    assert_non_null(error_str);
}

static void test_operations_on_closed_file(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    uint8_t buffer[1024];
    size_t bytes;
    int ret;

    /* Allocate but don't open */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Try operations on closed file */
    ret = dsf_read_audio_data(file, buffer, sizeof(buffer), &bytes);
    assert_int_not_equal(ret, DSF_SUCCESS);

    ret = dsf_write_audio_data(file, buffer, sizeof(buffer), &bytes);
    assert_int_not_equal(ret, DSF_SUCCESS);

    ret = dsf_free(file);
    assert_int_equal(ret, DSF_SUCCESS);
}

static void test_write_in_read_mode(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    uint8_t buffer[1024];
    size_t bytes_written;
    int ret;

    /* Create and close file */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_READ_MODE,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    dsf_finalize(file);
    dsf_close(file);
    dsf_free(file);

    /* Open in read mode */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_open(file, TEST_FILE_READ_MODE);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Try to write */
    memset(buffer, 0, sizeof(buffer));
    ret = dsf_write_audio_data(file, buffer, sizeof(buffer), &bytes_written);
    assert_int_not_equal(ret, DSF_SUCCESS);

    dsf_close(file);
    dsf_free(file);
}

/* =============================================================================
 * Test: File Removal
 * ===========================================================================*/

static void test_remove_file(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    int ret;
    FILE *check;

    /* Create file */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_REMOVE,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Remove file */
    ret = dsf_remove_file(file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_free(file);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Verify file doesn't exist */
    check = sa_fopen(TEST_FILE_REMOVE, "r");
    assert_null(check);
}

/* =============================================================================
 * Test: Large File Operations
 * ===========================================================================*/

static void test_large_audio_write(void **state)
{
    (void)state;
    dsf_t *file = NULL;
    uint8_t *buffer;
    size_t buffer_size = 1024 * 1024;  /* 1 MB */
    size_t bytes_written;
    int ret;

    /* Allocate large buffer */
    buffer = (uint8_t *)sa_malloc(buffer_size);
    assert_non_null(buffer);

    /* Initialize buffer */
    for (size_t i = 0; i < buffer_size; i++) {
        buffer[i] = (uint8_t)(i & 0xFF);
    }

    /* Create file and write large data */
    ret = dsf_alloc(&file);
    assert_int_equal(ret, DSF_SUCCESS);

    ret = dsf_create(file, TEST_FILE_AUDIO,
                     DSF_SAMPLE_FREQ_64FS,
                     DSF_CHANNEL_TYPE_STEREO,
                     2, DSF_BITS_PER_SAMPLE_1);
    assert_int_equal(ret, DSF_SUCCESS);

    /* Write multiple times */
    for (int i = 0; i < 10; i++) {
        ret = dsf_write_audio_data(file, buffer, buffer_size, &bytes_written);
        assert_int_equal(ret, DSF_SUCCESS);
        assert_int_equal(bytes_written, buffer_size);
    }

    dsf_finalize(file);
    dsf_close(file);
    dsf_free(file);

    sa_free(buffer);
}

/* =============================================================================
 * Main Test Runner
 * ===========================================================================*/

int main(void)
{
    const struct CMUnitTest allocation_tests[] = {
        cmocka_unit_test(test_alloc_and_free),
        cmocka_unit_test(test_alloc_null_pointer),
        cmocka_unit_test(test_free_null_pointer),
    };

    const struct CMUnitTest creation_tests[] = {
        cmocka_unit_test(test_create_stereo_dsd64),
        cmocka_unit_test(test_create_mono_dsd128),
        cmocka_unit_test(test_create_multichannel_dsd256),
        cmocka_unit_test(test_create_invalid_parameters),
    };

    const struct CMUnitTest opening_tests[] = {
        cmocka_unit_test(test_open_for_read),
        cmocka_unit_test(test_open_nonexistent_file),
        cmocka_unit_test(test_open_for_modify),
    };

    const struct CMUnitTest properties_tests[] = {
        cmocka_unit_test(test_get_file_properties),
    };

    const struct CMUnitTest audio_io_tests[] = {
        cmocka_unit_test(test_audio_write_and_read),
        cmocka_unit_test(test_audio_seek),
    };

    const struct CMUnitTest metadata_tests[] = {
        cmocka_unit_test(test_metadata_write_and_read),
        cmocka_unit_test(test_metadata_modify),
        cmocka_unit_test(test_file_without_metadata),
    };

    const struct CMUnitTest validation_tests[] = {
        cmocka_unit_test(test_validate_valid_file),
    };

    const struct CMUnitTest error_handling_tests[] = {
        cmocka_unit_test(test_error_strings),
        cmocka_unit_test(test_operations_on_closed_file),
        cmocka_unit_test(test_write_in_read_mode),
    };

    const struct CMUnitTest file_removal_tests[] = {
        cmocka_unit_test(test_remove_file),
    };

    const struct CMUnitTest large_file_tests[] = {
        cmocka_unit_test(test_large_audio_write),
    };

    int failed = 0;

    failed += cmocka_run_group_tests_name("Allocation/Deallocation Tests",
                                          allocation_tests, group_setup, NULL);
    failed += cmocka_run_group_tests_name("File Creation Tests",
                                          creation_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("File Opening Tests",
                                          opening_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("File Properties Tests",
                                          properties_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Audio I/O Tests",
                                          audio_io_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Metadata Tests",
                                          metadata_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Validation Tests",
                                          validation_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Error Handling Tests",
                                          error_handling_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("File Removal Tests",
                                          file_removal_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Large File Tests",
                                          large_file_tests, NULL, group_teardown);

    return failed;
}
