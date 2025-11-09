/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_nql.c - Unit tests for nQL parser and executor
 *
 * Licensed under the Apache License, Version 2.0
 */

#include <check.h>
#include <string.h>
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

  json_object_set(event->data, "log.level", json_string("INFO"));
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

  json_object_set(event->data, "log.level", json_string("INFO"));
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

  json_object_set(event->data, "log.level", json_string("INFO"));
  ck_assert_int_eq(nql_execute(expr, event, world), 0);

  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_nql_execute_aggregate_where) {
  nblex_world* world = NULL;
  nblex_input* input = NULL;
  nblex_event* event =
      build_event_with_field(&world, &input, "log.level", json_string("ERROR"));

  json_object_set_new(event->data, "log.service", json_string("api"));

  const char* expr =
      "aggregate count() by log.service where log.level == \"ERROR\"";
  ck_assert_int_eq(nql_execute(expr, event, world), 1);

  json_object_set(event->data, "log.level", json_string("INFO"));
  ck_assert_int_eq(nql_execute(expr, event, world), 0);

  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

/* Event capture for testing */
static nblex_event* captured_event = NULL;

static void capture_event_handler(nblex_event* event, void* user_data) {
  (void)user_data;
  if (captured_event) {
    nblex_event_free(captured_event);
  }
  captured_event = calloc(1, sizeof(nblex_event));
  if (captured_event && event) {
    memcpy(captured_event, event, sizeof(nblex_event));
    if (event->data) {
      json_incref(event->data);
      captured_event->data = event->data;
    }
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
  suite_add_tcase(s, tc_parse);

  TCase* tc_execute = tcase_create("Execute");
  tcase_add_test(tc_execute, test_nql_execute_filter);
  tcase_add_test(tc_execute, test_nql_execute_pipeline);
  tcase_add_test(tc_execute, test_nql_execute_show_where);
  tcase_add_test(tc_execute, test_nql_execute_aggregate_where);
  tcase_add_test(tc_execute, test_nql_execute_aggregate_emits_event);
  tcase_add_test(tc_execute, test_nql_execute_aggregate_group_by);
  tcase_add_test(tc_execute, test_nql_execute_correlate_emits_event);
  suite_add_tcase(s, tc_execute);

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

