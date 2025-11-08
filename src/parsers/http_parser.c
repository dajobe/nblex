/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * http_parser.c - HTTP/1.1 protocol parser
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_HTTP_LINE 8192

/* HTTP method detection */
static int is_http_method(const char* data, size_t len) {
  const char* methods[] = {
    "GET ", "POST ", "PUT ", "DELETE ", "HEAD ", "OPTIONS ",
    "PATCH ", "TRACE ", "CONNECT ", NULL
  };

  for (int i = 0; methods[i]; i++) {
    size_t method_len = strlen(methods[i]);
    if (len >= method_len && memcmp(data, methods[i], method_len) == 0) {
      return 1;
    }
  }
  return 0;
}

/* HTTP response detection */
static int is_http_response(const char* data, size_t len) {
  return len >= 8 && (memcmp(data, "HTTP/1.", 7) == 0);
}

/* Find end of line (CRLF or LF) */
static const char* find_eol(const char* data, size_t len) {
  for (size_t i = 0; i < len - 1; i++) {
    if (data[i] == '\r' && data[i + 1] == '\n') {
      return data + i;
    }
  }
  for (size_t i = 0; i < len; i++) {
    if (data[i] == '\n') {
      return data + i;
    }
  }
  return NULL;
}

/* Skip CRLF or LF */
static const char* skip_eol(const char* ptr, const char* end) {
  if (ptr < end && *ptr == '\r') {
    ptr++;
  }
  if (ptr < end && *ptr == '\n') {
    ptr++;
  }
  return ptr;
}

/* Parse HTTP request line */
static int parse_request_line(const char* line, size_t line_len,
                              char** method, char** uri, char** version) {
  const char* ptr = line;
  const char* end = line + line_len;

  /* Find method */
  const char* space1 = memchr(ptr, ' ', end - ptr);
  if (!space1) return 0;

  *method = strndup(ptr, space1 - ptr);
  ptr = space1 + 1;

  /* Find URI */
  const char* space2 = memchr(ptr, ' ', end - ptr);
  if (!space2) {
    free(*method);
    return 0;
  }

  *uri = strndup(ptr, space2 - ptr);
  ptr = space2 + 1;

  /* Get version */
  *version = strndup(ptr, end - ptr);

  return 1;
}

/* Parse HTTP response line */
static int parse_response_line(const char* line, size_t line_len,
                               char** version, int* status_code, char** status_text) {
  const char* ptr = line;
  const char* end = line + line_len;

  /* Find version */
  const char* space1 = memchr(ptr, ' ', end - ptr);
  if (!space1) return 0;

  *version = strndup(ptr, space1 - ptr);
  ptr = space1 + 1;

  /* Parse status code */
  char* endptr;
  *status_code = strtol(ptr, &endptr, 10);
  if (endptr == ptr || (*endptr != ' ' && endptr != end)) {
    free(*version);
    return 0;
  }

  /* Skip space after status code */
  if (endptr < end && *endptr == ' ') {
    endptr++;
  }

  /* Get status text */
  *status_text = strndup(endptr, end - endptr);

  return 1;
}

/* Parse HTTP header */
static int parse_header(const char* line, size_t line_len,
                       char** name, char** value) {
  const char* colon = memchr(line, ':', line_len);
  if (!colon) return 0;

  *name = strndup(line, colon - line);

  /* Skip colon and whitespace */
  const char* ptr = colon + 1;
  const char* end = line + line_len;
  while (ptr < end && (*ptr == ' ' || *ptr == '\t')) {
    ptr++;
  }

  *value = strndup(ptr, end - ptr);

  return 1;
}

