/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief Unit tests for dsdiff_io module using CMocka
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

#include "dsdiff_io.h"

#include <libsautil/compat.h>
#include <libsautil/sa_path.h>

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

/* Test file path */
#define TEST_FILE "test_dsdiff_io.tmp"

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
    sa_unlink(TEST_FILE);
    return 0;
}

static void cleanup_test_file(void)
{
    sa_unlink(TEST_FILE);
}

/* =============================================================================
 * Test: File Open/Close Operations
 * ===========================================================================*/

static void test_io_open_write(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_non_null(io);

    ret = dsdiff_io_close(io);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    cleanup_test_file();
}

static void test_io_open_write_null_params(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    int ret;

    ret = dsdiff_io_open_write(NULL, TEST_FILE);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    ret = dsdiff_io_open_write(io, NULL);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);
}

static void test_io_open_read_write_cycle(void **state)
{
    (void)state;
    dsdiff_io_t *io_write = NULL;
    dsdiff_io_t *io_read = NULL;
    int ret;

    cleanup_test_file();

    /* Create file */
    ret = dsdiff_io_open_write(io_write, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_close(io_write);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Read file */
    ret = dsdiff_io_open_read(io_read, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_non_null(io_read);

    ret = dsdiff_io_close(io_read);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    cleanup_test_file();
}

static void test_io_open_read_nonexistent(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_read(io, "nonexistent_file_12345.tmp");
    assert_int_not_equal(ret, DSDIFF_SUCCESS);
    assert_null(io);
}

static void test_io_open_modify(void **state)
{
    (void)state;
    dsdiff_io_t *io_write = NULL;
    dsdiff_io_t *io_modify = NULL;
    int ret;

    cleanup_test_file();

    /* Create file first */
    ret = dsdiff_io_open_write(io_write, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    ret = dsdiff_io_close(io_write);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Open for modification */
    ret = dsdiff_io_open_modify(io_modify, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_non_null(io_modify);

    ret = dsdiff_io_close(io_modify);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    cleanup_test_file();
}

static void test_io_close_null(void **state)
{
    (void)state;
    int ret;

    ret = dsdiff_io_close(NULL);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);
}

static void test_io_remove_file(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    int ret;
    int exists;

    cleanup_test_file();

    /* Create file */
    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Remove file */
    ret = sa_unlink(TEST_FILE);
    assert_int_equal(ret, 0);

    /* Verify file was deleted */
    struct stat st;
    exists = sa_stat(TEST_FILE, &st) == 0;
    assert_int_equal(exists, 0);
}

static void test_io_get_filename(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    char filename[256];
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_get_filename(io, filename, sizeof(filename));
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_non_null(strstr(filename, "test_dsdiff_io.tmp"));

    dsdiff_io_close(io);
    cleanup_test_file();
}

static void test_io_is_file_open(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    int is_open;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_is_open(io, &is_open);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(is_open, 1);

    dsdiff_io_close(io);
    cleanup_test_file();
}

/* =============================================================================
 * Test: Integer I/O Operations (Big-Endian)
 * ===========================================================================*/

static void test_io_uint8_write_read(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint8_t write_val = 0x42;
    uint8_t read_val = 0;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_write_uint8(io, write_val);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_close(io);

    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_uint8(io, &read_val);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(read_val, write_val);

    dsdiff_io_close(io);
    cleanup_test_file();
}

static void test_io_uint16_be_write_read(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint16_t write_val = 0x1234;
    uint16_t read_val = 0;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_write_uint16_be(io, write_val);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_close(io);

    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_uint16_be(io, &read_val);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(read_val, write_val);

    dsdiff_io_close(io);
    cleanup_test_file();
}

static void test_io_uint32_be_write_read(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint32_t write_val = 0x12345678;
    uint32_t read_val = 0;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_write_uint32_be(io, write_val);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_close(io);

    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_uint32_be(io, &read_val);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(read_val, write_val);

    dsdiff_io_close(io);
    cleanup_test_file();
}

static void test_io_uint64_be_write_read(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint64_t write_val = 0x123456789ABCDEF0ULL;
    uint64_t read_val = 0;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_write_uint64_be(io, write_val);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_close(io);

    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_uint64_be(io, &read_val);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(read_val == write_val);

    dsdiff_io_close(io);
    cleanup_test_file();
}

/* =============================================================================
 * Test: Chunk ID Operations
 * ===========================================================================*/

static void test_io_chunk_id_write_read(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint32_t write_id = 0x464D5438; /* "FMT8" */
    uint32_t read_id = 0;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_write_chunk_id(io, write_id);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_close(io);

    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_chunk_id(io, &read_id);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(read_id, write_id);

    dsdiff_io_close(io);
    cleanup_test_file();
}

/* =============================================================================
 * Test: Position Operations
 * ===========================================================================*/

static void test_io_seek_and_position(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint64_t position;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Write some data */
    dsdiff_io_write_uint32_be(io, 0x12345678);
    dsdiff_io_write_uint32_be(io, 0x9ABCDEF0);

    /* Get current position (should be 8) */
    ret = dsdiff_io_get_position(io, &position);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(position == 8);

    /* Seek to beginning */
    ret = dsdiff_io_seek(io, 0, DSDIFF_SEEK_SET, &position);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(position == 0);

    /* Seek forward 4 bytes */
    ret = dsdiff_io_seek(io, 4, DSDIFF_SEEK_CUR, &position);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(position == 4);

    /* Set position directly */
    ret = dsdiff_io_set_position(io, 2);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_get_position(io, &position);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(position == 2);

    dsdiff_io_close(io);
    cleanup_test_file();
}

static void test_io_get_file_size(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint64_t size;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Write 12 bytes */
    dsdiff_io_write_uint32_be(io, 0x12345678);
    dsdiff_io_write_uint32_be(io, 0x9ABCDEF0);
    dsdiff_io_write_uint32_be(io, 0x11223344);

    ret = dsdiff_io_get_file_size(io, &size);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_true(size == 12);

    dsdiff_io_close(io);
    cleanup_test_file();
}

/* =============================================================================
 * Test: String Operations
 * ===========================================================================*/

static void test_io_pstring_write_read(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    const char *write_str = "Hello, World!";
    char read_str[256];
    uint16_t write_len = (uint16_t)strlen(write_str);
    uint16_t read_len = 256;  /* Initialize to buffer capacity (FileSIAL.cpp line 672) */
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_write_pstring(io, write_len, write_str);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_close(io);

    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_pstring(io, &read_len, read_str, sizeof(read_str));
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(read_len, write_len + 1);
    assert_string_equal(read_str, write_str);

    dsdiff_io_close(io);
    cleanup_test_file();
}

static void test_io_pstring_empty(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    char read_str[256];
    uint16_t read_len = 256;  /* Initialize to buffer capacity (FileSIAL.cpp line 672) */
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_write_pstring(io, 0, "");
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_close(io);

    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_pstring(io, &read_len, read_str, sizeof(read_str));
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(read_len, 2);  /* length byte (1) + padding byte (1) */

    dsdiff_io_close(io);
    cleanup_test_file();
}

static void test_io_string_write_read(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    const char *write_str = "Test String";
    char read_str[256];
    uint32_t str_len = (uint32_t)strlen(write_str);
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_write_string(io, str_len, write_str);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_close(io);

    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_string(io, str_len, read_str);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_string_equal(read_str, write_str);

    dsdiff_io_close(io);
    cleanup_test_file();
}

/* =============================================================================
 * Test: Raw Byte Operations
 * ===========================================================================*/

static void test_io_bytes_write_read(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint8_t write_data[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                               0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    uint8_t read_data[16] = {0};
    size_t bytes_written = 0;
    size_t bytes_read = 0;
    int ret;
    int i;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_write_bytes(io, write_data, 16, &bytes_written);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(bytes_written, 16);

    dsdiff_io_close(io);

    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_bytes(io, read_data, 16, &bytes_read);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(bytes_read, 16);

    for (i = 0; i < 16; i++) {
        assert_int_equal(read_data[i], write_data[i]);
    }

    dsdiff_io_close(io);
    cleanup_test_file();
}

static void test_io_bytes_partial_read(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint8_t write_data[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    uint8_t read_data[4] = {0};
    size_t bytes_read = 0;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_write_bytes(io, write_data, 8, NULL);
    dsdiff_io_close(io);

    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_bytes(io, read_data, 4, &bytes_read);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    assert_int_equal(bytes_read, 4);
    assert_int_equal(read_data[0], 0x01);
    assert_int_equal(read_data[3], 0x04);

    dsdiff_io_close(io);
    cleanup_test_file();
}

/* =============================================================================
 * Test: Padding Operations
 * ===========================================================================*/

static void test_io_pad_byte_write_read(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_write_pad_byte(io);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_close(io);

    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_pad_byte(io);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Verify position moved */
    uint64_t pos;
    dsdiff_io_get_position(io, &pos);
    assert_true(pos == 1);

    dsdiff_io_close(io);
    cleanup_test_file();
}

/* =============================================================================
 * Test: Error Conditions
 * ===========================================================================*/

static void test_io_read_write_null_params(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint32_t value;
    int ret;

    cleanup_test_file();

    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    /* Test NULL parameters */
    ret = dsdiff_io_write_uint32_be(NULL, 0x12345678);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    ret = dsdiff_io_read_uint32_be(NULL, &value);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    ret = dsdiff_io_read_uint32_be(io, NULL);
    assert_int_equal(ret, DSDIFF_ERROR_INVALID_ARG);

    dsdiff_io_close(io);
    cleanup_test_file();
}

static void test_io_read_beyond_eof(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint32_t value;
    int ret;

    cleanup_test_file();

    /* Create small file */
    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);
    dsdiff_io_write_uint8(io, 0x42);
    dsdiff_io_close(io);

    /* Try to read more than available */
    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    ret = dsdiff_io_read_uint32_be(io, &value);
    assert_int_not_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_close(io);
    cleanup_test_file();
}

/* =============================================================================
 * Test: Mixed Operations
 * ===========================================================================*/

static void test_io_mixed_write_read(void **state)
{
    (void)state;
    dsdiff_io_t *io = NULL;
    uint8_t val8_w = 0x12, val8_r;
    uint16_t val16_w = 0x3456, val16_r;
    uint32_t val32_w = 0x789ABCDE, val32_r;
    uint64_t val64_w = 0xFEDCBA9876543210ULL, val64_r;
    int ret;

    cleanup_test_file();

    /* Write mixed data */
    ret = dsdiff_io_open_write(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_write_uint8(io, val8_w);
    dsdiff_io_write_uint16_be(io, val16_w);
    dsdiff_io_write_uint32_be(io, val32_w);
    dsdiff_io_write_uint64_be(io, val64_w);

    dsdiff_io_close(io);

    /* Read mixed data */
    ret = dsdiff_io_open_read(io, TEST_FILE);
    assert_int_equal(ret, DSDIFF_SUCCESS);

    dsdiff_io_read_uint8(io, &val8_r);
    assert_int_equal(val8_r, val8_w);

    dsdiff_io_read_uint16_be(io, &val16_r);
    assert_int_equal(val16_r, val16_w);

    dsdiff_io_read_uint32_be(io, &val32_r);
    assert_int_equal(val32_r, val32_w);

    dsdiff_io_read_uint64_be(io, &val64_r);
    assert_true(val64_r == val64_w);

    dsdiff_io_close(io);
    cleanup_test_file();
}

/* =============================================================================
 * Main Test Runner
 * ===========================================================================*/

int main(void)
{
    const struct CMUnitTest file_open_close_tests[] = {
        cmocka_unit_test(test_io_open_write),
        cmocka_unit_test(test_io_open_write_null_params),
        cmocka_unit_test(test_io_open_read_write_cycle),
        cmocka_unit_test(test_io_open_read_nonexistent),
        cmocka_unit_test(test_io_open_modify),
        cmocka_unit_test(test_io_close_null),
        cmocka_unit_test(test_io_remove_file),
        cmocka_unit_test(test_io_get_filename),
        cmocka_unit_test(test_io_is_file_open),
    };

    const struct CMUnitTest integer_be_tests[] = {
        cmocka_unit_test(test_io_uint8_write_read),
        cmocka_unit_test(test_io_uint16_be_write_read),
        cmocka_unit_test(test_io_uint32_be_write_read),
        cmocka_unit_test(test_io_uint64_be_write_read),
    };

    const struct CMUnitTest chunk_id_tests[] = {
        cmocka_unit_test(test_io_chunk_id_write_read),
    };

    const struct CMUnitTest position_tests[] = {
        cmocka_unit_test(test_io_seek_and_position),
        cmocka_unit_test(test_io_get_file_size),
    };

    const struct CMUnitTest string_tests[] = {
        cmocka_unit_test(test_io_pstring_write_read),
        cmocka_unit_test(test_io_pstring_empty),
        cmocka_unit_test(test_io_string_write_read),
    };

    const struct CMUnitTest byte_tests[] = {
        cmocka_unit_test(test_io_bytes_write_read),
        cmocka_unit_test(test_io_bytes_partial_read),
    };

    const struct CMUnitTest padding_tests[] = {
        cmocka_unit_test(test_io_pad_byte_write_read),
    };

    const struct CMUnitTest error_tests[] = {
        cmocka_unit_test(test_io_read_write_null_params),
        cmocka_unit_test(test_io_read_beyond_eof),
    };

    const struct CMUnitTest mixed_tests[] = {
        cmocka_unit_test(test_io_mixed_write_read),
    };

    int failed = 0;

    failed += cmocka_run_group_tests_name("File Open/Close Tests",
                                          file_open_close_tests, group_setup, NULL);
    failed += cmocka_run_group_tests_name("Integer I/O Tests (Big-Endian)",
                                          integer_be_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Chunk ID Tests",
                                          chunk_id_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Position Tests",
                                          position_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("String Tests",
                                          string_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Raw Byte Tests",
                                          byte_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Padding Tests",
                                          padding_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Error Condition Tests",
                                          error_tests, NULL, NULL);
    failed += cmocka_run_group_tests_name("Mixed Operation Tests",
                                          mixed_tests, NULL, group_teardown);

    return failed;
}
