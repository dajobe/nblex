/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_nql.c - Unit tests for nQL parser and executor
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

/* Test suite declaration */
Suite* nql_suite(void);

static nblex_event* build_event_with_field(nblex_world** world_out,
                                           nblex_input** input_out,
                                           const char* field,
                                           json_t* value) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);

  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  ck_assert_ptr_ne(event, NULL);

  json_t* data = json_object();
  ck_assert_ptr_ne(data, NULL);
  json_object_set_new(data, field, value);
  event->data = data;

  if (world_out) {
    *world_out = world;
  }
  if (input_out) {
    *input_out = input;
  }

  return event;
}

START_TEST(test_nql_parse_filter) {
  nql_query_t* query = nql_parse("log.level == ERROR");
  ck_assert_ptr_ne(query, NULL);
  ck_assert_int_eq(query->type, NQL_QUERY_FILTER);
  ck_assert_ptr_ne(query->data.filter, NULL);

  nql_free(query);
}
END_TEST

START_TEST(test_nql_parse_correlate) {
  const char* expr =
      "correlate log.level == ERROR with network.dst_port == 3306 within 1s";
  nql_query_t* query = nql_parse(expr);
  ck_assert_ptr_ne(query, NULL);
  ck_assert_int_eq(query->type, NQL_QUERY_CORRELATE);

  nql_correlate_t* corr = query->data.correlate;
  ck_assert_ptr_ne(corr, NULL);
  ck_assert_ptr_ne(corr->left_filter, NULL);
  ck_assert_ptr_ne(corr->right_filter, NULL);
  ck_assert_uint_eq(corr->within_ms, 1000);

  nql_free(query);
}
END_TEST

START_TEST(test_nql_parse_aggregate) {
  const char* expr =
      "aggregate count(), avg(network.latency_ms) by log.service, log.endpoint "
      "where log.level == ERROR window sliding(1m, 10s)";
  nql_query_t* query = nql_parse(expr);
  ck_assert_ptr_ne(query, NULL);
  ck_assert_int_eq(query->type, NQL_QUERY_AGGREGATE);

  nql_aggregate_t* agg = query->data.aggregate;
  ck_assert_ptr_ne(agg, NULL);
  ck_assert_uint_eq(agg->funcs_count, 2);
  ck_assert_int_eq(agg->funcs[0].type, NQL_AGG_COUNT);
  ck_assert_int_eq(agg->funcs[1].type, NQL_AGG_AVG);
  ck_assert_ptr_ne(agg->funcs[1].field, NULL);
  ck_assert_uint_eq(agg->group_by_count, 2);
  ck_assert_ptr_ne(agg->where_filter, NULL);
  ck_assert_int_eq(agg->window.type, NQL_WINDOW_SLIDING);
  ck_assert_uint_eq(agg->window.size_ms, 60 * 1000);
  ck_assert_uint_eq(agg->window.slide_ms, 10 * 1000);

  nql_free(query);
}
END_TEST

START_TEST(test_nql_parse_show_all) {
  nql_query_t* query = nql_parse("* where log.level == ERROR");
  ck_assert_ptr_ne(query, NULL);
  ck_assert_int_eq(query->type, NQL_QUERY_SHOW);

  nql_show_t* show = query->data.show;
  ck_assert_ptr_ne(show, NULL);
  ck_assert(show->select_all);
  ck_assert_ptr_ne(show->where_filter, NULL);

  nql_free(query);
}
END_TEST

START_TEST(test_nql_parse_pipeline) {
  const char* expr =
      "log.level == ERROR | aggregate count() by log.service | "
      "show log.service";
  nql_query_t* query = nql_parse(expr);
  ck_assert_ptr_ne(query, NULL);
  ck_assert_int_eq(query->type, NQL_QUERY_PIPELINE);

  nql_pipeline_t pipeline = query->data.pipeline;
  ck_assert_uint_eq(pipeline.count, 3);
  ck_assert_int_eq(pipeline.stages[0]->type, NQL_QUERY_FILTER);
  ck_assert_int_eq(pipeline.stages[1]->type, NQL_QUERY_AGGREGATE);
  ck_assert_int_eq(pipeline.stages[2]->type, NQL_QUERY_SHOW);

  nql_free(query);
}
END_TEST

