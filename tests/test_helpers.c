/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_helpers.c - Shared test utilities implementation
 *
 * Licensed under the Apache License, Version 2.0
 */

#include "test_helpers.h"
#include <stdlib.h>
#include <string.h>

/* Event capture state */
nblex_event* test_captured_event = NULL;
nblex_event** test_captured_events = NULL;
size_t test_captured_events_count = 0;
size_t test_captured_events_capacity = 0;

nblex_event* test_build_event_with_field(nblex_world** world_out,
                                          nblex_input** input_out,
                                          const char* field,
                                          json_t* value) {
  nblex_world* world = nblex_world_new();
  if (!world) {
    return NULL;
  }

  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  if (!input) {
    nblex_world_free(world);
    return NULL;
  }

  nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
  if (!event) {
    nblex_input_free(input);
    nblex_world_free(world);
    return NULL;
  }

  json_t* data = json_object();
  if (!data) {
    nblex_event_free(event);
    nblex_input_free(input);
    nblex_world_free(world);
    return NULL;
  }
  
  json_object_set_new(data, field, value);
  event->data = data;

  if (world_out) {
    *world_out = world;
  }
  if (input_out) {
    *input_out = input;
  }

  return event;
}

void test_capture_event_handler(nblex_event* event, void* user_data) {
  (void)user_data;
  
  /* Free previous single captured event */
  if (test_captured_event) {
    nblex_event_free(test_captured_event);
  }
  test_captured_event = nblex_event_clone(event);
  
  /* Also add to array */
  if (test_captured_events_count >= test_captured_events_capacity) {
    size_t new_capacity = test_captured_events_capacity == 0 
                          ? 16 
                          : test_captured_events_capacity * 2;
    nblex_event** new_events = realloc(test_captured_events, 
                                       new_capacity * sizeof(nblex_event*));
    if (new_events) {
      test_captured_events = new_events;
      test_captured_events_capacity = new_capacity;
    }
  }
  
  if (test_captured_events && 
      test_captured_events_count < test_captured_events_capacity) {
    test_captured_events[test_captured_events_count] = nblex_event_clone(event);
    if (test_captured_events[test_captured_events_count]) {
      test_captured_events_count++;
    }
  }
}

void test_reset_captured_events(void) {
  if (test_captured_event) {
    nblex_event_free(test_captured_event);
    test_captured_event = NULL;
  }
  
  if (test_captured_events) {
    for (size_t i = 0; i < test_captured_events_count; i++) {
      if (test_captured_events[i]) {
        nblex_event_free(test_captured_events[i]);
      }
    }
    free(test_captured_events);
    test_captured_events = NULL;
    test_captured_events_count = 0;
    test_captured_events_capacity = 0;
  }
}

