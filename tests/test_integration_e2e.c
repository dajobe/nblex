/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_integration_e2e.c - End-to-end integration tests
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

Suite* integration_e2e_suite(void);

START_TEST(test_e2e_file_and_network_simultaneously) {
  /* Test file input + network input processing simultaneously */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  /* Create temporary file with JSON logs */
  const char* log_content =
    "{\"level\":\"ERROR\",\"message\":\"Database connection failed\"}\n"
    "{\"level\":\"INFO\",\"message\":\"Retrying connection\"}\n"
    "{\"level\":\"WARN\",\"message\":\"High latency detected\"}\n";

  char* temp_file = create_temp_file(log_content);
  ck_assert_ptr_ne(temp_file, NULL);

  /* Create file input */
  nblex_input* file_input = nblex_input_file_new(world, temp_file);
  ck_assert_ptr_ne(file_input, NULL);
  nblex_input_set_format(file_input, NBLEX_FORMAT_JSON);

  /* Create network events manually (since we can't easily capture real traffic in tests) */
  nblex_input* net_input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  ck_assert_ptr_ne(net_input, NULL);

  uint64_t base_time = nblex_timestamp_now();

  /* Create network event */
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, net_input);
  net_event->timestamp_ns = base_time;
  net_event->data = json_object();
  json_object_set_new(net_event->data, "dst_port", json_integer(3306));
  json_object_set_new(net_event->data, "protocol", json_string("tcp"));
  json_object_set_new(net_event->data, "flags", json_string("RST"));

  /* Emit network event */
  nblex_event_emit(world, net_event);

  /* Process file input which will generate log events */
  /* Read and parse the file manually for testing */
  FILE* fp = fopen(temp_file, "r");
  if (fp) {
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
      nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, file_input);
      log_event->timestamp_ns = base_time + (10 * 1000000ULL); /* 10ms later */
      log_event->data = json_loads(line, 0, NULL);
      if (log_event->data) {
        nblex_event_emit(world, log_event);
      } else {
        nblex_event_free(log_event);
      }
    }
    fclose(fp);
  }

  /* Events are processed synchronously by nblex_event_emit */

  /* Verify events were processed */
  ck_assert_int_ge(world->events_processed, 3); /* At least 3 log events + 1 network event */
  ck_assert_int_ge(test_captured_events_count, 1);

  /* Cleanup */
  unlink(temp_file);
  free(temp_file);
  /* Don't free inputs directly - world_free will handle them */
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_e2e_log_correlation_output_pipeline) {
  /* Test complete pipeline: log parsing → correlation → output */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  /* Create log input */
  nblex_input* log_input = nblex_input_new(world, NBLEX_INPUT_FILE);
  ck_assert_ptr_ne(log_input, NULL);

  /* Create network input */
  nblex_input* net_input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  ck_assert_ptr_ne(net_input, NULL);

  uint64_t base_time = nblex_timestamp_now();

  /* Create log event */
  nblex_event* log_event = nblex_event_new(NBLEX_EVENT_LOG, log_input);
  log_event->timestamp_ns = base_time;
  log_event->data = json_object();
  json_object_set_new(log_event->data, "level", json_string("ERROR"));
  json_object_set_new(log_event->data, "message", json_string("Connection timeout"));
  json_object_set_new(log_event->data, "service", json_string("api"));

  /* Create network event within correlation window */
  nblex_event* net_event = nblex_event_new(NBLEX_EVENT_NETWORK, net_input);
  net_event->timestamp_ns = base_time + (30 * 1000000ULL); /* 30ms later */
  net_event->data = json_object();
  json_object_set_new(net_event->data, "dst_port", json_integer(443));
  json_object_set_new(net_event->data, "retransmits", json_integer(5));
  json_object_set_new(net_event->data, "latency_ms", json_integer(1500));

  /* Emit events */
  nblex_event_emit(world, log_event);
  nblex_event_emit(world, net_event);

  /* Events are processed synchronously by nblex_event_emit */

  /* Verify events were processed and correlated (correlation engine emits an extra event) */
  ck_assert_int_eq(world->events_processed, 3);
  ck_assert_int_ge(test_captured_events_count, 2);

  /* Verify we captured both log and network events */
  bool found_log = false;
  bool found_network = false;

  for (size_t i = 0; i < test_captured_events_count; i++) {
    if (test_captured_events[i]->type == NBLEX_EVENT_LOG) {
      found_log = true;
    } else if (test_captured_events[i]->type == NBLEX_EVENT_NETWORK) {
      found_network = true;
    }
  }

  ck_assert(found_log);
  ck_assert(found_network);

  /* Cleanup */
  /* Don't free inputs directly - world_free will handle them */
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_e2e_multiple_formats) {
  /* Test multiple input sources with different formats */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  /* Create JSON log file */
  const char* json_content = "{\"level\":\"ERROR\",\"msg\":\"Test error\"}\n";
  char* json_file = create_temp_file(json_content);
  ck_assert_ptr_ne(json_file, NULL);

  /* Create logfmt log file */
  const char* logfmt_content = "level=INFO msg=\"Test info\" service=api\n";
  char* logfmt_file = create_temp_file(logfmt_content);
  ck_assert_ptr_ne(logfmt_file, NULL);

  /* Create syslog format file */
  const char* syslog_content = "<34>Oct 11 22:14:15 server app: Test syslog message\n";
  char* syslog_file = create_temp_file(syslog_content);
  ck_assert_ptr_ne(syslog_file, NULL);

  /* Create inputs with different formats */
  nblex_input* json_input = nblex_input_file_new(world, json_file);
  ck_assert_ptr_ne(json_input, NULL);
  nblex_input_set_format(json_input, NBLEX_FORMAT_JSON);

  nblex_input* logfmt_input = nblex_input_file_new(world, logfmt_file);
  ck_assert_ptr_ne(logfmt_input, NULL);
  nblex_input_set_format(logfmt_input, NBLEX_FORMAT_LOGFMT);

  nblex_input* syslog_input = nblex_input_file_new(world, syslog_file);
  ck_assert_ptr_ne(syslog_input, NULL);
  nblex_input_set_format(syslog_input, NBLEX_FORMAT_SYSLOG);

  /* Parse and emit events from each file */
  uint64_t base_time = nblex_timestamp_now();

  /* JSON event */
  FILE* fp = fopen(json_file, "r");
  if (fp) {
    char line[1024];
    if (fgets(line, sizeof(line), fp)) {
      nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, json_input);
      event->timestamp_ns = base_time;
      event->data = json_loads(line, 0, NULL);
      if (event->data) {
        nblex_event_emit(world, event);
      } else {
        nblex_event_free(event);
      }
    }
    fclose(fp);
  }

  /* Logfmt event */
  fp = fopen(logfmt_file, "r");
  if (fp) {
    char line[1024];
    if (fgets(line, sizeof(line), fp)) {
      nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, logfmt_input);
      event->timestamp_ns = base_time + (10 * 1000000ULL);
      /* For simplicity, create a JSON representation */
      event->data = json_object();
      json_object_set_new(event->data, "level", json_string("INFO"));
      json_object_set_new(event->data, "msg", json_string("Test info"));
      json_object_set_new(event->data, "service", json_string("api"));
      nblex_event_emit(world, event);
    }
    fclose(fp);
  }

  /* Syslog event */
  fp = fopen(syslog_file, "r");
  if (fp) {
    char line[1024];
    if (fgets(line, sizeof(line), fp)) {
      nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, syslog_input);
      event->timestamp_ns = base_time + (20 * 1000000ULL);
      event->data = json_object();
      json_object_set_new(event->data, "message", json_string("Test syslog message"));
      nblex_event_emit(world, event);
    }
    fclose(fp);
  }

  /* Events are processed synchronously by nblex_event_emit */

  /* Verify all events were processed */
  ck_assert_int_eq(world->events_processed, 3);
  ck_assert_int_ge(test_captured_events_count, 3);

  /* Cleanup */
  unlink(json_file);
  unlink(logfmt_file);
  unlink(syslog_file);
  free(json_file);
  free(logfmt_file);
  free(syslog_file);
  /* Don't free inputs directly - world_free will handle them */
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

