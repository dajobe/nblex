/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_nql_execute.c - Unit tests for nQL executor (basic execution)
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

Suite* nql_execute_suite(void);

START_TEST(test_nql_execute_filter) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  nblex_event* event =
      test_build_event_with_field(&world, &input, "log.level", json_string("ERROR"));

  ck_assert_int_eq(nql_execute("log.level == \"ERROR\"", event, world), 1);

  json_object_set_new(event->data, "log.level", json_string("INFO"));
  ck_assert_int_eq(nql_execute("log.level == \"ERROR\"", event, world), 0);

  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_nql_execute_pipeline) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  nblex_event* event =
      test_build_event_with_field(&world, &input, "log.level", json_string("ERROR"));

  const char* expr =
      "log.level == \"ERROR\" | show log.level where log.level == \"ERROR\"";
  ck_assert_int_eq(nql_execute(expr, event, world), 1);

  json_object_set_new(event->data, "log.level", json_string("INFO"));
  ck_assert_int_eq(nql_execute(expr, event, world), 0);

  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_nql_execute_show_where) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  nblex_event* event =
      test_build_event_with_field(&world, &input, "log.level", json_string("ERROR"));

  json_object_set_new(event->data, "log.service", json_string("payments"));

  const char* expr = "show log.service where log.level == \"ERROR\"";
  ck_assert_int_eq(nql_execute(expr, event, world), 1);

  json_object_set_new(event->data, "log.level", json_string("INFO"));
  ck_assert_int_eq(nql_execute(expr, event, world), 0);

  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_nql_execute_aggregate_where) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  ck_assert_ptr_ne(event, NULL);
  
  json_t* data = json_object();
  ck_assert_ptr_ne(data, NULL);
  json_object_set_new(data, "log.level", json_string("ERROR"));
  json_object_set_new(data, "log.service", json_string("api"));
  event->data = data;

  const char* expr =
      "aggregate count() by log.service where log.level == \"ERROR\"";
  ck_assert_int_eq(nql_execute(expr, event, world), 1);

  json_object_set_new(event->data, "log.level", json_string("INFO"));
  ck_assert_int_eq(nql_execute(expr, event, world), 0);

  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_nql_execute_aggregate_emits_event) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_captured_event = NULL;
  
  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  ck_assert_ptr_ne(event, NULL);
  
  json_t* data = json_object();
  json_object_set_new(data, "log.level", json_string("ERROR"));
  json_object_set_new(data, "log.service", json_string("api"));
  json_object_set_new(data, "network.latency_ms", json_real(42.5));
  event->data = data;
  
  const char* expr = "aggregate count(), avg(network.latency_ms) where log.level == \"ERROR\"";
  ck_assert_int_eq(nql_execute(expr, event, world), 1);
  
  /* For non-windowed aggregates, event should be emitted immediately */
  ck_assert_ptr_ne(test_captured_event, NULL);
  ck_assert_ptr_ne(test_captured_event->data, NULL);
  
  json_t* result_type = json_object_get(test_captured_event->data, "nql_result_type");
  ck_assert_ptr_ne(result_type, NULL);
  ck_assert_str_eq(json_string_value(result_type), "aggregation");
  
  json_t* metrics = json_object_get(test_captured_event->data, "metrics");
  ck_assert_ptr_ne(metrics, NULL);
  
  json_t* count = json_object_get(metrics, "count");
  ck_assert_ptr_ne(count, NULL);
  ck_assert_int_eq(json_integer_value(count), 1);
  
  if (test_captured_event) {
    nblex_event_free(test_captured_event);
    test_captured_event = NULL;
  }
  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_nql_execute_aggregate_group_by) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_captured_event = NULL;
  
  /* First event for service "api" */
  nblex_event* event1 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data1 = json_object();
  json_object_set_new(data1, "log.service", json_string("api"));
  json_object_set_new(data1, "log.level", json_string("ERROR"));
  event1->data = data1;
  
  const char* expr = "aggregate count() by log.service where log.level == \"ERROR\"";
  ck_assert_int_eq(nql_execute(expr, event1, world), 1);
  
  /* Second event for service "payments" */
  nblex_event* event2 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data2 = json_object();
  json_object_set_new(data2, "log.service", json_string("payments"));
  json_object_set_new(data2, "log.level", json_string("ERROR"));
  event2->data = data2;
  
  ck_assert_int_eq(nql_execute(expr, event2, world), 1);
  
  /* Should have two separate buckets */
  ck_assert_ptr_ne(test_captured_event, NULL);
  
  if (test_captured_event) {
    json_t* group = json_object_get(test_captured_event->data, "group");
    ck_assert_ptr_ne(group, NULL);
    
    json_t* service = json_object_get(group, "log.service");
    ck_assert_ptr_ne(service, NULL);
    ck_assert_str_eq(json_string_value(service), "payments");
    
    nblex_event_free(test_captured_event);
    test_captured_event = NULL;
  }
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_nql_execute_correlate_emits_event) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_captured_event = NULL;
  
  /* Create log event */
  nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* log_data = json_object();
  json_object_set_new(log_data, "log.level", json_string("ERROR"));
  log_event->data = log_data;
  log_event->timestamp_ns = nblex_timestamp_now();
  
  /* Create network event shortly after */
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, input);
  json_t* net_data = json_object();
  json_object_set_new(net_data, "network.dst_port", json_integer(3306));
  net_event->data = net_data;
  net_event->timestamp_ns = log_event->timestamp_ns + 50000000; /* 50ms later */
  
  const char* expr = "correlate log.level == \"ERROR\" with network.dst_port == 3306 within 100ms";
  
  /* Execute log event first */
  ck_assert_int_eq(nql_execute(expr, log_event, world), 1);
  
  /* Execute network event - should trigger correlation */
  ck_assert_int_eq(nql_execute(expr, net_event, world), 1);
  
  /* Should have correlation event */
  ck_assert_ptr_ne(test_captured_event, NULL);
  ck_assert_ptr_ne(test_captured_event->data, NULL);
  
  json_t* result_type = json_object_get(test_captured_event->data, "nql_result_type");
  ck_assert_ptr_ne(result_type, NULL);
  ck_assert_str_eq(json_string_value(result_type), "correlation");
  
  json_t* left = json_object_get(test_captured_event->data, "left_event");
  ck_assert_ptr_ne(left, NULL);
  
  json_t* right = json_object_get(test_captured_event->data, "right_event");
  ck_assert_ptr_ne(right, NULL);
  
  if (test_captured_event) {
    nblex_event_free(test_captured_event);
    test_captured_event = NULL;
  }
  
  nblex_event_free(log_event);
  nblex_event_free(net_event);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_nql_execute_correlate_bidirectional) {
  /* Test that correlation works in both directions (left->right and right->left) */
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
  
  const char* expr = "correlate log.level == \"ERROR\" with network.dst_port == 3306 within 100ms";
  
  /* Test: right event first, then left event */
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, input);
  json_t* net_data = json_object();
  json_object_set_new(net_data, "network.dst_port", json_integer(3306));
  net_event->data = net_data;
  net_event->timestamp_ns = base_ts;
  
  nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* log_data = json_object();
  json_object_set_new(log_data, "log.level", json_string("ERROR"));
  log_event->data = log_data;
  log_event->timestamp_ns = base_ts + 50000000; /* 50ms later */
  
  /* Execute network event first */
  ck_assert_int_eq(nql_execute(expr, net_event, world), 1);
  ck_assert_ptr_eq(test_captured_event, NULL); /* No match yet */
  
  /* Execute log event - should trigger correlation */
  ck_assert_int_eq(nql_execute(expr, log_event, world), 1);
  ck_assert_ptr_ne(test_captured_event, NULL);
  
  /* Verify correlation event structure */
  json_t* result_type = json_object_get(test_captured_event->data, "nql_result_type");
  ck_assert_ptr_ne(result_type, NULL);
  ck_assert_str_eq(json_string_value(result_type), "correlation");
  
  json_t* left = json_object_get(test_captured_event->data, "left_event");
  ck_assert_ptr_ne(left, NULL);
  
  json_t* right = json_object_get(test_captured_event->data, "right_event");
  ck_assert_ptr_ne(right, NULL);
  
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

