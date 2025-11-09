/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * syslog_parser.c - Syslog log parser (RFC 5424, RFC 3164)
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

/* RFC 3164 syslog format: <pri>timestamp hostname tag: message */
static json_t* parse_rfc3164(const char* line) {
    json_t* root = json_object();
    if (!root) {
        return NULL;
    }

    const char* pos = line;

    /* Parse priority: <number> */
    if (*pos != '<') {
        json_decref(root);
        return NULL;
    }

    pos++; /* Skip < */
    char* endptr;
    long pri = strtol(pos, &endptr, 10);
    if (*endptr != '>' || pri < 0 || pri > 191) {
        json_decref(root);
        return NULL;
    }

    pos = endptr + 1; /* Skip > */

    /* Extract facility and severity from priority */
    int facility = pri / 8;
    int severity = pri % 8;

    /* Add priority fields */
    if (json_object_set_new(root, "syslog_priority", json_integer(pri)) != 0) {
        json_decref(root);
        return NULL;
    }
    if (json_object_set_new(root, "syslog_facility", json_integer(facility)) != 0) {
        json_decref(root);
        return NULL;
    }
    if (json_object_set_new(root, "syslog_severity", json_integer(severity)) != 0) {
        json_decref(root);
        return NULL;
    }

    /* Skip whitespace */
    while (*pos && isspace(*pos)) {
        pos++;
    }

    /* Parse timestamp: Mon DD HH:MM:SS */
    struct tm tm = {0};
    char month[4];
    int day, hour, min, sec;

    if (sscanf(pos, "%3s %2d %2d:%2d:%2d", month, &day, &hour, &min, &sec) == 5) {
        /* Convert month name to number */
        static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        int mon = -1;
        for (int i = 0; i < 12; i++) {
            if (strncmp(month, months[i], 3) == 0) {
                mon = i;
                break;
            }
        }

        if (mon >= 0) {
            tm.tm_mon = mon;
            tm.tm_mday = day;
            tm.tm_hour = hour;
            tm.tm_min = min;
            tm.tm_sec = sec;
            tm.tm_year = 100; /* Assume current century */

            time_t timestamp = mktime(&tm);
            if (json_object_set_new(root, "timestamp", json_integer(timestamp)) != 0) {
                json_decref(root);
                return NULL;
            }
        }

        /* Skip timestamp */
        while (*pos && !isspace(*pos)) {
            pos++;
        }
        while (*pos && isspace(*pos)) {
            pos++;
        }
    }

    /* Parse hostname */
    const char* hostname_start = pos;
    while (*pos && !isspace(*pos) && *pos != ':') {
        pos++;
    }

    if (pos > hostname_start) {
        size_t hostname_len = pos - hostname_start;
        char* hostname = malloc(hostname_len + 1);
        if (hostname) {
            memcpy(hostname, hostname_start, hostname_len);
            hostname[hostname_len] = '\0';
            json_object_set_new(root, "hostname", json_string(hostname));
            free(hostname);
        }
    }

    /* Skip to message */
    while (*pos && *pos != ':') {
        pos++;
    }
    if (*pos == ':') {
        pos++; /* Skip : */
        while (*pos && isspace(*pos)) {
            pos++;
        }
    }

    /* Rest is message */
    if (*pos) {
        if (json_object_set_new(root, "message", json_string(pos)) != 0) {
            json_decref(root);
            return NULL;
        }
    }

    return root;
}

/* RFC 5424 syslog format: <pri>version timestamp hostname app-name procid msgid [structured-data] message */
static json_t* parse_rfc5424(const char* line) {
    json_t* root = json_object();
    if (!root) {
        return NULL;
    }

    const char* pos = line;

    /* Parse priority: <number> */
    if (*pos != '<') {
        json_decref(root);
        return NULL;
    }

    pos++; /* Skip < */
    char* endptr;
    long pri = strtol(pos, &endptr, 10);
    if (*endptr != '>' || pri < 0 || pri > 191) {
        json_decref(root);
        return NULL;
    }

    pos = endptr + 1; /* Skip > */

    /* Extract facility and severity from priority */
    int facility = pri / 8;
    int severity = pri % 8;

    /* Add priority fields */
    if (json_object_set_new(root, "syslog_priority", json_integer(pri)) != 0) {
        json_decref(root);
        return NULL;
    }
    if (json_object_set_new(root, "syslog_facility", json_integer(facility)) != 0) {
        json_decref(root);
        return NULL;
    }
    if (json_object_set_new(root, "syslog_severity", json_integer(severity)) != 0) {
        json_decref(root);
        return NULL;
    }

    /* Parse version */
    if (!isdigit(*pos)) {
        json_decref(root);
        return NULL;
    }

    int version = *pos - '0';
    pos++;

    if (json_object_set_new(root, "syslog_version", json_integer(version)) != 0) {
        json_decref(root);
        return NULL;
    }

    if (*pos != ' ') {
        json_decref(root);
        return NULL;
    }
    pos++; /* Skip space */

    /* Parse ISO timestamp: 2023-11-08T10:30:45.123Z */
    struct tm tm = {0};
    int year, mon, day, hour, min, sec, usec = 0;

    if (sscanf(pos, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &mon, &day, &hour, &min, &sec) >= 6) {
        tm.tm_year = year - 1900;
        tm.tm_mon = mon - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = min;
        tm.tm_sec = sec;

        /* Check for microseconds */
        const char* usec_pos = pos;
        while (*usec_pos && *usec_pos != 'Z' && *usec_pos != '+' && *usec_pos != '-') {
            if (*usec_pos == '.') {
                usec_pos++;
                usec = 0;
                int digits = 0;
                while (isdigit(*usec_pos) && digits < 6) {
                    usec = usec * 10 + (*usec_pos - '0');
                    usec_pos++;
                    digits++;
                }
                for (int i = digits; i < 6; i++) {
                    usec *= 10;
                }
                break;
            }
            usec_pos++;
        }

        time_t timestamp = mktime(&tm);
        if (timestamp != -1) {
            if (json_object_set_new(root, "timestamp", json_integer(timestamp)) != 0) {
                json_decref(root);
                return NULL;
            }
            if (usec > 0) {
                if (json_object_set_new(root, "timestamp_usec", json_integer(usec)) != 0) {
                    json_decref(root);
                    return NULL;
                }
            }
        }

        /* Skip timestamp */
        while (*pos && *pos != ' ') {
            pos++;
        }
        while (*pos && isspace(*pos)) {
            pos++;
        }
    }

    /* Parse remaining fields: hostname app-name procid msgid [structured-data] message */
    /* For simplicity, treat the rest as the message for now */
    if (*pos) {
        if (json_object_set_new(root, "message", json_string(pos)) != 0) {
            json_decref(root);
            return NULL;
        }
    }

    return root;
}

/* Parse syslog line into JSON object */
json_t* nblex_parse_syslog_line(const char* line) {
    if (!line) {
        return NULL;
    }

    /* Try RFC 5424 first (starts with <pri>version) */
    if (line[0] == '<') {
        const char* pos = line + 1;
        while (*pos && *pos != '>') {
            pos++;
        }
        if (*pos == '>') {
            pos++; /* Skip > */
            if (*pos && isdigit(*pos)) {
                /* RFC 5424: <pri>version ... */
                return parse_rfc5424(line);
            } else if (*pos && isspace(*pos)) {
                /* RFC 3164: <pri> timestamp ... */
                return parse_rfc3164(line);
            }
        }
    }

    /* Default to RFC 3164 */
    return parse_rfc3164(line);
}
