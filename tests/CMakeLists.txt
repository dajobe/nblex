# nblex tests

# Find Check framework
find_library(CHECK_LIBRARY check REQUIRED)
find_library(SUBUNIT_LIBRARY subunit)
if(NOT CHECK_LIBRARY)
    message(FATAL_ERROR "Check framework not found")
endif()
if(NOT SUBUNIT_LIBRARY)
    set(SUBUNIT_LIBRARY "")
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
target_link_libraries(test_parsers nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

add_executable(test_filters test_filters.c)
target_link_libraries(test_filters nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

# nQL tests - split into logical modules
add_executable(test_nql_parse test_nql_parse.c)
target_link_libraries(test_nql_parse nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

add_executable(test_nql_execute test_nql_execute.c test_helpers.c)
target_link_libraries(test_nql_execute nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

add_executable(test_nql_windows test_nql_windows.c test_helpers.c)
target_link_libraries(test_nql_windows nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

add_executable(test_nql_schema test_nql_schema.c test_helpers.c)
target_link_libraries(test_nql_schema nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

add_executable(test_file_input test_file_input.c)
target_link_libraries(test_file_input nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

add_executable(test_metrics_output test_metrics_output.c)
target_link_libraries(test_metrics_output nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

# High priority unit tests
add_executable(test_world test_world.c test_helpers.c)
target_link_libraries(test_world nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

add_executable(test_correlation test_correlation.c test_helpers.c)
target_link_libraries(test_correlation nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

add_executable(test_config test_config.c)
target_link_libraries(test_config nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

# Integration tests - split into logical modules
add_executable(test_integration_file test_integration_file.c test_helpers.c test_integration_helpers.c)
target_link_libraries(test_integration_file nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

add_executable(test_integration_correlation test_integration_correlation.c test_helpers.c)
target_link_libraries(test_integration_correlation nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

add_executable(test_integration_config test_integration_config.c test_integration_helpers.c)
target_link_libraries(test_integration_config nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

add_executable(test_integration_output test_integration_output.c test_helpers.c test_integration_helpers.c)
target_link_libraries(test_integration_output nblex ${CHECK_LIBRARY} ${SUBUNIT_LIBRARY} m)

# Register tests
add_test(NAME parsers COMMAND test_parsers)
add_test(NAME filters COMMAND test_filters)
add_test(NAME nql_parse COMMAND test_nql_parse)
add_test(NAME nql_execute COMMAND test_nql_execute)
add_test(NAME nql_windows COMMAND test_nql_windows)
add_test(NAME nql_schema COMMAND test_nql_schema)
add_test(NAME file_input COMMAND test_file_input)
add_test(NAME metrics_output COMMAND test_metrics_output)
add_test(NAME world COMMAND test_world)
add_test(NAME correlation COMMAND test_correlation)
add_test(NAME config COMMAND test_config)
add_test(NAME integration_file COMMAND test_integration_file)
add_test(NAME integration_correlation COMMAND test_integration_correlation)
add_test(NAME integration_config COMMAND test_integration_config)
add_test(NAME integration_output COMMAND test_integration_output)

message(STATUS "Unit tests configured")
