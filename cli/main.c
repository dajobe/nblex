/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * main.c - nblex command-line tool
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "nblex/nblex.h"
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
  printf("  -o, --output FORMAT     Output format (json|text)\n");
  printf("  -c, --config FILE       Configuration file\n");
  printf("  -v, --version           Show version\n");
  printf("  -h, --help              Show this help\n");
  printf("\n");
  printf("Examples:\n");
  printf("  %s --logs /var/log/app.log --output json\n", program);
  printf("  %s --logs /var/log/app.log --network eth0\n", program);
  printf("  %s --config /etc/nblex/config.yaml\n", program);
}

static void event_handler_json(nblex_event* event, void* user_data) {
  (void)user_data;  /* Unused */

  if (!event) {
    return;
  }

  /* Convert event to JSON and print */
  char* json_str = nblex_event_to_json(event);
  if (json_str) {
    printf("%s\n", json_str);
    fflush(stdout);
    free(json_str);
  }
}

int main(int argc, char** argv) {
  const char* log_path = NULL;
  const char* network_iface = NULL;
  const char* filter = NULL;
  const char* output_format = "json";
  const char* config_file = NULL;

  static struct option long_options[] = {
    {"logs",    required_argument, 0, 'l'},
    {"network", required_argument, 0, 'n'},
    {"filter",  required_argument, 0, 'f'},
    {"output",  required_argument, 0, 'o'},
    {"config",  required_argument, 0, 'c'},
    {"version", no_argument,       0, 'v'},
    {"help",    no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };

  int opt;
  int option_index = 0;

  while ((opt = getopt_long(argc, argv, "l:n:f:o:c:vh",
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
      case 'o':
        output_format = optarg;
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

  /* Configure inputs based on command-line arguments */
  nblex_input* log_input = NULL;
  nblex_input* pcap_input = NULL;

  if (log_path) {
    log_input = nblex_input_file_new(world, log_path);
    if (!log_input) {
      fprintf(stderr, "Error: Failed to create log input for %s\n", log_path);
      nblex_world_free(world);
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
      return 1;
    }
    printf("Monitoring network: %s\n", network_iface);
  }

  if (filter) {
    fprintf(stderr, "Warning: Filtering not yet implemented\n");
    fprintf(stderr, "         Filter '%s' will be ignored\n", filter);
  }

  /* Set up event handler */
  if (strcmp(output_format, "json") == 0) {
    nblex_set_event_handler(world, event_handler_json, NULL);
  } else {
    fprintf(stderr, "Error: Unsupported output format '%s'\n", output_format);
    fprintf(stderr, "       Only 'json' is currently supported\n");
    nblex_world_free(world);
    return 1;
  }

  /* Start processing */
  printf("Starting nblex...\n");

  if (nblex_world_start(world) != 0) {
    fprintf(stderr, "Error: Failed to start nblex world\n");
    nblex_world_free(world);
    return 1;
  }

  printf("Running... (Press Ctrl+C to stop)\n\n");

  /* Run event loop (blocking) */
  int result = nblex_world_run(world);

  if (result != 0) {
    fprintf(stderr, "Error: Event loop exited with error\n");
  }

  /* Cleanup */
  nblex_world_free(world);

  return 0;
}
