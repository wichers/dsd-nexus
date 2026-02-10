cmake_minimum_required(VERSION 3.20)

# Suppress CMake deprecation warnings from third-party libraries (mbedtls, cmocka)
# These libraries may use older cmake_minimum_required() versions
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)

# Enable compile_commands.json for Makefile/Ninja generators.
# Disabled for Visual Studio generator: VS doesn't use it and CMake's symlink
# creation in fetched dependency dirs fails on Windows without Developer Mode.
if(NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)
endif()

project(dsd-nexus VERSION 0.1.81 LANGUAGES C CXX)

# =============================================================================
# Pitchfork Layout (PFL) Project Structure
# =============================================================================
# libs/           - Main project submodules (PFL 2.11)
#   libsacd/      - SACD ISO/Network reader library
#   libdst/       - DST decoder library
#   libdsf/       - DSF container library
#   libdsdiff/    - DSDIFF container library
#   libsautil/    - Utility library
# extras/         - Optional submodules (PFL 2.11)
#   sacd-extract/ - Extraction utility
#   dsd_convert/  - Conversion utility
# tests/          - Project-wide integration tests (PFL 2.4)
# examples/       - Sample usage programs (PFL 2.5)
# external/       - Third-party code (PFL 2.6)
# tools/          - Build scripts, CI helpers (PFL 2.8)
# docs/           - Documentation (PFL 2.9)
# build/          - Ephemeral build artifacts (PFL 2.1)
# =============================================================================

# Include FetchContent module
include(FetchContent)

# Unified version header generation (cmake/version.cmake)
include(cmake/version.cmake)

# Note: CPack guard for cmocka is in tests/CMakeLists.txt (the only dep that
# includes CPack). Our packaging configuration is in cmake/packaging.cmake.

# =============================================================================
# Options (must be declared before FetchContent to control shared/static)
# =============================================================================
option(BUILD_SHARED_LIBS "Build shared libraries (required for LGPL compliance)" ON)
option(BUILD_EXTRAS "Build optional tools (sacd-extract, dsd_convert)" ON)
option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test suite" ON)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)

# Symbol export is handled by per-library *_API macros (__declspec(dllexport/dllimport)).
# CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS is OFF because the cmake __create_def tool
# can crash with large mixed C/C++ object sets; explicit macros are more reliable.

# Ensure all libraries are built with position-independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Fetch ID3dev (third-party: keep as static)
FetchContent_Declare(
    id3dev
    GIT_REPOSITORY https://github.com/definitelyewan/id3dev
    GIT_TAG main
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL
)
set(_saved_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(id3dev)
set(BUILD_SHARED_LIBS ${_saved_BUILD_SHARED_LIBS})

# tinycsocket (cross-platform sockets) - header-only library (vendored)
add_library(tinycsocket INTERFACE)
target_include_directories(tinycsocket INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external/tinycsocket/include)
if(WIN32)
    target_link_libraries(tinycsocket INTERFACE wsock32 ws2_32 iphlpapi)
endif()

# Find packages
if(NOT MSVC)
    find_package(libsafec QUIET)
endif()

# Project metadata
set(PROJECT_DESCRIPTION "Cross-platform C libraries for working with DSD Audio")
set(PROJECT_HOMEPAGE_URL "https://github.com/yourusername/dsd-nexus")

# Note: Options are declared above (before FetchContent) for correct shared/static control

# C standard (C17 to match FFmpeg, fallback to C11)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_C_EXTENSIONS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W4 $<$<COMPILE_LANGUAGE:C>:/std:c17>)

    # Release mode optimizations for MSVC
    # /O2 - Maximum optimization (favor speed)
    # /Oi - Enable intrinsic functions
    # /Ot - Favor fast code
    # /GL - Whole program optimization
    # /GF - Eliminate duplicate strings
    # /Gy - Enable function-level linking
    # /arch:SSE2 is only needed for 32-bit; x64 has SSE2 as the baseline
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /Oi /Ot /GL /GF /Gy /arch:SSE2 /DNDEBUG")
    else()
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /O2 /Oi /Ot /GL /GF /Gy /DNDEBUG")
    endif()
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /OPT:REF /OPT:ICF")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG /OPT:REF /OPT:ICF")
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG")

    # RelWithDebInfo - optimized but with debug info
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} /O2 /Oi /Ot /GF /Gy /Zi /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /DEBUG /OPT:REF /OPT:ICF")
    set(CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO} /DEBUG /OPT:REF /OPT:ICF")

    if(ENABLE_COVERAGE)
        message(WARNING "Code coverage not supported on MSVC")
    endif()
    if(ENABLE_ASAN)
        add_compile_options(/fsanitize=address)
        add_link_options(/fsanitize=address)
    endif()
