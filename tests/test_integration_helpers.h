/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * test_integration_helpers.h - Shared helpers for integration tests
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#ifndef TEST_INTEGRATION_HELPERS_H
#define TEST_INTEGRATION_HELPERS_H

/* Create temporary file with content, returns path (caller must free) */
char* create_temp_file(const char* content);

#endif /* TEST_INTEGRATION_HELPERS_H */

