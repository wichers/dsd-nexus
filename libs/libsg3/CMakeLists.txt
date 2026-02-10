# =============================================================================
# libsg3 - SCSI Generic (sg3_utils) pass-through library
# =============================================================================

# Common source files (all platforms)
set(LIBSG3_COMMON_SOURCES
    sg_lib.c
    sg_lib_data.c
    sg_cmds_basic.c
    sg_cmds_basic2.c
    sg_cmds_extra.c
    sg_cmds_mmc.c
    sg_cmds_ps3.c
    sg_pt_common.c
)

# Public headers
set(LIBSG3_PUBLIC_HEADERS
    sg_lib.h
    sg_lib_data.h
    sg_cmds.h
    sg_cmds_basic.h
    sg_cmds_extra.h
    sg_cmds_mmc.h
    sg_cmds_ps3.h
    sg_pt.h
    sg_unaligned.h
    sg_pr2serr.h
    portable.h
)

# Private headers
set(LIBSG3_PRIVATE_HEADERS
)

# Platform-specific sources
set(LIBSG3_PLATFORM_SOURCES)

# =============================================================================
# Platform Detection and Configuration
# =============================================================================

# Detect platform and set appropriate defines
if(WIN32)
    # Windows (includes MinGW, MSVC, Cygwin)
    set(SG_LIB_WIN32 1)
    list(APPEND LIBSG3_PLATFORM_SOURCES sg_pt_win32.c)
    list(APPEND LIBSG3_PRIVATE_HEADERS sg_pt_win32.h)

    if(MINGW)
        set(SG_LIB_MINGW 1)
    endif()

    # Windows SPT interface options
    # Use direct (SPTD) for larger data transfers (>16KB), indirect (SPT) for smaller
    # Default to indirect (double-buffered) unless WIN32_SPT_DIRECT is set
    option(WIN32_SPT_DIRECT "Use SCSI Pass Through Direct (SPTD) interface" OFF)

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux
    set(SG_LIB_LINUX 1)
    list(APPEND LIBSG3_PLATFORM_SOURCES sg_pt_linux.c sg_io_linux.c)
    list(APPEND LIBSG3_PRIVATE_HEADERS sg_io_linux.h sg_linux_inc.h)

    # Linux-specific header checks
    include(CheckIncludeFile)
    check_include_file("linux/bsg.h" HAVE_LINUX_BSG_H)
    check_include_file("linux/kdev_t.h" HAVE_LINUX_KDEV_T_H)
    check_include_file("linux/types.h" HAVE_LINUX_TYPES_H)

    # Option to ignore BSG interface
    option(IGNORE_LINUX_BSG "Ignore Linux BSG interface" OFF)

    # Option for kernel includes vs glibc includes
    # See sg_io_linux.h comments about include file compatibility
    option(SG_KERNEL_INCLUDES "Use kernel headers instead of glibc" OFF)
    option(SG_TRICK_GNU_INCLUDES "Use GNU include trick (legacy)" OFF)

elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    # FreeBSD
    set(SG_LIB_FREEBSD 1)
    list(APPEND LIBSG3_PLATFORM_SOURCES sg_pt_freebsd.c)

elseif(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    # Solaris
    set(SG_LIB_SOLARIS 1)
    list(APPEND LIBSG3_PLATFORM_SOURCES sg_pt_solaris.c)

elseif(CMAKE_SYSTEM_NAME STREQUAL "OSF1")
    # Tru64 UNIX (OSF/1)
    set(SG_LIB_OSF1 1)
    list(APPEND LIBSG3_PLATFORM_SOURCES sg_pt_osf1.c)

else()
    message(WARNING "Unknown platform '${CMAKE_SYSTEM_NAME}' for libsg3, defaulting to Linux")
    set(SG_LIB_LINUX 1)
    list(APPEND LIBSG3_PLATFORM_SOURCES sg_pt_linux.c sg_io_linux.c)
    list(APPEND LIBSG3_PRIVATE_HEADERS sg_io_linux.h sg_linux_inc.h)
endif()

# =============================================================================
# Common Feature Detection
# =============================================================================

include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckSymbolExists)

# Standard headers
check_include_file("dlfcn.h" HAVE_DLFCN_H)
check_include_file("inttypes.h" HAVE_INTTYPES_H)
check_include_file("memory.h" HAVE_MEMORY_H)
check_include_file("stdint.h" HAVE_STDINT_H)
check_include_file("stdlib.h" HAVE_STDLIB_H)
check_include_file("strings.h" HAVE_STRINGS_H)
check_include_file("string.h" HAVE_STRING_H)
check_include_file("sys/stat.h" HAVE_SYS_STAT_H)
check_include_file("sys/types.h" HAVE_SYS_TYPES_H)
check_include_file("unistd.h" HAVE_UNISTD_H)

