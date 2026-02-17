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
set(CPACK_PACKAGE_VENDOR "DSD Nexus")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DSD Audio Toolkit - Convert, extract, and manage DSD audio")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/wichers/dsd-nexus")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# -- Generator selection & component list -------------------------------------

# Use -DSACD_VFS_ONLY=ON to package just sacd-vfs (for standalone .deb/.rpm)
if(CPACK_SACD_VFS_ONLY OR SACD_VFS_ONLY)
    set(CPACK_COMPONENTS_ALL sacd_vfs)
elseif(WIN32 OR APPLE)
    set(CPACK_COMPONENTS_ALL dsd_application dsd_clitools dsd_development)
else()
    set(CPACK_COMPONENTS_ALL dsd_application dsd_clitools dsd_development sacd_vfs)
endif()

# Enable per-component packaging so each component gets its own .deb/.rpm
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)

if(WIN32)
    set(CPACK_GENERATOR "IFW")
elseif(APPLE)
    set(CPACK_GENERATOR "IFW;DragNDrop;TGZ")
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
endif()

# -- Installer output filename ------------------------------------------------

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_arch "win64")
    else()
        set(_arch "win32")
    endif()
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${_arch}-setup")
elseif(APPLE)
    # Detect architecture for installer filename
    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64" AND CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
        set(_mac_arch "universal")
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(_mac_arch "x86_64")
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(_mac_arch "arm64")
    else()
        # No explicit arch set: use the host architecture
        execute_process(COMMAND uname -m OUTPUT_VARIABLE _mac_arch OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-macos-${_mac_arch}-setup")
endif()

# -- IFW-specific configuration -----------------------------------------------

set(CPACK_IFW_PACKAGE_TITLE "DSD Nexus ${PROJECT_VERSION}")
set(CPACK_IFW_PACKAGE_NAME "DSD Nexus")
set(CPACK_IFW_PACKAGE_PUBLISHER "DSD Nexus")
set(CPACK_IFW_PACKAGE_WIZARD_STYLE "Modern")
set(CPACK_IFW_PACKAGE_MAINTENANCE_TOOL_NAME "maintenancetool")

# Installer icon
if(APPLE AND EXISTS "${CMAKE_SOURCE_DIR}/extras/nexus-forge/icons/appicon.icns")
    set(CPACK_IFW_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/extras/nexus-forge/icons/appicon.icns")
elseif(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/extras/nexus-forge/icons/appicon.ico")
    set(CPACK_IFW_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/extras/nexus-forge/icons/appicon.ico")
endif()

# Default install location
if(WIN32 OR APPLE)
    set(CPACK_IFW_TARGET_DIRECTORY "@ApplicationsDir@/DSD Nexus")
else()
    set(CPACK_IFW_TARGET_DIRECTORY "@HomeDir@/nexus-forge")
endif()

# Allow users to add start menu / desktop shortcuts
set(CPACK_IFW_PACKAGE_START_MENU_DIRECTORY "DSD Nexus")

# =============================================================================
# Linux packaging: DEB / RPM for sacd-vfs
# =============================================================================
# NOTE: All CPACK_* variables MUST be set before include(CPack) below,
# otherwise they won't be written to CPackConfig.cmake.

if(UNIX AND NOT APPLE)
    # -- Output filename -------------------------------------------------------

    execute_process(COMMAND dpkg --print-architecture
        OUTPUT_VARIABLE _deb_arch OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT _deb_arch)
        set(_deb_arch "amd64")
    endif()
    set(CPACK_PACKAGE_FILE_NAME "sacd-vfs-${CPACK_PACKAGE_VERSION}-${_deb_arch}")

    # -- DEB-specific configuration --------------------------------------------
    # With CPACK_DEB_COMPONENT_INSTALL=ON, use SACD_VFS (uppercase component)
    # prefix for component-specific settings.

    set(CPACK_DEBIAN_SACD_VFS_PACKAGE_NAME "sacd-vfs")
    set(CPACK_DEBIAN_SACD_VFS_PACKAGE_DEPENDS "fuse3 (>= 3.10), debconf (>= 0.5) | debconf-2.0")
    set(CPACK_DEBIAN_SACD_VFS_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_SACD_VFS_PACKAGE_SECTION "sound")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Alexander Wichers <sander.wichers@gmail.com>")
    set(CPACK_DEBIAN_SACD_VFS_PACKAGE_DESCRIPTION
        "SACD Virtual Filesystem - mount SACD ISOs as directories with DSF files\n Presents SACD ISO disc images as browseable directory trees containing\n standard DSF audio files. Useful for music servers like Roon.")
    set(CPACK_DEBIAN_SACD_VFS_FILE_NAME "sacd-vfs-${CPACK_PACKAGE_VERSION}-${_deb_arch}.deb")

    # Copy control files to build tree with correct Unix permissions
    # (source files may have 0777 from Windows/NTFS mounts)
    set(_deb_ctrl_dir "${CMAKE_BINARY_DIR}/deb-control")
    file(MAKE_DIRECTORY "${_deb_ctrl_dir}")
    foreach(_script config postinst prerm postrm)
        file(COPY "${CMAKE_SOURCE_DIR}/packaging/linux/deb/${_script}"
            DESTINATION "${_deb_ctrl_dir}"
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                             GROUP_READ GROUP_EXECUTE
                             WORLD_READ WORLD_EXECUTE)
    endforeach()
    foreach(_data conffiles templates)
        file(COPY "${CMAKE_SOURCE_DIR}/packaging/linux/deb/${_data}"
            DESTINATION "${_deb_ctrl_dir}"
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE
                             GROUP_READ
                             WORLD_READ)
    endforeach()

    set(CPACK_DEBIAN_SACD_VFS_PACKAGE_CONTROL_EXTRA
        "${_deb_ctrl_dir}/conffiles"
        "${_deb_ctrl_dir}/templates"
        "${_deb_ctrl_dir}/config"
        "${_deb_ctrl_dir}/postinst"
        "${_deb_ctrl_dir}/prerm"
        "${_deb_ctrl_dir}/postrm")

    # -- RPM-specific configuration --------------------------------------------

    set(CPACK_RPM_SACD_VFS_PACKAGE_NAME "sacd-vfs")
    set(CPACK_RPM_SACD_VFS_PACKAGE_REQUIRES "fuse3 >= 3.10, mbedtls >= 3.0")
    set(CPACK_RPM_SACD_VFS_PACKAGE_GROUP "Applications/Multimedia")
    set(CPACK_RPM_PACKAGE_LICENSE "LGPL-2.1-or-later")
    set(CPACK_RPM_SACD_VFS_PACKAGE_LICENSE "LGPL-2.1-or-later")
    set(CPACK_RPM_SACD_VFS_FILE_NAME "sacd-vfs-${CPACK_PACKAGE_VERSION}-1.x86_64.rpm")
    set(CPACK_RPM_SACD_VFS_PACKAGE_DESCRIPTION
        "SACD Virtual Filesystem - mount SACD ISOs as directories with DSF files.\nPresents SACD ISO disc images as browseable directory trees containing\nstandard DSF audio files. Useful for music servers like Roon.")

    # Mark config as %config(noreplace) so RPM preserves user edits
    set(CPACK_RPM_SACD_VFS_USER_FILELIST
        "%config(noreplace) /etc/sacd-vfs/sacd-vfs.conf")

    set(CPACK_RPM_SACD_VFS_POST_INSTALL_SCRIPT_FILE
        "${CMAKE_SOURCE_DIR}/packaging/linux/rpm/post_install.sh")
    set(CPACK_RPM_SACD_VFS_PRE_UNINSTALL_SCRIPT_FILE
        "${CMAKE_SOURCE_DIR}/packaging/linux/rpm/pre_uninstall.sh")
    set(CPACK_RPM_SACD_VFS_POST_UNINSTALL_SCRIPT_FILE
        "${CMAKE_SOURCE_DIR}/packaging/linux/rpm/post_uninstall.sh")
endif()

# -- Include CPack (writes CPackConfig.cmake from variables set above) --------

unset(CPack_CMake_INCLUDED)
include(CPack)
include(CPackIFW OPTIONAL RESULT_VARIABLE _cpack_ifw_included)

# -- Component definitions (must come AFTER include(CPack)) -------------------

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
    DESCRIPTION "dsdctl, ps3drive-tool, and sacd-vfs command-line utilities"
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
        DESCRIPTION "dsdctl, ps3drive-tool, and sacd-vfs command-line utilities"
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

# -- Linux install rules & component (must come AFTER include(CPack)) ---------

if(UNIX AND NOT APPLE)
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/linux/sacd-vfs.service"
        DESTINATION lib/systemd/system
        COMPONENT sacd_vfs)

    # Use absolute /etc path so it doesn't end up under CMAKE_INSTALL_PREFIX
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/linux/sacd-vfs.conf"
        DESTINATION /etc/sacd-vfs
        COMPONENT sacd_vfs)

    install(PROGRAMS "${CMAKE_SOURCE_DIR}/packaging/linux/sacd-vfs-helper.sh"
        DESTINATION lib/sacd-vfs
        COMPONENT sacd_vfs)

    # Copyright and changelog for Debian policy compliance
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/linux/copyright"
        DESTINATION share/doc/sacd-vfs
        COMPONENT sacd_vfs)

    # Compress changelog at install time (Debian policy requires .gz)
    find_program(GZIP_EXE gzip)
    if(GZIP_EXE)
        set(_cl_src "${CMAKE_SOURCE_DIR}/packaging/linux/changelog")
        set(_cl_gz "${CMAKE_BINARY_DIR}/changelog.gz")
        execute_process(
            COMMAND ${GZIP_EXE} -9 -n -c "${_cl_src}"
            OUTPUT_FILE "${_cl_gz}")
        install(FILES "${_cl_gz}"
            DESTINATION share/doc/sacd-vfs
            COMPONENT sacd_vfs)
    endif()

    cpack_add_component(sacd_vfs
        DISPLAY_NAME "SACD VFS Service"
        DESCRIPTION "SACD FUSE virtual filesystem with systemd service"
    )
endif()
