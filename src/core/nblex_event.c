/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nblex_event.c - Event handling
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "nblex/nblex.h"
#include <stdlib.h>
#include <stdio.h>

struct nblex_event_s {
  nblex_event_type type;
  /* TODO: Add event data fields */
};

nblex_event_type nblex_event_get_type(nblex_event* event) {
  if (!event) {
    return NBLEX_EVENT_ERROR;
  }
  return event->type;
}

char* nblex_event_to_json(nblex_event* event) {
  if (!event) {
    return NULL;
  }

  /* TODO: Implement JSON serialization */
  char* json = strdup("{\"type\": \"placeholder\"}");
  return json;
}

const char* nblex_version_string(void) {
  return NBLEX_VERSION_STRING;
}

int nblex_version_major(void) {
  return NBLEX_VERSION_MAJOR;
}

int nblex_version_minor(void) {
  return NBLEX_VERSION_MINOR;
}

int nblex_version_patch(void) {
  return NBLEX_VERSION_PATCH;
}
