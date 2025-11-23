/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * pcap_input.c - PCAP network input agent
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

/* nblex_internal.h provides all network headers with correct BSD macros */
#include "../nblex_internal.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

/* TCP ECN flags - not always defined in system headers */
#ifndef TH_ECE
#define TH_ECE 0x40  /* ECN-Echo */
#endif
#ifndef TH_CWR
#define TH_CWR 0x80  /* Congestion Window Reduced */
#endif

/* Forward declarations */
static const nblex_input_vtable pcap_input_vtable;
static void packet_handler(u_char* user, const struct pcap_pkthdr* header, const u_char* packet);

/* Protocol dissector functions */
static void dissect_tcp(const u_char* packet, json_t* event);
static void dissect_udp(const u_char* packet, json_t* event);
static void dissect_icmp(const u_char* packet, json_t* event);

/* Poll callback for non-blocking pcap reads */
static void on_pcap_readable(uv_poll_t* handle, int status, int events) {
    nblex_input* input = (nblex_input*)handle->data;
    nblex_pcap_input_data* data = (nblex_pcap_input_data*)input->data;

    if (status < 0) {
        fprintf(stderr, "Poll error on pcap fd: %s\n", uv_strerror(status));
        return;
    }

    if (events & UV_READABLE) {
        /* Process up to 10 packets at a time to avoid blocking */
        int count = pcap_dispatch(data->pcap_handle, 10, packet_handler, (u_char*)input);

        if (count < 0) {
            fprintf(stderr, "Error in pcap_dispatch: %s\n", pcap_geterr(data->pcap_handle));
            uv_poll_stop(handle);
            data->capturing = false;
        } else if (count > 0) {
            data->packets_captured += count;
        }
    }
}

/* Packet handler callback */
static void packet_handler(u_char* user, const struct pcap_pkthdr* header, const u_char* packet) {
    nblex_input* input = (nblex_input*)user;
    nblex_world* world = input->world;
    nblex_pcap_input_data* data = (nblex_pcap_input_data*)input->data;

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

    nblex_pcap_input_data* data = calloc(1, sizeof(nblex_pcap_input_data));
    if (!data) {
        nblex_input_free(input);
        return NULL;
    }

    data->interface = strdup(interface);
    data->capturing = false;
    data->packets_captured = 0;
    data->packets_dropped = 0;
    input->data = data;

    /* Set vtable */
    input->vtable = &pcap_input_vtable;

    return input;
}

/* Start PCAP input */
static int pcap_input_start(nblex_input* input) {
    nblex_pcap_input_data* data = (nblex_pcap_input_data*)input->data;
    nblex_world* world = input->world;
    char errbuf[PCAP_ERRBUF_SIZE];
    bpf_u_int32 net, mask;

    /* Open interface */
    data->pcap_handle = pcap_open_live(
        data->interface,
        65535,              /* snapshot length */
        1,                  /* promiscuous mode */
        1,                  /* timeout in ms - small for non-blocking */
        errbuf
    );

    if (!data->pcap_handle) {
        fprintf(stderr, "Error opening interface %s: %s\n", data->interface, errbuf);
        return -1;
    }

    data->datalink = pcap_datalink(data->pcap_handle);

    /* Set non-blocking mode */
    if (pcap_setnonblock(data->pcap_handle, 1, errbuf) != 0) {
        fprintf(stderr, "Error setting non-blocking mode: %s\n", errbuf);
        pcap_close(data->pcap_handle);
        data->pcap_handle = NULL;
        return -1;
    }

    /* Get network info for filter compilation */
    if (pcap_lookupnet(data->interface, &net, &mask, errbuf) != 0) {
        fprintf(stderr, "Warning: Could not get network info for %s: %s\n", data->interface, errbuf);
        net = 0;
        mask = 0;
    }

    /* Compile and set filter if provided */
    if (input->filter) {
        /* Try to extract BPF-compatible predicates from nblex filter */
        char* bpf_filter = nblex_filter_to_bpf(input->filter);

        if (bpf_filter) {
            struct bpf_program fp;

            /* Compile the BPF filter */
            if (pcap_compile(data->pcap_handle, &fp, bpf_filter, 1, net) == -1) {
                fprintf(stderr, "Warning: Failed to compile BPF filter '%s': %s\n",
                       bpf_filter, pcap_geterr(data->pcap_handle));
                fprintf(stderr, "Continuing without BPF optimization\n");
            } else {
                /* Apply the BPF filter */
                if (pcap_setfilter(data->pcap_handle, &fp) == -1) {
                    fprintf(stderr, "Warning: Failed to set BPF filter '%s': %s\n",
                           bpf_filter, pcap_geterr(data->pcap_handle));
                    fprintf(stderr, "Continuing without BPF optimization\n");
                } else {
                    fprintf(stderr, "Applied BPF filter: %s\n", bpf_filter);
                }

                /* Free the compiled filter program */
                pcap_freecode(&fp);
            }

            free(bpf_filter);
        }
        /* Note: Full nblex filter will still be applied in userspace */
    }

    /* Get file descriptor for polling */
    data->pcap_fd = pcap_get_selectable_fd(data->pcap_handle);
    if (data->pcap_fd < 0) {
        fprintf(stderr, "Error: pcap_get_selectable_fd failed - interface may not support select()\n");
        pcap_close(data->pcap_handle);
        data->pcap_handle = NULL;
        return -1;
    }

    /* Initialize uv_poll for non-blocking reads */
    int rc = uv_poll_init(world->loop, &data->poll_handle, data->pcap_fd);
    if (rc != 0) {
        fprintf(stderr, "Error initializing uv_poll: %s\n", uv_strerror(rc));
        pcap_close(data->pcap_handle);
        data->pcap_handle = NULL;
        return -1;
    }

    /* Store input pointer in poll handle for callback */
    data->poll_handle.data = input;

    /* Start polling for readable events */
    rc = uv_poll_start(&data->poll_handle, UV_READABLE, on_pcap_readable);
    if (rc != 0) {
        fprintf(stderr, "Error starting uv_poll: %s\n", uv_strerror(rc));
        uv_close((uv_handle_t*)&data->poll_handle, NULL);
        pcap_close(data->pcap_handle);
        data->pcap_handle = NULL;
        return -1;
    }

    data->capturing = true;
    return 0;
}

/* Close callback for uv_poll */
static void on_poll_close(uv_handle_t* handle) {
    /* Poll handle closed successfully */
}

/* Stop PCAP input */
static int pcap_input_stop(nblex_input* input) {
    nblex_pcap_input_data* data = (nblex_pcap_input_data*)input->data;

    if (data->capturing) {
        /* Stop polling */
        uv_poll_stop(&data->poll_handle);

        /* Close the poll handle */
        uv_close((uv_handle_t*)&data->poll_handle, on_poll_close);

        data->capturing = false;
    }

    if (data->pcap_handle) {
        pcap_close(data->pcap_handle);
        data->pcap_handle = NULL;
    }

    return 0;
}

/* Free PCAP input */
static void pcap_input_free(nblex_input* input) {
    nblex_pcap_input_data* data = (nblex_pcap_input_data*)input->data;

    if (data) {
        /* Stop capture if still running */
        if (data->capturing) {
            pcap_input_stop(input);
        }

        if (data->pcap_handle) {
            pcap_close(data->pcap_handle);
        }

        free(data->interface);
        free(data);
    }
}

/* PCAP input vtable */
static const nblex_input_vtable pcap_input_vtable = {
    .name = "pcap",
    .start = pcap_input_start,
    .stop = pcap_input_stop,
    .free = pcap_input_free
};
