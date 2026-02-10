/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Unit tests for dsdiff_markers module using CMocka
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

#include "dsdiff_markers.h"

#include <libsautil/mem.h>

#include <stdio.h>
#include <string.h>
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
    return 0;
}

/* =============================================================================
 * Test: List Initialization
 * ===========================================================================*/

static void test_marker_list_init(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;

    dsdiff_marker_list_init(&list);

    assert_int_equal(list.count, 0);
    assert_int_equal(dsdiff_marker_list_is_empty(&list), 1);
    assert_int_equal(dsdiff_marker_list_get_count(&list), 0);
}

static void test_marker_list_init_null(void **state)
{
    (void)state;
    dsdiff_marker_list_init(NULL);
    /* Should not crash */
}

/* =============================================================================
 * Test: Adding Markers
 * ===========================================================================*/

static void test_marker_list_add_single(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker;
    int ret;

    dsdiff_marker_list_init(&list);

    /* Create a test marker */
    memset(&marker, 0, sizeof(marker));
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    marker.time.hours = 0;
    marker.time.minutes = 1;
    marker.time.seconds = 30;
    marker.time.samples = 1000;
    marker.marker_text = NULL;

    ret = dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(dsdiff_marker_list_get_count(&list), 1);
    assert_int_equal(dsdiff_marker_list_is_empty(&list), 0);

    dsdiff_marker_list_free(&list);
}

static void test_marker_list_add_multiple(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker;
    int ret;
    uint8_t i;

    dsdiff_marker_list_init(&list);

    /* Add 10 markers */
    for (i = 0; i < 10; i++) {
        memset(&marker, 0, sizeof(marker));
        marker.mark_type = DSDIFF_MARK_TRACK_START;
        marker.time.seconds = i;
        marker.marker_text = NULL;

        ret = dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);
        assert_int_equal(ret, DSDIFF_SUCCESS);
    }

    assert_int_equal(dsdiff_marker_list_get_count(&list), 10);

    dsdiff_marker_list_free(&list);
}

static void test_marker_list_add_with_text(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker;
    int ret;
    const char *test_text = "Track 1 - Test Song";

    dsdiff_marker_list_init(&list);

    memset(&marker, 0, sizeof(marker));
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    marker.time.seconds = 0;
    marker.marker_text = (char *)test_text;

    ret = dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(dsdiff_marker_list_get_count(&list), 1);

    dsdiff_marker_list_free(&list);
}

