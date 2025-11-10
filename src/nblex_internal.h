/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nblex_internal.h - Internal data structures and APIs
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#ifndef NBLEX_INTERNAL_H
#define NBLEX_INTERNAL_H

/* Feature test macros must be defined before any system headers */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
/* _BSD_SOURCE is deprecated in glibc 2.20+, but needed for older versions */
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

/* For pcap compatibility on Linux - enables BSD types in pcap.h */
/* Must be defined before <sys/types.h> is included */
#ifdef __linux__
#ifndef __FAVOR_BSD
#define __FAVOR_BSD
#endif
#endif

/* Include system types before pcap to ensure BSD types are available */
/* Order matters: sys/types.h must come after __FAVOR_BSD definition */
#include <sys/types.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

/* Define BSD types if not available (needed for pcap) */
#ifndef _UCHAR_DEFINED
typedef unsigned char u_char;
#define _UCHAR_DEFINED
#endif
#ifndef _USHORT_DEFINED
typedef unsigned short u_short;
#define _USHORT_DEFINED
#endif
#ifndef _UINT_DEFINED
typedef unsigned int u_int;
#define _UINT_DEFINED
#endif

#include "nblex/nblex.h"

/* Ensure pthread types are available for uv.h */
#include <pthread.h>

#include <uv.h>
#include <jansson.h>
#include <pcap.h>

/* Forward declarations */
typedef struct nblex_input_vtable_s nblex_input_vtable;
typedef struct filter_s filter_t;
typedef struct filter_node filter_node_t;

/*
 * World structure - main context
 */
struct nblex_world_s {
  bool opened;
  bool started;
  bool running;

  /* Event loop */
  uv_loop_t* loop;

  /* Inputs list */
  nblex_input** inputs;
  size_t inputs_count;
  size_t inputs_capacity;

  /* Event handler */
  nblex_event_handler event_handler;
  void* event_handler_data;

  /* Correlation engine */
  nblex_correlation* correlation;

  /* Statistics */
  uint64_t events_processed;
  uint64_t events_correlated;
};

/*
 * Input base structure
 */
struct nblex_input_s {
  nblex_world* world;
  nblex_input_type type;
  nblex_log_format format;

  /* Virtual table for input-specific operations */
  const nblex_input_vtable* vtable;

  /* Input-specific data */
  void* data;

  /* Filter for this input */
  filter_t* filter;
};

/*
 * Input virtual table
 */
struct nblex_input_vtable_s {
  const char* name;

  /* Start input */
  int (*start)(nblex_input* input);

  /* Stop input */
  int (*stop)(nblex_input* input);

  /* Free input-specific data */
  void (*free)(nblex_input* input);
};

/*
 * Event structure
 */
struct nblex_event_s {
  nblex_event_type type;
  uint64_t timestamp_ns;  /* Nanosecond timestamp */

  /* Source information */
  nblex_input* input;

  /* Event data (JSON object) */
  json_t* data;
};

/*
 * Input configuration
 */
typedef struct nblex_input_config_s nblex_input_config_t;
struct nblex_input_config_s {
    char* name;
    char* type;
    char* path;           /* for file inputs */
    char* interface;      /* for network inputs */
    char* filter;         /* pcap filter */
    char* format;         /* log format */
};

/*
 * Output configuration
 */
typedef struct nblex_output_config_s nblex_output_config_t;
struct nblex_output_config_s {
    char* name;
    char* type;
    char* path;           /* for file outputs */
    char* url;            /* for HTTP outputs */
    char* format;         /* output format */
};

/*
 * File input data
 */
typedef struct {
  char* path;
  char* dir_path;      /* Directory path for watching */
  char* filename;      /* Filename for filtering events */
  FILE* file;
  uv_fs_event_t fs_event;
  uv_timer_t poll_timer;
  bool watching;
  bool use_fs_event;   /* Whether to use fs_event or fallback to polling */

  /* Line buffer */
  char* line_buffer;
  size_t line_buffer_size;
  size_t line_buffer_capacity;
} nblex_file_input_data;

/*
 * Pcap input data
 */
typedef struct {
  char* interface;
  pcap_t* pcap_handle;
  uv_poll_t poll_handle;
  int pcap_fd;
  int datalink;
  bool capturing;

  /* Statistics */
  uint64_t packets_captured;
  uint64_t packets_dropped;
} nblex_pcap_input_data;

/*
 * Correlation event buffer entry
 */
typedef struct nblex_event_buffer_entry_s {
  nblex_event* event;
  struct nblex_event_buffer_entry_s* next;
} nblex_event_buffer_entry;

/*
 * Correlation structure
 */
struct nblex_correlation_s {
  nblex_world* world;

  /* Correlation strategies */
  nblex_correlation_type type;
  uint64_t window_ns;  /* Time window in nanoseconds */

  /* Event buffers for time-based correlation */
  nblex_event_buffer_entry* log_events;
  nblex_event_buffer_entry* network_events;
  size_t log_events_count;
  size_t network_events_count;