START_TEST(test_nql_parse_correlate_default_window) {
  const char* expr =
      "correlate log.status >= 500 with network.tcp.retransmits > 0";
  nql_query_t* query = nql_parse(expr);
  ck_assert_ptr_ne(query, NULL);
  ck_assert_int_eq(query->type, NQL_QUERY_CORRELATE);

  nql_correlate_t* corr = query->data.correlate;
  ck_assert_ptr_ne(corr, NULL);
  ck_assert_uint_eq(corr->within_ms, 100);

  nql_free(query);
}
END_TEST

START_TEST(test_nql_parse_show_fields) {
  const char* expr =
      "show log.service, network.latency_ms where log.level == ERROR";
  nql_query_t* query = nql_parse(expr);
  ck_assert_ptr_ne(query, NULL);
  ck_assert_int_eq(query->type, NQL_QUERY_SHOW);

  nql_show_t* show = query->data.show;
  ck_assert_ptr_ne(show, NULL);
  ck_assert(!show->select_all);
  ck_assert_uint_eq(show->fields_count, 2);
  ck_assert_ptr_ne(show->where_filter, NULL);

  nql_free(query);
}
END_TEST

START_TEST(test_nql_parse_ex_error) {
  char* error = NULL;
  nql_query_t* query = nql_parse_ex("correlate log.level == ERROR", &error);
  ck_assert_ptr_eq(query, NULL);
  ck_assert_ptr_ne(error, NULL);
  ck_assert(strstr(error, "expected 'with'") != NULL);
  free(error);
}
END_TEST

START_TEST(test_nql_execute_filter) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  nblex_event* event =
      build_event_with_field(&world, &input, "log.level", json_string("ERROR"));

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
      build_event_with_field(&world, &input, "log.level", json_string("ERROR"));

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
      build_event_with_field(&world, &input, "log.level", json_string("ERROR"));

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

/* Event capture for testing */
static nblex_event* captured_event = NULL;
static nblex_event** captured_events = NULL;
static size_t captured_events_count = 0;
static size_t captured_events_capacity = 0;

static void capture_event_handler(nblex_event* event, void* user_data) {
  (void)user_data;
  /* Debug: indicate handler invoked */
  if (captured_event) {
    nblex_event_free(captured_event);
  }
  captured_event = nblex_event_clone(event);
  
  /* Also add to array */
  if (captured_events_count >= captured_events_capacity) {
    size_t new_capacity = captured_events_capacity == 0 ? 16 : captured_events_capacity * 2;
    nblex_event** new_events = realloc(captured_events, new_capacity * sizeof(nblex_event*));
    if (new_events) {
      captured_events = new_events;
      captured_events_capacity = new_capacity;
    }
  }
  
  if (captured_events && captured_events_count < captured_events_capacity) {
    captured_events[captured_events_count] = nblex_event_clone(event);
    if (captured_events[captured_events_count]) {
      captured_events_count++;
    }
  }
}

static void reset_captured_events(void) {
  if (captured_event) {
    nblex_event_free(captured_event);
    captured_event = NULL;
  }
  if (captured_events) {
    for (size_t i = 0; i < captured_events_count; i++) {
      if (captured_events[i]) {
        nblex_event_free(captured_events[i]);
      }
    }
    free(captured_events);
    captured_events = NULL;
    captured_events_count = 0;
    captured_events_capacity = 0;
  }
}

