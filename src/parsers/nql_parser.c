/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nql_parser.c - nQL (nblex Query Language) parser
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* nQL query types */
typedef enum {
    NQL_TYPE_FILTER,        /* Simple filter */
    NQL_TYPE_CORRELATE,     /* Correlation query */
    NQL_TYPE_AGGREGATE,     /* Aggregation query */
    NQL_TYPE_SHOW,          /* Field selection */
    NQL_TYPE_PIPELINE       /* Pipeline of operations */
} nql_query_type_t;

/* Aggregation function types */
typedef enum {
    NQL_AGG_COUNT,
    NQL_AGG_SUM,
    NQL_AGG_AVG,
    NQL_AGG_MIN,
    NQL_AGG_MAX,
    NQL_AGG_PERCENTILE,
    NQL_AGG_DISTINCT
} nql_agg_func_type_t;

/* Window types */
typedef enum {
    NQL_WINDOW_NONE,
    NQL_WINDOW_TUMBLING,
    NQL_WINDOW_SLIDING
} nql_window_type_t;

/* Aggregation function */
typedef struct {
    nql_agg_func_type_t func;
    char* field;           /* NULL for count() */
    double percentile;     /* For percentile function */
} nql_agg_func_t;

/* Window specification */
typedef struct {
    nql_window_type_t type;
    uint64_t size_ms;      /* Window size in milliseconds */
    uint64_t slide_ms;     /* Slide interval (for sliding windows) */
} nql_window_t;

/* Aggregation query */
typedef struct {
    nql_agg_func_t* funcs;
    size_t funcs_count;
    char** group_by_fields;
    size_t group_by_count;
    filter_t* where_filter;  /* Optional WHERE clause */
    nql_window_t window;
} nql_aggregate_t;

/* Correlation query */
typedef struct {
    filter_t* left_filter;   /* First filter */
    filter_t* right_filter;  /* Second filter */
    uint64_t within_ms;      /* Time window in milliseconds */
} nql_correlate_t;

/* Field selection */
typedef struct {
    char** fields;
    size_t fields_count;
    filter_t* where_filter;  /* Optional WHERE clause */
} nql_show_t;

/* nQL query AST node */
typedef struct nql_query_s {
    nql_query_type_t type;
    union {
        filter_t* filter;        /* For FILTER type */
        nql_correlate_t* correlate;  /* For CORRELATE type */
        nql_aggregate_t* aggregate;   /* For AGGREGATE type */
        nql_show_t* show;        /* For SHOW type */
        struct {
            struct nql_query_s* left;
            struct nql_query_s* right;
        } pipeline;             /* For PIPELINE type */
    } data;
} nql_query_t;

/* Parser state */
typedef struct {
    const char* input;
    const char* pos;
    char* error_msg;
} nql_parser_t;

/* Forward declarations */
static void skip_whitespace(nql_parser_t* parser);
static int match_keyword(nql_parser_t* parser, const char* keyword);
static char* parse_identifier(nql_parser_t* parser);
static uint64_t parse_duration(nql_parser_t* parser);
static filter_t* parse_filter_expr(nql_parser_t* parser);
static nql_query_t* parse_query(nql_parser_t* parser);
static nql_query_t* parse_correlate(nql_parser_t* parser);
static nql_query_t* parse_aggregate(nql_parser_t* parser);
static nql_query_t* parse_show(nql_parser_t* parser);
static void free_query(nql_query_t* query);

/* Skip whitespace */
static void skip_whitespace(nql_parser_t* parser) {
    while (*parser->pos && isspace(*parser->pos)) {
        parser->pos++;
    }
}

/* Match keyword (case-insensitive) */
static int match_keyword(nql_parser_t* parser, const char* keyword) {
    skip_whitespace(parser);
    size_t len = strlen(keyword);
    if (strncasecmp(parser->pos, keyword, len) == 0) {
        char next = parser->pos[len];
        if (!next || isspace(next) || next == '(' || next == ')') {
            parser->pos += len;
            return 1;
        }
    }
    return 0;
}

