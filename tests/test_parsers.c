/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_parsers.c - Unit tests for log parsers
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include <check.h>
#include "../src/nblex_internal.h"

/* Forward declarations for functions not in public API */
json_t* nblex_parse_logfmt_line(const char* line);
json_t* nblex_parse_syslog_line(const char* line);

/* Test suite declaration */
Suite* parsers_suite(void);

START_TEST(test_json_parser) {
    /* Test valid JSON */
    json_t* result = nblex_parse_json_line("{\"level\":\"INFO\",\"message\":\"test\"}");
    ck_assert_ptr_ne(result, NULL);
    ck_assert(json_is_object(result));

    json_t* level = json_object_get(result, "level");
    ck_assert_ptr_ne(level, NULL);
    ck_assert_str_eq(json_string_value(level), "INFO");

    json_decref(result);

    /* Test invalid JSON */
    result = nblex_parse_json_line("{invalid json");
    ck_assert_ptr_eq(result, NULL);
}
END_TEST

START_TEST(test_logfmt_parser) {
    /* Test valid logfmt */
    json_t* result = nblex_parse_logfmt_line("level=INFO message=\"test message\" count=42");
    ck_assert_ptr_ne(result, NULL);
    ck_assert(json_is_object(result));

    json_t* level = json_object_get(result, "level");
    ck_assert_ptr_ne(level, NULL);
    ck_assert_str_eq(json_string_value(level), "INFO");

    json_t* count = json_object_get(result, "count");
    ck_assert_ptr_ne(count, NULL);
    ck_assert_int_eq(json_integer_value(count), 42);

    json_decref(result);

    /* Test empty input */
    result = nblex_parse_logfmt_line("");
    ck_assert_ptr_eq(result, NULL);
}
END_TEST

START_TEST(test_syslog_parser) {
    /* Test RFC 3164 syslog */
    json_t* result = nblex_parse_syslog_line("<34>Oct 11 22:14:15 mymachine su: 'su root' failed for user on /dev/pts/8");
    ck_assert_ptr_ne(result, NULL);
    ck_assert(json_is_object(result));

    json_t* priority = json_object_get(result, "syslog_priority");
    ck_assert_ptr_ne(priority, NULL);
    ck_assert_int_eq(json_integer_value(priority), 34);

    json_t* facility = json_object_get(result, "syslog_facility");
    ck_assert_ptr_ne(facility, NULL);
    ck_assert_int_eq(json_integer_value(facility), 4); /* auth */

    json_t* severity = json_object_get(result, "syslog_severity");
    ck_assert_ptr_ne(severity, NULL);
    ck_assert_int_eq(json_integer_value(severity), 2); /* crit */

    json_decref(result);

    /* Test invalid syslog */
    result = nblex_parse_syslog_line("not syslog");
    ck_assert_ptr_eq(result, NULL);
}
END_TEST

Suite* parsers_suite(void) {
    Suite* s = suite_create("Parsers");

    TCase* tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_json_parser);
    tcase_add_test(tc_core, test_logfmt_parser);
    tcase_add_test(tc_core, test_syslog_parser);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void) {
    int number_failed;
    Suite* s = parsers_suite();
    SRunner* sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