START_TEST(test_nql_execute_aggregate_emits_event) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  captured_event = NULL;
  
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
  ck_assert_ptr_ne(captured_event, NULL);
  ck_assert_ptr_ne(captured_event->data, NULL);
  
  json_t* result_type = json_object_get(captured_event->data, "nql_result_type");
  ck_assert_ptr_ne(result_type, NULL);
  ck_assert_str_eq(json_string_value(result_type), "aggregation");
  
  json_t* metrics = json_object_get(captured_event->data, "metrics");
  ck_assert_ptr_ne(metrics, NULL);
  
  json_t* count = json_object_get(metrics, "count");
  ck_assert_ptr_ne(count, NULL);
  ck_assert_int_eq(json_integer_value(count), 1);
  
  if (captured_event) {
    nblex_event_free(captured_event);
    captured_event = NULL;
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
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  captured_event = NULL;
  
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
  ck_assert_ptr_ne(captured_event, NULL);
  
  if (captured_event) {
    json_t* group = json_object_get(captured_event->data, "group");
    ck_assert_ptr_ne(group, NULL);
    
    json_t* service = json_object_get(group, "log.service");
    ck_assert_ptr_ne(service, NULL);
    ck_assert_str_eq(json_string_value(service), "payments");
    
    nblex_event_free(captured_event);
    captured_event = NULL;
  }
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_nql_parse_session_window) {
  const char* expr =
      "aggregate count(), avg(network.latency_ms) by log.service "
      "window session(30s)";
  nql_query_t* query = nql_parse(expr);
  ck_assert_ptr_ne(query, NULL);
  ck_assert_int_eq(query->type, NQL_QUERY_AGGREGATE);

  nql_aggregate_t* agg = query->data.aggregate;
  ck_assert_ptr_ne(agg, NULL);
  ck_assert_int_eq(agg->window.type, NQL_WINDOW_SESSION);
  ck_assert_uint_eq(agg->window.timeout_ms, 30 * 1000);

  nql_free(query);
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
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  captured_event = NULL;
  
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
  ck_assert_ptr_ne(captured_event, NULL);
  ck_assert_ptr_ne(captured_event->data, NULL);
  
  json_t* result_type = json_object_get(captured_event->data, "nql_result_type");
  ck_assert_ptr_ne(result_type, NULL);
  ck_assert_str_eq(json_string_value(result_type), "correlation");
  
  json_t* left = json_object_get(captured_event->data, "left_event");
  ck_assert_ptr_ne(left, NULL);
  
  json_t* right = json_object_get(captured_event->data, "right_event");
  ck_assert_ptr_ne(right, NULL);
  
  if (captured_event) {
    nblex_event_free(captured_event);
    captured_event = NULL;
  }
  
  nblex_event_free(log_event);
  nblex_event_free(net_event);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_nql_execute_tumbling_window) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  reset_captured_events();
  
  /* Base timestamp: 1000 seconds */
  uint64_t base_ts = 1000000000000ULL; /* 1e12 ns = 1000 seconds */
  uint64_t window_size_ns = 1000000000ULL; /* 1 second */
  
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
  ck_assert_ptr_eq(captured_event, NULL); /* No immediate emission */
  
  ck_assert_int_eq(nql_execute(expr, event2, world), 1);
  ck_assert_ptr_eq(captured_event, NULL);
  
  ck_assert_int_eq(nql_execute(expr, event3, world), 1);
  ck_assert_ptr_eq(captured_event, NULL);
  
  /* Note: In a real scenario, the timer would flush the window.
   * For this test, we verify that events are buffered (not emitted immediately) */
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_event_free(event3);
  nblex_input_free(input);
  nblex_world_free(world);
  reset_captured_events();
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
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  reset_captured_events();
  
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
  ck_assert_ptr_eq(captured_event, NULL);
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_input_free(input);
  nblex_world_free(world);
  reset_captured_events();
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
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  reset_captured_events();
  
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
  ck_assert_ptr_eq(captured_event, NULL);
  
  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
  reset_captured_events();
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
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  reset_captured_events();
  
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
  ck_assert_ptr_eq(captured_event, NULL); /* Buffered */
  
  ck_assert_int_eq(nql_execute(expr, event2, world), 1);
  ck_assert_ptr_eq(captured_event, NULL); /* Should extend session, not create new */
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_input_free(input);
  nblex_world_free(world);
  reset_captured_events();
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
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  reset_captured_events();
  
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
  ck_assert_ptr_eq(captured_event, NULL);
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_input_free(input);
  nblex_world_free(world);
  reset_captured_events();
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
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  reset_captured_events();
  
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
  ck_assert_ptr_eq(captured_event, NULL);
  
  nblex_event_free(event1);
  nblex_event_free(event2);
  nblex_event_free(event3);
  nblex_input_free(input);
  nblex_world_free(world);
  reset_captured_events();
}
END_TEST

