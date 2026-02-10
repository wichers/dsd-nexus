# dsdctl - Unified DSD audio control utility
#
# Subcommands:
#   convert  - Convert between DSD audio formats
#   extract  - Extract raw SACD ISO from PS3 drive or network
#   info     - Display file/disc metadata

cmake_minimum_required(VERSION 3.15)

# Source files
set(DSDCTL_SOURCES
    main.c
    cli_common.c
    cli_common.h
    cmd_convert.c
    cmd_convert.h
    cmd_extract.c
    cmd_extract.h
    cmd_info.c
    cmd_info.h
)

# Create executable
add_executable(dsdctl ${DSDCTL_SOURCES})

# Link libraries (umbrella provides all include dirs and dependencies)
target_link_libraries(dsdctl PRIVATE libdsd)

# Platform-specific settings
if(WIN32)
    target_link_libraries(dsdctl PRIVATE ws2_32)
else()
    target_link_libraries(dsdctl PRIVATE m)
endif()

# C standard
set_property(TARGET dsdctl PROPERTY C_STANDARD 11)
set_property(TARGET dsdctl PROPERTY C_STANDARD_REQUIRED ON)

# Compiler options
if(MSVC)
    target_compile_options(dsdctl PRIVATE /W4 /std:c11)
    target_compile_definitions(dsdctl PRIVATE
        _CRT_SECURE_NO_WARNINGS
        __STDC_WANT_LIB_EXT1__=1
    )
else()
    target_compile_options(dsdctl PRIVATE -Wall -Wextra -std=c11)
endif()

# Output directory
set_target_properties(dsdctl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin/RelWithDebInfo
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin/MinSizeRel
)

# Installation
install(TARGETS dsdctl
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT dsd_clitools
)
