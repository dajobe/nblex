/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nql_parser.c - nQL (nblex Query Language) parser
 *
 * Copyright (C)
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include "nql_parser.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_CORRELATE_WITHIN_MS 100

typedef struct {
  const char* input;
  const char* pos;
  char* error_msg;
} nql_parser_t;

static void parser_set_error(nql_parser_t* parser, const char* fmt, ...) {
  if (parser->error_msg) {
    return;
  }

  va_list ap;
  va_start(ap, fmt);

  char buffer[256];
  vsnprintf(buffer, sizeof(buffer), fmt, ap);
  va_end(ap);

  parser->error_msg = nblex_strdup(buffer);
}

static void skip_whitespace(nql_parser_t* parser) {
  while (*parser->pos && isspace((unsigned char)*parser->pos)) {
    parser->pos++;
  }
}

static bool consume_char(nql_parser_t* parser, char expected) {
  skip_whitespace(parser);
  if (*parser->pos == expected) {
    parser->pos++;
    return true;
  }
  return false;
}

static bool match_keyword(nql_parser_t* parser, const char* keyword) {
  skip_whitespace(parser);
  size_t len = strlen(keyword);
  if (strncasecmp(parser->pos, keyword, len) != 0) {
    return false;
  }

  char next = parser->pos[len];
  if (next && !(isspace((unsigned char)next) || next == '(' || next == ')' || next == ',')) {
    return false;
  }

  parser->pos += len;
  return true;
}

static bool keyword_at_position(nql_parser_t* parser,
                                const char* position,
                                const char* keyword) {
  size_t len = strlen(keyword);
  if (strncasecmp(position, keyword, len) != 0) {
    return false;
  }

  char prev = (position == parser->input) ? '\0' : *(position - 1);
  if (prev && !(isspace((unsigned char)prev) || prev == '(' || prev == ',')) {
    return false;
  }

  char next = position[len];
  if (next && !(isspace((unsigned char)next) || next == '(' || next == ')' || next == ',')) {
    return false;
  }

  return true;
}

static char* parse_identifier(nql_parser_t* parser) {
  skip_whitespace(parser);

  const char* start = parser->pos;
  if (!isalpha((unsigned char)*start) && *start != '_') {
    return NULL;
  }

  while (*parser->pos &&
         (isalnum((unsigned char)*parser->pos) || *parser->pos == '_' || *parser->pos == '.')) {
    parser->pos++;
  }

  size_t len = (size_t)(parser->pos - start);
  char* result = malloc(len + 1);
  if (!result) {
    parser_set_error(parser, "out of memory");
    return NULL;
  }

  memcpy(result, start, len);
  result[len] = '\0';
  return result;
}

static uint64_t parse_duration(nql_parser_t* parser) {
  skip_whitespace(parser);
  char* endptr = NULL;
  unsigned long long value = strtoull(parser->pos, &endptr, 10);
  if (endptr == parser->pos) {
    parser_set_error(parser, "expected duration value");
    return 0;
  }

  parser->pos = endptr;
  skip_whitespace(parser);

  uint64_t multiplier = 0;

  if (parser->pos[0] == 'm' && parser->pos[1] == 's') {
    multiplier = 1;
    parser->pos += 2;
  } else if (parser->pos[0] == 's') {
    multiplier = 1000;
    parser->pos += 1;
  } else if (parser->pos[0] == 'm') {
    multiplier = 60 * 1000;
    parser->pos += 1;
  } else if (parser->pos[0] == 'h') {
    multiplier = 60 * 60 * 1000;
    parser->pos += 1;
  } else {
    parser_set_error(parser, "expected duration unit (ms, s, m, h)");
    return 0;
  }

  return value * multiplier;
}

