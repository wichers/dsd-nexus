# =============================================================================
# nanopb - Protocol Buffers for embedded C
# =============================================================================

add_library(nanopb STATIC
    pb_common.c
    pb_encode.c
    pb_decode.c
)

target_include_directories(nanopb
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# Enable 32-bit field support for messages larger than 64KB
target_compile_definitions(nanopb PUBLIC PB_FIELD_32BIT=1)

set_target_properties(nanopb PROPERTIES
    OUTPUT_NAME "nanopb"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/lib/RelWithDebInfo
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/lib/MinSizeRel
)
