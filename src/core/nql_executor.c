/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nql_executor.c - nQL (nblex Query Language) executor
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include "../parsers/nql_parser.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Aggregation bucket for group-by */
typedef struct nql_agg_bucket_s {
    char** group_keys;          /* Group key values */
    size_t group_keys_count;
    
    /* Aggregation state */
    uint64_t count;
    double sum;
    double min;
    double max;
    double sum_squares;         /* For stddev */
    size_t distinct_count;
    json_t* distinct_values;    /* JSON array for distinct tracking */
    
    /* Percentile tracking (simplified: store values, compute on flush) */
    json_t* percentile_values;  /* JSON array of numeric values */
    
    uint64_t window_start_ns;   /* Window start timestamp */
    uint64_t window_end_ns;     /* Window end timestamp */
    uint64_t last_event_ns;     /* Last event time for session windows */

    struct nql_agg_bucket_s* next;
} nql_agg_bucket_t;

/* Aggregation execution state */
typedef struct nql_agg_state_s {
    /* Window configuration (extracted from query) */
    nql_window_t window;
    
    /* Aggregation functions (copied from query) */
    nql_agg_func_t* funcs;
    size_t funcs_count;
    
    /* Group by fields (copied from query) */
    char** group_by_fields;
    size_t group_by_count;
    
    nql_agg_bucket_t* buckets;  /* Linked list of buckets */
    size_t bucket_count;
    
    /* Window management */
    uv_timer_t* window_timer;   /* Timer for window expiration */
    uint64_t last_flush_ns;     /* Last window flush time */
    bool timer_active;
} nql_agg_state_t;

/* Correlation execution state */
typedef struct nql_corr_state_s {
    nql_query_t* query;
    
    /* Event buffers */
    nblex_event_buffer_entry* left_events;
    nblex_event_buffer_entry* right_events;
    size_t left_count;
    size_t right_count;
    
    /* Cleanup timer */
    uv_timer_t* cleanup_timer;
    bool timer_active;
} nql_corr_state_t;

/* Query execution context */
typedef struct nql_exec_ctx_s {
    nblex_world* world;
    nql_query_t* query;
    char* query_string;  /* Cache query string for matching */
    
    union {
        nql_agg_state_t* agg_state;
        nql_corr_state_t* corr_state;
    } state;
    
    struct nql_exec_ctx_s* next;
} nql_exec_ctx_t;

/* Global execution contexts (per world) */
static nql_exec_ctx_t* exec_contexts = NULL;

/* Forward declarations */
static int execute_filter(nql_query_t* query, nblex_event* event);
static int execute_correlate(nql_query_t* query, nblex_event* event, nblex_world* world, const char* query_str);
static int execute_show(nql_query_t* query, nblex_event* event);
static int execute_aggregate(nql_query_t* query, nblex_event* event, nblex_world* world, const char* query_str);
static int execute_pipeline(nql_query_t* query, nblex_event* event, nblex_world* world, const char* query_str);
static int execute_query(nql_query_t* query, nblex_event* event, nblex_world* world, const char* query_str);

/* Helper: Get JSON value by dot-notation path */
static json_t* json_get_path(json_t* obj, const char* path) {
    if (!obj || !path || !json_is_object(obj)) {
        return NULL;
    }
    
    /* First try direct lookup (for flat keys like "log.service") */
    json_t* direct = json_object_get(obj, path);
    if (direct) {
        return direct;
    }
    
    /* Then try nested lookup */
    const char* dot = strchr(path, '.');
    if (!dot) {
        return json_object_get(obj, path);
    }
    
    size_t prefix_len = dot - path;
    char* prefix = malloc(prefix_len + 1);
    if (!prefix) {
        return NULL;
    }
    memcpy(prefix, path, prefix_len);
    prefix[prefix_len] = '\0';
    
    json_t* nested = json_object_get(obj, prefix);
    free(prefix);
    
    if (!nested || !json_is_object(nested)) {
        return NULL;
    }
    
    return json_get_path(nested, dot + 1);
}

/* Helper: Extract group key values from event */
static char** extract_group_keys(nql_agg_state_t* agg_state, nblex_event* event, size_t* count_out) {
    if (!agg_state || !event || !event->data) {
        *count_out = 0;
        return NULL;
    }
    
    if (agg_state->group_by_count == 0 || !agg_state->group_by_fields) {
        *count_out = 0;
        return NULL;
    }
    
    char** keys = calloc(agg_state->group_by_count, sizeof(char*));
    if (!keys) {
        *count_out = 0;
        return NULL;
    }
    
    for (size_t i = 0; i < agg_state->group_by_count; i++) {
        if (!agg_state->group_by_fields[i]) {
            keys[i] = strdup("null");
            continue;
        }
        json_t* value = json_get_path(event->data, agg_state->group_by_fields[i]);
        if (value) {
            if (json_is_string(value)) {
                keys[i] = strdup(json_string_value(value));
            } else if (json_is_integer(value)) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%lld", (long long)json_integer_value(value));
                keys[i] = strdup(buf);
            } else if (json_is_real(value)) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%.6f", json_real_value(value));
                keys[i] = strdup(buf);
            } else {
                keys[i] = strdup("null");
            }
        } else {
            keys[i] = strdup("null");
        }
    }
    
    *count_out = agg_state->group_by_count;
    return keys;
}

/* Helper: Compare group keys */
static int compare_group_keys(char** keys1, char** keys2, size_t count) {
    /* Both NULL or both empty means same (no group by) */
    if (count == 0) {
        return 0;
    }
    
    if (!keys1 || !keys2) {
        return keys1 ? 1 : (keys2 ? -1 : 0);
    }
    
    for (size_t i = 0; i < count; i++) {
        int cmp = strcmp(keys1[i], keys2[i]);
        if (cmp != 0) {
            return cmp;
        }
    }
    return 0;
}

