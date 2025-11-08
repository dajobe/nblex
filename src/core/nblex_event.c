/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nblex_event.c - Event handling
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

nblex_event* nblex_event_new(nblex_event_type type, nblex_input* input) {
  nblex_event* event = calloc(1, sizeof(nblex_event));
  if (!event) {
    return NULL;
  }

  event->type = type;
  event->input = input;
  event->timestamp_ns = nblex_timestamp_now();
  event->data = NULL;

  return event;
}

void nblex_event_free(nblex_event* event) {
  if (!event) {
    return;
  }

  if (event->data) {
    json_decref(event->data);
  }

  free(event);
}

void nblex_event_emit(nblex_world* world, nblex_event* event) {
  if (!world || !event) {
    return;
  }

  world->events_processed++;

  /* Process through correlation engine if available */
  if (world->correlation) {
    nblex_correlation_process_event(world->correlation, event);
  }

  /* Call event handler if registered */
  if (world->event_handler) {
    world->event_handler(event, world->event_handler_data);
  }

  /* Free event after handling */
  nblex_event_free(event);
}

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

  return nblex_event_to_json_string(event);
}

/* Version functions */

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
