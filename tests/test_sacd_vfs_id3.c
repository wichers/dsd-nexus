/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Unit tests for SACD VFS ID3 tag modification functionality
 * This test suite covers ID3 metadata overlay operations including:
 * - Setting ID3 overlays for tracks
 * - Getting ID3 tags (original and overlayed)
 * - Saving overlays to XML sidecar files
 * - Clearing overlays
 * - Verifying overlay persistence
 * Test data:
 * - data/DSD.iso: Example SACD ISO image
 * - data/id3.tag: Binary ID3v2 tag data for testing
 * Run with Valgrind for memory leak detection:
 *   valgrind --leak-check=full --show-leak-kinds=all ./test_sacd_vfs_id3
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
#include <libsautil/mem.h>
#include <libsautil/compat.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <io.h>
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#endif

/* =============================================================================
 * Test Configuration
 * ===========================================================================*/

/* Paths relative to test working directory (build/tests/) */
#define TEST_ISO_PATH       "data\\DSD.iso"
#define TEST_ID3_TAG_PATH   "data\\id3.tag"
#define TEST_ISO_XML_PATH   "data\\DSD.iso.xml"

/* Expected ID3 tag file size */
#define EXPECTED_ID3_TAG_SIZE   372

/* =============================================================================
 * Test Fixtures
 * ===========================================================================*/

/** Test fixture containing VFS context and loaded ID3 tag data */
typedef struct {
    sacd_vfs_ctx_t *ctx;
    uint8_t *id3_tag_data;
    size_t id3_tag_size;
    bool iso_available;
    bool id3_available;
} test_fixture_t;

/**
 * @brief Load a binary file into memory
 */
static int load_binary_file(const char *path, uint8_t **data, size_t *size)
{
    FILE *fp = sa_fopen(path, "rb");
    if (!fp) {
        return -1;
    }

    sa_fseek64(fp, 0, SEEK_END);
    int64_t file_size = sa_ftell64(fp);
    sa_fseek64(fp, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(fp);
        return -1;
    }

    *data = (uint8_t *)sa_malloc((size_t)file_size);
    if (!*data) {
        fclose(fp);
        return -1;
    }

    size_t read_size = fread(*data, 1, (size_t)file_size, fp);
    fclose(fp);

    if (read_size != (size_t)file_size) {
        sa_free(*data);
        *data = NULL;
        return -1;
    }

    *size = (size_t)file_size;
    return 0;
}

/**
 * @brief Check if a file exists
 */
static bool file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

/**
 * @brief Delete a file if it exists
 */
static void delete_file_if_exists(const char *path)
{
    if (file_exists(path)) {
        remove(path);
    }
}

/**
 * @brief Setup function for tests requiring an open ISO
 */
static int setup_with_iso(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)sa_calloc(1, sizeof(test_fixture_t));
    if (!fixture) {
        return -1;
    }

    /* Check if test files exist */
    fixture->iso_available = file_exists(TEST_ISO_PATH);
    fixture->id3_available = file_exists(TEST_ID3_TAG_PATH);

    /* Load ID3 tag data if available */
    if (fixture->id3_available) {
        if (load_binary_file(TEST_ID3_TAG_PATH, &fixture->id3_tag_data, &fixture->id3_tag_size) != 0) {
            fixture->id3_available = false;
        }
    }

    /* Create VFS context */
    fixture->ctx = sacd_vfs_create();
    if (!fixture->ctx) {
        sa_free(fixture->id3_tag_data);
        sa_free(fixture);
        return -1;
    }

    /* Open ISO if available */
    if (fixture->iso_available) {
        int ret = sacd_vfs_open(fixture->ctx, TEST_ISO_PATH);
        if (ret != SACD_VFS_OK) {
            fixture->iso_available = false;
        }
    }

    /* Clean up any leftover XML sidecar from previous tests */
    delete_file_if_exists(TEST_ISO_XML_PATH);

    *state = fixture;
    return 0;
}

/**
 * @brief Teardown function
 */
