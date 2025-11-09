/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * pcap_input.c - PCAP network input agent
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include <sys/types.h>  /* For BSD type definitions */
#include <pcap.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <netinet/if_ether.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../nblex_internal.h"

/* PCAP input agent state */
typedef struct {
    pcap_t* handle;
    char* interface;
    char* filter;
    bpf_u_int32 net;
    bpf_u_int32 mask;
    int datalink;
} pcap_input_data_t;

/* Protocol dissector functions */
static void dissect_tcp(const u_char* packet, json_t* event);
static void dissect_udp(const u_char* packet, json_t* event);
static void dissect_icmp(const u_char* packet, json_t* event);

/* Packet handler callback */
static void packet_handler(u_char* user, const struct pcap_pkthdr* header, const u_char* packet) {
    nblex_input* input = (nblex_input*)user;
    nblex_world* world = input->world;
    pcap_input_data_t* data = (pcap_input_data_t*)input->data;

    /* Create event */
    nblex_event* event = nblex_event_new(NBLEX_EVENT_NETWORK, input);
    if (!event) {
        return;
    }

    json_t* json_data = json_object();
    if (!json_data) {
        nblex_event_free(event);
        return;
    }

    /* Basic packet info */
    json_object_set_new(json_data, "timestamp", json_real(header->ts.tv_sec + header->ts.tv_usec / 1000000.0));
    json_object_set_new(json_data, "length", json_integer(header->len));
    json_object_set_new(json_data, "captured_length", json_integer(header->caplen));
    json_object_set_new(json_data, "interface", json_string(data->interface));

    /* Parse packet based on datalink type */
    if (data->datalink == DLT_EN10MB) {
        /* Ethernet frame */
        struct ether_header* eth = (struct ether_header*)packet;

        /* Source and destination MAC addresses */
        char src_mac[18], dst_mac[18];
        snprintf(src_mac, sizeof(src_mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                eth->ether_shost[0], eth->ether_shost[1], eth->ether_shost[2],
                eth->ether_shost[3], eth->ether_shost[4], eth->ether_shost[5]);
        snprintf(dst_mac, sizeof(dst_mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                eth->ether_dhost[0], eth->ether_dhost[1], eth->ether_dhost[2],
                eth->ether_dhost[3], eth->ether_dhost[4], eth->ether_dhost[5]);

        json_object_set_new(json_data, "ethernet_src", json_string(src_mac));
        json_object_set_new(json_data, "ethernet_dst", json_string(dst_mac));
        json_object_set_new(json_data, "ethernet_type", json_integer(ntohs(eth->ether_type)));

        /* Skip Ethernet header */
        packet += sizeof(struct ether_header);
        size_t remaining = header->caplen - sizeof(struct ether_header);

        /* IP packet */
        if (ntohs(eth->ether_type) == ETHERTYPE_IP && remaining >= sizeof(struct ip)) {
            struct ip* ip = (struct ip*)packet;

            char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ip->ip_src, src_ip, sizeof(src_ip));
            inet_ntop(AF_INET, &ip->ip_dst, dst_ip, sizeof(dst_ip));

            json_object_set_new(json_data, "ip_version", json_integer(ip->ip_v));
            json_object_set_new(json_data, "ip_src", json_string(src_ip));
            json_object_set_new(json_data, "ip_dst", json_string(dst_ip));
            json_object_set_new(json_data, "ip_protocol", json_integer(ip->ip_p));
            json_object_set_new(json_data, "ip_ttl", json_integer(ip->ip_ttl));
            json_object_set_new(json_data, "ip_length", json_integer(ntohs(ip->ip_len)));

            /* Skip IP header */
            int ip_header_len = ip->ip_hl * 4;
            packet += ip_header_len;
            remaining -= ip_header_len;

            /* Transport layer */
            switch (ip->ip_p) {
                case IPPROTO_TCP:
                    if (remaining >= sizeof(struct tcphdr)) {
                        dissect_tcp(packet, json_data);
                    }
                    break;
                case IPPROTO_UDP:
                    if (remaining >= sizeof(struct udphdr)) {
                        dissect_udp(packet, json_data);
                    }
                    break;
                case IPPROTO_ICMP:
                    if (remaining >= sizeof(struct icmp)) {
                        dissect_icmp(packet, json_data);
                    }
                    break;
            }
        }
    }

    /* Set event data */
    event->data = json_data;

    /* Emit event */
    nblex_event_emit(world, event);

    /* Clean up */
    nblex_event_free(event);
}

/* TCP dissector */
static void dissect_tcp(const u_char* packet, json_t* event) {
    struct tcphdr* tcp = (struct tcphdr*)packet;

    json_object_set_new(event, "protocol", json_string("tcp"));
    json_object_set_new(event, "tcp_src_port", json_integer(ntohs(tcp->th_sport)));
    json_object_set_new(event, "tcp_dst_port", json_integer(ntohs(tcp->th_dport)));
    json_object_set_new(event, "tcp_seq", json_integer(ntohl(tcp->th_seq)));
    json_object_set_new(event, "tcp_ack", json_integer(ntohl(tcp->th_ack)));

    /* TCP flags */
    json_object_set_new(event, "tcp_flags_fin", json_boolean(tcp->th_flags & TH_FIN));
    json_object_set_new(event, "tcp_flags_syn", json_boolean(tcp->th_flags & TH_SYN));
    json_object_set_new(event, "tcp_flags_rst", json_boolean(tcp->th_flags & TH_RST));
    json_object_set_new(event, "tcp_flags_psh", json_boolean(tcp->th_flags & TH_PUSH));
    json_object_set_new(event, "tcp_flags_ack", json_boolean(tcp->th_flags & TH_ACK));
    json_object_set_new(event, "tcp_flags_urg", json_boolean(tcp->th_flags & TH_URG));
    json_object_set_new(event, "tcp_flags_ece", json_boolean(tcp->th_flags & TH_ECE));
    json_object_set_new(event, "tcp_flags_cwr", json_boolean(tcp->th_flags & TH_CWR));

    json_object_set_new(event, "tcp_window", json_integer(ntohs(tcp->th_win)));
    json_object_set_new(event, "tcp_checksum", json_integer(ntohs(tcp->th_sum)));
    json_object_set_new(event, "tcp_urgent", json_integer(ntohs(tcp->th_urp)));
}

/* UDP dissector */
static void dissect_udp(const u_char* packet, json_t* event) {
    struct udphdr* udp = (struct udphdr*)packet;

    json_object_set_new(event, "protocol", json_string("udp"));
    json_object_set_new(event, "udp_src_port", json_integer(ntohs(udp->uh_sport)));
    json_object_set_new(event, "udp_dst_port", json_integer(ntohs(udp->uh_dport)));
    json_object_set_new(event, "udp_length", json_integer(ntohs(udp->uh_ulen)));
    json_object_set_new(event, "udp_checksum", json_integer(ntohs(udp->uh_sum)));
}

/* ICMP dissector */
static void dissect_icmp(const u_char* packet, json_t* event) {
    struct icmp* icmp = (struct icmp*)packet;

    json_object_set_new(event, "protocol", json_string("icmp"));
    json_object_set_new(event, "icmp_type", json_integer(icmp->icmp_type));
    json_object_set_new(event, "icmp_code", json_integer(icmp->icmp_code));
    json_object_set_new(event, "icmp_checksum", json_integer(ntohs(icmp->icmp_cksum)));
}

/* Create PCAP input agent */
nblex_input* nblex_input_pcap_new(nblex_world* world, const char* interface) {
    if (!world || !interface) {
        return NULL;
    }

    nblex_input* input = nblex_input_new(world, NBLEX_INPUT_PCAP);
    if (!input) {
        return NULL;
    }

    pcap_input_data_t* data = calloc(1, sizeof(pcap_input_data_t));
    if (!data) {
        nblex_input_free(input);
        return NULL;
    }

    data->interface = strdup(interface);
    input->data = data;

    return input;
}

/* Start PCAP input */
static int pcap_input_start(nblex_input* input) {
    pcap_input_data_t* data = (pcap_input_data_t*)input->data;
    char errbuf[PCAP_ERRBUF_SIZE];

    /* Open interface */
    data->handle = pcap_open_live(
        data->interface,
        65535,              /* snapshot length */
        1,                  /* promiscuous mode */
        1000,               /* timeout */
        errbuf
    );

    if (!data->handle) {
        fprintf(stderr, "Error opening interface %s: %s\n", data->interface, errbuf);
        return -1;
    }

    /* Get network info */
    if (pcap_lookupnet(data->interface, &data->net, &data->mask, errbuf) != 0) {
        fprintf(stderr, "Warning: Could not get network info for %s: %s\n", data->interface, errbuf);
        data->net = 0;
        data->mask = 0;
    }

    /* Get datalink type */
    data->datalink = pcap_datalink(data->handle);

    /* Compile and set filter */
    if (data->filter) {
        struct bpf_program fp;
        if (pcap_compile(data->handle, &fp, data->filter, 0, data->net) != 0) {
            fprintf(stderr, "Error compiling filter '%s': %s\n",
                   data->filter, pcap_geterr(data->handle));
            pcap_close(data->handle);
            return -1;
        }

        if (pcap_setfilter(data->handle, &fp) != 0) {
            fprintf(stderr, "Error setting filter: %s\n", pcap_geterr(data->handle));
            pcap_freecode(&fp);
            pcap_close(data->handle);
            return -1;
        }

        pcap_freecode(&fp);
    }

    /* Start capture in separate thread */
    if (pcap_loop(data->handle, -1, packet_handler, (u_char*)input) != 0) {
        fprintf(stderr, "Error in pcap_loop: %s\n", pcap_geterr(data->handle));
        pcap_close(data->handle);
        return -1;
    }

    return 0;
}

/* Stop PCAP input */
static int pcap_input_stop(nblex_input* input) {
    pcap_input_data_t* data = (pcap_input_data_t*)input->data;

    if (data->handle) {
        pcap_breakloop(data->handle);
        pcap_close(data->handle);
        data->handle = NULL;
    }

    return 0;
}

/* Free PCAP input */
static void pcap_input_free(nblex_input* input) {
    pcap_input_data_t* data = (pcap_input_data_t*)input->data;

    if (data) {
        if (data->handle) {
            pcap_close(data->handle);
        }

        free(data->interface);
        free(data->filter);
        free(data);
    }
}

/* PCAP input vtable */
static nblex_input_vtable pcap_input_vtable = {
    .name = "pcap",
    .start = pcap_input_start,
    .stop = pcap_input_stop,
    .free = pcap_input_free
};
