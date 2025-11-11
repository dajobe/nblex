/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_helpers.h - Shared test utilities for nblex tests
 *
 * Licensed under the Apache License, Version 2.0
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include "../src/nblex_internal.h"

/* Build an event with a specific field-value pair */
nblex_event* test_build_event_with_field(nblex_world** world_out,
                                          nblex_input** input_out,
                                          const char* field,
                                          json_t* value);

/* Event capture for testing */
extern nblex_event* test_captured_event;
extern nblex_event** test_captured_events;
extern size_t test_captured_events_count;
extern size_t test_captured_events_capacity;

/* Event capture handler */
void test_capture_event_handler(nblex_event* event, void* user_data);

/* Reset captured events */
void test_reset_captured_events(void);

#endif /* TEST_HELPERS_H */

