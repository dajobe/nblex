/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * logfmt_parser.c - logfmt (key=value) log parser
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Parse a single logfmt key=value pair */
static int parse_key_value_pair(const char** pos, char** key, char** value) {
    const char* start = *pos;
    const char* end;

    /* Skip leading whitespace */
    while (*start && isspace(*start)) {
        start++;
    }

    /* Find key end */
    end = start;
    while (*end && *end != '=' && !isspace(*end)) {
        end++;
    }

    if (end == start || *end != '=') {
        /* No valid key=value pair found */
        return 0;
    }

    /* Extract key */
    size_t key_len = end - start;
    *key = malloc(key_len + 1);
    if (!*key) {
        return -1;
    }
    memcpy(*key, start, key_len);
    (*key)[key_len] = '\0';

    /* Move to value */
    start = end + 1;

    /* Find value end (next space or end of string) */
    end = start;
    if (*end == '"') {
        /* Quoted value */
        end++;
        start = end; /* Skip opening quote */
        while (*end && *end != '"') {
            if (*end == '\\' && *(end + 1)) {
                /* Skip escaped character */
                end += 2;
            } else {
                end++;
            }
        }
        if (*end == '"') {
            /* Extract quoted value */
            size_t value_len = end - start;
            *value = malloc(value_len + 1);
            if (!*value) {
                free(*key);
                *key = NULL;
                return -1;
            }
            memcpy(*value, start, value_len);
            (*value)[value_len] = '\0';
            *pos = end + 1; /* Skip closing quote */
        } else {
            /* Unterminated quote */
            free(*key);
            *key = NULL;
            return 0;
        }
    } else {
        /* Unquoted value */
        while (*end && !isspace(*end)) {
            end++;
        }
        size_t value_len = end - start;
        *value = malloc(value_len + 1);
        if (!*value) {
            free(*key);
            *key = NULL;
            return -1;
        }
        memcpy(*value, start, value_len);
        (*value)[value_len] = '\0';
        *pos = end;
    }

    return 1;
}

/* Parse logfmt line into JSON object */
json_t* nblex_parse_logfmt_line(const char* line) {
    if (!line || !*line) {
        return NULL;
    }

    json_t* root = json_object();
    if (!root) {
        return NULL;
    }

    const char* pos = line;
    int found_pairs = 0;

    while (*pos) {
        char* key = NULL;
        char* value = NULL;

        int result = parse_key_value_pair(&pos, &key, &value);
        if (result < 0) {
            /* Memory error */
            json_decref(root);
            return NULL;
        } else if (result == 0) {
            /* Skip invalid content */
            while (*pos && !isspace(*pos)) {
                pos++;
            }
            continue;
        }

        found_pairs++;

        /* Try to detect value type */
        json_t* json_value = NULL;

        /* Check if it's a number */
        if (*value == '-' || (*value >= '0' && *value <= '9')) {
            char* endptr;
            long long_val = strtol(value, &endptr, 10);
            if (*endptr == '\0') {
                /* Pure integer */
                json_value = json_integer(long_val);
            } else if (*endptr == '.' || *endptr == 'e' || *endptr == 'E') {
                /* Try float */
                double double_val = strtod(value, &endptr);
                if (*endptr == '\0') {
                    json_value = json_real(double_val);
                }
            }
        }

        /* Check for boolean values */
        if (!json_value) {
            if (strcmp(value, "true") == 0) {
                json_value = json_true();
            } else if (strcmp(value, "false") == 0) {
                json_value = json_false();
            }
        }

        /* Default to string */
        if (!json_value) {
            json_value = json_string(value);
        }

        if (!json_value) {
            free(key);
            free(value);
            json_decref(root);
            return NULL;
        }

        if (json_object_set_new(root, key, json_value) != 0) {
            free(key);
            free(value);
            json_decref(root);
            return NULL;
        }

        free(key);
        free(value);
    }

    /* If no valid key-value pairs were found, return NULL */
    if (found_pairs == 0) {
        json_decref(root);
        return NULL;
    }

    return root;
}
