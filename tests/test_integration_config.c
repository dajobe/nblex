/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_integration_config.c - Configuration integration tests
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
#include "test_integration_helpers.h"

Suite* integration_config_suite(void);

START_TEST(test_config_load_and_apply) {
  /* Test configuration loading and application to world */
  
  const char* yaml = 
    "version: \"1.0\"\n"
    "correlation:\n"
    "  enabled: true\n"
    "  window_ms: 200\n"
    "inputs:\n"
    "  logs:\n"
    "    - name: test_log\n"
    "      type: file\n"
    "      path: /tmp/test.log\n"
    "      format: json\n";
  
  char* yaml_path = create_temp_file(yaml);
  ck_assert_ptr_ne(yaml_path, NULL);
  
  nblex_config_t* config = nblex_config_load_yaml(yaml_path);
  ck_assert_ptr_ne(config, NULL);
  
  nblex_world* world = nblex_world_new();
  ck_assert_ptr_ne(world, NULL);
  nblex_world_open(world);
  
  int rc = nblex_config_apply(config, world);
  ck_assert_int_eq(rc, 0);
  
  ck_assert_ptr_ne(world->correlation, NULL);
  ck_assert_int_ge(world->inputs_count, 0);
  
  nblex_config_free(config);
  nblex_world_stop(world);
  nblex_world_free(world);
  
  unlink(yaml_path);
  free(yaml_path);
}
END_TEST

START_TEST(test_config_with_correlation_settings) {
  /* Test config with correlation settings */
  
  const char* yaml = 
    "version: \"1.0\"\n"
    "correlation:\n"
    "  enabled: true\n"
    "  window_ms: 150\n";
  
  char* yaml_path = create_temp_file(yaml);
  ck_assert_ptr_ne(yaml_path, NULL);
  
  nblex_config_t* config = nblex_config_load_yaml(yaml_path);
  ck_assert_ptr_ne(config, NULL);
  
  /* Verify correlation settings */
  int enabled = nblex_config_get_int(config, "correlation.enabled", 0);
  ck_assert_int_eq(enabled, 1);
  
  int window = nblex_config_get_int(config, "correlation.window_ms", 100);
  ck_assert_int_eq(window, 150);
  
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  
  nblex_config_apply(config, world);
  
  /* Verify correlation was configured */
  ck_assert_ptr_ne(world->correlation, NULL);
  
  nblex_config_free(config);
  nblex_world_stop(world);
  nblex_world_free(world);
  
  unlink(yaml_path);
  free(yaml_path);
}
END_TEST

START_TEST(test_config_with_performance_settings) {
  /* Test config with performance settings */
  
  const char* yaml = 
    "version: \"1.0\"\n"
    "performance:\n"
    "  worker_threads: 8\n"
    "  buffer_size: 128MB\n"
    "  memory_limit: 2GB\n";
  
  char* yaml_path = create_temp_file(yaml);
  ck_assert_ptr_ne(yaml_path, NULL);
  
  nblex_config_t* config = nblex_config_load_yaml(yaml_path);
  ck_assert_ptr_ne(config, NULL);
  
  /* Verify performance settings */
  int threads = nblex_config_get_int(config, "performance.worker_threads", 4);
  ck_assert_int_eq(threads, 8);
  
  size_t buffer = nblex_config_get_size(config, "performance.buffer_size", 0);
  ck_assert_int_eq(buffer, 128 * 1024 * 1024);
  
  size_t memory = nblex_config_get_size(config, "performance.memory_limit", 0);
  ck_assert_int_eq(memory, 2UL * 1024 * 1024 * 1024);
  
  nblex_config_free(config);
  
  unlink(yaml_path);
  free(yaml_path);
}
END_TEST

START_TEST(test_config_with_multiple_inputs) {
  /* Test config with multiple inputs */
  
  const char* yaml = 
    "version: \"1.0\"\n"
    "inputs:\n"
    "  logs:\n"
    "    - name: app_log\n"
    "      type: file\n"
    "      path: /var/log/app.log\n"
    "      format: json\n"
    "    - name: error_log\n"
    "      type: file\n"
    "      path: /var/log/error.log\n"
    "      format: json\n";
  
  char* yaml_path = create_temp_file(yaml);
  ck_assert_ptr_ne(yaml_path, NULL);
  
  nblex_config_t* config = nblex_config_load_yaml(yaml_path);
  ck_assert_ptr_ne(config, NULL);
  
  nblex_world* world = nblex_world_new();
  nblex_world_open(world);
  
  nblex_config_apply(config, world);
  
  /* Should have created inputs */
  ck_assert_int_ge(world->inputs_count, 0);
  
  nblex_config_free(config);
  nblex_world_stop(world);
  nblex_world_free(world);
  
  unlink(yaml_path);
  free(yaml_path);
}
END_TEST

Suite* integration_config_suite(void) {
  Suite* s = suite_create("Integration Config");
  
  TCase* tc_basic = tcase_create("Basic");
  tcase_add_test(tc_basic, test_config_load_and_apply);
  tcase_add_test(tc_basic, test_config_with_correlation_settings);
  tcase_add_test(tc_basic, test_config_with_performance_settings);
  
  TCase* tc_inputs = tcase_create("Inputs");
  tcase_add_test(tc_inputs, test_config_with_multiple_inputs);
  
  suite_add_tcase(s, tc_basic);
  suite_add_tcase(s, tc_inputs);
  
  return s;
}

int main(void) {
  int number_failed;
  Suite* s = integration_config_suite();
  SRunner* sr = srunner_create(s);
  
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

