# =============================================================================
# libdsdpcm_core - C++ DSD-to-PCM core library (with TBB)
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(libdsdpcm_core LANGUAGES CXX)

# Option to select parallelization backend
option(DSDPCM_USE_TBB "Use Intel TBB for parallelization" ON)
option(DSDPCM_USE_STL "Use C++17 STL parallel algorithms" OFF)
option(DSDPCM_USE_IPP "Use Intel IPP for optimized signal processing" ON)

# Require C++20 for std::semaphore
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Find or Fetch TBB if enabled
# =============================================================================
if(DSDPCM_USE_TBB)
    set(TBB_FOUND FALSE)

    # Try CMake config first (Windows, installed TBB)
    find_package(TBB CONFIG QUIET)
    if(TBB_FOUND)
        message(STATUS "libdsdpcm: TBB support enabled (system CMake config)")
    else()
        # Try pkg-config (Linux system packages like libtbb-dev)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(TBB QUIET tbb)
            if(TBB_FOUND)
                # Create imported target for consistency
                add_library(TBB::tbb INTERFACE IMPORTED)
                set_target_properties(TBB::tbb PROPERTIES
                    INTERFACE_INCLUDE_DIRECTORIES "${TBB_INCLUDE_DIRS}"
                    INTERFACE_LINK_LIBRARIES "${TBB_LIBRARIES}"
                )
                message(STATUS "libdsdpcm: TBB support enabled (system pkg-config: ${TBB_LIBRARIES})")
            endif()
        endif()
    endif()

    # Fall back to FetchContent if system TBB not found
    if(NOT TBB_FOUND)
        message(STATUS "libdsdpcm: System TBB not found, fetching from GitHub...")
        include(FetchContent)
        FetchContent_Declare(
            tbb
            GIT_REPOSITORY https://github.com/uxlfoundation/oneTBB.git
            GIT_TAG v2021.13.0
            GIT_SHALLOW TRUE
        )
        # Disable TBB tests and examples
        set(TBB_TEST OFF CACHE BOOL "" FORCE)
        set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(TBB_STRICT OFF CACHE BOOL "" FORCE)

        # Keep TBB as static (third-party dependency) using save/restore
        # to avoid globally overriding BUILD_SHARED_LIBS
        set(_saved_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF)
        FetchContent_MakeAvailable(tbb)
        set(BUILD_SHARED_LIBS ${_saved_BUILD_SHARED_LIBS})

        # Suppress warnings from all TBB targets (external library)
        set(_tbb_targets tbb tbbmalloc tbbmalloc_proxy tbbbind)
        foreach(_target ${_tbb_targets})
            if(TARGET ${_target})
                if(MSVC)
                    target_compile_options(${_target} PRIVATE /W0)
                else()
                    target_compile_options(${_target} PRIVATE -w)
                endif()
            endif()
        endforeach()

        message(STATUS "libdsdpcm: TBB support enabled (fetched from GitHub)")
    endif()
endif()