/* Parse identifier */
static char* parse_identifier(nql_parser_t* parser) {
    skip_whitespace(parser);
    const char* start = parser->pos;
    
    if (!isalpha(*start) && *start != '_') {
        return NULL;
    }
    
    while (*parser->pos && (isalnum(*parser->pos) || *parser->pos == '_' || *parser->pos == '.')) {
        parser->pos++;
    }
    
    size_t len = parser->pos - start;
    char* ident = malloc(len + 1);
    if (!ident) {
        return NULL;
    }
    memcpy(ident, start, len);
    ident[len] = '\0';
    return ident;
}

/* Parse duration (e.g., "100ms", "1s", "5m", "1h") */
static uint64_t parse_duration(nql_parser_t* parser) {
    skip_whitespace(parser);
    
    char* endptr;
    unsigned long value = strtoul(parser->pos, &endptr, 10);
    if (endptr == parser->pos) {
        return 0;
    }
    
    parser->pos = endptr;
    skip_whitespace(parser);
    
    uint64_t multiplier = 1;
    if (*parser->pos == 'm') {
        parser->pos++;
        if (*parser->pos == 's') {
            /* milliseconds */
            multiplier = 1;
            parser->pos++;
        } else {
            /* minutes */
            multiplier = 60 * 1000;
        }
    } else if (*parser->pos == 's') {
        /* seconds */
        multiplier = 1000;
        parser->pos++;
    } else if (*parser->pos == 'h') {
        /* hours */
        multiplier = 60 * 60 * 1000;
        parser->pos++;
    }
    
    return value * multiplier;
}

/* Parse filter expression (reuses filter engine) */
static filter_t* parse_filter_expr(nql_parser_t* parser) {
    skip_whitespace(parser);
    
    /* Save current position */
    const char* start = parser->pos;
    const char* saved_pos = parser->pos;
    
    /* Find the end of the filter expression by looking ahead */
    const char* end = parser->pos;
    int paren_depth = 0;
    
    while (*end) {
        if (*end == '(') {
            paren_depth++;
        } else if (*end == ')') {
            paren_depth--;
        } else if (paren_depth == 0) {
            /* Check for keywords that end filter expressions */
            parser->pos = end;
            if (match_keyword(parser, "where") || 
                match_keyword(parser, "with") ||
                match_keyword(parser, "within") ||
                match_keyword(parser, "by") ||
                match_keyword(parser, "window") ||
                match_keyword(parser, "limit") ||
                *end == '|') {
                break;
            }
            parser->pos = end + 1;
        }
        end++;
    }
    
    if (end == start) {
        return NULL;
    }
    
    /* Extract filter string */
    size_t len = end - start;
    char* filter_str = malloc(len + 1);
    if (!filter_str) {
        parser->pos = saved_pos;
        return NULL;
    }
    memcpy(filter_str, start, len);
    filter_str[len] = '\0';
    
    /* Trim trailing whitespace */
    while (len > 0 && isspace(filter_str[len - 1])) {
        filter_str[--len] = '\0';
    }
    
    /* Parse using filter engine */
    filter_t* filter = nblex_filter_new(filter_str);
    free(filter_str);
    
    /* Update parser position */
    parser->pos = end;
    
    return filter;
}

/* Parse correlation query */
static nql_query_t* parse_correlate(nql_parser_t* parser) {
    if (!match_keyword(parser, "correlate")) {
        return NULL;
    }
    
    nql_query_t* query = calloc(1, sizeof(nql_query_t));
    if (!query) {
        return NULL;
    }
    
    query->type = NQL_TYPE_CORRELATE;
    query->data.correlate = calloc(1, sizeof(nql_correlate_t));
    if (!query->data.correlate) {
        free(query);
        return NULL;
    }
    
    /* Parse left filter */
    query->data.correlate->left_filter = parse_filter_expr(parser);
    if (!query->data.correlate->left_filter) {
        free(query->data.correlate);
        free(query);
        return NULL;
    }
    
    /* Expect "with" */
    if (!match_keyword(parser, "with")) {
        nblex_filter_free(query->data.correlate->left_filter);
        free(query->data.correlate);
        free(query);
        return NULL;
    }
    
    /* Parse right filter */
    query->data.correlate->right_filter = parse_filter_expr(parser);
    if (!query->data.correlate->right_filter) {
        nblex_filter_free(query->data.correlate->left_filter);
        free(query->data.correlate);
        free(query);
        return NULL;
    }
    
    /* Optional "within" clause */
    if (match_keyword(parser, "within")) {
        query->data.correlate->within_ms = parse_duration(parser);
        if (query->data.correlate->within_ms == 0) {
            query->data.correlate->within_ms = 100; /* Default 100ms */
        }
    } else {
        query->data.correlate->within_ms = 100; /* Default 100ms */
    }
    
    return query;
}