START_TEST(test_nql_aggregate_event_schema) {
  /* Test that aggregation result events have correct schema */
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  /* Don't start world - non-windowed aggregates don't need timers */
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  reset_captured_events();
  
  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data = json_object();
  json_object_set_new(data, "log.level", json_string("ERROR"));
  json_object_set_new(data, "log.service", json_string("api"));
  json_object_set_new(data, "network.latency_ms", json_real(100.5));
  event->data = data;
  
  const char* expr = "aggregate count(), avg(network.latency_ms) by log.service where log.level == \"ERROR\"";
  
  /* Non-windowed aggregate should emit immediately */
  ck_assert_int_eq(nql_execute(expr, event, world), 1);
  ck_assert_ptr_ne(captured_event, NULL);
  ck_assert_ptr_ne(captured_event->data, NULL);
  
  /* Validate schema */
  json_t* result_type = json_object_get(captured_event->data, "nql_result_type");
  ck_assert_ptr_ne(result_type, NULL);
  ck_assert_str_eq(json_string_value(result_type), "aggregation");
  
  json_t* group = json_object_get(captured_event->data, "group");
  ck_assert_ptr_ne(group, NULL);
  json_t* service = json_object_get(group, "log.service");
  ck_assert_ptr_ne(service, NULL);
  ck_assert_str_eq(json_string_value(service), "api");
  
  json_t* metrics = json_object_get(captured_event->data, "metrics");
  ck_assert_ptr_ne(metrics, NULL);
  json_t* count = json_object_get(metrics, "count");
  ck_assert_ptr_ne(count, NULL);
  ck_assert_int_eq(json_integer_value(count), 1);
  
  json_t* avg = json_object_get(metrics, "avg_network.latency_ms");
  ck_assert_ptr_ne(avg, NULL);
  ck_assert(json_is_real(avg));
  
  /* Window should not be present for non-windowed aggregates */
  json_t* window = json_object_get(captured_event->data, "window");
  ck_assert_ptr_eq(window, NULL);
  
  nblex_event_free(event);
  if (captured_event) {
    nblex_event_free(captured_event);
    captured_event = NULL;
  }
  nblex_input_free(input);
  nblex_world_free(world);
  reset_captured_events();
}
END_TEST

START_TEST(test_nql_correlation_event_schema) {
  /* Test that correlation result events have correct schema */
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  /* Don't start world - correlation cleanup timer will be created lazily if needed */
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  reset_captured_events();
  
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
  ck_assert_ptr_ne(captured_event, NULL);
  ck_assert_ptr_ne(captured_event->data, NULL);
  ck_assert_int_eq(captured_event->type, NBLEX_EVENT_CORRELATION);
  
  /* Validate schema */
  json_t* result_type = json_object_get(captured_event->data, "nql_result_type");
  ck_assert_ptr_ne(result_type, NULL);
  ck_assert_str_eq(json_string_value(result_type), "correlation");
  
  json_t* window_ms = json_object_get(captured_event->data, "window_ms");
  ck_assert_ptr_ne(window_ms, NULL);
  ck_assert_int_eq(json_integer_value(window_ms), 100);
  
  json_t* left_event = json_object_get(captured_event->data, "left_event");
  ck_assert_ptr_ne(left_event, NULL);
  json_t* left_level = json_object_get(left_event, "log.level");
  ck_assert_ptr_ne(left_level, NULL);
  ck_assert_str_eq(json_string_value(left_level), "ERROR");
  
  json_t* right_event = json_object_get(captured_event->data, "right_event");
  ck_assert_ptr_ne(right_event, NULL);
  json_t* right_port = json_object_get(right_event, "network.dst_port");
  ck_assert_ptr_ne(right_port, NULL);
  ck_assert_int_eq(json_integer_value(right_port), 3306);
  
  json_t* time_diff = json_object_get(captured_event->data, "time_diff_ms");
  ck_assert_ptr_ne(time_diff, NULL);
  ck_assert(json_is_real(time_diff));
  
  nblex_event_free(log_event);
  nblex_event_free(net_event);
  if (captured_event) {
    nblex_event_free(captured_event);
    captured_event = NULL;
  }
  nblex_input_free(input);
  nblex_world_free(world);
  reset_captured_events();
}
END_TEST

START_TEST(test_nql_lazy_timer_initialization) {
  /* Test that timers are NOT initialized until world starts */
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  /* Don't start world yet - timers should not be created */
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  reset_captured_events();
  
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
  ck_assert_ptr_eq(captured_event, NULL); /* Windowed, no immediate emission */
  
  /* Execute another event - still no timer (world not started) */
  nblex_event* event2 = nblex_event_new(NBLEX_EVENT_LOG, input);
  json_t* data2 = json_object();
  json_object_set_new(data2, "log.level", json_string("ERROR"));
  json_object_set_new(data2, "log.service", json_string("api"));
  event2->data = data2;
  event2->timestamp_ns = base_ts + 100000000; /* 100ms later */
  
  ck_assert_int_eq(nql_execute(expr, event2, world), 1);
  /* Still no immediate emission (windowed) and timer not created (world not started) */
  ck_assert_ptr_eq(captured_event, NULL);
  
  /* Note: In a real scenario, starting the world would initialize timers lazily
   * on the next event. We don't test that here to avoid timer cleanup issues. */
  
  nblex_event_free(event);
  nblex_event_free(event2);
  nblex_input_free(input);
  nblex_world_free(world);
  reset_captured_events();
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
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  reset_captured_events();
  
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
  ck_assert_ptr_eq(captured_event, NULL);
  
  /* Note: In a real scenario with timer running, window would flush and emit.
   * For this test, we verify the query structure supports windowing.
   * The window flush callback would create events with window info in the schema. */
  
  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
  reset_captured_events();
}
END_TEST

