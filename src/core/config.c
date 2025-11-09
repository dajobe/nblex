/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * config.c - YAML configuration file support
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <yaml.h>  // TODO: Enable when yaml pkg-config is fixed
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Configuration structure */
typedef struct {
    /* Version */
    char* version;

    /* Inputs */
    nblex_input_config_t* inputs;
    size_t inputs_count;

    /* Outputs */
    nblex_output_config_t* outputs;
    size_t outputs_count;

    /* Correlation */
    int correlation_enabled;
    int correlation_window_ms;

    /* Performance */
    int worker_threads;
    size_t buffer_size;
    size_t memory_limit;
} nblex_config_t;

/* Configuration structures are defined in nblex_internal.h */

/* Load YAML configuration */
nblex_config_t* nblex_config_load_yaml(const char* filename) {
    /* TODO: Implement YAML parsing */
    fprintf(stderr, "YAML configuration loading not yet implemented\n");

    /* For now, return a basic config */
    nblex_config_t* config = calloc(1, sizeof(nblex_config_t));
    if (!config) {
        return NULL;
    }

    config->version = strdup("1.0");
    config->correlation_enabled = 1;
    config->correlation_window_ms = 100;
    config->worker_threads = 4;
    config->buffer_size = 64 * 1024 * 1024;  /* 64MB */
    config->memory_limit = 1024 * 1024 * 1024;  /* 1GB */

    return config;
}

/* Free configuration */
void nblex_config_free(nblex_config_t* config) {
    if (!config) {
        return;
    }

    free(config->version);

    /* Free inputs */
    for (size_t i = 0; i < config->inputs_count; i++) {
        free(config->inputs[i].name);
        free(config->inputs[i].type);
        free(config->inputs[i].path);
        free(config->inputs[i].interface);
        free(config->inputs[i].filter);
        free(config->inputs[i].format);
    }
    free(config->inputs);

    /* Free outputs */
    for (size_t i = 0; i < config->outputs_count; i++) {
        free(config->outputs[i].name);
        free(config->outputs[i].type);
        free(config->outputs[i].path);
        free(config->outputs[i].url);
        free(config->outputs[i].format);
    }
    free(config->outputs);

    free(config);
}

/* Apply configuration to world */
int nblex_config_apply(nblex_config_t* config, nblex_world* world) {
    if (!config || !world) {
        return -1;
    }

    /* TODO: Apply configuration settings to world */

    /* For now, just return success */
    return 0;
}

/* Get configuration value as string */
const char* nblex_config_get_string(nblex_config_t* config, const char* key) {
    if (!config || !key) {
        return NULL;
    }

    /* Simple key-value lookup for basic config */
    if (strcmp(key, "version") == 0) {
        return config->version;
    }

    return NULL;
}

/* Get configuration value as int */
int nblex_config_get_int(nblex_config_t* config, const char* key, int default_value) {
    if (!config || !key) {
        return default_value;
    }

    if (strcmp(key, "correlation.enabled") == 0) {
        return config->correlation_enabled;
    } else if (strcmp(key, "correlation.window_ms") == 0) {
        return config->correlation_window_ms;
    } else if (strcmp(key, "performance.worker_threads") == 0) {
        return config->worker_threads;
    }

    return default_value;
}

/* Get configuration value as size_t */
size_t nblex_config_get_size(nblex_config_t* config, const char* key, size_t default_value) {
    if (!config || !key) {
        return default_value;
    }

    if (strcmp(key, "performance.buffer_size") == 0) {
        return config->buffer_size;
    } else if (strcmp(key, "performance.memory_limit") == 0) {
        return config->memory_limit;
    }

    return default_value;
}
