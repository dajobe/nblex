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

/* Clone an event. The event struct is duplicated, the JSON data (if any)
 * is incref'd. The input pointer is copied (not cloned). The returned
 * event must be freed with nblex_event_free().
 */
nblex_event* nblex_event_clone(nblex_event* src) {
  if (!src) return NULL;
  nblex_event* dst = calloc(1, sizeof(nblex_event));
  if (!dst) return NULL;
  dst->type = src->type;
  dst->timestamp_ns = src->timestamp_ns;
  dst->input = src->input;
  dst->data = src->data;
  if (dst->data) {
    json_incref(dst->data);
  }
  return dst;
}

void nblex_event_emit(nblex_world* world, nblex_event* event) {
  if (!world || !event) {
    return;
  }

  /* Check filter if input has one */
  if (event->input && event->input->filter) {
    if (!nblex_filter_matches(event->input->filter, event)) {
      /* Filter doesn't match, drop event */
      nblex_event_free(event);
      return;
    }
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
