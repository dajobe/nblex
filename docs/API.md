# nblex API Documentation

**Version:** 0.5.0
**Last Updated:** 2025-11-13

______________________________________________________________________

## Table of Contents

1. [Introduction](#introduction)
2. [Core API](#core-api)
3. [Input API](#input-api)
4. [Event API](#event-api)
5. [Correlation API](#correlation-api)
6. [Filter API](#filter-api)
7. [nQL Query API](#nql-query-api)
8. [Output API](#output-api)
9. [Configuration API](#configuration-api)
10. [Error Handling](#error-handling)
11. [Memory Management](#memory-management)
12. [Examples](#examples)

______________________________________________________________________

## Introduction

The nblex library provides a C API for log and network event correlation. This document describes the public API functions and data structures.

### Headers

```c
#include <nblex/nblex.h>
```

### Linking

```bash
gcc -o myapp myapp.c -lnblex -luv -ljansson -lpcap -lpcre2-8
```

Or use pkg-config:

```bash
gcc -o myapp myapp.c $(pkg-config --cflags --libs nblex)
```

______________________________________________________________________

## Core API

### World Management

The `nblex_world` is the main context for all nblex operations.

#### nblex_world_new

```c
nblex_world* nblex_world_new(void);
```

Creates a new nblex world instance.

**Returns:** New world instance, or `NULL` on error.

**Example:**
```c
nblex_world* world = nblex_world_new();
if (!world) {
    fprintf(stderr, "Failed to create world\n");
    exit(1);
}
```

#### nblex_world_open

```c
int nblex_world_open(nblex_world* world);
```

Initializes the world for use (sets up event loop, correlation engine, etc.).

**Parameters:**
- `world`: World instance

**Returns:** `0` on success, non-zero on error.

**Example:**
```c
if (nblex_world_open(world) != 0) {
    fprintf(stderr, "Failed to open world\n");
    nblex_world_free(world);
    exit(1);
}
```

#### nblex_world_start

```c
int nblex_world_start(nblex_world* world);
```

Starts processing events (starts all inputs, begins correlation).

**Parameters:**
- `world`: World instance

**Returns:** `0` on success, non-zero on error.

#### nblex_world_stop

```c
int nblex_world_stop(nblex_world* world);
```

Stops processing events (stops all inputs, pauses correlation).

**Parameters:**
- `world`: World instance

**Returns:** `0` on success, non-zero on error.

#### nblex_world_run

```c
int nblex_world_run(nblex_world* world);
```

Runs the event loop (blocking until stopped).

**Parameters:**
- `world`: World instance

**Returns:** `0` on success, non-zero on error.

**Example:**
```c
nblex_world_start(world);
nblex_world_run(world);  /* Blocks here */
```

#### nblex_world_free

```c
void nblex_world_free(nblex_world* world);
```

Frees a world instance and all associated resources.

**Parameters:**
- `world`: World instance

______________________________________________________________________

## Input API

### Input Types

```c
typedef enum {
    NBLEX_INPUT_FILE,       /* File input */
    NBLEX_INPUT_SYSLOG,     /* Syslog input */
    NBLEX_INPUT_PCAP,       /* Packet capture input */
    NBLEX_INPUT_SOCKET      /* Socket input */
} nblex_input_type;
```

### Log Formats

```c
typedef enum {
    NBLEX_FORMAT_JSON,      /* JSON format */
    NBLEX_FORMAT_LOGFMT,    /* Logfmt format */
    NBLEX_FORMAT_SYSLOG,    /* Syslog format */
    NBLEX_FORMAT_NGINX,     /* Nginx combined log format */
    NBLEX_FORMAT_REGEX      /* Custom regex format */
} nblex_log_format;
```

### nblex_input_file_new

```c
nblex_input* nblex_input_file_new(nblex_world* world, const char* path);
```

Creates a file input for log monitoring.

**Parameters:**
- `world`: World instance
- `path`: Path to file (supports wildcards like `/var/log/app/*.log`)

**Returns:** New input instance, or `NULL` on error.

**Example:**
```c
nblex_input* log_input = nblex_input_file_new(world, "/var/log/app.log");
if (!log_input) {
    fprintf(stderr, "Failed to create file input\n");
    return -1;
}
```

### nblex_input_pcap_new

```c
nblex_input* nblex_input_pcap_new(nblex_world* world, const char* interface);
```

Creates a packet capture input for network monitoring.

**Parameters:**
- `world`: World instance
- `interface`: Network interface name (e.g., "eth0", "any") or pcap file path

**Returns:** New input instance, or `NULL` on error.

**Example:**
```c
nblex_input* net_input = nblex_input_pcap_new(world, "eth0");
```

### nblex_input_set_format

```c
int nblex_input_set_format(nblex_input* input, nblex_log_format format);
```

Sets the log format for a file input.

**Parameters:**
- `input`: Input instance
- `format`: Log format (JSON, logfmt, etc.)

**Returns:** `0` on success, non-zero on error.

**Example:**
```c
nblex_input_set_format(log_input, NBLEX_FORMAT_JSON);
```

### nblex_input_set_filter

```c
int nblex_input_set_filter(nblex_input* input, const char* filter);
```

Sets a filter expression for an input.

**Parameters:**
- `input`: Input instance
- `filter`: Filter expression (e.g., "level == ERROR")

**Returns:** `0` on success, non-zero on error.

**Example:**
```c
nblex_input_set_filter(log_input, "log.level >= WARN");
nblex_input_set_filter(net_input, "tcp port 443");
```

### nblex_input_free

```c
void nblex_input_free(nblex_input* input);
```

Frees an input instance.

**Parameters:**
- `input`: Input instance

______________________________________________________________________

## Event API

### Event Types

```c
typedef enum {
    NBLEX_EVENT_LOG,           /* Log event */
    NBLEX_EVENT_NETWORK,       /* Network event */
    NBLEX_EVENT_CORRELATION,   /* Correlated event */
    NBLEX_EVENT_ERROR          /* Error event */
} nblex_event_type;
```

### Event Handler

```c
typedef void (*nblex_event_handler)(nblex_event* event, void* user_data);
```

### nblex_set_event_handler

```c
int nblex_set_event_handler(nblex_world* world,
                              nblex_event_handler handler,
                              void* user_data);
```

Sets the event callback function.

**Parameters:**
- `world`: World instance
- `handler`: Callback function
- `user_data`: User data passed to callback

**Returns:** `0` on success, non-zero on error.

**Example:**
```c
void on_event(nblex_event* event, void* user_data) {
    if (event->type == NBLEX_EVENT_CORRELATION) {
        printf("Correlated event!\n");
    }
}

nblex_set_event_handler(world, on_event, NULL);
```

### nblex_event_new

```c
nblex_event* nblex_event_new(nblex_event_type type, nblex_input* input);
```

Creates a new event (typically used internally or for testing).

**Parameters:**
- `type`: Event type
- `input`: Source input

**Returns:** New event instance, or `NULL` on error.

### nblex_event_free

```c
void nblex_event_free(nblex_event* event);
```

Frees an event instance.

**Parameters:**
- `event`: Event instance

### nblex_event_emit

```c
void nblex_event_emit(nblex_world* world, nblex_event* event);
```

Emits an event into the processing pipeline.

**Parameters:**
- `world`: World instance
- `event`: Event to emit

### nblex_event_to_json

```c
char* nblex_event_to_json(nblex_event* event);
```

Converts an event to JSON string.

**Parameters:**
- `event`: Event instance

**Returns:** JSON string (caller must free), or `NULL` on error.

**Example:**
```c
char* json = nblex_event_to_json(event);
if (json) {
    printf("%s\n", json);
    free(json);
}
```

______________________________________________________________________

## Correlation API

### Correlation Strategies

```c
typedef enum {
    NBLEX_CORR_TIME_BASED,    /* Time-based correlation */
    NBLEX_CORR_ID_BASED,      /* ID-based correlation */
    NBLEX_CORR_CONNECTION     /* Connection-based correlation */
} nblex_correlation_type;
```

### nblex_correlation_new

```c
nblex_correlation* nblex_correlation_new(nblex_world* world);
```

Creates a correlation engine.

**Parameters:**
- `world`: World instance

**Returns:** New correlation instance, or `NULL` on error.

### nblex_correlation_add_strategy

```c
int nblex_correlation_add_strategy(nblex_correlation* corr,
                                     nblex_correlation_type type,
                                     uint32_t window_ms);
```

Adds a correlation strategy.

**Parameters:**
- `corr`: Correlation instance
- `type`: Strategy type
- `window_ms`: Time window in milliseconds (for time-based)

**Returns:** `0` on success, non-zero on error.

**Example:**
```c
nblex_correlation* corr = nblex_correlation_new(world);
nblex_correlation_add_strategy(corr, NBLEX_CORR_TIME_BASED, 100);  /* 100ms window */
```

### nblex_correlation_free

```c
void nblex_correlation_free(nblex_correlation* corr);
```

Frees a correlation instance.

**Parameters:**
- `corr`: Correlation instance

______________________________________________________________________

## Filter API

### Filter Expressions

Filter expressions use a simple syntax for matching event fields.

**Operators:**
- `==` - Equal
- `!=` - Not equal
- `<`, `>`, `<=`, `>=` - Comparison
- `=~` - Regex match
- `!~` - Regex not match
- `AND`, `OR`, `NOT` - Boolean logic

**Examples:**
```
log.level == ERROR
log.level >= WARN
log.message =~ /timeout/i
log.level == ERROR AND network.port == 443
```

______________________________________________________________________

## nQL Query API

### Query Syntax

nQL (nblex Query Language) provides SQL-like syntax for streaming queries.

**Basic Filter:**
```
log.level == ERROR
```

**Correlation:**
```
correlate log.level == ERROR with network.dst_port == 3306 within 100ms
```

**Aggregation:**
```
aggregate count() where log.level == ERROR window 1m
aggregate count(), avg(network.latency_ms) by log.service window tumbling(1m)
```

**Pipeline:**
```
log.level >= WARN | aggregate count() by log.service
```

### nQL Functions

#### nql_parse

```c
void* nql_parse(const char* query);
```

Parses an nQL query string.

**Parameters:**
- `query`: nQL query string

**Returns:** Query context, or `NULL` on parse error.

#### nql_execute

```c
int nql_execute(void* query_ctx, nblex_world* world);
```

Executes a parsed nQL query.

**Parameters:**
- `query_ctx`: Query context from `nql_parse`
- `world`: World instance

**Returns:** `0` on success, non-zero on error.

#### nql_free

```c
void nql_free(void* query_ctx);
```

Frees a query context.

**Parameters:**
- `query_ctx`: Query context

**Example:**
```c
void* query = nql_parse("aggregate count() where log.level == ERROR window 1m");
if (!query) {
    fprintf(stderr, "Parse error\n");
    return -1;
}

nql_execute(query, world);
nql_free(query);
```

______________________________________________________________________

## Output API

### File Output

```c
void* nblex_file_output_new(const char* path, const char* format);
int nblex_file_output_write(void* output, nblex_event* event);
int nblex_file_output_set_rotation(void* output, int max_size_mb, int max_age_days, int max_count);
void nblex_file_output_free(void* output);
```

**Example:**
```c
void* file_out = nblex_file_output_new("/var/log/nblex/output.jsonl", "json");
nblex_file_output_set_rotation(file_out, 100, 7, 10);  /* 100MB, 7 days, 10 files */
```

### HTTP Output

```c
void* nblex_http_output_new(const char* url);
int nblex_http_output_send(void* output, nblex_event* event);
int nblex_http_output_set_method(void* output, const char* method);
int nblex_http_output_set_timeout(void* output, int timeout_seconds);
void nblex_http_output_free(void* output);
```

**Example:**
```c
void* http_out = nblex_http_output_new("https://webhook.example.com/events");
nblex_http_output_set_method(http_out, "POST");
nblex_http_output_set_timeout(http_out, 30);
```

### Metrics Output

```c
void* nblex_metrics_output_new(int port);
int nblex_metrics_output_record(void* output, nblex_event* event);
char* nblex_metrics_output_format(void* output);
void nblex_metrics_output_free(void* output);
```

**Example:**
```c
void* metrics = nblex_metrics_output_new(9090);
/* Metrics are automatically exposed on http://localhost:9090/metrics */
```

______________________________________________________________________

## Configuration API

### YAML Configuration

```c
int nblex_load_config(nblex_world* world, const char* config_path);
```

Loads configuration from a YAML file.

**Parameters:**
- `world`: World instance
- `config_path`: Path to configuration file

**Returns:** `0` on success, non-zero on error.

**Example:**
```c
if (nblex_load_config(world, "/etc/nblex/config.yaml") != 0) {
    fprintf(stderr, "Failed to load config\n");
    return -1;
}
```

______________________________________________________________________

## Error Handling

All functions that return `int` use the following convention:
- `0` - Success
- Non-zero - Error

Functions that return pointers return `NULL` on error.

Check `errno` for system errors.

**Example:**
```c
if (nblex_world_start(world) != 0) {
    fprintf(stderr, "Error: %s\n", strerror(errno));
    return -1;
}
```

______________________________________________________________________

## Memory Management

### Rules

1. **Caller-owned**: Objects created with `_new` must be freed with `_free`
2. **Reference counting**: Events are reference counted internally
3. **JSON data**: jansson library manages JSON memory
4. **Strings**: Strings returned by nblex functions must be freed with `free()`

### Example

```c
/* Create world - must free */
nblex_world* world = nblex_world_new();

/* Create input - must free */
nblex_input* input = nblex_input_file_new(world, "/var/log/app.log");

/* Process events... */

/* Free in reverse order */
nblex_input_free(input);
nblex_world_free(world);
```

______________________________________________________________________

## Examples

### Example 1: Basic Log Monitoring

```c
#include <nblex/nblex.h>
#include <stdio.h>

void on_event(nblex_event* event, void* user_data) {
    char* json = nblex_event_to_json(event);
    printf("%s\n", json);
    free(json);
}

int main() {
    nblex_world* world = nblex_world_new();
    nblex_world_open(world);

    nblex_input* input = nblex_input_file_new(world, "/var/log/app.log");
    nblex_input_set_format(input, NBLEX_FORMAT_JSON);
    nblex_input_set_filter(input, "log.level >= ERROR");

    nblex_set_event_handler(world, on_event, NULL);

    nblex_world_start(world);
    nblex_world_run(world);

    nblex_input_free(input);
    nblex_world_free(world);
    return 0;
}
```

### Example 2: Log and Network Correlation

```c
#include <nblex/nblex.h>
#include <stdio.h>

void on_event(nblex_event* event, void* user_data) {
    if (event->type == NBLEX_EVENT_CORRELATION) {
        printf("Correlation detected!\n");
        char* json = nblex_event_to_json(event);
        printf("%s\n", json);
        free(json);
    }
}

int main() {
    nblex_world* world = nblex_world_new();
    nblex_world_open(world);

    /* Add log input */
    nblex_input* log_input = nblex_input_file_new(world, "/var/log/app.log");
    nblex_input_set_format(log_input, NBLEX_FORMAT_JSON);

    /* Add network input */
    nblex_input* net_input = nblex_input_pcap_new(world, "eth0");
    nblex_input_set_filter(net_input, "tcp port 443");

    /* Enable correlation */
    nblex_correlation* corr = nblex_correlation_new(world);
    nblex_correlation_add_strategy(corr, NBLEX_CORR_TIME_BASED, 100);

    nblex_set_event_handler(world, on_event, NULL);

    nblex_world_start(world);
    nblex_world_run(world);

    nblex_correlation_free(corr);
    nblex_input_free(net_input);
    nblex_input_free(log_input);
    nblex_world_free(world);
    return 0;
}
```

### Example 3: nQL Aggregation

```c
#include <nblex/nblex.h>
#include <stdio.h>

void on_event(nblex_event* event, void* user_data) {
    char* json = nblex_event_to_json(event);
    printf("%s\n", json);
    free(json);
}

int main() {
    nblex_world* world = nblex_world_new();
    nblex_world_open(world);

    nblex_input* input = nblex_input_file_new(world, "/var/log/app.log");
    nblex_input_set_format(input, NBLEX_FORMAT_JSON);

    /* Parse and execute nQL query */
    void* query = nql_parse("aggregate count() by log.service where log.level == ERROR window 1m");
    nql_execute(query, world);

    nblex_set_event_handler(world, on_event, NULL);

    nblex_world_start(world);
    nblex_world_run(world);

    nql_free(query);
    nblex_input_free(input);
    nblex_world_free(world);
    return 0;
}
```

______________________________________________________________________

## Version History

- **0.5.0** (2025-11-13) - Added nQL API, enhanced correlation, output formatters
- **0.1.0** (2025-10-01) - Initial API release

______________________________________________________________________

## See Also

- [User Guide](USER_GUIDE.md)
- [Configuration Guide](CONFIGURATION.md)
- [nQL Reference](NQL_REFERENCE.md)
- [SPEC.md](../SPEC.md)
