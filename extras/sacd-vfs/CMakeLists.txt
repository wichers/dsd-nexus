# =============================================================================
# sacd-vfs - Cross-platform FUSE adapter for SACD Overlay VFS
#
# Supports:
#   - Linux/macOS: Native libfuse3
#   - Windows: WinFSP FUSE compatibility layer
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# =============================================================================
# Platform Detection and FUSE Integration
# =============================================================================

if(WIN32)
    # Windows: Use WinFSP FUSE compatibility layer
    message(STATUS "sacd-vfs: Configuring for Windows (WinFSP)")

    # Option to specify WinFSP location
    option(WINFSP_PATH "Path to WinFSP installation" "")

    # Search for WinFSP
    if(WINFSP_PATH)
        set(WINFSP_SEARCH_PATHS "${WINFSP_PATH}")
    else()
        set(WINFSP_SEARCH_PATHS
            "$ENV{ProgramFiles\(x86\)}/WinFsp"
            "$ENV{ProgramFiles}/WinFsp"
            "$ENV{ProgramW6432}/WinFsp"
            "${CMAKE_SOURCE_DIR}/external/winfsp"
        )
    endif()

    # Find WinFSP include directory
    find_path(WINFSP_INCLUDE_DIR
        NAMES fuse3/fuse.h fuse/fuse.h
        PATHS ${WINFSP_SEARCH_PATHS}
        PATH_SUFFIXES inc include
    )

    # Find WinFSP library
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(WINFSP_LIB_NAME "winfsp-x64")
        set(WINFSP_LIB_SUFFIX "lib")
    else()
        set(WINFSP_LIB_NAME "winfsp-x86")
        set(WINFSP_LIB_SUFFIX "lib")
    endif()

    find_library(WINFSP_LIBRARY
        NAMES ${WINFSP_LIB_NAME}
        PATHS ${WINFSP_SEARCH_PATHS}
        PATH_SUFFIXES ${WINFSP_LIB_SUFFIX}
    )

    if(NOT WINFSP_INCLUDE_DIR OR NOT WINFSP_LIBRARY)
        message(STATUS "sacd-vfs: WinFSP not found")
        message(STATUS "  Searched in: ${WINFSP_SEARCH_PATHS}")
        message(STATUS "  Install WinFSP from https://winfsp.dev/ or set WINFSP_PATH")
        message(STATUS "  Skipping sacd-vfs build")
        return()
    endif()

    message(STATUS "sacd-vfs: Found WinFSP")
    message(STATUS "  Include: ${WINFSP_INCLUDE_DIR}")
    message(STATUS "  Library: ${WINFSP_LIBRARY}")

    set(FUSE_INCLUDE_DIRS "${WINFSP_INCLUDE_DIR}")
    set(FUSE_LIBRARIES "${WINFSP_LIBRARY}")
    set(FUSE_DEFINITIONS "")

else()
    # Unix: Use native libfuse3 (or macFUSE on macOS)
    if(APPLE)
        message(STATUS "sacd-vfs: Configuring for macOS (macFUSE)")
    else()
        message(STATUS "sacd-vfs: Configuring for Unix (libfuse3)")
    endif()

    # Option to use installed FUSE vs fetched source (headers only)
    option(FUSE_USE_INSTALLED "Use installed libfuse3 instead of fetching from GitHub" ON)

    if(FUSE_USE_INSTALLED)
        # Use system-installed FUSE (recommended)
        find_package(PkgConfig QUIET)
        if(NOT PkgConfig_FOUND)
            message(STATUS "sacd-vfs: pkg-config not found, cannot detect FUSE")
            message(STATUS "  Install pkg-config or set -DFUSE_USE_INSTALLED=OFF")
            message(STATUS "  Skipping sacd-vfs build")
            return()
        endif()

        # Try fuse3 first, then macFUSE (fuse-t), then fuse
        pkg_check_modules(FUSE3 fuse3)
        if(NOT FUSE3_FOUND AND APPLE)
            # macFUSE installs as "fuse" via pkg-config at /usr/local/lib/pkgconfig
            # or via Homebrew at typical brew prefixes
            set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/local/lib/pkgconfig:/opt/homebrew/lib/pkgconfig")
            pkg_check_modules(FUSE3 fuse)
        endif()

        if(NOT FUSE3_FOUND)
            message(STATUS "sacd-vfs: FUSE library not found")
            if(APPLE)
                message(STATUS "  Install macFUSE from https://osxfuse.github.io/")
                message(STATUS "  Or install FUSE-T: brew install fuse-t")
            else()
                message(STATUS "  Install with: sudo apt install libfuse3-dev (Debian/Ubuntu)")
                message(STATUS "                sudo dnf install fuse3-devel (Fedora)")
            endif()
            message(STATUS "  Or use -DFUSE_USE_INSTALLED=OFF to fetch headers from GitHub")
            message(STATUS "  Skipping sacd-vfs build")
            return()
        endif()

        message(STATUS "sacd-vfs: Using installed FUSE")
        message(STATUS "  Include: ${FUSE3_INCLUDE_DIRS}")
        message(STATUS "  Library: ${FUSE3_LIBRARIES}")

        set(FUSE_INCLUDE_DIRS ${FUSE3_INCLUDE_DIRS})
        set(FUSE_LIBRARIES ${FUSE3_LIBRARIES})
        set(FUSE_DEFINITIONS ${FUSE3_CFLAGS_OTHER})

    else()
        # Fetch libfuse from GitHub for headers
        include(FetchContent)

        message(STATUS "sacd-vfs: Fetching libfuse from GitHub (headers only)...")
        message(STATUS "  NOTE: Runtime still requires system-installed libfuse3!")

        FetchContent_Declare(
            libfuse
            GIT_REPOSITORY https://github.com/libfuse/libfuse.git
            GIT_TAG        fuse-3.16.2
            GIT_SHALLOW    TRUE
            GIT_PROGRESS   TRUE
        )

        FetchContent_MakeAvailable(libfuse)

        set(FUSE_INCLUDE_DIRS "${libfuse_SOURCE_DIR}/include")
        message(STATUS "sacd-vfs: libfuse headers at ${FUSE_INCLUDE_DIRS}")

        # Still need to link against system libfuse3
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(FUSE3_SYS fuse3 QUIET)
            if(FUSE3_SYS_FOUND)
                set(FUSE_LIBRARIES ${FUSE3_SYS_LIBRARIES})
                set(FUSE_DEFINITIONS ${FUSE3_SYS_CFLAGS_OTHER})
            else()
                message(WARNING "sacd-vfs: System libfuse3 not found. Binary will fail to link.")
                set(FUSE_LIBRARIES "fuse3")
                set(FUSE_DEFINITIONS "")
            endif()
        else()
            set(FUSE_LIBRARIES "fuse3")
            set(FUSE_DEFINITIONS "")
        endif()
    endif()