static filter_t* parse_filter_expr(nql_parser_t* parser,
                                   const char** keywords,
                                   size_t keyword_count,
                                   bool stop_on_pipe) {
  skip_whitespace(parser);
  const char* start = parser->pos;
  const char* current = parser->pos;

  int paren_depth = 0;
  bool in_single = false;
  bool in_double = false;

  while (*current) {
    char ch = *current;

    if (in_single) {
      if (ch == '\\' && current[1]) {
        current += 2;
        continue;
      }
      if (ch == '\'') {
        in_single = false;
      }
      current++;
      continue;
    }

    if (in_double) {
      if (ch == '\\' && current[1]) {
        current += 2;
        continue;
      }
      if (ch == '"') {
        in_double = false;
      }
      current++;
      continue;
    }

    if (ch == '\'') {
      in_single = true;
      current++;
      continue;
    }

    if (ch == '"') {
      in_double = true;
      current++;
      continue;
    }

    if (ch == '(') {
      paren_depth++;
      current++;
      continue;
    }

    if (ch == ')') {
      if (paren_depth > 0) {
        paren_depth--;
      }
      current++;
      continue;
    }

    if (stop_on_pipe && ch == '|' && paren_depth == 0) {
      break;
    }

    if (paren_depth == 0 && keyword_count > 0) {
      for (size_t i = 0; i < keyword_count; i++) {
        if (keyword_at_position(parser, current, keywords[i])) {
          goto done;
        }
      }
    }

    current++;
  }

done:
  const char* end = current;
  while (end > start && isspace((unsigned char)*(end - 1))) {
    end--;
  }

  if (end == start) {
    parser_set_error(parser, "expected filter expression");
    return NULL;
  }

  size_t len = (size_t)(end - start);
  char* buffer = malloc(len + 1);
  if (!buffer) {
    parser_set_error(parser, "out of memory");
    return NULL;
  }

  memcpy(buffer, start, len);
  buffer[len] = '\0';

  filter_t* filter = nblex_filter_new(buffer);
  free(buffer);

  if (!filter) {
    parser_set_error(parser, "invalid filter expression");
    return NULL;
  }

  parser->pos = current;
  return filter;
}

static int add_agg_function(nql_parser_t* parser,
                            nql_aggregate_t* aggregate,
                            const nql_agg_func_t* func) {
  size_t new_count = aggregate->funcs_count + 1;
  nql_agg_func_t* new_funcs =
      realloc(aggregate->funcs, new_count * sizeof(nql_agg_func_t));
  if (!new_funcs) {
    parser_set_error(parser, "out of memory");
    return -1;
  }

  new_funcs[aggregate->funcs_count] = *func;
  aggregate->funcs = new_funcs;
  aggregate->funcs_count = new_count;
  return 0;
}

static int add_group_by_field(nql_parser_t* parser,
                              nql_aggregate_t* aggregate,
                              char* field) {
  size_t new_count = aggregate->group_by_count + 1;
  char** new_fields =
      realloc(aggregate->group_by_fields, new_count * sizeof(char*));
  if (!new_fields) {
    parser_set_error(parser, "out of memory");
    free(field);
    return -1;
  }

  new_fields[aggregate->group_by_count] = field;
  aggregate->group_by_fields = new_fields;
  aggregate->group_by_count = new_count;
  return 0;
}

static int add_show_field(nql_parser_t* parser, nql_show_t* show, char* field) {
  size_t new_count = show->fields_count + 1;
  char** new_fields = realloc(show->fields, new_count * sizeof(char*));
  if (!new_fields) {
    parser_set_error(parser, "out of memory");
    free(field);
    return -1;
  }

  new_fields[show->fields_count] = field;
  show->fields = new_fields;
  show->fields_count = new_count;
  return 0;
}