static void test_marker_list_add_null_params(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker;
    int ret;

    dsdiff_marker_list_init(&list);
    memset(&marker, 0, sizeof(marker));

    /* Test NULL list */
    ret = dsdiff_marker_list_add(NULL, &marker, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    /* Test NULL marker */
    ret = dsdiff_marker_list_add(&list, NULL, DSDIFF_SAMPLE_FREQ_64FS);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    dsdiff_marker_list_free(&list);
}

/* =============================================================================
 * Test: Retrieving Markers
 * ===========================================================================*/

static void test_marker_list_get(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker_in, marker_out;
    uint32_t sample_rate;
    int ret;
    const char *test_text = "Test Marker";

    dsdiff_marker_list_init(&list);

    /* Add a marker */
    memset(&marker_in, 0, sizeof(marker_in));
    marker_in.mark_type = DSDIFF_MARK_INDEX;
    marker_in.time.hours = 1;
    marker_in.time.minutes = 23;
    marker_in.time.seconds = 45;
    marker_in.time.samples = 5000;
    marker_in.marker_text = (char *)test_text;

    ret = dsdiff_marker_list_add(&list, &marker_in, DSDIFF_SAMPLE_FREQ_128FS);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Retrieve the marker */
    memset(&marker_out, 0, sizeof(marker_out));
    ret = dsdiff_marker_list_get(&list, 0, &marker_out, &sample_rate);

    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(marker_out.mark_type, DSDIFF_MARK_INDEX);
    assert_int_equal(marker_out.time.hours, 1);
    assert_int_equal(marker_out.time.minutes, 23);
    assert_int_equal(marker_out.time.seconds, 45);
    assert_int_equal(marker_out.time.samples, 5000);
    assert_int_equal(sample_rate, DSDIFF_SAMPLE_FREQ_128FS);
    assert_non_null(marker_out.marker_text);
    assert_string_equal(marker_out.marker_text, test_text);

    /* Free the duplicated text */
    if (marker_out.marker_text) {
        sa_free(marker_out.marker_text);
    }

    dsdiff_marker_list_free(&list);
}

static void test_marker_list_get_invalid_index(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker;
    int ret;

    dsdiff_marker_list_init(&list);

    /* Try to get from empty list */
    ret = dsdiff_marker_list_get(&list, 0, &marker, NULL);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    /* Add one marker */
    memset(&marker, 0, sizeof(marker));
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    /* Try to get invalid index */
    ret = dsdiff_marker_list_get(&list, 1, &marker, NULL);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    ret = dsdiff_marker_list_get(&list, 100, &marker, NULL);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    dsdiff_marker_list_free(&list);
}

static void test_marker_list_get_null_params(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker;
    int ret;

    dsdiff_marker_list_init(&list);
    memset(&marker, 0, sizeof(marker));
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    /* Test NULL list */
    ret = dsdiff_marker_list_get(NULL, 0, &marker, NULL);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    /* Test NULL marker */
    ret = dsdiff_marker_list_get(&list, 0, NULL, NULL);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    dsdiff_marker_list_free(&list);
}

/* =============================================================================
 * Test: Deleting Markers
 * ===========================================================================*/

static void test_marker_list_delete(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker;
    int ret;
    uint8_t i;

    dsdiff_marker_list_init(&list);

    /* Add 5 markers */
    for (i = 0; i < 5; i++) {
        memset(&marker, 0, sizeof(marker));
        marker.time.seconds = i;
        dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);
    }

    assert_int_equal(dsdiff_marker_list_get_count(&list), 5);

    /* Delete middle marker (index 2) */
    ret = dsdiff_marker_list_remove(&list, 2);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(dsdiff_marker_list_get_count(&list), 4);

    /* Delete first marker (index 0) */
    ret = dsdiff_marker_list_remove(&list, 0);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(dsdiff_marker_list_get_count(&list), 3);

    /* Delete last marker (now index 2) */
    ret = dsdiff_marker_list_remove(&list, 2);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(dsdiff_marker_list_get_count(&list), 2);

    dsdiff_marker_list_free(&list);
}

static void test_marker_list_delete_all(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker;
    int ret;

    dsdiff_marker_list_init(&list);

    /* Add 3 markers */
    memset(&marker, 0, sizeof(marker));
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    /* Delete all markers one by one (always delete index 0) */
    ret = dsdiff_marker_list_remove(&list, 0);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(dsdiff_marker_list_get_count(&list), 2);

    ret = dsdiff_marker_list_remove(&list, 0);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(dsdiff_marker_list_get_count(&list), 1);

    ret = dsdiff_marker_list_remove(&list, 0);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(dsdiff_marker_list_get_count(&list), 0);
    assert_int_equal(dsdiff_marker_list_is_empty(&list), 1);

    dsdiff_marker_list_free(&list);
}

static void test_marker_list_delete_invalid(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker;
    int ret;

    dsdiff_marker_list_init(&list);

    /* Try to delete from empty list */
    ret = dsdiff_marker_list_remove(&list, 0);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    /* Add one marker */
    memset(&marker, 0, sizeof(marker));
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    /* Try invalid indices */
    ret = dsdiff_marker_list_remove(&list, 1);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    ret = dsdiff_marker_list_remove(&list, 100);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    /* Test NULL list */
    ret = dsdiff_marker_list_remove(NULL, 0);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    dsdiff_marker_list_free(&list);
}

