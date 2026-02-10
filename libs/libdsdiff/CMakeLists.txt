# =============================================================================
# libdsdiff - DSDIFF container format library
# =============================================================================

# Source files (in src/)
set(LIBDSDIFF_SOURCES
    src/dsdiff_io.c
    src/dsdiff_markers.c
    src/dsdiff_chunks.c
    src/dsdiff.c
)

# Public headers (in include/dsdiff/)
set(LIBDSDIFF_PUBLIC_HEADERS
    include/libdsdiff/dsdiff.h
    include/libdsdiff/dsdiff_export.h
)

# Private headers (in src/)
set(LIBDSDIFF_PRIVATE_HEADERS
    src/dsdiff_types.h
    src/dsdiff_io.h
    src/dsdiff_markers.h
    src/dsdiff_chunks.h
)

# Create OBJECT library (combined into umbrella libdsd)
add_library(libdsdiff OBJECT
    ${LIBDSDIFF_SOURCES}
    ${LIBDSDIFF_PUBLIC_HEADERS}
    ${LIBDSDIFF_PRIVATE_HEADERS}
)

# Set include directories for this library
target_include_directories(libdsdiff
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries (PUBLIC so consumers of libdsdiff also link against sautil)
target_link_libraries(libdsdiff PUBLIC sautil)

# Output directories handled by umbrella library (libdsd)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(libdsdiff PRIVATE /std:c11)
endif()


# Export macros and installation handled by umbrella library (libdsd)


# Export macro control (for dsdiff_export.h)
# Note: USING_DLL is set on the umbrella library's INTERFACE, not here.
if(BUILD_SHARED_LIBS)
    target_compile_definitions(libdsdiff PRIVATE DSDIFF_BUILDING_DLL)
endif()
