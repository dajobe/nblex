/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * metrics_output.c - Metrics output handler (Prometheus format)
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <jansson.h>

/* Aggregation metric entry */
struct aggregation_metric_s {
    char* metric_name;      /* e.g., "count", "avg_latency_ms" */
    json_t* labels;         /* Group labels as JSON object */
    double value;           /* Metric value */
    struct aggregation_metric_s* next;
};
typedef struct aggregation_metric_s aggregation_metric_t;

/* Metrics output state */
struct metrics_output_s {
    FILE* file;
    char* path;
    char* format;           /* prometheus, statsd, etc. */
    time_t last_flush;
    int flush_interval;     /* seconds */

    /* Metrics counters */
    uint64_t events_total;
    uint64_t events_by_type[NBLEX_EVENT_MAX];
    uint64_t events_by_input[10];  /* Assume max 10 inputs */
    uint64_t bytes_processed;
    uint64_t correlations_found;

    /* Aggregation metrics list */
    aggregation_metric_t* aggregation_metrics;
};
typedef struct metrics_output_s metrics_output_t;

/* Free a single aggregation metric */
static void free_aggregation_metric(aggregation_metric_t* metric) {
    if (!metric) {
        return;
    }
    free(metric->metric_name);
    if (metric->labels) {
        json_decref(metric->labels);
    }
    free(metric);
}

/* Free all aggregation metrics */
static void free_all_aggregation_metrics(aggregation_metric_t* head) {
    while (head) {
        aggregation_metric_t* next = head->next;
        free_aggregation_metric(head);
        head = next;
    }
}

/* Format labels for Prometheus output */
static char* format_prometheus_labels(json_t* labels) {
    if (!labels || !json_is_object(labels)) {
        return strdup("");
    }

    size_t size = 2; /* "{}" */
    const char* key;
    json_t* value;

    json_object_foreach(labels, key, value) {
        /* Estimate size: key + "=\"" + value + "\"," */
        const char* val_str = json_string_value(value);
        if (val_str) {
            size += strlen(key) + strlen(val_str) + 5; /* "=\"," */
        }
    }

    char* result = malloc(size);
    if (!result) {
        return NULL;
    }

    strcpy(result, "{");
    int first = 1;
    json_object_foreach(labels, key, value) {
        if (!first) {
            strcat(result, ",");
        }
        first = 0;
        strcat(result, key);
        strcat(result, "=\"");
        const char* val_str = json_string_value(value);
        if (val_str) {
            strcat(result, val_str);
        }
        strcat(result, "\"");
    }
    strcat(result, "}");

    return result;
}

/* Flush metrics to file */
int nblex_metrics_output_flush(metrics_output_t* output) {
    if (!output || !output->file) {
        return -1;
    }

    /* Rewind file */
    rewind(output->file);

    if (strcmp(output->format, "prometheus") == 0) {
        /* Write Prometheus format */
        fprintf(output->file, "# HELP nblex_events_total Total number of events processed\n");
        fprintf(output->file, "# TYPE nblex_events_total counter\n");
        fprintf(output->file, "nblex_events_total %llu\n", (unsigned long long)output->events_total);

        fprintf(output->file, "# HELP nblex_events_by_type Events by type\n");
        fprintf(output->file, "# TYPE nblex_events_by_type counter\n");
        const char* type_names[] = {"unknown", "log", "network", "correlation"};
        for (int i = 0; i < NBLEX_EVENT_MAX && i < 4; i++) {
            if (output->events_by_type[i] > 0) {
                fprintf(output->file, "nblex_events_by_type{type=\"%s\"} %llu\n",
                       type_names[i], (unsigned long long)output->events_by_type[i]);
            }
        }

        fprintf(output->file, "# HELP nblex_bytes_processed Total bytes processed\n");
        fprintf(output->file, "# TYPE nblex_bytes_processed counter\n");
        fprintf(output->file, "nblex_bytes_processed %llu\n", (unsigned long long)output->bytes_processed);

        fprintf(output->file, "# HELP nblex_correlations_found Total correlations found\n");
        fprintf(output->file, "# TYPE nblex_correlations_found counter\n");
        fprintf(output->file, "nblex_correlations_found %llu\n", (unsigned long long)output->correlations_found);

        /* Export aggregation metrics */
        if (output->aggregation_metrics) {
            fprintf(output->file, "# HELP nblex_aggregation Aggregation metrics from nQL queries\n");
            fprintf(output->file, "# TYPE nblex_aggregation gauge\n");

            aggregation_metric_t* metric = output->aggregation_metrics;
            while (metric) {
                char* labels_str = format_prometheus_labels(metric->labels);
                if (labels_str && strlen(labels_str) > 2) {
                    /* Has labels - format: nblex_aggregation{metric="name",label1="val1",...} */
                    fprintf(output->file, "nblex_aggregation{metric=\"%s\",%s} %.6f\n",
                           metric->metric_name,
                           labels_str + 1, /* Skip opening '{' */
                           metric->value);
                    free(labels_str);
                } else {
                    /* No labels - format: nblex_aggregation{metric="name"} */
                    if (labels_str) {
                        free(labels_str);
                    }
                    fprintf(output->file, "nblex_aggregation{metric=\"%s\"} %.6f\n",
                           metric->metric_name, metric->value);
                }
                metric = metric->next;
            }
        }
    }

    fflush(output->file);
    output->last_flush = time(NULL);

    /* Clear aggregation metrics after flush (Prometheus will scrape periodically) */
    free_all_aggregation_metrics(output->aggregation_metrics);
    output->aggregation_metrics = NULL;

    return 0;
}

