# ps3drive-tool: PS3 BluRay Drive Utility

cmake_minimum_required(VERSION 3.15)

add_executable(ps3drive-tool
    ps3drive_tool.c
)

target_link_libraries(ps3drive-tool PRIVATE libdsd)

# Set output directory
set_target_properties(ps3drive-tool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin/RelWithDebInfo
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin/MinSizeRel
)

install(TARGETS ps3drive-tool
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT dsd_clitools
)
