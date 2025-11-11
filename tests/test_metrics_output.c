/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_metrics_output.c - Unit tests for metrics output formatter
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
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/nblex_internal.h"

/* Forward declarations for internal functions */
typedef struct metrics_output_s metrics_output_t;
metrics_output_t* nblex_metrics_output_new(const char* path, const char* format);
void nblex_metrics_output_free(metrics_output_t* output);
int nblex_metrics_output_write(metrics_output_t* output, nblex_event* event);
int nblex_metrics_output_flush(metrics_output_t* output);

/* Test suite declaration */
Suite* metrics_output_suite(void);

/* Helper function to read file contents */
static char* read_file_contents(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) {
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    size_t read = fread(content, 1, size, f);
    content[read] = '\0';
    fclose(f);
    
    return content;
}

START_TEST(test_metrics_output_basic) {
    char tmpfile[] = "/tmp/nblex_test_metrics_XXXXXX";
    int fd = mkstemp(tmpfile);
    ck_assert_int_ne(fd, -1);
    close(fd);
    unlink(tmpfile);
    
    metrics_output_t* output = nblex_metrics_output_new(tmpfile, "prometheus");
    ck_assert_ptr_ne(output, NULL);
    
    nblex_world* world = nblex_world_new();
    ck_assert_ptr_ne(world, NULL);
    ck_assert_int_eq(nblex_world_open(world), 0);
    
    nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
    ck_assert_ptr_ne(input, NULL);
    
    /* Create a regular event */
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    json_t* data = json_object();
    json_object_set_new(data, "log.level", json_string("ERROR"));
    event->data = data;
    
    /* Write event */
    ck_assert_int_eq(nblex_metrics_output_write(output, event), 0);
    
    /* Flush */
    ck_assert_int_eq(nblex_metrics_output_flush(output), 0);
    
    /* Check file contents */
    char* content = read_file_contents(tmpfile);
    ck_assert_ptr_ne(content, NULL);
    ck_assert_str_ne(content, "");
    ck_assert_ptr_ne(strstr(content, "nblex_events_total"), NULL);
    
    free(content);
    nblex_event_free(event);
    nblex_input_free(input);
    nblex_world_free(world);
    nblex_metrics_output_free(output);
    unlink(tmpfile);
}
END_TEST

START_TEST(test_metrics_output_aggregation_no_labels) {
    char tmpfile[] = "/tmp/nblex_test_metrics_XXXXXX";
    int fd = mkstemp(tmpfile);
    ck_assert_int_ne(fd, -1);
    close(fd);
    unlink(tmpfile);
    
    metrics_output_t* output = nblex_metrics_output_new(tmpfile, "prometheus");
    ck_assert_ptr_ne(output, NULL);
    
    nblex_world* world = nblex_world_new();
    ck_assert_ptr_ne(world, NULL);
    ck_assert_int_eq(nblex_world_open(world), 0);
    
    nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
    ck_assert_ptr_ne(input, NULL);
    
    /* Create aggregation event without labels */
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    json_t* data = json_object();
    json_object_set_new(data, "nql_result_type", json_string("aggregation"));
    
    json_t* metrics = json_object();
    json_object_set_new(metrics, "count", json_integer(42));
    json_object_set_new(metrics, "avg_latency_ms", json_real(123.45));
    json_object_set_new(data, "metrics", metrics);
    
    event->data = data;
    event->input = NULL; /* Derived event */
    
    /* Write event */
    ck_assert_int_eq(nblex_metrics_output_write(output, event), 0);
    
    /* Flush */
    ck_assert_int_eq(nblex_metrics_output_flush(output), 0);
    
    /* Check file contents */
    char* content = read_file_contents(tmpfile);
    ck_assert_ptr_ne(content, NULL);
    ck_assert_ptr_ne(strstr(content, "nblex_aggregation"), NULL);
    ck_assert_ptr_ne(strstr(content, "metric=\"count\""), NULL);
    ck_assert_ptr_ne(strstr(content, "metric=\"avg_latency_ms\""), NULL);
    ck_assert_ptr_ne(strstr(content, "42.000000"), NULL);
    ck_assert_ptr_ne(strstr(content, "123.450000"), NULL);
    
    free(content);
    nblex_event_free(event);
    nblex_input_free(input);
    nblex_world_free(world);
    nblex_metrics_output_free(output);
    unlink(tmpfile);
}
END_TEST

