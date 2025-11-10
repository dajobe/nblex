/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * dns_parser.c - DNS protocol dissector
 *
 * Copyright (C) 2025
 * Licensed under the Apache License, Version 2.0
 */

#include "../nblex_internal.h"
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* DNS header structure */
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

/* DNS question structure */
typedef struct {
    uint16_t qtype;
    uint16_t qclass;
} dns_question_t;

/* DNS resource record structure */
typedef struct {
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
} dns_rr_t;

/* Extract DNS name from compressed format */
static char* extract_dns_name(const u_char* packet, size_t packet_len, size_t* offset, const u_char* start) {
    char name[256] = {0};
    char* ptr = name;
    size_t max_len = sizeof(name) - 1;
    int jumped = 0;
    size_t jump_offset = 0;

    while (*offset < packet_len) {
        uint8_t len = packet[*offset];
        (*offset)++;

        if (len == 0) {
            break; /* End of name */
        }

        if ((len & 0xC0) == 0xC0) {
            /* Compressed name pointer */
            if (!jumped) {
                jump_offset = *offset + 1;
                jumped = 1;
            }
            uint16_t pointer = ((len & 0x3F) << 8) | packet[*offset];
            (*offset)++;
            *offset = pointer;
            continue;
        }

        if ((size_t)(ptr - name) + len + 1 >= max_len) {
            break; /* Name too long */
        }

        if (ptr != name) {
            *ptr++ = '.';
        }

        memcpy(ptr, packet + *offset, len);
        ptr += len;
        *offset += len;
    }

    if (jumped) {
        *offset = jump_offset;
    }

    return ptr > name ? strdup(name) : NULL;
}

/* Parse DNS questions */
static json_t* parse_dns_questions(const u_char* packet, size_t packet_len, size_t* offset, uint16_t qdcount) {
    json_t* questions = json_array();
    if (!questions) {
        return NULL;
    }

    for (int i = 0; i < qdcount && *offset < packet_len - sizeof(dns_question_t); i++) {
        /* Parse name */
        char* name = extract_dns_name(packet, packet_len, offset, packet);
        if (!name) {
            json_decref(questions);
            return NULL;
        }

        /* Parse question */
        dns_question_t* question = (dns_question_t*)(packet + *offset);
        *offset += sizeof(dns_question_t);

        json_t* q = json_object();
        if (!q) {
            free(name);
            json_decref(questions);
            return NULL;
        }

        json_object_set_new(q, "name", json_string(name));
        json_object_set_new(q, "type", json_integer(ntohs(question->qtype)));
        json_object_set_new(q, "class", json_integer(ntohs(question->qclass)));

        json_array_append_new(questions, q);
        free(name);
    }

    return questions;
}