# =============================================================================
# Find Intel IPP if enabled
# =============================================================================
if(DSDPCM_USE_IPP)
    # Try to find IPP via environment variable or standard paths
    if(DEFINED ENV{IPPROOT})
        set(IPP_ROOT "$ENV{IPPROOT}")
    elseif(DEFINED ENV{IPP_ROOT})
        set(IPP_ROOT "$ENV{IPP_ROOT}")
    elseif(WIN32)
        # Standard Intel oneAPI installation path on Windows
        set(IPP_SEARCH_PATHS
            "C:/Program Files (x86)/Intel/oneAPI/ipp/latest"
            "C:/Program Files/Intel/oneAPI/ipp/latest"
        )
        foreach(path ${IPP_SEARCH_PATHS})
            if(EXISTS "${path}/include/ipp.h")
                set(IPP_ROOT "${path}")
                break()
            endif()
        endforeach()
    else()
        # Standard paths on Linux
        set(IPP_SEARCH_PATHS
            "/opt/intel/oneapi/ipp/latest"
            "/opt/intel/ipp"
        )
        foreach(path ${IPP_SEARCH_PATHS})
            if(EXISTS "${path}/include/ipp.h")
                set(IPP_ROOT "${path}")
                break()
            endif()
        endforeach()
    endif()

    if(IPP_ROOT)
        message(STATUS "Found Intel IPP at: ${IPP_ROOT}")
        set(IPP_FOUND TRUE)
        set(IPP_INCLUDE_DIR "${IPP_ROOT}/include")

        # Determine library directory based on architecture
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            if(WIN32)
                set(IPP_LIB_DIR "${IPP_ROOT}/lib")
            else()
                set(IPP_LIB_DIR "${IPP_ROOT}/lib/intel64")
            endif()
        else()
            if(WIN32)
                set(IPP_LIB_DIR "${IPP_ROOT}/lib/ia32")
            else()
                set(IPP_LIB_DIR "${IPP_ROOT}/lib/ia32")
            endif()
        endif()

        # Find the required libraries (static versions only)
        if(WIN32)
            # Static libraries on Windows: ippsmt.lib, ippcoremt.lib
            # Use full path to ensure we get the static versions
            set(IPP_IPPS_LIBRARY "${IPP_LIB_DIR}/ippsmt.lib")
            set(IPP_IPPCORE_LIBRARY "${IPP_LIB_DIR}/ippcoremt.lib")
            set(IPP_IPPVM_LIBRARY "${IPP_LIB_DIR}/ippvmmt.lib")

            # Verify files exist
            if(NOT EXISTS "${IPP_IPPS_LIBRARY}")
                message(WARNING "IPP static library not found: ${IPP_IPPS_LIBRARY}")
                set(IPP_IPPS_LIBRARY "")
            endif()
            if(NOT EXISTS "${IPP_IPPCORE_LIBRARY}")
                message(WARNING "IPP static library not found: ${IPP_IPPCORE_LIBRARY}")
                set(IPP_IPPCORE_LIBRARY "")
            endif()
            if(NOT EXISTS "${IPP_IPPVM_LIBRARY}")
                set(IPP_IPPVM_LIBRARY "")
            endif()
        else()
            # Static libraries on Linux
            find_library(IPP_IPPS_LIBRARY NAMES libipps.a PATHS "${IPP_LIB_DIR}" NO_DEFAULT_PATH)
            find_library(IPP_IPPCORE_LIBRARY NAMES libippcore.a PATHS "${IPP_LIB_DIR}" NO_DEFAULT_PATH)
            find_library(IPP_IPPVM_LIBRARY NAMES libippvm.a PATHS "${IPP_LIB_DIR}" NO_DEFAULT_PATH)
        endif()

        if(IPP_IPPS_LIBRARY AND IPP_IPPCORE_LIBRARY)
            message(STATUS "  IPP signal processing library: ${IPP_IPPS_LIBRARY}")
            message(STATUS "  IPP core library: ${IPP_IPPCORE_LIBRARY}")
            if(IPP_IPPVM_LIBRARY)
                message(STATUS "  IPP VM library: ${IPP_IPPVM_LIBRARY}")
            endif()
        else()
            message(WARNING "IPP libraries not found in ${IPP_LIB_DIR}")
            set(IPP_FOUND FALSE)
        endif()
    else()
        message(STATUS "Intel IPP not found - using standard FIR implementation")
        set(IPP_FOUND FALSE)
    endif()
endif()

# =============================================================================
# Source files
# =============================================================================
set(DSDPCM_CORE_SOURCES
    binding/dsdpcm_decoder.cpp
)

# Select engine based on backend
if(DSDPCM_USE_TBB)
    list(APPEND DSDPCM_CORE_SOURCES binding/dsdpcm_engine_tbb.cpp)
elseif(DSDPCM_USE_STL)
    list(APPEND DSDPCM_CORE_SOURCES binding/dsdpcm_engine_stl.cpp)
else()
    list(APPEND DSDPCM_CORE_SOURCES binding/dsdpcm_engine.cpp)