static int parse_agg_function(nql_parser_t* parser, nql_aggregate_t* aggregate) {
  skip_whitespace(parser);

  const char* start = parser->pos;
  while (isalpha((unsigned char)*parser->pos)) {
    parser->pos++;
  }

  if (parser->pos == start) {
    parser_set_error(parser, "expected aggregation function");
    return -1;
  }

  size_t len = (size_t)(parser->pos - start);
  char func_name[32];
  if (len >= sizeof(func_name)) {
    parser_set_error(parser, "aggregation function name too long");
    return -1;
  }
  for (size_t i = 0; i < len; i++) {
    func_name[i] = (char)tolower((unsigned char)start[i]);
  }
  func_name[len] = '\0';

  if (!consume_char(parser, '(')) {
    parser_set_error(parser, "expected '(' after aggregation function");
    return -1;
  }

  nql_agg_func_t func = {0};

  if (strcmp(func_name, "count") == 0) {
    func.type = NQL_AGG_COUNT;
    skip_whitespace(parser);
    if (!consume_char(parser, ')')) {
      parser_set_error(parser, "expected ')' after count()");
      goto error;
    }
  } else if (strcmp(func_name, "percentile") == 0) {
    func.type = NQL_AGG_PERCENTILE;
    func.field = parse_identifier(parser);
    if (!func.field) {
      parser_set_error(parser, "expected field name in percentile()");
      goto error;
    }

    if (!consume_char(parser, ',')) {
      parser_set_error(parser, "expected ',' in percentile()");
      goto error;
    }

    skip_whitespace(parser);
    char* endptr = NULL;
    func.percentile = strtod(parser->pos, &endptr);
    if (endptr == parser->pos) {
      parser_set_error(parser, "expected percentile value");
      goto error;
    }
    parser->pos = endptr;

    skip_whitespace(parser);
    if (!consume_char(parser, ')')) {
      parser_set_error(parser, "expected ')' after percentile()");
      goto error;
    }
  } else {
    if (strcmp(func_name, "sum") == 0) {
      func.type = NQL_AGG_SUM;
    } else if (strcmp(func_name, "avg") == 0) {
      func.type = NQL_AGG_AVG;
    } else if (strcmp(func_name, "min") == 0) {
      func.type = NQL_AGG_MIN;
    } else if (strcmp(func_name, "max") == 0) {
      func.type = NQL_AGG_MAX;
    } else if (strcmp(func_name, "distinct") == 0) {
      func.type = NQL_AGG_DISTINCT;
    } else {
      parser_set_error(parser, "unknown aggregation function '%s'", func_name);
      goto error;
    }

    func.field = parse_identifier(parser);
    if (!func.field) {
      parser_set_error(parser, "expected field name for aggregation");
      goto error;
    }

    skip_whitespace(parser);
    if (!consume_char(parser, ')')) {
      parser_set_error(parser, "expected ')' after aggregation");
      goto error;
    }
  }

  return add_agg_function(parser, aggregate, &func);

error:
  free(func.field);
  return -1;
}

static nql_query_t* allocate_query(nql_query_type_t type) {
  nql_query_t* query = calloc(1, sizeof(nql_query_t));
  if (query) {
    query->type = type;
  }
  return query;
}

static nql_query_t* parse_correlate(nql_parser_t* parser) {
  const char* saved = parser->pos;
  if (!match_keyword(parser, "correlate")) {
    parser->pos = saved;
    return NULL;
  }

  nql_query_t* query = allocate_query(NQL_QUERY_CORRELATE);
  if (!query) {
    parser_set_error(parser, "out of memory");
    return NULL;
  }

  query->data.correlate = calloc(1, sizeof(nql_correlate_t));
  if (!query->data.correlate) {
    free(query);
    parser_set_error(parser, "out of memory");
    return NULL;
  }

  const char* left_keywords[] = {"with", "within", "|"};
  query->data.correlate->left_filter =
      parse_filter_expr(parser, left_keywords, 3, true);
  if (!query->data.correlate->left_filter) {
    nql_free(query);
    return NULL;
  }

  if (!match_keyword(parser, "with")) {
    parser_set_error(parser, "expected 'with' in correlate clause");
    nql_free(query);
    return NULL;
  }

  const char* right_keywords[] = {"within", "|"};
  query->data.correlate->right_filter =
      parse_filter_expr(parser, right_keywords, 2, true);
  if (!query->data.correlate->right_filter) {
    nql_free(query);
    return NULL;
  }

  if (match_keyword(parser, "within")) {
    uint64_t within = parse_duration(parser);
    if (parser->error_msg) {
      nql_free(query);
      return NULL;
    }
    query->data.correlate->within_ms =
        within ? within : DEFAULT_CORRELATE_WITHIN_MS;
  } else {
    query->data.correlate->within_ms = DEFAULT_CORRELATE_WITHIN_MS;
  }

  return query;
}

