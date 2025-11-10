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
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>

#define INITIAL_LINE_BUFFER_CAPACITY 1024
#define POLL_INTERVAL_MS 100

/* Forward declarations */
static int file_input_start(nblex_input* input);
static int file_input_stop(nblex_input* input);
static void file_input_free_data(nblex_input* input);
static void file_poll_timer_cb(uv_timer_t* handle);
static void file_fs_event_cb(uv_fs_event_t* handle, const char* filename, int events, int status);
static void file_read_new_data(nblex_input* input);

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

  /* Extract directory and filename for file watching */
  char* path_copy = nblex_strdup(path);
  if (!path_copy) {
    free(data->path);
    free(data);
    free(input);
    return NULL;
  }

  data->dir_path = nblex_strdup(dirname(path_copy));
  data->filename = nblex_strdup(basename(path_copy));
  free(path_copy);

  if (!data->dir_path || !data->filename) {
    if (data->dir_path) free(data->dir_path);
    if (data->filename) free(data->filename);
    free(data->path);
    free(data);
    free(input);
    return NULL;
  }

  data->file = NULL;
  data->watching = false;
  data->use_fs_event = false;

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
    fprintf(stderr, "Error: Failed to open file '%s': %s\n", 
            data->path, strerror(errno));
    return -1;
  }

  /* Set line buffering so new data is immediately available */
  setvbuf(data->file, NULL, _IOLBF, 0);

  /* Seek to end for tailing */
  fseek(data->file, 0, SEEK_END);

  /* Try to use uv_fs_event for efficient file watching */
  int ret = uv_fs_event_init(input->world->loop, &data->fs_event);
  if (ret == 0) {
    data->fs_event.data = input;
    ret = uv_fs_event_start(&data->fs_event, file_fs_event_cb,
                            data->dir_path, 0);
    if (ret == 0) {
      data->use_fs_event = true;
    } else {
      /* fs_event failed, will fall back to polling */
      uv_close((uv_handle_t*)&data->fs_event, NULL);
    }
  }

  /* Always use polling timer (as primary or backup to fs_event) */
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
    if (data->use_fs_event) {
      uv_fs_event_stop(&data->fs_event);
      uv_close((uv_handle_t*)&data->fs_event, NULL);
    } else {
      uv_timer_stop(&data->poll_timer);
      uv_close((uv_handle_t*)&data->poll_timer, NULL);
    }
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

  if (data->dir_path) {
    free(data->dir_path);
  }

  if (data->filename) {
    free(data->filename);
  }

  if (data->line_buffer) {
    free(data->line_buffer);
  }

  free(data);
}

/* Read new data from file (shared by both polling and fs_event callbacks) */
static void file_read_new_data(nblex_input* input) {
  if (!input || !input->data) {
    return;
  }

  nblex_file_input_data* data = (nblex_file_input_data*)input->data;
  if (!data->file) {
    return;
  }

  /* Check if file has grown by comparing current position to file size */
  long current_pos = ftell(data->file);
  if (current_pos < 0) {
    return;
  }

  struct stat st;
  if (stat(data->path, &st) == 0) {
    /* If file has grown, we can read new data */
    if (st.st_size > (off_t)current_pos) {
      /* File has grown - clear EOF flag and read new data */
      clearerr(data->file);
    } else if (feof(data->file)) {
      /* At EOF and file hasn't grown - nothing to read */
      return;
    }
  } else {
    /* If stat fails, clear error and try reading anyway */
    clearerr(data->file);
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
      /* If JSON parsing fails, fall back to creating a simple message object */
      if (!event->data) {
        event->data = json_object();
        json_object_set_new(event->data, "message", json_string(buffer));
      }
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

/* File system event callback - triggered when file changes */
static void file_fs_event_cb(uv_fs_event_t* handle, const char* filename, int events, int status) {
  nblex_input* input = (nblex_input*)handle->data;
  if (!input || !input->data) {
    return;
  }

  nblex_file_input_data* data = (nblex_file_input_data*)input->data;

  /* Check if this event is for our file (if filename is provided) */
  /* On some platforms, filename might be NULL, so we check the file anyway */
  if (filename && strcmp(filename, data->filename) != 0) {
    return;
  }

  /* Check if file was modified or renamed */
  if (status == 0 && (events & (UV_RENAME | UV_CHANGE))) {
    file_read_new_data(input);
  }
}

/* Polling timer callback (fallback when fs_event is not available) */
static void file_poll_timer_cb(uv_timer_t* handle) {
  nblex_input* input = (nblex_input*)handle->data;
  file_read_new_data(input);
}
