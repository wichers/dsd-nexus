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
    # Unix: Use native libfuse3
    message(STATUS "sacd-vfs: Configuring for Unix (libfuse3)")

    # Option to use installed FUSE vs fetched source (headers only)
    option(FUSE_USE_INSTALLED "Use installed libfuse3 instead of fetching from GitHub" ON)

    if(FUSE_USE_INSTALLED)
        # Use system-installed libfuse3 (recommended)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FUSE3 fuse3)

        if(NOT FUSE3_FOUND)
            message(STATUS "sacd-vfs: libfuse3 not found")
            message(STATUS "  Install with: sudo apt install libfuse3-dev (Debian/Ubuntu)")
            message(STATUS "                sudo dnf install fuse3-devel (Fedora)")
            message(STATUS "                brew install macfuse (macOS)")
            message(STATUS "  Or use -DFUSE_USE_INSTALLED=OFF to fetch headers from GitHub")
            return()
        endif()

        message(STATUS "sacd-vfs: Using installed libfuse3")
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
# Dependency chain: libsacdvfs -> libsacd, libdst -> libps3drive -> libsg3 -> sautil
target_link_libraries(sacd-vfs
    PRIVATE
        libsacdvfs
        libsacd
        libdst
        libps3drive
        libsg3
        sautil
        ${FUSE_LIBRARIES}
        # External deps of OBJECT libraries that don't propagate transitively
        nanopb
        tinycsocket
        id3dev
)
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
