/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * http_parser.c - HTTP protocol dissector
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Parse HTTP request line: METHOD URI VERSION */
static int parse_http_request_line(const char* line, json_t* http) {
    if (!line || !http) {
        return -1;
    }

    const char* pos = line;
    const char* start;

    /* Extract method */
    start = pos;
    while (*pos && !isspace(*pos)) {
        pos++;
    }

    if (pos == start) {
        return -1;
    }

    size_t method_len = pos - start;
    char* method = malloc(method_len + 1);
    if (!method) {
        return -1;
    }
    memcpy(method, start, method_len);
    method[method_len] = '\0';

    json_object_set_new(http, "method", json_string(method));
    free(method);

    /* Skip spaces */
    while (*pos && isspace(*pos)) {
        pos++;
    }

    /* Extract URI */
    start = pos;
    while (*pos && !isspace(*pos)) {
        pos++;
    }

    if (pos > start) {
        size_t uri_len = pos - start;
        char* uri = malloc(uri_len + 1);
        if (uri) {
            memcpy(uri, start, uri_len);
            uri[uri_len] = '\0';
            json_object_set_new(http, "uri", json_string(uri));
            free(uri);
        }
    }

    /* Skip spaces */
    while (*pos && isspace(*pos)) {
        pos++;
    }

    /* Extract version */
    start = pos;
    while (*pos && !isspace(*pos) && *pos != '\r' && *pos != '\n') {
        pos++;
    }

    if (pos > start) {
        size_t version_len = pos - start;
        char* version = malloc(version_len + 1);
        if (version) {
            memcpy(version, start, version_len);
            version[version_len] = '\0';
            json_object_set_new(http, "version", json_string(version));
            free(version);
        }
    }

    return 0;
}

/* Parse HTTP status line: VERSION CODE REASON */
static int parse_http_status_line(const char* line, json_t* http) {
    if (!line || !http) {
        return -1;
    }

    const char* pos = line;

    /* Extract version */
    const char* start = pos;
    while (*pos && !isspace(*pos)) {
        pos++;
    }

    if (pos > start) {
        size_t version_len = pos - start;
        char* version = malloc(version_len + 1);
        if (version) {
            memcpy(version, start, version_len);
            version[version_len] = '\0';
            json_object_set_new(http, "version", json_string(version));
            free(version);
        }
    }

    /* Skip spaces */
    while (*pos && isspace(*pos)) {
        pos++;
    }

    /* Extract status code */
    start = pos;
    while (*pos && isdigit(*pos)) {
        pos++;
    }

    if (pos > start) {
        int status_code = atoi(start);
        json_object_set_new(http, "status_code", json_integer(status_code));
    }

    /* Skip spaces */
    while (*pos && isspace(*pos)) {
        pos++;
    }

    /* Extract reason phrase */
    start = pos;
    while (*pos && !isspace(*pos) && *pos != '\r' && *pos != '\n') {
        pos++;
    }

    if (pos > start) {
        size_t reason_len = pos - start;
        char* reason = malloc(reason_len + 1);
        if (reason) {
            memcpy(reason, start, reason_len);
            reason[reason_len] = '\0';
            json_object_set_new(http, "reason", json_string(reason));
            free(reason);
        }
    }

    return 0;
}

/* Parse HTTP headers */
static int parse_http_headers(const char* data, size_t data_len, json_t* http) {
    if (!data || !http) {
        return -1;
    }

    json_t* headers = json_object();
    if (!headers) {
        return -1;
    }

    const char* pos = data;
    const char* end = data + data_len;

    while (pos < end) {
        /* Find end of line */
        const char* line_end = pos;
        while (line_end < end && *line_end != '\r' && *line_end != '\n') {
            line_end++;
        }

        if (line_end == pos) {
            /* Empty line - end of headers */
            break;
        }

        /* Find colon */
        const char* colon = pos;
        while (colon < line_end && *colon != ':') {
            colon++;
        }

        if (colon < line_end) {
            /* Extract header name */
            size_t name_len = colon - pos;
            char* name = malloc(name_len + 1);
            if (!name) {
                json_decref(headers);
                return -1;
            }

            memcpy(name, pos, name_len);
            name[name_len] = '\0';

            /* Convert to lowercase */
            for (size_t i = 0; i < name_len; i++) {
                name[i] = tolower(name[i]);
            }

            /* Extract header value */
            const char* value_start = colon + 1;
            while (value_start < line_end && isspace(*value_start)) {
                value_start++;
            }

            size_t value_len = line_end - value_start;
            char* value = malloc(value_len + 1);
            if (!value) {
                free(name);
                json_decref(headers);
                return -1;
            }

            memcpy(value, value_start, value_len);
            value[value_len] = '\0';

            json_object_set_new(headers, name, json_string(value));

            free(name);
            free(value);
        }

        /* Move to next line */
        pos = line_end;
        if (pos < end && *pos == '\r') {
            pos++;
        }
        if (pos < end && *pos == '\n') {
            pos++;
        }
    }

    json_object_set_new(http, "headers", headers);
    return 0;
}

/* Parse HTTP payload into JSON */
json_t* nblex_parse_http_payload(const char* data, size_t data_len, int is_request) {
    if (!data || data_len == 0) {
        return NULL;
    }

    json_t* http = json_object();
    if (!http) {
        return NULL;
    }

    const char* pos = data;
    const char* end = data + data_len;

    /* Find first line */
    const char* line_end = pos;
    while (line_end < end && *line_end != '\r' && *line_end != '\n') {
        line_end++;
    }

    if (line_end == pos) {
        json_decref(http);
        return NULL;
    }

    /* Extract first line */
    size_t first_line_len = line_end - pos;
    char* first_line = malloc(first_line_len + 1);
    if (!first_line) {
        json_decref(http);
        return NULL;
    }

    memcpy(first_line, pos, first_line_len);
    first_line[first_line_len] = '\0';

    /* Parse request or response line */
    int parse_result;
    if (is_request) {
        parse_result = parse_http_request_line(first_line, http);
    } else {
        parse_result = parse_http_status_line(first_line, http);
    }

    free(first_line);

    if (parse_result != 0) {
        json_decref(http);
        return NULL;
    }

    /* Move to headers */
    pos = line_end;
    if (pos < end && *pos == '\r') {
        pos++;
    }
    if (pos < end && *pos == '\n') {
        pos++;
    }

    /* Parse headers */
    size_t remaining = end - pos;
    if (parse_http_headers(pos, remaining, http) != 0) {
        json_decref(http);
        return NULL;
    }

    return http;
}
