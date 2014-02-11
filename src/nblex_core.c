/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nblex_core.c - core
 *
 * Copyright (C) 2013-2014, David Beckett http://www.dajobe.org/
 *
 * This package is Free Software
 *
 * It is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License (LGPL) V2.1 or any newer version
 *   2. GNU General Public License (GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 *
 * You may not use this file except in compliance with at least one of
 * the above three licenses.
 *
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * complete terms and further detail along with the license texts for
 * the licenses in COPYING.LIB, COPYING and LICENSE-2.0.txt respectively.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include <nblex_config.h>
#endif

#include <stdio.h>
#include <string.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "nblex.h"
#include "nblex_internal.h"


/**
 * nblex_new_world:
 *
 * Allocate a new nblex_world object.
 *
 * The nblex_world is initialized with nblex_world_open().
 * Allocation and initialization are decoupled to allow
 * changing settings on the world object before init.
 *
 * Return value: nblex_world object or NULL on failure
 **/
nblex_world*
nblex_new_world(void)
{
  nblex_world* world;

  world = NBLEX_CALLOC(nblex_world*, 1, sizeof(*world));
  if(!world)
    return NULL;

  return world;
}


/**
 * nblex_world_open:
 * @world: nblex_world object
 *
 * Initialise the nblex library.
 *
 * Initializes a #nblex_world object created by nblex_new_world().
 * Allocation and initialization are decoupled to allow
 * changing settings on the world object before init.
 *
 * The initialized world object is used with subsequent nblex API calls.
 *
 * Return value: non-0 on failure
 **/
int
nblex_world_open(nblex_world *world)
{
  if(!world)
    return 1;

  if(world->opened++)
    return 0; /* not an error */

  return 0;
}


/**
 * nblex_free_world:
 * @world: nblex_world object
 *
 * Terminate the nblex library.
 *
 * Destroys a nblex_world object and all static information.
 *
 * Return value: non-0 on failure
 **/
int
nblex_free_world(nblex_world* world)
{
  if(!world)
    return 1;

  if(world->codepoints_capacity) {
    NBLEX_FREE(nblex_unchar*, world->codepoints);
    world->codepoints = NULL;
    world->codepoints_capacity = 0;
  }

  NBLEX_FREE(nblex_world, world);

  return 0;
}
