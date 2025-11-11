/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_nql_schema.c - Unit tests for nQL result event schemas
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

Suite* nql_schema_suite(void);

START_TEST(test_nql_aggregate_event_schema) {
  /* Test that aggregation result events have correct schema */
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data = json_object();
  json_object_set_new(data, "log.level", json_string("ERROR"));
  json_object_set_new(data, "log.service", json_string("api"));
  json_object_set_new(data, "network.latency_ms", json_real(100.5));
  event->data = data;
  
  const char* expr = "aggregate count(), avg(network.latency_ms) by log.service where log.level == \"ERROR\"";
  
  /* Non-windowed aggregate should emit immediately */
  ck_assert_int_eq(nql_execute(expr, event, world), 1);
  ck_assert_ptr_ne(test_captured_event, NULL);
  ck_assert_ptr_ne(test_captured_event->data, NULL);
  
  /* Validate schema */
  json_t* result_type = json_object_get(test_captured_event->data, "nql_result_type");
  ck_assert_ptr_ne(result_type, NULL);
  ck_assert_str_eq(json_string_value(result_type), "aggregation");
  
  json_t* group = json_object_get(test_captured_event->data, "group");
  ck_assert_ptr_ne(group, NULL);
  json_t* service = json_object_get(group, "log.service");
  ck_assert_ptr_ne(service, NULL);
  ck_assert_str_eq(json_string_value(service), "api");
  
  json_t* metrics = json_object_get(test_captured_event->data, "metrics");
  ck_assert_ptr_ne(metrics, NULL);
  json_t* count = json_object_get(metrics, "count");
  ck_assert_ptr_ne(count, NULL);
  ck_assert_int_eq(json_integer_value(count), 1);
  
  json_t* avg = json_object_get(metrics, "avg_network.latency_ms");
  ck_assert_ptr_ne(avg, NULL);
  ck_assert(json_is_real(avg));
  
  /* Window should not be present for non-windowed aggregates */
  json_t* window = json_object_get(test_captured_event->data, "window");
  ck_assert_ptr_eq(window, NULL);
  
  nblex_event_free(event);
  if (test_captured_event) {
    nblex_event_free(test_captured_event);
    test_captured_event = NULL;
  }
  nblex_input_free(input);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_nql_correlation_event_schema) {
  /* Test that correlation result events have correct schema */
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  uint64_t base_ts = nblex_timestamp_now();
  
  /* Create log event */
  nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* log_data = json_object();
  json_object_set_new(log_data, "log.level", json_string("ERROR"));
  json_object_set_new(log_data, "log.service", json_string("api"));
  log_event->data = log_data;
  log_event->timestamp_ns = base_ts;
  
  /* Create network event shortly after */
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, input);
  json_t* net_data = json_object();
  json_object_set_new(net_data, "network.dst_port", json_integer(3306));
  json_object_set_new(net_data, "network.tcp.flags", json_string("RST"));
  net_event->data = net_data;
  net_event->timestamp_ns = base_ts + 50000000; /* 50ms later */
  
  const char* expr = "correlate log.level == \"ERROR\" with network.dst_port == 3306 within 100ms";
  
  /* Execute log event first */
  ck_assert_int_eq(nql_execute(expr, log_event, world), 1);
  
  /* Execute network event - should trigger correlation */
  ck_assert_int_eq(nql_execute(expr, net_event, world), 1);
  
  /* Should have correlation event */
  ck_assert_ptr_ne(test_captured_event, NULL);
  ck_assert_ptr_ne(test_captured_event->data, NULL);
  ck_assert_int_eq(test_captured_event->type, NBLEX_EVENT_CORRELATION);
  
  /* Validate schema */
  json_t* result_type = json_object_get(test_captured_event->data, "nql_result_type");
  ck_assert_ptr_ne(result_type, NULL);
  ck_assert_str_eq(json_string_value(result_type), "correlation");
  
  json_t* window_ms = json_object_get(test_captured_event->data, "window_ms");
  ck_assert_ptr_ne(window_ms, NULL);
  ck_assert_int_eq(json_integer_value(window_ms), 100);
  
  json_t* left_event = json_object_get(test_captured_event->data, "left_event");
  ck_assert_ptr_ne(left_event, NULL);
  json_t* left_level = json_object_get(left_event, "log.level");
  ck_assert_ptr_ne(left_level, NULL);
  ck_assert_str_eq(json_string_value(left_level), "ERROR");
  
  json_t* right_event = json_object_get(test_captured_event->data, "right_event");
  ck_assert_ptr_ne(right_event, NULL);
  json_t* right_port = json_object_get(right_event, "network.dst_port");
  ck_assert_ptr_ne(right_port, NULL);
  ck_assert_int_eq(json_integer_value(right_port), 3306);
  
  json_t* time_diff = json_object_get(test_captured_event->data, "time_diff_ms");
  ck_assert_ptr_ne(time_diff, NULL);
  ck_assert(json_is_real(time_diff));
  
  nblex_event_free(log_event);
  nblex_event_free(net_event);
  if (test_captured_event) {
    nblex_event_free(test_captured_event);
    test_captured_event = NULL;
  }
  nblex_input_free(input);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

Suite* nql_schema_suite(void) {
  Suite* s = suite_create("nQL Schema");

  TCase* tc_aggregate = tcase_create("Aggregate");
  tcase_add_test(tc_aggregate, test_nql_aggregate_event_schema);
  suite_add_tcase(s, tc_aggregate);

  TCase* tc_correlate = tcase_create("Correlate");
  tcase_add_test(tc_correlate, test_nql_correlation_event_schema);
  suite_add_tcase(s, tc_correlate);

  return s;
}

int main(void) {
  int number_failed;
  Suite* s = nql_schema_suite();
  SRunner* sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

