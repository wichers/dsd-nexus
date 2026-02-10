# libdsdpipe - DSD Audio Pipeline Library
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)

project(libdsdpipe
    VERSION 1.0.0
    LANGUAGES C
    DESCRIPTION "DSD Audio Pipeline Library"
)

include(FetchContent)

# Option for standalone/testing mode (no external library dependencies)
option(DSDPIPE_STANDALONE "Build without external library dependencies (for testing)" OFF)

# Library sources
set(DSDPIPE_SOURCES
    src/dsdpipe.c
    src/track_select.c
    src/metadata.c
    src/metadata_tags.c
    src/id3_parser.c
    src/frame_queue.c
    src/reader_thread.c
    src/source_sacd.c
    src/source_dsdiff.c
    src/source_dsf.c
    src/sink_dsf.c
    src/sink_dsdiff.c
    src/sink_wav.c
    src/sink_flac.c
    src/sink_print.c
    src/sink_cuesheet.c
    src/sink_xml.c
    src/sink_id3.c
    src/transform_dst.c
    src/transform_dsd2pcm.c
)

# Create OBJECT library (combined into umbrella libdsd)
add_library(dsdpipe OBJECT ${DSDPIPE_SOURCES})

# Include directories
target_include_directories(dsdpipe
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        # Include paths for dependency headers (needed for source files)
        ${CMAKE_SOURCE_DIR}/libs/libsacd/include
        ${CMAKE_SOURCE_DIR}/libs/libdsf/include
        ${CMAKE_SOURCE_DIR}/libs/libdsdiff/include
        ${CMAKE_SOURCE_DIR}/libs/libdst/include
        ${CMAKE_SOURCE_DIR}/libs/libdsdpcm/include
)

# Link dependencies
if(DSDPIPE_STANDALONE)
    message(STATUS "libdsdpipe: Building in STANDALONE mode (stub implementations)")
    target_compile_definitions(dsdpipe PRIVATE DSDPIPE_STANDALONE=1)
    # Only link sautil for buffer_pool
    target_link_libraries(dsdpipe PUBLIC sautil)
else()
    # Full dependencies
    target_link_libraries(dsdpipe
        PUBLIC
            sautil
        PRIVATE
            libsacd
            libdsdiff
            libdsf
            libdst
            id3dev
    )
    # libdsdpcm for DSD-to-PCM conversion
    if(TARGET libdsdpcm)
        target_link_libraries(dsdpipe PRIVATE libdsdpcm)
    endif()
endif()

# =============================================================================
# Optional FLAC Support (for FLAC output in sink_flac.c)
# =============================================================================
# Try system FLAC first, then fetch from GitHub if not found
option(DSDPIPE_FETCH_FLAC "Fetch FLAC from GitHub if not found on system" ON)

