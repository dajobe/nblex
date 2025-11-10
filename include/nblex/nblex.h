/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nblex.h - Network & Buffer Log EXplorer public API
 *
 * Copyright (C) 2025
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NBLEX_H
#define NBLEX_H

/* Feature test macros must be defined before any system headers */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Version information */
#define NBLEX_VERSION_MAJOR 0
#define NBLEX_VERSION_MINOR 1
#define NBLEX_VERSION_PATCH 0
#define NBLEX_VERSION_STRING "0.1.0"

/* API export macros */
#ifndef NBLEX_API
#  ifdef _WIN32
#    ifdef NBLEX_BUILD
#      define NBLEX_API __declspec(dllexport)
#    else
#      define NBLEX_API __declspec(dllimport)
#    endif
#  else
#    define NBLEX_API __attribute__((visibility("default")))
#  endif
#endif

/* Forward declarations */
typedef struct nblex_world_s nblex_world;
typedef struct nblex_input_s nblex_input;
typedef struct nblex_output_s nblex_output;
typedef struct nblex_event_s nblex_event;
typedef struct nblex_correlation_s nblex_correlation;

/* Event types */
typedef enum {
  NBLEX_EVENT_LOG,          /* Log event */
  NBLEX_EVENT_NETWORK,      /* Network event */
  NBLEX_EVENT_CORRELATION,  /* Correlated event */
  NBLEX_EVENT_ERROR,        /* Error event */
  NBLEX_EVENT_MAX           /* Maximum event type value */
} nblex_event_type;

/* Input types */
typedef enum {
  NBLEX_INPUT_FILE,         /* File input */
  NBLEX_INPUT_SYSLOG,       /* Syslog input */
  NBLEX_INPUT_PCAP,         /* Packet capture input */
  NBLEX_INPUT_SOCKET        /* Socket input */
} nblex_input_type;

/* Log formats */
typedef enum {
  NBLEX_FORMAT_JSON,        /* JSON format */
  NBLEX_FORMAT_LOGFMT,      /* Logfmt format */
  NBLEX_FORMAT_SYSLOG,      /* Syslog format */
  NBLEX_FORMAT_NGINX,       /* Nginx combined log format */
  NBLEX_FORMAT_REGEX        /* Custom regex format */
} nblex_log_format;

/* Correlation strategies */
typedef enum {
  NBLEX_CORR_TIME_BASED,    /* Time-based correlation */
  NBLEX_CORR_ID_BASED,      /* ID-based correlation */
  NBLEX_CORR_CONNECTION     /* Connection-based correlation */
} nblex_correlation_type;

/* Event callback */
typedef void (*nblex_event_handler)(nblex_event* event, void* user_data);

/*
 * Core API - World management
 */

/**
 * nblex_world_new - Create a new nblex world
 *
 * Returns: New world instance or NULL on error
 */
NBLEX_API nblex_world* nblex_world_new(void);

/**
 * nblex_world_open - Initialize a world for use
 *
 * @world: World instance
 * Returns: 0 on success, non-zero on error
 */
NBLEX_API int nblex_world_open(nblex_world* world);

/**
 * nblex_world_free - Free a world instance
 *
 * @world: World instance
 */
NBLEX_API void nblex_world_free(nblex_world* world);

/**
 * nblex_world_start - Start processing events
 *
 * @world: World instance
 * Returns: 0 on success, non-zero on error
 */
NBLEX_API int nblex_world_start(nblex_world* world);

/**
 * nblex_world_stop - Stop processing events
 *
 * @world: World instance
 * Returns: 0 on success, non-zero on error
 */
NBLEX_API int nblex_world_stop(nblex_world* world);

/**
 * nblex_world_run - Run event loop (blocking)
 *
 * @world: World instance
 * Returns: 0 on success, non-zero on error
 */
NBLEX_API int nblex_world_run(nblex_world* world);

/*
 * Input API
 */

/**
 * nblex_input_file_new - Create a file input
 *
 * @world: World instance
 * @path: File path
 * Returns: New input instance or NULL on error
 */
NBLEX_API nblex_input* nblex_input_file_new(nblex_world* world, const char* path);

/**
 * nblex_input_pcap_new - Create a packet capture input
 *
 * @world: World instance
 * @interface: Network interface name
 * Returns: New input instance or NULL on error
 */
NBLEX_API nblex_input* nblex_input_pcap_new(nblex_world* world, const char* interface);

/**
 * nblex_input_set_format - Set log format for an input
 *
 * @input: Input instance
 * @format: Log format
 * Returns: 0 on success, non-zero on error
 */
NBLEX_API int nblex_input_set_format(nblex_input* input, nblex_log_format format);

/**
 * nblex_input_set_filter - Set filter for an input
 *
 * @input: Input instance
 * @filter: Filter expression
 * Returns: 0 on success, non-zero on error
 */
NBLEX_API int nblex_input_set_filter(nblex_input* input, const char* filter);

/**
 * nblex_input_free - Free an input instance
 *
 * @input: Input instance
 */
NBLEX_API void nblex_input_free(nblex_input* input);

/*
 * Correlation API
 */

/**
 * nblex_correlation_new - Create a correlation engine
 *
 * @world: World instance
 * Returns: New correlation instance or NULL on error
 */
NBLEX_API nblex_correlation* nblex_correlation_new(nblex_world* world);

/**
 * nblex_correlation_add_strategy - Add a correlation strategy
 *
 * @corr: Correlation instance
 * @type: Strategy type
 * @window_ms: Time window in milliseconds (for time-based)
 * Returns: 0 on success, non-zero on error
 */
NBLEX_API int nblex_correlation_add_strategy(nblex_correlation* corr,
                                              nblex_correlation_type type,
                                              uint32_t window_ms);

/**
 * nblex_correlation_free - Free a correlation instance
 *
 * @corr: Correlation instance
 */
NBLEX_API void nblex_correlation_free(nblex_correlation* corr);

/*
 * Event API
 */

/**
 * nblex_set_event_handler - Set event callback
 *
 * @world: World instance
 * @handler: Callback function
 * @user_data: User data passed to callback
 * Returns: 0 on success, non-zero on error
 */
NBLEX_API int nblex_set_event_handler(nblex_world* world,
                                       nblex_event_handler handler,
                                       void* user_data);

/**
 * nblex_event_get_type - Get event type
 *
 * @event: Event instance
 * Returns: Event type
 */
NBLEX_API nblex_event_type nblex_event_get_type(nblex_event* event);

/**
 * nblex_event_to_json - Convert event to JSON string
 *
 * @event: Event instance
 * Returns: JSON string (caller must free) or NULL on error
 */
NBLEX_API char* nblex_event_to_json(nblex_event* event);

/*
 * Utility functions
 */

/**
 * nblex_version_string - Get version string
 *
 * Returns: Version string
 */
NBLEX_API const char* nblex_version_string(void);

/**
 * nblex_version_major - Get major version
 *
 * Returns: Major version number
 */
NBLEX_API int nblex_version_major(void);

/**
 * nblex_version_minor - Get minor version
 *
 * Returns: Minor version number
 */
NBLEX_API int nblex_version_minor(void);

/**
 * nblex_version_patch - Get patch version
 *
 * Returns: Patch version number
 */
NBLEX_API int nblex_version_patch(void);

#ifdef __cplusplus
}
#endif

#endif /* NBLEX_H */