/* Helper: Free group keys */
static void free_group_keys(char** keys, size_t count) {
    if (!keys) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(keys[i]);
    }
    free(keys);
}

/* Helper: Get numeric value from field */
static double get_numeric_value(json_t* obj, const char* field) {
    json_t* value = json_get_path(obj, field);
    if (!value) {
        return 0.0;
    }
    
    if (json_is_integer(value)) {
        return (double)json_integer_value(value);
    } else if (json_is_real(value)) {
        return json_real_value(value);
    }
    
    return 0.0;
}

/* Helper: Create aggregation result event */
static nblex_event* create_agg_result_event(nql_agg_bucket_t* bucket,
                                            nql_agg_state_t* agg_state,
                                            nblex_world* world) {
    nblex_event* event = nblex_event_new(NBLEX_EVENT_LOG, NULL);
    if (!event) {
        return NULL;
    }
    
    json_t* result = json_object();
    if (!result) {
        nblex_event_free(event);
        return NULL;
    }
    
    json_object_set_new(result, "nql_result_type", json_string("aggregation"));
    
    /* Add group keys */
    if (bucket->group_keys_count > 0 && agg_state->group_by_count > 0 && agg_state->group_by_fields) {
        json_t* groups = json_object();
        for (size_t i = 0; i < bucket->group_keys_count && i < agg_state->group_by_count; i++) {
            json_object_set_new(groups, agg_state->group_by_fields[i],
                               json_string(bucket->group_keys[i]));
        }
        json_object_set_new(result, "group", groups);
    }
    
    /* Add aggregation results */
    json_t* metrics = json_object();
    if (agg_state->funcs && agg_state->funcs_count > 0) {
        for (size_t i = 0; i < agg_state->funcs_count; i++) {
            nql_agg_func_t* func = &agg_state->funcs[i];
            const char* func_name = NULL;
            json_t* value = NULL;
            
            switch (func->type) {
            case NQL_AGG_COUNT:
                func_name = "count";
                value = json_integer(bucket->count);
                break;
                
            case NQL_AGG_SUM:
                if (func->field) {
                    func_name = func->field;
                    value = json_real(bucket->sum);
                }
                break;
                
            case NQL_AGG_AVG:
                if (func->field) {
                    char name[256];
                    snprintf(name, sizeof(name), "avg_%s", func->field);
                    func_name = name;
                    value = json_real(bucket->count > 0 ? bucket->sum / bucket->count : 0.0);
                }
                break;
                
            case NQL_AGG_MIN:
                if (func->field) {
                    char name[256];
                    snprintf(name, sizeof(name), "min_%s", func->field);
                    func_name = name;
                    value = json_real(bucket->min);
                }
                break;
                
            case NQL_AGG_MAX:
                if (func->field) {
                    char name[256];
                    snprintf(name, sizeof(name), "max_%s", func->field);
                    func_name = name;
                    value = json_real(bucket->max);
                }
                break;
                
            case NQL_AGG_PERCENTILE:
                if (func->field && bucket->percentile_values &&
                    json_array_size(bucket->percentile_values) > 0) {
                    /* Simple percentile: sort and pick */
                    size_t size = json_array_size(bucket->percentile_values);
                    double* values = malloc(size * sizeof(double));
                    if (values) {
                        for (size_t j = 0; j < size; j++) {
                            json_t* v = json_array_get(bucket->percentile_values, j);
                            values[j] = json_is_number(v) ? json_number_value(v) : 0.0;
                        }
                        /* Simple sort */
                        for (size_t j = 0; j < size - 1; j++) {
                            for (size_t k = j + 1; k < size; k++) {
                                if (values[j] > values[k]) {
                                    double tmp = values[j];
                                    values[j] = values[k];
                                    values[k] = tmp;
                                }
                            }
                        }
                        size_t idx = (size_t)((func->percentile / 100.0) * size);
                        if (idx >= size) idx = size - 1;
                        char name[256];
                        snprintf(name, sizeof(name), "p%.0f_%s", func->percentile, func->field);
                        func_name = name;
                        value = json_real(values[idx]);
                        free(values);
                    }
                }
                break;
                
            case NQL_AGG_DISTINCT:
                if (func->field) {
                    char name[256];
                    snprintf(name, sizeof(name), "distinct_%s", func->field);
                    func_name = name;
                    value = json_integer(bucket->distinct_count);
                }
                break;
            }
            
            if (func_name && value) {
                json_object_set_new(metrics, func_name, value);
            }
        }
    }
    
    json_object_set_new(result, "metrics", metrics);
    
    /* Window info */
    if (agg_state->window.type != NQL_WINDOW_NONE) {
        json_t* window_info = json_object();
        json_object_set_new(window_info, "start_ns", json_integer(bucket->window_start_ns));
        json_object_set_new(window_info, "end_ns", json_integer(bucket->window_end_ns));
        json_object_set_new(result, "window", window_info);
    }
    
    event->data = result;
    return event;
}

/* Helper: Free bucket resources */
static void free_bucket_resources(nql_agg_bucket_t* bucket) {
    if (!bucket) {
        return;
    }
    
    if (bucket->group_keys) {
        free_group_keys(bucket->group_keys, bucket->group_keys_count);
        bucket->group_keys = NULL;
    }
    if (bucket->distinct_values) {
        json_decref(bucket->distinct_values);
        bucket->distinct_values = NULL;
    }
    if (bucket->percentile_values) {
        json_decref(bucket->percentile_values);
        bucket->percentile_values = NULL;
    }
}