/* Parse aggregate query */
static nql_query_t* parse_aggregate(nql_parser_t* parser) {
    if (!match_keyword(parser, "aggregate")) {
        return NULL;
    }
    
    nql_query_t* query = calloc(1, sizeof(nql_query_t));
    if (!query) {
        return NULL;
    }
    
    query->type = NQL_TYPE_AGGREGATE;
    query->data.aggregate = calloc(1, sizeof(nql_aggregate_t));
    if (!query->data.aggregate) {
        free(query);
        return NULL;
    }
    
    /* Parse aggregation functions */
    query->data.aggregate->funcs = NULL;
    query->data.aggregate->funcs_count = 0;
    
    skip_whitespace(parser);
    if (*parser->pos == '(') {
        parser->pos++;
        skip_whitespace(parser);
        
        /* Parse function list */
        while (*parser->pos && *parser->pos != ')') {
            skip_whitespace(parser);
            
            nql_agg_func_t func = {0};
            
            if (match_keyword(parser, "count")) {
                func.func = NQL_AGG_COUNT;
                func.field = NULL;
                func.percentile = 0;
            } else if (match_keyword(parser, "sum")) {
                func.func = NQL_AGG_SUM;
                func.percentile = 0;
                skip_whitespace(parser);
                if (*parser->pos == '(') {
                    parser->pos++;
                    func.field = parse_identifier(parser);
                    skip_whitespace(parser);
                    if (*parser->pos == ')') {
                        parser->pos++;
                    }
                }
            } else if (match_keyword(parser, "avg")) {
                func.func = NQL_AGG_AVG;
                func.percentile = 0;
                skip_whitespace(parser);
                if (*parser->pos == '(') {
                    parser->pos++;
                    func.field = parse_identifier(parser);
                    skip_whitespace(parser);
                    if (*parser->pos == ')') {
                        parser->pos++;
                    }
                }
            } else if (match_keyword(parser, "min")) {
                func.func = NQL_AGG_MIN;
                func.percentile = 0;
                skip_whitespace(parser);
                if (*parser->pos == '(') {
                    parser->pos++;
                    func.field = parse_identifier(parser);
                    skip_whitespace(parser);
                    if (*parser->pos == ')') {
                        parser->pos++;
                    }
                }
            } else if (match_keyword(parser, "max")) {
                func.func = NQL_AGG_MAX;
                func.percentile = 0;
                skip_whitespace(parser);
                if (*parser->pos == '(') {
                    parser->pos++;
                    func.field = parse_identifier(parser);
                    skip_whitespace(parser);
                    if (*parser->pos == ')') {
                        parser->pos++;
                    }
                }
            } else if (match_keyword(parser, "percentile")) {
                func.func = NQL_AGG_PERCENTILE;
                func.percentile = 0;
                skip_whitespace(parser);
                if (*parser->pos == '(') {
                    parser->pos++;
                    func.field = parse_identifier(parser);
                    skip_whitespace(parser);
                    if (*parser->pos == ',') {
                        parser->pos++;
                        skip_whitespace(parser);
                        func.percentile = strtod(parser->pos, (char**)&parser->pos);
                    }
                    skip_whitespace(parser);
                    if (*parser->pos == ')') {
                        parser->pos++;
                    }
                }
            } else {
                break;
            }
            
            /* Add function to list */
            size_t new_size = (query->data.aggregate->funcs_count + 1) * sizeof(nql_agg_func_t);
            nql_agg_func_t* new_funcs = realloc(query->data.aggregate->funcs, new_size);
            if (!new_funcs) {
                /* Cleanup on error */
                for (size_t i = 0; i < query->data.aggregate->funcs_count; i++) {
                    free(query->data.aggregate->funcs[i].field);
                }
                free(query->data.aggregate->funcs);
                free(query->data.aggregate);
                free(query);
                return NULL;
            }
            query->data.aggregate->funcs = new_funcs;
            query->data.aggregate->funcs[query->data.aggregate->funcs_count++] = func;
            
            skip_whitespace(parser);
            if (*parser->pos == ',') {
                parser->pos++;
            } else {
                break;
            }
        }
        
        skip_whitespace(parser);
        if (*parser->pos == ')') {
            parser->pos++;
        }
    }
    
    /* Default to count() if no functions specified */
    if (query->data.aggregate->funcs_count == 0) {
        nql_agg_func_t func = {NQL_AGG_COUNT, NULL, 0.0};
        query->data.aggregate->funcs = malloc(sizeof(nql_agg_func_t));
        if (query->data.aggregate->funcs) {
            query->data.aggregate->funcs[0] = func;
            query->data.aggregate->funcs_count = 1;
        }
    }
    
    /* Optional "by" clause */
    if (match_keyword(parser, "by")) {
        query->data.aggregate->group_by_fields = NULL;
        query->data.aggregate->group_by_count = 0;
        
        while (1) {
            char* field = parse_identifier(parser);
            if (!field) {
                break;
            }
            
            size_t new_size = (query->data.aggregate->group_by_count + 1) * sizeof(char*);
            char** new_fields = realloc(query->data.aggregate->group_by_fields, new_size);
            if (!new_fields) {
                for (size_t i = 0; i < query->data.aggregate->group_by_count; i++) {
                    free(query->data.aggregate->group_by_fields[i]);
                }
                free(query->data.aggregate->group_by_fields);
                free(field);
                break;
            }
            query->data.aggregate->group_by_fields = new_fields;
            query->data.aggregate->group_by_fields[query->data.aggregate->group_by_count++] = field;
            
            skip_whitespace(parser);
            if (*parser->pos == ',') {
                parser->pos++;
            } else {
                break;
            }
        }
    }
    
    /* Optional "where" clause */
    if (match_keyword(parser, "where")) {
        query->data.aggregate->where_filter = parse_filter_expr(parser);
    }
    
    /* Optional "window" clause */
    if (match_keyword(parser, "window")) {
        skip_whitespace(parser);
        if (match_keyword(parser, "tumbling")) {
            query->data.aggregate->window.type = NQL_WINDOW_TUMBLING;
            skip_whitespace(parser);
            if (*parser->pos == '(') {
                parser->pos++;
                query->data.aggregate->window.size_ms = parse_duration(parser);
                skip_whitespace(parser);
                if (*parser->pos == ')') {
                    parser->pos++;
                }
            }
        } else if (match_keyword(parser, "sliding")) {
            query->data.aggregate->window.type = NQL_WINDOW_SLIDING;
            skip_whitespace(parser);
            if (*parser->pos == '(') {
                parser->pos++;
                query->data.aggregate->window.size_ms = parse_duration(parser);
                skip_whitespace(parser);
                if (*parser->pos == ',') {
                    parser->pos++;
                    query->data.aggregate->window.slide_ms = parse_duration(parser);
                }
                skip_whitespace(parser);
                if (*parser->pos == ')') {
                    parser->pos++;
                }
            }
        } else {
            /* Simple duration */
            query->data.aggregate->window.type = NQL_WINDOW_TUMBLING;
            query->data.aggregate->window.size_ms = parse_duration(parser);
        }
    }
    
    return query;
}

