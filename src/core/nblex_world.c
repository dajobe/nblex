/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nblex_world.c - Core world management
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "nblex/nblex.h"
#include <stdlib.h>
#include <string.h>

struct nblex_world_s {
  bool opened;
  bool started;
  nblex_event_handler event_handler;
  void* event_handler_data;
};

nblex_world* nblex_world_new(void) {
  nblex_world* world = calloc(1, sizeof(nblex_world));
  if (!world) {
    return NULL;
  }

  return world;
}

int nblex_world_open(nblex_world* world) {
  if (!world) {
    return -1;
  }

  if (world->opened) {
    return -1;
  }

  world->opened = true;
  return 0;
}

void nblex_world_free(nblex_world* world) {
  if (!world) {
    return;
  }

  free(world);
}

int nblex_world_start(nblex_world* world) {
  if (!world || !world->opened) {
    return -1;
  }

  world->started = true;
  return 0;
}

int nblex_world_stop(nblex_world* world) {
  if (!world) {
    return -1;
  }

  world->started = false;
  return 0;
}

int nblex_world_run(nblex_world* world) {
  if (!world || !world->started) {
    return -1;
  }

  /* TODO: Implement event loop */
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