/* Parse DNS resource records */
static json_t* parse_dns_rrs(const u_char* packet, size_t packet_len, size_t* offset, uint16_t count, const char* section) {
    json_t* rrs = json_array();
    if (!rrs) {
        return NULL;
    }

    for (int i = 0; i < count && *offset < packet_len - sizeof(dns_rr_t); i++) {
        /* Parse name */
        char* name = extract_dns_name(packet, packet_len, offset, packet);
        if (!name) {
            json_decref(rrs);
            return NULL;
        }

        /* Parse RR */
        dns_rr_t* rr = (dns_rr_t*)(packet + *offset);
        *offset += sizeof(dns_rr_t);

        json_t* record = json_object();
        if (!record) {
            free(name);
            json_decref(rrs);
            return NULL;
        }

        json_object_set_new(record, "name", json_string(name));
        json_object_set_new(record, "type", json_integer(ntohs(rr->type)));
        json_object_set_new(record, "class", json_integer(ntohs(rr->class)));
        json_object_set_new(record, "ttl", json_integer(ntohl(rr->ttl)));
        json_object_set_new(record, "rdlength", json_integer(ntohs(rr->rdlength)));

        /* Parse RDATA based on type */
        uint16_t rdlength = ntohs(rr->rdlength);
        if (*offset + rdlength <= packet_len) {
            switch (ntohs(rr->type)) {
                case 1: { /* A record */
                    if (rdlength == 4) {
                        struct in_addr addr;
                        memcpy(&addr, packet + *offset, 4);
                        char ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &addr, ip, sizeof(ip));
                        json_object_set_new(record, "address", json_string(ip));
                    }
                    break;
                }
                case 28: { /* AAAA record */
                    if (rdlength == 16) {
                        struct in6_addr addr;
                        memcpy(&addr, packet + *offset, 16);
                        char ip[INET6_ADDRSTRLEN];
                        inet_ntop(AF_INET6, &addr, ip, sizeof(ip));
                        json_object_set_new(record, "address", json_string(ip));
                    }
                    break;
                }
                case 5: { /* CNAME record */
                    size_t temp_offset = *offset;
                    char* cname = extract_dns_name(packet, packet_len, &temp_offset, packet);
                    if (cname) {
                        json_object_set_new(record, "cname", json_string(cname));
                        free(cname);
                    }
                    break;
                }
                case 2:   /* NS record */
                case 12: { /* PTR record */
                    size_t temp_offset = *offset;
                    char* target = extract_dns_name(packet, packet_len, &temp_offset, packet);
                    if (target) {
                        json_object_set_new(record, "target", json_string(target));
                        free(target);
                    }
                    break;
                }
            }
        }

        *offset += rdlength;
        json_array_append_new(rrs, record);
        free(name);
    }

    return rrs;
}

/* Parse DNS payload into JSON */
json_t* nblex_parse_dns_payload(const u_char* data, size_t data_len) {
    if (!data || data_len < sizeof(dns_header_t)) {
        return NULL;
    }

    json_t* dns = json_object();
    if (!dns) {
        return NULL;
    }

    /* Parse header */
    dns_header_t* header = (dns_header_t*)data;

    json_object_set_new(dns, "id", json_integer(ntohs(header->id)));
    json_object_set_new(dns, "qr", json_boolean((ntohs(header->flags) & 0x8000) != 0));
    json_object_set_new(dns, "opcode", json_integer((ntohs(header->flags) & 0x7800) >> 11));
    json_object_set_new(dns, "aa", json_boolean((ntohs(header->flags) & 0x0400) != 0));
    json_object_set_new(dns, "tc", json_boolean((ntohs(header->flags) & 0x0200) != 0));
    json_object_set_new(dns, "rd", json_boolean((ntohs(header->flags) & 0x0100) != 0));
    json_object_set_new(dns, "ra", json_boolean((ntohs(header->flags) & 0x0080) != 0));
    json_object_set_new(dns, "rcode", json_integer(ntohs(header->flags) & 0x000F));

    uint16_t qdcount = ntohs(header->qdcount);
    uint16_t ancount = ntohs(header->ancount);
    uint16_t nscount = ntohs(header->nscount);
    uint16_t arcount = ntohs(header->arcount);

    json_object_set_new(dns, "qdcount", json_integer(qdcount));
    json_object_set_new(dns, "ancount", json_integer(ancount));
    json_object_set_new(dns, "nscount", json_integer(nscount));
    json_object_set_new(dns, "arcount", json_integer(arcount));

    /* Parse sections */
    size_t offset = sizeof(dns_header_t);

    if (qdcount > 0) {
        json_t* questions = parse_dns_questions(data, data_len, &offset, qdcount);
        if (questions) {
            json_object_set_new(dns, "questions", questions);
        }
    }

    if (ancount > 0) {
        json_t* answers = parse_dns_rrs(data, data_len, &offset, ancount, "answer");
        if (answers) {
            json_object_set_new(dns, "answers", answers);
        }
    }

    if (nscount > 0) {
        json_t* authorities = parse_dns_rrs(data, data_len, &offset, nscount, "authority");
        if (authorities) {
            json_object_set_new(dns, "authorities", authorities);
        }
    }

    if (arcount > 0) {
        json_t* additionals = parse_dns_rrs(data, data_len, &offset, arcount, "additional");
        if (additionals) {
            json_object_set_new(dns, "additionals", additionals);
        }
    }

    return dns;
}
