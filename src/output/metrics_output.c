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

/* Metrics output state */
typedef struct {
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
} metrics_output_t;

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
    }

    fflush(output->file);
    output->last_flush = time(NULL);

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

    /* Check if we need to flush */
    time_t now = time(NULL);
    if (now - output->last_flush >= output->flush_interval) {
        return nblex_metrics_output_flush(output);
    }

    return 0;
}
