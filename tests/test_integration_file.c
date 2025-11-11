/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_integration_file.c - File input integration tests
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
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "../src/nblex_internal.h"
#include "test_helpers.h"
#include "test_integration_helpers.h"

Suite* integration_file_suite(void);

START_TEST(test_file_input_json_parsing_pipeline) {
  /* Test file input → JSON parsing → event emission pipeline */
  
  const char* log_content =
    "{\"level\":\"ERROR\",\"message\":\"Database connection failed\",\"timestamp\":\"2025-11-10T10:00:00Z\"}\n"
    "{\"level\":\"INFO\",\"message\":\"Request processed\",\"timestamp\":\"2025-11-10T10:00:01Z\"}\n"
    "{\"level\":\"WARN\",\"message\":\"Slow query detected\",\"timestamp\":\"2025-11-10T10:00:02Z\"}\n";

  char* log_path = create_temp_file(log_content);
  ck_assert_ptr_ne(log_path, NULL);

  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  nblex_input* input = nblex_input_file_new(world, log_path);
  ck_assert_ptr_ne(input, NULL);
  input->format = NBLEX_FORMAT_JSON;

  /* Manually process file content (simulating file tailing) */
  FILE* file = fopen(log_path, "r");
  ck_assert_ptr_ne(file, NULL);

  int events_processed = 0;
  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), file) != NULL) {
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
    }
    if (len == 0) continue;

    /* Parse JSON line */
    json_t* parsed = nblex_parse_json_line(buffer);
    if (parsed) {
      nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
      event->data = parsed;
      nblex_event_emit(world, event);
      events_processed++;
    }
  }
  fclose(file);

  ck_assert_int_eq(events_processed, 3);
  ck_assert_int_ge(world->events_processed, 3);

  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();

  unlink(log_path);
  free(log_path);
}
END_TEST

START_TEST(test_file_input_logfmt_parsing_pipeline) {
  /* Test file input → logfmt parsing → event emission */
  
  const char* log_content =
    "level=ERROR message=\"Connection timeout\" duration=5000\n"
    "level=INFO message=\"Request completed\" status=200\n"
    "level=WARN message=\"High memory usage\" memory=85\n";

  char* log_path = create_temp_file(log_content);
  ck_assert_ptr_ne(log_path, NULL);

  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  nblex_input* input = nblex_input_file_new(world, log_path);
  input->format = NBLEX_FORMAT_LOGFMT;

  FILE* file = fopen(log_path, "r");
  ck_assert_ptr_ne(file, NULL);

  int events_processed = 0;
  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), file) != NULL) {
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
    }
    if (len == 0) continue;

    json_t* parsed = nblex_parse_logfmt_line(buffer);
    if (parsed) {
      nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
      event->data = parsed;
      nblex_event_emit(world, event);
      events_processed++;
    }
  }
  fclose(file);

  ck_assert_int_eq(events_processed, 3);
  ck_assert_int_ge(world->events_processed, 3);

  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();

  unlink(log_path);
  free(log_path);
}
END_TEST

START_TEST(test_file_input_syslog_parsing_pipeline) {
  /* Test file input → syslog parsing → event emission */
  
  const char* log_content =
    "<34>1 2025-11-10T10:00:00Z hostname app - - [msg] Database error\n"
    "<30>1 2025-11-10T10:00:01Z hostname app - - [msg] Request processed\n"
    "<28>1 2025-11-10T10:00:02Z hostname app - - [msg] Warning message\n";

  char* log_path = create_temp_file(log_content);
  ck_assert_ptr_ne(log_path, NULL);

  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  nblex_input* input = nblex_input_file_new(world, log_path);
  input->format = NBLEX_FORMAT_SYSLOG;

  FILE* file = fopen(log_path, "r");
  ck_assert_ptr_ne(file, NULL);

  int events_processed = 0;
  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), file) != NULL) {
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
    }
    if (len == 0) continue;

    json_t* parsed = nblex_parse_syslog_line(buffer);
    if (parsed) {
      nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
      event->data = parsed;
      nblex_event_emit(world, event);
      events_processed++;
    }
  }
  fclose(file);

  ck_assert_int_eq(events_processed, 3);
  ck_assert_int_ge(world->events_processed, 3);

  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();

  unlink(log_path);
  free(log_path);
}
END_TEST

START_TEST(test_file_input_nginx_parsing_pipeline) {
  /* Test file input → nginx parsing → event emission */
  
  const char* log_content =
    "127.0.0.1 - - [09/Nov/2025:17:28:06 -0800] \"GET /api/users HTTP/1.1\" 200 1234 \"-\" \"curl/8.7.1\"\n"
    "192.168.1.1 - user [09/Nov/2025:17:30:00 -0800] \"POST /api/data HTTP/1.1\" 500 0 \"https://example.com\" \"Mozilla/5.0\"\n"
    "10.0.0.1 - - [09/Nov/2025:12:00:00 -0800] \"GET /test HTTP/1.0\" 404 500 \"-\" \"-\"\n";

  char* log_path = create_temp_file(log_content);
  ck_assert_ptr_ne(log_path, NULL);

  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  nblex_input* input = nblex_input_file_new(world, log_path);
  input->format = NBLEX_FORMAT_NGINX;

  FILE* file = fopen(log_path, "r");
  ck_assert_ptr_ne(file, NULL);

  int events_processed = 0;
  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), file) != NULL) {
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
    }
    if (len == 0) continue;

    json_t* parsed = nblex_parse_nginx_line(buffer);
    if (parsed) {
      nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
      event->data = parsed;
      nblex_event_emit(world, event);
      events_processed++;
    }
  }
  fclose(file);

  ck_assert_int_eq(events_processed, 3);
  ck_assert_int_ge(world->events_processed, 3);

  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();

  unlink(log_path);
  free(log_path);
}
END_TEST