START_TEST(test_nql_execute_lazy_timer_initialization) {
  /* Test that timers are NOT initialized until world starts */
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  /* Don't start world yet - timers should not be created */
  
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
  
  /* Execute query before world is started - should still work (no timer created yet) */
  ck_assert_int_eq(nql_execute(expr, event, world), 1);
  ck_assert_ptr_eq(test_captured_event, NULL); /* Windowed, no immediate emission */
  
  /* Execute another event - still no timer (world not started) */
  nblex_event* event2 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data2 = json_object();
  json_object_set_new(data2, "log.level", json_string("ERROR"));
  json_object_set_new(data2, "log.service", json_string("api"));
  event2->data = data2;
  event2->timestamp_ns = base_ts + 100000000; /* 100ms later */
  
  ck_assert_int_eq(nql_execute(expr, event2, world), 1);
  /* Still no immediate emission (windowed) and timer not created (world not started) */
  ck_assert_ptr_eq(test_captured_event, NULL);
  
  nblex_event_free(event);
  nblex_event_free(event2);
  nblex_input_free(input);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

Suite* nql_execute_suite(void) {
  Suite* s = suite_create("nQL Execute");

  TCase* tc_filters = tcase_create("Filters");
  tcase_add_test(tc_filters, test_nql_execute_filter);
  tcase_add_test(tc_filters, test_nql_execute_pipeline);
  suite_add_tcase(s, tc_filters);

  TCase* tc_show = tcase_create("Show");
  tcase_add_test(tc_show, test_nql_execute_show_where);
  suite_add_tcase(s, tc_show);

  TCase* tc_aggregate = tcase_create("Aggregate");
  tcase_add_test(tc_aggregate, test_nql_execute_aggregate_where);
  tcase_add_test(tc_aggregate, test_nql_execute_aggregate_emits_event);
  tcase_add_test(tc_aggregate, test_nql_execute_aggregate_group_by);
  suite_add_tcase(s, tc_aggregate);

  TCase* tc_correlate = tcase_create("Correlate");
  tcase_add_test(tc_correlate, test_nql_execute_correlate_emits_event);
  tcase_add_test(tc_correlate, test_nql_execute_correlate_bidirectional);
  suite_add_tcase(s, tc_correlate);

  TCase* tc_timers = tcase_create("Timers");
  tcase_add_test(tc_timers, test_nql_execute_lazy_timer_initialization);
  suite_add_tcase(s, tc_timers);

  return s;
}

int main(void) {
  int number_failed;
  Suite* s = nql_execute_suite();
  SRunner* sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

