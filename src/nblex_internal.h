/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nblex_internal.h - Internal data structures and APIs
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#ifndef NBLEX_INTERNAL_H
#define NBLEX_INTERNAL_H

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "nblex/nblex.h"
#include <pthread.h>
#include <uv.h>
#include <jansson.h>
#include <pcap.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Forward declarations */
typedef struct nblex_input_vtable_s nblex_input_vtable;

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
  FILE* file;
  uv_fs_event_t fs_event;
  uv_timer_t poll_timer;
  bool watching;

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

/* Events */
nblex_event* nblex_event_new(nblex_event_type type, nblex_input* input);
void nblex_event_free(nblex_event* event);
void nblex_event_emit(nblex_world* world, nblex_event* event);

/* Timestamp */
static inline uint64_t nblex_timestamp_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
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

/* Correlation */
int nblex_correlation_start(nblex_correlation* corr);
void nblex_correlation_process_event(nblex_correlation* corr, nblex_event* event);

/* JSON output */
char* nblex_event_to_json_string(nblex_event* event);

#endif /* NBLEX_INTERNAL_H */
