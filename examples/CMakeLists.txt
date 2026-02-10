# =============================================================================
# Examples - Sample usage programs (PFL 2.5)
# =============================================================================

add_executable(dsdiff dsdiff.c)
#add_executable(sacd_vfs_usage sacd_vfs_usage.c)

# =============================================================================
# dsd-convert: DSD audio format converter (DSF <-> DSDIFF)
# =============================================================================
add_executable(dsd-convert dsd_convert.c)

target_link_libraries(dsd-convert PRIVATE libdsd)

# =============================================================================
# dsd-tools: SACD to DSF converter using libdsdpipe
# =============================================================================
if(TARGET dsdpipe)
    add_executable(dsd-tools dsd_tools.c)

    target_link_libraries(dsd-tools PRIVATE libdsd)

    if(WIN32)
        target_link_libraries(dsd-tools PRIVATE ws2_32)
    else()
        target_link_libraries(dsd-tools PRIVATE m)
    endif()

    list(APPEND EXAMPLE_TARGETS dsd-tools)
    message(STATUS "dsd-tools example will be built")
endif()

# Link against the sacd library using the alias
if(TARGET sacd::sacd)
#    target_link_libraries(sacd_vfs_usage PRIVATE sacd::sacd)
else()
    message(FATAL_ERROR "No sacd library target available")
endif()

# Link dsdiff example against umbrella library
target_link_libraries(dsdiff PRIVATE libdsd)
#target_include_directories(sacd_vfs_usage PRIVATE
#    ${SAUTIL_CONFIG_PATH}
#)

# Set output directories
set(EXAMPLE_TARGETS dsdiff dsd-convert)

set_target_properties(${EXAMPLE_TARGETS} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/examples/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/examples/Release
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/examples/RelWithDebInfo
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/examples/MinSizeRel
)
