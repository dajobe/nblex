/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * nql_executor.c - nQL (nblex Query Language) executor
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include "../parsers/nql_parser.c"  /* Include parser for AST structures */
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
static int execute_filter(nql_query_t* query, nblex_event* event);
static int execute_correlate(nql_query_t* query, nblex_event* event, nblex_world* world);
static int execute_show(nql_query_t* query, nblex_event* event);
static int execute_aggregate(nql_query_t* query, nblex_event* event);
static int execute_pipeline(nql_query_t* query, nblex_event* event, nblex_world* world);
static int execute_query(nql_query_t* query, nblex_event* event, nblex_world* world);

/* Execute filter query */
static int execute_filter(nql_query_t* query, nblex_event* event) {
    if (!query || query->type != NQL_TYPE_FILTER || !query->data.filter) {
        return 0;
    }
    
    return nblex_filter_matches(query->data.filter, event);
}

/* Execute correlation query */
static int execute_correlate(nql_query_t* query, nblex_event* event, nblex_world* world) {
    if (!query || query->type != NQL_TYPE_CORRELATE || !query->data.correlate) {
        return 0;
    }
    
    /* For now, correlation is handled by the correlation engine */
    /* This executor just checks if the event matches either filter */
    if (query->data.correlate->left_filter && 
        nblex_filter_matches(query->data.correlate->left_filter, event)) {
        return 1;
    }
    
    if (query->data.correlate->right_filter && 
        nblex_filter_matches(query->data.correlate->right_filter, event)) {
        return 1;
    }
    
    return 0;
}

/* Execute show query */
static int execute_show(nql_query_t* query, nblex_event* event) {
    if (!query || query->type != NQL_TYPE_SHOW || !query->data.show) {
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

/* Execute aggregate query (basic - full implementation requires windowing) */
static int execute_aggregate(nql_query_t* query, nblex_event* event) {
    if (!query || query->type != NQL_TYPE_AGGREGATE || !query->data.aggregate) {
        return 0;
    }
    
    /* If there's a WHERE clause, check it */
    if (query->data.aggregate->where_filter) {
        if (!nblex_filter_matches(query->data.aggregate->where_filter, event)) {
            return 0;
        }
    }
    
    /* Aggregate query matches if WHERE clause passes (or no WHERE clause) */
    /* Actual aggregation happens in windowing/grouping logic (to be implemented) */
    return 1;
}

/* Execute pipeline query */
static int execute_pipeline(nql_query_t* query, nblex_event* event, nblex_world* world) {
    if (!query || query->type != NQL_TYPE_PIPELINE) {
        return 0;
    }
    
    /* Execute left side first */
    int left_result = execute_query(query->data.pipeline.left, event, world);
    if (!left_result) {
        return 0;
    }
    
    /* Then execute right side on the result */
    return execute_query(query->data.pipeline.right, event, world);
}

/* Execute query (main entry point) */
int execute_query(nql_query_t* query, nblex_event* event, nblex_world* world) {
    if (!query || !event) {
        return 0;
    }
    
    switch (query->type) {
        case NQL_TYPE_FILTER:
            return execute_filter(query, event);
            
        case NQL_TYPE_CORRELATE:
            return execute_correlate(query, event, world);
            
        case NQL_TYPE_SHOW:
            return execute_show(query, event);
            
        case NQL_TYPE_AGGREGATE:
            return execute_aggregate(query, event);
            
        case NQL_TYPE_PIPELINE:
            return execute_pipeline(query, event, world);
            
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
    
    int result = execute_query(query, event, world);
    nql_free(query);
    
    return result;
}

