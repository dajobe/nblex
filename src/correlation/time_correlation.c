/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * time_correlation.c - Time-based event correlation
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>

#define CLEANUP_INTERVAL_MS 1000  /* Clean up old events every second */
#define MAX_BUFFER_SIZE 10000     /* Maximum events to buffer */

/* Forward declarations */
static void cleanup_timer_cb(uv_timer_t* handle);
static void correlation_check_event(nblex_correlation* corr, nblex_event* new_event);

/*
 * Create a new correlation engine
 */
nblex_correlation* nblex_correlation_new(nblex_world* world) {
  if (!world) {
    return NULL;
  }

  nblex_correlation* corr = nblex_calloc(1, sizeof(nblex_correlation));
  if (!corr) {
    return NULL;
  }

  corr->world = world;
  corr->type = NBLEX_CORR_TIME_BASED;
  corr->window_ns = 100 * 1000000ULL;  /* Default 100ms in nanoseconds */
  corr->log_events = NULL;
  corr->network_events = NULL;
  corr->log_events_count = 0;
  corr->network_events_count = 0;
  corr->correlations_found = 0;
  corr->timer_initialized = 0;  /* Timer not initialized yet */

  return corr;
}

/*
 * Add a correlation strategy
 */
int nblex_correlation_add_strategy(nblex_correlation* corr,
                                    nblex_correlation_type type,
                                    uint32_t window_ms) {
  if (!corr) {
    return -1;
  }

  corr->type = type;
  corr->window_ns = (uint64_t)window_ms * 1000000ULL;  /* Convert ms to ns */

  return 0;
}

/*
 * Start correlation engine
 */
int nblex_correlation_start(nblex_correlation* corr) {
  if (!corr || !corr->world || !corr->world->loop) {
    return -1;
  }

  /* Initialize cleanup timer */
  int result = uv_timer_init(corr->world->loop, &corr->cleanup_timer);
  if (result != 0) {
    return -1;
  }

  corr->cleanup_timer.data = corr;
  corr->timer_initialized = 1;  /* Mark timer as initialized */

  /* Start periodic cleanup */
  result = uv_timer_start(&corr->cleanup_timer, cleanup_timer_cb,
                         CLEANUP_INTERVAL_MS, CLEANUP_INTERVAL_MS);
  if (result != 0) {
    corr->timer_initialized = 0;  /* Reset on failure */
    return -1;
  }

  return 0;
}

/*
 * Free correlation engine
 */
void nblex_correlation_free(nblex_correlation* corr) {
  if (!corr) {
    return;
  }

  /* Stop and close timer only if it was initialized */
  if (corr->timer_initialized && corr->world && corr->world->loop) {
    uv_timer_stop(&corr->cleanup_timer);
    uv_close((uv_handle_t*)&corr->cleanup_timer, NULL);
  }

  /* Free buffered log events */
  nblex_event_buffer_entry* entry = corr->log_events;
  while (entry) {
    nblex_event_buffer_entry* next = entry->next;
    nblex_event_free(entry->event);
    nblex_free(entry);
    entry = next;
  }

  /* Free buffered network events */
  entry = corr->network_events;
  while (entry) {
    nblex_event_buffer_entry* next = entry->next;
    nblex_event_free(entry->event);
    nblex_free(entry);
    entry = next;
  }

  nblex_free(corr);
}

/*
 * Add event to buffer
 */
static int add_to_buffer(nblex_event_buffer_entry** buffer_head,
                        size_t* count,
                        nblex_event* event) {
  /* Check buffer size limit */
  if (*count >= MAX_BUFFER_SIZE) {
    return -1;
  }

  /* Create buffer entry */
  nblex_event_buffer_entry* entry = nblex_malloc(sizeof(nblex_event_buffer_entry));
  if (!entry) {
    return -1;
  }

  /* Duplicate event using clone helper to properly manage JSON refs */
  nblex_event* event_copy = nblex_event_clone(event);
  if (!event_copy) {
    nblex_free(entry);
    return -1;
  }
  entry->event = event_copy;
  entry->next = *buffer_head;
  *buffer_head = entry;
  (*count)++;

  return 0;
}

/*
 * Remove old events from buffer
 */
static void cleanup_old_events(nblex_event_buffer_entry** buffer_head,
                               size_t* count,
                               uint64_t cutoff_time) {
  nblex_event_buffer_entry** entry_ptr = buffer_head;

  while (*entry_ptr) {
    nblex_event_buffer_entry* entry = *entry_ptr;

    if (entry->event->timestamp_ns < cutoff_time) {
      /* Remove this entry */
      *entry_ptr = entry->next;
      nblex_event_free(entry->event);
      nblex_free(entry);
      (*count)--;
    } else {
      entry_ptr = &entry->next;
    }
  }
}

