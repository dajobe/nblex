/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * config.c - YAML configuration file support
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Configuration structure */
struct nblex_config_s {
    /* Version */
    char* version;

    /* Inputs */
    nblex_input_config_t* inputs;
    size_t inputs_count;
    size_t inputs_capacity;

    /* Outputs */
    nblex_output_config_t* outputs;
    size_t outputs_count;
    size_t outputs_capacity;

    /* Correlation */
    int correlation_enabled;
    int correlation_window_ms;

    /* Performance */
    int worker_threads;
    size_t buffer_size;
    size_t memory_limit;
};

/* Configuration structures are defined in nblex_internal.h */

/* Helper: Parse string value from YAML scalar */
static char* parse_yaml_scalar(yaml_event_t* event) {
    if (event->type != YAML_SCALAR_EVENT) {
        return NULL;
    }
    yaml_char_t* value = event->data.scalar.value;
    size_t length = event->data.scalar.length;
    char* result = malloc(length + 1);
    if (!result) {
        return NULL;
    }
    memcpy(result, value, length);
    result[length] = '\0';
    return result;
}


/* Load YAML configuration */
nblex_config_t* nblex_config_load_yaml(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open config file '%s': %s\n", filename, strerror(errno));
        return NULL;
    }

    yaml_parser_t parser;
    yaml_event_t event;
    nblex_config_t* config = NULL;

    /* Initialize parser */
    if (!yaml_parser_initialize(&parser)) {
        fprintf(stderr, "Error: Failed to initialize YAML parser\n");
        fclose(file);
        return NULL;
    }

    yaml_parser_set_input_file(&parser, file);

    /* Allocate config structure */
    config = calloc(1, sizeof(nblex_config_t));
    if (!config) {
        yaml_parser_delete(&parser);
        fclose(file);
        return NULL;
    }

    /* Set defaults */
    config->version = strdup("1.0");
    config->correlation_enabled = 1;
    config->correlation_window_ms = 100;
    config->worker_threads = 4;
    config->buffer_size = 64 * 1024 * 1024;  /* 64MB */
    config->memory_limit = 1024 * 1024 * 1024;  /* 1GB */
    config->inputs_capacity = 8;
    config->outputs_capacity = 8;
    config->inputs = calloc(config->inputs_capacity, sizeof(nblex_input_config_t));
    config->outputs = calloc(config->outputs_capacity, sizeof(nblex_output_config_t));

    if (!config->inputs || !config->outputs) {
        nblex_config_free(config);
        yaml_parser_delete(&parser);
        fclose(file);
        return NULL;
    }

    /* Parse YAML */
    int in_inputs = 0, in_outputs = 0, in_correlation = 0, in_performance = 0;
    int in_logs = 0, in_network = 0;
    int expecting_key = 1;
    char* current_key = NULL;
    nblex_input_config_t* current_input = NULL;
    nblex_output_config_t* current_output = NULL;

    do {
        if (!yaml_parser_parse(&parser, &event)) {
            fprintf(stderr, "Error: YAML parse error at line %zu\n", parser.problem_mark.line + 1);
            nblex_config_free(config);
            yaml_parser_delete(&parser);
            fclose(file);
            return NULL;
        }

        switch (event.type) {
            case YAML_STREAM_START_EVENT:
            case YAML_DOCUMENT_START_EVENT:
            case YAML_DOCUMENT_END_EVENT:
            case YAML_STREAM_END_EVENT:
                break;

            case YAML_MAPPING_START_EVENT:
                if (in_inputs && !in_logs && !in_network) {
                    /* Starting a new input */
                    if (config->inputs_count >= config->inputs_capacity) {
                        config->inputs_capacity *= 2;
                        config->inputs = realloc(config->inputs, 
                                                config->inputs_capacity * sizeof(nblex_input_config_t));
                    }
                    current_input = &config->inputs[config->inputs_count++];
                    memset(current_input, 0, sizeof(nblex_input_config_t));
                } else if (in_outputs) {
                    /* Starting a new output */
                    if (config->outputs_count >= config->outputs_capacity) {
                        config->outputs_capacity *= 2;
                        config->outputs = realloc(config->outputs,
                                                config->outputs_capacity * sizeof(nblex_output_config_t));
                    }
                    current_output = &config->outputs[config->outputs_count++];
                    memset(current_output, 0, sizeof(nblex_output_config_t));
                }
                break;

            case YAML_MAPPING_END_EVENT:
                if (in_logs) in_logs = 0;
                if (in_network) in_network = 0;
                current_input = NULL;
                current_output = NULL;
                break;

            case YAML_SEQUENCE_START_EVENT:
                if (current_key && strcmp(current_key, "logs") == 0) {
                    in_logs = 1;
                } else if (current_key && strcmp(current_key, "network") == 0) {
                    in_network = 1;
                }
                break;

            case YAML_SEQUENCE_END_EVENT:
                break;

            case YAML_SCALAR_EVENT: {
                char* value = parse_yaml_scalar(&event);
                if (!value) break;

                if (expecting_key) {
                    free(current_key);
                    current_key = value;
                    expecting_key = 0;

                    /* Check for top-level sections */
                    if (strcmp(current_key, "version") == 0) {
                        /* Will be set in next scalar */
                    } else if (strcmp(current_key, "inputs") == 0) {
                        in_inputs = 1;
                    } else if (strcmp(current_key, "outputs") == 0) {
                        in_outputs = 1;
                    } else if (strcmp(current_key, "correlation") == 0) {
                        in_correlation = 1;
                    } else if (strcmp(current_key, "performance") == 0) {
                        in_performance = 1;
                    }
                } else {
                    /* This is a value */
                    if (strcmp(current_key, "version") == 0) {
                        free(config->version);
                        config->version = value;
                    } else if (current_input) {
                        if (strcmp(current_key, "name") == 0) {
                            current_input->name = value;
                        } else if (strcmp(current_key, "type") == 0) {
                            current_input->type = value;
                        } else if (strcmp(current_key, "path") == 0) {
                            current_input->path = value;
                        } else if (strcmp(current_key, "interface") == 0) {
                            current_input->interface = value;
                        } else if (strcmp(current_key, "filter") == 0) {
                            current_input->filter = value;
                        } else if (strcmp(current_key, "format") == 0) {
                            current_input->format = value;
                        } else {
                            free(value);
                        }
                    } else if (current_output) {
                        if (strcmp(current_key, "name") == 0) {
                            current_output->name = value;
                        } else if (strcmp(current_key, "type") == 0) {
                            current_output->type = value;
                        } else if (strcmp(current_key, "path") == 0) {
                            current_output->path = value;
                        } else if (strcmp(current_key, "url") == 0) {
                            current_output->url = value;
                        } else if (strcmp(current_key, "format") == 0) {
                            current_output->format = value;
                        } else {
                            free(value);
                        }
                    } else if (in_correlation) {
                        if (strcmp(current_key, "enabled") == 0) {
                            config->correlation_enabled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
                            free(value);
                        } else if (strcmp(current_key, "window_ms") == 0) {
                            config->correlation_window_ms = atoi(value);
                            free(value);
                        } else {
                            free(value);
                        }
                    } else if (in_performance) {
                        if (strcmp(current_key, "worker_threads") == 0) {
                            config->worker_threads = atoi(value);
                            free(value);
                        } else if (strcmp(current_key, "buffer_size") == 0) {
                            /* Parse size with units (MB, GB) */
                            size_t size = atoi(value);
                            if (strstr(value, "GB")) size *= 1024 * 1024 * 1024;
                            else if (strstr(value, "MB")) size *= 1024 * 1024;
                            else if (strstr(value, "KB")) size *= 1024;
                            config->buffer_size = size;
                            free(value);
                        } else if (strcmp(current_key, "memory_limit") == 0) {
                            size_t size = atoi(value);
                            if (strstr(value, "GB")) size *= 1024 * 1024 * 1024;
                            else if (strstr(value, "MB")) size *= 1024 * 1024;
                            else if (strstr(value, "KB")) size *= 1024;
                            config->memory_limit = size;
                            free(value);
                        } else {
                            free(value);
                        }
                    } else {
                        free(value);
                    }
                    free(current_key);
                    current_key = NULL;
                    expecting_key = 1;
                }
                break;
            }

            default:
                break;
        }

        yaml_event_delete(&event);
    } while (event.type != YAML_STREAM_END_EVENT);

    yaml_parser_delete(&parser);
    fclose(file);

    if (current_key) free(current_key);
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

    /* Apply correlation settings */
    if (config->correlation_enabled && world->correlation) {
        nblex_correlation_add_strategy(world->correlation, 
                                       NBLEX_CORR_TIME_BASED,
                                       config->correlation_window_ms);
    }

    /* Create inputs from config */
    for (size_t i = 0; i < config->inputs_count; i++) {
        nblex_input_config_t* input_cfg = &config->inputs[i];
        nblex_input* input = NULL;

        if (!input_cfg->type) {
            continue;
        }

        if (strcmp(input_cfg->type, "file") == 0 && input_cfg->path) {
            input = nblex_input_file_new(world, input_cfg->path);
            if (input && input_cfg->format) {
                /* Parse format string */
                if (strcmp(input_cfg->format, "json") == 0) {
                    nblex_input_set_format(input, NBLEX_FORMAT_JSON);
                } else if (strcmp(input_cfg->format, "logfmt") == 0) {
                    nblex_input_set_format(input, NBLEX_FORMAT_LOGFMT);
                } else if (strcmp(input_cfg->format, "syslog") == 0) {
                    nblex_input_set_format(input, NBLEX_FORMAT_SYSLOG);
                }
            }
        } else if (strcmp(input_cfg->type, "pcap") == 0 && input_cfg->interface) {
            input = nblex_input_pcap_new(world, input_cfg->interface);
        }

        if (input && input_cfg->filter) {
            nblex_input_set_filter(input, input_cfg->filter);
        }
    }

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