/* =============================================================================
 * Test: Marker Sorting
 * ===========================================================================*/

static void test_marker_list_sort(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker, retrieved;
    int ret;

    dsdiff_marker_list_init(&list);

    /* Add markers in non-chronological order */
    memset(&marker, 0, sizeof(marker));
    marker.time.seconds = 30;
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    marker.time.seconds = 10;
    marker.mark_type = DSDIFF_MARK_INDEX;
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    marker.time.seconds = 20;
    marker.mark_type = DSDIFF_MARK_TRACK_STOP;
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    marker.time.seconds = 10;
    marker.mark_type = DSDIFF_MARK_TRACK_START;
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    /* Sort the list */
    dsdiff_marker_list_sort(&list);

    /* Verify order */
    ret = dsdiff_marker_list_get(&list, 0, &retrieved, NULL);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(retrieved.time.seconds, 10);
    assert_int_equal(retrieved.mark_type, DSDIFF_MARK_TRACK_START);

    ret = dsdiff_marker_list_get(&list, 1, &retrieved, NULL);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(retrieved.time.seconds, 10);
    assert_int_equal(retrieved.mark_type, DSDIFF_MARK_INDEX);

    ret = dsdiff_marker_list_get(&list, 2, &retrieved, NULL);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(retrieved.time.seconds, 20);

    ret = dsdiff_marker_list_get(&list, 3, &retrieved, NULL);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(retrieved.time.seconds, 30);

    dsdiff_marker_list_free(&list);
}

static void test_marker_list_sort_empty(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;

    dsdiff_marker_list_init(&list);

    /* Sort empty list (should not crash) */
    dsdiff_marker_list_sort(&list);

    assert_int_equal(dsdiff_marker_list_get_count(&list), 0);

    dsdiff_marker_list_free(&list);
}

static void test_marker_list_sort_single(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker;

    dsdiff_marker_list_init(&list);

    memset(&marker, 0, sizeof(marker));
    marker.time.seconds = 42;
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    /* Sort single-item list (should not crash) */
    dsdiff_marker_list_sort(&list);

    assert_int_equal(dsdiff_marker_list_get_count(&list), 1);

    dsdiff_marker_list_free(&list);
}

static void test_marker_list_sort_null(void **state)
{
    (void)state;
    /* Sort NULL list (should not crash) */
    dsdiff_marker_list_sort(NULL);
}

/* =============================================================================
 * Test: List Cleanup
 * ===========================================================================*/

static void test_marker_list_free(void **state)
{
    (void)state;
    dsdiff_marker_list_t list;
    dsdiff_marker_t marker;

    dsdiff_marker_list_init(&list);

    /* Add markers with text */
    memset(&marker, 0, sizeof(marker));
    marker.marker_text = (char *)"Test 1";
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    marker.marker_text = (char *)"Test 2";
    dsdiff_marker_list_add(&list, &marker, DSDIFF_SAMPLE_FREQ_64FS);

    assert_int_equal(dsdiff_marker_list_get_count(&list), 2);

    /* Free the list */
    dsdiff_marker_list_free(&list);

    assert_int_equal(dsdiff_marker_list_get_count(&list), 0);
    assert_int_equal(dsdiff_marker_list_is_empty(&list), 1);
}

static void test_marker_list_free_null(void **state)
{
    (void)state;
    /* Free NULL list (should not crash) */
    dsdiff_marker_list_free(NULL);
}

/* =============================================================================
 * Test: Edge Cases
 * ===========================================================================*/