/* Helper: Remove bucket from list */
static void remove_bucket_from_list(nql_agg_state_t* agg_state, nql_agg_bucket_t* bucket_to_remove) {
    if (!agg_state || !bucket_to_remove) {
        return;
    }
    
    nql_agg_bucket_t** prev_ptr = &agg_state->buckets;
    nql_agg_bucket_t* current = agg_state->buckets;
    
    while (current) {
        if (current == bucket_to_remove) {
            *prev_ptr = current->next;
            free_bucket_resources(current);
            free(current);
            agg_state->bucket_count--;
            return;
        }
        prev_ptr = &current->next;
        current = current->next;
    }
}

/* Window flush callback */
static void window_flush_cb(uv_timer_t* handle) {
    nql_agg_state_t* agg_state = (nql_agg_state_t*)handle->data;
    if (!agg_state) {
        return;
    }
    
    nblex_world* world = NULL;
    
    /* Find world from context */
    nql_exec_ctx_t* ctx = exec_contexts;
    while (ctx) {
        if (ctx->state.agg_state == agg_state) {
            world = ctx->world;
            break;
        }
        ctx = ctx->next;
    }
    
    if (!world) {
        return;
    }
    
    uint64_t now = nblex_timestamp_now();
    uint64_t window_size_ns = agg_state->window.size_ms * 1000000ULL;

    /* Process all buckets */
    nql_agg_bucket_t* bucket = agg_state->buckets;
    nql_agg_bucket_t* next_bucket = NULL;
    
    while (bucket) {
        next_bucket = bucket->next; /* Save next before potential removal */
        bool should_flush = false;
        bool should_remove = false;

        if (agg_state->window.type == NQL_WINDOW_SESSION) {
            /* Session window: flush if timeout since last event */
            uint64_t timeout_ns = agg_state->window.timeout_ms * 1000000ULL;
            if (bucket->count > 0 && (now - bucket->last_event_ns) >= timeout_ns) {
                should_flush = true;
                should_remove = true; /* Remove after flush */
            }
        } else if (agg_state->window.type == NQL_WINDOW_TUMBLING) {
            /* Tumbling window: flush if window end reached, then reset for next window */
            if (bucket->window_end_ns <= now) {
                if (bucket->count > 0) {
                    should_flush = true;
                }
                /* Reset for next window */
                bucket->count = 0;
                bucket->sum = 0.0;
                bucket->min = INFINITY;
                bucket->max = -INFINITY;
                bucket->sum_squares = 0.0;
                bucket->distinct_count = 0;
                if (bucket->distinct_values) {
                    json_decref(bucket->distinct_values);
                    bucket->distinct_values = json_array();
                }
                if (bucket->percentile_values) {
                    json_decref(bucket->percentile_values);
                    bucket->percentile_values = json_array();
                }
                /* Calculate next window start */
                uint64_t windows_passed = (now - bucket->window_start_ns) / window_size_ns;
                bucket->window_start_ns = bucket->window_start_ns + (windows_passed * window_size_ns);
                bucket->window_end_ns = bucket->window_start_ns + window_size_ns;
            }
        } else if (agg_state->window.type == NQL_WINDOW_SLIDING) {
            /* Sliding window: flush if window end reached, then remove */
            if (bucket->window_end_ns <= now) {
                if (bucket->count > 0) {
                    should_flush = true;
                }
                should_remove = true; /* Remove expired sliding window */
            }
        }

        if (should_flush) {
            nblex_event* result_event = create_agg_result_event(bucket, agg_state, world);
            if (result_event) {
                nblex_event_emit(world, result_event);
            }
        }
        
        if (should_remove) {
            remove_bucket_from_list(agg_state, bucket);
        }
        
        bucket = next_bucket;
    }
    
    agg_state->last_flush_ns = now;
}

/* Helper: Copy aggregation configuration from query to state */
static int copy_agg_config(nql_agg_state_t* agg_state, nql_aggregate_t* agg) {
    if (!agg_state || !agg) {
        return -1;
    }
    
    /* Copy window configuration */
    agg_state->window = agg->window;
    
    /* Copy aggregation functions */
    agg_state->funcs_count = agg->funcs_count;
    if (agg->funcs_count > 0 && agg->funcs) {
        agg_state->funcs = calloc(agg->funcs_count, sizeof(nql_agg_func_t));
        if (!agg_state->funcs) {
            return -1;
        }
        for (size_t i = 0; i < agg->funcs_count; i++) {
            agg_state->funcs[i] = agg->funcs[i];
            agg_state->funcs[i].field = NULL; /* Initialize to NULL */
            /* Copy field string if present */
            if (agg->funcs[i].field) {
                agg_state->funcs[i].field = strdup(agg->funcs[i].field);
                if (!agg_state->funcs[i].field) {
                    /* Free already copied fields */
                    for (size_t j = 0; j < i; j++) {
                        free(agg_state->funcs[j].field);
                    }
                    free(agg_state->funcs);
                    agg_state->funcs = NULL;
                    return -1;
                }
            }
        }
    } else {
        agg_state->funcs = NULL;
        agg_state->funcs_count = 0;
    }
    
    /* Copy group by fields */
    agg_state->group_by_count = agg->group_by_count;
    if (agg->group_by_count > 0 && agg->group_by_fields) {
        agg_state->group_by_fields = calloc(agg->group_by_count, sizeof(char*));
        if (!agg_state->group_by_fields) {
            /* Free funcs */
            if (agg_state->funcs) {
                for (size_t i = 0; i < agg_state->funcs_count; i++) {
                    free(agg_state->funcs[i].field);
                }
                free(agg_state->funcs);
            }
            return -1;
        }
        for (size_t i = 0; i < agg->group_by_count; i++) {
            if (agg->group_by_fields[i]) {
                agg_state->group_by_fields[i] = strdup(agg->group_by_fields[i]);
            } else {
                agg_state->group_by_fields[i] = NULL;
            }
            if (agg->group_by_fields[i] && !agg_state->group_by_fields[i]) {
                /* strdup failed */
                /* Free already copied fields */
                for (size_t j = 0; j < i; j++) {
                    free(agg_state->group_by_fields[j]);
                }
                free(agg_state->group_by_fields);
                /* Free funcs */
                if (agg_state->funcs) {
                    for (size_t j = 0; j < agg_state->funcs_count; j++) {
                        free(agg_state->funcs[j].field);
                    }
                    free(agg_state->funcs);
                }
                return -1;
            }
        }
    } else {
        agg_state->group_by_fields = NULL;
        agg_state->group_by_count = 0;
    }
    
    /* Note: where_filter is checked before getting state, so we don't need to store it */
    
    return 0;
}

