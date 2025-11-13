/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_integration_output_formatters.c - Output formatters integration tests
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
#include <sys/stat.h>
#include "../src/nblex_internal.h"
#include "test_helpers.h"
#include "test_integration_helpers.h"

Suite* integration_output_formatters_suite(void);

/* Use actual output types from nblex_internal.h */
typedef struct file_output_s file_output_t;
typedef struct http_output_s http_output_t;
typedef struct metrics_output_s metrics_output_t;

/* Declare external functions from output modules */
extern file_output_t* nblex_file_output_new(const char* path, const char* format);
extern void nblex_file_output_free(file_output_t* output);
extern int nblex_file_output_write(file_output_t* output, nblex_event* event);
extern void nblex_file_output_set_rotation(file_output_t* output, int max_size_mb, int max_age_days, int max_count);

extern http_output_t* nblex_http_output_new(const char* url);
extern void nblex_http_output_free(http_output_t* output);
extern int nblex_http_output_write(http_output_t* output, nblex_event* event);
extern void nblex_http_output_set_method(http_output_t* output, const char* method);
extern void nblex_http_output_set_timeout(http_output_t* output, int timeout_seconds);

extern metrics_output_t* nblex_metrics_output_new(const char* path, const char* format);
extern void nblex_metrics_output_free(metrics_output_t* output);
extern int nblex_metrics_output_write(metrics_output_t* output, nblex_event* event);

START_TEST(test_file_output_write_and_rotation) {
  /* Test file output with rotation management */

  char temp_template[] = "/tmp/nblex_test_output_XXXXXX";
  char* temp_dir = mkdtemp(temp_template);
  ck_assert_ptr_ne(temp_dir, NULL);

  char output_path[1024];
  snprintf(output_path, sizeof(output_path), "%s/test.log", temp_dir);

  /* Create file output */
  file_output_t* file_out = nblex_file_output_new(output_path, "json");
  ck_assert_ptr_ne(file_out, NULL);

  /* Configure rotation: 1MB max size */
  nblex_file_output_set_rotation(file_out, 1, 0, 3);

  /* Create test event */
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);

  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  event->timestamp_ns = nblex_timestamp_now();
  event->data = json_object();
  json_object_set_new(event->data, "level", json_string("ERROR"));
  json_object_set_new(event->data, "message", json_string("Test error message"));

  /* Write event */
  int result = nblex_file_output_write(file_out, event);
  ck_assert_int_eq(result, 0);

  /* Verify file was created and contains data */
  struct stat st;
  result = stat(output_path, &st);
  ck_assert_int_eq(result, 0);
  ck_assert_int_gt(st.st_size, 0);

  /* Read file content to verify */
  FILE* fp = fopen(output_path, "r");
  ck_assert_ptr_ne(fp, NULL);

  char buffer[4096];
  size_t read_size = fread(buffer, 1, sizeof(buffer) - 1, fp);
  ck_assert_int_gt(read_size, 0);
  buffer[read_size] = '\0';
  fclose(fp);

  /* Verify it contains ERROR and our message */
  ck_assert_ptr_ne(strstr(buffer, "ERROR"), NULL);
  ck_assert_ptr_ne(strstr(buffer, "Test error message"), NULL);

  /* Cleanup */
  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
  nblex_file_output_free(file_out);

  unlink(output_path);
  rmdir(temp_dir);
}
END_TEST

