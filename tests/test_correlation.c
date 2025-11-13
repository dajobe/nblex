/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_correlation.c - Unit tests for time-based correlation engine
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
Suite* correlation_suite(void);

START_TEST(test_correlation_new) {
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  
  nblex_correlation* corr = nblex_correlation_new(world);
  ck_assert_ptr_ne(corr, NULL);
  ck_assert_ptr_eq(corr->world, world);
  ck_assert_int_eq(corr->type, NBLEX_CORR_TIME_BASED);
  ck_assert_int_eq(corr->window_ns, 100 * 1000000ULL);
  ck_assert_ptr_eq(corr->log_events, NULL);
  ck_assert_ptr_eq(corr->network_events, NULL);
  ck_assert_int_eq(corr->log_events_count, 0);
  ck_assert_int_eq(corr->network_events_count, 0);
  ck_assert_int_eq(corr->correlations_found, 0);
  ck_assert_int_eq(corr->timer_initialized, 0);
  
  nblex_correlation_free(corr);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_correlation_new_null_world) {
  nblex_correlation* corr = nblex_correlation_new(NULL);
  ck_assert_ptr_eq(corr, NULL);
}
END_TEST

START_TEST(test_correlation_add_strategy) {
  nblex_world* world = nblex_world_new();
  nblex_correlation* corr = nblex_correlation_new(world);
  ck_assert_ptr_ne(corr, NULL);
  
  int rc = nblex_correlation_add_strategy(corr, NBLEX_CORR_TIME_BASED, 200);
  ck_assert_int_eq(rc, 0);
  ck_assert_int_eq(corr->type, NBLEX_CORR_TIME_BASED);
  ck_assert_int_eq(corr->window_ns, 200 * 1000000ULL);
  
  nblex_correlation_free(corr);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_correlation_add_strategy_null_corr) {
  int rc = nblex_correlation_add_strategy(NULL, NBLEX_CORR_TIME_BASED, 100);
  ck_assert_int_eq(rc, -1);
}
END_TEST

START_TEST(test_correlation_start) {
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_correlation* corr = nblex_correlation_new(world);
  ck_assert_ptr_ne(corr, NULL);
  
  int rc = nblex_correlation_start(corr);
  ck_assert_int_eq(rc, 0);
  ck_assert_int_eq(corr->timer_initialized, 1);
  
  /* Stop timer before freeing */
  uv_timer_stop(&corr->cleanup_timer);
  /* Let nblex_correlation_free handle closing the timer */
  nblex_correlation_free(corr);
  /* Run event loop to process timer close callbacks (required for libuv cleanup) */
  for (int i = 0; i < 10 && uv_loop_alive(world->loop); i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }
  /* nblex_world_free will handle world's correlation and event loop cleanup */
  nblex_world_free(world);
}
END_TEST

START_TEST(test_correlation_start_null_corr) {
  int rc = nblex_correlation_start(NULL);
  ck_assert_int_eq(rc, -1);
}
END_TEST

START_TEST(test_correlation_start_null_world) {
  nblex_world* world = nblex_world_new();
  nblex_correlation* corr = nblex_correlation_new(world);
  
  /* Set world to NULL to test NULL handling without use-after-free */
  corr->world = NULL;
  
  /* Should handle NULL world gracefully */
  int rc = nblex_correlation_start(corr);
  ck_assert_int_eq(rc, -1);
  
  /* Restore world pointer for proper cleanup */
  corr->world = world;
  nblex_correlation_free(corr);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_correlation_free_null) {
  /* Should not crash */
  nblex_correlation_free(NULL);
}
END_TEST

START_TEST(test_correlation_free_cleanup) {
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_correlation* corr = nblex_correlation_new(world);
  nblex_correlation_start(corr);
  
  /* Stop timer before freeing */
  uv_timer_stop(&corr->cleanup_timer);
  /* Let nblex_correlation_free handle closing the timer */
  nblex_correlation_free(corr);
  /* world_free will handle cleanup including running event loop for correlation timer */
  nblex_world_free(world);
  /* If we get here without crashing, cleanup worked */
}
END_TEST