/*
 * Create correlation event
 */
static nblex_event* create_correlation_event(nblex_correlation* corr,
                                             nblex_event* log_event,
                                             nblex_event* network_event) {
  nblex_event* corr_event = nblex_malloc(sizeof(nblex_event));
  if (!corr_event) {
    return NULL;
  }

  corr_event->type = NBLEX_EVENT_CORRELATION;
  corr_event->timestamp_ns = log_event->timestamp_ns;
  corr_event->input = NULL;

  /* Create JSON object with both events */
  json_t* corr_data = json_object();
  if (!corr_data) {
    nblex_free(corr_event);
    return NULL;
  }

  json_object_set_new(corr_data, "correlation_type", json_string("time_based"));
  json_object_set_new(corr_data, "window_ms",
                      json_integer(corr->window_ns / 1000000ULL));

  /* Add log event data */
  if (log_event->data) {
    json_object_set(corr_data, "log", log_event->data);
  }

  /* Add network event data */
  if (network_event->data) {
    json_object_set(corr_data, "network", network_event->data);
  }

  /* Calculate time difference */
  int64_t time_diff_ns = (int64_t)log_event->timestamp_ns -
                         (int64_t)network_event->timestamp_ns;
  double time_diff_ms = time_diff_ns / 1000000.0;
  json_object_set_new(corr_data, "time_diff_ms", json_real(time_diff_ms));

  corr_event->data = corr_data;

  return corr_event;
}

/*
 * Check for correlations with new event
 */
static void correlation_check_event(nblex_correlation* corr, nblex_event* new_event) {
  if (!corr || !new_event) {
    return;
  }

  /* Determine which buffer to check against */
  nblex_event_buffer_entry* check_buffer = NULL;
  if (new_event->type == NBLEX_EVENT_LOG) {
    check_buffer = corr->network_events;
  } else if (new_event->type == NBLEX_EVENT_NETWORK) {
    check_buffer = corr->log_events;
  } else {
    return;  /* Don't correlate other event types */
  }

  /* Check for matches within time window */
  nblex_event_buffer_entry* entry = check_buffer;
  while (entry) {
    nblex_event* buffered_event = entry->event;

    /* Calculate time difference */
    int64_t time_diff = (int64_t)new_event->timestamp_ns -
                       (int64_t)buffered_event->timestamp_ns;

    /* Check if within window (±) */
    if (llabs(time_diff) <= (int64_t)corr->window_ns) {
      /* Found a correlation! */
      nblex_event* corr_event;

      if (new_event->type == NBLEX_EVENT_LOG) {
        corr_event = create_correlation_event(corr, new_event, buffered_event);
      } else {
        corr_event = create_correlation_event(corr, buffered_event, new_event);
      }

      if (corr_event) {
        corr->correlations_found++;
        corr->world->events_correlated++;

        /* Emit correlation event */
        nblex_event_emit(corr->world, corr_event);

        /* Free correlation event */
        nblex_event_free(corr_event);
      }
    }

    entry = entry->next;
  }
}

/*
 * Process event through correlation engine
 */
void nblex_correlation_process_event(nblex_correlation* corr, nblex_event* event) {
  if (!corr || !event) {
    return;
  }

  /* Check for correlations with existing events */
  correlation_check_event(corr, event);

  /* Add event to appropriate buffer */
  if (event->type == NBLEX_EVENT_LOG) {
    add_to_buffer(&corr->log_events, &corr->log_events_count, event);
  } else if (event->type == NBLEX_EVENT_NETWORK) {
    add_to_buffer(&corr->network_events, &corr->network_events_count, event);
  }
}

/*
 * Periodic cleanup timer callback
 */
static void cleanup_timer_cb(uv_timer_t* handle) {
  nblex_correlation* corr = (nblex_correlation*)handle->data;
  if (!corr) {
    return;
  }

  /* Calculate cutoff time (current time - 2 * window) */
  uint64_t now = nblex_timestamp_now();
  uint64_t cutoff = now - (corr->window_ns * 2);

  /* Clean up old events from both buffers */
  cleanup_old_events(&corr->log_events, &corr->log_events_count, cutoff);
  cleanup_old_events(&corr->network_events, &corr->network_events_count, cutoff);
}
