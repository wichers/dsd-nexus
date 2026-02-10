# =============================================================================
# libps3drive - PS3 BluRay Drive Interface Library
# =============================================================================
cmake_minimum_required(VERSION 3.20)

include(FetchContent)

# -----------------------------------------------------------------------------
# Find or Fetch mbedtls (provides mbedcrypto library)
# -----------------------------------------------------------------------------
set(MBEDTLS_FOUND FALSE)

# Try CMake config first (Windows, installed mbedtls)
find_package(MbedTLS CONFIG QUIET)
if(MbedTLS_FOUND)
    message(STATUS "libps3drive: mbedTLS support enabled (system CMake config)")
    set(MBEDTLS_FOUND TRUE)
    set(MBEDCRYPTO_TARGET MbedTLS::mbedcrypto)
else()
    # Try manual detection (Linux system packages like libmbedtls-dev)
    find_path(MBEDTLS_INCLUDE_DIR mbedtls/aes.h)
    find_library(MBEDCRYPTO_LIBRARY mbedcrypto)

    if(MBEDTLS_INCLUDE_DIR AND MBEDCRYPTO_LIBRARY)
        # Create imported target for consistency
        add_library(MbedTLS::mbedcrypto INTERFACE IMPORTED)
        set_target_properties(MbedTLS::mbedcrypto PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "${MBEDCRYPTO_LIBRARY}"
        )
        message(STATUS "libps3drive: mbedTLS support enabled (system library: ${MBEDCRYPTO_LIBRARY})")
        set(MBEDTLS_FOUND TRUE)
        set(MBEDCRYPTO_TARGET MbedTLS::mbedcrypto)
    endif()
endif()

# Fall back to FetchContent if system mbedtls not found
if(NOT MBEDTLS_FOUND)
    message(STATUS "libps3drive: System mbedTLS not found, fetching from GitHub...")
    FetchContent_Declare(
        mbedtls
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG        v3.6.0
    )

    # Configure mbedtls options before making it available
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)

    # Keep mbedtls as static (third-party dependency)
    set(_saved_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(mbedtls)
    set(BUILD_SHARED_LIBS ${_saved_BUILD_SHARED_LIBS})

    # Suppress warnings for mbedtls targets (third-party code)
    if(TARGET mbedcrypto)
        if(MSVC)
            target_compile_options(mbedcrypto PRIVATE /w)
        else()
            target_compile_options(mbedcrypto PRIVATE -w)
        endif()
    endif()
    if(TARGET mbedtls)
        if(MSVC)
            target_compile_options(mbedtls PRIVATE /w)
        else()
            target_compile_options(mbedtls PRIVATE -w)
        endif()
    endif()
    if(TARGET mbedx509)
        if(MSVC)
            target_compile_options(mbedx509 PRIVATE /w)
        else()
            target_compile_options(mbedx509 PRIVATE -w)
        endif()
    endif()

    message(STATUS "libps3drive: mbedTLS support enabled (fetched from GitHub)")
    set(MBEDCRYPTO_TARGET mbedcrypto)
endif()

# -----------------------------------------------------------------------------
# Source files
# -----------------------------------------------------------------------------
set(LIBPS3DRIVE_SOURCES
    src/ps3drive.c
    src/ps3drive_auth.c
    src/ps3drive_sac.c
    src/ps3drive_crypto.c
    src/ps3drive_info.c
    src/ps3drive_pairing.c
    src/ps3drive_fw.c
)

# Public headers (installed)
set(LIBPS3DRIVE_PUBLIC_HEADERS
    include/libps3drive/ps3drive.h
    include/libps3drive/ps3drive_types.h
    include/libps3drive/ps3drive_error.h
)

# Private headers (not installed)
set(LIBPS3DRIVE_PRIVATE_HEADERS
    src/ps3drive_internal.h
    src/ps3drive_crypto.h
    src/ps3drive_keys.h
)

# -----------------------------------------------------------------------------
# Create library
# -----------------------------------------------------------------------------
add_library(libps3drive OBJECT
    ${LIBPS3DRIVE_SOURCES}
    ${LIBPS3DRIVE_PUBLIC_HEADERS}
    ${LIBPS3DRIVE_PRIVATE_HEADERS}
)

# -----------------------------------------------------------------------------
# Include directories
# -----------------------------------------------------------------------------
target_include_directories(libps3drive
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# -----------------------------------------------------------------------------
# Link dependencies
# -----------------------------------------------------------------------------
target_link_libraries(libps3drive
    PUBLIC
        libsg3
        sautil
    PRIVATE
        ${MBEDCRYPTO_TARGET}
)

# -----------------------------------------------------------------------------
# Compile definitions
# -----------------------------------------------------------------------------
target_compile_definitions(libps3drive
    PRIVATE
        PS3DRIVE_USE_MBEDTLS=1
)

# Platform-specific definitions
if(WIN32)
    target_compile_definitions(libps3drive PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# -----------------------------------------------------------------------------
# Properties
# -----------------------------------------------------------------------------
set_target_properties(libps3drive PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Output directories and installation handled by umbrella library (libdsd)