/* Parse HTTP request */
json_t* nblex_parse_http_request(const u_char* payload, size_t payload_len) {
  if (!payload || payload_len == 0) {
    return NULL;
  }

  const char* data = (const char*)payload;

  /* Check if it looks like an HTTP request */
  if (!is_http_method(data, payload_len)) {
    return NULL;
  }

  /* Find first line */
  const char* eol = find_eol(data, payload_len);
  if (!eol) {
    return NULL;
  }

  size_t line_len = eol - data;
  if (line_len > MAX_HTTP_LINE) {
    return NULL;
  }

  /* Parse request line */
  char* method = NULL;
  char* uri = NULL;
  char* version = NULL;

  if (!parse_request_line(data, line_len, &method, &uri, &version)) {
    return NULL;
  }

  json_t* http_data = json_object();
  json_object_set_new(http_data, "type", json_string("request"));
  json_object_set_new(http_data, "method", json_string(method));
  json_object_set_new(http_data, "uri", json_string(uri));
  json_object_set_new(http_data, "version", json_string(version));

  free(method);
  free(uri);
  free(version);

  /* Parse headers */
  json_t* headers = json_object();
  const char* ptr = skip_eol(eol, data + payload_len);

  while (ptr < data + payload_len) {
    eol = find_eol(ptr, (data + payload_len) - ptr);
    if (!eol) break;

    line_len = eol - ptr;

    /* Empty line marks end of headers */
    if (line_len == 0) {
      break;
    }

    if (line_len > MAX_HTTP_LINE) {
      break;
    }

    char* name = NULL;
    char* value = NULL;
    if (parse_header(ptr, line_len, &name, &value)) {
      json_object_set_new(headers, name, json_string(value));
      free(name);
      free(value);
    }

    ptr = skip_eol(eol, data + payload_len);
  }

  json_object_set_new(http_data, "headers", headers);

  return http_data;
}

/* Parse HTTP response */
json_t* nblex_parse_http_response(const u_char* payload, size_t payload_len) {
  if (!payload || payload_len == 0) {
    return NULL;
  }

  const char* data = (const char*)payload;

  /* Check if it looks like an HTTP response */
  if (!is_http_response(data, payload_len)) {
    return NULL;
  }

  /* Find first line */
  const char* eol = find_eol(data, payload_len);
  if (!eol) {
    return NULL;
  }

  size_t line_len = eol - data;
  if (line_len > MAX_HTTP_LINE) {
    return NULL;
  }

  /* Parse response line */
  char* version = NULL;
  int status_code = 0;
  char* status_text = NULL;

  if (!parse_response_line(data, line_len, &version, &status_code, &status_text)) {
    return NULL;
  }

  json_t* http_data = json_object();
  json_object_set_new(http_data, "type", json_string("response"));
  json_object_set_new(http_data, "version", json_string(version));
  json_object_set_new(http_data, "status_code", json_integer(status_code));
  json_object_set_new(http_data, "status_text", json_string(status_text));

  free(version);
  free(status_text);

  /* Parse headers */
  json_t* headers = json_object();
  const char* ptr = skip_eol(eol, data + payload_len);

  while (ptr < data + payload_len) {
    eol = find_eol(ptr, (data + payload_len) - ptr);
    if (!eol) break;

    line_len = eol - ptr;

    /* Empty line marks end of headers */
    if (line_len == 0) {
      break;
    }

    if (line_len > MAX_HTTP_LINE) {
      break;
    }

    char* name = NULL;
    char* value = NULL;
    if (parse_header(ptr, line_len, &name, &value)) {
      json_object_set_new(headers, name, json_string(value));
      free(name);
      free(value);
    }

    ptr = skip_eol(eol, data + payload_len);
  }

  json_object_set_new(http_data, "headers", headers);

  return http_data;
}

/* Try to parse payload as HTTP (request or response) */
json_t* nblex_parse_http(const u_char* payload, size_t payload_len) {
  json_t* result;

  /* Try as request first */
  result = nblex_parse_http_request(payload, payload_len);
  if (result) {
    return result;
  }

  /* Try as response */
  result = nblex_parse_http_response(payload, payload_len);
  if (result) {
    return result;
  }

  return NULL;
}