else()
    # FFmpeg-style GCC/Clang warning flags (common to C and C++)
    add_compile_options(
        # Basic warnings
        -Wall
        -Wdisabled-optimization
        -Wpointer-arith
        -Wredundant-decls
        -Wwrite-strings
        -Wtype-limits
        -Wundef
        -Wempty-body

        # Format warnings
        -Wformat
        -fdiagnostics-color=auto

        # Disable specific warnings for FFmpeg-derived code
        -Wno-parentheses
        -Wno-switch
        -Wno-format-zero-length
        -Wno-unused-const-variable
        -Wno-bool-operation
        -Wno-char-subscripts
        -Wno-maybe-uninitialized

        # Critical warnings as errors
        -Werror=format-security
        -Werror=return-type
        -Werror=vla
    )

    # C-specific flags (append to CMAKE_C_FLAGS to avoid affecting C++)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wmissing-prototypes -Wstrict-prototypes -Wno-pointer-sign -Werror=implicit-function-declaration -Werror=missing-prototypes")

    # Release mode optimizations for GCC/Clang (FFmpeg-style)
    # -O3 - Maximum optimization
    # -fno-math-errno - Faster math (no errno checks)
    # -fno-signed-zeros - IEEE compliance not required
    # -fno-tree-vectorize - Controlled vectorization
    # -fomit-frame-pointer - More registers available
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -fno-math-errno -fno-signed-zeros -fno-tree-vectorize -fomit-frame-pointer -DNDEBUG")

    # FFmpeg-style linker flags
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--as-needed -Wl,-z,noexecstack")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--as-needed -Wl,-z,noexecstack")

    # Strip symbols in release
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -s")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -s")

    # Enable LTO if supported
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_OUTPUT)
    if(IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()

    # RelWithDebInfo - optimized but with debug info
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -O2 -g -DNDEBUG")

    if(ENABLE_COVERAGE)
        add_compile_options(--coverage)
        add_link_options(--coverage)
    endif()
    if(ENABLE_ASAN)
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

# =============================================================================
# External Dependencies (external/)
# =============================================================================
add_subdirectory(external/nanopb)

# Suppress warnings from external library
if(TARGET nanopb)
    if(MSVC)
        target_compile_options(nanopb PRIVATE /W0)
    else()
        target_compile_options(nanopb PRIVATE -w)
    endif()
endif()

# =============================================================================
# Library Subdirectories (libs/)
# =============================================================================
# Order matters: dependencies must be added before dependents
add_subdirectory(libs/libsautil)
add_subdirectory(libs/libsg3)
add_subdirectory(libs/libps3drive)
add_subdirectory(libs/libdsdiff)
add_subdirectory(libs/libdsf)
add_subdirectory(libs/libdst)
add_subdirectory(libs/libdsdpcm)
add_subdirectory(libs/libsacd)
add_subdirectory(libs/libsacdvfs)
add_subdirectory(libs/libdsdpipe)

# libmlt - DSD multimedia pipeline library (converted from MLT framework)
# Set required variables for libmlt CMakeLists.txt
# set(MLT_VERSION_MAJOR 7)
# set(MLT_VERSION_MINOR 37)
# set(MLT_VERSION_REVISION 0)
# set(MLT_VERSION "${MLT_VERSION_MAJOR}.${MLT_VERSION_MINOR}.${MLT_VERSION_REVISION}")
# set(MLT_COMPILE_OPTIONS "")
# set(MLT_INSTALL_DATA_DIR "${CMAKE_INSTALL_DATADIR}/libmlt-${MLT_VERSION_MAJOR}")
# add_subdirectory(libs/libmlt)

# MLT modules - DSD audio module (producers, filters, consumers for DSD formats)
# set(MLT_MODULE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}/mlt-${MLT_VERSION_MAJOR}")
# set(MLT_INSTALL_MODULE_DIR "${CMAKE_INSTALL_LIBDIR}/mlt-${MLT_VERSION_MAJOR}")
# option(BUILD_LIBMLTDSD "Build MLT DSD module (DSD/DSF/SACD producers and filters)" ON)
# if(BUILD_LIBMLTDSD)
#     add_subdirectory(libs/libmltdsd)
# endif()

# =============================================================================
# Umbrella library: libdsd (combines all libs/ into one shared/static library)
# =============================================================================
include(GNUInstallDirs)

add_library(libdsd)
add_library(dsd::dsd ALIAS libdsd)

# Collect all OBJECT libraries (PUBLIC propagates include dirs to consumers)
target_link_libraries(libdsd PUBLIC
    sautil libsg3 libps3drive libdsdiff libdsf libdst
    libdsdpcm libsacd libsacdvfs dsdpipe
)

# External dependencies (PRIVATE to individual OBJECT libs, must be linked here)
target_link_libraries(libdsd PRIVATE dsdpcm_core nanopb id3dev tinycsocket)
if(TARGET mbedcrypto)
    target_link_libraries(libdsd PRIVATE mbedcrypto)
endif()
if(TARGET FLAC::FLAC)
    target_link_libraries(libdsd PRIVATE FLAC::FLAC)
elseif(TARGET FLAC)
    target_link_libraries(libdsd PRIVATE FLAC)
endif()

# C++ linker required (libdsdpcm has .cpp sources)
set_target_properties(libdsd PROPERTIES LINKER_LANGUAGE CXX)

# DLL export/import control
if(BUILD_SHARED_LIBS)
    # When building the umbrella DLL, ALL object libs must define ALL BUILDING
    # macros so that cross-lib header includes resolve to dllexport (not dllimport).
    set(_all_building_defines
        SACD_API_EXPORTS
        PS3DRIVE_BUILDING_DLL DSDPCM_BUILDING_DLL
        DSDIFF_BUILDING_DLL DSF_BUILDING_DLL DST_BUILDING_DLL
        SACDVFS_BUILDING_DLL DSDPIPE_BUILDING_DLL
    )
    foreach(_obj sautil libsg3 libps3drive libdsdiff libdsf libdst
                 libdsdpcm libsacd libsacdvfs dsdpipe)
        target_compile_definitions(${_obj} PRIVATE ${_all_building_defines})
    endforeach()

    # Consumer-side: USING_DLL macros trigger __declspec(dllimport) in headers
    target_compile_definitions(libdsd INTERFACE
        DSDPCM_USING_DLL DSDIFF_USING_DLL DSF_USING_DLL
        DST_USING_DLL SACDVFS_USING_DLL DSDPIPE_USING_DLL
    )
    # sautil: absence of SACD_NO_DLL triggers dllimport automatically
else()
    target_compile_definitions(libdsd PUBLIC SACD_NO_DLL)
endif()

# Output directories
set_target_properties(libdsd PROPERTIES
    OUTPUT_NAME "dsd"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/lib/RelWithDebInfo
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/lib/MinSizeRel
    LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/lib/RelWithDebInfo
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/lib/MinSizeRel
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin/RelWithDebInfo
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin/MinSizeRel
)

# Project-wide version header (DSD_NEXUS_ prefix)
configure_version_header(
    TARGET      libdsd
    PREFIX      DSD_NEXUS_
    GUARD       DSD_NEXUS_VERSION_H
    PRODUCT     "DSD-Nexus"
    VERSION     ${PROJECT_VERSION}
    OUTPUT_DIR  ${CMAKE_BINARY_DIR}/include/dsd-nexus
)

# Install umbrella library
install(TARGETS libdsd
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dsd_development
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT dsd_application
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT dsd_application
)

# Install all public headers from libs/ (those with include/ subdirectories)
foreach(_lib libsautil libsg3 libps3drive libdsdiff libdsf libdst
             libdsdpcm libsacd libsacdvfs libdsdpipe)
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/libs/${_lib}/include)
        install(DIRECTORY libs/${_lib}/include/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            COMPONENT dsd_development
            FILES_MATCHING PATTERN "*.h"
        )
    endif()
