/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * file_output.c - File output handler
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* File output state */
typedef struct {
    FILE* file;
    char* path;
    char* format;
    int rotation_max_size;    /* Max size in MB */
    int rotation_max_age;     /* Max age in days */
    int rotation_max_count;   /* Max number of files to keep */
    time_t last_rotation;
    long current_size;
} file_output_t;

/* Rotate file if needed */
static int rotate_file_if_needed(file_output_t* output) {
    if (!output->file) {
        return 0;
    }

    int should_rotate = 0;
    time_t now = time(NULL);

    /* Check size */
    if (output->rotation_max_size > 0) {
        fseek(output->file, 0, SEEK_END);
        long size_mb = ftell(output->file) / (1024 * 1024);
        if (size_mb >= output->rotation_max_size) {
            should_rotate = 1;
        }
    }

    /* Check age */
    if (output->rotation_max_age > 0) {
        if (now - output->last_rotation >= output->rotation_max_age * 24 * 60 * 60) {
            should_rotate = 1;
        }
    }

    if (!should_rotate) {
        return 0;
    }

    /* Close current file */
    fclose(output->file);
    output->file = NULL;

    /* Rename current file with timestamp */
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));

    char rotated_path[1024];
    snprintf(rotated_path, sizeof(rotated_path), "%s.%s", output->path, timestamp);

    rename(output->path, rotated_path);

    /* TODO: Clean up old rotated files based on rotation_max_count */

    /* Open new file */
    output->file = fopen(output->path, "a");
    if (!output->file) {
        fprintf(stderr, "Error reopening rotated file %s: %s\n", output->path, strerror(errno));
        return -1;
    }

    output->last_rotation = now;
    output->current_size = 0;

    return 0;
}

/* Initialize file output */
file_output_t* nblex_file_output_new(const char* path, const char* format) {
    if (!path) {
        return NULL;
    }

    file_output_t* output = calloc(1, sizeof(file_output_t));
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
        output->format = strdup("json");
        if (!output->format) {
            free(output->path);
            free(output);
            return NULL;
        }
    }

    /* Open file for appending */
    output->file = fopen(path, "a");
    if (!output->file) {
        fprintf(stderr, "Error opening output file %s: %s\n", path, strerror(errno));
        free(output->format);
        free(output->path);
        free(output);
        return NULL;
    }

    output->last_rotation = time(NULL);
    output->current_size = 0;

    /* Default rotation settings */
    output->rotation_max_size = 100;  /* 100MB */
    output->rotation_max_age = 7;     /* 7 days */
    output->rotation_max_count = 10;  /* 10 files */

    return output;
}

/* Free file output */
void nblex_file_output_free(file_output_t* output) {
    if (!output) {
        return;
    }

    if (output->file) {
        fclose(output->file);
    }

    free(output->path);
    free(output->format);
    free(output);
}

/* Set rotation parameters */
void nblex_file_output_set_rotation(file_output_t* output, int max_size_mb, int max_age_days, int max_count) {
    if (!output) {
        return;
    }

    output->rotation_max_size = max_size_mb;
    output->rotation_max_age = max_age_days;
    output->rotation_max_count = max_count;
}

/* Write event to file */
int nblex_file_output_write(file_output_t* output, nblex_event* event) {
    if (!output || !output->file || !event) {
        return -1;
    }

    /* Check if rotation is needed */
    if (rotate_file_if_needed(output) != 0) {
        return -1;
    }

    /* Format event */
    char* json_str = nblex_event_to_json_string(event);
    if (!json_str) {
        return -1;
    }

    /* Write to file */
    size_t len = strlen(json_str);
    if (fwrite(json_str, 1, len, output->file) != len) {
        free(json_str);
        return -1;
    }

    /* Add newline */
    if (fwrite("\n", 1, 1, output->file) != 1) {
        free(json_str);
        return -1;
    }

    /* Flush */
    fflush(output->file);

    output->current_size += len + 1;
    free(json_str);

    return 0;
}
