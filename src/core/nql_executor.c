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
    
    struct nql_agg_bucket_s* next;
} nql_agg_bucket_t;

/* Aggregation execution state */
typedef struct nql_agg_state_s {
    nql_query_t* query;         /* Reference to query AST */
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
static char** extract_group_keys(nql_aggregate_t* agg, nblex_event* event, size_t* count_out) {
    if (!agg || !event || !event->data || agg->group_by_count == 0) {
        *count_out = 0;
        return NULL;
    }
    
    char** keys = calloc(agg->group_by_count, sizeof(char*));
    if (!keys) {
        *count_out = 0;
        return NULL;
    }
    
    for (size_t i = 0; i < agg->group_by_count; i++) {
        json_t* value = json_get_path(event->data, agg->group_by_fields[i]);
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
    
    *count_out = agg->group_by_count;
    return keys;
}

/* Helper: Compare group keys */
static int compare_group_keys(char** keys1, char** keys2, size_t count) {
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
                                            nql_aggregate_t* agg,
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
    if (bucket->group_keys_count > 0) {
        json_t* groups = json_object();
        for (size_t i = 0; i < bucket->group_keys_count && i < agg->group_by_count; i++) {
            json_object_set_new(groups, agg->group_by_fields[i],
                               json_string(bucket->group_keys[i]));
        }
        json_object_set_new(result, "group", groups);
    }
    
    /* Add aggregation results */
    json_t* metrics = json_object();
    for (size_t i = 0; i < agg->funcs_count; i++) {
        nql_agg_func_t* func = &agg->funcs[i];
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
    
    json_object_set_new(result, "metrics", metrics);
    
    /* Window info */
    if (agg->window.type != NQL_WINDOW_NONE) {
        json_t* window_info = json_object();
        json_object_set_new(window_info, "start_ns", json_integer(bucket->window_start_ns));
        json_object_set_new(window_info, "end_ns", json_integer(bucket->window_end_ns));
        json_object_set_new(result, "window", window_info);
    }
    
    event->data = result;
    return event;
}

/* Window flush callback */
static void window_flush_cb(uv_timer_t* handle) {
    nql_agg_state_t* agg_state = (nql_agg_state_t*)handle->data;
    if (!agg_state || !agg_state->query || !agg_state->query->data.aggregate) {
        return;
    }
    
    nql_aggregate_t* agg = agg_state->query->data.aggregate;
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
    uint64_t window_size_ns = agg->window.size_ms * 1000000ULL;
    
    /* Flush all buckets */
    nql_agg_bucket_t* bucket = agg_state->buckets;
    while (bucket) {
        if (bucket->window_end_ns <= now) {
            nblex_event* result_event = create_agg_result_event(bucket, agg, world);
            if (result_event) {
                nblex_event_emit(world, result_event);
            }
            
            /* Reset bucket for next window */
            bucket->count = 0;
            bucket->sum = 0.0;
            bucket->min = INFINITY;
            bucket->max = -INFINITY;
            bucket->sum_squares = 0.0;
            bucket->distinct_count = 0;
            if (bucket->distinct_values) {
                json_decref(bucket->distinct_values);
                bucket->distinct_values = NULL;
            }
            if (bucket->percentile_values) {
                json_decref(bucket->percentile_values);
                bucket->percentile_values = NULL;
            }
            bucket->window_start_ns = now;
            bucket->window_end_ns = now + window_size_ns;
        }
        bucket = bucket->next;
    }
    
    agg_state->last_flush_ns = now;
}

/* Get or create aggregation state */
static nql_agg_state_t* get_agg_state(nql_query_t* query, nblex_world* world, const char* query_str) {
    if (!query || query->type != NQL_QUERY_AGGREGATE || !world) {
        return NULL;
    }
    
    /* Check existing contexts by query string */
    if (query_str) {
        nql_exec_ctx_t* ctx = exec_contexts;
        while (ctx) {
            if (ctx->query_string && strcmp(ctx->query_string, query_str) == 0 &&
                ctx->world == world && ctx->state.agg_state) {
                return ctx->state.agg_state;
            }
            ctx = ctx->next;
        }
    }
    
    /* Check existing contexts by query pointer */
    nql_exec_ctx_t* ctx = exec_contexts;
    while (ctx) {
        if (ctx->query == query && ctx->world == world && ctx->state.agg_state) {
            return ctx->state.agg_state;
        }
        ctx = ctx->next;
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
    
    agg_state->query = query;
    agg_state->buckets = NULL;
    agg_state->bucket_count = 0;
    agg_state->timer_active = false;
    
    /* Initialize window timer if needed */
    nql_aggregate_t* agg = query->data.aggregate;
    if (agg->window.type != NQL_WINDOW_NONE && world->loop) {
        agg_state->window_timer = calloc(1, sizeof(uv_timer_t));
        if (agg_state->window_timer) {
            uv_timer_init(world->loop, agg_state->window_timer);
            agg_state->window_timer->data = agg_state;
            
            uint64_t interval_ms = agg->window.slide_ms > 0 ?
                                   agg->window.slide_ms : agg->window.size_ms;
            uv_timer_start(agg_state->window_timer, window_flush_cb,
                          interval_ms, interval_ms);
            agg_state->timer_active = true;
        }
    }
    
    new_ctx->world = world;
    new_ctx->query = query;
    new_ctx->query_string = query_str ? strdup(query_str) : NULL;
    new_ctx->state.agg_state = agg_state;
    new_ctx->next = exec_contexts;
    exec_contexts = new_ctx;
    
    return agg_state;
}

/* Get or create bucket for group keys */
static nql_agg_bucket_t* get_agg_bucket(nql_agg_state_t* agg_state,
                                        char** group_keys,
                                        size_t group_keys_count) {
    if (!agg_state) {
        free_group_keys(group_keys, group_keys_count);
        return NULL;
    }
    
    /* Search existing buckets */
    nql_agg_bucket_t* bucket = agg_state->buckets;
    while (bucket) {
        if (compare_group_keys(bucket->group_keys, group_keys, group_keys_count) == 0) {
            /* Found existing bucket, free the new keys */
            free_group_keys(group_keys, group_keys_count);
            return bucket;
        }
        bucket = bucket->next;
    }
    
    /* Create new bucket */
    bucket = calloc(1, sizeof(nql_agg_bucket_t));
    if (!bucket) {
        free_group_keys(group_keys, group_keys_count);
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
    
    uint64_t now = nblex_timestamp_now();
    nql_aggregate_t* agg = agg_state->query->data.aggregate;
    uint64_t window_size_ns = agg->window.size_ms * 1000000ULL;
    bucket->window_start_ns = now;
    bucket->window_end_ns = now + window_size_ns;
    
    bucket->next = agg_state->buckets;
    agg_state->buckets = bucket;
    agg_state->bucket_count++;
    
    return bucket;
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
    char** group_keys = extract_group_keys(agg, event, &group_keys_count);
    
    /* Get or create bucket */
    nql_agg_bucket_t* bucket = get_agg_bucket(agg_state, group_keys, group_keys_count);
    if (!bucket) {
        free_group_keys(group_keys, group_keys_count);
        return 0;
    }
    
    /* Update aggregations */
    bucket->count++;
    
    for (size_t i = 0; i < agg->funcs_count; i++) {
        nql_agg_func_t* func = &agg->funcs[i];
        
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
    
    /* For non-windowed queries, emit immediately */
    if (agg->window.type == NQL_WINDOW_NONE) {
        nblex_event* result_event = create_agg_result_event(bucket, agg, world);
        if (result_event) {
            nblex_event_emit(world, result_event);
        }
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