static nql_query_t* parse_aggregate(nql_parser_t* parser) {
  const char* saved = parser->pos;
  if (!match_keyword(parser, "aggregate")) {
    parser->pos = saved;
    return NULL;
  }

  nql_query_t* query = allocate_query(NQL_QUERY_AGGREGATE);
  if (!query) {
    parser_set_error(parser, "out of memory");
    return NULL;
  }

  query->data.aggregate = calloc(1, sizeof(nql_aggregate_t));
  if (!query->data.aggregate) {
    free(query);
    parser_set_error(parser, "out of memory");
    return NULL;
  }
  query->data.aggregate->window.type = NQL_WINDOW_NONE;

  bool wrapped = false;
  if (consume_char(parser, '(')) {
    wrapped = true;
  }

  if (parse_agg_function(parser, query->data.aggregate) != 0) {
    nql_free(query);
    return NULL;
  }

  while (consume_char(parser, ',')) {
    if (parse_agg_function(parser, query->data.aggregate) != 0) {
      nql_free(query);
      return NULL;
    }
  }

  if (wrapped) {
    if (!consume_char(parser, ')')) {
      parser_set_error(parser, "expected ')' after aggregation function list");
      nql_free(query);
      return NULL;
    }
  }

  if (match_keyword(parser, "by")) {
    do {
      char* field = parse_identifier(parser);
      if (!field) {
        parser_set_error(parser, "expected field after 'by'");
        nql_free(query);
        return NULL;
      }
      if (add_group_by_field(parser, query->data.aggregate, field) != 0) {
        nql_free(query);
        return NULL;
      }
    } while (consume_char(parser, ','));
  }

  if (match_keyword(parser, "where")) {
    const char* where_keywords[] = {"window", "|"};
    query->data.aggregate->where_filter =
        parse_filter_expr(parser, where_keywords, 2, true);
    if (!query->data.aggregate->where_filter) {
      nql_free(query);
      return NULL;
    }
  }

  if (match_keyword(parser, "window")) {
    nql_window_t* window = &query->data.aggregate->window;
    if (match_keyword(parser, "tumbling")) {
      window->type = NQL_WINDOW_TUMBLING;
      if (!consume_char(parser, '(')) {
        parser_set_error(parser, "expected '(' after tumbling");
        nql_free(query);
        return NULL;
      }
      window->size_ms = parse_duration(parser);
      if (parser->error_msg) {
        nql_free(query);
        return NULL;
      }
      if (!consume_char(parser, ')')) {
        parser_set_error(parser, "expected ')' after tumbling window");
        nql_free(query);
        return NULL;
      }
    } else if (match_keyword(parser, "sliding")) {
      window->type = NQL_WINDOW_SLIDING;
      if (!consume_char(parser, '(')) {
        parser_set_error(parser, "expected '(' after sliding");
        nql_free(query);
        return NULL;
      }
      window->size_ms = parse_duration(parser);
      if (parser->error_msg) {
        nql_free(query);
        return NULL;
      }
      if (!consume_char(parser, ',')) {
        parser_set_error(parser, "expected ',' in sliding window");
        nql_free(query);
        return NULL;
      }
      window->slide_ms = parse_duration(parser);
      if (parser->error_msg) {
        nql_free(query);
        return NULL;
      }
      if (!consume_char(parser, ')')) {
        parser_set_error(parser, "expected ')' after sliding window");
        nql_free(query);
        return NULL;
      }
    } else {
      window->type = NQL_WINDOW_TUMBLING;
      window->size_ms = parse_duration(parser);
      if (parser->error_msg) {
        nql_free(query);
        return NULL;
      }
    }
  }

  return query;
}

