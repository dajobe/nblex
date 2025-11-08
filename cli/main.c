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

  /* TODO: Configure inputs based on command-line arguments */
  if (log_path) {
    printf("Would monitor logs: %s\n", log_path);
  }

  if (network_iface) {
    printf("Would monitor network: %s\n", network_iface);
  }

  if (filter) {
    printf("Would apply filter: %s\n", filter);
  }

  printf("Output format: %s\n", output_format);

  /* TODO: Start processing */
  printf("\nNote: nblex is under development. Core functionality not yet implemented.\n");

  /* Cleanup */
  nblex_world_free(world);

  return 0;
}
