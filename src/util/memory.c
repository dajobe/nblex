/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * memory.c - Memory management utilities
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>

/* Memory allocation wrappers for future instrumentation */

void* nblex_malloc(size_t size) {
  return malloc(size);
}

void* nblex_calloc(size_t nmemb, size_t size) {
  return calloc(nmemb, size);
}

void* nblex_realloc(void* ptr, size_t size) {
  return realloc(ptr, size);
}

void nblex_free(void* ptr) {
  free(ptr);
}

char* nblex_strdup(const char* s) {
  if (!s) {
    return NULL;
  }
  return strdup(s);
}