START_TEST(test_metrics_output_aggregation_with_labels) {
    char tmpfile[] = "/tmp/nblex_test_metrics_XXXXXX";
    int fd = mkstemp(tmpfile);
    ck_assert_int_ne(fd, -1);
    close(fd);
    unlink(tmpfile);
    
    metrics_output_t* output = nblex_metrics_output_new(tmpfile, "prometheus");
    ck_assert_ptr_ne(output, NULL);
    
    nblex_world* world = nblex_world_new();
    ck_assert_ptr_ne(world, NULL);
    ck_assert_int_eq(nblex_world_open(world), 0);
    
    nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
    ck_assert_ptr_ne(input, NULL);
    
    /* Create aggregation event with labels */
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    json_t* data = json_object();
    json_object_set_new(data, "nql_result_type", json_string("aggregation"));
    
    json_t* group = json_object();
    json_object_set_new(group, "log.service", json_string("api"));
    json_object_set_new(group, "log.status", json_string("500"));
    json_object_set_new(data, "group", group);
    
    json_t* metrics = json_object();
    json_object_set_new(metrics, "count", json_integer(5));
    json_object_set_new(metrics, "avg_latency_ms", json_real(250.5));
    json_object_set_new(data, "metrics", metrics);
    
    event->data = data;
    event->input = NULL; /* Derived event */
    
    /* Write event */
    ck_assert_int_eq(nblex_metrics_output_write(output, event), 0);
    
    /* Flush */
    ck_assert_int_eq(nblex_metrics_output_flush(output), 0);
    
    /* Check file contents */
    char* content = read_file_contents(tmpfile);
    ck_assert_ptr_ne(content, NULL);
    ck_assert_ptr_ne(strstr(content, "nblex_aggregation"), NULL);
    ck_assert_ptr_ne(strstr(content, "log.service=\"api\""), NULL);
    ck_assert_ptr_ne(strstr(content, "log.status=\"500\""), NULL);
    ck_assert_ptr_ne(strstr(content, "metric=\"count\""), NULL);
    ck_assert_ptr_ne(strstr(content, "5.000000"), NULL);
    
    free(content);
    nblex_event_free(event);
    nblex_input_free(input);
    nblex_world_free(world);
    nblex_metrics_output_free(output);
    unlink(tmpfile);
}
END_TEST

START_TEST(test_metrics_output_correlation_event) {
    char tmpfile[] = "/tmp/nblex_test_metrics_XXXXXX";
    int fd = mkstemp(tmpfile);
    ck_assert_int_ne(fd, -1);
    close(fd);
    unlink(tmpfile);
    
    metrics_output_t* output = nblex_metrics_output_new(tmpfile, "prometheus");
    ck_assert_ptr_ne(output, NULL);
    
    nblex_world* world = nblex_world_new();
    ck_assert_ptr_ne(world, NULL);
    ck_assert_int_eq(nblex_world_open(world), 0);
    
    nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
    ck_assert_ptr_ne(input, NULL);
    
    /* Create correlation event */
    nblex_event* event = nblex_event_new(NBLEX_EVENT_CORRELATION, input);
    json_t* data = json_object();
    json_object_set_new(data, "nql_result_type", json_string("correlation"));
    event->data = data;
    
    /* Write event */
    ck_assert_int_eq(nblex_metrics_output_write(output, event), 0);
    
    /* Flush */
    ck_assert_int_eq(nblex_metrics_output_flush(output), 0);
    
    /* Check file contents */
    char* content = read_file_contents(tmpfile);
    ck_assert_ptr_ne(content, NULL);
    ck_assert_ptr_ne(strstr(content, "nblex_correlations_found"), NULL);
    ck_assert_ptr_ne(strstr(content, "1"), NULL);
    
    free(content);
    nblex_event_free(event);
    nblex_input_free(input);
    nblex_world_free(world);
    nblex_metrics_output_free(output);
    unlink(tmpfile);
}
END_TEST