# Function checks
check_function_exists(getopt_long HAVE_GETOPT_LONG)
check_function_exists(lseek64 HAVE_LSEEK64)
check_function_exists(posix_fadvise HAVE_POSIX_FADVISE)
check_function_exists(posix_memalign HAVE_POSIX_MEMALIGN)
check_function_exists(sysconf HAVE_SYSCONF)

# Option for full SCSI sense strings (increases binary size)
option(SG_SCSI_STRINGS "Include full SCSI sense strings" ON)

# Package information
set(PACKAGE "sg3_utils")
set(PACKAGE_NAME "sg3_utils")
set(PACKAGE_VERSION "1.48")
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME "sg3_utils")
set(PACKAGE_URL "https://sg.danny.cz/sg/sg3_utils.html")
set(PACKAGE_BUGREPORT "dgilbert@interlog.com")
set(VERSION "${PACKAGE_VERSION}")

# Build host for identification
set(SG_LIB_BUILD_HOST "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

# =============================================================================
# Generate config.h
# =============================================================================

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
    @ONLY
)

# =============================================================================
# Create Library
# =============================================================================

add_library(libsg3 OBJECT
    ${LIBSG3_COMMON_SOURCES}
    ${LIBSG3_PLATFORM_SOURCES}
    ${LIBSG3_PUBLIC_HEADERS}
    ${LIBSG3_PRIVATE_HEADERS}
)

# Set include directories
target_include_directories(libsg3
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include/libsg3>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
)

# =============================================================================
# Platform-specific Compile Definitions
# =============================================================================

# Set platform defines
if(SG_LIB_WIN32)
    target_compile_definitions(libsg3 PUBLIC SG_LIB_WIN32=1)
    if(SG_LIB_MINGW)
        target_compile_definitions(libsg3 PUBLIC SG_LIB_MINGW=1)
    endif()
    if(WIN32_SPT_DIRECT)
        target_compile_definitions(libsg3 PUBLIC WIN32_SPT_DIRECT=1)
    endif()
    # MSVC: Suppress warnings for this third-party library (sg3_utils)
    if(MSVC)
        target_compile_definitions(libsg3 PRIVATE _CRT_SECURE_NO_WARNINGS=1)
        target_compile_options(libsg3 PRIVATE
            /wd4244  # conversion, possible loss of data
            /wd4267  # size_t to int conversion
            /wd4100  # unreferenced formal parameter
        )
    endif()
endif()

if(SG_LIB_LINUX)
    target_compile_definitions(libsg3 PUBLIC SG_LIB_LINUX=1)
    if(HAVE_LINUX_BSG_H AND NOT IGNORE_LINUX_BSG)
        target_compile_definitions(libsg3 PRIVATE HAVE_LINUX_BSG_H=1)
    endif()
    if(IGNORE_LINUX_BSG)
        target_compile_definitions(libsg3 PRIVATE IGNORE_LINUX_BSG=1)
    endif()
    if(SG_KERNEL_INCLUDES)
        target_compile_definitions(libsg3 PRIVATE SG_KERNEL_INCLUDES=1)
    endif()
    if(SG_TRICK_GNU_INCLUDES)
        target_compile_definitions(libsg3 PRIVATE SG_TRICK_GNU_INCLUDES=1)
    endif()
endif()

if(SG_LIB_FREEBSD)
    target_compile_definitions(libsg3 PUBLIC SG_LIB_FREEBSD=1)
endif()

if(SG_LIB_SOLARIS)
    target_compile_definitions(libsg3 PUBLIC SG_LIB_SOLARIS=1)
endif()

if(SG_LIB_OSF1)
    target_compile_definitions(libsg3 PUBLIC SG_LIB_OSF1=1)
endif()

if(SG_SCSI_STRINGS)
    target_compile_definitions(libsg3 PRIVATE SG_SCSI_STRINGS=1)
endif()

# Indicate we have a generated config.h
target_compile_definitions(libsg3 PRIVATE HAVE_CONFIG_H=1)

# =============================================================================
# Library Properties
# =============================================================================

# Output directories and installation handled by umbrella library (libdsd)

# =============================================================================
# Configuration Summary
# =============================================================================

message(STATUS "")
message(STATUS "libsg3 Configuration:")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
if(SG_LIB_WIN32)
    if(SG_LIB_MINGW)
        message(STATUS "  Backend: Win32 SPT (MinGW)")
    else()
        message(STATUS "  Backend: Win32 SPT")
    endif()
    message(STATUS "  SPT Direct: ${WIN32_SPT_DIRECT}")
elseif(SG_LIB_LINUX)
    message(STATUS "  Backend: Linux sg/bsg")
    message(STATUS "  BSG support: ${HAVE_LINUX_BSG_H}")
elseif(SG_LIB_FREEBSD)
    message(STATUS "  Backend: FreeBSD CAM")
elseif(SG_LIB_SOLARIS)
    message(STATUS "  Backend: Solaris USCSI")
elseif(SG_LIB_OSF1)
    message(STATUS "  Backend: Tru64 UNIX")
endif()
message(STATUS "  SCSI strings: ${SG_SCSI_STRINGS}")
message(STATUS "")
