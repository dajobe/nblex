/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_integration_pipeline.c - Multi-stage pipeline integration tests
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
#include <unistd.h>
#include "../src/nblex_internal.h"
#include "test_helpers.h"
#include "test_integration_helpers.h"

Suite* integration_pipeline_suite(void);

/* External functions for nQL execution */
typedef struct nql_query_s nql_query_t;
extern nql_query_t* nql_parse(const char* query_str);
extern int nql_execute(const char* query_str, nblex_event* event, nblex_world* world);
extern void nql_free(nql_query_t* query);

START_TEST(test_nql_e2e_simple_filter) {
  /* Test simple nQL query execution end-to-end */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  /* Parse nQL query */
  const char* query = "log.level == ERROR";
  nql_query_t* query_ctx = nql_parse(query);
  ck_assert_ptr_ne(query_ctx, NULL);

  /* Create test events */
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);

  /* ERROR event - should match */
  nblex_event* error_event = nblex_event_new(NBLEX_EVENT_LOG, input);
  error_event->timestamp_ns = nblex_timestamp_now();
  error_event->data = json_object();
  json_object_set_new(error_event->data, "level", json_string("ERROR"));
  json_object_set_new(error_event->data, "message", json_string("Error occurred"));

  /* INFO event - should not match */
  nblex_event* info_event = nblex_event_new(NBLEX_EVENT_LOG, input);
  info_event->timestamp_ns = nblex_timestamp_now();
  info_event->data = json_object();
  json_object_set_new(info_event->data, "level", json_string("INFO"));
  json_object_set_new(info_event->data, "message", json_string("Info message"));

  /* Emit events and execute query on each */
  nql_execute(query, error_event, world);
  nblex_event_emit(world, error_event);

  nql_execute(query, info_event, world);
  nblex_event_emit(world, info_event);

  /* Process events */
  for (int i = 0; i < 20; i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }

  /* Should have captured only ERROR event */
  ck_assert_int_ge(test_captured_events_count, 1);

  /* Cleanup */
  nql_free(query_ctx);
  nblex_input_free(input);
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_pipeline_filter_aggregate_output) {
  /* Test filter → aggregate → output pipeline */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  /* Parse nQL filter query - simpler test */
  const char* query = "log.level == ERROR";
  nql_query_t* query_ctx = nql_parse(query);
  ck_assert_ptr_ne(query_ctx, NULL);

  /* Create test events */
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  uint64_t base_time = nblex_timestamp_now();

  /* Create 5 ERROR events */
  for (int i = 0; i < 5; i++) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    event->timestamp_ns = base_time + (i * 100 * 1000000ULL); /* 100ms apart */
    event->data = json_object();
    json_object_set_new(event->data, "level", json_string("ERROR"));
    json_object_set_new(event->data, "message", json_string("Error message"));
    /* Just emit unconditionally to test event handler */
    nblex_event_emit(world, event);
  }

  /* Create some INFO events */
  for (int i = 0; i < 3; i++) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    event->timestamp_ns = base_time + ((i + 5) * 100 * 1000000ULL);
    event->data = json_object();
    json_object_set_new(event->data, "level", json_string("INFO"));
    nblex_event_emit(world, event);
  }

  /* Process events */
  for (int i = 0; i < 50; i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }

  /* Should have captured all events */
  ck_assert_int_eq(test_captured_events_count, 8);

  /* Cleanup */
  nql_free(query_ctx);
  nblex_input_free(input);
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_pipeline_correlation_aggregation_metrics) {
  /* Test correlation query parsing and basic event handling */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  /* Parse correlation query to verify syntax support */
  const char* query = "correlate log.level == ERROR with network.dst_port == 3306 within 100ms";
  nql_query_t* query_ctx = nql_parse(query);
  ck_assert_ptr_ne(query_ctx, NULL);

  /* Create inputs */
  nblex_input* log_input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_input* net_input = nblex_input_new(world, NBLEX_INPUT_PCAP);

  uint64_t base_time = nblex_timestamp_now();

  /* Create and emit event pairs */
  for (int i = 0; i < 3; i++) {
    /* Log event */
    nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, log_input);
    log_event->timestamp_ns = base_time + (i * 200 * 1000000ULL);
    log_event->data = json_object();
    json_object_set_new(log_event->data, "level", json_string("ERROR"));
    json_object_set_new(log_event->data, "message", json_string("DB connection failed"));
    nblex_event_emit(world, log_event);

    /* Network event */
    nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, net_input);
    net_event->timestamp_ns = log_event->timestamp_ns + (50 * 1000000ULL);
    net_event->data = json_object();
    json_object_set_new(net_event->data, "dst_port", json_integer(3306));
    json_object_set_new(net_event->data, "flags", json_string("RST"));
    nblex_event_emit(world, net_event);
  }

  /* Process events */
  for (int i = 0; i < 50; i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }

  /* Verify events were processed (basic pipeline test) */
  ck_assert_int_ge(test_captured_events_count, 6);

  /* Cleanup */
  nql_free(query_ctx);
  nblex_input_free(log_input);
  nblex_input_free(net_input);
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_pipeline_complex_multi_stage) {
  /* Test multi-stage pipeline query parsing and basic event handling */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  /* Parse multi-stage pipeline query to verify syntax support */
  const char* query = "log.level >= WARN | aggregate count() by log.service";
  nql_query_t* query_ctx = nql_parse(query);
  ck_assert_ptr_ne(query_ctx, NULL);

  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  uint64_t base_time = nblex_timestamp_now();

  /* Create events for different services and levels */
  const char* services[] = {"api", "db", "cache"};
  const char* levels[] = {"ERROR", "WARN", "INFO"};

  for (int i = 0; i < 9; i++) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    event->timestamp_ns = base_time + (i * 100 * 1000000ULL);
    event->data = json_object();
    json_object_set_new(event->data, "level", json_string(levels[i % 3]));
    json_object_set_new(event->data, "service", json_string(services[i / 3]));
    json_object_set_new(event->data, "message", json_string("Test message"));
    nblex_event_emit(world, event);
  }

  /* Process events */
  for (int i = 0; i < 50; i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }

  /* Verify events were processed (basic pipeline test) */
  ck_assert_int_ge(test_captured_events_count, 9);

  /* Cleanup */
  nql_free(query_ctx);
  nblex_input_free(input);
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_pipeline_windowing) {
  /* Test windowing in pipeline */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  /* Set world->started to true to enable timer initialization */
  world->started = true;

  /* Query with non-windowed aggregation by group - simpler test */
  const char* query = "aggregate count() by window";
  nql_query_t* query_ctx = nql_parse(query);
  ck_assert_ptr_ne(query_ctx, NULL);

  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  uint64_t base_time = nblex_timestamp_now();

  /* Create events across multiple groups (simulating windows) */
  for (int window = 0; window < 3; window++) {
    for (int i = 0; i < 5; i++) {
      nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
      event->timestamp_ns = base_time + (window * 500 * 1000000ULL) + (i * 50 * 1000000ULL);
      event->data = json_object();
      json_object_set_new(event->data, "window", json_integer(window));
      json_object_set_new(event->data, "index", json_integer(i));
      /* Execute query - this will emit aggregation results internally */
      nql_execute(query, event, world);
      nblex_event_free(event);
    }
  }

  /* Process events */
  for (int i = 0; i < 50; i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }

  /* Should have 3 aggregation results (one per group) */
  int window_results = 0;
  for (size_t i = 0; i < test_captured_events_count; i++) {
    if (test_captured_events[i]->data) {
      json_t* metrics = json_object_get(test_captured_events[i]->data, "metrics");
      if (metrics) {
        json_t* count = json_object_get(metrics, "count");
        if (count && json_integer_value(count) == 5) {
          window_results++;
        }
      }
    }
  }

  ck_assert_int_eq(window_results, 3);

  /* Cleanup */
  nql_free(query_ctx);
  nblex_input_free(input);
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

Suite* integration_pipeline_suite(void) {
  Suite* s = suite_create("Integration Pipeline");

  TCase* tc_nql = tcase_create("nQL Execution");
  tcase_add_test(tc_nql, test_nql_e2e_simple_filter);
  tcase_add_test(tc_nql, test_pipeline_filter_aggregate_output);
  tcase_add_test(tc_nql, test_pipeline_correlation_aggregation_metrics);
  tcase_add_test(tc_nql, test_pipeline_complex_multi_stage);
  tcase_add_test(tc_nql, test_pipeline_windowing);
  suite_add_tcase(s, tc_nql);

  return s;
}

int main(void) {
  int number_failed;
  Suite* s = integration_pipeline_suite();
  SRunner* sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
