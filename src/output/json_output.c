/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * json_output.c - JSON output formatter
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char* event_type_to_string(nblex_event_type type) {
  switch (type) {
    case NBLEX_EVENT_LOG:
      return "log";
    case NBLEX_EVENT_NETWORK:
      return "network";
    case NBLEX_EVENT_CORRELATION:
      return "correlation";
    case NBLEX_EVENT_ERROR:
      return "error";
    default:
      return "unknown";
  }
}

char* nblex_event_to_json_string(nblex_event* event) {
  if (!event) {
    return NULL;
  }

  json_t* root = json_object();
  if (!root) {
    return NULL;
  }

  /* Add event type */
  json_object_set_new(root, "type", json_string(event_type_to_string(event->type)));

  /* Add timestamp */
  json_object_set_new(root, "timestamp_ns", json_integer(event->timestamp_ns));

  /* Add source information */
  if (event->input) {
    json_t* source = json_object();
    const char* input_type_str = NULL;

    switch (event->input->type) {
      case NBLEX_INPUT_FILE:
        input_type_str = "file";
        break;
      case NBLEX_INPUT_SYSLOG:
        input_type_str = "syslog";
        break;
      case NBLEX_INPUT_PCAP:
        input_type_str = "pcap";
        break;
      case NBLEX_INPUT_SOCKET:
        input_type_str = "socket";
        break;
      default:
        input_type_str = "unknown";
        break;
    }

    json_object_set_new(source, "type", json_string(input_type_str));
    json_object_set_new(root, "source", source);
  }

  /* Add event data */
  if (event->data) {
    /* Increment reference count since we're adding it to a new object */
    json_incref(event->data);
    json_object_set_new(root, "data", event->data);
  }

  /* Serialize to string */
  char* json_str = json_dumps(root, JSON_COMPACT);

  json_decref(root);

  return json_str;
}
