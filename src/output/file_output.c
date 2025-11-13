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
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include <unistd.h>

/* File output state */
struct file_output_s {
    FILE* file;
    char* path;
    char* format;
    int rotation_max_size;    /* Max size in MB */
    int rotation_max_age;     /* Max age in days */
    int rotation_max_count;   /* Max number of files to keep */
    time_t last_rotation;
    long current_size;
};
typedef struct file_output_s file_output_t;

/* Entry for tracking rotated files */
typedef struct {
    char* path;
    time_t mtime;
} rotated_file_entry;

/* Compare function for qsort - newest first */
static int compare_rotated_files(const void* a, const void* b) {
    const rotated_file_entry* fa = (const rotated_file_entry*)a;
    const rotated_file_entry* fb = (const rotated_file_entry*)b;

    if (fa->mtime > fb->mtime) return -1;
    if (fa->mtime < fb->mtime) return 1;
    return 0;
}

/* Clean up old rotated files */
static void cleanup_old_rotated_files(file_output_t* output) {
    if (output->rotation_max_count <= 0) {
        return;  /* No limit set */
    }

    /* Extract directory and basename from path */
    char* path_copy1 = strdup(output->path);
    if (!path_copy1) {
        return;
    }

    char* path_copy2 = strdup(output->path);
    if (!path_copy2) {
        free(path_copy1);
        return;
    }

    /* basename() and dirname() may modify the string, so use separate copies */
    char* dir_name = dirname(path_copy1);
    char* base_name = basename(path_copy2);

    /* Copy the results as they might point to static buffers */
    char dir_buf[1024];
    char base_buf[256];
    snprintf(dir_buf, sizeof(dir_buf), "%s", dir_name);
    snprintf(base_buf, sizeof(base_buf), "%s", base_name);

    /* Free the path copies */
    free(path_copy1);
    free(path_copy2);

    /* Open directory */
    DIR* dir = opendir(dir_buf);
    if (!dir) {
        return;
    }

    /* Build list of rotated files */
    rotated_file_entry* files = NULL;
    size_t files_count = 0;
    size_t files_capacity = 0;

    struct dirent* entry;
    size_t base_len = strlen(base_buf);

    while ((entry = readdir(dir)) != NULL) {
        /* Check if filename starts with basename and has a timestamp suffix */
        if (strncmp(entry->d_name, base_buf, base_len) == 0 &&
            entry->d_name[base_len] == '.') {

            /* Verify it looks like a timestamp (YYYYMMDD_HHMMSS) */
            const char* timestamp_part = entry->d_name + base_len + 1;
            if (strlen(timestamp_part) >= 15 &&
                timestamp_part[8] == '_') {

                /* Build full path */
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s/%s", dir_buf, entry->d_name);

                /* Get modification time */
                struct stat st;
                if (stat(full_path, &st) == 0) {
                    /* Add to list */
                    if (files_count >= files_capacity) {
                        files_capacity = files_capacity ? files_capacity * 2 : 16;
                        rotated_file_entry* new_files = realloc(files,
                            files_capacity * sizeof(rotated_file_entry));
                        if (!new_files) {
                            break;
                        }
                        files = new_files;
                    }

                    files[files_count].path = strdup(full_path);
                    files[files_count].mtime = st.st_mtime;
                    if (files[files_count].path) {
                        files_count++;
                    }
                }
            }
        }
    }

    closedir(dir);

    /* If we have more files than max_count, delete oldest ones */
    if (files_count > (size_t)output->rotation_max_count) {
        /* Sort by modification time (newest first) */
        qsort(files, files_count, sizeof(rotated_file_entry), compare_rotated_files);

        /* Delete files beyond max_count */
        for (size_t i = output->rotation_max_count; i < files_count; i++) {
            if (unlink(files[i].path) != 0) {
                /* Log error but continue */
                fprintf(stderr, "Warning: Failed to delete old rotated file %s: %s\n",
                       files[i].path, strerror(errno));
            }
        }
    }

    /* Free file list */
    for (size_t i = 0; i < files_count; i++) {
        free(files[i].path);
    }
    free(files);
}

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

    /* Clean up old rotated files if count limit is set */
    cleanup_old_rotated_files(output);

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
