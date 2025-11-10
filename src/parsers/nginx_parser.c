/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nginx_parser.c - Nginx combined log format parser
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Parse nginx combined log format:
 * $remote_addr - $remote_user [$time_local] "$request" $status $body_bytes_sent "$http_referer" "$http_user_agent"
 *
 * Example:
 * 127.0.0.1 - - [09/Nov/2025:17:28:06 -0800] "GET / HTTP/2.0" 403 146 "-" "curl/8.7.1"
 */
json_t* nblex_parse_nginx_line(const char* line) {
  if (!line || !*line) {
    return NULL;
  }

  json_t* root = json_object();
  if (!root) {
    return NULL;
  }

  const char* pos = line;
  const char* end;

  /* Parse remote_addr (IP address) - everything up to first space */
  end = pos;
  while (*end && !isspace(*end)) {
    end++;
  }
  if (end > pos) {
    size_t len = end - pos;
    char* remote_addr = malloc(len + 1);
    if (remote_addr) {
      memcpy(remote_addr, pos, len);
      remote_addr[len] = '\0';
      json_object_set_new(root, "remote_addr", json_string(remote_addr));
      free(remote_addr);
    }
  }
  pos = end;

  /* Skip whitespace and dash */
  while (*pos && (isspace(*pos) || *pos == '-')) {
    pos++;
  }

  /* Parse remote_user - everything up to space or [ */
  end = pos;
  while (*end && !isspace(*end) && *end != '[') {
    end++;
  }
  if (end > pos && *pos != '-') {
    size_t len = end - pos;
    char* remote_user = malloc(len + 1);
    if (remote_user) {
      memcpy(remote_user, pos, len);
      remote_user[len] = '\0';
      json_object_set_new(root, "remote_user", json_string(remote_user));
      free(remote_user);
    }
  }
  pos = end;

  /* Skip whitespace */
  while (*pos && isspace(*pos)) {
    pos++;
  }

  /* Parse time_local - [DD/Mon/YYYY:HH:MM:SS TZ] */
  if (*pos == '[') {
    pos++; /* Skip [ */
    end = pos;
    while (*end && *end != ']') {
      end++;
    }
    if (*end == ']') {
      size_t len = end - pos;
      char* time_local = malloc(len + 1);
      if (time_local) {
        memcpy(time_local, pos, len);
        time_local[len] = '\0';
        json_object_set_new(root, "time_local", json_string(time_local));
        free(time_local);
      }
      pos = end + 1;
    }
  }

  /* Skip whitespace */
  while (*pos && isspace(*pos)) {
    pos++;
  }

  /* Parse request - "METHOD PATH PROTOCOL" */
  if (*pos == '"') {
    pos++; /* Skip opening quote */
    end = pos;
    while (*end && *end != '"') {
      if (*end == '\\' && *(end + 1) == '"') {
        end += 2; /* Skip escaped quote */
      } else {
        end++;
      }
    }
    if (*end == '"') {
      size_t len = end - pos;
      char* request = malloc(len + 1);
      if (request) {
        memcpy(request, pos, len);
        request[len] = '\0';
        json_object_set_new(root, "request", json_string(request));
        
        /* Try to parse method, path, and protocol from request */
        char* method_end = strchr(request, ' ');
        if (method_end) {
          *method_end = '\0';
          json_object_set_new(root, "method", json_string(request));
          
          char* path_start = method_end + 1;
          char* path_end = strchr(path_start, ' ');
          if (path_end) {
            *path_end = '\0';
            json_object_set_new(root, "path", json_string(path_start));
            
            char* protocol = path_end + 1;
            if (*protocol) {
              json_object_set_new(root, "protocol", json_string(protocol));
            }
          } else {
            json_object_set_new(root, "path", json_string(path_start));
          }
        }
        
        free(request);
      }
      pos = end + 1;
    }
  }

  /* Skip whitespace */
  while (*pos && isspace(*pos)) {
    pos++;
  }

  /* Parse status code */
  if (isdigit(*pos)) {
    char* status_end;
    long status = strtol(pos, &status_end, 10);
    if (status_end > pos) {
      json_object_set_new(root, "status", json_integer(status));
      pos = status_end;
    }
  }

  /* Skip whitespace */
  while (*pos && isspace(*pos)) {
    pos++;
  }

  /* Parse body_bytes_sent */
  if (isdigit(*pos)) {
    char* bytes_end;
    long bytes = strtol(pos, &bytes_end, 10);
    if (bytes_end > pos) {
      json_object_set_new(root, "body_bytes_sent", json_integer(bytes));
      pos = bytes_end;
    }
  }

  /* Skip whitespace */
  while (*pos && isspace(*pos)) {
    pos++;
  }

  /* Parse http_referer - quoted string */
  if (*pos == '"') {
    pos++; /* Skip opening quote */
    end = pos;
    while (*end && *end != '"') {
      if (*end == '\\' && *(end + 1) == '"') {
        end += 2;
      } else {
        end++;
      }
    }
    if (*end == '"') {
      size_t len = end - pos;
      char* referer = malloc(len + 1);
      if (referer) {
        memcpy(referer, pos, len);
        referer[len] = '\0';
        if (len > 0 && strcmp(referer, "-") != 0) {
          json_object_set_new(root, "http_referer", json_string(referer));
        }
        free(referer);
      }
      pos = end + 1;
    }
  }

  /* Skip whitespace */
  while (*pos && isspace(*pos)) {
    pos++;
  }

  /* Parse http_user_agent - quoted string (last field) */
  if (*pos == '"') {
    pos++; /* Skip opening quote */
    end = pos;
    while (*end && *end != '"') {
      if (*end == '\\' && *(end + 1) == '"') {
        end += 2;
      } else {
        end++;
      }
    }
    if (*end == '"') {
      size_t len = end - pos;
      char* user_agent = malloc(len + 1);
      if (user_agent) {
        memcpy(user_agent, pos, len);
        user_agent[len] = '\0';
        if (len > 0 && strcmp(user_agent, "-") != 0) {
          json_object_set_new(root, "http_user_agent", json_string(user_agent));
        }
        free(user_agent);
      }
    }
  }

  return root;
}
