/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nblex_core.c - lexer
 *
 * Copyright (C) 2013, David Beckett http://www.dajobe.org/
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


static int
nblex_reset(nblex_world* world)
{
  /* reset any existing data */
  world->bytes_size = 0;
  world->codepoints_size = 0;

  return 0;
}


/**
 * nblex_start:
 * @world: world
 *
 * Start lexing
 *
 * Return value: non-0 on failure
 */
int
nblex_start(nblex_world* world)
{
  if(!world)
    return 1;

  if(nblex_reset(world))
    return 1;

  world->started = 1;

  return 0;
}


/**
 * nblex_finish
 * @world: world
 *
 * Finish lexing
 *
 * Return value: non-0 on failure
 */
int
nblex_finish(nblex_world* world)
{
  int rc = 0;
  
  if(!world)
    return 1;

  if(!world->started)
    return 1;

  if(world->bytes_size)
    rc = nblex_add_bytes(world, NULL, 0);

  if(!rc && world->codepoints_size)
    rc = nblex_add_codepoints(world, NULL, 0);

  world->started = 0;

  return rc;
}


/**
 * nblex_add_byte:
 * @world: world
 * @b: byte
 *
 * Add a byte to the lexer
 *
 * Take care to avoid mixing adding bytes with this function and
 * nblex_add_bytes() with adding codepoints with
 * nblex_add_codepoint() or nblex_add_codepoints(), unless you know
 * what you are doing.
 *
 * Return value: non-0 on failure
 */
int
nblex_add_byte(nblex_world* world, const unsigned char b) 
{
  unsigned char buffer[1]; /* static */

  buffer[0] = b;
  return nblex_add_bytes(world, buffer, (size_t)1);
}


/**
 * nblex_add_bytes:
 * @world: world:
 * @buffer: array of bytes to decode (or NULL)
 * @len: length of @buffer (or 0)
 *
 * Add bytes to the lexer
 *
 * If @buffer is NULL or @len is 0 then mark the end of the input
 * and process any stored data.
 *
 * Take care to avoid mixing adding bytes with this function and
 * nblex_add_byte() with adding codepoints with nblex_add_codepoint()
 * or nblex_add_codepoints(), unless you know what you are doing.
 *
 * Return value: non-0 on failure
 */
int
nblex_add_bytes(nblex_world* world, const unsigned char* buffer,
                size_t len)
{
  int rc;
  size_t bytes_offset = world->bytes_size /* 0 .. 3 */ ;
  
  if(!world)
    return 1;

  if(!buffer || !len) {
    if(bytes_offset) {
      nblex_unichar output;

      /* It's the end of the input */
      rc = nblex_unicode_utf8_string_get_char(world->bytes, bytes_offset,
                                              &output);
      if(rc < 1)
        /* any error in remaining data */
        output = NBLEX_CODEPOINT_INVALID;

      rc = nblex_add_codepoint(world, output);
      bytes_offset = 0;
    }
  } else {
    /* process input data */
    size_t offset = 0; /* 0 .. len-1 */
    while(offset < len) {
      nblex_unichar output = 0;

      world->bytes[bytes_offset++] = buffer[offset++];

      rc = nblex_unicode_utf8_string_get_char(world->bytes, bytes_offset,
                                              &output);
      if(rc > 0) {
        /* got a character and absorbed 'rc' bytes 1-4 */
        if(rc == 1) {
          world->bytes[0] = world->bytes[1];
          world->bytes[1] = world->bytes[2];
          world->bytes[2] = world->bytes[3];
        } else if(rc == 2) {
          world->bytes[0] = world->bytes[2];
          world->bytes[1] = world->bytes[3];
        } else if(rc == 3) {
          world->bytes[0] = world->bytes[3];
        }

        bytes_offset = 0;
        rc = nblex_add_codepoint(world, output);
        if(rc)
          break;

      } else {
        /* some kind of short data or bad data error */
        if(bytes_offset == 4) {
          /* Full working buffer so the data must be junk.
           * Skip over the bytes and record the codepoint issue.
           */
          rc = nblex_add_codepoint(world, NBLEX_CODEPOINT_INVALID);
          if(rc)
            break;

          bytes_offset = 0;
        } else {
          /* bytes_offset is 1-3 so add more bytes from input */
        }
      }
    }
  }

  world->bytes_size = bytes_offset;

  return rc;
}


static void
nblex_print_codepoint(FILE* stream, const nblex_unichar codepoint)
{
  if(codepoint <= NBLEX_UNICODE_MAX_CODEPOINT)
    fprintf(stream, "U+%05X",
            NBLEX_GOOD_CAST(unsigned int, (codepoint & 0x10ffff)));
  else if(codepoint == NBLEX_CODEPOINT_INVALID) 
    fputs("INVALID_UNICODEPOINT", stream);
  else
    fputs("UNKNOWN", stream);
}


/**
 * nblex_add_codepoint:
 * @world: world
 * @codepoint: codepoint
 *
 * Add a Unicode codepoint to the lexer
 *
 * Take care to avoid mixing adding bytes with nblex_add_byte() and
 * nblex_add_bytes() with adding codepoints with this function or
 * nblex_add_codepoints(), unless you know what you are doing.
 *
 * Return value: non-0 on failure
 */
int
nblex_add_codepoint(nblex_world* world, const nblex_unichar codepoint)
{
  if(!world)
    return 1;

  fputs("nblex_add_codepoint(", stderr);
  nblex_print_codepoint(stderr, codepoint);
  fputc(')', stderr);
  fputc('\n', stderr);
  return 0;
}


/**
 * nblex_add_codepoints:
 * @world: world:
 * @buffer: sequence of codepoints to add (or NULL)
 * @len: length of @buffer (or 0)
 *
 * Add a sequence of Unicode codepoints to the lexer
 *
 * If @buffer is NULL or @len is 0 then mark the end of the input
 * and process any stored data.
 *
 * Take care to avoid mixing adding bytes with nblex_add_byte() and
 * nblex_add_bytes() with adding codepoints with this function or
 * nblex_add_codepoint(), unless you know what you are doing.
 *
 * Return value: non-0 on failure
 */
int
nblex_add_codepoints(nblex_world* world, const nblex_unichar* codepoints,
                     size_t len)
{
  int is_end = (!codepoints || !len);
  size_t offset;
  int rc = 0;
  
  if(!world)
    return 1;

  for(offset = 0; offset < len; offset++) {
    rc = nblex_add_codepoint(world, codepoints[offset]);
    if(rc)
      break;
  }

  if(!rc && is_end)
    rc = nblex_add_codepoint(world, NBLEX_CODEPOINT_END_OF_INPUT);

  return rc;
}