endif()

# Header files (for IDE project display)
set(DSDPCM_CORE_HEADERS
    binding/dsdpcm_decoder.h
    binding/dsdpcm_engine.h
    binding/dsdpcm_engine_stl.h
    binding/dsdpcm_engine_tbb.h
    dsdpcm_converter.h
    dsdpcm_converter_multistage.h
    dsdpcm_converter_direct.h
    dsdpcm_converter_user.h
    dsdpcm_filter_setup.h
    dsdpcm_constants.h
    dsdpcm_fir.h
    dsdpcm_fir_ipp.h
    pcmpcm_fir.h
    pcmpcm_fir_ipp.h
    pcmpcm_src.h
    std_semaphore.h
)

# =============================================================================
# Create static library
# =============================================================================
add_library(dsdpcm_core STATIC ${DSDPCM_CORE_SOURCES} ${DSDPCM_CORE_HEADERS})

# Alias for namespace-style usage
add_library(dsdpcm::core ALIAS dsdpcm_core)

# =============================================================================
# Include directories
# =============================================================================
target_include_directories(dsdpcm_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/binding>
        $<INSTALL_INTERFACE:include/libdsdpcm_core>
)

# =============================================================================
# Link dependencies
# =============================================================================
if(DSDPCM_USE_TBB)
    target_link_libraries(dsdpcm_core PUBLIC TBB::tbb)
    target_compile_definitions(dsdpcm_core PRIVATE DSDPCM_USE_TBB=1)

    # Mark TBB headers as system includes to suppress warnings from external library
    get_target_property(TBB_INCLUDE_DIRS TBB::tbb INTERFACE_INCLUDE_DIRECTORIES)
    if(TBB_INCLUDE_DIRS)
        target_include_directories(dsdpcm_core SYSTEM PRIVATE ${TBB_INCLUDE_DIRS})
    endif()
elseif(DSDPCM_USE_STL)
    target_compile_definitions(dsdpcm_core PRIVATE DSDPCM_USE_STL=1)
endif()

# Link math library on Unix
if(NOT WIN32)
    target_link_libraries(dsdpcm_core PRIVATE m)
endif()

# Link IPP if found (static linking)
if(DSDPCM_USE_IPP AND IPP_FOUND)
    target_include_directories(dsdpcm_core PRIVATE "${IPP_INCLUDE_DIR}")

    # Link order matters for static libraries
    set(IPP_LIBRARIES ${IPP_IPPS_LIBRARY} ${IPP_IPPCORE_LIBRARY})
    if(IPP_IPPVM_LIBRARY)
        list(APPEND IPP_LIBRARIES ${IPP_IPPVM_LIBRARY})
    endif()

    target_link_libraries(dsdpcm_core PRIVATE ${IPP_LIBRARIES})
    target_compile_definitions(dsdpcm_core PRIVATE _USE_IPP=1)
    message(STATUS "Building dsdpcm_core with Intel IPP optimization (static)")
endif()

# =============================================================================
# Compiler options
# =============================================================================
if(MSVC)
    target_compile_options(dsdpcm_core PRIVATE
        /W4
        /wd4100  # unreferenced formal parameter
        /wd4244  # conversion from 'type1' to 'type2', possible loss of data
        /wd4267  # conversion from 'size_t' to 'type', possible loss of data
    )
else()
    target_compile_options(dsdpcm_core PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-sign-compare  # Suppress unsigned comparison warnings (external code)
        -Wno-type-limits   # Suppress 'unsigned < 0' warnings (external code)
    )
endif()

# =============================================================================
# Library properties
# =============================================================================
set_target_properties(dsdpcm_core PROPERTIES
    OUTPUT_NAME "dsdpcm_core"
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib/Debug
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib/Release
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/lib/RelWithDebInfo
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/lib/MinSizeRel
)

# =============================================================================
# Installation
# =============================================================================
include(GNUInstallDirs)

install(TARGETS dsdpcm_core
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${DSDPCM_CORE_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/libdsdpcm_core
)
