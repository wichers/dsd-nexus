/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Unit tests for DSDIFF file reading operations using CMocka
 * Converted from: DSDIFF13ReadTest.cpp
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

#include "dsdiff_types.h"
#include "dsdiff_markers.h"

#include <libsautil/mem.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* =============================================================================
 * Helper: Create test file for reading
 * ===========================================================================*/

static int create_test_dsd_file(const char *filename)
{
    dsdiff_t *file = NULL;
    unsigned char *data;
    uint32_t frames_written;
    int ret;

    ret = dsdiff_new(&file);
    if (ret != DSDIFF_SUCCESS) return ret;

    ret = dsdiff_create(file, filename, DSDIFF_AUDIO_DSD,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    if (ret != DSDIFF_SUCCESS) {
        dsdiff_close(file);
        return ret;
    }

    /* Write some test data (digital silence pattern: 0x55 = 01010101) */
    data = (unsigned char*)sa_malloc(2 * 1024);
    memset(data, 0x55, 2 * 1024);
    dsdiff_write_dsd_data(file, data, 1024, &frames_written);
    sa_free(data);

    /* Add a marker */
    dsdiff_marker_t marker;
    memset(&marker, 0, sizeof(marker));
    marker.time.hours = 0;
    marker.time.minutes = 0;
    marker.time.seconds = 10;
    marker.time.samples = 0;
    marker.mark_channel = DSDIFF_MARK_CHANNEL_ALL;
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    marker.track_flags = DSDIFF_TRACK_FLAG_NONE;
    marker.marker_text = NULL;
    dsdiff_add_dsd_marker(file, &marker);

    dsdiff_finalize(file);
    dsdiff_close(file);

    return DSDIFF_SUCCESS;
}

static int create_test_dst_file(const char *filename)
{
    dsdiff_t *file = NULL;
    unsigned char *data;
    int ret;

    ret = dsdiff_new(&file);
    if (ret != DSDIFF_SUCCESS) return ret;

    ret = dsdiff_create(file, filename, DSDIFF_AUDIO_DST,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    if (ret != DSDIFF_SUCCESS) {
        dsdiff_close(file);
        return ret;
    }

    /* Write some DST frames */
    data = (unsigned char*)sa_malloc(1024);
    memset(data, 0x66, 1024);
    dsdiff_write_dst_frame(file, data, 1024);
    memset(data, 0x77, 512);
    dsdiff_write_dst_frame(file, data, 512);
    sa_free(data);

    dsdiff_finalize(file);
    dsdiff_close(file);

    return DSDIFF_SUCCESS;
}

/* =============================================================================
 * Setup and Teardown
 * ===========================================================================*/

static int group_setup(void **state)
{
    (void)state;
    return 0;
}

static int group_teardown(void **state)
{
    (void)state;
    remove("test_read_dsd.dff");
    remove("test_props.dff");
    remove("test_chanids.dff");
    remove("test_read_data.dff");
    remove("test_read_twice.dff");
    remove("test_read_dst.dff");
    remove("test_dst_indexed.dff");
    remove("test_read_markers.dff");
    remove("test_filename.dff");
    return 0;
}

/* =============================================================================
 * Test: File Opening and Basic Properties
 * ===========================================================================*/

static void test_read_open_file(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_file_mode_t mode;
    int ret;

    ret = create_test_dsd_file("test_read_dsd.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_read_dsd.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_open_mode(file, &mode);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(mode, DSDIFF_FILE_MODE_READ);

    dsdiff_close(file);
}

static void test_read_file_properties(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    uint16_t channel_count;
    uint32_t sample_rate;
    uint64_t sound_data_size, num_sample_frames;
    uint16_t sample_bits;
    dsdiff_audio_type_t file_type;
    int ret;

    ret = create_test_dsd_file("test_props.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_props.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_channel_count(file, &channel_count);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(channel_count, 2);

    ret = dsdiff_get_sample_rate(file, &sample_rate);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(sample_rate, DSDIFF_SAMPLE_FREQ_64FS);

    ret = dsdiff_get_sample_bits(file, &sample_bits);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(sample_bits, 1);

    ret = dsdiff_get_audio_type(file, &file_type);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(file_type, DSDIFF_AUDIO_DSD);

    ret = dsdiff_get_dsd_data_size(file, &sound_data_size);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(sound_data_size != 0);

    ret = dsdiff_get_sample_frame_count(file, &num_sample_frames);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(num_sample_frames != 0);

    dsdiff_close(file);
}

static void test_read_channel_ids(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_channel_id_t chan_ids[2];
    int ret;

    ret = create_test_dsd_file("test_chanids.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_chanids.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_channel_ids(file, chan_ids);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(chan_ids[0], DSDIFF_CHAN_SLFT);
    assert_int_equal(chan_ids[1], DSDIFF_CHAN_SRGT);

    dsdiff_close(file);
}

/* =============================================================================
 * Test: Reading Sound Data (DSD)
 * ===========================================================================*/

static void test_read_dsd_data(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    unsigned char data[20];
    uint32_t frames_read;
    int ret;

    ret = create_test_dsd_file("test_read_data.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_read_data.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_read_dsd_data(file, data, 10, &frames_read);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(frames_read != 0);

    /* Verify data (should be 0x55 pattern) */
    for (uint32_t i = 0; i < frames_read * 2; i++) {
        assert_int_equal(data[i], 0x55);
    }

    dsdiff_close(file);
}

static void test_read_dsd_data_twice(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    unsigned char data1[20], data2[20];
    uint32_t frames_read;
    int ret;

    ret = create_test_dsd_file("test_read_twice.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_read_twice.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_read_dsd_data(file, data1, 10, &frames_read);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_seek_dsd_start(file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_read_dsd_data(file, data2, 10, &frames_read);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    assert_memory_equal(data1, data2, 20);

    dsdiff_close(file);
}

/* =============================================================================
 * Test: Reading DST Data
 * ===========================================================================*/

static void test_read_dst_frame(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    unsigned char data[2048];
    uint32_t frame_size, nr_frames;
    int ret;

    ret = create_test_dst_file("test_read_dst.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_read_dst.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_dst_frame_count(file, &nr_frames);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(nr_frames, 2);

    ret = dsdiff_read_dst_frame(file, data, 2048, &frame_size);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(frame_size, 1024);

    for (uint32_t i = 0; i < frame_size; i++) {
        assert_int_equal(data[i], 0x66);
    }

    dsdiff_close(file);
}

static void test_read_dst_frame_indexed(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    unsigned char data[2048];
    uint32_t frame_size;
    int ret, is_indexed;

    ret = create_test_dst_file("test_dst_indexed.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_dst_indexed.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_has_dst_index(file, &is_indexed);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    if (is_indexed) {
        ret = dsdiff_read_dst_frame_at_index(file, 1, data, 2048, &frame_size);
        assert_int_equal(ret, DSDIFF_SUCCESS);
        assert_int_equal(frame_size, 512);

        for (uint32_t i = 0; i < frame_size; i++) {
            assert_int_equal(data[i], 0x77);
        }
    }

    dsdiff_close(file);
}

/* =============================================================================
 * Test: Reading Markers and Metadata
 * ===========================================================================*/

static void test_read_markers(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_marker_t marker;
    int ret, nr_markers;

    ret = create_test_dsd_file("test_read_markers.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_read_markers.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_dsd_marker_count(file, &nr_markers);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(nr_markers, 1);

    memset(&marker, 0, sizeof(marker));
    ret = dsdiff_get_dsd_marker(file, 0, &marker);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(marker.time.seconds, 10);
    assert_int_equal(marker.mark_type, DSDIFF_MARK_TRACK_START);

    dsdiff_close(file);
}

static void test_read_filename(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    char filename[256];
    int ret;

    ret = create_test_dsd_file("test_filename.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_filename.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_filename(file, filename, 256);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(strlen(filename) != 0);

    dsdiff_close(file);
}

/* =============================================================================
 * Main Test Runner
 * ===========================================================================*/

int main(void)
{
    const struct CMUnitTest file_opening_tests[] = {
        cmocka_unit_test(test_read_open_file),
        cmocka_unit_test(test_read_file_properties),
        cmocka_unit_test(test_read_channel_ids),
    };

    const struct CMUnitTest dsd_reading_tests[] = {
        cmocka_unit_test(test_read_dsd_data),
        cmocka_unit_test(test_read_dsd_data_twice),
    };

    const struct CMUnitTest dst_reading_tests[] = {
        cmocka_unit_test(test_read_dst_frame),
        cmocka_unit_test(test_read_dst_frame_indexed),
    };

    const struct CMUnitTest metadata_reading_tests[] = {
        cmocka_unit_test(test_read_markers),
        cmocka_unit_test(test_read_filename),
    };

    int failed = 0;

    failed += cmocka_run_group_tests_name("File Opening Tests",
                                          file_opening_tests, group_setup, NULL);
    failed += cmocka_run_group_tests_name("DSD Reading Tests",
                                          dsd_reading_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("DST Reading Tests",
                                          dst_reading_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Metadata Reading Tests",
                                          metadata_reading_tests, NULL, group_teardown);

    return failed;
}