if(NOT DSDPIPE_STANDALONE)
    # Try CMake config first (Windows, installed FLAC)
    find_package(FLAC CONFIG QUIET)
    if(FLAC_FOUND)
        target_link_libraries(dsdpipe PRIVATE FLAC::FLAC)
        target_compile_definitions(dsdpipe PRIVATE HAVE_LIBFLAC=1)
        message(STATUS "libdsdpipe: FLAC support enabled (system CMake config)")
    else()
        # Try pkg-config (Linux system packages like libflac-dev)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(FLAC QUIET flac)
            if(FLAC_FOUND)
                target_link_libraries(dsdpipe PRIVATE ${FLAC_LIBRARIES})
                target_include_directories(dsdpipe PRIVATE ${FLAC_INCLUDE_DIRS})
                target_compile_definitions(dsdpipe PRIVATE HAVE_LIBFLAC=1)
                message(STATUS "libdsdpipe: FLAC support enabled (system pkg-config: ${FLAC_LIBRARIES})")
            endif()
        endif()
    endif()

    # Fall back to FetchContent if system FLAC not found
    if(NOT FLAC_FOUND AND DSDPIPE_FETCH_FLAC)
        # Fetch FLAC from GitHub
        message(STATUS "libdsdpipe: System FLAC not found, fetching from GitHub...")
        FetchContent_Declare(
            flac
            GIT_REPOSITORY https://github.com/xiph/flac.git
            GIT_TAG        1.4.3
            GIT_SHALLOW    TRUE
        )

        # FLAC build options - disable unnecessary components
        set(BUILD_CXXLIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
        set(WITH_OGG OFF CACHE BOOL "" FORCE)
        set(WITH_STACK_PROTECTOR OFF CACHE BOOL "" FORCE)
        set(BUILD_UTILS OFF CACHE BOOL "" FORCE)

        # Keep FLAC as static (third-party dependency)
        set(_saved_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF)
        FetchContent_MakeAvailable(flac)
        set(BUILD_SHARED_LIBS ${_saved_BUILD_SHARED_LIBS})

        # Suppress warnings for all FLAC targets (external library)
        if(TARGET FLAC)
            set(_flac_targets FLAC FLAC++)
            # Also suppress warnings for FLAC support libraries if they exist
            if(TARGET grabbag)
                list(APPEND _flac_targets grabbag)
            endif()
            if(TARGET replaygain_analysis)
                list(APPEND _flac_targets replaygain_analysis)
            endif()
            if(TARGET replaygain_synthesis)
                list(APPEND _flac_targets replaygain_synthesis)
            endif()
            if(TARGET getopt)
                list(APPEND _flac_targets getopt)
            endif()
            if(TARGET utf8)
                list(APPEND _flac_targets utf8)
            endif()
            if(TARGET benchmark_residual)
                list(APPEND _flac_targets benchmark_residual)
            endif()
            if(TARGET benchmark_lpc)
                list(APPEND _flac_targets benchmark_lpc)
            endif()

            foreach(_target ${_flac_targets})
                if(TARGET ${_target})
                    if(MSVC)
                        target_compile_options(${_target} PRIVATE /W0)
                    else()
                        target_compile_options(${_target} PRIVATE -w)
                    endif()
                endif()
            endforeach()

            target_link_libraries(dsdpipe PRIVATE FLAC)
            target_compile_definitions(dsdpipe PRIVATE HAVE_LIBFLAC=1)
            set(FLAC_FOUND TRUE)
            message(STATUS "libdsdpipe: FLAC support enabled (fetched from GitHub)")
        else()
            message(WARNING "libdsdpipe: Failed to create FLAC target from FetchContent")
        endif()
    endif()

    # Final status if FLAC still not found
    if(NOT FLAC_FOUND)
        message(STATUS "libdsdpipe: FLAC support disabled (libFLAC not found)")
    endif()
endif()

# =============================================================================
# dr_wav Support (for WAV output in sink_wav.c)
# =============================================================================
# dr_wav is a single-header library; implementation is compiled in sink_wav.c
target_include_directories(dsdpipe PRIVATE ${CMAKE_SOURCE_DIR}/external/dr_libs)
message(STATUS "libdsdpipe: WAV support enabled (dr_wav)")

# Compiler settings
target_compile_features(dsdpipe PUBLIC c_std_11)

if(MSVC)
    target_compile_definitions(dsdpipe PRIVATE
        _CRT_SECURE_NO_WARNINGS
        __STDC_WANT_LIB_EXT1__=1
    )
    target_compile_options(dsdpipe PRIVATE /W4 /std:c11)
else()
    # Match FFmpeg's default flags (no -Wpedantic unless --enable-extra-warnings)
    target_compile_options(dsdpipe PRIVATE -Wall -Wextra)
    target_compile_definitions(dsdpipe PRIVATE
        __STDC_WANT_LIB_EXT1__=1
    )
endif()

# Version configuration
configure_version_header(
    TARGET      dsdpipe
    PREFIX      DSDPIPE_
    GUARD       DSDPIPE_VERSION_H
    PRODUCT     "libdsdpipe"
    VERSION     ${PROJECT_VERSION}
    OUTPUT_DIR  ${CMAKE_CURRENT_BINARY_DIR}/include/libdsdpipe
)

# Export macros and installation handled by umbrella library (libdsd)


# Export macro control (for dsdpipe_export.h)
# Note: USING_DLL is set on the umbrella library's INTERFACE, not here.
if(BUILD_SHARED_LIBS)
    target_compile_definitions(dsdpipe PRIVATE DSDPIPE_BUILDING_DLL)
endif()