/* Helper: Free aggregation state resources */
static void free_agg_state_resources(nql_agg_state_t* agg_state) {
    if (!agg_state) {
        return;
    }
    
    /* Free aggregation functions */
    if (agg_state->funcs) {
        for (size_t i = 0; i < agg_state->funcs_count; i++) {
            free(agg_state->funcs[i].field);
        }
        free(agg_state->funcs);
        agg_state->funcs = NULL;
    }
    
    /* Free group by fields */
    if (agg_state->group_by_fields) {
        for (size_t i = 0; i < agg_state->group_by_count; i++) {
            free(agg_state->group_by_fields[i]);
        }
        free(agg_state->group_by_fields);
        agg_state->group_by_fields = NULL;
    }
    
    /* Note: where_filter is not freed here - it's owned by the query */
}

/* Get or create aggregation state */
static nql_agg_state_t* get_agg_state(nql_query_t* query, nblex_world* world, const char* query_str) {
    if (!query || query->type != NQL_QUERY_AGGREGATE || !world) {
        return NULL;
    }
    
    nql_aggregate_t* agg = query->data.aggregate;
    if (!agg) {
        return NULL;
    }
    
    /* Check existing contexts by query string */
    if (query_str) {
        nql_exec_ctx_t* ctx = exec_contexts;
        while (ctx) {
            if (ctx->query_string && strcmp(ctx->query_string, query_str) == 0 &&
                ctx->world == world && ctx->state.agg_state) {
                /* State already exists, return it */
                return ctx->state.agg_state;
            }
            ctx = ctx->next;
        }
    }
    
    /* Create new context and state */
    nql_exec_ctx_t* new_ctx = calloc(1, sizeof(nql_exec_ctx_t));
    if (!new_ctx) {
        return NULL;
    }
    
    nql_agg_state_t* agg_state = calloc(1, sizeof(nql_agg_state_t));
    if (!agg_state) {
        free(new_ctx);
        return NULL;
    }
    
    agg_state->buckets = NULL;
    agg_state->bucket_count = 0;
    agg_state->timer_active = false;
    
    /* Copy aggregation configuration from query */
    if (copy_agg_config(agg_state, agg) != 0) {
        free(agg_state);
        free(new_ctx);
        return NULL;
    }
    
    /* Initialize window timer if needed */
    /* Only create timer if world loop is initialized AND started */
    if (agg_state->window.type != NQL_WINDOW_NONE && world->loop && world->started) {
        agg_state->window_timer = calloc(1, sizeof(uv_timer_t));
        if (agg_state->window_timer) {
            uv_timer_init(world->loop, agg_state->window_timer);
            agg_state->window_timer->data = agg_state;

            uint64_t interval_ms;
            if (agg_state->window.type == NQL_WINDOW_SESSION) {
                /* Session window: check frequently for timeout */
                interval_ms = agg_state->window.timeout_ms / 2;
                if (interval_ms < 100) interval_ms = 100;
            } else {
                /* Tumbling/sliding window */
                interval_ms = agg_state->window.slide_ms > 0 ?
                              agg_state->window.slide_ms : agg_state->window.size_ms;
            }

            uv_timer_start(agg_state->window_timer, window_flush_cb,
                          interval_ms, interval_ms);
            agg_state->timer_active = true;
        }
    }
    
    new_ctx->world = world;
    new_ctx->query = query;  /* Store for reference, but state doesn't depend on it */
    new_ctx->query_string = query_str ? strdup(query_str) : NULL;
    new_ctx->state.agg_state = agg_state;
    new_ctx->next = exec_contexts;
    exec_contexts = new_ctx;
    
    return agg_state;
}

/* Helper: Find bucket by group keys and window start */
static nql_agg_bucket_t* find_bucket_by_window(nql_agg_state_t* agg_state,
                                               char** group_keys,
                                               size_t group_keys_count,
                                               uint64_t window_start_ns) {
    nql_agg_bucket_t* bucket = agg_state->buckets;
    while (bucket) {
        if (compare_group_keys(bucket->group_keys, group_keys, group_keys_count) == 0 &&
            bucket->window_start_ns == window_start_ns) {
            return bucket;
        }
        bucket = bucket->next;
    }
    return NULL;
}

