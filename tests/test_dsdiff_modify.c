/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Unit tests for DSDIFF file modification operations using CMocka
 * Converted from: DSDIFFModifyTest.cpp
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
 * Helper: Create test file for modification
 * ===========================================================================*/

static int create_test_file_with_metadata(const char *filename)
{
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode;
    dsdiff_marker_t marker;
    dsdiff_comment_t comment;
    unsigned char *data;
    uint32_t frames_written;
    char artist[] = "Original Artist";
    char title[] = "Original Title";
    char emid[] = "ORIGINAL-EMID-12345";
    char comment_text[] = "Original Comment";
    char marker_text[] = "Original Marker";
    int ret;

    ret = dsdiff_new(&file);
    if (ret != DSDIFF_SUCCESS) return ret;

    memset(&timecode, 0, sizeof(timecode));
    timecode.hours = 1;
    timecode.minutes = 2;
    timecode.seconds = 3;
    timecode.samples = 4;
    dsdiff_set_start_timecode(file, &timecode);

    ret = dsdiff_create(file, filename, DSDIFF_AUDIO_DSD,
                        2, 1, DSDIFF_SAMPLE_FREQ_64FS);
    if (ret != DSDIFF_SUCCESS) {
        dsdiff_close(file);
        return ret;
    }

    dsdiff_set_disc_artist(file, artist);
    dsdiff_set_disc_title(file, title);
    dsdiff_set_emid(file, emid);

    memset(&marker, 0, sizeof(marker));
    marker.time.hours = 0;
    marker.time.minutes = 1;
    marker.time.seconds = 30;
    marker.time.samples = 0;
    marker.mark_channel = DSDIFF_MARK_CHANNEL_ALL;
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    marker.track_flags = DSDIFF_TRACK_FLAG_NONE;
    marker.text_length = (uint32_t) strlen(marker_text);
    marker.marker_text = marker_text;
    dsdiff_add_dsd_marker(file, &marker);

    memset(&comment, 0, sizeof(comment));
    comment.year = 2025;
    comment.month = 1;
    comment.day = 15;
    comment.hour = 10;
    comment.minute = 30;
    comment.comment_type = 0;
    comment.comment_ref = 0;
    comment.text_length = (uint32_t) strlen(comment_text);
    comment.text = comment_text;
    dsdiff_add_comment(file, &comment);

    data = (unsigned char*)sa_malloc(2 * 1024);
    memset(data, 0x55, 2 * 1024);
    dsdiff_write_dsd_data(file, data, 1024, &frames_written);
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
    remove("test_modify.dff");
    remove("test_modify_chans.dff");
    remove("test_modify_meta.dff");
    remove("test_modify_tc.dff");
    remove("test_add_marker.dff");
    remove("test_del_marker.dff");
    remove("test_add_comment.dff");
    remove("test_del_comment.dff");
    remove("test_modify_ls.dff");
    return 0;
}

/* =============================================================================
 * Test: Opening for Modification
 * ===========================================================================*/

