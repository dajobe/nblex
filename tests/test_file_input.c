/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_file_input.c - Unit tests for file input and format detection
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../src/nblex_internal.h"

/* Forward declarations for functions not in public API */
nblex_log_format nblex_detect_log_format(const char* path);
json_t* nblex_parse_nginx_line(const char* line);

/* Test suite declaration */
Suite* file_input_suite(void);

/* Helper: Create temporary file with content */
static char* create_temp_file(const char* content) {
    char template[] = "/tmp/nblex_test_XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        return NULL;
    }
    
    if (content) {
        write(fd, content, strlen(content));
    }
    close(fd);
    
    char* path = strdup(template);
    return path;
}

START_TEST(test_format_detection_jsonl) {
    nblex_log_format format = nblex_detect_log_format("/var/log/app.jsonl");
    ck_assert_int_eq(format, NBLEX_FORMAT_JSON);
    
    format = nblex_detect_log_format("/tmp/test.JSON");
    ck_assert_int_eq(format, NBLEX_FORMAT_JSON);
}
END_TEST

START_TEST(test_format_detection_nginx) {
    nblex_log_format format = nblex_detect_log_format("/var/log/nginx/access.log");
    ck_assert_int_eq(format, NBLEX_FORMAT_NGINX);
    
    format = nblex_detect_log_format("/tmp/nginx_error.log");
    ck_assert_int_eq(format, NBLEX_FORMAT_NGINX);
    
    format = nblex_detect_log_format("/var/log/NGINX/access.log");
    ck_assert_int_eq(format, NBLEX_FORMAT_NGINX);
}
END_TEST

START_TEST(test_format_detection_syslog) {
    nblex_log_format format = nblex_detect_log_format("/var/log/syslog");
    ck_assert_int_eq(format, NBLEX_FORMAT_SYSLOG);
    
    format = nblex_detect_log_format("/tmp/syslog_messages.log");
    ck_assert_int_eq(format, NBLEX_FORMAT_SYSLOG);
}
END_TEST

START_TEST(test_format_detection_logfmt) {
    nblex_log_format format = nblex_detect_log_format("/var/log/app.logfmt");
    ck_assert_int_eq(format, NBLEX_FORMAT_LOGFMT);
    
    format = nblex_detect_log_format("/tmp/logfmt_output");
    ck_assert_int_eq(format, NBLEX_FORMAT_LOGFMT);
}
END_TEST

START_TEST(test_format_detection_default) {
    nblex_log_format format = nblex_detect_log_format("/var/log/generic.log");
    ck_assert_int_eq(format, NBLEX_FORMAT_JSON);
    
    format = nblex_detect_log_format(NULL);
    ck_assert_int_eq(format, NBLEX_FORMAT_JSON);
}
END_TEST

START_TEST(test_nginx_parser_basic) {
    const char* line = "127.0.0.1 - - [09/Nov/2025:17:28:06 -0800] \"GET / HTTP/2.0\" 403 146 \"-\" \"curl/8.7.1\"";
    json_t* result = nblex_parse_nginx_line(line);
    
    ck_assert_ptr_ne(result, NULL);
    ck_assert(json_is_object(result));
    
    json_t* remote_addr = json_object_get(result, "remote_addr");
    ck_assert_ptr_ne(remote_addr, NULL);
    ck_assert_str_eq(json_string_value(remote_addr), "127.0.0.1");
    
    json_t* request = json_object_get(result, "request");
    ck_assert_ptr_ne(request, NULL);
    ck_assert_str_eq(json_string_value(request), "GET / HTTP/2.0");
    
    json_t* method = json_object_get(result, "method");
    ck_assert_ptr_ne(method, NULL);
    ck_assert_str_eq(json_string_value(method), "GET");
    
    json_t* path = json_object_get(result, "path");
    ck_assert_ptr_ne(path, NULL);
    ck_assert_str_eq(json_string_value(path), "/");
    
    json_t* protocol = json_object_get(result, "protocol");
    ck_assert_ptr_ne(protocol, NULL);
    ck_assert_str_eq(json_string_value(protocol), "HTTP/2.0");
    
    json_t* status = json_object_get(result, "status");
    ck_assert_ptr_ne(status, NULL);
    ck_assert_int_eq(json_integer_value(status), 403);
    
    json_t* bytes = json_object_get(result, "body_bytes_sent");
    ck_assert_ptr_ne(bytes, NULL);
    ck_assert_int_eq(json_integer_value(bytes), 146);
    
    json_decref(result);
}
END_TEST

