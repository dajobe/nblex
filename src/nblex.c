/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nblex.c - Non-blocking lexer test
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
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <stdarg.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "nblex.h"
#include "nblex_internal.h"


int main(int argc, char *argv[]);


int
main(int argc, char *argv[])
{
  const char* program = PACKAGE;
  int help = 0;
  int usage = 0;
  nblex_world* world = NULL;

  if(argc == 2) {
    const char* arg = argv[1];
    if(!strcmp(arg, "-v") || !strcmp(arg, "--version")) {
      fputs(PACKAGE_VERSION, stdout);
      fputc('\n', stdout);
      exit(0);
    } else if(!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
      help = 1;
    }
  } else
    usage = 1;

  if(usage) {
    fprintf(stderr, "Try `%s -h' for more information.\n", program);
    exit(1);
  }

  if(help) {
    fprintf(stdout, "%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
    printf("Usage: %s [OPTIONS] <arguments>\n", program);
    exit(0);
  }


  world = nblex_new_world();
  goto tidy;

  tidy:
  if(world)
    nblex_free_world(world);

  exit(0);
}
