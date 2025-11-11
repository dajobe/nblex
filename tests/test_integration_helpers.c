/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_integration_helpers.c - Shared helpers for integration tests
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

/* Feature test macros must be defined before any system headers */
#ifndef __APPLE__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#include "test_integration_helpers.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

/* Helper to create temporary file with content */
char* create_temp_file(const char* content) {
  char template[] = "/tmp/nblex_test_integration_XXXXXX";
  int fd = mkstemp(template);
  if (fd < 0) {
    return NULL;
  }
  
  if (content) {
    ssize_t written = write(fd, content, strlen(content));
    if (written < 0 || (size_t)written != strlen(content)) {
      close(fd);
      unlink(template);
      return NULL;
    }
    fsync(fd);
  }
  close(fd);
  
  char* path = malloc(strlen(template) + 1);
  if (path) {
    strcpy(path, template);
  }
  return path;
}