static int teardown_with_iso(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;
    if (!fixture) {
        return 0;
    }

    if (fixture->ctx) {
        sacd_vfs_close(fixture->ctx);
        sacd_vfs_destroy(fixture->ctx);
    }

    sa_free(fixture->id3_tag_data);
    sa_free(fixture);

    /* Clean up XML sidecar created during tests */
    delete_file_if_exists(TEST_ISO_XML_PATH);

    *state = NULL;
    return 0;
}

/* =============================================================================
 * Test: ID3 Tag Data Verification
 * ===========================================================================*/

/**
 * @brief Verify the test ID3 tag file is valid
 */
static void test_id3_tag_file_valid(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->id3_available) {
        skip();
    }

    /* Verify size matches expected */
    assert_int_equal(fixture->id3_tag_size, EXPECTED_ID3_TAG_SIZE);

    /* Verify it starts with a valid ID3v2 frame ID (TIT2 = title) */
    assert_true(fixture->id3_tag_size >= 4);
    assert_memory_equal(fixture->id3_tag_data, "TIT2", 4);
}

/* =============================================================================
 * Test: Get Original ID3 Tag
 * ===========================================================================*/

/**
 * @brief Test getting the original ID3 tag from a track
 */
static void test_get_original_id3_tag(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available) {
        skip();
    }

    /* Get ID3 tag for track 1 in stereo area */
    uint8_t *buffer = NULL;
    size_t size = 0;
    int ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_STEREO, 1, &buffer, &size);

    assert_int_equal(ret, SACD_VFS_OK);
    assert_non_null(buffer);
    assert_true(size > 0);

    /* The original ID3 tag should be generated from disc metadata */
    /* Just verify it's a valid ID3v2 tag starting with "ID3" */
    if (size >= 3) {
        assert_memory_equal(buffer, "ID3", 3);
    }

    sa_free(buffer);
}

/**
 * @brief Test getting ID3 tag for invalid track number
 */
static void test_get_id3_tag_invalid_track(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available) {
        skip();
    }

    uint8_t *buffer = NULL;
    size_t size = 0;

    /* Track 0 is invalid (tracks are 1-based) */
    int ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_STEREO, 0, &buffer, &size);
    assert_int_equal(ret, SACD_VFS_ERROR_INVALID_PARAMETER);

    /* Track 255 should be out of range */
    ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_STEREO, 255, &buffer, &size);
    assert_int_not_equal(ret, SACD_VFS_OK);
}

/* =============================================================================
 * Test: Set ID3 Overlay
 * ===========================================================================*/

/**
 * @brief Test setting an ID3 overlay for a track
 */
static void test_set_id3_overlay(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available || !fixture->id3_available) {
        skip();
    }

    /* Initially no unsaved changes */
    bool has_changes = sacd_vfs_has_unsaved_id3_changes(fixture->ctx);
    assert_false(has_changes);

    /* Set ID3 overlay for track 1 */
    int ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                        fixture->id3_tag_data, fixture->id3_tag_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Now there should be unsaved changes */
    has_changes = sacd_vfs_has_unsaved_id3_changes(fixture->ctx);
    assert_true(has_changes);
}

/**
 * @brief Test that getting ID3 tag returns overlay after setting
 */
static void test_get_id3_returns_overlay(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available || !fixture->id3_available) {
        skip();
    }

    /* Set ID3 overlay for track 1 */
    int ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                        fixture->id3_tag_data, fixture->id3_tag_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Get ID3 tag - should return the overlay */
    uint8_t *buffer = NULL;
    size_t size = 0;
    ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_STEREO, 1, &buffer, &size);

    assert_int_equal(ret, SACD_VFS_OK);
    assert_non_null(buffer);
    assert_int_equal(size, fixture->id3_tag_size);

    /* Verify the data matches */
    assert_memory_equal(buffer, fixture->id3_tag_data, fixture->id3_tag_size);

    sa_free(buffer);
}

/**
 * @brief Test setting overlay for multiple tracks
 */
