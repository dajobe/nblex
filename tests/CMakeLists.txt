# nblex tests

# Find Check framework
find_library(CHECK_LIBRARY check REQUIRED)
if(NOT CHECK_LIBRARY)
    message(FATAL_ERROR "Check framework not found")
endif()

# Get Check include directories
find_path(CHECK_INCLUDE_DIR check.h REQUIRED)
if(NOT CHECK_INCLUDE_DIR)
    message(FATAL_ERROR "Check headers not found")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_BINARY_DIR}/../
    ${CHECK_INCLUDE_DIR}
)

# Test executables
add_executable(test_parsers test_parsers.c)
target_link_libraries(test_parsers nblex ${CHECK_LIBRARY})

add_executable(test_filters test_filters.c)
target_link_libraries(test_filters nblex ${CHECK_LIBRARY})

add_executable(test_nql test_nql.c)
target_link_libraries(test_nql nblex ${CHECK_LIBRARY})

# Register tests
add_test(NAME parsers COMMAND test_parsers)
add_test(NAME filters COMMAND test_filters)
add_test(NAME nql COMMAND test_nql)

message(STATUS "Unit tests configured")
