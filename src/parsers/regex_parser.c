/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * regex_parser.c - Regex-based log parser
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

/* Feature test macros - let nblex_internal.h handle platform-specific setup */

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>

/* Regex parser context */
struct regex_parser_s {
    pcre2_code* regex;
    pcre2_match_data* match_data;
    int capture_count;
    char** field_names;
    int field_count;
};
typedef struct regex_parser_s regex_parser_t;

/* Initialize regex parser with pattern and field mapping */
regex_parser_t* nblex_regex_parser_new(const char* pattern, const char** field_names, int field_count) {
    if (!pattern || !field_names || field_count <= 0) {
        return NULL;
    }

    regex_parser_t* parser = calloc(1, sizeof(regex_parser_t));
    if (!parser) {
        return NULL;
    }

    int error_code;
    PCRE2_SIZE error_offset;

    parser->regex = pcre2_compile(
        (PCRE2_SPTR)pattern,
        PCRE2_ZERO_TERMINATED,
        PCRE2_UTF | PCRE2_UCP,
        &error_code,
        &error_offset,
        NULL
    );

    if (!parser->regex) {
        free(parser);
        return NULL;
    }

    parser->match_data = pcre2_match_data_create_from_pattern(parser->regex, NULL);
    if (!parser->match_data) {
        pcre2_code_free(parser->regex);
        free(parser);
        return NULL;
    }

    /* Get capture count */
    pcre2_pattern_info(parser->regex, PCRE2_INFO_CAPTURECOUNT, &parser->capture_count);

    /* Store field names */
    parser->field_names = malloc(sizeof(char*) * field_count);
    if (!parser->field_names) {
        pcre2_match_data_free(parser->match_data);
        pcre2_code_free(parser->regex);
        free(parser);
        return NULL;
    }

    for (int i = 0; i < field_count; i++) {
        parser->field_names[i] = strdup(field_names[i]);
        if (!parser->field_names[i]) {
            for (int j = 0; j < i; j++) {
                free(parser->field_names[j]);
            }
            free(parser->field_names);
            pcre2_match_data_free(parser->match_data);
            pcre2_code_free(parser->regex);
            free(parser);
            return NULL;
        }
    }

    parser->field_count = field_count;

    return parser;
}

/* Free regex parser */
void nblex_regex_parser_free(regex_parser_t* parser) {
    if (!parser) {
        return;
    }

    if (parser->field_names) {
        for (int i = 0; i < parser->field_count; i++) {
            free(parser->field_names[i]);
        }
        free(parser->field_names);
    }

    if (parser->match_data) {
        pcre2_match_data_free(parser->match_data);
    }

    if (parser->regex) {
        pcre2_code_free(parser->regex);
    }

    free(parser);
}

/* Parse line with regex into JSON object */
json_t* nblex_regex_parser_parse(regex_parser_t* parser, const char* line) {
    if (!parser || !line) {
        return NULL;
    }

    int rc = pcre2_match(
        parser->regex,
        (PCRE2_SPTR)line,
        strlen(line),
        0,                    /* start at offset 0 */
        0,                    /* default options */
        parser->match_data,
        NULL                  /* default match context */
    );

    if (rc < 0) {
        /* No match or error */
        return NULL;
    }

    json_t* root = json_object();
    if (!root) {
        return NULL;
    }

    PCRE2_SIZE* ovector = pcre2_get_ovector_pointer(parser->match_data);

    /* Extract captured groups */
    int capture_index = 0;
    for (int i = 0; i < parser->field_count && capture_index < rc; i++) {
        PCRE2_SIZE start = ovector[2 * capture_index];
        PCRE2_SIZE end = ovector[2 * capture_index + 1];

        if (start < end) {
            size_t len = end - start;
            char* value = malloc(len + 1);
            if (!value) {
                json_decref(root);
                return NULL;
            }

            memcpy(value, line + start, len);
            value[len] = '\0';

            json_t* json_value = json_string(value);
            free(value);

            if (!json_value) {
                json_decref(root);
                return NULL;
            }

            if (json_object_set_new(root, parser->field_names[i], json_value) != 0) {
                json_decref(root);
                return NULL;
            }
        }

        capture_index++;
    }

    return root;
}