START_TEST(test_file_input_with_filters) {
  /* Test file input → parsing → filter → event emission */
  
  const char* log_content =
    "{\"level\":\"ERROR\",\"message\":\"Database connection failed\"}\n"
    "{\"level\":\"INFO\",\"message\":\"Request processed\"}\n"
    "{\"level\":\"ERROR\",\"message\":\"Timeout occurred\"}\n"
    "{\"level\":\"DEBUG\",\"message\":\"Debug message\"}\n";

  char* log_path = create_temp_file(log_content);
  ck_assert_ptr_ne(log_path, NULL);

  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  nblex_input* input = nblex_input_file_new(world, log_path);
  input->format = NBLEX_FORMAT_JSON;

  /* Create filter: level == ERROR */
  filter_t* filter = nblex_filter_new("level == \"ERROR\"");
  ck_assert_ptr_ne(filter, NULL);

  FILE* file = fopen(log_path, "r");
  ck_assert_ptr_ne(file, NULL);

  int events_processed = 0;
  int events_filtered = 0;
  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), file) != NULL) {
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
    }
    if (len == 0) continue;

    json_t* parsed = nblex_parse_json_line(buffer);
    if (parsed) {
      nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
      event->data = parsed;
      
      /* Apply filter */
      if (nblex_filter_matches(filter, event)) {
        nblex_event_emit(world, event);
        events_filtered++;
      } else {
        nblex_event_free(event);
      }
      events_processed++;
    }
  }
  fclose(file);

  /* Should have processed 4 events, but only 2 should pass filter */
  ck_assert_int_eq(events_processed, 4);
  ck_assert_int_eq(events_filtered, 2);
  ck_assert_int_eq(world->events_processed, 2);

  nblex_filter_free(filter);
  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();

  unlink(log_path);
  free(log_path);
}
END_TEST

START_TEST(test_file_rotation_simulation) {
  /* Test file rotation by simulating new file creation */
  
  const char* log_content1 =
    "{\"level\":\"INFO\",\"message\":\"First file line 1\"}\n"
    "{\"level\":\"INFO\",\"message\":\"First file line 2\"}\n";

  char* log_path = create_temp_file(log_content1);
  ck_assert_ptr_ne(log_path, NULL);

  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_set_event_handler(world, test_capture_event_handler, NULL);
  test_reset_captured_events();

  nblex_input* input = nblex_input_file_new(world, log_path);
  input->format = NBLEX_FORMAT_JSON;

  /* Process first file */
  FILE* file1 = fopen(log_path, "r");
  ck_assert_ptr_ne(file1, NULL);

  int events_from_file1 = 0;
  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), file1) != NULL) {
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
    }
    if (len == 0) continue;

    json_t* parsed = nblex_parse_json_line(buffer);
    if (parsed) {
      nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
      event->data = parsed;
      nblex_event_emit(world, event);
      events_from_file1++;
    }
  }
  fclose(file1);

  ck_assert_int_eq(events_from_file1, 2);

  /* Simulate rotation: remove old file, create new one */
  unlink(log_path);

  const char* log_content2 =
    "{\"level\":\"INFO\",\"message\":\"Rotated file line 1\"}\n"
    "{\"level\":\"INFO\",\"message\":\"Rotated file line 2\"}\n"
    "{\"level\":\"INFO\",\"message\":\"Rotated file line 3\"}\n";

  /* Create new file with same path (simulating rotation) */
  FILE* file2 = fopen(log_path, "w");
  ck_assert_ptr_ne(file2, NULL);
  fputs(log_content2, file2);
  fclose(file2);

  /* Process rotated file */
  FILE* file3 = fopen(log_path, "r");
  ck_assert_ptr_ne(file3, NULL);

  int events_from_file2 = 0;
  while (fgets(buffer, sizeof(buffer), file3) != NULL) {
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
    }
    if (len == 0) continue;

    json_t* parsed = nblex_parse_json_line(buffer);
    if (parsed) {
      nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
      event->data = parsed;
      nblex_event_emit(world, event);
      events_from_file2++;
    }
  }
  fclose(file3);

  ck_assert_int_eq(events_from_file2, 3);
  ck_assert_int_eq(world->events_processed, 5); /* 2 from first file + 3 from rotated file */

  nblex_world_stop(world);
  nblex_world_free(world);
  test_reset_captured_events();

  unlink(log_path);
  free(log_path);
}
END_TEST

Suite* integration_file_suite(void) {
  Suite* s = suite_create("Integration File");
  
  TCase* tc_parsing = tcase_create("Parsing Pipeline");
  tcase_add_test(tc_parsing, test_file_input_json_parsing_pipeline);
  tcase_add_test(tc_parsing, test_file_input_logfmt_parsing_pipeline);
  tcase_add_test(tc_parsing, test_file_input_syslog_parsing_pipeline);
  tcase_add_test(tc_parsing, test_file_input_nginx_parsing_pipeline);
  
  TCase* tc_filtering = tcase_create("Filtering");
  tcase_add_test(tc_filtering, test_file_input_with_filters);
  
  TCase* tc_rotation = tcase_create("Rotation");
  tcase_add_test(tc_rotation, test_file_rotation_simulation);
  
  suite_add_tcase(s, tc_parsing);
  suite_add_tcase(s, tc_filtering);
  suite_add_tcase(s, tc_rotation);
  
  return s;
}

int main(void) {
  int number_failed;
  Suite* s = integration_file_suite();
  SRunner* sr = srunner_create(s);
  
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