endforeach()

# libsautil headers are in libs/libsautil/ directly (not include/ subdirectory)
install(FILES
    libs/libsautil/attributes.h
    libs/libsautil/base64.h
    libs/libsautil/bprint.h
    libs/libsautil/bswap.h
    libs/libsautil/buffer.h
    libs/libsautil/buffer_internal.h
    libs/libsautil/common.h
    libs/libsautil/compat.h
    libs/libsautil/cpu.h
    libs/libsautil/dynarray.h
    libs/libsautil/error.h
    libs/libsautil/export.h
    libs/libsautil/getopt.h
    libs/libsautil/get_bits.h
    libs/libsautil/internal.h
    libs/libsautil/intmath.h
    libs/libsautil/intreadwrite.h
    libs/libsautil/ll.h
    libs/libsautil/log.h
    libs/libsautil/macros.h
    libs/libsautil/mathops.h
    libs/libsautil/mem.h
    libs/libsautil/mem_internal.h
    libs/libsautil/reverse.h
    libs/libsautil/sastring.h
    libs/libsautil/sa_assert.h
    libs/libsautil/sa_path.h
    libs/libsautil/sa_tpool.h
    libs/libsautil/sa_tpool_internal.h
    libs/libsautil/sxmlc.h
    libs/libsautil/time.h
    libs/libsautil/timer.h
    libs/libsautil/time_internal.h
    libs/libsautil/tree.h
    libs/libsautil/va_copy.h
    libs/libsautil/win_utf8_io.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libsautil
    COMPONENT dsd_development
)

