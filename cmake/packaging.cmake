# =============================================================================
# CPack / Qt Installer Framework (IFW) packaging configuration
# =============================================================================
#
# Usage:
#   cmake -B build -DCPACK_IFW_ROOT=C:/Qt/Tools/QtInstallerFramework/4.x ...
#   cmake --build build --config Release
#   cd build && cpack -G IFW -C Release
#
# The IFW generator requires the Qt Installer Framework tools (binarycreator).
# Set CPACK_IFW_ROOT or add the QtIFW bin/ directory to PATH.
# =============================================================================

# -- CPack metadata -----------------------------------------------------------

set(CPACK_PACKAGE_NAME "nexus-forge")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Nexus Forge")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DSD Audio Toolkit - Convert, extract, and manage DSD audio")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/wichers/dsd-nexus")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Installer output filename
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_arch "win64")
else()
    set(_arch "win32")
endif()
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${_arch}-setup")

# -- Generator selection ------------------------------------------------------

if(WIN32)
    set(CPACK_GENERATOR "IFW")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

# -- IFW-specific configuration -----------------------------------------------

set(CPACK_IFW_PACKAGE_TITLE "Nexus Forge ${PROJECT_VERSION}")
set(CPACK_IFW_PACKAGE_NAME "Nexus Forge")
set(CPACK_IFW_PACKAGE_PUBLISHER "Nexus Forge")
set(CPACK_IFW_PACKAGE_WIZARD_STYLE "Modern")
set(CPACK_IFW_PACKAGE_MAINTENANCE_TOOL_NAME "maintenancetool")

# Installer icon (Windows .ico)
if(EXISTS "${CMAKE_SOURCE_DIR}/extras/nexus-forge/icons/appicon.ico")
    set(CPACK_IFW_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/extras/nexus-forge/icons/appicon.ico")
endif()

# Default install location
if(WIN32)
    set(CPACK_IFW_TARGET_DIRECTORY "@ApplicationsDir@/Nexus Forge")
else()
    set(CPACK_IFW_TARGET_DIRECTORY "@HomeDir@/nexus-forge")
endif()

# Allow users to add start menu / desktop shortcuts
set(CPACK_IFW_PACKAGE_START_MENU_DIRECTORY "Nexus Forge")

# Only package our explicit components (exclude Unspecified)
set(CPACK_COMPONENTS_ALL dsd_application dsd_clitools dsd_development)

# -- Unblock CPack (third-party deps may have set the include guard) ----------

unset(CPack_CMake_INCLUDED)
include(CPack)
include(CPackIFW OPTIONAL RESULT_VARIABLE _cpack_ifw_included)

# -- Component definitions ----------------------------------------------------

# Application component: GUI app + shared library + Qt runtime
if(TARGET nexus-forge)
    set(_app_display "Nexus Forge Application")
    set(_app_desc "Nexus Forge desktop application with Qt runtime and shared libraries")
else()
    set(_app_display "DSD Shared Library")
    set(_app_desc "DSD shared library (dsd.dll)")
endif()

cpack_add_component(dsd_application
    DISPLAY_NAME "${_app_display}"
    DESCRIPTION "${_app_desc}"
    REQUIRED
)

# CLI tools component
cpack_add_component(dsd_clitools
    DISPLAY_NAME "Command-Line Tools"
    DESCRIPTION "dsdctl, ps3drive-tool, and sacd-mount command-line utilities"
    DEPENDS dsd_application
)

# Development files component
cpack_add_component(dsd_development
    DISPLAY_NAME "Development Files"
    DESCRIPTION "C headers and import library (libdsd.lib) for development"
    DISABLED
    DEPENDS dsd_application
)

# -- IFW component customisation (only when CPackIFW was loaded) --------------

if(_cpack_ifw_included)
    cpack_ifw_configure_component(dsd_application
        DISPLAY_NAME "${_app_display}"
        DESCRIPTION "${_app_desc}"
        SORTING_PRIORITY 100
        FORCED_INSTALLATION
        SCRIPT "${CMAKE_SOURCE_DIR}/cmake/ifw/installscript.qs"
    )

    cpack_ifw_configure_component(dsd_clitools
        DISPLAY_NAME "Command-Line Tools"
        DESCRIPTION "dsdctl, ps3drive-tool, and sacd-mount command-line utilities"
        SORTING_PRIORITY 90
        DEPENDS dsd_application
    )

    cpack_ifw_configure_component(dsd_development
        DISPLAY_NAME "Development Files"
        DESCRIPTION "C headers and import library (libdsd.lib) for development"
        SORTING_PRIORITY 80
        DEFAULT FALSE
        DEPENDS dsd_application
    )
endif()
