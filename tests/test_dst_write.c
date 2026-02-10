/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Unit tests for DST file writing operations using CMocka
 * Converted from: DSTWriteTest.cpp
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

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <libdsdiff/dsdiff.h>

#include "dsdiff_types.h"
#include "dsdiff_markers.h"

#include <libsautil/mem.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
    remove("test_dst_basic.dff");
    remove("test_dst_stereo.dff");
    remove("test_dst_crc.dff");
    remove("test_dst_sizes.dff");
    remove("test_dst_comments.dff");
    remove("test_dst_markers.dff");
    remove("test_dst_framerate.dff");
    return 0;
}

/* =============================================================================
 * Test: Create DST File and Write Basic Data
 * ===========================================================================*/

static void test_dst_write_basic(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    unsigned char *data_1, *data_2;
    unsigned long datasize = 100;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_dst_basic.dff", DSDIFF_AUDIO_DST,
                        1, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Add a comment */
    dsdiff_comment_t comment;
    char text[] = "abcdefghijklmnnopqrstuwxyz1234567890!@#$%^&*()_+";
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

    /* Write DST frames */
    data_1 = (unsigned char*)sa_malloc(2 * datasize);
    data_2 = (unsigned char*)sa_malloc(datasize);
    memset(data_1, 1, 2 * datasize);
    memset(data_2, 2, datasize);

    ret = dsdiff_write_dst_frame(file, data_1, 2 * datasize);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_write_dst_frame(file, data_2, datasize);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    sa_free(data_1);
    sa_free(data_2);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Create DST File with 2 Channels
 * ===========================================================================*/

static void test_dst_write_stereo(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    dsdiff_comment_t comment;
    dsdiff_marker_t marker;
    unsigned char *data;
    unsigned long datasize = 100;
    char text1[] = "ABCDEFGHIJKLMNNOPQRSTUWXYZ\n1234567890\n!@#$%^&*()_+\n";
    char artist[] = "0123456789";
    char title[] = "MyTitleName";
    char emid[] = "ABCDE12345";
    char marker_text[] = "ABCDEFGHIJKLMNNOPQRSTUWXYZ\n1234567890\n!@#$%^&*()_+\n";
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 1;
    timecode.seconds = 1;
    timecode.samples = 1;
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_dst_stereo.dff", DSDIFF_AUDIO_DST,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_set_loudspeaker_config(file, DSDIFF_LS_CONFIG_STEREO);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    data = (unsigned char*)sa_malloc(datasize);
    memset(data, 1, datasize);
    ret = dsdiff_write_dst_frame(file, data, datasize);
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
    comment.comment_type = 1;
    comment.comment_ref = 66;
    comment.text_length = (uint32_t) strlen(text1);
    comment.text = text1;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Set DIIN metadata */
    memset(&marker, 0, sizeof(marker));
    marker.time.hours = 1;
    marker.time.minutes = 2;
    marker.time.seconds = 3;
    marker.time.samples = 4;
    marker.mark_channel = DSDIFF_MARK_CHANNEL_ALL;
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    marker.track_flags = DSDIFF_TRACK_FLAG_LFE_MUTE;
    marker.text_length = (uint32_t) strlen(marker_text);
    marker.marker_text = marker_text;
    ret = dsdiff_add_dsd_marker(file, &marker);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_set_disc_artist(file, artist);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_set_disc_title(file, title);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_set_emid(file, emid);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Write DST Frames with CRC
 * ===========================================================================*/

static void test_dst_write_with_crc(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    unsigned char *data_1, *data_2, *crc_1;
    unsigned long datasize = 100;
    unsigned long crcsize = 10;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 1;
    timecode.seconds = 1;
    timecode.samples = 1;
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_dst_crc.dff", DSDIFF_AUDIO_DST,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    data_1 = (unsigned char*)sa_malloc(2 * datasize);
    data_2 = (unsigned char*)sa_malloc(datasize);
    crc_1 = (unsigned char*)sa_malloc(crcsize);
    memset(data_1, 1, 2 * datasize);
    memset(data_2, 2, datasize);
    memset(crc_1, 1, crcsize);

    ret = dsdiff_write_dst_frame_with_crc(file, data_1, 2 * datasize, crc_1, crcsize);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_write_dst_frame_with_crc(file, data_2, datasize, crc_1, crcsize);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    sa_free(data_1);
    sa_free(data_2);
    sa_free(crc_1);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Write DST Frames with Different Sizes
 * ===========================================================================*/

static void test_dst_write_different_sizes(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    unsigned char *data;
    unsigned long datasize = 1000;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 1;
    timecode.seconds = 1;
    timecode.samples = 1;
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_dst_sizes.dff", DSDIFF_AUDIO_DST,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    data = (unsigned char*)sa_malloc(datasize);

    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < 10; i++) {
            uint32_t size_to_write = ((datasize / 11) * i) + i;
            if (size_to_write < 5) size_to_write = 5 + i + j;

            memset(data, i, datasize);
            ret = dsdiff_write_dst_frame(file, data, size_to_write);
            assert_int_equal(ret, DSDIFF_SUCCESS);
        }
    }

    sa_free(data);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Write All Comment Types
 * ===========================================================================*/

static void test_dst_write_all_comments(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    dsdiff_comment_t comment;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 1;
    timecode.seconds = 1;
    timecode.samples = 1;
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_dst_comments.dff", DSDIFF_AUDIO_DST,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    char text1[] = "General";
    char text2[] = "Channel";
    char text3[] = "Sound Source DSD";
    char text4[] = "Sound Source Analog";
    char text5[] = "Sound Source PCM";
    char text6[] = "History Remark";
    char text7[] = "History Operator";
    char text8[] = "History Create Machine";
    char text9[] = "History Place Zone";

    memset(&comment, 0, sizeof(comment));
    comment.minute = 1;
    comment.hour = 2;
    comment.day = 3;
    comment.month = 4;
    comment.year = 2000;

    /* General comment (type 0) */
    comment.comment_type = 0;
    comment.comment_ref = 0;
    comment.text_length = (uint32_t) strlen(text1);
    comment.text = text1;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Channel comment (type 1) */
    comment.comment_type = 1;
    comment.comment_ref = 1;
    comment.text_length = (uint32_t) strlen(text2);
    comment.text = text2;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Sound Source comment (type 2) - DSD recording */
    comment.comment_type = 2;
    comment.comment_ref = 0;
    comment.text_length = (uint32_t) strlen(text3);
    comment.text = text3;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Sound Source comment (type 2) - Analog recording */
    comment.comment_ref = 1;
    comment.text_length = (uint32_t) strlen(text4);
    comment.text = text4;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Sound Source comment (type 2) - PCM recording */
    comment.comment_ref = 2;
    comment.text_length = (uint32_t) strlen(text5);
    comment.text = text5;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* File History comment (type 3) - General Remark */
    comment.comment_type = 3;
    comment.comment_ref = 0;
    comment.text_length = (uint32_t) strlen(text6);
    comment.text = text6;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* File History comment (type 3) - Operator */
    comment.comment_ref = 1;
    comment.text_length = (uint32_t) strlen(text7);
    comment.text = text7;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* File History comment (type 3) - Create Machine */
    comment.comment_ref = 2;
    comment.text_length = (uint32_t) strlen(text8);
    comment.text = text8;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* File History comment (type 3) - Place Zone */
    comment.comment_ref = 3;
    comment.text_length = (uint32_t) strlen(text9);
    comment.text = text9;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Write All Marker Types
 * ===========================================================================*/

static void test_dst_write_all_markers(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    dsdiff_marker_t marker;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 1;
    timecode.seconds = 1;
    timecode.samples = 1;
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_dst_markers.dff", DSDIFF_AUDIO_DST,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    char text1[] = "MarkTrackStart";
    char text2[] = "MarkTrackStop";
    char text3[] = "MarkIndex";

    memset(&marker, 0, sizeof(marker));
    marker.time.hours = 1;
    marker.time.minutes = 2;
    marker.time.seconds = 3;
    marker.time.samples = 4;
    marker.track_flags = DSDIFF_TRACK_FLAG_LFE_MUTE;

    /* MarkTrackStart */
    marker.mark_channel = DSDIFF_MARK_CHANNEL_ALL;
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    marker.text_length = (uint32_t) strlen(text1);
    marker.marker_text = text1;
    ret = dsdiff_add_dsd_marker(file, &marker);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* MarkTrackStop */
    marker.mark_channel = (dsdiff_mark_channel_t)1;
    marker.mark_type = DSDIFF_MARK_TRACK_STOP;
    marker.text_length = (uint32_t) strlen(text2);
    marker.marker_text = text2;
    ret = dsdiff_add_dsd_marker(file, &marker);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* MarkIndex */
    marker.mark_type = DSDIFF_MARK_INDEX;
    marker.text_length = (uint32_t) strlen(text3);
    marker.marker_text = text3;
    ret = dsdiff_add_dsd_marker(file, &marker);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Write DST with Frame Rate Setting
 * ===========================================================================*/

static void test_dst_write_with_framerate(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    unsigned char *data;
    unsigned long datasize = 1000;
    unsigned long crcsize = 10;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 1;
    timecode.seconds = 1;
    timecode.samples = 1;
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, "test_dst_framerate.dff", DSDIFF_AUDIO_DST,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    data = (unsigned char*)sa_malloc(datasize);
    unsigned char *crc_data = (unsigned char*)sa_malloc(crcsize);
    memset(data, 1, datasize);
    memset(crc_data, 1, crcsize);

    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < 10; i++) {
            uint32_t size_to_write = ((datasize / 11) * i) + i;
            if (size_to_write < 5) size_to_write = 5 + i + j;

            memset(data, i, datasize);
            memset(crc_data, i, crcsize);
            ret = dsdiff_write_dst_frame_with_crc(file, data, size_to_write, crc_data, crcsize);
            assert_int_equal(ret, DSDIFF_SUCCESS);
        }
    }

    sa_free(data);
    sa_free(crc_data);

    ret = dsdiff_set_dst_frame_rate(file, 80);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Main Test Runner
 * ===========================================================================*/

int main(void)
{
    const struct CMUnitTest basic_write_tests[] = {
        cmocka_unit_test(test_dst_write_basic),
        cmocka_unit_test(test_dst_write_stereo),
    };

    const struct CMUnitTest crc_write_tests[] = {
        cmocka_unit_test(test_dst_write_with_crc),
    };

    const struct CMUnitTest variable_size_tests[] = {
        cmocka_unit_test(test_dst_write_different_sizes),
    };

    const struct CMUnitTest comment_tests[] = {
        cmocka_unit_test(test_dst_write_all_comments),
    };

    const struct CMUnitTest marker_tests[] = {
        cmocka_unit_test(test_dst_write_all_markers),
    };

    const struct CMUnitTest framerate_tests[] = {
        cmocka_unit_test(test_dst_write_with_framerate),
    };

    int failed = 0;

    failed += cmocka_run_group_tests_name("Basic DST Write Tests",
                                          basic_write_tests, group_setup, NULL);
    failed += cmocka_run_group_tests_name("CRC Write Tests",
                                          crc_write_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Variable Size Tests",
                                          variable_size_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Comment Tests",
                                          comment_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Marker Tests",
                                          marker_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Frame Rate Tests",
                                          framerate_tests, NULL, group_teardown);

    return failed;
}