START_TEST(test_metrics_output_metrics_cleared_after_flush) {
    char tmpfile[] = "/tmp/nblex_test_metrics_XXXXXX";
    int fd = mkstemp(tmpfile);
    ck_assert_int_ne(fd, -1);
    close(fd);
    unlink(tmpfile);
    
    metrics_output_t* output = nblex_metrics_output_new(tmpfile, "prometheus");
    ck_assert_ptr_ne(output, NULL);
    
    nblex_world* world = nblex_world_new();
    ck_assert_ptr_ne(world, NULL);
    ck_assert_int_eq(nblex_world_open(world), 0);
    
    nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
    ck_assert_ptr_ne(input, NULL);
    
    /* Create aggregation event */
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    json_t* data = json_object();
    json_object_set_new(data, "nql_result_type", json_string("aggregation"));
    
    json_t* metrics = json_object();
    json_object_set_new(metrics, "count", json_integer(10));
    json_object_set_new(data, "metrics", metrics);
    
    event->data = data;
    event->input = NULL;
    
    /* Write and flush */
    ck_assert_int_eq(nblex_metrics_output_write(output, event), 0);
    ck_assert_int_eq(nblex_metrics_output_flush(output), 0);
    
    /* Write another event and flush again */
    json_object_set_new(metrics, "count", json_integer(20));
    ck_assert_int_eq(nblex_metrics_output_write(output, event), 0);
    ck_assert_int_eq(nblex_metrics_output_flush(output), 0);
    
    /* Check file contents - should only have the latest metrics */
    char* content = read_file_contents(tmpfile);
    ck_assert_ptr_ne(content, NULL);
    
    /* Count occurrences of "20.000000" - should appear once */
    int count_20 = 0;
    char* pos = content;
    while ((pos = strstr(pos, "20.000000")) != NULL) {
        count_20++;
        pos++;
    }
    ck_assert_int_eq(count_20, 1);
    
    free(content);
    nblex_event_free(event);
    nblex_input_free(input);
    nblex_world_free(world);
    nblex_metrics_output_free(output);
    unlink(tmpfile);
}
END_TEST

START_TEST(test_metrics_output_multiple_metrics) {
    char tmpfile[] = "/tmp/nblex_test_metrics_XXXXXX";
    int fd = mkstemp(tmpfile);
    ck_assert_int_ne(fd, -1);
    close(fd);
    unlink(tmpfile);
    
    metrics_output_t* output = nblex_metrics_output_new(tmpfile, "prometheus");
    ck_assert_ptr_ne(output, NULL);
    
    nblex_world* world = nblex_world_new();
    ck_assert_ptr_ne(world, NULL);
    ck_assert_int_eq(nblex_world_open(world), 0);
    
    nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
    ck_assert_ptr_ne(input, NULL);
    
    /* Create aggregation event with multiple metrics */
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    json_t* data = json_object();
    json_object_set_new(data, "nql_result_type", json_string("aggregation"));
    
    json_t* metrics = json_object();
    json_object_set_new(metrics, "count", json_integer(100));
    json_object_set_new(metrics, "sum", json_real(5000.0));
    json_object_set_new(metrics, "avg", json_real(50.0));
    json_object_set_new(metrics, "min", json_real(10.0));
    json_object_set_new(metrics, "max", json_real(200.0));
    json_object_set_new(data, "metrics", metrics);
    
    event->data = data;
    event->input = NULL;
    
    /* Write and flush */
    ck_assert_int_eq(nblex_metrics_output_write(output, event), 0);
    ck_assert_int_eq(nblex_metrics_output_flush(output), 0);
    
    /* Check file contents */
    char* content = read_file_contents(tmpfile);
    ck_assert_ptr_ne(content, NULL);
    ck_assert_ptr_ne(strstr(content, "metric=\"count\""), NULL);
    ck_assert_ptr_ne(strstr(content, "metric=\"sum\""), NULL);
    ck_assert_ptr_ne(strstr(content, "metric=\"avg\""), NULL);
    ck_assert_ptr_ne(strstr(content, "metric=\"min\""), NULL);
    ck_assert_ptr_ne(strstr(content, "metric=\"max\""), NULL);
    
    free(content);
    nblex_event_free(event);
    nblex_input_free(input);
    nblex_world_free(world);
    nblex_metrics_output_free(output);
    unlink(tmpfile);
}
END_TEST

Suite* metrics_output_suite(void) {
    Suite* s = suite_create("Metrics Output");

    TCase* tc_basic = tcase_create("Basic");
    tcase_add_test(tc_basic, test_metrics_output_basic);
    tcase_add_test(tc_basic, test_metrics_output_correlation_event);
    suite_add_tcase(s, tc_basic);

    TCase* tc_aggregation = tcase_create("Aggregation");
    tcase_add_test(tc_aggregation, test_metrics_output_aggregation_no_labels);
    tcase_add_test(tc_aggregation, test_metrics_output_aggregation_with_labels);
    tcase_add_test(tc_aggregation, test_metrics_output_multiple_metrics);
    tcase_add_test(tc_aggregation, test_metrics_output_metrics_cleared_after_flush);
    suite_add_tcase(s, tc_aggregation);

    return s;
}

int main(void) {
    int number_failed;
    Suite* s = metrics_output_suite();
    SRunner* sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