/* Helper: Create a new bucket with specified window */
static nql_agg_bucket_t* create_bucket_with_window(nql_agg_state_t* agg_state,
                                                    char** group_keys,
                                                    size_t group_keys_count,
                                                    uint64_t window_start_ns,
                                                    uint64_t window_end_ns) {
    nql_agg_bucket_t* bucket = calloc(1, sizeof(nql_agg_bucket_t));
    if (!bucket) {
        return NULL;
    }
    
    bucket->group_keys = group_keys;
    bucket->group_keys_count = group_keys_count;
    bucket->count = 0;
    bucket->sum = 0.0;
    bucket->min = INFINITY;
    bucket->max = -INFINITY;
    bucket->sum_squares = 0.0;
    bucket->distinct_count = 0;
    bucket->distinct_values = json_array();
    bucket->percentile_values = json_array();
    bucket->window_start_ns = window_start_ns;
    bucket->window_end_ns = window_end_ns;
    bucket->last_event_ns = window_start_ns;
    
    bucket->next = agg_state->buckets;
    agg_state->buckets = bucket;
    agg_state->bucket_count++;
    
    return bucket;
}

/* Get or create bucket(s) for group keys and event timestamp */
static int get_or_create_buckets_for_event(nql_agg_state_t* agg_state,
                                           char** group_keys,
                                           size_t group_keys_count,
                                           uint64_t event_timestamp_ns,
                                           nql_agg_bucket_t*** buckets_out,
                                           size_t* buckets_count_out) {
    if (!agg_state || !buckets_out || !buckets_count_out) {
        if (group_keys) {
            free_group_keys(group_keys, group_keys_count);
        }
        return -1;
    }
    
    *buckets_count_out = 0;
    *buckets_out = NULL;
    
    if (agg_state->window.type == NQL_WINDOW_NONE) {
        /* No windowing: one bucket per group key (or single bucket if no group by) */
        /* Search for existing bucket by group keys only (ignore window) */
        nql_agg_bucket_t* bucket = agg_state->buckets;
        while (bucket) {
            if (compare_group_keys(bucket->group_keys, group_keys, group_keys_count) == 0 &&
                bucket->window_start_ns == 0 && bucket->window_end_ns == UINT64_MAX) {
                /* Found existing no-window bucket */
                if (group_keys) {
                    free_group_keys(group_keys, group_keys_count);
                }
                *buckets_out = calloc(1, sizeof(nql_agg_bucket_t*));
                if (!*buckets_out) {
                    return -1;
                }
                (*buckets_out)[0] = bucket;
                *buckets_count_out = 1;
                return 0;
            }
            bucket = bucket->next;
        }
        
        /* Create bucket with no window */
        bucket = create_bucket_with_window(agg_state, group_keys, group_keys_count, 0, UINT64_MAX);
        if (!bucket) {
            if (group_keys) {
                free_group_keys(group_keys, group_keys_count);
            }
            return -1;
        }
        
        *buckets_out = calloc(1, sizeof(nql_agg_bucket_t*));
        if (!*buckets_out) {
            return -1;
        }
        (*buckets_out)[0] = bucket;
        *buckets_count_out = 1;
        return 0;
    }
    
    if (agg_state->window.type == NQL_WINDOW_SESSION) {
        /* Session window: find or create bucket for this group */
        nql_agg_bucket_t* bucket = NULL;
        nql_agg_bucket_t* iter = agg_state->buckets;
        uint64_t timeout_ns = agg_state->window.timeout_ms * 1000000ULL;
        
        /* Find existing session bucket for this group */
        while (iter) {
            if (compare_group_keys(iter->group_keys, group_keys, group_keys_count) == 0 &&
                iter->window_end_ns == UINT64_MAX) { /* Session windows have UINT64_MAX as end */
                /* Check if event is within session timeout */
                if (event_timestamp_ns >= iter->last_event_ns &&
                    (event_timestamp_ns - iter->last_event_ns) < timeout_ns) {
                    bucket = iter;
                    break;
                }
            }
            iter = iter->next;
        }
        
        if (!bucket) {
            /* Create new session window */
            bucket = create_bucket_with_window(agg_state, group_keys, group_keys_count,
                                             event_timestamp_ns, UINT64_MAX);
            if (!bucket) {
                if (group_keys) {
                    free_group_keys(group_keys, group_keys_count);
                }
                return -1;
            }
        } else {
            if (group_keys) {
                free_group_keys(group_keys, group_keys_count);
            }
        }
        
        *buckets_out = calloc(1, sizeof(nql_agg_bucket_t*));
        if (!*buckets_out) {
            return -1;
        }
        (*buckets_out)[0] = bucket;
        *buckets_count_out = 1;
        return 0;
    }
    
    /* Tumbling or sliding window */
    uint64_t window_size_ns = agg_state->window.size_ms * 1000000ULL;
    uint64_t slide_ns = agg_state->window.slide_ms > 0 ? agg_state->window.slide_ms * 1000000ULL : window_size_ns;
    
    if (agg_state->window.type == NQL_WINDOW_TUMBLING) {
        /* Tumbling: event belongs to one window, aligned to window boundaries */
        uint64_t window_start = (event_timestamp_ns / window_size_ns) * window_size_ns;
        nql_agg_bucket_t* bucket = find_bucket_by_window(agg_state, group_keys, group_keys_count, window_start);
        if (!bucket) {
            bucket = create_bucket_with_window(agg_state, group_keys, group_keys_count,
                                             window_start, window_start + window_size_ns);
            if (!bucket) {
                if (group_keys) {
                    free_group_keys(group_keys, group_keys_count);
                }
                return -1;
            }
        } else {
            if (group_keys) {
                free_group_keys(group_keys, group_keys_count);
            }
        }
        
        *buckets_out = calloc(1, sizeof(nql_agg_bucket_t*));
        if (!*buckets_out) {
            return -1;
        }
        (*buckets_out)[0] = bucket;
        *buckets_count_out = 1;
        return 0;
    }
    
    /* Sliding window: event may belong to multiple windows */
    /* Find all windows that contain this event */
    /* Windows are aligned to slide boundaries */
    uint64_t window_start = ((event_timestamp_ns / slide_ns) * slide_ns);
    
    /* Calculate how many windows can contain this event */
    /* A window contains an event if: window_start <= event_ts < window_start + window_size */
    /* So we need windows where: window_start <= event_ts AND window_start + window_size > event_ts */
    /* Which means: window_start > event_ts - window_size AND window_start <= event_ts */
    uint64_t earliest_window_start = (event_timestamp_ns >= window_size_ns) ?
                                     (event_timestamp_ns - window_size_ns) : 0;
    earliest_window_start = ((earliest_window_start / slide_ns) * slide_ns);
    
    size_t max_windows = ((window_start - earliest_window_start) / slide_ns) + 1;
    if (max_windows > 1000) {
        max_windows = 1000; /* Safety limit */
    }
    
    nql_agg_bucket_t** buckets = calloc(max_windows, sizeof(nql_agg_bucket_t*));
    if (!buckets) {
        if (group_keys) {
            free_group_keys(group_keys, group_keys_count);
        }
        return -1;
    }
    
    size_t count = 0;
    uint64_t current_window_start = earliest_window_start;
    bool keys_owned_by_bucket = false;
    
    while (current_window_start <= event_timestamp_ns &&
           current_window_start + window_size_ns > event_timestamp_ns &&
           count < max_windows) {
        nql_agg_bucket_t* bucket = find_bucket_by_window(agg_state, group_keys, group_keys_count, current_window_start);
        if (!bucket) {
            bucket = create_bucket_with_window(agg_state, group_keys, group_keys_count,
                                             current_window_start, current_window_start + window_size_ns);
            if (!bucket) {
                free(buckets);
                if (!keys_owned_by_bucket && group_keys) {
                    free_group_keys(group_keys, group_keys_count);
                }
                return -1;
            }
            keys_owned_by_bucket = true; /* Keys now owned by bucket */
        }
        buckets[count++] = bucket;
        
        current_window_start += slide_ns;
    }
    
    /* Free group keys if they weren't used by any bucket */
    if (!keys_owned_by_bucket && count > 0) {
        /* Check if any bucket owns these keys */
        for (size_t i = 0; i < count; i++) {
            if (buckets[i]->group_keys == group_keys) {
                keys_owned_by_bucket = true;
                break;
            }
        }
    }
    
    if (!keys_owned_by_bucket && group_keys) {
        free_group_keys(group_keys, group_keys_count);
    }
    
    *buckets_out = buckets;
    *buckets_count_out = count;
    return 0;
}

