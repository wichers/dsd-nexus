/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Unit tests for DSDIFF file operations using CMocka
 * Converted from: DSDIFF14Test.cpp
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
    /* Clean up test files */
    remove("test_dsd.dff");
    remove("test_dst.dff");
    remove("test_read.dff");
    remove("test_modify.dff");
    remove("test_markers.dff");
    remove("test_all_markers.dff");
    remove("test_sort_markers.dff");
    remove("test_comments.dff");
    remove("test_metadata.dff");
    remove("test_stereo.dff");
    remove("test_mc5.dff");
    return 0;
}

/* =============================================================================
 * Test: File Creation and Opening
 * ===========================================================================*/

static void test_file_create_dsd(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_non_null(file);

    memset(&timecode, 0, sizeof(timecode));
    ret = dsdiff_set_start_timecode(file, &timecode);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_create(file, "test_dsd.dff", DSDIFF_AUDIO_DSD,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_finalize(file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_close(file);
    assert_int_equal(ret, DSDIFF_SUCCESS);
}

static void test_file_create_dst(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 2;
    timecode.seconds = 3;
    timecode.samples = 4;
    ret = dsdiff_set_start_timecode(file, &timecode);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_create(file, "test_dst.dff", DSDIFF_AUDIO_DST,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_finalize(file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_close(file);
    assert_int_equal(ret, DSDIFF_SUCCESS);
}

static void test_file_open_for_read(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_file_mode_t mode;
    int ret;

    /* First create a test file */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_create(file, "test_read.dff", DSDIFF_AUDIO_DSD,
                        1, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Now open for reading */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_read.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_open_mode(file, &mode);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(mode, DSDIFF_FILE_MODE_READ);

    ret = dsdiff_close(file);
    assert_int_equal(ret, DSDIFF_SUCCESS);
}

static void test_file_open_for_modify(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_file_mode_t mode;
    int ret;

    /* First create a test file */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_create(file, "test_modify.dff", DSDIFF_AUDIO_DSD,
                        1, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Now open for modification */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_modify(file, "test_modify.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_open_mode(file, &mode);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(mode, DSDIFF_FILE_MODE_MODIFY);

    ret = dsdiff_finalize(file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_close(file);
    assert_int_equal(ret, DSDIFF_SUCCESS);
}

/* =============================================================================
 * Test: Marker Operations
 * ===========================================================================*/

static void test_marker_write_and_read(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_marker_t marker_in, marker_out;
    char marker_text[] = "Test Marker";
    int ret, nr_markers;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_create(file, "test_markers.dff", DSDIFF_AUDIO_DSD,
                        1, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&marker_in, 0, sizeof(marker_in));
    marker_in.time.hours = 1;
    marker_in.time.minutes = 2;
    marker_in.time.seconds = 3;
    marker_in.time.samples = 4;
    marker_in.offset = 5;
    marker_in.mark_channel = DSDIFF_MARK_CHANNEL_ALL;
    marker_in.mark_type = DSDIFF_MARK_TRACK_START;
    marker_in.track_flags = DSDIFF_TRACK_FLAG_NONE;
    marker_in.text_length = (uint32_t) strlen(marker_text);
    marker_in.marker_text = marker_text;

    ret = dsdiff_add_dsd_marker(file, &marker_in);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Read markers back */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_markers.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_dsd_marker_count(file, &nr_markers);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(nr_markers, 1);

    memset(&marker_out, 0, sizeof(marker_out));
    ret = dsdiff_get_dsd_marker(file, 0, &marker_out);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    assert_int_equal(marker_out.time.hours, 1);
    assert_int_equal(marker_out.time.minutes, 2);
    assert_int_equal(marker_out.time.seconds, 3);
    assert_int_equal(marker_out.time.samples, 4);
    assert_int_equal(marker_out.mark_type, DSDIFF_MARK_TRACK_START);

    if (marker_out.marker_text) {
        sa_free(marker_out.marker_text);
    }

    dsdiff_close(file);
}

static void test_marker_all_types(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_marker_t marker;
    char text1[] = "noflags";
    char text2[] = "MUTE4";
    char text3[] = "Mute1";
    char text4[] = "Mute2";
    char text5[] = "Mute3";
    int ret, nr_markers;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_create(file, "test_all_markers.dff", DSDIFF_AUDIO_DSD,
                        1, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&marker, 0, sizeof(marker));
    marker.time.hours = 1;
    marker.time.minutes = 2;
    marker.time.seconds = 3;
    marker.time.samples = 4;

    /* Marker 1: Track Start, no flags */
    marker.track_flags = DSDIFF_TRACK_FLAG_NONE;
    marker.mark_channel = DSDIFF_MARK_CHANNEL_ALL;
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    marker.text_length = sizeof(text1) - 1;
    marker.marker_text = text1;
    ret = dsdiff_add_dsd_marker(file, &marker);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Marker 2: Track Stop, MUTE4 */
    marker.mark_channel = (dsdiff_mark_channel_t)1;
    marker.mark_type = DSDIFF_MARK_TRACK_STOP;
    marker.text_length = sizeof(text2) - 1;
    marker.marker_text = text2;
    marker.track_flags = DSDIFF_TRACK_FLAG_TMF4_MUTE;
    ret = dsdiff_add_dsd_marker(file, &marker);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Marker 3: Program Start, MUTE1 */
    marker.mark_type = DSDIFF_MARK_PROGRAM_START;
    marker.text_length = sizeof(text3) - 1;
    marker.marker_text = text3;
    marker.track_flags = DSDIFF_TRACK_FLAG_TMF1_MUTE;
    ret = dsdiff_add_dsd_marker(file, &marker);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Marker 4: Index, MUTE2 */
    marker.mark_type = DSDIFF_MARK_INDEX;
    marker.text_length = sizeof(text4) - 1;
    marker.marker_text = text4;
    marker.track_flags = DSDIFF_TRACK_FLAG_TMF2_MUTE;
    ret = dsdiff_add_dsd_marker(file, &marker);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Marker 5: Index, MUTE3 */
    marker.mark_type = DSDIFF_MARK_INDEX;
    marker.text_length = sizeof(text5) - 1;
    marker.marker_text = text5;
    marker.track_flags = DSDIFF_TRACK_FLAG_TMF3_MUTE;
    ret = dsdiff_add_dsd_marker(file, &marker);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Read back and verify count */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_all_markers.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_dsd_marker_count(file, &nr_markers);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(nr_markers, 5);

    dsdiff_close(file);
}

static void test_marker_sort(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_marker_t marker, retrieved;
    int ret, nr_markers;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_create(file, "test_sort_markers.dff", DSDIFF_AUDIO_DSD,
                        1, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Add markers in non-chronological order */
    memset(&marker, 0, sizeof(marker));
    marker.mark_channel = DSDIFF_MARK_CHANNEL_ALL;
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    marker.track_flags = DSDIFF_TRACK_FLAG_NONE;
    marker.marker_text = NULL;
    marker.text_length = 0;

    marker.time.seconds = 30;
    dsdiff_add_dsd_marker(file, &marker);

    marker.time.seconds = 10;
    dsdiff_add_dsd_marker(file, &marker);

    marker.time.seconds = 20;
    dsdiff_add_dsd_marker(file, &marker);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Open for modification and sort */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_modify(file, "test_sort_markers.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_sort_dsd_markers(file, DSDIFF_MARKER_SORT_TIMESTAMP);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Read back and verify order */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_sort_markers.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_dsd_marker_count(file, &nr_markers);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(nr_markers, 3);

    memset(&retrieved, 0, sizeof(retrieved));
    ret = dsdiff_get_dsd_marker(file, 0, &retrieved);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(retrieved.time.seconds, 10);

    memset(&retrieved, 0, sizeof(retrieved));
    ret = dsdiff_get_dsd_marker(file, 1, &retrieved);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(retrieved.time.seconds, 20);

    memset(&retrieved, 0, sizeof(retrieved));
    ret = dsdiff_get_dsd_marker(file, 2, &retrieved);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(retrieved.time.seconds, 30);

    dsdiff_close(file);
}

/* =============================================================================
 * Test: Comment Operations
 * ===========================================================================*/

static void test_comment_write_and_read(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_comment_t comment_in, comment_out;
    char comment_text[] = "Test Comment";
    int ret, nr_comments;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_create(file, "test_comments.dff", DSDIFF_AUDIO_DSD,
                        1, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&comment_in, 0, sizeof(comment_in));
    comment_in.year = 2025;
    comment_in.month = 1;
    comment_in.day = 15;
    comment_in.hour = 10;
    comment_in.minute = 30;
    comment_in.comment_type = 0;
    comment_in.comment_ref = 0;
    comment_in.text_length = (uint32_t) strlen(comment_text);
    comment_in.text = comment_text;

    ret = dsdiff_add_comment(file, &comment_in);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Read comment back */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_comments.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_comment_count(file, &nr_comments);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(nr_comments, 1);

    memset(&comment_out, 0, sizeof(comment_out));
    ret = dsdiff_get_comment(file, 0, &comment_out);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    assert_int_equal(comment_out.year, 2025);
    assert_int_equal(comment_out.month, 1);
    assert_int_equal(comment_out.day, 15);

    dsdiff_close(file);
}

/* =============================================================================
 * Test: Metadata Operations
 * ===========================================================================*/

static void test_metadata_artist_title_emid(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    char artist_in[] = "Test Artist";
    char title_in[] = "Test Title";
    char emid_in[] = "TEST-EMID-12345";
    char artist_out[256];
    char title_out[256];
    char emid_out[256];
    uint32_t size;
    int ret, has_artist, has_title, has_emid;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_create(file, "test_metadata.dff", DSDIFF_AUDIO_DSD,
                        1, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_set_disc_artist(file, artist_in);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_set_disc_title(file, title_in);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_set_emid(file, emid_in);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Read metadata back */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_metadata.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_has_disc_artist(file, &has_artist);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(has_artist != 0);

    size = sizeof(artist_out);
    ret = dsdiff_get_disc_artist(file, &size, artist_out);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_string_equal(artist_out, artist_in);

    ret = dsdiff_has_disc_title(file, &has_title);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(has_title != 0);

    size = sizeof(title_out);
    ret = dsdiff_get_disc_title(file, &size, title_out);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_string_equal(title_out, title_in);

    ret = dsdiff_has_emid(file, &has_emid);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(has_emid != 0);

    size = sizeof(emid_out);
    ret = dsdiff_get_emid(file, &size, emid_out);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_string_equal(emid_out, emid_in);

    dsdiff_close(file);
}

/* =============================================================================
 * Test: Channel Configuration
 * ===========================================================================*/

static void test_channel_stereo(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_channel_id_t chan_ids_out[2];
    uint16_t channel_count;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_create(file, "test_stereo.dff", DSDIFF_AUDIO_DSD,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Read back and verify */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_stereo.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_channel_count(file, &channel_count);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(channel_count, 2);

    ret = dsdiff_get_channel_ids(file, chan_ids_out);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(chan_ids_out[0], DSDIFF_CHAN_SLFT);
    assert_int_equal(chan_ids_out[1], DSDIFF_CHAN_SRGT);

    dsdiff_close(file);
}

static void test_channel_multichannel(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_channel_id_t chan_ids_out[5];
    uint16_t channel_count;
    int ret;

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_create(file, "test_mc5.dff", DSDIFF_AUDIO_DSD,
                        5, 1, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Read back and verify */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_mc5.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_channel_count(file, &channel_count);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(channel_count, 5);

    ret = dsdiff_get_channel_ids(file, chan_ids_out);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(chan_ids_out[0], DSDIFF_CHAN_MLFT);
    assert_int_equal(chan_ids_out[1], DSDIFF_CHAN_MRGT);
    assert_int_equal(chan_ids_out[2], DSDIFF_CHAN_C);
    assert_int_equal(chan_ids_out[3], DSDIFF_CHAN_LS);
    assert_int_equal(chan_ids_out[4], DSDIFF_CHAN_RS);

    dsdiff_close(file);
}

/* =============================================================================
 * Main Test Runner
 * ===========================================================================*/

int main(void)
{
    const struct CMUnitTest file_creation_tests[] = {
        cmocka_unit_test(test_file_create_dsd),
        cmocka_unit_test(test_file_create_dst),
        cmocka_unit_test(test_file_open_for_read),
        cmocka_unit_test(test_file_open_for_modify),
    };

    const struct CMUnitTest marker_tests[] = {
        cmocka_unit_test(test_marker_write_and_read),
        cmocka_unit_test(test_marker_all_types),
        cmocka_unit_test(test_marker_sort),
    };

    const struct CMUnitTest comment_tests[] = {
        cmocka_unit_test(test_comment_write_and_read),
    };

    const struct CMUnitTest metadata_tests[] = {
        cmocka_unit_test(test_metadata_artist_title_emid),
    };

    const struct CMUnitTest channel_tests[] = {
        cmocka_unit_test(test_channel_stereo),
        cmocka_unit_test(test_channel_multichannel),
    };

    int failed = 0;

    failed += cmocka_run_group_tests_name("File Creation Tests",
                                          file_creation_tests, group_setup, NULL);
    failed += cmocka_run_group_tests_name("Marker Tests",
                                          marker_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Comment Tests",
                                          comment_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Metadata Tests",
                                          metadata_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Channel Tests",
                                          channel_tests, NULL, group_teardown);

    return failed;
}
