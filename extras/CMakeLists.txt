# =============================================================================
# Extras - Optional tools and utilities (PFL 2.11)
# =============================================================================

# =============================================================================
# ps3drive-tool: PS3 BluRay Drive Utility
# =============================================================================
if(BUILD_PS3DRIVE)
    add_subdirectory(ps3-drive)
endif()

# =============================================================================
# SACD VFS Mount - Cross-platform FUSE adapter
# Uses libfuse3 on Linux/macOS and WinFSP FUSE layer on Windows
# =============================================================================
add_subdirectory(sacd-vfs)

# =============================================================================
# dsdctl: Unified DSD audio control utility
# Subcommands: convert, extract, info
# =============================================================================
add_subdirectory(dsdctl)

# =============================================================================
# Nexus Forge: Qt6 desktop frontend for DSD audio conversion
# Only built when Qt6 is available
# =============================================================================
find_package(Qt6 QUIET COMPONENTS Core)
if(Qt6_FOUND)
    add_subdirectory(nexus-forge)
else()
    message(STATUS "Qt6 not found -- skipping nexus-forge")
endif()
