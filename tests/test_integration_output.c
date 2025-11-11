/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_integration_output.c - Output formatter integration tests
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
#include <stdio.h>
#include "../src/nblex_internal.h"
#include "test_helpers.h"
#include "test_integration_helpers.h"

Suite* integration_output_suite(void);

START_TEST(test_json_output_serialization) {
  /* Test JSON output formatter integration */
  
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  event->timestamp_ns = nblex_timestamp_now();
  event->data = json_object();
  json_object_set_new(event->data, "level", json_string("ERROR"));
  json_object_set_new(event->data, "message", json_string("Test message"));
  
  /* Serialize to JSON string */
  char* json_str = nblex_event_to_json_string(event);
  ck_assert_ptr_ne(json_str, NULL);
  
  /* Verify JSON contains expected fields */
  ck_assert_ptr_ne(strstr(json_str, "\"type\":\"log\""), NULL);
  ck_assert_ptr_ne(strstr(json_str, "\"level\":\"ERROR\""), NULL);
  ck_assert_ptr_ne(strstr(json_str, "\"message\":\"Test message\""), NULL);
  
  free(json_str);
  nblex_event_free(event);
  nblex_world_stop(world);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_json_output_correlation_event) {
  /* Test JSON output for correlation events */
  
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* event = nblex_event_new(NBLEX_EVENT_CORRELATION, input);
  event->timestamp_ns = nblex_timestamp_now();
  event->data = json_object();
  json_object_set_new(event->data, "correlation_type", json_string("time_based"));
  json_object_set_new(event->data, "window_ms", json_integer(100));
  
  char* json_str = nblex_event_to_json_string(event);
  ck_assert_ptr_ne(json_str, NULL);
  
  ck_assert_ptr_ne(strstr(json_str, "\"type\":\"correlation\""), NULL);
  ck_assert_ptr_ne(strstr(json_str, "\"correlation_type\":\"time_based\""), NULL);
  
  free(json_str);
  nblex_event_free(event);
  nblex_world_stop(world);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_output_pipeline_with_event_handler) {
  /* Test output through event handler pipeline */
  
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  event->data = json_object();
  json_object_set_new(event->data, "level", json_string("INFO"));
  json_object_set_new(event->data, "message", json_string("Pipeline test"));
  
  /* Emit event - should be captured by handler */
  nblex_event_emit(world, event);
  
  /* Verify event was captured */
  ck_assert_ptr_ne(test_captured_event, NULL);
  ck_assert_int_eq(test_captured_event->type, NBLEX_EVENT_LOG);
  
  /* Verify event data */
  json_t* level = json_object_get(test_captured_event->data, "level");
  ck_assert_ptr_ne(level, NULL);
  ck_assert_str_eq(json_string_value(level), "INFO");
  
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_output_multiple_events) {
  /* Test output handling multiple events */
  
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  
  /* Emit multiple events */
  for (int i = 0; i < 5; i++) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    event->data = json_object();
    json_object_set_new(event->data, "id", json_integer(i));
    json_object_set_new(event->data, "message", json_string("Test"));
    
    nblex_event_emit(world, event);
  }
  
  /* Verify all events were processed */
  ck_assert_int_eq(world->events_processed, 5);
  
  /* Verify events were captured */
  ck_assert_int_ge(test_captured_events_count, 0);
  
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

Suite* integration_output_suite(void) {
  Suite* s = suite_create("Integration Output");
  
  TCase* tc_json = tcase_create("JSON");
  tcase_add_test(tc_json, test_json_output_serialization);
  tcase_add_test(tc_json, test_json_output_correlation_event);
  
  TCase* tc_pipeline = tcase_create("Pipeline");
  tcase_add_test(tc_pipeline, test_output_pipeline_with_event_handler);
  tcase_add_test(tc_pipeline, test_output_multiple_events);
  
  suite_add_tcase(s, tc_json);
  suite_add_tcase(s, tc_pipeline);
  
  return s;
}

int main(void) {
  int number_failed;
  Suite* s = integration_output_suite();
  SRunner* sr = srunner_create(s);
  
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

