/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_world.c - Unit tests for nblex world management
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

/* Feature test macros must be defined before any system headers */
#ifndef __APPLE__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include "../src/nblex_internal.h"
#include "test_helpers.h"

/* Test suite declaration */
Suite* world_suite(void);

START_TEST(test_world_new) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_ptr_ne(world->loop, NULL);
  ck_assert_int_eq(world->opened, false);
  ck_assert_int_eq(world->started, false);
  ck_assert_int_eq(world->running, false);
  ck_assert_int_eq(world->inputs_count, 0);
  ck_assert_int_eq(world->inputs_capacity, 8);
  ck_assert_ptr_ne(world->inputs, NULL);
  
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_new_null_on_error) {
  /* Test that world_new handles allocation failures gracefully */
  /* This is hard to test without mocking, but we can at least verify
   * normal operation works */
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_open) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  int rc = nblex_world_open(world);
  ck_assert_int_eq(rc, 0);
  ck_assert_int_eq(world->opened, true);
  ck_assert_ptr_ne(world->correlation, NULL);
  
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_open_null_world) {
  int rc = nblex_world_open(NULL);
  ck_assert_int_eq(rc, -1);
}
END_TEST

START_TEST(test_world_open_already_opened) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  nblex_world_open(world);
  int rc = nblex_world_open(world);
  ck_assert_int_eq(rc, -1);
  
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_free_null) {
  /* Should not crash */
  nblex_world_free(NULL);
}
END_TEST

START_TEST(test_world_free_cleanup) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  nblex_world_open(world);
  nblex_world_free(world);
  /* If we get here without crashing, cleanup worked */
}
END_TEST

START_TEST(test_world_start) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  nblex_world_open(world);
  
  int rc = nblex_world_start(world);
  ck_assert_int_eq(rc, 0);
  ck_assert_int_eq(world->started, true);
  
  /* Stop before freeing - world_free will handle cleanup */
  nblex_world_stop(world);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_start_not_opened) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  int rc = nblex_world_start(world);
  ck_assert_int_eq(rc, -1);
  ck_assert_int_eq(world->started, false);
  
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_start_null_world) {
  int rc = nblex_world_start(NULL);
  ck_assert_int_eq(rc, -1);
}
END_TEST

START_TEST(test_world_start_already_started) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  nblex_world_open(world);
  nblex_world_start(world);
  
  int rc = nblex_world_start(world);
  ck_assert_int_eq(rc, -1);
  
  /* Stop before freeing - world_free will handle cleanup */
  nblex_world_stop(world);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_stop) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  nblex_world_open(world);
  nblex_world_start(world);
  
  int rc = nblex_world_stop(world);
  ck_assert_int_eq(rc, 0);
  ck_assert_int_eq(world->started, false);
  ck_assert_int_eq(world->running, false);
  
  /* world_free will handle cleanup including running event loop for correlation timer */
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_stop_null_world) {
  int rc = nblex_world_stop(NULL);
  ck_assert_int_eq(rc, -1);
}
END_TEST

START_TEST(test_world_run_not_started) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  nblex_world_open(world);
  
  int rc = nblex_world_run(world);
  ck_assert_int_eq(rc, -1);
  
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_run_null_world) {
  int rc = nblex_world_run(NULL);
  ck_assert_int_eq(rc, -1);
}
END_TEST

START_TEST(test_world_set_event_handler) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  int rc = nblex_set_event_handler(world, test_capture_event_handler, NULL);
  ck_assert_int_eq(rc, 0);
  ck_assert_ptr_eq(world->event_handler, test_capture_event_handler);
  ck_assert_ptr_eq(world->event_handler_data, NULL);
  
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_set_event_handler_null_world) {
  int rc = nblex_set_event_handler(NULL, test_capture_event_handler, NULL);
  ck_assert_int_eq(rc, -1);
}
END_TEST

START_TEST(test_world_add_input) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  nblex_world_open(world);
  
  /* Create a mock input */
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  int rc = nblex_world_add_input(world, input);
  ck_assert_int_eq(rc, 0);
  ck_assert_int_eq(world->inputs_count, 1);
  ck_assert_ptr_eq(world->inputs[0], input);
  
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_add_input_null_world) {
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  
  int rc = nblex_world_add_input(NULL, input);
  ck_assert_int_eq(rc, -1);
  
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_add_input_null_input) {
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  
  int rc = nblex_world_add_input(world, NULL);
  ck_assert_int_eq(rc, -1);
  
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_add_input_grow_array) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  nblex_world_open(world);
  
  /* Add inputs until we exceed initial capacity */
  for (int i = 0; i < 10; i++) {
    nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
    ck_assert_ptr_ne(input, NULL);
    
    int rc = nblex_world_add_input(world, input);
    ck_assert_int_eq(rc, 0);
  }
  
  ck_assert_int_eq(world->inputs_count, 10);
  ck_assert_int_ge(world->inputs_capacity, 10);
  
  nblex_world_free(world);
}
END_TEST

START_TEST(test_world_event_emission) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  /* Create and emit an event */
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  event->data = json_object();
  json_object_set_new(event->data, "level", json_string("INFO"));
  
  nblex_event_emit(world, event);
  
  /* Check that event was captured */
  ck_assert_ptr_ne(test_captured_event, NULL);
  ck_assert_int_eq(test_captured_event->type, NBLEX_EVENT_LOG);
  
  /* Note: nblex_event_emit frees the event, so we don't free it here */
  /* Stop before freeing - world_free will handle cleanup */
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

Suite* world_suite(void) {
  Suite* s = suite_create("World");
  TCase* tc_core = tcase_create("Core");
  
  tcase_add_test(tc_core, test_world_new);
  tcase_add_test(tc_core, test_world_new_null_on_error);
  tcase_add_test(tc_core, test_world_open);
  tcase_add_test(tc_core, test_world_open_null_world);
  tcase_add_test(tc_core, test_world_open_already_opened);
  tcase_add_test(tc_core, test_world_free_null);
  tcase_add_test(tc_core, test_world_free_cleanup);
  
  TCase* tc_lifecycle = tcase_create("Lifecycle");
  tcase_add_test(tc_lifecycle, test_world_start);
  tcase_add_test(tc_lifecycle, test_world_start_not_opened);
  tcase_add_test(tc_lifecycle, test_world_start_null_world);
  tcase_add_test(tc_lifecycle, test_world_start_already_started);
  tcase_add_test(tc_lifecycle, test_world_stop);
  tcase_add_test(tc_lifecycle, test_world_stop_null_world);
  tcase_add_test(tc_lifecycle, test_world_run_not_started);
  tcase_add_test(tc_lifecycle, test_world_run_null_world);
  
  TCase* tc_inputs = tcase_create("Inputs");
  tcase_add_test(tc_inputs, test_world_add_input);
  tcase_add_test(tc_inputs, test_world_add_input_null_world);
  tcase_add_test(tc_inputs, test_world_add_input_null_input);
  tcase_add_test(tc_inputs, test_world_add_input_grow_array);
  
  TCase* tc_events = tcase_create("Events");
  tcase_add_test(tc_events, test_world_set_event_handler);
  tcase_add_test(tc_events, test_world_set_event_handler_null_world);
  tcase_add_test(tc_events, test_world_event_emission);
  
  suite_add_tcase(s, tc_core);
  suite_add_tcase(s, tc_lifecycle);
  suite_add_tcase(s, tc_inputs);
  suite_add_tcase(s, tc_events);
  
  return s;
}

int main(void) {
  int number_failed;
  Suite* s = world_suite();
  SRunner* sr = srunner_create(s);
  
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

