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

# Find all required libraries
find_library(LIBUV_LIBRARY uv REQUIRED)
find_library(JANSSON_LIBRARY jansson REQUIRED)
find_library(PCAP_LIBRARY pcap REQUIRED)
find_library(CURL_LIBRARY curl REQUIRED)
find_library(YAML_LIBRARY yaml REQUIRED)
if(NOT LIBUV_LIBRARY)
    message(FATAL_ERROR "libuv not found")
endif()
if(NOT JANSSON_LIBRARY)
    message(FATAL_ERROR "libjansson not found")
endif()
if(NOT PCAP_LIBRARY)
    message(FATAL_ERROR "libpcap not found")
endif()
if(NOT CURL_LIBRARY)
    message(FATAL_ERROR "libcurl not found")
endif()
if(NOT YAML_LIBRARY)
    message(FATAL_ERROR "libyaml not found")
endif()

# Get include directories from pkg-config
pkg_check_modules(LIBUV libuv)
pkg_check_modules(JANSSON jansson)
pkg_check_modules(PCRE2 libpcre2-8)
pkg_check_modules(YAML yaml-0.1)

if(LIBUV_FOUND)
    include_directories(${LIBUV_INCLUDE_DIRS})
else()
    # Fallback - assume standard location
    include_directories(/opt/homebrew/include)
endif()

if(JANSSON_FOUND)
    include_directories(${JANSSON_INCLUDE_DIRS})
else()
    # Fallback - assume standard location
    include_directories(/opt/homebrew/include)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${PCRE2_INCLUDE_DIRS}
    ${YAML_INCLUDE_DIRS}
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
    src/core/filter_engine.c
    src/core/config.c
    src/core/nql_executor.c

    # Input
    src/input/file_input.c
    src/input/pcap_input.c
    src/input/input_base.c
    src/input/pcap_input.c

    # Parsers
    src/parsers/json_parser.c
    src/parsers/logfmt_parser.c
    src/parsers/syslog_parser.c
    src/parsers/nginx_parser.c
    src/parsers/regex_parser.c
    src/parsers/http_parser.c
    src/parsers/dns_parser.c
    src/parsers/nql_parser.c

    # Output
    src/output/json_output.c
    src/output/file_output.c
    src/output/http_output.c
    src/output/metrics_output.c

    # Correlation
    src/correlation/time_correlation.c

    # Utilities
    src/util/memory.c
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
    ${LIBUV_LIBRARY}
    ${JANSSON_LIBRARY}
    ${PCRE2_LIBRARIES}
    ${PCAP_LIBRARY}
    ${CURL_LIBRARY}
    ${YAML_LIBRARY}
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