/* Helper: Update bucket with event data */
static void update_bucket_with_event(nql_agg_bucket_t* bucket, nblex_event* event, nql_agg_state_t* agg_state) {
    if (!bucket || !event || !agg_state) {
        return;
    }
    
    bucket->count++;
    
    /* Update last event time for session windows */
    if (agg_state->window.type == NQL_WINDOW_SESSION) {
        uint64_t event_ts = event->timestamp_ns ? event->timestamp_ns : nblex_timestamp_now();
        if (event_ts > bucket->last_event_ns) {
            bucket->last_event_ns = event_ts;
        }
    }
    
    if (agg_state->funcs && agg_state->funcs_count > 0) {
        for (size_t i = 0; i < agg_state->funcs_count; i++) {
            nql_agg_func_t* func = &agg_state->funcs[i];
            
            if (func->type == NQL_AGG_COUNT) {
                /* Already counted */
                continue;
            }
            
            if (!func->field) {
                continue;
            }
            
            double value = get_numeric_value(event->data, func->field);
            
            switch (func->type) {
                case NQL_AGG_SUM:
                case NQL_AGG_AVG:
                    bucket->sum += value;
                    bucket->sum_squares += value * value;
                    if (value < bucket->min) bucket->min = value;
                    if (value > bucket->max) bucket->max = value;
                    break;
                    
                case NQL_AGG_MIN:
                    if (value < bucket->min) bucket->min = value;
                    break;
                    
                case NQL_AGG_MAX:
                    if (value > bucket->max) bucket->max = value;
                    break;
                    
                case NQL_AGG_PERCENTILE:
                    if (bucket->percentile_values) {
                        json_array_append_new(bucket->percentile_values, json_real(value));
                    }
                    break;
                    
                case NQL_AGG_DISTINCT:
                    /* Simple distinct tracking */
                    if (bucket->distinct_values) {
                        json_t* val = json_real(value);
                        bool found = false;
                        size_t size = json_array_size(bucket->distinct_values);
                        for (size_t j = 0; j < size; j++) {
                            json_t* existing = json_array_get(bucket->distinct_values, j);
                            if (json_is_number(existing) &&
                                fabs(json_number_value(existing) - value) < 1e-9) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            json_array_append(bucket->distinct_values, val);
                            bucket->distinct_count++;
                        }
                        json_decref(val);
                    }
                    break;
                    
                case NQL_AGG_COUNT:
                    /* COUNT is handled by bucket->count increment above */
                    break;
                    
                default:
                    break;
            }
        }
    }
}

