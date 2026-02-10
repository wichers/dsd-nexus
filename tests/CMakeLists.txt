# =============================================================================
# CMocka Setup for Unit Testing
# =============================================================================
include(FetchContent)

FetchContent_Declare(
    cmocka
    GIT_REPOSITORY https://git.cryptomilk.org/projects/cmocka.git
    GIT_TAG cmocka-2.0.1
    GIT_SHALLOW TRUE
)

# Disable cmocka examples and tests
set(WITH_EXAMPLES OFF CACHE BOOL "" FORCE)
set(UNIT_TESTING OFF CACHE BOOL "" FORCE)

# Force cmocka to build as static (tests link statically)
set(_saved_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
# cmocka's CPackConfig.cmake may include CPack; this is harmless because
# cmake/packaging.cmake re-includes CPack with our own configuration later.
FetchContent_MakeAvailable(cmocka)
set(BUILD_SHARED_LIBS ${_saved_BUILD_SHARED_LIBS})

# Disable warnings for cmocka (third-party library)
if(TARGET cmocka)
    if(MSVC)
        target_compile_options(cmocka PRIVATE /W0)
    else()
        target_compile_options(cmocka PRIVATE -w)
    endif()
endif()

# =============================================================================
# Library Private Include Directories (for testing internal functions)
# =============================================================================
set(LIBDSDIFF_PRIVATE_DIR ${CMAKE_SOURCE_DIR}/libs/libdsdiff/src)
set(LIBDSF_PRIVATE_DIR ${CMAKE_SOURCE_DIR}/libs/libdsf/src)
set(LIBSACD_PRIVATE_DIR ${CMAKE_SOURCE_DIR}/libs/libsacd/src)
set(LIBSACDVFS_PRIVATE_DIR ${CMAKE_SOURCE_DIR}/libs/libsacdvfs/src)
set(LIBDST_PRIVATE_DIR ${CMAKE_SOURCE_DIR}/libs/libdst/src)
set(LIBSAUTIL_DIR ${CMAKE_SOURCE_DIR}/libs/libsautil)

# =============================================================================
# Test Executables
# =============================================================================

# Test executable for dsdiff_markers
add_executable(test_dsdiff_markers
    test_dsdiff_markers.c
)

# Link against libdsdiff library and cmocka
target_link_libraries(test_dsdiff_markers PRIVATE libdsd_static cmocka)

# Include cmocka headers and library private directories
target_include_directories(test_dsdiff_markers PRIVATE
    ${cmocka_SOURCE_DIR}/include
    ${LIBDSDIFF_PRIVATE_DIR}
)

# Set output directory for test executable
set_target_properties(test_dsdiff_markers PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Add test to CTest
add_test(NAME dsdiff_markers_test COMMAND test_dsdiff_markers)

# Set working directory for the test
set_tests_properties(dsdiff_markers_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(test_dsdiff_markers PRIVATE /W4)
endif()

# Test executable for dsdiff_io
add_executable(test_dsdiff_io
    test_dsdiff_io.c
)

# Link against libdsdiff library and cmocka
target_link_libraries(test_dsdiff_io PRIVATE libdsd_static cmocka)

# Include cmocka headers and library private directories
target_include_directories(test_dsdiff_io PRIVATE
    ${cmocka_SOURCE_DIR}/include
    ${LIBDSDIFF_PRIVATE_DIR}
)

# Set output directory for test executable
set_target_properties(test_dsdiff_io PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Add test to CTest
add_test(NAME dsdiff_io_test COMMAND test_dsdiff_io)

# Set working directory for the test
set_tests_properties(dsdiff_io_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(test_dsdiff_io PRIVATE /W4)
endif()

# Test executable for dsdiff_file
add_executable(test_dsdiff_file
    test_dsdiff_file.c
)

# Link against libdsdiff library and cmocka
target_link_libraries(test_dsdiff_file PRIVATE libdsd_static cmocka)

# Include cmocka headers and library private directories
target_include_directories(test_dsdiff_file PRIVATE
    ${cmocka_SOURCE_DIR}/include
    ${LIBDSDIFF_PRIVATE_DIR}
)

# Set output directory for test executable
set_target_properties(test_dsdiff_file PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Add test to CTest
add_test(NAME dsdiff_file_test COMMAND test_dsdiff_file)

# Set working directory for the test
set_tests_properties(dsdiff_file_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(test_dsdiff_file PRIVATE /W4)
endif()

# Test executable for dsdiff_read
add_executable(test_dsdiff_read
    test_dsdiff_read.c
)

# Link against libdsdiff library and cmocka
target_link_libraries(test_dsdiff_read PRIVATE libdsd_static cmocka)

# Include cmocka headers and library private directories
target_include_directories(test_dsdiff_read PRIVATE
    ${cmocka_SOURCE_DIR}/include
    ${LIBDSDIFF_PRIVATE_DIR}
)

# Set output directory for test executable
set_target_properties(test_dsdiff_read PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Add test to CTest
add_test(NAME dsdiff_read_test COMMAND test_dsdiff_read)

# Set working directory for the test
set_tests_properties(dsdiff_read_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(test_dsdiff_read PRIVATE /W4)
endif()

# Test executable for dsdiff_modify
add_executable(test_dsdiff_modify
    test_dsdiff_modify.c
)

# Link against libdsdiff library and cmocka
target_link_libraries(test_dsdiff_modify PRIVATE libdsd_static cmocka)

# Include cmocka headers and library private directories
target_include_directories(test_dsdiff_modify PRIVATE
    ${cmocka_SOURCE_DIR}/include
    ${LIBDSDIFF_PRIVATE_DIR}
)

# Set output directory for test executable
set_target_properties(test_dsdiff_modify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Add test to CTest
add_test(NAME dsdiff_modify_test COMMAND test_dsdiff_modify)

# Set working directory for the test
set_tests_properties(dsdiff_modify_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(test_dsdiff_modify PRIVATE /W4)
endif()

# Test executable for dst_write
add_executable(test_dst_write
    test_dst_write.c
)

# Link against libdsdiff library and cmocka
target_link_libraries(test_dst_write PRIVATE libdsd_static cmocka)

# Include cmocka headers and library private directories
target_include_directories(test_dst_write PRIVATE
    ${cmocka_SOURCE_DIR}/include
    ${LIBDSDIFF_PRIVATE_DIR}
    ${LIBDST_PRIVATE_DIR}
)

# Set output directory for test executable
set_target_properties(test_dst_write PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Add test to CTest
add_test(NAME dst_write_test COMMAND test_dst_write)

# Set working directory for the test
set_tests_properties(dst_write_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(test_dst_write PRIVATE /W4)
endif()

# Test executable for dsdiff_write
add_executable(test_dsdiff_write
    test_dsdiff_write.c
)

# Link against libdsdiff library and cmocka
target_link_libraries(test_dsdiff_write PRIVATE libdsd_static cmocka)

# Include cmocka headers and library private directories
target_include_directories(test_dsdiff_write PRIVATE
    ${cmocka_SOURCE_DIR}/include
    ${LIBDSDIFF_PRIVATE_DIR}
)

# Set output directory for test executable
set_target_properties(test_dsdiff_write PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Add test to CTest
add_test(NAME dsdiff_write_test COMMAND test_dsdiff_write)

# Set working directory for the test
set_tests_properties(dsdiff_write_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(test_dsdiff_write PRIVATE /W4)
endif()

# =============================================================================
# CMocka-based Test: dsf_file (Unit Tests with CMocka)
# =============================================================================
add_executable(test_dsf_file
    test_dsf_file.c
)

# Link against libdsf and cmocka
target_link_libraries(test_dsf_file PRIVATE libdsd_static cmocka)

# Include cmocka headers and library private directories
target_include_directories(test_dsf_file PRIVATE
    ${cmocka_SOURCE_DIR}/include
    ${LIBDSF_PRIVATE_DIR}
)

# Set output directory for test executable
set_target_properties(test_dsf_file PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Add test to CTest
add_test(NAME dsf_file_test COMMAND test_dsf_file)

# Set working directory for the test
set_tests_properties(dsf_file_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(test_dsf_file PRIVATE /W4)
endif()

# =============================================================================
# CMocka-based Test: sacd_vfs (SACD Virtual Filesystem Tests)
# =============================================================================
add_executable(test_sacd_vfs
    test_sacd_vfs.c
)

# Link against libsacdvfs and cmocka
target_link_libraries(test_sacd_vfs PRIVATE libdsd_static cmocka)

# Include cmocka headers and library private directories
target_include_directories(test_sacd_vfs PRIVATE
    ${cmocka_SOURCE_DIR}/include
    ${LIBSACDVFS_PRIVATE_DIR}
    ${SAUTIL_CONFIG_PATH}
)

# Set output directory for test executable
set_target_properties(test_sacd_vfs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Add test to CTest
add_test(NAME sacd_vfs_test COMMAND test_sacd_vfs)

# Set working directory for the test
set_tests_properties(sacd_vfs_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(test_sacd_vfs PRIVATE /W4)
endif()

# =============================================================================
# Verification Tool: verify_vfs_dsd (Compare VFS output with reference files)
# =============================================================================
add_executable(verify_vfs_dsd
    verify_vfs_dsd.c
)

# Link against libsacdvfs and sautil (for sa_tpool in benchmark mode)
target_link_libraries(verify_vfs_dsd PRIVATE libdsd_static)

# Include library headers
target_include_directories(verify_vfs_dsd PRIVATE
    ${LIBSACDVFS_PRIVATE_DIR}
    ${SAUTIL_CONFIG_PATH}
)

# Set output directory
set_target_properties(verify_vfs_dsd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(verify_vfs_dsd PRIVATE /W4)
endif()

# =============================================================================
# Test Tool: test_dst_reader (DST Reader Seeking and Sequential Reading Tests)
# =============================================================================
add_executable(test_dst_reader
    test_dst_reader.c
)

# Link against libsacd
target_link_libraries(test_dst_reader PRIVATE libdsd_static)

# Include library headers
target_include_directories(test_dst_reader PRIVATE
    ${LIBSACD_PRIVATE_DIR}
    ${SAUTIL_CONFIG_PATH}
)

# Set output directory
set_target_properties(test_dst_reader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(test_dst_reader PRIVATE /W4)
endif()

# =============================================================================
# CMocka-based Test: sacd_vfs_id3 (SACD VFS ID3 Tag Modification Tests)
# =============================================================================
add_executable(test_sacd_vfs_id3
    test_sacd_vfs_id3.c
)

# Link against libsacdvfs, sautil, and cmocka
target_link_libraries(test_sacd_vfs_id3 PRIVATE libdsd_static cmocka)

# Include cmocka headers and library private directories
target_include_directories(test_sacd_vfs_id3 PRIVATE
    ${cmocka_SOURCE_DIR}/include
    ${LIBSACDVFS_PRIVATE_DIR}
    ${SAUTIL_CONFIG_PATH}
)

# Set output directory for test executable
set_target_properties(test_sacd_vfs_id3 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# Add test to CTest
add_test(NAME sacd_vfs_id3_test COMMAND test_sacd_vfs_id3)

# Set working directory for the test
set_tests_properties(sacd_vfs_id3_test PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(test_sacd_vfs_id3 PRIVATE /W4)
endif()