static void test_set_id3_overlay_multiple_tracks(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available || !fixture->id3_available) {
        skip();
    }

    /* Get track count */
    uint8_t track_count = 0;
    int ret = sacd_vfs_get_track_count(fixture->ctx, SACD_VFS_AREA_STEREO, &track_count);
    assert_int_equal(ret, SACD_VFS_OK);

    if (track_count < 2) {
        skip();
    }

    /* Set overlay for track 1 */
    ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                    fixture->id3_tag_data, fixture->id3_tag_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Set overlay for track 2 */
    ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 2,
                                    fixture->id3_tag_data, fixture->id3_tag_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Verify both overlays are set */
    uint8_t *buffer1 = NULL;
    uint8_t *buffer2 = NULL;
    size_t size1 = 0, size2 = 0;

    ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_STEREO, 1, &buffer1, &size1);
    assert_int_equal(ret, SACD_VFS_OK);
    assert_int_equal(size1, fixture->id3_tag_size);

    ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_STEREO, 2, &buffer2, &size2);
    assert_int_equal(ret, SACD_VFS_OK);
    assert_int_equal(size2, fixture->id3_tag_size);

    sa_free(buffer1);
    sa_free(buffer2);
}

/**
 * @brief Test replacing an existing overlay
 */
static void test_replace_id3_overlay(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available || !fixture->id3_available) {
        skip();
    }

    /* Set initial overlay */
    int ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                        fixture->id3_tag_data, fixture->id3_tag_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Create modified data (flip first byte) */
    uint8_t *modified_data = (uint8_t *)sa_malloc(fixture->id3_tag_size);
    assert_non_null(modified_data);
    memcpy(modified_data, fixture->id3_tag_data, fixture->id3_tag_size);
    modified_data[0] ^= 0xFF;

    /* Replace with modified data */
    ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                    modified_data, fixture->id3_tag_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Verify the modified data is returned */
    uint8_t *buffer = NULL;
    size_t size = 0;
    ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_STEREO, 1, &buffer, &size);
    assert_int_equal(ret, SACD_VFS_OK);
    assert_memory_equal(buffer, modified_data, fixture->id3_tag_size);

    sa_free(modified_data);
    sa_free(buffer);
}

/* =============================================================================
 * Test: Clear ID3 Overlay
 * ===========================================================================*/

/**
 * @brief Test clearing an ID3 overlay
 */
static void test_clear_id3_overlay(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available || !fixture->id3_available) {
        skip();
    }

    /* Set overlay */
    int ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                        fixture->id3_tag_data, fixture->id3_tag_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Verify overlay is set */
    uint8_t *buffer = NULL;
    size_t size = 0;
    ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_STEREO, 1, &buffer, &size);
    assert_int_equal(ret, SACD_VFS_OK);
    assert_int_equal(size, fixture->id3_tag_size);
    sa_free(buffer);
    buffer = NULL;

    /* Clear the overlay */
    ret = sacd_vfs_clear_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Get ID3 tag again - should return original disc metadata */
    ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_STEREO, 1, &buffer, &size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Size should be different from overlay (original tag generated from disc) */
    /* Just verify it's a valid ID3 tag */
    assert_non_null(buffer);
    assert_true(size > 0);

    sa_free(buffer);
}

/**
 * @brief Test clearing non-existent overlay is OK
 */
static void test_clear_nonexistent_overlay(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available) {
        skip();
    }

    /* Clear overlay for track that has no overlay - should succeed */
    int ret = sacd_vfs_clear_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1);
    assert_int_equal(ret, SACD_VFS_OK);
}

/* =============================================================================
 * Test: Save ID3 Overlay
 * ===========================================================================*/

/**
 * @brief Test saving ID3 overlays to XML sidecar
 */
static void test_save_id3_overlay(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available || !fixture->id3_available) {
        skip();
    }

    /* Ensure no XML file exists */
    delete_file_if_exists(TEST_ISO_XML_PATH);

    /* Set overlay */
    int ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                        fixture->id3_tag_data, fixture->id3_tag_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Save to XML sidecar */
    ret = sacd_vfs_save_id3_overlay(fixture->ctx);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Verify XML file was created */
    assert_true(file_exists(TEST_ISO_XML_PATH));

    /* After saving, there should be no unsaved changes */
    bool has_changes = sacd_vfs_has_unsaved_id3_changes(fixture->ctx);
    assert_false(has_changes);
}

/**
 * @brief Test that saving when no changes is OK
 */
