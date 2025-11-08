/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * pcap_input.c - Network packet capture input
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>

/* Forward declarations */
static int pcap_input_start(nblex_input* input);
static int pcap_input_stop(nblex_input* input);
static void pcap_input_free_data(nblex_input* input);
static void pcap_poll_cb(uv_poll_t* handle, int status, int events);

/* Virtual table for pcap input */
static const nblex_input_vtable pcap_input_vtable = {
  .name = "pcap",
  .start = pcap_input_start,
  .stop = pcap_input_stop,
  .free = pcap_input_free_data
};

nblex_input* nblex_input_pcap_new(nblex_world* world, const char* interface) {
  if (!world || !interface) {
    return NULL;
  }

  nblex_input* input = nblex_input_new(world, NBLEX_INPUT_PCAP);
  if (!input) {
    return NULL;
  }

  nblex_pcap_input_data* data = calloc(1, sizeof(nblex_pcap_input_data));
  if (!data) {
    free(input);
    return NULL;
  }

  data->interface = nblex_strdup(interface);
  if (!data->interface) {
    free(data);
    free(input);
    return NULL;
  }

  data->pcap_handle = NULL;
  data->capturing = false;
  data->packets_captured = 0;
  data->packets_dropped = 0;

  input->data = data;
  input->vtable = &pcap_input_vtable;

  /* Add to world */
  nblex_world_add_input(world, input);

  return input;
}

static int pcap_input_start(nblex_input* input) {
  if (!input || !input->data) {
    return -1;
  }

  nblex_pcap_input_data* data = (nblex_pcap_input_data*)input->data;

  char errbuf[PCAP_ERRBUF_SIZE];

  /* Open pcap handle on interface */
  data->pcap_handle = pcap_open_live(data->interface,
                                      65535,   /* snaplen */
                                      1,       /* promiscuous mode */
                                      100,     /* read timeout (ms) */
                                      errbuf);
  if (!data->pcap_handle) {
    fprintf(stderr, "Error opening pcap on %s: %s\n", data->interface, errbuf);
    return -1;
  }

  /* Set non-blocking mode */
  if (pcap_setnonblock(data->pcap_handle, 1, errbuf) < 0) {
    fprintf(stderr, "Error setting non-blocking mode: %s\n", errbuf);
    pcap_close(data->pcap_handle);
    data->pcap_handle = NULL;
    return -1;
  }

  /* Get file descriptor for polling */
  data->pcap_fd = pcap_get_selectable_fd(data->pcap_handle);
  if (data->pcap_fd < 0) {
    fprintf(stderr, "Error: pcap_get_selectable_fd failed\n");
    pcap_close(data->pcap_handle);
    data->pcap_handle = NULL;
    return -1;
  }

  /* Initialize libuv poll handle */
  uv_poll_init(input->world->loop, &data->poll_handle, data->pcap_fd);
  data->poll_handle.data = input;

  /* Start polling for readable events */
  uv_poll_start(&data->poll_handle, UV_READABLE, pcap_poll_cb);

  data->capturing = true;

  return 0;
}

static int pcap_input_stop(nblex_input* input) {
  if (!input || !input->data) {
    return -1;
  }

  nblex_pcap_input_data* data = (nblex_pcap_input_data*)input->data;

  if (data->capturing) {
    uv_poll_stop(&data->poll_handle);
    uv_close((uv_handle_t*)&data->poll_handle, NULL);
    data->capturing = false;
  }

  if (data->pcap_handle) {
    pcap_close(data->pcap_handle);
    data->pcap_handle = NULL;
  }

  return 0;
}

static void pcap_input_free_data(nblex_input* input) {
  if (!input || !input->data) {
    return;
  }

  nblex_pcap_input_data* data = (nblex_pcap_input_data*)input->data;

  if (data->interface) {
    free(data->interface);
  }

  free(data);
}

