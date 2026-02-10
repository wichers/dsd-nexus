# =============================================================================
# libsacd - SACD library
# =============================================================================

# Source files (in src/)
set(LIBSACD_SOURCES
    src/sacd_charset.c
    src/sacd_genre_tables.c
    src/sacd_dst_reader.c
    src/sacd.c
    src/sacd_helpers.c
    src/sacd_dsd_reader.c
    src/sacd_area_toc.c
    src/sacd_master_toc.c
    src/sacd_input.c
    src/sacd_input_file.c
    src/sacd_input_network.c
    src/sacd_input_ps3drive.c
    src/sacd_pb_stream.c
    proto/sacd_ripper.pb.c
)

# Public headers (in include/sacd/)
set(LIBSACD_PUBLIC_HEADERS
    include/libsacd/sacd.h
)

# Private headers (in src/)
set(LIBSACD_PRIVATE_HEADERS
    src/sacd_input.h
    src/sacd_specification.h
    src/sacd_sector_reader.h
    src/sacd_charset.h
    src/sacd_master_toc.h
    src/sacd_dsd_reader.h
    src/sacd_dst_reader.h
    src/sacd_area_toc.h
    src/sacd_frame_reader.h
    src/sacd_pb_stream.h
    proto/sacd_ripper.pb.h
)

# Create OBJECT library (combined into umbrella libdsd)
add_library(libsacd OBJECT
    ${LIBSACD_SOURCES}
    ${LIBSACD_PUBLIC_HEADERS}
    ${LIBSACD_PRIVATE_HEADERS}
)

# Set include directories for this library
target_include_directories(libsacd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/proto
)

# Output directories handled by umbrella library (libdsd)

# Link libraries (PUBLIC so consumers of libsacd also link against dependencies)
target_link_libraries(libsacd
    PUBLIC sautil libps3drive
    PRIVATE nanopb tinycsocket
)

# Version configuration
configure_version_header(
    TARGET      libsacd
    PREFIX      LIBSACD_
    GUARD       LIBSACD_VERSION_H
    PRODUCT     "libsacd"
    VERSION     ${CMAKE_PROJECT_VERSION}
    OUTPUT_DIR  ${CMAKE_CURRENT_BINARY_DIR}/include/libsacd
)

# Export macros and installation handled by umbrella library (libdsd)
