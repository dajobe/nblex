/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * buffer.c - Buffer management utilities
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* TODO: Implement ring buffer for event streaming */

struct nblex_buffer_s {
  void* data;
  size_t size;
  size_t capacity;
};

nblex_buffer* nblex_buffer_new(size_t initial_capacity) {
  nblex_buffer* buf = calloc(1, sizeof(nblex_buffer));
  if (!buf) {
    return NULL;
  }

  buf->data = malloc(initial_capacity);
  if (!buf->data) {
    free(buf);
    return NULL;
  }

  buf->capacity = initial_capacity;
  buf->size = 0;

  return buf;
}

void nblex_buffer_free(nblex_buffer* buf) {
  if (!buf) {
    return;
  }

  free(buf->data);
  free(buf);
}

/* TODO: Implement buffer operations */