static void test_save_no_changes(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available) {
        skip();
    }

    /* No overlays set */
    bool has_changes = sacd_vfs_has_unsaved_id3_changes(fixture->ctx);
    assert_false(has_changes);

    /* Saving should succeed (nothing to do) */
    int ret = sacd_vfs_save_id3_overlay(fixture->ctx);
    assert_int_equal(ret, SACD_VFS_OK);
}

/* =============================================================================
 * Test: ID3 Overlay Persistence (Load After Save)
 * ===========================================================================*/

/**
 * @brief Test that saved overlays are loaded on reopen
 */
static void test_overlay_persistence(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available || !fixture->id3_available) {
        skip();
    }

    /* Ensure no XML file exists */
    delete_file_if_exists(TEST_ISO_XML_PATH);

    /* Set and save overlay */
    int ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                        fixture->id3_tag_data, fixture->id3_tag_size);
    assert_int_equal(ret, SACD_VFS_OK);

    ret = sacd_vfs_save_id3_overlay(fixture->ctx);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Close the context */
    sacd_vfs_close(fixture->ctx);

    /* Reopen the ISO */
    ret = sacd_vfs_open(fixture->ctx, TEST_ISO_PATH);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Get ID3 tag - should return the saved overlay */
    uint8_t *buffer = NULL;
    size_t size = 0;
    ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_STEREO, 1, &buffer, &size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Verify the data matches the original overlay */
    assert_int_equal(size, fixture->id3_tag_size);
    assert_memory_equal(buffer, fixture->id3_tag_data, fixture->id3_tag_size);

    sa_free(buffer);
}

/* =============================================================================
 * Test: ID3 Overlay with Virtual File
 * ===========================================================================*/

/**
 * @brief Test that ID3 overlay affects virtual file reads
 */
static void test_overlay_affects_virtual_file(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available || !fixture->id3_available) {
        skip();
    }

    /* Get track filename */
    char track_filename[256];
    int ret = sacd_vfs_get_track_filename(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                           track_filename, sizeof(track_filename));
    assert_int_equal(ret, SACD_VFS_OK);

    /* Build VFS path */
    char vfs_path[512];
    snprintf(vfs_path, sizeof(vfs_path), "/Stereo/%s", track_filename);

    /* Open virtual file before overlay */
    sacd_vfs_file_t *file1 = NULL;
    ret = sacd_vfs_file_open(fixture->ctx, vfs_path, &file1);
    assert_int_equal(ret, SACD_VFS_OK);

    sacd_vfs_file_info_t info1;
    sacd_vfs_file_get_info(file1, &info1);
    uint64_t original_metadata_size = info1.metadata_size;

    sacd_vfs_file_close(file1);

    /* Set ID3 overlay */
    ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                    fixture->id3_tag_data, fixture->id3_tag_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Open virtual file after overlay */
    sacd_vfs_file_t *file2 = NULL;
    ret = sacd_vfs_file_open(fixture->ctx, vfs_path, &file2);
    assert_int_equal(ret, SACD_VFS_OK);

    sacd_vfs_file_info_t info2;
    sacd_vfs_file_get_info(file2, &info2);

    /* Metadata size should now match the overlay size */
    assert_int_equal(info2.metadata_size, fixture->id3_tag_size);

    /* Read the metadata region */
    uint8_t *read_buffer = (uint8_t *)sa_malloc(fixture->id3_tag_size);
    assert_non_null(read_buffer);

    ret = sacd_vfs_file_seek(file2, (int64_t)info2.metadata_offset, SEEK_SET);
    assert_int_equal(ret, SACD_VFS_OK);

    size_t bytes_read = 0;
    ret = sacd_vfs_file_read(file2, read_buffer, fixture->id3_tag_size, &bytes_read);
    assert_int_equal(ret, SACD_VFS_OK);
    assert_int_equal(bytes_read, fixture->id3_tag_size);

    /* Verify the read data matches the overlay */
    assert_memory_equal(read_buffer, fixture->id3_tag_data, fixture->id3_tag_size);

    sa_free(read_buffer);
    sacd_vfs_file_close(file2);

    /* Suppress warning about potentially unused variable */
    (void)original_metadata_size;
}

/* =============================================================================
 * Test: Area-Specific Overlays
 * ===========================================================================*/

