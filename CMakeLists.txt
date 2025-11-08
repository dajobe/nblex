# nblex - Network & Buffer Log EXplorer
# CMake build configuration

cmake_minimum_required(VERSION 3.10)
project(nblex VERSION 0.1.0 LANGUAGES C)

# Use C11 standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build options
option(NBLEX_BUILD_TESTS "Build tests" ON)
option(NBLEX_BUILD_EXAMPLES "Build examples" ON)
option(NBLEX_BUILD_CLI "Build CLI tool" ON)
option(NBLEX_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(NBLEX_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wno-unused-parameter
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Release>:-O3>
    )
endif()

# Sanitizers
if(NBLEX_ENABLE_ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

if(NBLEX_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

# Find required dependencies
find_package(PkgConfig REQUIRED)

# Core dependencies
pkg_check_modules(LIBUV REQUIRED libuv)
pkg_check_modules(JANSSON REQUIRED jansson)

# Optional dependencies (TODO: add later)
# pkg_check_modules(PCRE2 REQUIRED libpcre2-8)
# find_library(PCAP_LIBRARY pcap REQUIRED)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${LIBUV_INCLUDE_DIRS}
    ${JANSSON_INCLUDE_DIRS}
)

# Generate version header
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/nblex/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/nblex/version.h
)

# Source files
set(NBLEX_SOURCES
    # Core
    src/core/nblex_world.c
    src/core/nblex_event.c

    # Input
    src/input/file_input.c
    src/input/input_base.c

    # Parsers
    src/parsers/json_parser.c

    # Output
    src/output/json_output.c

    # Utilities
    src/util/memory.c
    src/util/buffer.c
)

# Build shared library
add_library(nblex SHARED ${NBLEX_SOURCES})
set_target_properties(nblex PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    PUBLIC_HEADER include/nblex/nblex.h
)

# Link dependencies
target_link_libraries(nblex
    ${LIBUV_LIBRARIES}
    ${JANSSON_LIBRARIES}
)

# Build CLI tool
if(NBLEX_BUILD_CLI)
    add_executable(nblex-cli cli/main.c)
    target_link_libraries(nblex-cli nblex)
    set_target_properties(nblex-cli PROPERTIES OUTPUT_NAME nblex)
endif()

# Build tests
if(NBLEX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Build examples
if(NBLEX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install targets
install(TARGETS nblex
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include/nblex
)

if(NBLEX_BUILD_CLI)
    install(TARGETS nblex-cli
        RUNTIME DESTINATION bin
    )
endif()

# pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/nblex.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/nblex.pc
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nblex.pc
    DESTINATION lib/pkgconfig
)

# Print configuration summary
message(STATUS "nblex ${PROJECT_VERSION}")
message(STATUS "  C Standard: C${CMAKE_C_STANDARD}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build tests: ${NBLEX_BUILD_TESTS}")
message(STATUS "  Build examples: ${NBLEX_BUILD_EXAMPLES}")
message(STATUS "  Build CLI: ${NBLEX_BUILD_CLI}")
message(STATUS "  AddressSanitizer: ${NBLEX_ENABLE_ASAN}")
message(STATUS "  UndefinedBehaviorSanitizer: ${NBLEX_ENABLE_UBSAN}")
