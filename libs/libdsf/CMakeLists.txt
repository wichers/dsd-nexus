# =============================================================================
# libdsf - DSF container format library
# =============================================================================

# Source files (in src/)
set(LIBDSF_SOURCES
    src/dsf_io.c
    src/dsf_chunks.c
    src/dsf.c
)

# Public headers (in include/dsf/)
set(LIBDSF_PUBLIC_HEADERS
    include/libdsf/dsf.h
    include/libdsf/dsf_export.h
)

# Private headers (in src/)
set(LIBDSF_PRIVATE_HEADERS
    src/dsf_types.h
    src/dsf_io.h
    src/dsf_chunks.h
)

# Create OBJECT library (combined into umbrella libdsd)
add_library(libdsf OBJECT
    ${LIBDSF_SOURCES}
    ${LIBDSF_PUBLIC_HEADERS}
    ${LIBDSF_PRIVATE_HEADERS}
)

# Set include directories for this library
target_include_directories(libdsf
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries (PUBLIC so consumers of libdsf also link against sautil)
target_link_libraries(libdsf PUBLIC sautil)

# Output directories handled by umbrella library (libdsd)

# MSVC-specific compiler flags
if(MSVC)
    target_compile_options(libdsf PRIVATE /std:c11)
endif()


# Export macros and installation handled by umbrella library (libdsd)


# Export macro control (for dsf_export.h)
# Note: USING_DLL is set on the umbrella library's INTERFACE, not here.
if(BUILD_SHARED_LIBS)
    target_compile_definitions(libdsf PRIVATE DSF_BUILDING_DLL)
endif()