/* Process a single packet */
static void process_packet(nblex_input* input, const struct pcap_pkthdr* header,
                           const u_char* packet) {
  nblex_pcap_input_data* data = (nblex_pcap_input_data*)input->data;
  data->packets_captured++;

  /* Parse Ethernet header (14 bytes) */
  if (header->caplen < 14) {
    return; /* Packet too small */
  }

  /* Skip Ethernet header, get IP header */
  const struct ip* ip_header = (struct ip*)(packet + 14);

  /* Verify we have at least IP header */
  if (header->caplen < 14 + sizeof(struct ip)) {
    return;
  }

  /* Only process IPv4 for now */
  if (ip_header->ip_v != 4) {
    return;
  }

  /* Create event */
  nblex_event* event = nblex_event_new(NBLEX_EVENT_NETWORK, input);
  if (!event) {
    return;
  }

  /* Create JSON object for packet data */
  json_t* packet_data = json_object();
  if (!packet_data) {
    nblex_event_free(event);
    return;
  }

  /* Add basic packet information */
  char src_ip[INET_ADDRSTRLEN];
  char dst_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &ip_header->ip_src, src_ip, INET_ADDRSTRLEN);
  inet_ntop(AF_INET, &ip_header->ip_dst, dst_ip, INET_ADDRSTRLEN);

  json_object_set_new(packet_data, "src_ip", json_string(src_ip));
  json_object_set_new(packet_data, "dst_ip", json_string(dst_ip));
  json_object_set_new(packet_data, "protocol", json_integer(ip_header->ip_p));
  json_object_set_new(packet_data, "length", json_integer(ntohs(ip_header->ip_len)));

  /* Parse TCP/UDP ports if available */
  int ip_header_len = ip_header->ip_hl * 4;
  const u_char* transport_header = packet + 14 + ip_header_len;

  if (ip_header->ip_p == IPPROTO_TCP) {
    if (header->caplen >= 14 + ip_header_len + sizeof(struct tcphdr)) {
      const struct tcphdr* tcp = (struct tcphdr*)transport_header;
      json_object_set_new(packet_data, "protocol_name", json_string("TCP"));
      json_object_set_new(packet_data, "src_port", json_integer(ntohs(tcp->th_sport)));
      json_object_set_new(packet_data, "dst_port", json_integer(ntohs(tcp->th_dport)));
    }
  } else if (ip_header->ip_p == IPPROTO_UDP) {
    if (header->caplen >= 14 + ip_header_len + sizeof(struct udphdr)) {
      const struct udphdr* udp = (struct udphdr*)transport_header;
      json_object_set_new(packet_data, "protocol_name", json_string("UDP"));
      json_object_set_new(packet_data, "src_port", json_integer(ntohs(udp->uh_sport)));
      json_object_set_new(packet_data, "dst_port", json_integer(ntohs(udp->uh_dport)));
    }
  } else {
    json_object_set_new(packet_data, "protocol_name", json_string("OTHER"));
  }

  event->data = packet_data;

  /* Emit event */
  nblex_event_emit(input->world, event);
}

static void pcap_poll_cb(uv_poll_t* handle, int status, int events) {
  if (status < 0) {
    fprintf(stderr, "Poll error: %s\n", uv_strerror(status));
    return;
  }

  if (!(events & UV_READABLE)) {
    return;
  }

  nblex_input* input = (nblex_input*)handle->data;
  if (!input || !input->data) {
    return;
  }

  nblex_pcap_input_data* data = (nblex_pcap_input_data*)input->data;
  if (!data->pcap_handle) {
    return;
  }

  /* Process available packets (non-blocking) */
  struct pcap_pkthdr* header;
  const u_char* packet;
  int result;

  while ((result = pcap_next_ex(data->pcap_handle, &header, &packet)) == 1) {
    process_packet(input, header, packet);
  }

  if (result == -1) {
    fprintf(stderr, "Error reading packet: %s\n", pcap_geterr(data->pcap_handle));
  }
}