START_TEST(test_correlation_process_log_event) {
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  nblex_correlation* corr = nblex_correlation_new(world);
  nblex_correlation_add_strategy(corr, NBLEX_CORR_TIME_BASED, 100);
  
  /* Create a log event */
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, input);
  log_event->timestamp_ns = nblex_timestamp_now();
  log_event->data = json_object();
  json_object_set_new(log_event->data, "level", json_string("ERROR"));
  
  nblex_correlation_process_event(corr, log_event);
  
  /* Event should be buffered */
  ck_assert_int_eq(corr->log_events_count, 1);
  ck_assert_ptr_ne(corr->log_events, NULL);
  
  nblex_event_free(log_event);
  nblex_input_free(input);
  nblex_correlation_free(corr);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_correlation_process_network_event) {
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  nblex_correlation* corr = nblex_correlation_new(world);
  nblex_correlation_add_strategy(corr, NBLEX_CORR_TIME_BASED, 100);
  
  /* Create a network event */
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, input);
  net_event->timestamp_ns = nblex_timestamp_now();
  net_event->data = json_object();
  json_object_set_new(net_event->data, "dst_port", json_integer(3306));
  
  nblex_correlation_process_event(corr, net_event);
  
  /* Event should be buffered */
  ck_assert_int_eq(corr->network_events_count, 1);
  ck_assert_ptr_ne(corr->network_events, NULL);
  
  nblex_event_free(net_event);
  nblex_input_free(input);
  nblex_correlation_free(corr);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_correlation_time_based_match) {
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  nblex_correlation* corr = nblex_correlation_new(world);
  nblex_correlation_add_strategy(corr, NBLEX_CORR_TIME_BASED, 100);
  
  uint64_t base_time = nblex_timestamp_now();
  
  /* Create a log event */
  nblex_input* log_input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, log_input);
  log_event->timestamp_ns = base_time;
  log_event->data = json_object();
  json_object_set_new(log_event->data, "level", json_string("ERROR"));
  
  /* Create a network event within window */
  nblex_input* net_input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, net_input);
  net_event->timestamp_ns = base_time + (50 * 1000000ULL); /* 50ms later */
  net_event->data = json_object();
  json_object_set_new(net_event->data, "dst_port", json_integer(3306));
  
  /* Process log event first */
  nblex_correlation_process_event(corr, log_event);
  ck_assert_int_eq(corr->log_events_count, 1);
  
  /* Process network event - should correlate */
  nblex_correlation_process_event(corr, net_event);
  ck_assert_int_eq(corr->correlations_found, 1);
  ck_assert_int_eq(corr->network_events_count, 1);
  
  /* Check that correlation event was emitted */
  ck_assert_ptr_ne(test_captured_event, NULL);
  ck_assert_int_eq(test_captured_event->type, NBLEX_EVENT_CORRELATION);
  
  nblex_event_free(log_event);
  nblex_event_free(net_event);
  nblex_input_free(log_input);
  nblex_input_free(net_input);
  /* Free correlation (timer not started, so safe to free directly) */
  nblex_correlation_free(corr);
  /* Stop world to ensure clean shutdown before freeing */
  nblex_world_stop(world);
  /* world_free will handle cleanup including running event loop for correlation timer */
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_correlation_time_based_no_match_outside_window) {
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  nblex_correlation* corr = nblex_correlation_new(world);
  nblex_correlation_add_strategy(corr, NBLEX_CORR_TIME_BASED, 100);
  
  uint64_t base_time = nblex_timestamp_now();
  
  /* Create a log event */
  nblex_input* log_input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, log_input);
  log_event->timestamp_ns = base_time;
  log_event->data = json_object();
  json_object_set_new(log_event->data, "level", json_string("ERROR"));
  
  /* Create a network event outside window */
  nblex_input* net_input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, net_input);
  net_event->timestamp_ns = base_time + (200 * 1000000ULL); /* 200ms later */
  net_event->data = json_object();
  json_object_set_new(net_event->data, "dst_port", json_integer(3306));
  
  /* Process log event first */
  nblex_correlation_process_event(corr, log_event);
  
  /* Process network event - should NOT correlate */
  nblex_correlation_process_event(corr, net_event);
  ck_assert_int_eq(corr->correlations_found, 0);
  
  nblex_event_free(log_event);
  nblex_event_free(net_event);
  nblex_input_free(log_input);
  nblex_input_free(net_input);
  /* Free correlation (no timer was started, so safe to free directly) */
  nblex_correlation_free(corr);
  /* Stop world to ensure clean shutdown before freeing */
  nblex_world_stop(world);
  /* Let world_free handle cleanup of world's correlation and event loop */
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_correlation_bidirectional_matching) {
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();
  
  nblex_correlation* corr = nblex_correlation_new(world);
  nblex_correlation_add_strategy(corr, NBLEX_CORR_TIME_BASED, 100);
  
  uint64_t base_time = nblex_timestamp_now();
  
  /* Create a network event first */
  nblex_input* net_input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, net_input);
  net_event->timestamp_ns = base_time;
  net_event->data = json_object();
  json_object_set_new(net_event->data, "dst_port", json_integer(3306));
  
  /* Create a log event within window */
  nblex_input* log_input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, log_input);
  log_event->timestamp_ns = base_time + (50 * 1000000ULL); /* 50ms later */
  log_event->data = json_object();
  json_object_set_new(log_event->data, "level", json_string("ERROR"));
  
  /* Process network event first */
  nblex_correlation_process_event(corr, net_event);
  
  /* Process log event - should correlate (bidirectional) */
  nblex_correlation_process_event(corr, log_event);
  ck_assert_int_eq(corr->correlations_found, 1);
  
  nblex_event_free(log_event);
  nblex_event_free(net_event);
  nblex_input_free(log_input);
  nblex_input_free(net_input);
  /* Free correlation (timer not started, so safe to free directly) */
  nblex_correlation_free(corr);
  /* Stop world to ensure clean shutdown before freeing */
  nblex_world_stop(world);
  /* world_free will handle cleanup including running event loop for correlation timer */
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_correlation_process_null_corr) {
  nblex_world* world = nblex_world_new();
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  
  /* Should not crash */
  nblex_correlation_process_event(NULL, event);
  
  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
}
END_TEST

