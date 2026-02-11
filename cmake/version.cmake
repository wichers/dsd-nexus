# cmake/version.cmake
# Provides configure_version_header() -- a single function every library or
# application target can call to generate a version.h from the shared
# template at <root>/include/version.h.in.
#
# Usage:
#   configure_version_header(
#       TARGET      dsdpipe                 # CMake target to attach include dir to
#       PREFIX      DSDPIPE_                # C macro prefix (trailing _ included)
#       GUARD       DSDPIPE_VERSION_H       # Include-guard macro name
#       PRODUCT     "libdsdpipe"            # Human-readable product name
#       VERSION     ${PROJECT_VERSION}      # Semver string "major.minor.patch"
#       OUTPUT_DIR  ${CMAKE_CURRENT_BINARY_DIR}/include/libdsdpipe
#   )

# ── Resolve the template path once at include-time ───────────────────────────
# CMAKE_SOURCE_DIR always points to the top-level source, even inside
# subprojects that declare their own project().
set(_VERSION_TEMPLATE "${CMAKE_SOURCE_DIR}/include/version.h.in")

# ── Git metadata (resolved once) ────────────────────────────────────────────
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} remote get-url origin
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _GIT_REPO_URL
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(_GIT_COMMIT_HASH "unknown")
    set(_GIT_REPO_URL    "unknown")
endif()

# ── Public helper ────────────────────────────────────────────────────────────
function(configure_version_header)
    cmake_parse_arguments(
        VER                             # prefix
        ""                              # options  (none)
        "TARGET;PREFIX;GUARD;PRODUCT;VERSION;OUTPUT_DIR"  # one-value keywords
        ""                              # multi-value keywords (none)
        ${ARGN}
    )

    # ── Validate required args ───────────────────────────────────────────
    foreach(_arg TARGET PREFIX GUARD PRODUCT VERSION OUTPUT_DIR)
        if(NOT DEFINED VER_${_arg} OR VER_${_arg} STREQUAL "")
            message(FATAL_ERROR "configure_version_header: ${_arg} is required")
        endif()
    endforeach()

    # ── Split version string ─────────────────────────────────────────────
    string(REPLACE "." ";" _ver_parts "${VER_VERSION}")
    list(LENGTH _ver_parts _ver_len)

    list(GET _ver_parts 0 VERSION_MAJOR)
    if(_ver_len GREATER 1)
        list(GET _ver_parts 1 VERSION_MINOR)
    else()
        set(VERSION_MINOR 0)
    endif()
    if(_ver_len GREATER 2)
        list(GET _ver_parts 2 VERSION_PATCH)
    else()
        set(VERSION_PATCH 0)
    endif()

    # ── Set all variables expected by version.h.in ───────────────────────
    set(VERSION_GUARD   "${VER_GUARD}")
    set(VERSION_PREFIX  "${VER_PREFIX}")
    set(VERSION_STRING  "${VER_VERSION}")
    set(VERSION_PRODUCT "${VER_PRODUCT}")
    set(GIT_COMMIT_HASH "${_GIT_COMMIT_HASH}")
    set(GIT_REPO_URL    "${_GIT_REPO_URL}")

    # ── Generate ─────────────────────────────────────────────────────────
    configure_file(
        ${_VERSION_TEMPLATE}
        ${VER_OUTPUT_DIR}/version.h
        @ONLY
    )

    # ── Expose the generated header to the target ────────────────────────
    # PUBLIC: consumers use #include <libfoo/version.h> via OUTPUT_DIR/..
    # PRIVATE: internal sources use #include "version.h" via OUTPUT_DIR
    target_include_directories(${VER_TARGET}
        PUBLIC
            $<BUILD_INTERFACE:${VER_OUTPUT_DIR}/..>
        PRIVATE
            ${VER_OUTPUT_DIR}
    )
endfunction()
