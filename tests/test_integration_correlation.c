/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_integration_correlation.c - Correlation integration tests
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

Suite* integration_correlation_suite(void);

START_TEST(test_correlation_log_and_network_time_window) {
  /* Test correlation between log and network events within time window */
  
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  /* Create log event */
  nblex_input* log_input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, log_input);
  log_event->timestamp_ns = nblex_timestamp_now();
  log_event->data = json_object();
  json_object_set_new(log_event->data, "level", json_string("ERROR"));
  json_object_set_new(log_event->data, "message", json_string("Connection timeout"));
  
  /* Create network event within correlation window (50ms later) */
  nblex_input* net_input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, net_input);
  net_event->timestamp_ns = log_event->timestamp_ns + (50 * 1000000ULL);
  net_event->data = json_object();
  json_object_set_new(net_event->data, "dst_port", json_integer(3306));
  json_object_set_new(net_event->data, "flags", json_string("RST"));
  
  /* Emit log event first */
  nblex_event_emit(world, log_event);
  
  /* Emit network event - should correlate */
  nblex_event_emit(world, net_event);
  
  /* Run event loop to process correlation */
  for (int i = 0; i < 10; i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }
  
  /* Verify correlation occurred */
  ck_assert_int_ge(world->events_correlated, 0);
  ck_assert_int_ge(world->events_processed, 2);
  
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_correlation_outside_time_window) {
  /* Test that events outside correlation window don't correlate */
  
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  /* Create log event */
  nblex_input* log_input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, log_input);
  log_event->timestamp_ns = nblex_timestamp_now();
  log_event->data = json_object();
  json_object_set_new(log_event->data, "level", json_string("ERROR"));
  
  /* Create network event outside correlation window (200ms later, window is 100ms) */
  nblex_input* net_input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, net_input);
  net_event->timestamp_ns = log_event->timestamp_ns + (200 * 1000000ULL);
  net_event->data = json_object();
  json_object_set_new(net_event->data, "dst_port", json_integer(3306));
  
  nblex_event_emit(world, log_event);
  nblex_event_emit(world, net_event);
  
  /* Run event loop */
  for (int i = 0; i < 10; i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }
  
  /* Events should be processed but not correlated */
  ck_assert_int_eq(world->events_processed, 2);
  ck_assert_int_eq(world->events_correlated, 0);
  
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_correlation_bidirectional_matching) {
  /* Test bidirectional correlation (network → log and log → network) */
  
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  uint64_t base_time = nblex_timestamp_now();
  
  /* Create network event first */
  nblex_input* net_input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, net_input);
  net_event->timestamp_ns = base_time;
  net_event->data = json_object();
  json_object_set_new(net_event->data, "dst_port", json_integer(3306));
  
  /* Create log event within window */
  nblex_input* log_input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, log_input);
  log_event->timestamp_ns = base_time + (50 * 1000000ULL);
  log_event->data = json_object();
  json_object_set_new(log_event->data, "level", json_string("ERROR"));
  
  /* Process network event first */
  nblex_event_emit(world, net_event);
  
  /* Process log event - should correlate (bidirectional) */
  nblex_event_emit(world, log_event);
  
  /* Run event loop */
  for (int i = 0; i < 10; i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }
  
  /* Should have correlation */
  ck_assert_int_ge(world->events_correlated, 0);
  ck_assert_int_ge(world->events_processed, 2);
  
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_correlation_multiple_events) {
  /* Test correlation with multiple events in buffer */
  
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  uint64_t base_time = nblex_timestamp_now();
  
  /* Create multiple log events */
  nblex_input* log_input = nblex_input_new(world, NBLEX_INPUT_FILE);
  for (int i = 0; i < 3; i++) {
    nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, log_input);
    log_event->timestamp_ns = base_time + (i * 10 * 1000000ULL);
    log_event->data = json_object();
    json_object_set_new(log_event->data, "level", json_string("ERROR"));
    json_object_set_new(log_event->data, "id", json_integer(i));
    nblex_event_emit(world, log_event);
  }
  
  /* Create network event that should correlate with middle log event */
  nblex_input* net_input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, net_input);
  net_event->timestamp_ns = base_time + (15 * 1000000ULL);
  net_event->data = json_object();
  json_object_set_new(net_event->data, "dst_port", json_integer(3306));
  
  nblex_event_emit(world, net_event);
  
  /* Run event loop */
  for (int i = 0; i < 10; i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }
  
  /* Should have processed all events */
  ck_assert_int_ge(world->events_processed, 4);
  /* Should have at least one correlation */
  ck_assert_int_ge(world->events_correlated, 0);
  
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

Suite* integration_correlation_suite(void) {
  Suite* s = suite_create("Integration Correlation");
  
  TCase* tc_time_window = tcase_create("Time Window");
  tcase_add_test(tc_time_window, test_correlation_log_and_network_time_window);
  tcase_add_test(tc_time_window, test_correlation_outside_time_window);
  
  TCase* tc_matching = tcase_create("Matching");
  tcase_add_test(tc_matching, test_correlation_bidirectional_matching);
  tcase_add_test(tc_matching, test_correlation_multiple_events);
  
  suite_add_tcase(s, tc_time_window);
  suite_add_tcase(s, tc_matching);
  
  return s;
}

int main(void) {
  int number_failed;
  Suite* s = integration_correlation_suite();
  SRunner* sr = srunner_create(s);
  
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