/* Parse show query */
static nql_query_t* parse_show(nql_parser_t* parser) {
    if (!match_keyword(parser, "show")) {
        return NULL;
    }
    
    nql_query_t* query = calloc(1, sizeof(nql_query_t));
    if (!query) {
        return NULL;
    }
    
    query->type = NQL_TYPE_SHOW;
    query->data.show = calloc(1, sizeof(nql_show_t));
    if (!query->data.show) {
        free(query);
        return NULL;
    }
    
    query->data.show->fields = NULL;
    query->data.show->fields_count = 0;
    
    /* Parse field list */
    while (1) {
        char* field = parse_identifier(parser);
        if (!field) {
            break;
        }
        
        size_t new_size = (query->data.show->fields_count + 1) * sizeof(char*);
        char** new_fields = realloc(query->data.show->fields, new_size);
        if (!new_fields) {
            for (size_t i = 0; i < query->data.show->fields_count; i++) {
                free(query->data.show->fields[i]);
            }
            free(query->data.show->fields);
            free(field);
            free(query->data.show);
            free(query);
            return NULL;
        }
        query->data.show->fields = new_fields;
        query->data.show->fields[query->data.show->fields_count++] = field;
        
        skip_whitespace(parser);
        if (*parser->pos == ',') {
            parser->pos++;
        } else {
            break;
        }
    }
    
    /* Optional "where" clause */
    if (match_keyword(parser, "where")) {
        query->data.show->where_filter = parse_filter_expr(parser);
    }
    
    return query;
}