/**
 * @brief Test that overlays are area-specific
 */
static void test_overlay_area_specific(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available || !fixture->id3_available) {
        skip();
    }

    /* Check if multichannel area exists */
    bool has_multichannel = sacd_vfs_has_area(fixture->ctx, SACD_VFS_AREA_MULTICHANNEL);
    if (!has_multichannel) {
        skip();
    }

    /* Set overlay for stereo track 1 */
    int ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                        fixture->id3_tag_data, fixture->id3_tag_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Get ID3 for multichannel track 1 - should NOT have the overlay */
    uint8_t *mc_buffer = NULL;
    size_t mc_size = 0;
    ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_MULTICHANNEL, 1, &mc_buffer, &mc_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Multichannel track should have original ID3, not the overlay */
    /* (Unless they happen to be the same size, verify content differs) */
    if (mc_size == fixture->id3_tag_size) {
        /* Sizes match, but content should differ */
        int cmp = memcmp(mc_buffer, fixture->id3_tag_data, fixture->id3_tag_size);
        assert_int_not_equal(cmp, 0);
    }

    sa_free(mc_buffer);
}

/* =============================================================================
 * Test: Error Handling
 * ===========================================================================*/

/**
 * @brief Test ID3 operations on closed context
 */
static void test_id3_operations_closed_context(void **state)
{
    (void)state;

    sacd_vfs_ctx_t *ctx = sacd_vfs_create();
    assert_non_null(ctx);

    uint8_t dummy[10] = {0};
    uint8_t *buffer = NULL;
    size_t size = 0;

    /* All operations should return NOT_OPEN on closed context */
    int ret = sacd_vfs_get_id3_tag(ctx, SACD_VFS_AREA_STEREO, 1, &buffer, &size);
    assert_int_equal(ret, SACD_VFS_ERROR_NOT_OPEN);

    ret = sacd_vfs_set_id3_overlay(ctx, SACD_VFS_AREA_STEREO, 1, dummy, sizeof(dummy));
    assert_int_equal(ret, SACD_VFS_ERROR_NOT_OPEN);

    ret = sacd_vfs_clear_id3_overlay(ctx, SACD_VFS_AREA_STEREO, 1);
    assert_int_equal(ret, SACD_VFS_ERROR_NOT_OPEN);

    ret = sacd_vfs_save_id3_overlay(ctx);
    assert_int_equal(ret, SACD_VFS_ERROR_NOT_OPEN);

    bool has_changes = sacd_vfs_has_unsaved_id3_changes(ctx);
    assert_false(has_changes);

    sacd_vfs_destroy(ctx);
}

/**
 * @brief Test ID3 operations with invalid area
 */
static void test_id3_operations_invalid_area(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available || !fixture->id3_available) {
        skip();
    }

    uint8_t *buffer = NULL;
    size_t size = 0;

    /* Invalid area type */
    int ret = sacd_vfs_get_id3_tag(fixture->ctx, (sacd_vfs_area_t)99, 1, &buffer, &size);
    assert_int_not_equal(ret, SACD_VFS_OK);

    ret = sacd_vfs_set_id3_overlay(fixture->ctx, (sacd_vfs_area_t)99, 1,
                                    fixture->id3_tag_data, fixture->id3_tag_size);
    assert_int_not_equal(ret, SACD_VFS_OK);

    ret = sacd_vfs_clear_id3_overlay(fixture->ctx, (sacd_vfs_area_t)99, 1);
    assert_int_not_equal(ret, SACD_VFS_OK);
}

/* =============================================================================
 * Test: Large ID3 Tag Handling
 * ===========================================================================*/

/**
 * @brief Test setting a large ID3 overlay
 */
