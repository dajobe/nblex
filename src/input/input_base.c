/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * input_base.c - Input base implementation
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>

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

  return input;
}

void nblex_input_free(nblex_input* input) {
  if (!input) {
    return;
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

int nblex_input_set_filter(nblex_input* input, const char* filter) {
  if (!input) {
    return -1;
  }

  /* TODO: Parse and store filter expression */
  (void)filter;  /* Unused for now */

  return 0;
}
