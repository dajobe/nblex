/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_nql_parse.c - Unit tests for nQL parser
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

Suite* nql_parse_suite(void);

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

Suite* nql_parse_suite(void) {
  Suite* s = suite_create("nQL Parse");

  TCase* tc_core = tcase_create("Core");
  tcase_add_test(tc_core, test_nql_parse_filter);
  tcase_add_test(tc_core, test_nql_parse_correlate);
  tcase_add_test(tc_core, test_nql_parse_aggregate);
  tcase_add_test(tc_core, test_nql_parse_show_all);
  tcase_add_test(tc_core, test_nql_parse_pipeline);
  tcase_add_test(tc_core, test_nql_parse_correlate_default_window);
  tcase_add_test(tc_core, test_nql_parse_show_fields);
  tcase_add_test(tc_core, test_nql_parse_ex_error);
  suite_add_tcase(s, tc_core);

  TCase* tc_windows = tcase_create("Windows");
  tcase_add_test(tc_windows, test_nql_parse_session_window);
  suite_add_tcase(s, tc_windows);

  return s;
}

int main(void) {
  int number_failed;
  Suite* s = nql_parse_suite();
  SRunner* sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