static void test_large_id3_overlay(void **state)
{
    test_fixture_t *fixture = (test_fixture_t *)*state;

    if (!fixture->iso_available) {
        skip();
    }

    /* Create a large ID3 tag (64KB) */
    size_t large_size = 64 * 1024;
    uint8_t *large_data = (uint8_t *)sa_calloc(large_size, 1);
    assert_non_null(large_data);

    /* Fill with pattern */
    for (size_t i = 0; i < large_size; i++) {
        large_data[i] = (uint8_t)(i & 0xFF);
    }

    /* Set large overlay */
    int ret = sacd_vfs_set_id3_overlay(fixture->ctx, SACD_VFS_AREA_STEREO, 1,
                                        large_data, large_size);
    assert_int_equal(ret, SACD_VFS_OK);

    /* Verify it can be retrieved */
    uint8_t *buffer = NULL;
    size_t size = 0;
    ret = sacd_vfs_get_id3_tag(fixture->ctx, SACD_VFS_AREA_STEREO, 1, &buffer, &size);
    assert_int_equal(ret, SACD_VFS_OK);
    assert_int_equal(size, large_size);
    assert_memory_equal(buffer, large_data, large_size);

    sa_free(buffer);
    sa_free(large_data);
}

/* =============================================================================
 * Main Test Runner
 * ===========================================================================*/

int main(void)
{
    const struct CMUnitTest id3_basic_tests[] = {
        cmocka_unit_test_setup_teardown(test_id3_tag_file_valid, setup_with_iso, teardown_with_iso),
        cmocka_unit_test_setup_teardown(test_get_original_id3_tag, setup_with_iso, teardown_with_iso),
        cmocka_unit_test_setup_teardown(test_get_id3_tag_invalid_track, setup_with_iso, teardown_with_iso),
    };

    const struct CMUnitTest id3_overlay_tests[] = {
        cmocka_unit_test_setup_teardown(test_set_id3_overlay, setup_with_iso, teardown_with_iso),
        cmocka_unit_test_setup_teardown(test_get_id3_returns_overlay, setup_with_iso, teardown_with_iso),
        cmocka_unit_test_setup_teardown(test_set_id3_overlay_multiple_tracks, setup_with_iso, teardown_with_iso),
        cmocka_unit_test_setup_teardown(test_replace_id3_overlay, setup_with_iso, teardown_with_iso),
    };

    const struct CMUnitTest id3_clear_tests[] = {
        cmocka_unit_test_setup_teardown(test_clear_id3_overlay, setup_with_iso, teardown_with_iso),
        cmocka_unit_test_setup_teardown(test_clear_nonexistent_overlay, setup_with_iso, teardown_with_iso),
    };

    const struct CMUnitTest id3_save_tests[] = {
        cmocka_unit_test_setup_teardown(test_save_id3_overlay, setup_with_iso, teardown_with_iso),
        cmocka_unit_test_setup_teardown(test_save_no_changes, setup_with_iso, teardown_with_iso),
        cmocka_unit_test_setup_teardown(test_overlay_persistence, setup_with_iso, teardown_with_iso),
    };

    const struct CMUnitTest id3_virtual_file_tests[] = {
        cmocka_unit_test_setup_teardown(test_overlay_affects_virtual_file, setup_with_iso, teardown_with_iso),
        cmocka_unit_test_setup_teardown(test_overlay_area_specific, setup_with_iso, teardown_with_iso),
    };

    const struct CMUnitTest id3_error_tests[] = {
        cmocka_unit_test(test_id3_operations_closed_context),
        cmocka_unit_test_setup_teardown(test_id3_operations_invalid_area, setup_with_iso, teardown_with_iso),
    };

    const struct CMUnitTest id3_stress_tests[] = {
        cmocka_unit_test_setup_teardown(test_large_id3_overlay, setup_with_iso, teardown_with_iso),
    };

    int failed = 0;

    printf("=== SACD VFS ID3 Tag Modification Tests ===\n\n");
    printf("Note: Tests require data/DSD.iso and data/id3.tag files.\n");
    printf("      Tests will be skipped if files are not available.\n\n");

    failed += cmocka_run_group_tests_name("ID3 Basic Tests",
                                          id3_basic_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("ID3 Overlay Tests",
                                          id3_overlay_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("ID3 Clear Tests",
                                          id3_clear_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("ID3 Save/Persistence Tests",
                                          id3_save_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("ID3 Virtual File Tests",
                                          id3_virtual_file_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("ID3 Error Handling Tests",
                                          id3_error_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("ID3 Stress Tests",
                                          id3_stress_tests, NULL, NULL);

    return failed;
}