static void test_modify_open_file(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_file_mode_t mode;
    int ret;

    ret = create_test_file_with_metadata("test_modify.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_modify(file, "test_modify.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_open_mode(file, &mode);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(mode, DSDIFF_FILE_MODE_MODIFY);

    dsdiff_finalize(file);
    dsdiff_close(file);
}

/* =============================================================================
 * Test: Modifying Channel IDs
 * ===========================================================================*/

static void test_modify_channel_ids(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_channel_id_t chan_ids_in[2], chan_ids_out[2];
    int ret;

    ret = create_test_file_with_metadata("test_modify_chans.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_modify(file, "test_modify_chans.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    chan_ids_in[0] = DSDIFF_CHAN_C000 + 5;
    chan_ids_in[1] = DSDIFF_CHAN_SRGT;
    ret = dsdiff_set_channel_ids(file, chan_ids_in, 2);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Reopen and verify */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_modify_chans.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_channel_ids(file, chan_ids_out);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(chan_ids_out[0], DSDIFF_CHAN_C000 + 5);
    assert_int_equal(chan_ids_out[1], DSDIFF_CHAN_SRGT);

    dsdiff_close(file);
}

/* =============================================================================
 * Test: Modifying Metadata
 * ===========================================================================*/

static void test_modify_metadata(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    char artist_out[256], title_out[256], emid_out[256];
    uint32_t size;
    int ret;

    ret = create_test_file_with_metadata("test_modify_meta.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_modify(file, "test_modify_meta.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_set_disc_artist(file, "Modified Artist");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_set_disc_title(file, "Modified Title");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_set_emid(file, "MODIFIED-EMID");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Reopen and verify */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_modify_meta.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    size = sizeof(artist_out);
    ret = dsdiff_get_disc_artist(file, &size, artist_out);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_string_equal(artist_out, "Modified Artist");

    size = sizeof(title_out);
    ret = dsdiff_get_disc_title(file, &size, title_out);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_string_equal(title_out, "Modified Title");

    size = sizeof(emid_out);
    ret = dsdiff_get_emid(file, &size, emid_out);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_string_equal(emid_out, "MODIFIED-EMID");

    dsdiff_close(file);
}

/* =============================================================================
 * Test: Modifying Timecode
 * ===========================================================================*/

static void test_modify_timecode(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_timecode_t timecode_in, timecode_out;
    int ret, has_timecode;

    ret = create_test_file_with_metadata("test_modify_tc.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_modify(file, "test_modify_tc.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_has_start_timecode(file, &has_timecode);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    if (has_timecode) {
        memset(&timecode_in, 0, sizeof(timecode_in));
        timecode_in.hours = 50;
        timecode_in.minutes = 40;
        timecode_in.seconds = 30;
        timecode_in.samples = 20;
        ret = dsdiff_set_start_timecode(file, &timecode_in);
        assert_int_equal(ret, DSDIFF_SUCCESS);

        dsdiff_finalize(file);
        dsdiff_close(file);

        /* Reopen and verify */
        ret = dsdiff_new(&file);
        assert_int_equal(ret, DSDIFF_SUCCESS);

        ret = dsdiff_open(file, "test_modify_tc.dff");
        assert_int_equal(ret, DSDIFF_SUCCESS);

        memset(&timecode_out, 0, sizeof(timecode_out));
        ret = dsdiff_get_start_timecode(file, &timecode_out);
        assert_int_equal(ret, DSDIFF_SUCCESS);
        assert_int_equal(timecode_out.hours, 50);
        assert_int_equal(timecode_out.minutes, 40);
        assert_int_equal(timecode_out.seconds, 30);
        assert_int_equal(timecode_out.samples, 20);

        dsdiff_close(file);
    } else {
        dsdiff_close(file);
    }
}

/* =============================================================================
 * Test: Adding and Deleting Markers
 * ===========================================================================*/

static void test_modify_add_marker(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_marker_t marker;
    char marker_text[] = "Added Marker";
    int ret, nr_markers_before, nr_markers_after;

    ret = create_test_file_with_metadata("test_add_marker.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_modify(file, "test_add_marker.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_dsd_marker_count(file, &nr_markers_before);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    memset(&marker, 0, sizeof(marker));
    marker.time.hours = 0;
    marker.time.minutes = 5;
    marker.time.seconds = 0;
    marker.time.samples = 0;
    marker.mark_channel = DSDIFF_MARK_CHANNEL_ALL;
    marker.mark_type = DSDIFF_MARK_INDEX;
    marker.track_flags = DSDIFF_TRACK_FLAG_NONE;
    marker.text_length = (uint32_t) strlen(marker_text);
    marker.marker_text = marker_text;
    ret = dsdiff_add_dsd_marker(file, &marker);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Reopen and verify */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_add_marker.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_dsd_marker_count(file, &nr_markers_after);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(nr_markers_after, nr_markers_before + 1);

    dsdiff_close(file);
}

static void test_modify_delete_markers(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    int ret, nr_markers;

    ret = create_test_file_with_metadata("test_del_marker.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_modify(file, "test_del_marker.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_dsd_marker_count(file, &nr_markers);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    while (nr_markers > 0) {
        ret = dsdiff_delete_dsd_marker(file, 0);
        assert_int_equal(ret, DSDIFF_SUCCESS);
        nr_markers--;
    }

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Reopen and verify */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_del_marker.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_dsd_marker_count(file, &nr_markers);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(nr_markers, 0);

    dsdiff_close(file);
}

/* =============================================================================
 * Test: Adding and Deleting Comments
 * ===========================================================================*/

static void test_modify_add_comment(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_comment_t comment;
    char comment_text[] = "Added Comment";
    int ret, nr_comments_before, nr_comments_after;

    ret = create_test_file_with_metadata("test_add_comment.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_modify(file, "test_add_comment.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_comment_count(file, &nr_comments_before);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(nr_comments_before, 1);

    memset(&comment, 0, sizeof(comment));
    comment.year = 2025;
    comment.month = 12;
    comment.day = 25;
    comment.hour = 15;
    comment.minute = 30;
    comment.comment_type = 0;
    comment.comment_ref = 0;
    comment.text_length = (uint32_t) strlen(comment_text);
    comment.text = comment_text;
    ret = dsdiff_add_comment(file, &comment);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Reopen and verify */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_add_comment.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_comment_count(file, &nr_comments_after);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(nr_comments_after, nr_comments_before + 1);

    dsdiff_close(file);
}

static void test_modify_delete_comments(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    int ret, nr_comments;

    ret = create_test_file_with_metadata("test_del_comment.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_modify(file, "test_del_comment.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_comment_count(file, &nr_comments);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    while (nr_comments > 0) {
        ret = dsdiff_delete_comment(file, 0);
        assert_int_equal(ret, DSDIFF_SUCCESS);
        nr_comments--;
    }

    dsdiff_finalize(file);
    dsdiff_close(file);

    /* Reopen and verify */
    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_open(file, "test_del_comment.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_get_comment_count(file, &nr_comments);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(nr_comments, 0);

    dsdiff_close(file);
}

/* =============================================================================
 * Test: Modifying Loudspeaker Configuration
 * ===========================================================================*/

static void test_modify_loudspeaker_config(void **state)
{
    (void)state;
    dsdiff_t *file = NULL;
    dsdiff_loudspeaker_config_t config_out;
    int ret, has_config;

    ret = create_test_file_with_metadata("test_modify_ls.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_new(&file);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_modify(file, "test_modify_ls.dff");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_has_loudspeaker_config(file, &has_config);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    if (has_config) {
        ret = dsdiff_set_loudspeaker_config(file, DSDIFF_LS_CONFIG_STEREO);
        assert_int_equal(ret, DSDIFF_SUCCESS);

        dsdiff_finalize(file);
        dsdiff_close(file);

        /* Reopen and verify */
        ret = dsdiff_new(&file);
        assert_int_equal(ret, DSDIFF_SUCCESS);

        ret = dsdiff_open(file, "test_modify_ls.dff");
        assert_int_equal(ret, DSDIFF_SUCCESS);

        ret = dsdiff_get_loudspeaker_config(file, &config_out);
        assert_int_equal(ret, DSDIFF_SUCCESS);
        assert_int_equal(config_out, DSDIFF_LS_CONFIG_STEREO);

        dsdiff_close(file);
    } else {
        dsdiff_close(file);
    }
}

/* =============================================================================
 * Main Test Runner
 * ===========================================================================*/

int main(void)
{
    const struct CMUnitTest file_opening_tests[] = {
        cmocka_unit_test(test_modify_open_file),
    };

    const struct CMUnitTest channel_modification_tests[] = {
        cmocka_unit_test(test_modify_channel_ids),
    };

    const struct CMUnitTest metadata_modification_tests[] = {
        cmocka_unit_test(test_modify_metadata),
        cmocka_unit_test(test_modify_timecode),
    };

    const struct CMUnitTest marker_modification_tests[] = {
        cmocka_unit_test(test_modify_add_marker),
        cmocka_unit_test(test_modify_delete_markers),
    };

    const struct CMUnitTest comment_modification_tests[] = {
        cmocka_unit_test(test_modify_add_comment),
        cmocka_unit_test(test_modify_delete_comments),
    };

    const struct CMUnitTest loudspeaker_tests[] = {
        cmocka_unit_test(test_modify_loudspeaker_config),
    };

    int failed = 0;

    failed += cmocka_run_group_tests_name("File Opening Tests",
                                          file_opening_tests, group_setup, NULL);
    failed += cmocka_run_group_tests_name("Channel Modification Tests",
                                          channel_modification_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Metadata Modification Tests",
                                          metadata_modification_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Marker Modification Tests",
                                          marker_modification_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Comment Modification Tests",
                                          comment_modification_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Loudspeaker Config Tests",
                                          loudspeaker_tests, NULL, group_teardown);

    return failed;
}