/* Execute aggregate query */
static int execute_aggregate(nql_query_t* query, nblex_event* event, nblex_world* world, const char* query_str) {
    if (!query || query->type != NQL_QUERY_AGGREGATE || !query->data.aggregate || !event) {
        return 0;
    }
    
    nql_aggregate_t* agg = query->data.aggregate;
    
    /* Check WHERE clause */
    if (agg->where_filter) {
        if (!nblex_filter_matches(agg->where_filter, event)) {
            return 0;
        }
    }
    
    /* Get aggregation state */
    nql_agg_state_t* agg_state = get_agg_state(query, world, query_str);
    if (!agg_state) {
        return 0;
    }
    
    /* Extract group keys */
    size_t group_keys_count = 0;
    char** group_keys = extract_group_keys(agg_state, event, &group_keys_count);
    
    /* Get event timestamp */
    uint64_t event_timestamp_ns = event->timestamp_ns ? event->timestamp_ns : nblex_timestamp_now();
    
    /* Get or create buckets for this event */
    nql_agg_bucket_t** buckets = NULL;
    size_t buckets_count = 0;
    if (get_or_create_buckets_for_event(agg_state, group_keys, group_keys_count,
                                        event_timestamp_ns, &buckets, &buckets_count) != 0) {
        return 0;
    }
    
    /* Update all buckets with event data */
    for (size_t i = 0; i < buckets_count; i++) {
        update_bucket_with_event(buckets[i], event, agg_state);
    }
    
    /* For non-windowed queries, emit immediately */
    nblex_event* result_event = NULL;
    if (agg_state->window.type == NQL_WINDOW_NONE && buckets_count > 0) {
        result_event = create_agg_result_event(buckets[0], agg_state, world);
    }
    
    free(buckets);
    
    if (result_event) {
        nblex_event_emit(world, result_event);
    }
    
    return 1;
}

/* Execute filter query */
static int execute_filter(nql_query_t* query, nblex_event* event) {
    if (!query || query->type != NQL_QUERY_FILTER || !query->data.filter) {
        return 0;
    }
    
    return nblex_filter_matches(query->data.filter, event);
}

/* Correlation buffer management */
static int add_corr_event(nql_corr_state_t* corr_state, nblex_event* event, bool is_left) {
    nblex_event_buffer_entry* entry = calloc(1, sizeof(nblex_event_buffer_entry));
    if (!entry) {
        return -1;
    }
    
    /* Duplicate event */
    nblex_event* event_copy = calloc(1, sizeof(nblex_event));
    if (!event_copy) {
        free(entry);
        return -1;
    }
    
    memcpy(event_copy, event, sizeof(nblex_event));
    if (event_copy->data) {
        json_incref(event_copy->data);
    }
    
    entry->event = event_copy;
    
    if (is_left) {
        entry->next = corr_state->left_events;
        corr_state->left_events = entry;
        corr_state->left_count++;
    } else {
        entry->next = corr_state->right_events;
        corr_state->right_events = entry;
        corr_state->right_count++;
    }
    
    return 0;
}

/* Correlation cleanup callback */
static void corr_cleanup_cb(uv_timer_t* handle) {
    nql_corr_state_t* corr_state = (nql_corr_state_t*)handle->data;
    if (!corr_state || !corr_state->query || !corr_state->query->data.correlate) {
        return;
    }
    
    uint64_t now = nblex_timestamp_now();
    uint64_t window_ns = corr_state->query->data.correlate->within_ms * 1000000ULL;
    uint64_t cutoff = now - (window_ns * 2);
    
    /* Clean left buffer */
    nblex_event_buffer_entry** entry_ptr = &corr_state->left_events;
    while (*entry_ptr) {
        if ((*entry_ptr)->event->timestamp_ns < cutoff) {
            nblex_event_buffer_entry* old = *entry_ptr;
            *entry_ptr = old->next;
            nblex_event_free(old->event);
            free(old);
            corr_state->left_count--;
        } else {
            entry_ptr = &(*entry_ptr)->next;
        }
    }
    
    /* Clean right buffer */
    entry_ptr = &corr_state->right_events;
    while (*entry_ptr) {
        if ((*entry_ptr)->event->timestamp_ns < cutoff) {
            nblex_event_buffer_entry* old = *entry_ptr;
            *entry_ptr = old->next;
            nblex_event_free(old->event);
            free(old);
            corr_state->right_count--;
        } else {
            entry_ptr = &(*entry_ptr)->next;
        }
    }
}

/* Get or create correlation state */
static nql_corr_state_t* get_corr_state(nql_query_t* query, nblex_world* world, const char* query_str) {
    if (!query || query->type != NQL_QUERY_CORRELATE || !world) {
        return NULL;
    }
    
    /* Check existing contexts by query string */
    if (query_str) {
        nql_exec_ctx_t* ctx = exec_contexts;
        while (ctx) {
            if (ctx->query_string && strcmp(ctx->query_string, query_str) == 0 &&
                ctx->world == world && ctx->state.corr_state) {
                return ctx->state.corr_state;
            }
            ctx = ctx->next;
        }
    }
    
    /* Check existing contexts by query pointer */
    nql_exec_ctx_t* ctx = exec_contexts;
    while (ctx) {
        if (ctx->query == query && ctx->world == world && ctx->state.corr_state) {
            return ctx->state.corr_state;
        }
        ctx = ctx->next;
    }
    
    /* Create new context and state */
    nql_exec_ctx_t* new_ctx = calloc(1, sizeof(nql_exec_ctx_t));
    if (!new_ctx) {
        return NULL;
    }
    
    nql_corr_state_t* corr_state = calloc(1, sizeof(nql_corr_state_t));
    if (!corr_state) {
        free(new_ctx);
        return NULL;
    }
    
    corr_state->query = query;
    corr_state->left_events = NULL;
    corr_state->right_events = NULL;
    corr_state->left_count = 0;
    corr_state->right_count = 0;
    corr_state->timer_active = false;
    
    /* Initialize cleanup timer */
    if (world->loop) {
        corr_state->cleanup_timer = calloc(1, sizeof(uv_timer_t));
        if (corr_state->cleanup_timer) {
            uv_timer_init(world->loop, corr_state->cleanup_timer);
            corr_state->cleanup_timer->data = corr_state;
            uv_timer_start(corr_state->cleanup_timer, corr_cleanup_cb, 1000, 1000);
            corr_state->timer_active = true;
        }
    }
    
    new_ctx->world = world;
    new_ctx->query = query;
    new_ctx->query_string = query_str ? strdup(query_str) : NULL;
    new_ctx->state.corr_state = corr_state;
    new_ctx->next = exec_contexts;
    exec_contexts = new_ctx;
    
    return corr_state;
}