static nql_query_t* parse_show(nql_parser_t* parser) {
  const char* saved = parser->pos;
  if (!match_keyword(parser, "show")) {
    parser->pos = saved;
    return NULL;
  }

  nql_query_t* query = allocate_query(NQL_QUERY_SHOW);
  if (!query) {
    parser_set_error(parser, "out of memory");
    return NULL;
  }

  query->data.show = calloc(1, sizeof(nql_show_t));
  if (!query->data.show) {
    free(query);
    parser_set_error(parser, "out of memory");
    return NULL;
  }

  if (consume_char(parser, '*')) {
    query->data.show->select_all = true;
  } else {
    char* field = parse_identifier(parser);
    if (!field) {
      parser_set_error(parser, "expected field list for show");
      nql_free(query);
      return NULL;
    }
    if (add_show_field(parser, query->data.show, field) != 0) {
      nql_free(query);
      return NULL;
    }

    while (consume_char(parser, ',')) {
      field = parse_identifier(parser);
      if (!field) {
        parser_set_error(parser, "expected field in show list");
        nql_free(query);
        return NULL;
      }
      if (add_show_field(parser, query->data.show, field) != 0) {
        nql_free(query);
        return NULL;
      }
    }
  }

  if (match_keyword(parser, "where")) {
    const char* where_keywords[] = {"|"};
    query->data.show->where_filter =
        parse_filter_expr(parser, where_keywords, 1, true);
    if (!query->data.show->where_filter) {
      nql_free(query);
      return NULL;
    }
  }

  return query;
}

static nql_query_t* parse_select_all(nql_parser_t* parser) {
  const char* saved = parser->pos;
  if (!consume_char(parser, '*')) {
    parser->pos = saved;
    return NULL;
  }

  nql_query_t* query = allocate_query(NQL_QUERY_SHOW);
  if (!query) {
    parser_set_error(parser, "out of memory");
    return NULL;
  }

  query->data.show = calloc(1, sizeof(nql_show_t));
  if (!query->data.show) {
    free(query);
    parser_set_error(parser, "out of memory");
    return NULL;
  }

  query->data.show->select_all = true;

  if (match_keyword(parser, "where")) {
    const char* where_keywords[] = {"|"};
    query->data.show->where_filter =
        parse_filter_expr(parser, where_keywords, 1, true);
    if (!query->data.show->where_filter) {
      nql_free(query);
      return NULL;
    }
  }

  return query;
}

static nql_query_t* parse_filter_query(nql_parser_t* parser) {
  nql_query_t* query = allocate_query(NQL_QUERY_FILTER);
  if (!query) {
    parser_set_error(parser, "out of memory");
    return NULL;
  }

  const char* stop_keywords[] = {"|"};
  query->data.filter = parse_filter_expr(parser, stop_keywords, 1, true);
  if (!query->data.filter) {
    nql_free(query);
    return NULL;
  }

  return query;
}

static int append_pipeline_stage(nql_parser_t* parser,
                                 nql_pipeline_t* pipeline,
                                 nql_query_t* stage) {
  size_t new_count = pipeline->count + 1;
  nql_query_t** new_stages =
      realloc(pipeline->stages, new_count * sizeof(nql_query_t*));
  if (!new_stages) {
    parser_set_error(parser, "out of memory");
    return -1;
  }

  new_stages[pipeline->count] = stage;
  pipeline->stages = new_stages;
  pipeline->count = new_count;
  return 0;
}

static nql_query_t* parse_single_query(nql_parser_t* parser) {
  const char* saved = parser->pos;

  nql_query_t* query = parse_correlate(parser);
  if (query || parser->error_msg) {
    return query;
  }
  parser->pos = saved;

  query = parse_aggregate(parser);
  if (query || parser->error_msg) {
    return query;
  }
  parser->pos = saved;

  query = parse_show(parser);
  if (query || parser->error_msg) {
    return query;
  }
  parser->pos = saved;

  query = parse_select_all(parser);
  if (query || parser->error_msg) {
    return query;
  }
  parser->pos = saved;

  query = parse_filter_query(parser);
  return query;
}