  /* Timer for periodic cleanup */
  uv_timer_t cleanup_timer;
  int timer_initialized;  /* Track if timer was initialized */

  /* Statistics */
  uint64_t correlations_found;
};

/*
 * Internal utility functions
 */

/* Memory */
void* nblex_malloc(size_t size);
void* nblex_calloc(size_t nmemb, size_t size);
void* nblex_realloc(void* ptr, size_t size);
void nblex_free(void* ptr);
char* nblex_strdup(const char* s);

/* Buffer utilities */
typedef struct nblex_buffer_s nblex_buffer;
nblex_buffer* nblex_buffer_new(size_t initial_capacity);
void nblex_buffer_free(nblex_buffer* buf);

/* Events */
nblex_event* nblex_event_new(nblex_event_type type, nblex_input* input);
void nblex_event_free(nblex_event* event);
void nblex_event_emit(nblex_world* world, nblex_event* event);

/* Timestamp */
static inline uint64_t nblex_timestamp_now(void) {
  return uv_hrtime();
}

/* Inputs */
nblex_input* nblex_input_new(nblex_world* world, nblex_input_type type);
void nblex_input_free(nblex_input* input);
int nblex_world_add_input(nblex_world* world, nblex_input* input);

/* JSON parsing */
json_t* nblex_parse_json_line(const char* line);

/* HTTP parsing */
json_t* nblex_parse_http(const u_char* payload, size_t payload_len);
json_t* nblex_parse_http_request(const u_char* payload, size_t payload_len);
json_t* nblex_parse_http_response(const u_char* payload, size_t payload_len);
json_t* nblex_parse_http_payload(const char* data, size_t data_len, int is_request);

/* DNS parsing */
json_t* nblex_parse_dns_payload(const u_char* data, size_t data_len);

/* Log parsing */
json_t* nblex_parse_logfmt_line(const char* line);
json_t* nblex_parse_syslog_line(const char* line);

/* Regex parser */
struct regex_parser_s;
typedef struct regex_parser_s regex_parser_t;
regex_parser_t* nblex_regex_parser_new(const char* pattern, const char** field_names, int field_count);
void nblex_regex_parser_free(regex_parser_t* parser);
json_t* nblex_regex_parser_parse(regex_parser_t* parser, const char* line);

/* Correlation */
int nblex_correlation_start(nblex_correlation* corr);
void nblex_correlation_process_event(nblex_correlation* corr, nblex_event* event);

/* JSON output */
char* nblex_event_to_json_string(nblex_event* event);

/* Filter engine */
filter_t* nblex_filter_new(const char* expression);
void nblex_filter_free(filter_t* filter);
int nblex_filter_matches(const filter_t* filter, const nblex_event* event);
filter_node_t* parse_filter_full(const char* expr);

/* nQL parser */
typedef struct nql_query_s nql_query_t;
nql_query_t* nql_parse(const char* query_str);
nql_query_t* nql_parse_ex(const char* query_str, char** error_out);
void nql_free(nql_query_t* query);

/* nQL executor */
int nql_execute(const char* query_str, nblex_event* event, nblex_world* world);

/* Configuration */
typedef struct nblex_config_s nblex_config_t;
nblex_config_t* nblex_config_load_yaml(const char* filename);
void nblex_config_free(nblex_config_t* config);
int nblex_config_apply(nblex_config_t* config, nblex_world* world);
const char* nblex_config_get_string(nblex_config_t* config, const char* key);
int nblex_config_get_int(nblex_config_t* config, const char* key, int default_value);
size_t nblex_config_get_size(nblex_config_t* config, const char* key, size_t default_value);

/* Output types - forward declarations */
typedef struct file_output_s file_output_t;
typedef struct http_output_s http_output_t;
typedef struct metrics_output_s metrics_output_t;

file_output_t* nblex_file_output_new(const char* path, const char* format);
void nblex_file_output_free(file_output_t* output);
int nblex_file_output_write(file_output_t* output, nblex_event* event);
void nblex_file_output_set_rotation(file_output_t* output, int max_size_mb, int max_age_days, int max_count);

http_output_t* nblex_http_output_new(const char* url);
void nblex_http_output_free(http_output_t* output);
int nblex_http_output_write(http_output_t* output, nblex_event* event);
void nblex_http_output_set_method(http_output_t* output, const char* method);
void nblex_http_output_set_timeout(http_output_t* output, int timeout_seconds);

metrics_output_t* nblex_metrics_output_new(const char* path, const char* format);
void nblex_metrics_output_free(metrics_output_t* output);
int nblex_metrics_output_write(metrics_output_t* output, nblex_event* event);
int nblex_metrics_output_flush(metrics_output_t* output);
void nblex_metrics_output_set_flush_interval(metrics_output_t* output, int interval_seconds);

#endif /* NBLEX_INTERNAL_H */
