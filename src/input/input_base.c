/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * input_base.c - Input base implementation
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Detect log format from file path */
nblex_log_format nblex_detect_log_format(const char* path) {
  if (!path) {
    return NBLEX_FORMAT_JSON;  /* Default */
  }

  /* Check file extension first */
  const char* ext = strrchr(path, '.');
  if (ext) {
    if (strcasecmp(ext, ".jsonl") == 0 || strcasecmp(ext, ".json") == 0) {
      return NBLEX_FORMAT_JSON;
    }
  }

  /* Check path for keywords (case-insensitive) */
  char* path_lower = strdup(path);
  if (path_lower) {
    /* Convert to lowercase for comparison */
    for (char* p = path_lower; *p; p++) {
      if (*p >= 'A' && *p <= 'Z') {
        *p = *p - 'A' + 'a';
      }
    }

    if (strstr(path_lower, "nginx") != NULL) {
      free(path_lower);
      return NBLEX_FORMAT_NGINX;
    }
    if (strstr(path_lower, "syslog") != NULL) {
      free(path_lower);
      return NBLEX_FORMAT_SYSLOG;
    }
    if (strstr(path_lower, "logfmt") != NULL) {
      free(path_lower);
      return NBLEX_FORMAT_LOGFMT;
    }

    free(path_lower);
  }

  /* Default to JSON */
  return NBLEX_FORMAT_JSON;
}

nblex_input* nblex_input_new(nblex_world* world, nblex_input_type type) {
  if (!world) {
    return NULL;
  }

  nblex_input* input = calloc(1, sizeof(nblex_input));
  if (!input) {
    return NULL;
  }

  input->world = world;
  input->type = type;
  input->format = NBLEX_FORMAT_JSON;  /* Default */
  input->vtable = NULL;
  input->data = NULL;
  input->filter = NULL;  /* No filter by default */

  return input;
}

void nblex_input_free(nblex_input* input) {
  if (!input) {
    return;
  }

  /* Free filter if any */
  if (input->filter) {
    nblex_filter_free(input->filter);
    input->filter = NULL;
  }

  /* Call type-specific free */
  if (input->vtable && input->vtable->free) {
    input->vtable->free(input);
  }

  free(input);
}

int nblex_input_set_format(nblex_input* input, nblex_log_format format) {
  if (!input) {
    return -1;
  }

  input->format = format;
  return 0;
}

int nblex_input_set_filter(nblex_input* input, const char* filter_expr) {
  if (!input) {
    return -1;
  }

  if (!filter_expr || strlen(filter_expr) == 0) {
    /* Clear existing filter */
    if (input->filter) {
      nblex_filter_free(input->filter);
      input->filter = NULL;
    }
    return 0;
  }

  /* Parse and store filter expression */
  filter_t* filter = nblex_filter_new(filter_expr);
  if (!filter) {
    return -1;
  }

  /* Free existing filter if any */
  if (input->filter) {
    nblex_filter_free(input->filter);
  }

  input->filter = filter;
  return 0;
}