START_TEST(test_e2e_pcap_file_offline) {
  /* Test offline pcap file processing */

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);

  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  /* For testing, we'll create synthetic network events instead of real pcap file
   * In a production test, you would use a real pcap file or text2pcap */

  nblex_input* pcap_input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  ck_assert_ptr_ne(pcap_input, NULL);

  uint64_t base_time = nblex_timestamp_now();

  /* Create multiple network events simulating pcap playback */
  for (int i = 0; i < 5; i++) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_NETWORK, pcap_input);
    event->timestamp_ns = base_time + (i * 100 * 1000000ULL);
    event->data = json_object();
    json_object_set_new(event->data, "packet_num", json_integer(i));
    json_object_set_new(event->data, "dst_port", json_integer(80 + i));
    json_object_set_new(event->data, "protocol", json_string("tcp"));
    nblex_event_emit(world, event);
  }

  /* Events are processed synchronously by nblex_event_emit */

  /* Verify packets were processed */
  ck_assert_int_eq(world->events_processed, 5);
  ck_assert_int_ge(test_captured_events_count, 5);

  /* Cleanup */
  nblex_input_free(pcap_input);
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();
}
END_TEST

Suite* integration_e2e_suite(void) {
  Suite* s = suite_create("Integration E2E");

  TCase* tc_e2e = tcase_create("End-to-End Flows");
  tcase_add_test(tc_e2e, test_e2e_file_and_network_simultaneously);
  tcase_add_test(tc_e2e, test_e2e_log_correlation_output_pipeline);
  tcase_add_test(tc_e2e, test_e2e_multiple_formats);
  tcase_add_test(tc_e2e, test_e2e_pcap_file_offline);
  suite_add_tcase(s, tc_e2e);

  return s;
}

int main(void) {
  int number_failed;
  Suite* s = integration_e2e_suite();
  SRunner* sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