/* Parse query (main entry point) */
static nql_query_t* parse_query(nql_parser_t* parser) {
    skip_whitespace(parser);
    
    /* Try correlation first */
    nql_query_t* query = parse_correlate(parser);
    if (query) {
        return query;
    }
    
    /* Try aggregate */
    query = parse_aggregate(parser);
    if (query) {
        return query;
    }
    
    /* Try show */
    query = parse_show(parser);
    if (query) {
        return query;
    }
    
    /* Try simple filter */
    filter_t* filter = parse_filter_expr(parser);
    if (filter) {
        query = calloc(1, sizeof(nql_query_t));
        if (query) {
            query->type = NQL_TYPE_FILTER;
            query->data.filter = filter;
        }
        return query;
    }
    
    return NULL;
}

/* Free query AST */
static void free_query(nql_query_t* query) {
    if (!query) {
        return;
    }
    
    switch (query->type) {
        case NQL_TYPE_FILTER:
            if (query->data.filter) {
                nblex_filter_free(query->data.filter);
            }
            break;
            
        case NQL_TYPE_CORRELATE:
            if (query->data.correlate) {
                if (query->data.correlate->left_filter) {
                    nblex_filter_free(query->data.correlate->left_filter);
                }
                if (query->data.correlate->right_filter) {
                    nblex_filter_free(query->data.correlate->right_filter);
                }
                free(query->data.correlate);
            }
            break;
            
        case NQL_TYPE_AGGREGATE:
            if (query->data.aggregate) {
                for (size_t i = 0; i < query->data.aggregate->funcs_count; i++) {
                    free(query->data.aggregate->funcs[i].field);
                }
                free(query->data.aggregate->funcs);
                
                for (size_t i = 0; i < query->data.aggregate->group_by_count; i++) {
                    free(query->data.aggregate->group_by_fields[i]);
                }
                free(query->data.aggregate->group_by_fields);
                
                if (query->data.aggregate->where_filter) {
                    nblex_filter_free(query->data.aggregate->where_filter);
                }
                free(query->data.aggregate);
            }
            break;
            
        case NQL_TYPE_SHOW:
            if (query->data.show) {
                for (size_t i = 0; i < query->data.show->fields_count; i++) {
                    free(query->data.show->fields[i]);
                }
                free(query->data.show->fields);
                
                if (query->data.show->where_filter) {
                    nblex_filter_free(query->data.show->where_filter);
                }
                free(query->data.show);
            }
            break;
            
        case NQL_TYPE_PIPELINE:
            if (query->data.pipeline.left) {
                free_query(query->data.pipeline.left);
            }
            if (query->data.pipeline.right) {
                free_query(query->data.pipeline.right);
            }
            break;
    }
    
    free(query);
}

/* Public API: Parse nQL query */
nql_query_t* nql_parse(const char* query_str) {
    if (!query_str) {
        return NULL;
    }
    
    nql_parser_t parser = {
        .input = query_str,
        .pos = query_str,
        .error_msg = NULL
    };
    
    nql_query_t* query = parse_query(&parser);
    
    /* Check for pipeline operator */
    skip_whitespace(&parser);
    if (*parser.pos == '|') {
        parser.pos++;
        nql_query_t* right = parse_query(&parser);
        if (right) {
            nql_query_t* pipeline = calloc(1, sizeof(nql_query_t));
            if (pipeline) {
                pipeline->type = NQL_TYPE_PIPELINE;
                pipeline->data.pipeline.left = query;
                pipeline->data.pipeline.right = right;
                query = pipeline;
            } else {
                free_query(query);
                free_query(right);
                return NULL;
            }
        }
    }
    
    return query;
}

/* Public API: Free nQL query */
void nql_free(nql_query_t* query) {
    free_query(query);
}

