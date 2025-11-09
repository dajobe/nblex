/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * main.c - nblex command-line tool
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "nblex/nblex.h"
#include "../src/nblex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static void print_version(void) {
  printf("nblex %s\n", nblex_version_string());
  printf("Network & Buffer Log EXplorer\n");
  printf("Copyright (C) 2025\n");
  printf("Licensed under Apache License 2.0\n");
}

static void print_usage(const char* program) {
  printf("Usage: %s [OPTIONS]\n\n", program);
  printf("Options:\n");
  printf("  -l, --logs PATH         Monitor log file(s)\n");
  printf("  -n, --network IFACE     Monitor network interface\n");
  printf("  -f, --filter EXPR       Filter expression\n");
  printf("  -q, --query QUERY       nQL query expression\n");
  printf("  -o, --output FORMAT     Output format (json|file|http|metrics)\n");
  printf("  -O, --output-file PATH  Output file path (for file output)\n");
  printf("  -U, --output-url URL    Output URL (for http output)\n");
  printf("  -c, --config FILE       Configuration file\n");
  printf("  -v, --version           Show version\n");
  printf("  -h, --help              Show this help\n");
  printf("\n");
  printf("Examples:\n");
  printf("  %s --logs /var/log/app.log --output json\n", program);
  printf("  %s --logs /var/log/app.log --output file --output-file /tmp/events.jsonl\n", program);
  printf("  %s --logs /var/log/app.log --network eth0 --output http --output-url http://localhost:8080/events\n", program);
  printf("  %s --config /etc/nblex/config.yaml\n", program);
}

/* Output handlers */
static file_output_t* file_output = NULL;
static http_output_t* http_output = NULL;
static metrics_output_t* metrics_output = NULL;

static void event_handler_json(nblex_event* event, void* user_data) {
  (void)user_data;

  if (!event) {
    return;
  }

  char* json_str = nblex_event_to_json(event);
  if (json_str) {
    printf("%s\n", json_str);
    fflush(stdout);
    free(json_str);
  }
}

static void event_handler_multi(nblex_event* event, void* user_data) {
  (void)user_data;  /* Unused */

  if (!event) {
    return;
  }

  /* JSON output (stdout) */
  char* json_str = nblex_event_to_json(event);
  if (json_str) {
    printf("%s\n", json_str);
    fflush(stdout);
    free(json_str);
  }

  /* File output */
  if (file_output) {
    nblex_file_output_write(file_output, event);
  }

  /* HTTP output */
  if (http_output) {
    nblex_http_output_write(http_output, event);
  }

  /* Metrics output */
  if (metrics_output) {
    nblex_metrics_output_write(metrics_output, event);
  }
}

static void event_handler_query(nblex_event* event, void* user_data) {
  const char* query_str = (const char*)user_data;
  
  if (!event || !query_str) {
    return;
  }
  
  /* Execute query on event */
  nblex_world* world = event->input ? event->input->world : NULL;
  if (world && nql_execute(query_str, event, world)) {
    /* Query matched - output event */
    char* json_str = nblex_event_to_json(event);
    if (json_str) {
      printf("%s\n", json_str);
      fflush(stdout);
      free(json_str);
    }
  }
}

