cmake_minimum_required(VERSION 3.20)
project(nexus-forge VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Qt6 COMPONENTS Svg QUIET)
if(UNIX AND NOT APPLE)
    find_package(Qt6 COMPONENTS DBus QUIET)
endif()

if(NOT Qt6Svg_FOUND)
    message(WARNING "Qt6 Svg component not found -- SVG icons will not be visible in the application toolbar. Install qt6-svg package to fix this.")
endif()

# LGPL compliance: Qt6 must be dynamically linked
get_target_property(_qt_type Qt6::Core TYPE)
if(_qt_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "Static Qt6 detected. nexus-forge requires shared Qt6 for LGPL compliance.")
endif()

# Sources organized by category
set(NEXUS_PIPELINE_SOURCES
    pipeline/dsdpipeparameters.cpp
    pipeline/dsdconverter.cpp
    pipeline/dsdworker.cpp
    pipeline/dsdprobe.cpp
    pipeline/extractworker.cpp
)
if(BUILD_PS3DRIVE)
    list(APPEND NEXUS_PIPELINE_SOURCES pipeline/ps3driveworker.cpp)
endif()

set(NEXUS_UI_SOURCES
    ui/mainwindow.cpp
    ui/convertlist.cpp
    ui/convertlistdelegate.cpp
    ui/progressbarpainter.cpp
    ui/aboutdialog.cpp
    ui/optionsdialog.cpp
    ui/addtaskdialog.cpp
    ui/dsdparamdialog.cpp
    ui/extractdialog.cpp
)

set(NEXUS_SERVICES_SOURCES
    services/paths.cpp
    services/constants.cpp
    services/xmllookuptable.cpp
    services/notification.cpp
    services/notificationservice.cpp
    services/notificationservice-qt.cpp
    services/notificationservice-notifysend.cpp
    services/filepathoperations.cpp
    services/extensions.cpp
)

set(NEXUS_FORMS
    ui/mainwindow.ui
    ui/aboutdialog.ui
)

set(NEXUS_RESOURCES
    images.qrc
)

if(WIN32)
    set(NEXUS_RC appicon.rc)
endif()

add_executable(nexus-forge
    main.cpp
    ${NEXUS_PIPELINE_SOURCES}
    ${NEXUS_UI_SOURCES}
    ${NEXUS_SERVICES_SOURCES}
    ${NEXUS_FORMS}
    ${NEXUS_RESOURCES}
    ${NEXUS_RC}
)

target_include_directories(nexus-forge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(nexus-forge PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    libdsd
)

if(TARGET Qt6::Svg)
    target_link_libraries(nexus-forge PRIVATE Qt6::Svg)
endif()

if(WIN32)
    target_compile_definitions(nexus-forge PRIVATE TOOLS_IN_DATA_PATH)
    # Set WIN32_EXECUTABLE for release builds (no console window)
    set_target_properties(nexus-forge PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
elseif(APPLE)
    set_target_properties(nexus-forge PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Nexus Forge"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.nexusforge.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "appicon.icns"
        # Use @rpath-relative linking; macdeployqt rewrites rpaths when
        # bundling frameworks so CPack install_name_tool won't find stale paths.
        INSTALL_RPATH "@executable_path/../Frameworks"
        BUILD_WITH_INSTALL_RPATH FALSE
    )
    # Copy .icns into the bundle's Resources directory
    set(_icns_file "${CMAKE_CURRENT_SOURCE_DIR}/icons/appicon.icns")
    if(EXISTS "${_icns_file}")
        set_source_files_properties("${_icns_file}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(nexus-forge PRIVATE "${_icns_file}")
    endif()
elseif(UNIX)
    if(TARGET Qt6::DBus)
        target_link_libraries(nexus-forge PRIVATE Qt6::DBus)
    endif()
endif()

# Version configuration
configure_version_header(
    TARGET      nexus-forge
    PREFIX      NEXUS_FORGE_
    GUARD       NEXUS_FORGE_VERSION_H
    PRODUCT     "Nexus Forge"
    VERSION     ${PROJECT_VERSION}
    OUTPUT_DIR  ${CMAKE_CURRENT_BINARY_DIR}/nexus-forge
)

target_compile_definitions(nexus-forge PRIVATE
    OPERATION_TIMEOUT=30000
    DEFAULT_THREAD_COUNT=1
    APP_NAME="Nexus Forge"
    APP_ORGANIZATION="nexus-forge"
)
if(NOT BUILD_PS3DRIVE)
    target_compile_definitions(nexus-forge PRIVATE SACD_NO_PS3DRIVE)
endif()

# Output alongside dsd.dll so the executable can find the DLL at runtime
set_target_properties(nexus-forge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin/RelWithDebInfo
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin/MinSizeRel
)

# =============================================================================
# Installation
# =============================================================================

if(APPLE)
    install(TARGETS nexus-forge
        BUNDLE DESTINATION "."
        COMPONENT dsd_application
    )
else()
    install(TARGETS nexus-forge
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT dsd_application
    )
endif()

# =============================================================================
# Qt deploy: Collect Qt runtime frameworks/DLLs and plugins for packaging
# =============================================================================

# Locate Qt tools directory
get_target_property(_qmake_path Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qmake_path}" DIRECTORY)

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}" NO_DEFAULT_PATH)

    if(WINDEPLOYQT_EXECUTABLE)
        set(_qt_deploy_dir "${CMAKE_BINARY_DIR}/qt_deploy")

        # Run windeployqt after building nexus-forge to stage Qt DLLs
        add_custom_command(TARGET nexus-forge POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${_qt_deploy_dir}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_qt_deploy_dir}"
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --dir "${_qt_deploy_dir}"
                --no-translations
                --no-compiler-runtime
                --no-system-d3d-compiler
                --no-opengl-sw
                $<TARGET_FILE:nexus-forge>
            COMMENT "Running windeployqt to collect Qt6 runtime files..."
            VERBATIM
        )

        # Include the staged Qt runtime files in the installer
        install(DIRECTORY "${_qt_deploy_dir}/"
            DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT dsd_application
        )
    else()
        message(WARNING "windeployqt not found -- Qt runtime DLLs will not be bundled in the installer")
    endif()

elseif(APPLE)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}" NO_DEFAULT_PATH)

    if(MACDEPLOYQT_EXECUTABLE)
        # Run macdeployqt after building to embed Qt frameworks into the .app bundle
        add_custom_command(TARGET nexus-forge POST_BUILD
            COMMAND "${MACDEPLOYQT_EXECUTABLE}"
                "$<TARGET_BUNDLE_DIR:nexus-forge>"
                -always-overwrite
            COMMENT "Running macdeployqt to embed Qt frameworks into app bundle..."
            VERBATIM
        )
    else()
        message(WARNING "macdeployqt not found -- Qt frameworks will not be bundled in the app")
    endif()
endif()
