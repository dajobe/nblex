/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nql_parser.h - nQL (nblex Query Language) AST definitions
 *
 * Copyright (C)
 * Licensed under the Apache License, Version 2.0
 */

#ifndef NBLEX_NQL_PARSER_H
#define NBLEX_NQL_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration from filter engine */
typedef struct filter_s filter_t;

typedef enum {
  NQL_QUERY_FILTER,
  NQL_QUERY_CORRELATE,
  NQL_QUERY_AGGREGATE,
  NQL_QUERY_SHOW,
  NQL_QUERY_PIPELINE
} nql_query_type_t;

typedef enum {
  NQL_AGG_COUNT,
  NQL_AGG_SUM,
  NQL_AGG_AVG,
  NQL_AGG_MIN,
  NQL_AGG_MAX,
  NQL_AGG_PERCENTILE,
  NQL_AGG_DISTINCT
} nql_agg_func_type_t;

typedef enum {
  NQL_WINDOW_NONE,
  NQL_WINDOW_TUMBLING,
  NQL_WINDOW_SLIDING,
  NQL_WINDOW_SESSION
} nql_window_type_t;

typedef struct {
  nql_agg_func_type_t type;
  char* field;       /* NULL for count() */
  double percentile; /* Only used for percentile() */
} nql_agg_func_t;

typedef struct {
  nql_window_type_t type;
  uint64_t size_ms;       /* For tumbling/sliding windows */
  uint64_t slide_ms;      /* For sliding windows */
  uint64_t timeout_ms;    /* For session windows - gap timeout */
} nql_window_t;

typedef struct {
  nql_agg_func_t* funcs;
  size_t funcs_count;
  char** group_by_fields;
  size_t group_by_count;
  filter_t* where_filter;
  nql_window_t window;
} nql_aggregate_t;

typedef struct {
  filter_t* left_filter;
  filter_t* right_filter;
  uint64_t within_ms;
} nql_correlate_t;

typedef struct {
  char** fields;
  size_t fields_count;
  bool select_all;
  filter_t* where_filter;
} nql_show_t;

typedef struct nql_query_s nql_query_t;

typedef struct {
  nql_query_t** stages;
  size_t count;
} nql_pipeline_t;

struct nql_query_s {
  nql_query_type_t type;
  union {
    filter_t* filter;
    nql_correlate_t* correlate;
    nql_aggregate_t* aggregate;
    nql_show_t* show;
    nql_pipeline_t pipeline;
  } data;
};

nql_query_t* nql_parse(const char* query_str);
nql_query_t* nql_parse_ex(const char* query_str, char** error_out);
void nql_free(nql_query_t* query);

#endif /* NBLEX_NQL_PARSER_H */

