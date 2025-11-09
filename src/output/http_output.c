/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * http_output.c - HTTP output handler for webhooks
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

/* HTTP output state */
typedef struct {
    char* url;
    char* method;      /* POST, PUT, etc. */
    char* content_type;
    char* user_agent;
    int timeout_seconds;
    CURL* curl_handle;
} http_output_t;

/* Free HTTP output */
void nblex_http_output_free(http_output_t* output) {
    if (!output) {
        return;
    }

    if (output->curl_handle) {
        curl_easy_cleanup(output->curl_handle);
    }

    free(output->url);
    free(output->method);
    free(output->content_type);
    free(output->user_agent);
    free(output);
}

/* Initialize HTTP output */
http_output_t* nblex_http_output_new(const char* url) {
    if (!url) {
        return NULL;
    }

    http_output_t* output = calloc(1, sizeof(http_output_t));
    if (!output) {
        return NULL;
    }

    output->url = strdup(url);
    if (!output->url) {
        free(output);
        return NULL;
    }

    /* Default settings */
    output->method = strdup("POST");
    output->content_type = strdup("application/json");
    output->user_agent = strdup("nblex/1.0");
    output->timeout_seconds = 30;

    if (!output->method || !output->content_type || !output->user_agent) {
        nblex_http_output_free(output);
        return NULL;
    }

    /* Initialize curl */
    output->curl_handle = curl_easy_init();
    if (!output->curl_handle) {
        nblex_http_output_free(output);
        return NULL;
    }

    /* Set URL */
    curl_easy_setopt(output->curl_handle, CURLOPT_URL, output->url);

    /* Set method */
    curl_easy_setopt(output->curl_handle, CURLOPT_POST, 1L);

    /* Set headers */
    struct curl_slist* headers = NULL;
    char content_type_header[256];
    snprintf(content_type_header, sizeof(content_type_header), "Content-Type: %s", output->content_type);
    headers = curl_slist_append(headers, content_type_header);

    char user_agent_header[256];
    snprintf(user_agent_header, sizeof(user_agent_header), "User-Agent: %s", output->user_agent);
    headers = curl_slist_append(headers, user_agent_header);

    curl_easy_setopt(output->curl_handle, CURLOPT_HTTPHEADER, headers);

    /* Set timeout */
    curl_easy_setopt(output->curl_handle, CURLOPT_TIMEOUT, output->timeout_seconds);

    /* Don't verify SSL by default (for development) */
    curl_easy_setopt(output->curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(output->curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);

    return output;
}

/* Set HTTP method */
void nblex_http_output_set_method(http_output_t* output, const char* method) {
    if (!output || !method) {
        return;
    }

    free(output->method);
    output->method = strdup(method);

    /* Update curl options based on method */
    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(output->curl_handle, CURLOPT_POST, 1L);
        curl_easy_setopt(output->curl_handle, CURLOPT_HTTPGET, 0L);
        curl_easy_setopt(output->curl_handle, CURLOPT_CUSTOMREQUEST, NULL);
    } else if (strcmp(method, "PUT") == 0) {
        curl_easy_setopt(output->curl_handle, CURLOPT_POST, 0L);
        curl_easy_setopt(output->curl_handle, CURLOPT_HTTPGET, 0L);
        curl_easy_setopt(output->curl_handle, CURLOPT_CUSTOMREQUEST, "PUT");
    } else {
        curl_easy_setopt(output->curl_handle, CURLOPT_POST, 0L);
        curl_easy_setopt(output->curl_handle, CURLOPT_HTTPGET, 0L);
        curl_easy_setopt(output->curl_handle, CURLOPT_CUSTOMREQUEST, method);
    }
}

/* Set timeout */
void nblex_http_output_set_timeout(http_output_t* output, int timeout_seconds) {
    if (!output || timeout_seconds <= 0) {
        return;
    }

    output->timeout_seconds = timeout_seconds;
    curl_easy_setopt(output->curl_handle, CURLOPT_TIMEOUT, timeout_seconds);
}

/* Write event to HTTP endpoint */
int nblex_http_output_write(http_output_t* output, nblex_event* event) {
    if (!output || !output->curl_handle || !event) {
        return -1;
    }

    /* Format event as JSON */
    char* json_str = nblex_event_to_json_string(event);
    if (!json_str) {
        return -1;
    }

    /* Set POST data */
    curl_easy_setopt(output->curl_handle, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(output->curl_handle, CURLOPT_POSTFIELDSIZE, strlen(json_str));

    /* Perform request */
    CURLcode res = curl_easy_perform(output->curl_handle);

    free(json_str);

    if (res != CURLE_OK) {
        fprintf(stderr, "HTTP output failed: %s\n", curl_easy_strerror(res));
        return -1;
    }

    return 0;
}