START_TEST(test_correlation_process_null_event) {
  nblex_world* world = nblex_world_new();
  nblex_correlation* corr = nblex_correlation_new(world);
  
  /* Should not crash */
  nblex_correlation_process_event(corr, NULL);
  
  nblex_correlation_free(corr);
  nblex_world_free(world);
}
END_TEST

Suite* correlation_suite(void) {
  Suite* s = suite_create("Correlation");
  TCase* tc_core = tcase_create("Core");
  
  tcase_add_test(tc_core, test_correlation_new);
  tcase_add_test(tc_core, test_correlation_new_null_world);
  tcase_add_test(tc_core, test_correlation_add_strategy);
  tcase_add_test(tc_core, test_correlation_add_strategy_null_corr);
  tcase_add_test(tc_core, test_correlation_free_null);
  tcase_add_test(tc_core, test_correlation_free_cleanup);
  
  TCase* tc_lifecycle = tcase_create("Lifecycle");
  tcase_add_test(tc_lifecycle, test_correlation_start);
  tcase_add_test(tc_lifecycle, test_correlation_start_null_corr);
  tcase_add_test(tc_lifecycle, test_correlation_start_null_world);
  
  TCase* tc_processing = tcase_create("Processing");
  tcase_add_test(tc_processing, test_correlation_process_log_event);
  tcase_add_test(tc_processing, test_correlation_process_network_event);
  tcase_add_test(tc_processing, test_correlation_process_null_corr);
  tcase_add_test(tc_processing, test_correlation_process_null_event);
  
  TCase* tc_matching = tcase_create("Matching");
  tcase_add_test(tc_matching, test_correlation_time_based_match);
  tcase_add_test(tc_matching, test_correlation_time_based_no_match_outside_window);
  tcase_add_test(tc_matching, test_correlation_bidirectional_matching);
  
  suite_add_tcase(s, tc_core);
  suite_add_tcase(s, tc_lifecycle);
  suite_add_tcase(s, tc_processing);
  suite_add_tcase(s, tc_matching);
  
  return s;
}

int main(void) {
  int number_failed;
  Suite* s = correlation_suite();
  SRunner* sr = srunner_create(s);
  
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

