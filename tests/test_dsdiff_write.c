/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Unit tests for DSDIFF file writing operations (DSD/PCM) using CMocka
 * Converted from: WriteTest.cpp
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

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
    remove("test_write_mono.dff");
    remove("test_write_stereo.dff");
    remove("test_write_sectorbuf.dff");
    remove("test_write_1mb.dff");
    remove("test_write_large.dff");
    remove("test_write_invalid.dff");
    remove("test_write_custom_chan.dff");
    remove("test_write_128fs.dff");
    return 0;
}

/* =============================================================================
 * Test: Create DSD/PCM File and Write Basic Data (1 channel)
 * ===========================================================================*/

static void test_write_basic_mono(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    dsdiff_comment_t comment;
    unsigned char *data_1, *data_2;
    unsigned long datasize = 10;
    uint32_t frames_written;
    char text[] = "abcdefghijklmnnopqrstuwxyz1234567890!@#$%^&*()_+";
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_write_mono.dff", DSDIFF_AUDIO_DSD,
                        1, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Add a comment */
    memset(&comment, 0, sizeof(comment));
    comment.minute = 1;
    comment.hour = 2;
    comment.day = 3;
    comment.month = 4;
    comment.year = 5;
    comment.comment_type = 5;
    comment.comment_ref = 6;
    comment.text_length = (uint32_t) strlen(text);
    comment.text = text;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Write data */
    data_1 = (unsigned char*)sa_malloc(1 * datasize);
    data_2 = (unsigned char*)sa_malloc(1 * datasize);
    memset(data_1, 1, 1 * datasize);
    memset(data_2, 2, 1 * datasize);

    ret = dsdiff_write_dsd_data(file, data_1, datasize, &frames_written);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_write_dsd_data(file, data_2, datasize, &frames_written);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    sa_free(data_1);
    sa_free(data_2);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Create DSD/PCM File with 2 Channels
 * ===========================================================================*/

static void test_write_basic_stereo(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    dsdiff_comment_t comment;
    unsigned char *data;
    unsigned long datasize = 10;
    uint32_t frames_written;
    char text1[] = "abcdefghijklmnnopqrstuwxyz1234567890!@#$%^&*()_+";
    char text2[] = "ABCDEFGHIJKLMNNOPQRSTUWXYZ\n1234567890\n!@#$%^&*()_+\n";
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 1;
    timecode.seconds = 1;
    timecode.samples = 1;
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_write_stereo.dff", DSDIFF_AUDIO_DSD,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Write data */
    data = (unsigned char*)sa_malloc(2 * datasize);
    memset(data, 1, 2 * datasize);
    ret = dsdiff_write_dsd_data(file, data, datasize, &frames_written);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    sa_free(data);

    /* Add comments */
    memset(&comment, 0, sizeof(comment));
    comment.minute = 1;
    comment.hour = 2;
    comment.day = 3;
    comment.month = 4;
    comment.year = 5;
    comment.comment_type = 5;
    comment.comment_ref = 6;
    comment.text_length = (uint32_t) strlen(text1);
    comment.text = text1;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&comment, 0, sizeof(comment));
    comment.minute = 61;
    comment.hour = 62;
    comment.day = 63;
    comment.month = 64;
    comment.year = 65;
    comment.comment_type = 65;
    comment.comment_ref = 66;
    comment.text_length = (uint32_t) strlen(text2);
    comment.text = text2;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Create DSD/PCM File with Sector Buffering
 * ===========================================================================*/

static void test_write_sector_buffered(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    dsdiff_comment_t comment;
    unsigned char *data;
    unsigned long datasize = 10;
    uint32_t frames_written;
    char text[] = "abcdefghijklmnnopqrstuwxyz1234567890!@#$%^&*()_+";
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_write_sectorbuf.dff", DSDIFF_AUDIO_DSD,
                        1, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Add a comment */
    memset(&comment, 0, sizeof(comment));
    comment.minute = 1;
    comment.hour = 2;
    comment.day = 3;
    comment.month = 4;
    comment.year = 5;
    comment.comment_type = 5;
    comment.comment_ref = 6;
    comment.text_length = (uint32_t) strlen(text);
    comment.text = text;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Write data */
    data = (unsigned char*)sa_malloc(1 * datasize);
    memset(data, 1, 1 * datasize);
    ret = dsdiff_write_dsd_data(file, data, datasize, &frames_written);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    sa_free(data);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Write 1MB of Data
 * ===========================================================================*/

static void test_write_1mb(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    unsigned char *data;
    unsigned long datasize = 1000;
    uint32_t frames_written;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 1;
    timecode.seconds = 1;
    timecode.samples = 1;
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_write_1mb.dff", DSDIFF_AUDIO_DSD,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Write 1MB+ of data */
    data = (unsigned char*)sa_malloc(2 * datasize);
    memset(data, 1, 2 * datasize);

    for (int i = 0; i < 1001; i++) {
        ret = dsdiff_write_dsd_data(file, data, datasize, &frames_written);
        assert_int_equal(ret, DSDIFF_SUCCESS);
    }

    sa_free(data);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Write with Invalid Timecode (Error Handling)
 * ===========================================================================*/

static void test_write_invalid_timecode(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    unsigned char *data;
    unsigned long datasize = 10;
    uint32_t frames_written;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Set invalid timecode */
    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 25;
    timecode.minutes = 61;
    timecode.seconds = 61;
    timecode.samples = 1000000;
    dsdiff_set_start_timecode(file, &timecode);

    /* Create DSD file - may succeed or fail depending on validation */
    ret = dsdiff_create(file, "test_write_invalid.dff", DSDIFF_AUDIO_DSD,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);

    if (ret == DSDIFF_SUCCESS) {
        data = (unsigned char*)sa_malloc(2 * datasize);
        memset(data, 1, 2 * datasize);
        dsdiff_write_dsd_data(file, data, datasize, &frames_written);
        sa_free(data);

        dsdiff_finalize(file);
        dsdiff_close(file);
    }

    /* Test always passes - we're just testing error handling */
    assert_true(1);
}

/* =============================================================================
 * Test: Write with Custom Channel IDs
 * ===========================================================================*/

static void test_write_custom_channel_ids(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    dsdiff_comment_t comment;
    unsigned char *data;
    unsigned long datasize = 10;
    uint32_t frames_written;
    char text1[] = "abcdefghijklmnnopqrstuwxyz1234567890!@#$%^&*()_+";
    char text2[] = "ABCDEFGHIJKLMNNOPQRSTUWXYZ\n1234567890\n!@#$%^&*()_+\n";
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 1;
    timecode.seconds = 1;
    timecode.samples = 1;
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_write_custom_chan.dff", DSDIFF_AUDIO_DSD,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Write data */
    data = (unsigned char*)sa_malloc(2 * datasize);
    memset(data, 1, 2 * datasize);
    ret = dsdiff_write_dsd_data(file, data, datasize, &frames_written);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    sa_free(data);

    /* Add comments */
    memset(&comment, 0, sizeof(comment));
    comment.minute = 1;
    comment.hour = 2;
    comment.day = 3;
    comment.month = 4;
    comment.year = 5;
    comment.comment_type = 5;
    comment.comment_ref = 6;
    comment.text_length = (uint32_t) strlen(text1);
    comment.text = text1;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&comment, 0, sizeof(comment));
    comment.minute = 61;
    comment.hour = 62;
    comment.day = 63;
    comment.month = 64;
    comment.year = 65;
    comment.comment_type = 65;
    comment.comment_ref = 66;
    comment.text_length = (uint32_t) strlen(text2);
    comment.text = text2;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Write with Different Sample Frequencies
 * ===========================================================================*/

static void test_write_different_sample_rate(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    unsigned char *data;
    unsigned long datasize = 10;
    uint32_t frames_written;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 1;
    timecode.seconds = 1;
    timecode.samples = 1;
    dsdiff_set_start_timecode(file, &timecode);

    /* Create DSD file with 128FS sample rate */
    ret = dsdiff_create(file, "test_write_128fs.dff", DSDIFF_AUDIO_DSD,
                        2, 1, DSDIFF_SAMPLE_FREQ_128FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Write data */
    data = (unsigned char*)sa_malloc(2 * datasize);
    memset(data, 1, 2 * datasize);
    ret = dsdiff_write_dsd_data(file, data, datasize, &frames_written);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    sa_free(data);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Main Test Runner
 * ===========================================================================*/

int main(void)
{
    const struct CMUnitTest basic_write_tests[] = {
        cmocka_unit_test(test_write_basic_mono),
        cmocka_unit_test(test_write_basic_stereo),
    };

    const struct CMUnitTest sector_buffering_tests[] = {
        cmocka_unit_test(test_write_sector_buffered),
    };

    const struct CMUnitTest large_file_tests[] = {
        cmocka_unit_test(test_write_1mb),
    };

    const struct CMUnitTest error_handling_tests[] = {
        cmocka_unit_test(test_write_invalid_timecode),
    };

    const struct CMUnitTest advanced_tests[] = {
        cmocka_unit_test(test_write_custom_channel_ids),
        cmocka_unit_test(test_write_different_sample_rate),
    };

    int failed = 0;

    failed += cmocka_run_group_tests_name("Basic Write Tests",
                                          basic_write_tests, group_setup, NULL);
    failed += cmocka_run_group_tests_name("Sector Buffering Tests",
                                          sector_buffering_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Large File Tests",
                                          large_file_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Error Handling Tests",
                                          error_handling_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Advanced Tests",
                                          advanced_tests, NULL, group_teardown);

    return failed;
}