static void test_marker_entry_create(void **state)
{
    (void)state;
    dsdiff_marker_t marker;
    dsdiff_marker_entry_t *entry;
    const char *test_text = "Test Entry";

    memset(&marker, 0, sizeof(marker));
    marker.mark_type = DSDIFF_MARK_PROGRAM_START;
    marker.time.hours = 2;
    marker.marker_text = (char *)test_text;

    entry = dsdiff_marker_entry_create(&marker, DSDIFF_SAMPLE_FREQ_256FS);

    assert_non_null(entry);
    assert_int_equal(entry->marker.mark_type, DSDIFF_MARK_PROGRAM_START);
    assert_int_equal(entry->marker.time.hours, 2);
    assert_int_equal(entry->sample_rate, DSDIFF_SAMPLE_FREQ_256FS);
    assert_non_null(entry->marker.marker_text);
    assert_string_equal(entry->marker.marker_text, test_text);

    dsdiff_marker_entry_free(entry);
}

static void test_marker_entry_create_null(void **state)
{
    (void)state;
    dsdiff_marker_entry_t *entry;

    entry = dsdiff_marker_entry_create(NULL, DSDIFF_SAMPLE_FREQ_64FS);
    assert_null(entry);
}

static void test_marker_entry_free_null(void **state)
{
    (void)state;
    /* Free NULL entry (should not crash) */
    dsdiff_marker_entry_free(NULL);
}

static void test_marker_list_get_count_null(void **state)
{
    (void)state;
    uint32_t count;

    count = dsdiff_marker_list_get_count(NULL);
    assert_int_equal(count, 0);
}

static void test_marker_list_is_empty_null(void **state)
{
    (void)state;
    int empty;

    empty = dsdiff_marker_list_is_empty(NULL);
    assert_int_equal(empty, 1);
}

/* =============================================================================
 * Main Test Runner
 * ===========================================================================*/

int main(void)
{
    const struct CMUnitTest init_tests[] = {
        cmocka_unit_test(test_marker_list_init),
        cmocka_unit_test(test_marker_list_init_null),
    };

    const struct CMUnitTest add_tests[] = {
        cmocka_unit_test(test_marker_list_add_single),
        cmocka_unit_test(test_marker_list_add_multiple),
        cmocka_unit_test(test_marker_list_add_with_text),
        cmocka_unit_test(test_marker_list_add_null_params),
    };

    const struct CMUnitTest get_tests[] = {
        cmocka_unit_test(test_marker_list_get),
        cmocka_unit_test(test_marker_list_get_invalid_index),
        cmocka_unit_test(test_marker_list_get_null_params),
    };

    const struct CMUnitTest delete_tests[] = {
        cmocka_unit_test(test_marker_list_delete),
        cmocka_unit_test(test_marker_list_delete_all),
        cmocka_unit_test(test_marker_list_delete_invalid),
    };

    const struct CMUnitTest sort_tests[] = {
        cmocka_unit_test(test_marker_list_sort),
        cmocka_unit_test(test_marker_list_sort_empty),
        cmocka_unit_test(test_marker_list_sort_single),
        cmocka_unit_test(test_marker_list_sort_null),
    };

    const struct CMUnitTest cleanup_tests[] = {
        cmocka_unit_test(test_marker_list_free),
        cmocka_unit_test(test_marker_list_free_null),
    };

    const struct CMUnitTest edge_case_tests[] = {
        cmocka_unit_test(test_marker_entry_create),
        cmocka_unit_test(test_marker_entry_create_null),
        cmocka_unit_test(test_marker_entry_free_null),
        cmocka_unit_test(test_marker_list_get_count_null),
        cmocka_unit_test(test_marker_list_is_empty_null),
    };

    int failed = 0;

    failed += cmocka_run_group_tests_name("List Initialization Tests",
                                          init_tests, group_setup, NULL);
    failed += cmocka_run_group_tests_name("Adding Markers Tests",
                                          add_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Retrieving Markers Tests",
                                          get_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Deleting Markers Tests",
                                          delete_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Sorting Tests",
                                          sort_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Cleanup Tests",
                                          cleanup_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Edge Case Tests",
                                          edge_case_tests, NULL, group_teardown);

    return failed;
}
