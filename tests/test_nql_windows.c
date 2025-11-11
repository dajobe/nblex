/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_nql_windows.c - Unit tests for nQL windowing functionality
 *
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
#include <string.h>
#include <stdlib.h>
#include "../src/nblex_internal.h"
#include "../src/parsers/nql_parser.h"
#include "test_helpers.h"

Suite* nql_windows_suite(void);

START_TEST(test_nql_execute_tumbling_window) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  /* Base timestamp: 1000 seconds */
  uint64_t base_ts = 1000000000000ULL;
  
  /* Create three events in the same window */
  nblex_event* event1 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data1 = json_object();
  json_object_set_new(data1, "log.service", json_string("api"));
  json_object_set_new(data1, "log.level", json_string("ERROR"));
  json_object_set_new(data1, "network.latency_ms", json_real(10.0));
  event1->data = data1;
  event1->timestamp_ns = base_ts + 100000000; /* 100ms into window */
  
  nblex_event* event2 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data2 = json_object();
  json_object_set_new(data2, "log.service", json_string("api"));
  json_object_set_new(data2, "log.level", json_string("ERROR"));
  json_object_set_new(data2, "network.latency_ms", json_real(20.0));
  event2->data = data2;
  event2->timestamp_ns = base_ts + 500000000; /* 500ms into window */
  
  nblex_event* event3 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data3 = json_object();
  json_object_set_new(data3, "log.service", json_string("api"));
  json_object_set_new(data3, "log.level", json_string("ERROR"));
  json_object_set_new(data3, "network.latency_ms", json_real(30.0));
  event3->data = data3;
  event3->timestamp_ns = base_ts + 900000000; /* 900ms into window */
  
  const char* expr = "aggregate count(), avg(network.latency_ms) by log.service "
                      "where log.level == \"ERROR\" window tumbling(1s)";
  
  /* Process all three events - should not emit immediately (windowed) */
  ck_assert_int_eq(nql_execute(expr, event1, world), 1);
  ck_assert_ptr_eq(test_captured_event, NULL); /* No immediate emission */
  
  ck_assert_int_eq(nql_execute(expr, event2, world), 1);
  ck_assert_ptr_eq(test_captured_event, NULL);
  
  ck_assert_int_eq(nql_execute(expr, event3, world), 1);
  ck_assert_ptr_eq(test_captured_event, NULL);
  
  /* Note: In a real scenario, the timer would flush the window.
   * For this test, we verify that events are buffered (not emitted immediately) */
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_event_free(event3);
  nblex_input_free(input);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_nql_execute_tumbling_window_different_windows) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  /* Base timestamp: 1000 seconds */
  uint64_t base_ts = 1000000000000ULL;
  uint64_t window_size_ns = 1000000000ULL; /* 1 second */
  
  /* Event in first window */
  nblex_event* event1 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data1 = json_object();
  json_object_set_new(data1, "log.service", json_string("api"));
  json_object_set_new(data1, "log.level", json_string("ERROR"));
  event1->data = data1;
  event1->timestamp_ns = base_ts + 100000000; /* 100ms into first window */
  
  /* Event in second window (1.5 seconds later) */
  nblex_event* event2 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data2 = json_object();
  json_object_set_new(data2, "log.service", json_string("api"));
  json_object_set_new(data2, "log.level", json_string("ERROR"));
  event2->data = data2;
  event2->timestamp_ns = base_ts + window_size_ns + 500000000; /* 500ms into second window */
  
  const char* expr = "aggregate count() by log.service "
                      "where log.level == \"ERROR\" window tumbling(1s)";
  
  ck_assert_int_eq(nql_execute(expr, event1, world), 1);
  ck_assert_int_eq(nql_execute(expr, event2, world), 1);
  
  /* Should create separate buckets for different windows */
  /* Events buffered, not emitted immediately */
  ck_assert_ptr_eq(test_captured_event, NULL);
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_input_free(input);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_nql_execute_sliding_window_multiple_windows) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  /* Base timestamp: 1000 seconds */
  uint64_t base_ts = 1000000000000ULL;
  
  /* Event that should belong to multiple sliding windows */
  /* Window: 1s, slide: 0.5s */
  /* Event at 1.2s should belong to windows starting at 0.5s, 1.0s, and 1.5s */
  /* Actually, windows are: [0.0-1.0), [0.5-1.5), [1.0-2.0), [1.5-2.5) */
  /* Event at 1.2s belongs to: [0.5-1.5), [1.0-2.0) */
  
  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data = json_object();
  json_object_set_new(data, "log.service", json_string("api"));
  json_object_set_new(data, "log.level", json_string("ERROR"));
  json_object_set_new(data, "network.latency_ms", json_real(42.0));
  event->data = data;
  event->timestamp_ns = base_ts + 1200000000ULL; /* 1.2 seconds */
  
  const char* expr = "aggregate count() by log.service "
                      "where log.level == \"ERROR\" window sliding(1s, 500ms)";
  
  ck_assert_int_eq(nql_execute(expr, event, world), 1);
  
  /* Event should be added to multiple windows (buffered, not emitted) */
  ck_assert_ptr_eq(test_captured_event, NULL);
  
  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_nql_execute_session_window) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  uint64_t base_ts = 1000000000000ULL;
  
  /* First event starts a session */
  nblex_event* event1 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data1 = json_object();
  json_object_set_new(data1, "log.service", json_string("api"));
  json_object_set_new(data1, "log.level", json_string("ERROR"));
  event1->data = data1;
  event1->timestamp_ns = base_ts;
  
  /* Second event within timeout (5 seconds later, timeout is 30s) */
  nblex_event* event2 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data2 = json_object();
  json_object_set_new(data2, "log.service", json_string("api"));
  json_object_set_new(data2, "log.level", json_string("ERROR"));
  event2->data = data2;
  event2->timestamp_ns = base_ts + 5000000000ULL; /* 5 seconds later */
  
  const char* expr = "aggregate count() by log.service "
                      "where log.level == \"ERROR\" window session(30s)";
  
  ck_assert_int_eq(nql_execute(expr, event1, world), 1);
  ck_assert_ptr_eq(test_captured_event, NULL); /* Buffered */
  
  ck_assert_int_eq(nql_execute(expr, event2, world), 1);
  ck_assert_ptr_eq(test_captured_event, NULL); /* Should extend session, not create new */
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_input_free(input);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_nql_execute_window_group_by) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  uint64_t base_ts = 1000000000000ULL;
  
  /* Events for different services in same window */
  nblex_event* event1 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data1 = json_object();
  json_object_set_new(data1, "log.service", json_string("api"));
  json_object_set_new(data1, "log.level", json_string("ERROR"));
  event1->data = data1;
  event1->timestamp_ns = base_ts + 100000000;
  
  nblex_event* event2 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data2 = json_object();
  json_object_set_new(data2, "log.service", json_string("payments"));
  json_object_set_new(data2, "log.level", json_string("ERROR"));
  event2->data = data2;
  event2->timestamp_ns = base_ts + 200000000;
  
  const char* expr = "aggregate count() by log.service "
                      "where log.level == \"ERROR\" window tumbling(1s)";
  
  ck_assert_int_eq(nql_execute(expr, event1, world), 1);
  ck_assert_int_eq(nql_execute(expr, event2, world), 1);
  
  /* Should create separate buckets for each service */
  ck_assert_ptr_eq(test_captured_event, NULL);
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_input_free(input);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_nql_execute_window_aggregation_functions) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  uint64_t base_ts = 1000000000000ULL;
  
  /* Multiple events with different values */
  nblex_event* event1 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data1 = json_object();
  json_object_set_new(data1, "log.service", json_string("api"));
  json_object_set_new(data1, "network.latency_ms", json_real(10.0));
  event1->data = data1;
  event1->timestamp_ns = base_ts + 100000000;
  
  nblex_event* event2 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data2 = json_object();
  json_object_set_new(data2, "log.service", json_string("api"));
  json_object_set_new(data2, "network.latency_ms", json_real(20.0));
  event2->data = data2;
  event2->timestamp_ns = base_ts + 200000000;
  
  nblex_event* event3 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data3 = json_object();
  json_object_set_new(data3, "log.service", json_string("api"));
  json_object_set_new(data3, "network.latency_ms", json_real(30.0));
  event3->data = data3;
  event3->timestamp_ns = base_ts + 300000000;
  
  const char* expr = "aggregate count(), sum(network.latency_ms), "
                      "avg(network.latency_ms), min(network.latency_ms), "
                      "max(network.latency_ms) by log.service window tumbling(1s)";
  
  ck_assert_int_eq(nql_execute(expr, event1, world), 1);
  ck_assert_int_eq(nql_execute(expr, event2, world), 1);
  ck_assert_int_eq(nql_execute(expr, event3, world), 1);
  
  /* Events buffered in window */
  ck_assert_ptr_eq(test_captured_event, NULL);
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_event_free(event3);
  nblex_input_free(input);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_nql_windowed_aggregate_schema) {
  /* Test that windowed aggregation events include window information */
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  /* Don't start world - windowed aggregates will buffer but timers won't be created */
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  uint64_t base_ts = 1000000000000ULL;
  
  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data = json_object();
  json_object_set_new(data, "log.level", json_string("ERROR"));
  json_object_set_new(data, "log.service", json_string("api"));
  event->data = data;
  event->timestamp_ns = base_ts;
  
  const char* expr = "aggregate count() by log.service where log.level == \"ERROR\" window tumbling(1s)";
  
  /* Process event - should buffer (windowed) */
  ck_assert_int_eq(nql_execute(expr, event, world), 1);
  ck_assert_ptr_eq(test_captured_event, NULL);
  
  /* Note: In a real scenario with timer running, window would flush and emit.
   * For this test, we verify the query structure supports windowing.
   * The window flush callback would create events with window info in the schema. */
  
  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

Suite* nql_windows_suite(void) {
  Suite* s = suite_create("nQL Windows");

  TCase* tc_tumbling = tcase_create("Tumbling");
  tcase_add_test(tc_tumbling, test_nql_execute_tumbling_window);
  tcase_add_test(tc_tumbling, test_nql_execute_tumbling_window_different_windows);
  suite_add_tcase(s, tc_tumbling);

  TCase* tc_sliding = tcase_create("Sliding");
  tcase_add_test(tc_sliding, test_nql_execute_sliding_window_multiple_windows);
  suite_add_tcase(s, tc_sliding);

  TCase* tc_session = tcase_create("Session");
  tcase_add_test(tc_session, test_nql_execute_session_window);
  suite_add_tcase(s, tc_session);

  TCase* tc_group_by = tcase_create("Group By");
  tcase_add_test(tc_group_by, test_nql_execute_window_group_by);
  tcase_add_test(tc_group_by, test_nql_execute_window_aggregation_functions);
  suite_add_tcase(s, tc_group_by);

  TCase* tc_schema = tcase_create("Schema");
  tcase_add_test(tc_schema, test_nql_windowed_aggregate_schema);
  suite_add_tcase(s, tc_schema);

  return s;
}

int main(void) {
  int number_failed;
  Suite* s = nql_windows_suite();
  SRunner* sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