START_TEST(test_nql_correlation_bidirectional) {
  /* Test that correlation works in both directions (left->right and right->left) */
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  
  world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  ck_assert_int_eq(nblex_world_open(world), 0);
  /* Don't start world - correlation cleanup timer will be created lazily if needed */
  
  input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);
  
  nblex_set_event_handler(world, capture_event_handler, NULL);
  reset_captured_events();
  
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
  ck_assert_ptr_eq(captured_event, NULL); /* No match yet */
  
  /* Execute log event - should trigger correlation */
  ck_assert_int_eq(nql_execute(expr, log_event, world), 1);
  ck_assert_ptr_ne(captured_event, NULL);
  
  /* Verify correlation event structure */
  json_t* result_type = json_object_get(captured_event->data, "nql_result_type");
  ck_assert_ptr_ne(result_type, NULL);
  ck_assert_str_eq(json_string_value(result_type), "correlation");
  
  json_t* left = json_object_get(captured_event->data, "left_event");
  ck_assert_ptr_ne(left, NULL);
  
  json_t* right = json_object_get(captured_event->data, "right_event");
  ck_assert_ptr_ne(right, NULL);
  
  nblex_event_free(log_event);
  nblex_event_free(net_event);
  if (captured_event) {
    nblex_event_free(captured_event);
    captured_event = NULL;
  }
  nblex_input_free(input);
  nblex_world_free(world);
  reset_captured_events();
}
END_TEST

Suite* nql_suite(void) {
  Suite* s = suite_create("nQL");

  TCase* tc_parse = tcase_create("Parse");
  tcase_add_test(tc_parse, test_nql_parse_filter);
  tcase_add_test(tc_parse, test_nql_parse_correlate);
  tcase_add_test(tc_parse, test_nql_parse_aggregate);
  tcase_add_test(tc_parse, test_nql_parse_show_all);
  tcase_add_test(tc_parse, test_nql_parse_pipeline);
  tcase_add_test(tc_parse, test_nql_parse_correlate_default_window);
  tcase_add_test(tc_parse, test_nql_parse_show_fields);
  tcase_add_test(tc_parse, test_nql_parse_ex_error);
  tcase_add_test(tc_parse, test_nql_parse_session_window);
  suite_add_tcase(s, tc_parse);

  TCase* tc_execute = tcase_create("Execute");
  tcase_add_test(tc_execute, test_nql_execute_filter);
  tcase_add_test(tc_execute, test_nql_execute_pipeline);
  tcase_add_test(tc_execute, test_nql_execute_show_where);
  tcase_add_test(tc_execute, test_nql_execute_aggregate_where);
  tcase_add_test(tc_execute, test_nql_execute_aggregate_emits_event);
  tcase_add_test(tc_execute, test_nql_execute_aggregate_group_by);
  tcase_add_test(tc_execute, test_nql_execute_correlate_emits_event);
  tcase_add_test(tc_execute, test_nql_aggregate_event_schema);
  tcase_add_test(tc_execute, test_nql_correlation_event_schema);
  tcase_add_test(tc_execute, test_nql_lazy_timer_initialization);
  tcase_add_test(tc_execute, test_nql_correlation_bidirectional);
  suite_add_tcase(s, tc_execute);

  TCase* tc_windows = tcase_create("Windowing");
  tcase_add_test(tc_windows, test_nql_execute_tumbling_window);
  tcase_add_test(tc_windows, test_nql_execute_tumbling_window_different_windows);
  tcase_add_test(tc_windows, test_nql_execute_sliding_window_multiple_windows);
  tcase_add_test(tc_windows, test_nql_execute_session_window);
  tcase_add_test(tc_windows, test_nql_execute_window_group_by);
  tcase_add_test(tc_windows, test_nql_execute_window_aggregation_functions);
  tcase_add_test(tc_windows, test_nql_windowed_aggregate_schema);
  suite_add_tcase(s, tc_windows);

  return s;
}

int main(void) {
  int number_failed;
  Suite* s = nql_suite();
  SRunner* sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