int main(int argc, char** argv) {
  const char* log_path = NULL;
  const char* network_iface = NULL;
  const char* filter = NULL;
  const char* query = NULL;
  const char* output_format = "json";
  const char* output_file = NULL;
  const char* output_url = NULL;
  const char* config_file = NULL;

  static struct option long_options[] = {
    {"logs",       required_argument, 0, 'l'},
    {"network",   required_argument, 0, 'n'},
    {"filter",    required_argument, 0, 'f'},
    {"query",     required_argument, 0, 'q'},
    {"output",    required_argument, 0, 'o'},
    {"output-file", required_argument, 0, 'O'},
    {"output-url", required_argument, 0, 'U'},
    {"config",    required_argument, 0, 'c'},
    {"version",   no_argument,       0, 'v'},
    {"help",      no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };

  int opt;
  int option_index = 0;

  while ((opt = getopt_long(argc, argv, "l:n:f:q:o:O:U:c:vh",
                            long_options, &option_index)) != -1) {
    switch (opt) {
      case 'l':
        log_path = optarg;
        break;
      case 'n':
        network_iface = optarg;
        break;
      case 'f':
        filter = optarg;
        break;
      case 'q':
        query = optarg;
        break;
      case 'o':
        output_format = optarg;
        break;
      case 'O':
        output_file = optarg;
        break;
      case 'U':
        output_url = optarg;
        break;
      case 'c':
        config_file = optarg;
        break;
      case 'v':
        print_version();
        return 0;
      case 'h':
        print_usage(argv[0]);
        return 0;
      default:
        print_usage(argv[0]);
        return 1;
    }
  }

  if (!log_path && !network_iface && !config_file) {
    fprintf(stderr, "Error: Must specify --logs, --network, or --config\n\n");
    print_usage(argv[0]);
    return 1;
  }

  /* Initialize nblex */
  nblex_world* world = nblex_world_new();
  if (!world) {
    fprintf(stderr, "Error: Failed to create nblex world\n");
    return 1;
  }

  if (nblex_world_open(world) != 0) {
    fprintf(stderr, "Error: Failed to open nblex world\n");
    nblex_world_free(world);
    return 1;
  }

  /* Load configuration file if specified */
  nblex_config_t* config = NULL;
  if (config_file) {
    config = nblex_config_load_yaml(config_file);
    if (!config) {
      fprintf(stderr, "Error: Failed to load configuration file '%s'\n", config_file);
      nblex_world_free(world);
      return 1;
    }
    printf("Loaded configuration from %s\n", config_file);
    
    /* Apply configuration */
    if (nblex_config_apply(config, world) != 0) {
      fprintf(stderr, "Warning: Failed to apply some configuration settings\n");
    }
  }

  /* Configure inputs based on command-line arguments (if not using config file) */
  nblex_input* log_input = NULL;
  nblex_input* pcap_input = NULL;

  if (!config_file) {
    if (log_path) {
      log_input = nblex_input_file_new(world, log_path);
      if (!log_input) {
        fprintf(stderr, "Error: Failed to create log input for %s\n", log_path);
        nblex_world_free(world);
        if (config) nblex_config_free(config);
        return 1;
      }

      /* Set format to JSON by default */
      nblex_input_set_format(log_input, NBLEX_FORMAT_JSON);
      printf("Monitoring logs: %s (format: json)\n", log_path);
    }

    if (network_iface) {
      pcap_input = nblex_input_pcap_new(world, network_iface);
      if (!pcap_input) {
        fprintf(stderr, "Error: Failed to create pcap input for %s\n", network_iface);
        fprintf(stderr, "       Make sure you have permission to capture packets (try running with sudo)\n");
        nblex_world_free(world);
        if (config) nblex_config_free(config);
        return 1;
      }
      printf("Monitoring network: %s\n", network_iface);
    }

    if (filter) {
      /* Apply filter to all inputs */
      if (log_input) {
        if (nblex_input_set_filter(log_input, filter) != 0) {
          fprintf(stderr, "Warning: Failed to set filter '%s' on log input\n", filter);
        } else {
          printf("Filter applied: %s\n", filter);
        }
      }
      if (pcap_input) {
        if (nblex_input_set_filter(pcap_input, filter) != 0) {
          fprintf(stderr, "Warning: Failed to set filter '%s' on network input\n", filter);
        }
      }
    }
  }

  /* Set up output handlers */
  if (strcmp(output_format, "file") == 0) {
    if (!output_file) {
      fprintf(stderr, "Error: --output-file required for file output\n");
      nblex_world_free(world);
      if (config) nblex_config_free(config);
      return 1;
    }
    file_output = nblex_file_output_new(output_file, "json");
    if (!file_output) {
      fprintf(stderr, "Error: Failed to create file output\n");
      nblex_world_free(world);
      if (config) nblex_config_free(config);
      return 1;
    }
    nblex_set_event_handler(world, event_handler_multi, NULL);
    printf("Writing to file: %s\n", output_file);
  } else if (strcmp(output_format, "http") == 0) {
    if (!output_url) {
      fprintf(stderr, "Error: --output-url required for http output\n");
      nblex_world_free(world);
      if (config) nblex_config_free(config);
      return 1;
    }
    http_output = nblex_http_output_new(output_url);
    if (!http_output) {
      fprintf(stderr, "Error: Failed to create HTTP output\n");
      nblex_world_free(world);
      if (config) nblex_config_free(config);
      return 1;
    }
    nblex_set_event_handler(world, event_handler_multi, NULL);
    printf("Sending to URL: %s\n", output_url);
  } else if (strcmp(output_format, "metrics") == 0) {
    if (!output_file) {
      fprintf(stderr, "Error: --output-file required for metrics output\n");
      nblex_world_free(world);
      if (config) nblex_config_free(config);
      return 1;
    }
    metrics_output = nblex_metrics_output_new(output_file, "prometheus");
    if (!metrics_output) {
      fprintf(stderr, "Error: Failed to create metrics output\n");
      nblex_world_free(world);
      if (config) nblex_config_free(config);
      return 1;
    }
    nblex_set_event_handler(world, event_handler_multi, NULL);
    printf("Writing metrics to: %s\n", output_file);
  } else if (strcmp(output_format, "json") == 0) {
    if (query) {
      /* Use query handler */
      nblex_set_event_handler(world, event_handler_query, (void*)query);
      printf("Query: %s\n", query);
    } else {
      nblex_set_event_handler(world, event_handler_json, NULL);
    }
  } else {
    fprintf(stderr, "Error: Unsupported output format '%s'\n", output_format);
    fprintf(stderr, "       Supported formats: json, file, http, metrics\n");
    nblex_world_free(world);
    if (config) nblex_config_free(config);
    return 1;
  }

  /* Start processing */
  printf("Starting nblex...\n");

  if (nblex_world_start(world) != 0) {
    fprintf(stderr, "Error: Failed to start nblex world\n");
    nblex_world_free(world);
    if (config) nblex_config_free(config);
    if (file_output) nblex_file_output_free(file_output);
    if (http_output) nblex_http_output_free(http_output);
    if (metrics_output) nblex_metrics_output_free(metrics_output);
    return 1;
  }

  printf("Running... (Press Ctrl+C to stop)\n\n");

  /* Run event loop (blocking) */
  int result = nblex_world_run(world);

  if (result != 0) {
    fprintf(stderr, "Error: Event loop exited with error\n");
  }

  /* Cleanup */
  if (file_output) nblex_file_output_free(file_output);
  if (http_output) nblex_http_output_free(http_output);
  if (metrics_output) nblex_metrics_output_free(metrics_output);
  if (config) nblex_config_free(config);
  nblex_world_free(world);

  return 0;
}
