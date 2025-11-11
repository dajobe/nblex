/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nblex_world.c - Core world management
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_INPUTS_CAPACITY 8

nblex_world* nblex_world_new(void) {
  nblex_world* world = calloc(1, sizeof(nblex_world));
  if (!world) {
    return NULL;
  }

  /* Allocate event loop */
  world->loop = malloc(sizeof(uv_loop_t));
  if (!world->loop) {
    free(world);
    return NULL;
  }

  if (uv_loop_init(world->loop) != 0) {
    free(world->loop);
    free(world);
    return NULL;
  }

  /* Allocate inputs array */
  world->inputs = calloc(INITIAL_INPUTS_CAPACITY, sizeof(nblex_input*));
  if (!world->inputs) {
    uv_loop_close(world->loop);
    free(world->loop);
    free(world);
    return NULL;
  }

  world->inputs_capacity = INITIAL_INPUTS_CAPACITY;
  world->inputs_count = 0;

  return world;
}

int nblex_world_open(nblex_world* world) {
  if (!world) {
    return -1;
  }

  if (world->opened) {
    return -1;
  }

  /* Create correlation engine */
  world->correlation = nblex_correlation_new(world);
  if (!world->correlation) {
    return -1;
  }

  /* Set default 100ms time-based correlation */
  nblex_correlation_add_strategy(world->correlation, NBLEX_CORR_TIME_BASED, 100);

  world->opened = true;
  return 0;
}

void nblex_world_free(nblex_world* world) {
  if (!world) {
    return;
  }

  /* Stop if running */
  if (world->running) {
    nblex_world_stop(world);
  }

  /* Stop all inputs explicitly to close their handles */
  if (world->started && world->inputs) {
    for (size_t i = 0; i < world->inputs_count; i++) {
      nblex_input* input = world->inputs[i];
      if (input && input->vtable && input->vtable->stop) {
        input->vtable->stop(input);
      }
    }
  }

  /* Free correlation engine (this closes its timer handle) */
  /* The close callback will free the correlation struct, and the uv_loop_close
   * loop below will process the close callback */
  if (world->correlation) {
    nblex_correlation_free(world->correlation);
    world->correlation = NULL;  /* Clear pointer after freeing */
  }

  /* Ask the executor module to shutdown exec-context timers that reference
   * this world's loop so their close callbacks can be processed during the
   * loop drain. The executor has access to the internal exec_contexts list.
   */
  nblex_exec_contexts_shutdown_world(world);

  /* Free inputs */
  if (world->inputs) {
    for (size_t i = 0; i < world->inputs_count; i++) {
      if (world->inputs[i]) {
        nblex_input_free(world->inputs[i]);
      }
    }
    free(world->inputs);
  }

  /* Close event loop */
  if (world->loop) {
    /* Attempt to close the loop. If there are still active handles,
     * uv_loop_close() will return UV_EBUSY; in that case run the loop
     * to allow close callbacks to run and retry until it succeeds.
     */
    int rc = uv_loop_close(world->loop);
    while (rc == UV_EBUSY) {
      /* Run pending close callbacks once and retry. Use UV_RUN_ONCE so the
       * loop will process at least one pending callback (close callbacks)
       * which allows uv_loop_close() to eventually succeed.
       */
      uv_run(world->loop, UV_RUN_ONCE);
      rc = uv_loop_close(world->loop);
    }

    free(world->loop);
  }

  free(world);
}

int nblex_world_start(nblex_world* world) {
  if (!world || !world->opened) {
    return -1;
  }

  if (world->started) {
    return -1;  /* Already started */
  }

  /* Start correlation engine */
  if (world->correlation) {
    if (nblex_correlation_start(world->correlation) != 0) {
      return -1;
    }
  }

  /* Start all inputs */
  for (size_t i = 0; i < world->inputs_count; i++) {
    nblex_input* input = world->inputs[i];
    if (input && input->vtable && input->vtable->start) {
      if (input->vtable->start(input) != 0) {
        return -1;
      }
    }
  }

  world->started = true;
  return 0;
}

int nblex_world_stop(nblex_world* world) {
  if (!world) {
    return -1;
  }

  /* Stop all inputs */
  for (size_t i = 0; i < world->inputs_count; i++) {
    nblex_input* input = world->inputs[i];
    if (input && input->vtable && input->vtable->stop) {
      input->vtable->stop(input);
    }
  }

  world->started = false;
  world->running = false;

  /* Stop event loop */
  if (world->loop) {
    uv_stop(world->loop);
  }

  return 0;
}

int nblex_world_run(nblex_world* world) {
  if (!world || !world->started) {
    return -1;
  }

  world->running = true;

  /* Run event loop */
  uv_run(world->loop, UV_RUN_DEFAULT);

  world->running = false;
  return 0;
}

int nblex_set_event_handler(nblex_world* world,
                             nblex_event_handler handler,
                             void* user_data) {
  if (!world) {
    return -1;
  }

  world->event_handler = handler;
  world->event_handler_data = user_data;
  return 0;
}

int nblex_world_add_input(nblex_world* world, nblex_input* input) {
  if (!world || !input) {
    return -1;
  }

  /* Grow array if needed */
  if (world->inputs_count >= world->inputs_capacity) {
    size_t new_capacity = world->inputs_capacity * 2;
    nblex_input** new_inputs = realloc(world->inputs,
                                       new_capacity * sizeof(nblex_input*));
    if (!new_inputs) {
      return -1;
    }
    world->inputs = new_inputs;
    world->inputs_capacity = new_capacity;
  }

  world->inputs[world->inputs_count++] = input;
  return 0;
}
