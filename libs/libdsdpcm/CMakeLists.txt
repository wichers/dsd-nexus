# =============================================================================
# libdsdpcm - DSD to PCM conversion library (C wrapper)
# =============================================================================

# =============================================================================
# Add the C++ core library from external
# =============================================================================
add_subdirectory(${CMAKE_SOURCE_DIR}/external/libdsdpcm
                 ${CMAKE_BINARY_DIR}/external/libdsdpcm)

# =============================================================================
# Source files
# =============================================================================
set(LIBDSDPCM_SOURCES
    src/dsdpcm_wrapper.cpp
    src/dsdpcm_fir_io.c
)

# Public headers
set(LIBDSDPCM_PUBLIC_HEADERS
    include/libdsdpcm/dsdpcm.h
)

# Private headers
set(LIBDSDPCM_PRIVATE_HEADERS
    src/dsdpcm_internal.h
)

# =============================================================================
# Create OBJECT library (combined into umbrella libdsd)
# =============================================================================
add_library(libdsdpcm OBJECT
    ${LIBDSDPCM_SOURCES}
    ${LIBDSDPCM_PUBLIC_HEADERS}
    ${LIBDSDPCM_PRIVATE_HEADERS}
)

# =============================================================================
# Include directories
# =============================================================================
target_include_directories(libdsdpcm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# =============================================================================
# Link dependencies
# =============================================================================
# Private: dsdpcm_core (C++ implementation)
# Public: sautil (for utilities and win_utf8_io)
target_link_libraries(libdsdpcm
    PRIVATE dsdpcm_core
    PUBLIC sautil
)

# =============================================================================
# Compiler settings
# =============================================================================
# Require C++20 for the wrapper (needed to interface with dsdpcm_core)
set_target_properties(libdsdpcm PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# =============================================================================
# Compiler options
# =============================================================================
if(MSVC)
    target_compile_options(libdsdpcm PRIVATE
        /W4
        /wd4100  # unreferenced formal parameter
        /wd4244  # conversion possible loss of data
        /wd4267  # conversion from 'size_t' to 'type'
    )
    # C11 for C sources, C++20 for C++ sources
    target_compile_options(libdsdpcm PRIVATE
        $<$<COMPILE_LANGUAGE:C>:/std:c11>
        $<$<COMPILE_LANGUAGE:CXX>:/std:c++20>
    )
else()
    target_compile_options(libdsdpcm PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Output directories and installation handled by umbrella library (libdsd)

# Export private directory for testing
set(LIBDSDPCM_PRIVATE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src CACHE PATH
    "Path to libdsdpcm private headers" FORCE)