/* Free metrics output */
void nblex_metrics_output_free(metrics_output_t* output) {
    if (!output) {
        return;
    }

    /* Final flush */
    nblex_metrics_output_flush(output);

    if (output->file) {
        fclose(output->file);
    }

    /* Free aggregation metrics */
    free_all_aggregation_metrics(output->aggregation_metrics);

    free(output->path);
    free(output->format);
    free(output);
}

/* Initialize metrics output */
metrics_output_t* nblex_metrics_output_new(const char* path, const char* format) {
    if (!path) {
        return NULL;
    }

    metrics_output_t* output = calloc(1, sizeof(metrics_output_t));
    if (!output) {
        return NULL;
    }

    output->path = strdup(path);
    if (!output->path) {
        free(output);
        return NULL;
    }

    if (format) {
        output->format = strdup(format);
        if (!output->format) {
            free(output->path);
            free(output);
            return NULL;
        }
    } else {
        output->format = strdup("prometheus");
        if (!output->format) {
            free(output->path);
            free(output);
            return NULL;
        }
    }

    /* Open file for writing (metrics files are usually overwritten) */
    output->file = fopen(path, "w");
    if (!output->file) {
        fprintf(stderr, "Error opening metrics file %s: %s\n", path, strerror(errno));
        free(output->format);
        free(output->path);
        free(output);
        return NULL;
    }

    output->last_flush = time(NULL);
    output->flush_interval = 60;  /* 1 minute */

    /* Initialize counters */
    memset(output->events_by_type, 0, sizeof(output->events_by_type));
    memset(output->events_by_input, 0, sizeof(output->events_by_input));

    /* Initialize aggregation metrics list */
    output->aggregation_metrics = NULL;

    return output;
}

/* Set flush interval */
void nblex_metrics_output_set_flush_interval(metrics_output_t* output, int seconds) {
    if (!output || seconds <= 0) {
        return;
    }

    output->flush_interval = seconds;
}

/* Write event to metrics */
int nblex_metrics_output_write(metrics_output_t* output, nblex_event* event) {
    if (!output || !event) {
        return -1;
    }

    /* Update counters */
    output->events_total++;

    if (event->type < NBLEX_EVENT_MAX) {
        output->events_by_type[event->type]++;
    }

    /* Estimate bytes (rough approximation) */
    if (event->data) {
        output->bytes_processed += json_dumpb(event->data, NULL, 0, 0);
    }

    /* Check if it's a correlation event */
    if (event->type == NBLEX_EVENT_CORRELATION) {
        output->correlations_found++;
    }

    /* Check if it's a derived event (aggregation or correlation) */
    if (event->data) {
        json_t* result_type = json_object_get(event->data, "nql_result_type");
        if (result_type && json_is_string(result_type)) {
            const char* result_type_str = json_string_value(result_type);
            
            /* Handle aggregation events - extract metrics */
            if (strcmp(result_type_str, "aggregation") == 0) {
                json_t* metrics = json_object_get(event->data, "metrics");
                json_t* group = json_object_get(event->data, "group");
                
                if (metrics && json_is_object(metrics)) {
                    const char* metric_key;
                    json_t* metric_value;
                    
                    /* Iterate over metrics and store them */
                    json_object_foreach(metrics, metric_key, metric_value) {
                        if (json_is_number(metric_value)) {
                            /* Create new metric entry */
                            aggregation_metric_t* new_metric = calloc(1, sizeof(aggregation_metric_t));
                            if (!new_metric) {
                                continue;
                            }
                            
                            new_metric->metric_name = strdup(metric_key);
                            if (!new_metric->metric_name) {
                                free(new_metric);
                                continue;
                            }
                            
                            /* Copy group labels if present */
                            if (group && json_is_object(group)) {
                                new_metric->labels = json_deep_copy(group);
                                if (!new_metric->labels) {
                                    free(new_metric->metric_name);
                                    free(new_metric);
                                    continue;
                                }
                            } else {
                                new_metric->labels = NULL;
                            }
                            
                            /* Get metric value */
                            if (json_is_integer(metric_value)) {
                                new_metric->value = (double)json_integer_value(metric_value);
                            } else {
                                new_metric->value = json_real_value(metric_value);
                            }
                            
                            /* Add to list (prepend for simplicity) */
                            new_metric->next = output->aggregation_metrics;
                            output->aggregation_metrics = new_metric;
                        }
                    }
                }
            }
        }
    }

    /* Check if we need to flush */
    time_t now = time(NULL);
    if (now - output->last_flush >= output->flush_interval) {
        return nblex_metrics_output_flush(output);
    }

    return 0;
}
