/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * json_parser.c - JSON log parser
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>

json_t* nblex_parse_json_line(const char* line) {
  if (!line) {
    return NULL;
  }

  json_error_t error;
  json_t* root = json_loads(line, 0, &error);

  if (!root) {
    /* JSON parse error - return NULL */
    return NULL;
  }

  /* Verify it's an object */
  if (!json_is_object(root)) {
    json_decref(root);
    return NULL;
  }

  return root;
}
