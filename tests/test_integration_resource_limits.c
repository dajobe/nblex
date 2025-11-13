/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_integration_resource_limits.c - Resource limits integration tests
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

Suite* integration_resource_limits_suite(void);

/* External functions for resource management */
extern int nblex_world_set_memory_limit(nblex_world* world, size_t limit_bytes);
extern size_t nblex_world_get_memory_usage(nblex_world* world);
extern int nblex_world_set_buffer_limit(nblex_world* world, size_t limit_bytes);
extern size_t nblex_world_get_buffer_usage(nblex_world* world);

START_TEST(test_memory_quota_enforcement) {
  /* Test memory quota enforcement */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);

  /* Set memory limit to 10MB */
  int result = nblex_world_set_memory_limit(world, 10 * 1024 * 1024);
  ck_assert_int_eq(result, 0);

  /* Create input */
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);

  /* Create many events to test memory limit */
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  int events_created = 0;
  for (int i = 0; i < 1000; i++) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    if (!event) {
      /* Memory limit reached */
      break;
    }

    event->timestamp_ns = nblex_timestamp_now();
    event->data = json_object();

    /* Create a moderately sized JSON object */
    for (int j = 0; j < 100; j++) {
      char key[32];
      snprintf(key, sizeof(key), "field_%d", j);
      json_object_set_new(event->data, key, json_string("value"));
    }

    nblex_event_emit(world, event);
    events_created++;

    /* Check memory usage */
    size_t memory_usage = nblex_world_get_memory_usage(world);
    if (memory_usage > 9 * 1024 * 1024) {
      /* Approaching limit */
      break;
    }
  }

  /* Verify we didn't exceed the limit significantly */
  size_t final_memory = nblex_world_get_memory_usage(world);
  ck_assert_int_le(final_memory, 11 * 1024 * 1024); /* Allow 10% margin */

  /* Cleanup */
  nblex_input_free(input);
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_buffer_size_limits) {
  /* Test buffer size limits */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);

  /* Set buffer limit to 1MB */
  int result = nblex_world_set_buffer_limit(world, 1 * 1024 * 1024);
  ck_assert_int_eq(result, 0);

  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);

  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  /* Create events until buffer is full */
  int events_buffered = 0;
  for (int i = 0; i < 1000; i++) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    if (!event) {
      break;
    }

    event->timestamp_ns = nblex_timestamp_now();
    event->data = json_object();
    json_object_set_new(event->data, "index", json_integer(i));
    json_object_set_new(event->data, "message", json_string("Test message"));

    /* Emit without processing to fill buffer */
    nblex_event_emit(world, event);
    events_buffered++;

    size_t buffer_usage = nblex_world_get_buffer_usage(world);
    if (buffer_usage >= 950 * 1024) { /* 95% of limit */
      break;
    }
  }

  /* Verify buffer usage is within limits */
  size_t final_buffer = nblex_world_get_buffer_usage(world);
  ck_assert_int_le(final_buffer, 1 * 1024 * 1024);

  /* Process events to drain buffer */
  for (int i = 0; i < 50; i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }

  /* Buffer should be reduced after processing */
  size_t after_process = nblex_world_get_buffer_usage(world);
  ck_assert_int_lt(after_process, final_buffer);

  /* Cleanup */
  nblex_input_free(input);
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_event_rate_limiting) {
  /* Test event rate limiting (if implemented) */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);

  /* Emit burst of events */
  int burst_size = 100;
  uint64_t start_time = nblex_timestamp_now();

  for (int i = 0; i < burst_size; i++) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    event->timestamp_ns = start_time + (i * 1000000ULL); /* 1ms apart */
    event->data = json_object();
    json_object_set_new(event->data, "burst_index", json_integer(i));
    nblex_event_emit(world, event);
  }

  /* Process events */
  for (int i = 0; i < 50; i++) {
    uv_run(world->loop, UV_RUN_ONCE);
  }

  /* Verify events were processed */
  ck_assert_int_ge(world->events_processed, burst_size / 2);
  ck_assert_int_le(world->events_processed, burst_size);

  /* Cleanup */
  nblex_input_free(input);
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_resource_cleanup_after_limit) {
  /* Test that resources are properly cleaned up when limits are hit */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);

  /* Set very small memory limit */
  nblex_world_set_memory_limit(world, 1 * 1024 * 1024); /* 1MB */

  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(input, NULL);

  /* Get initial memory usage */
  size_t initial_memory = nblex_world_get_memory_usage(world);

  /* Create and immediately free some events */
  for (int i = 0; i < 100; i++) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    if (event) {
      event->timestamp_ns = nblex_timestamp_now();
      event->data = json_object();
      json_object_set_new(event->data, "temp", json_string("value"));
      nblex_event_free(event);
    }
  }

  /* Memory should be back to approximately initial level */
  size_t final_memory = nblex_world_get_memory_usage(world);
  ck_assert_int_le(final_memory, initial_memory + (100 * 1024)); /* Allow 100KB margin */

  /* Cleanup */
  nblex_input_free(input);
  nblex_world_stop(world);
  nblex_world_free(world);
}
END_TEST

Suite* integration_resource_limits_suite(void) {
  Suite* s = suite_create("Integration Resource Limits");

  TCase* tc_limits = tcase_create("Resource Limits");
  tcase_add_test(tc_limits, test_memory_quota_enforcement);
  tcase_add_test(tc_limits, test_buffer_size_limits);
  tcase_add_test(tc_limits, test_event_rate_limiting);
  tcase_add_test(tc_limits, test_resource_cleanup_after_limit);
  suite_add_tcase(s, tc_limits);

  return s;
}

int main(void) {
  int number_failed;
  Suite* s = integration_resource_limits_suite();
  SRunner* sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