START_TEST(test_nginx_parser_with_referer) {
    const char* line = "192.168.1.1 - user [09/Nov/2025:17:30:00 -0800] \"POST /api/data HTTP/1.1\" 200 1234 \"https://example.com\" \"Mozilla/5.0\"";
    json_t* result = nblex_parse_nginx_line(line);
    
    ck_assert_ptr_ne(result, NULL);
    
    json_t* remote_user = json_object_get(result, "remote_user");
    ck_assert_ptr_ne(remote_user, NULL);
    ck_assert_str_eq(json_string_value(remote_user), "user");
    
    json_t* referer = json_object_get(result, "http_referer");
    ck_assert_ptr_ne(referer, NULL);
    ck_assert_str_eq(json_string_value(referer), "https://example.com");
    
    json_t* user_agent = json_object_get(result, "http_user_agent");
    ck_assert_ptr_ne(user_agent, NULL);
    ck_assert_str_eq(json_string_value(user_agent), "Mozilla/5.0");
    
    json_t* method = json_object_get(result, "method");
    ck_assert_ptr_ne(method, NULL);
    ck_assert_str_eq(json_string_value(method), "POST");
    
    json_t* path = json_object_get(result, "path");
    ck_assert_ptr_ne(path, NULL);
    ck_assert_str_eq(json_string_value(path), "/api/data");
    
    json_decref(result);
}
END_TEST

START_TEST(test_nginx_parser_minus_fields) {
    const char* line = "10.0.0.1 - - [09/Nov/2025:12:00:00 -0800] \"GET /test HTTP/1.0\" 200 500 \"-\" \"-\"";
    json_t* result = nblex_parse_nginx_line(line);
    
    ck_assert_ptr_ne(result, NULL);
    
    /* Fields with "-" should not be present */
    json_t* referer = json_object_get(result, "http_referer");
    ck_assert_ptr_eq(referer, NULL);
    
    json_t* user_agent = json_object_get(result, "http_user_agent");
    ck_assert_ptr_eq(user_agent, NULL);
    
    json_decref(result);
}
END_TEST

START_TEST(test_file_input_creation) {
    nblex_world* world = nblex_world_new();
    ck_assert_ptr_ne(world, NULL);
    
    char* test_file = create_temp_file("test line\n");
    ck_assert_ptr_ne(test_file, NULL);
    
    nblex_input* input = nblex_input_file_new(world, test_file);
    ck_assert_ptr_ne(input, NULL);
    ck_assert_int_eq(input->type, NBLEX_INPUT_FILE);
    
    /* Cleanup */
    unlink(test_file);
    free(test_file);
    nblex_world_free(world);
}
END_TEST

START_TEST(test_file_input_format_detection) {
    nblex_world* world = nblex_world_new();
    ck_assert_ptr_ne(world, NULL);
    
    char* nginx_file = create_temp_file(NULL);
    ck_assert_ptr_ne(nginx_file, NULL);
    
    /* Set filename to contain nginx */
    char* nginx_path = malloc(strlen(nginx_file) + 20);
    strcpy(nginx_path, "/tmp/nginx_test_");
    strcat(nginx_path, strrchr(nginx_file, '_') + 1);
    unlink(nginx_file);
    free(nginx_file);
    
    /* Create empty file */
    FILE* f = fopen(nginx_path, "w");
    fclose(f);
    
    nblex_input* input = nblex_input_file_new(world, nginx_path);
    ck_assert_ptr_ne(input, NULL);
    
    /* Auto-detect format */
    nblex_log_format format = nblex_detect_log_format(nginx_path);
    nblex_input_set_format(input, format);
    ck_assert_int_eq(input->format, NBLEX_FORMAT_NGINX);
    
    /* Cleanup */
    unlink(nginx_path);
    free(nginx_path);
    nblex_world_free(world);
}
END_TEST

Suite* file_input_suite(void) {
    Suite* s = suite_create("File Input");

    TCase* tc_format = tcase_create("Format Detection");
    tcase_add_test(tc_format, test_format_detection_jsonl);
    tcase_add_test(tc_format, test_format_detection_nginx);
    tcase_add_test(tc_format, test_format_detection_syslog);
    tcase_add_test(tc_format, test_format_detection_logfmt);
    tcase_add_test(tc_format, test_format_detection_default);
    suite_add_tcase(s, tc_format);

    TCase* tc_nginx = tcase_create("Nginx Parser");
    tcase_add_test(tc_nginx, test_nginx_parser_basic);
    tcase_add_test(tc_nginx, test_nginx_parser_with_referer);
    tcase_add_test(tc_nginx, test_nginx_parser_minus_fields);
    suite_add_tcase(s, tc_nginx);

    TCase* tc_file = tcase_create("File Input");
    tcase_add_test(tc_file, test_file_input_creation);
    tcase_add_test(tc_file, test_file_input_format_detection);
    suite_add_tcase(s, tc_file);

    return s;
}

int main(void) {
    int number_failed;
    Suite* s = file_input_suite();
    SRunner* sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