# =============================================================================
# Static library for tests: libdsd_static
# Always built as STATIC so tests can link all symbols without DLL export issues.
# =============================================================================
add_library(libdsd_static STATIC)

target_link_libraries(libdsd_static PUBLIC
    sautil libsg3 libps3drive libdsdiff libdsf libdst
    libdsdpcm libsacd libsacdvfs dsdpipe
)

target_link_libraries(libdsd_static PRIVATE dsdpcm_core nanopb id3dev tinycsocket)
if(TARGET mbedcrypto)
    target_link_libraries(libdsd_static PRIVATE mbedcrypto)
endif()
if(TARGET FLAC::FLAC)
    target_link_libraries(libdsd_static PRIVATE FLAC::FLAC)
elseif(TARGET FLAC)
    target_link_libraries(libdsd_static PRIVATE FLAC)
endif()

set_target_properties(libdsd_static PROPERTIES LINKER_LANGUAGE CXX)

# Ensure test code sees empty _API macros (no dllimport)
target_compile_definitions(libdsd_static PUBLIC SACD_NO_DLL)

# Backward-compat aliases so existing targets keep working
add_library(sacd::sacd ALIAS libdsd)
add_library(dsdpipe::dsdpipe ALIAS libdsd)

# =============================================================================
# Extras (extras/)
# =============================================================================
if(BUILD_EXTRAS)
    add_subdirectory(extras)
endif()

# =============================================================================
# Examples (examples/)
# =============================================================================
if(BUILD_EXAMPLES)
     add_subdirectory(examples)
endif()

# =============================================================================
# Tests (tests/)
# =============================================================================
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# =============================================================================
# Installation (pkg-config and CMake config files)
# =============================================================================
# Note: GNUInstallDirs included above (in umbrella library section)

# pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libsacd.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/libsacd.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libsacd.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# CMake config files
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/libsacdConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/libsacdConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libsacd
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/libsacdConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/libsacdConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/libsacdConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/libsacd
)

# Uninstall target
if(NOT TARGET uninstall)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
        IMMEDIATE @ONLY
    )

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    )
endif()

# =============================================================================
# Configuration Summary
# =============================================================================
message(STATUS "")
message(STATUS "dsd-nexus ${PROJECT_VERSION} Configuration Summary")
message(STATUS "==========================================")
message(STATUS "Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "Shared libraries:  ${BUILD_SHARED_LIBS}")
message(STATUS "Build extras:      ${BUILD_EXTRAS}")
message(STATUS "Build examples:    ${BUILD_EXAMPLES}")
message(STATUS "Build tests:       ${BUILD_TESTS}")
message(STATUS "Code coverage:     ${ENABLE_COVERAGE}")
message(STATUS "Address Sanitizer: ${ENABLE_ASAN}")
message(STATUS "Install prefix:    ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")

# =============================================================================
# Packaging (CPack / Qt Installer Framework)
# =============================================================================
include(cmake/packaging.cmake)
