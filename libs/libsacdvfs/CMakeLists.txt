# =============================================================================
# libsacdvfs - SACD Virtual File System library
# =============================================================================

# Source files (in src/)
set(LIBSACDVFS_SOURCES
    src/sacd_vfs.c
    src/sacd_id3.c
    src/sacd_overlay.c
    src/sacd_overlay_path.c
    src/sacd_overlay_iso.c
)

# Public headers (in include/libsacdvfs/)
set(LIBSACDVFS_PUBLIC_HEADERS
    include/libsacdvfs/sacd_vfs.h
    include/libsacdvfs/sacd_overlay.h
    include/libsacdvfs/sacdvfs_export.h
)

# Private headers (in src/)
set(LIBSACDVFS_PRIVATE_HEADERS
    src/sacd_id3.h
)

# Create OBJECT library (combined into umbrella libdsd)
add_library(libsacdvfs OBJECT
    ${LIBSACDVFS_SOURCES}
    ${LIBSACDVFS_PUBLIC_HEADERS}
    ${LIBSACDVFS_PRIVATE_HEADERS}
)

# Set include directories for this library
target_include_directories(libsacdvfs
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Output directories handled by umbrella library (libdsd)

# Link libraries (PUBLIC so consumers of libsacdvfs also link against dependencies)
target_link_libraries(libsacdvfs
    PUBLIC libsacd libdst
    PRIVATE sautil id3dev
)



# Export macros and installation handled by umbrella library (libdsd)


# Export macro control (for sacdvfs_export.h)
# Note: USING_DLL is set on the umbrella library's INTERFACE, not here.
if(BUILD_SHARED_LIBS)
    target_compile_definitions(libsacdvfs PRIVATE SACDVFS_BUILDING_DLL)
endif()
