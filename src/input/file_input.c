/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * file_input.c - File input handler (log tailing)
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INITIAL_LINE_BUFFER_CAPACITY 1024
#define POLL_INTERVAL_MS 100

/* Forward declarations */
static int file_input_start(nblex_input* input);
static int file_input_stop(nblex_input* input);
static void file_input_free_data(nblex_input* input);
static void file_poll_timer_cb(uv_timer_t* handle);

/* Virtual table for file input */
static const nblex_input_vtable file_input_vtable = {
  .name = "file",
  .start = file_input_start,
  .stop = file_input_stop,
  .free = file_input_free_data
};

nblex_input* nblex_input_file_new(nblex_world* world, const char* path) {
  if (!world || !path) {
    return NULL;
  }

  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_FILE);
  if (!input) {
    return NULL;
  }

  nblex_file_input_data* data = calloc(1, sizeof(nblex_file_input_data));
  if (!data) {
    free(input);
    return NULL;
  }

  data->path = nblex_strdup(path);
  if (!data->path) {
    free(data);
    free(input);
    return NULL;
  }

  data->file = NULL;
  data->watching = false;

  /* Allocate line buffer */
  data->line_buffer = malloc(INITIAL_LINE_BUFFER_CAPACITY);
  if (!data->line_buffer) {
    free(data->path);
    free(data);
    free(input);
    return NULL;
  }

  data->line_buffer_capacity = INITIAL_LINE_BUFFER_CAPACITY;
  data->line_buffer_size = 0;

  input->data = data;
  input->vtable = &file_input_vtable;

  /* Add to world */
  nblex_world_add_input(world, input);

  return input;
}

static int file_input_start(nblex_input* input) {
  if (!input || !input->data) {
    return -1;
  }

  nblex_file_input_data* data = (nblex_file_input_data*)input->data;

  /* Open file */
  data->file = fopen(data->path, "r");
  if (!data->file) {
    return -1;
  }

  /* Seek to end for tailing */
  fseek(data->file, 0, SEEK_END);

  /* Start polling timer */
  uv_timer_init(input->world->loop, &data->poll_timer);
  data->poll_timer.data = input;

  uv_timer_start(&data->poll_timer, file_poll_timer_cb,
                 POLL_INTERVAL_MS, POLL_INTERVAL_MS);

  data->watching = true;

  return 0;
}

static int file_input_stop(nblex_input* input) {
  if (!input || !input->data) {
    return -1;
  }

  nblex_file_input_data* data = (nblex_file_input_data*)input->data;

  if (data->watching) {
    uv_timer_stop(&data->poll_timer);
    uv_close((uv_handle_t*)&data->poll_timer, NULL);
    data->watching = false;
  }

  if (data->file) {
    fclose(data->file);
    data->file = NULL;
  }

  return 0;
}

static void file_input_free_data(nblex_input* input) {
  if (!input || !input->data) {
    return;
  }

  nblex_file_input_data* data = (nblex_file_input_data*)input->data;

  if (data->path) {
    free(data->path);
  }

  if (data->line_buffer) {
    free(data->line_buffer);
  }

  free(data);
}

static void file_poll_timer_cb(uv_timer_t* handle) {
  nblex_input* input = (nblex_input*)handle->data;
  if (!input || !input->data) {
    return;
  }

  nblex_file_input_data* data = (nblex_file_input_data*)input->data;
  if (!data->file) {
    return;
  }

  /* Read lines from file */
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), data->file) != NULL) {
    size_t len = strlen(buffer);

    /* Remove trailing newline */
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
      len--;
    }

    /* Skip empty lines */
    if (len == 0) {
      continue;
    }

    /* Create event */
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, input);
    if (!event) {
      continue;
    }

    /* Parse line based on format */
    if (input->format == NBLEX_FORMAT_JSON) {
      event->data = nblex_parse_json_line(buffer);
    } else {
      /* For other formats, create a simple JSON object with the raw line */
      event->data = json_object();
      json_object_set_new(event->data, "message", json_string(buffer));
    }

    if (!event->data) {
      nblex_event_free(event);
      continue;
    }

    /* Emit event */
    nblex_event_emit(input->world, event);
  }
}