endif()

# =============================================================================
# Build Configuration
# =============================================================================

# Source files
set(SACD_VFS_SOURCES
    fuse_main.c
    fuse_ops.c
)

# Create executable
add_executable(sacd-vfs
    ${SACD_VFS_SOURCES}
)

# Include directories
target_include_directories(sacd-vfs
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${FUSE_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/libs
)

# Compile definitions
target_compile_definitions(sacd-vfs
    PRIVATE
        FUSE_USE_VERSION=35
        _FILE_OFFSET_BITS=64
        SACD_NO_DLL
        ${FUSE_DEFINITIONS}
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(sacd-vfs
        PRIVATE
            _CRT_SECURE_NO_WARNINGS
            WIN32_LEAN_AND_MEAN
    )
endif()

# Link libraries (statically linked via OBJECT libraries, not the full libdsd umbrella)
# OBJECT libraries don't propagate object files transitively (requires CMake 3.21+),
# so every OBJECT lib in the dependency chain must be listed explicitly.
# Dependency chain: libsacdvfs -> libsacd, libdst -> [libps3drive -> libsg3] -> sautil
set(_sacd_vfs_libs libsacdvfs libsacd libdst sautil)
if(BUILD_PS3DRIVE)
    list(APPEND _sacd_vfs_libs libps3drive libsg3)
endif()
set(_sacd_vfs_ext_deps id3dev)
if(BUILD_PS3DRIVE)
    list(APPEND _sacd_vfs_ext_deps nanopb tinycsocket)
endif()
target_link_libraries(sacd-vfs
    PRIVATE
        ${_sacd_vfs_libs}
        ${FUSE_LIBRARIES}
        # External deps of OBJECT libraries that don't propagate transitively
        ${_sacd_vfs_ext_deps}
)
if(NOT BUILD_PS3DRIVE)
    target_compile_definitions(sacd-vfs PRIVATE SACD_NO_PS3DRIVE)
endif()
if(TARGET mbedcrypto)
    target_link_libraries(sacd-vfs PRIVATE mbedcrypto)
elseif(TARGET MbedTLS::mbedcrypto)
    target_link_libraries(sacd-vfs PRIVATE MbedTLS::mbedcrypto)
endif()


# Compiler flags
if(FUSE_CFLAGS)
    target_compile_options(sacd-vfs
        PRIVATE
            ${FUSE_CFLAGS}
    )
endif()

# Set output directory
set_target_properties(sacd-vfs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin/RelWithDebInfo
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin/MinSizeRel
)

# =============================================================================
# Installation
# =============================================================================

install(TARGETS sacd-vfs
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT dsd_clitools
)

# Also install under sacd_vfs component for standalone Linux packaging
if(UNIX AND NOT APPLE)
    install(TARGETS sacd-vfs
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT sacd_vfs
    )
endif()

# =============================================================================
# Usage Information
# =============================================================================

message(STATUS "")
message(STATUS "sacd-vfs configuration:")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
if(WIN32)
    message(STATUS "  FUSE layer: WinFSP")
else()
    message(STATUS "  FUSE layer: libfuse3")
endif()
message(STATUS "  FUSE headers: ${FUSE_INCLUDE_DIRS}")
message(STATUS "  FUSE library: ${FUSE_LIBRARIES}")
message(STATUS "")