static nql_query_t* parse_pipeline(nql_parser_t* parser) {
  nql_query_t* first = parse_single_query(parser);
  if (!first) {
    return NULL;
  }

  nql_pipeline_t pipeline = {0};
  if (append_pipeline_stage(parser, &pipeline, first) != 0) {
    nql_free(first);
    return NULL;
  }

  while (true) {
    skip_whitespace(parser);
    if (*parser->pos != '|') {
      break;
    }
    parser->pos++; /* consume '|' */

    nql_query_t* stage = parse_single_query(parser);
    if (!stage) {
      for (size_t i = 0; i < pipeline.count; i++) {
        nql_free(pipeline.stages[i]);
      }
      free(pipeline.stages);
      return NULL;
    }

    if (append_pipeline_stage(parser, &pipeline, stage) != 0) {
      for (size_t i = 0; i < pipeline.count; i++) {
        nql_free(pipeline.stages[i]);
      }
      free(pipeline.stages);
      nql_free(stage);
      return NULL;
    }
  }

  if (pipeline.count == 1) {
    free(pipeline.stages);
    return first;
  }

  nql_query_t* query = allocate_query(NQL_QUERY_PIPELINE);
  if (!query) {
    parser_set_error(parser, "out of memory");
    for (size_t i = 0; i < pipeline.count; i++) {
      nql_free(pipeline.stages[i]);
    }
    free(pipeline.stages);
    return NULL;
  }

  query->data.pipeline = pipeline;
  return query;
}

static nql_query_t* parse_entry(nql_parser_t* parser) {
  skip_whitespace(parser);
  if (!*parser->pos) {
    parser_set_error(parser, "empty query");
    return NULL;
  }
  return parse_pipeline(parser);
}

static void free_aggregate(nql_aggregate_t* aggregate) {
  if (!aggregate) {
    return;
  }

  for (size_t i = 0; i < aggregate->funcs_count; i++) {
    free(aggregate->funcs[i].field);
  }
  free(aggregate->funcs);

  for (size_t i = 0; i < aggregate->group_by_count; i++) {
    free(aggregate->group_by_fields[i]);
  }
  free(aggregate->group_by_fields);

  if (aggregate->where_filter) {
    nblex_filter_free(aggregate->where_filter);
  }

  free(aggregate);
}

static void free_correlate(nql_correlate_t* correlate) {
  if (!correlate) {
    return;
  }

  if (correlate->left_filter) {
    nblex_filter_free(correlate->left_filter);
  }
  if (correlate->right_filter) {
    nblex_filter_free(correlate->right_filter);
  }
  free(correlate);
}

static void free_show(nql_show_t* show) {
  if (!show) {
    return;
  }

  for (size_t i = 0; i < show->fields_count; i++) {
    free(show->fields[i]);
  }
  free(show->fields);

  if (show->where_filter) {
    nblex_filter_free(show->where_filter);
  }

  free(show);
}

void nql_free(nql_query_t* query) {
  if (!query) {
    return;
  }

  switch (query->type) {
    case NQL_QUERY_FILTER:
      if (query->data.filter) {
        nblex_filter_free(query->data.filter);
      }
      break;
    case NQL_QUERY_CORRELATE:
      free_correlate(query->data.correlate);
      break;
    case NQL_QUERY_AGGREGATE:
      free_aggregate(query->data.aggregate);
      break;
    case NQL_QUERY_SHOW:
      free_show(query->data.show);
      break;
    case NQL_QUERY_PIPELINE:
      if (query->data.pipeline.stages) {
        for (size_t i = 0; i < query->data.pipeline.count; i++) {
          nql_free(query->data.pipeline.stages[i]);
        }
        free(query->data.pipeline.stages);
      }
      break;
  }

  free(query);
}

nql_query_t* nql_parse_ex(const char* query_str, char** error_out) {
  if (!query_str) {
    if (error_out) {
      *error_out = nblex_strdup("query string is NULL");
    }
    return NULL;
  }

  nql_parser_t parser = {
      .input = query_str,
      .pos = query_str,
      .error_msg = NULL,
  };

  nql_query_t* query = parse_entry(&parser);

  if (query && !parser.error_msg) {
    skip_whitespace(&parser);
    if (*parser.pos) {
      parser_set_error(&parser, "unexpected trailing input");
      nql_free(query);
      query = NULL;
    }
  }

  if (error_out) {
    *error_out = parser.error_msg;
  } else if (parser.error_msg) {
    free(parser.error_msg);
  }

  return query;
}

nql_query_t* nql_parse(const char* query_str) {
  return nql_parse_ex(query_str, NULL);
}

