#ifndef NET_DNS_H
#define NET_DNS_H

#include <stdint.h>
#include <stddef.h>

// DNS header structure
struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

// DNS question structure
struct dns_question {
    uint16_t qtype;
    uint16_t qclass;
} __attribute__((packed));

// DNS resource record structure
struct dns_resource_record {
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t rdata[];
} __attribute__((packed));

// DNS constants
#define DNS_PORT 53
#define DNS_MAX_NAME_LENGTH 255
#define DNS_TIMEOUT_MS 5000
#define DNS_MAX_RETRIES 3

// DNS Query Types
#define DNS_TYPE_A     1   // IPv4 address
#define DNS_TYPE_NS    2   // Nameserver
#define DNS_TYPE_CNAME 5   // Canonical name
#define DNS_TYPE_SOA   6   // Start of Authority
#define DNS_TYPE_MX    15  // Mail exchange
#define DNS_TYPE_AAAA  28  // IPv6 address

// DNS Flags
#define DNS_FLAG_QR    0x8000  // Query/Response flag
#define DNS_FLAG_AA    0x0400  // Authoritative Answer
#define DNS_FLAG_TC    0x0200  // Truncation flag
#define DNS_FLAG_RD    0x0100  // Recursion Desired
#define DNS_FLAG_RA    0x0080  // Recursion Available
#define DNS_FLAG_RCODE 0x000F  // Response code mask

// DNS Response Codes
#define DNS_RCODE_NOERROR  0   // No error
#define DNS_RCODE_FORMERR  1   // Format error
#define DNS_RCODE_SERVFAIL 2   // Server failure
#define DNS_RCODE_NXDOMAIN 3   // Non-existent domain
#define DNS_RCODE_NOTIMP   4   // Not implemented
#define DNS_RCODE_REFUSED  5   // Query refused

// Public DNS functions
void dns_init(void);
uint32_t net_resolve_dns(const char* hostname);

// DNS configuration functions
void dns_set_server(uint32_t server_ip);
void dns_reset_servers(void);
uint32_t dns_get_primary_server(void);

// DNS statistics and diagnostics
typedef struct {
    uint32_t queries_sent;
    uint32_t responses_received;
    uint32_t timeouts;
    uint32_t errors;
    uint32_t cache_hits;
    uint32_t cache_misses;
} dns_stats_t;

void dns_get_stats(dns_stats_t* stats);
void dns_reset_stats(void);

#endif // NET_DNS_H