START_TEST(test_file_output_multiple_events) {
  /* Test writing multiple events to file */

  char temp_template[] = "/tmp/nblex_test_output_XXXXXX";
  char* temp_dir = mkdtemp(temp_template);
  ck_assert_ptr_ne(temp_dir, NULL);

  char output_path[1024];
  snprintf(output_path, sizeof(output_path), "%s/test.log", temp_dir);

  file_output_t* file_out = nblex_file_output_new(output_path, "json");
  ck_assert_ptr_ne(file_out, NULL);

  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);

  /* Write 10 events */
  for (int i = 0; i < 10; i++) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    event->timestamp_ns = nblex_timestamp_now();
    event->data = json_object();
    json_object_set_new(event->data, "event_num", json_integer(i));
    json_object_set_new(event->data, "level", json_string("INFO"));

    int result = nblex_file_output_write(file_out, event);
    ck_assert_int_eq(result, 0);

    nblex_event_free(event);
  }

  /* Verify all events were written */
  FILE* fp = fopen(output_path, "r");
  ck_assert_ptr_ne(fp, NULL);

  int line_count = 0;
  char line[1024];
  while (fgets(line, sizeof(line), fp)) {
    line_count++;
  }
  fclose(fp);

  ck_assert_int_eq(line_count, 10);

  /* Cleanup */
  nblex_input_free(input);
  nblex_world_free(world);
  nblex_file_output_free(file_out);
  unlink(output_path);
  rmdir(temp_dir);
}
END_TEST

START_TEST(test_http_output_configuration) {
  /* Test HTTP output configuration */

  http_output_t* http_out = nblex_http_output_new("http://localhost:8080/webhook");
  ck_assert_ptr_ne(http_out, NULL);

  /* Set method */
  nblex_http_output_set_method(http_out, "PUT");

  /* Set timeout */
  nblex_http_output_set_timeout(http_out, 60);

  /* Cleanup */
  nblex_http_output_free(http_out);
}
END_TEST

START_TEST(test_http_output_write) {
  /* Test HTTP output write (will fail to connect, but tests the code path) */

  http_output_t* http_out = nblex_http_output_new("http://localhost:9999/test");
  ck_assert_ptr_ne(http_out, NULL);

  nblex_http_output_set_timeout(http_out, 1); /* Short timeout */

  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);

  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  event->timestamp_ns = nblex_timestamp_now();
  event->data = json_object();
  json_object_set_new(event->data, "test", json_string("value"));

  /* This will fail to connect, but should handle gracefully */
  int result = nblex_http_output_write(http_out, event);
  /* We expect failure since there's no server listening */
  ck_assert_int_ne(result, 0);

  /* Cleanup */
  nblex_event_free(event);
  nblex_input_free(input);
  nblex_world_free(world);
  nblex_http_output_free(http_out);
}
END_TEST

START_TEST(test_metrics_output_basic) {
  /* Test basic metrics output */

  metrics_output_t* metrics_out = nblex_metrics_output_new("/tmp/nblex_metrics.txt", "prometheus");
  ck_assert_ptr_ne(metrics_out, NULL);

  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);

  /* Record some events */
  for (int i = 0; i < 5; i++) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    event->timestamp_ns = nblex_timestamp_now();
    event->data = json_object();
    json_object_set_new(event->data, "level", json_string("ERROR"));

    nblex_metrics_output_write(metrics_out, event);
    nblex_event_free(event);
  }

  /* Cleanup */
  nblex_input_free(input);
  nblex_world_free(world);
  nblex_metrics_output_free(metrics_out);
  unlink("/tmp/nblex_metrics.txt");
}
END_TEST

Suite* integration_output_formatters_suite(void) {
  Suite* s = suite_create("Integration Output Formatters");

  TCase* tc_file = tcase_create("File Output");
  tcase_add_test(tc_file, test_file_output_write_and_rotation);
  tcase_add_test(tc_file, test_file_output_multiple_events);
  suite_add_tcase(s, tc_file);

  TCase* tc_http = tcase_create("HTTP Output");
  tcase_add_test(tc_http, test_http_output_configuration);
  tcase_add_test(tc_http, test_http_output_write);
  suite_add_tcase(s, tc_http);

  TCase* tc_metrics = tcase_create("Metrics Output");
  tcase_add_test(tc_metrics, test_metrics_output_basic);
  suite_add_tcase(s, tc_metrics);

  return s;
}

int main(void) {
  int number_failed;
  Suite* s = integration_output_formatters_suite();
  SRunner* sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
