/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_filters.c - Unit tests for filter engine
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

/* Feature test macros must be defined before any system headers */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <check.h>
#include "../src/nblex_internal.h"

/* Forward declarations for functions not in public API */
typedef struct filter_s filter_t;
filter_t* nblex_filter_new(const char* expression);
void nblex_filter_free(filter_t* filter);
int nblex_filter_matches(const filter_t* filter, const nblex_event* event);

/* Test suite declaration */
Suite* filters_suite(void);

START_TEST(test_filter_equals) {
    /* Create world for testing */
    nblex_world* world = nblex_world_new();
    ck_assert_ptr_ne(world, NULL);

    /* Create dummy input */
    nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
    ck_assert_ptr_ne(input, NULL);

    /* Create filter */
    filter_t* filter = nblex_filter_new("level == \"INFO\"");
    ck_assert_ptr_ne(filter, NULL);

    /* Create test event */
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    ck_assert_ptr_ne(event, NULL);

    json_t* data = json_object();
    json_object_set_new(data, "level", json_string("INFO"));
    event->data = data;

    /* Test matching */
    ck_assert_int_eq(nblex_filter_matches(filter, event), 1);

    /* Test non-matching */
    json_object_set(data, "level", json_string("ERROR"));
    ck_assert_int_eq(nblex_filter_matches(filter, event), 0);

    /* Cleanup */
    nblex_event_free(event);
    nblex_filter_free(filter);
    nblex_input_free(input);
    nblex_world_free(world);
}
END_TEST

START_TEST(test_filter_numeric) {
    /* Create world for testing */
    nblex_world* world = nblex_world_new();
    ck_assert_ptr_ne(world, NULL);

    /* Create dummy input */
    nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
    ck_assert_ptr_ne(input, NULL);

    /* Create filter */
    filter_t* filter = nblex_filter_new("status >= 400");
    ck_assert_ptr_ne(filter, NULL);

    /* Create test event */
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    ck_assert_ptr_ne(event, NULL);

    json_t* data = json_object();
    json_object_set_new(data, "status", json_integer(500));
    event->data = data;

    /* Test matching */
    ck_assert_int_eq(nblex_filter_matches(filter, event), 1);

    /* Test non-matching */
    json_object_set(data, "status", json_integer(200));
    ck_assert_int_eq(nblex_filter_matches(filter, event), 0);

    /* Cleanup */
    nblex_event_free(event);
    nblex_filter_free(filter);
    nblex_input_free(input);
    nblex_world_free(world);
}
END_TEST

START_TEST(test_filter_and_or) {
    /* Create world for testing */
    nblex_world* world = nblex_world_new();
    ck_assert_ptr_ne(world, NULL);

    /* Create dummy input */
    nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
    ck_assert_ptr_ne(input, NULL);

    /* Create AND filter */
    filter_t* filter = nblex_filter_new("level == \"ERROR\" AND status >= 500");
    ck_assert_ptr_ne(filter, NULL);

    /* Create test event */
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    ck_assert_ptr_ne(event, NULL);

    json_t* data = json_object();
    json_object_set_new(data, "level", json_string("ERROR"));
    json_object_set_new(data, "status", json_integer(500));
    event->data = data;

    /* Test matching (both conditions true) */
    ck_assert_int_eq(nblex_filter_matches(filter, event), 1);

    /* Test non-matching (one condition false) */
    json_object_set(data, "status", json_integer(400));
    ck_assert_int_eq(nblex_filter_matches(filter, event), 0);

    /* Cleanup */
    nblex_event_free(event);
    nblex_filter_free(filter);
    nblex_input_free(input);
    nblex_world_free(world);
}
END_TEST

Suite* filters_suite(void) {
    Suite* s = suite_create("Filters");

    TCase* tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_filter_equals);
    tcase_add_test(tc_core, test_filter_numeric);
    tcase_add_test(tc_core, test_filter_and_or);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void) {
    int number_failed;
    Suite* s = filters_suite();
    SRunner* sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