/* Create correlation result event */
static nblex_event* create_corr_result_event(nblex_event* left_event,
                                            nblex_event* right_event,
                                            nql_correlate_t* corr,
                                            nblex_world* world) {
    nblex_event* result = nblex_event_new(NBLEX_EVENT_CORRELATION, NULL);
    if (!result) {
        return NULL;
    }
    
    json_t* corr_data = json_object();
    if (!corr_data) {
        nblex_event_free(result);
        return NULL;
    }
    
    json_object_set_new(corr_data, "nql_result_type", json_string("correlation"));
    json_object_set_new(corr_data, "window_ms", json_integer(corr->within_ms));
    
    if (left_event->data) {
        json_object_set(corr_data, "left_event", left_event->data);
    }
    if (right_event->data) {
        json_object_set(corr_data, "right_event", right_event->data);
    }
    
    int64_t time_diff_ns = (int64_t)left_event->timestamp_ns - (int64_t)right_event->timestamp_ns;
    json_object_set_new(corr_data, "time_diff_ms", json_real(time_diff_ns / 1000000.0));
    
    result->data = corr_data;
    result->timestamp_ns = left_event->timestamp_ns > right_event->timestamp_ns ?
                         left_event->timestamp_ns : right_event->timestamp_ns;
    
    return result;
}

/* Execute correlation query */
static int execute_correlate(nql_query_t* query, nblex_event* event, nblex_world* world, const char* query_str) {
    if (!query || query->type != NQL_QUERY_CORRELATE || !query->data.correlate || !event) {
        return 0;
    }
    
    nql_correlate_t* corr = query->data.correlate;
    bool matches_left = corr->left_filter && nblex_filter_matches(corr->left_filter, event);
    bool matches_right = corr->right_filter && nblex_filter_matches(corr->right_filter, event);
    
    if (!matches_left && !matches_right) {
        return 0;
    }
    
    /* Get correlation state */
    nql_corr_state_t* corr_state = get_corr_state(query, world, query_str);
    if (!corr_state) {
        return 0;
    }
    
    uint64_t window_ns = corr->within_ms * 1000000ULL;
    uint64_t now = event->timestamp_ns;
    
    /* Check for matches */
    if (matches_left) {
        add_corr_event(corr_state, event, true);
        
        /* Check against right buffer */
        nblex_event_buffer_entry* entry = corr_state->right_events;
        while (entry) {
            int64_t diff = (int64_t)now - (int64_t)entry->event->timestamp_ns;
            if (llabs(diff) <= (int64_t)window_ns) {
                nblex_event* result = create_corr_result_event(event, entry->event, corr, world);
                if (result) {
                    nblex_event_emit(world, result);
                }
            }
            entry = entry->next;
        }
    }
    
    if (matches_right) {
        add_corr_event(corr_state, event, false);
        
        /* Check against left buffer */
        nblex_event_buffer_entry* entry = corr_state->left_events;
        while (entry) {
            int64_t diff = (int64_t)now - (int64_t)entry->event->timestamp_ns;
            if (llabs(diff) <= (int64_t)window_ns) {
                nblex_event* result = create_corr_result_event(entry->event, event, corr, world);
                if (result) {
                    nblex_event_emit(world, result);
                }
            }
            entry = entry->next;
        }
    }
    
    return 1;
}

/* Execute show query */
static int execute_show(nql_query_t* query, nblex_event* event) {
    if (!query || query->type != NQL_QUERY_SHOW || !query->data.show) {
        return 0;
    }
    
    /* If there's a WHERE clause, check it */
    if (query->data.show->where_filter) {
        if (!nblex_filter_matches(query->data.show->where_filter, event)) {
            return 0;
        }
    }
    
    /* Show query matches if WHERE clause passes (or no WHERE clause) */
    return 1;
}

/* Execute pipeline query */
static int execute_pipeline(nql_query_t* query, nblex_event* event, nblex_world* world, const char* query_str) {
    if (!query || query->type != NQL_QUERY_PIPELINE || !query->data.pipeline.stages) {
        return 0;
    }

    for (size_t i = 0; i < query->data.pipeline.count; i++) {
        if (!execute_query(query->data.pipeline.stages[i], event, world, query_str)) {
            return 0;
        }
    }

    return 1;
}

/* Execute query (main entry point) */
int execute_query(nql_query_t* query, nblex_event* event, nblex_world* world, const char* query_str) {
    if (!query || !event) {
        return 0;
    }
    
    switch (query->type) {
        case NQL_QUERY_FILTER:
            return execute_filter(query, event);
            
        case NQL_QUERY_CORRELATE:
            return execute_correlate(query, event, world, query_str);
            
        case NQL_QUERY_SHOW:
            return execute_show(query, event);
            
        case NQL_QUERY_AGGREGATE:
            return execute_aggregate(query, event, world, query_str);
            
        case NQL_QUERY_PIPELINE:
            return execute_pipeline(query, event, world, query_str);
            
        default:
            return 0;
    }
}

/* Public API: Create query from string and execute on event */
int nql_execute(const char* query_str, nblex_event* event, nblex_world* world) {
    if (!query_str || !event) {
        return 0;
    }
    
    nql_query_t* query = nql_parse(query_str);
    if (!query) {
        return 0;
    }
    
    int result = execute_query(query, event, world, query_str);
    nql_free(query);
    
    return result;
}
