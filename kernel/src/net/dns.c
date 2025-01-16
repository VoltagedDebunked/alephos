#include <net/dns.h>
#include <net/net.h>
#include <utils/mem.h>
#include <utils/str.h>

// Default DNS servers (Google DNS and Cloudflare DNS)
static const uint32_t default_dns_servers[] = {
    0x08080808,  // 8.8.8.8 (Google)
    0x08080404,  // 8.8.4.4 (Google)
    0x01010101,  // 1.1.1.1 (Cloudflare)
    0x01000001   // 1.0.0.1 (Cloudflare)
};

#define MAX_DNS_SERVERS 4
static uint32_t dns_servers[MAX_DNS_SERVERS];
static size_t num_dns_servers = 0;
static uint16_t dns_query_id = 0;

// Statistics tracking
static dns_stats_t dns_statistics = {0};

// Convert domain name to DNS format
static int dns_encode_name(const char* domain, uint8_t* buffer) {
    int written = 0;
    const char* label = domain;
    const char* dot;

    while (label && *label) {
        // Find next dot or end of string
        dot = strchr(label, '.');
        int len = dot ? (dot - label) : strlen(label);

        // Check length limits
        if (len > 63 || written + len + 1 > DNS_MAX_NAME_LENGTH) {
            return -1;
        }

        // Write length byte
        buffer[written++] = len;

        // Copy label
        memcpy(buffer + written, label, len);
        written += len;

        // Move to next label
        if (dot) {
            label = dot + 1;
        } else {
            break;
        }
    }

    // Add terminating zero
    buffer[written++] = 0;

    return written;
}

static inline uint16_t htons(uint16_t x) {
    return ((x & 0xFF) << 8) | ((x & 0xFF00) >> 8);
}

static inline uint16_t ntohs(uint16_t x) {
    return htons(x);  // Same operation for both conversions
}

// Build DNS query packet
static int dns_build_query(const char* domain, uint8_t* buffer, size_t buffer_size) {
    if (!domain || !buffer || buffer_size < sizeof(struct dns_header)) {
        return -1;
    }

    // Initialize header
    struct dns_header* header = (struct dns_header*)buffer;
    header->id = ++dns_query_id;
    header->flags = htons(DNS_FLAG_RD);  // Recursion desired
    header->qdcount = htons(1);  // One question
    header->ancount = 0;
    header->nscount = 0;
    header->arcount = 0;

    // Encode domain name
    int name_length = dns_encode_name(domain, buffer + sizeof(struct dns_header));
    if (name_length < 0) {
        return -1;
    }

    // Add question section
    struct dns_question* question = (struct dns_question*)(buffer + sizeof(struct dns_header) + name_length);
    question->qtype = htons(DNS_TYPE_A);    // Request A record (IPv4)
    question->qclass = htons(1);            // IN class

    return sizeof(struct dns_header) + name_length + sizeof(struct dns_question);
}

// Parse DNS response and extract IP address
static uint32_t dns_parse_response(const uint8_t* response, size_t response_length) {
    if (!response || response_length < sizeof(struct dns_header)) {
        dns_statistics.errors++;
        return 0;
    }

    struct dns_header* header = (struct dns_header*)response;

    // Check response flags
    uint16_t flags = ntohs(header->flags);
    if (!(flags & DNS_FLAG_QR)) {  // Must be a response
        dns_statistics.errors++;
        return 0;
    }

    uint8_t rcode = flags & DNS_FLAG_RCODE;
    if (rcode != DNS_RCODE_NOERROR) {
        dns_statistics.errors++;
        return 0;
    }

    // Skip header
    const uint8_t* cur = response + sizeof(struct dns_header);
    const uint8_t* end = response + response_length;

    // Skip question section
    uint16_t qdcount = ntohs(header->qdcount);
    while (qdcount-- > 0) {
        // Skip name
        while (cur < end && *cur) {
            if (*cur & 0xC0) {  // Compression pointer
                cur += 2;
                break;
            }
            cur += *cur + 1;
        }
        if (cur >= end || cur + sizeof(struct dns_question) > end) {
            dns_statistics.errors++;
            return 0;
        }
        cur += sizeof(struct dns_question);
    }

    // Parse answer section
    uint16_t ancount = ntohs(header->ancount);
    while (ancount-- > 0) {
        // Skip name
        while (cur < end && *cur) {
            if (*cur & 0xC0) {  // Compression pointer
                cur += 2;
                break;
            }
            cur += *cur + 1;
        }
        if (cur >= end) {
            dns_statistics.errors++;
            return 0;
        }

        // Get resource record
        struct dns_resource_record* rr = (struct dns_resource_record*)cur;
        if (cur + sizeof(struct dns_resource_record) > end) {
            dns_statistics.errors++;
            return 0;
        }
        cur += sizeof(struct dns_resource_record);

        // Check if this is an A record
        if (ntohs(rr->type) == DNS_TYPE_A && ntohs(rr->rdlength) == 4) {
            dns_statistics.responses_received++;
            // Return first IPv4 address found
            return *(uint32_t*)cur;
        }

        // Skip record data
        cur += ntohs(rr->rdlength);
    }

    dns_statistics.errors++;
    return 0;
}

// Initialize DNS resolver
void dns_init(void) {
    dns_query_id = 0;
    num_dns_servers = 0;

    // Add default DNS servers
    for (size_t i = 0; i < sizeof(default_dns_servers) / sizeof(default_dns_servers[0]); i++) {
        dns_servers[num_dns_servers++] = default_dns_servers[i];
    }

    // Reset statistics
    dns_reset_stats();
}

// Main DNS resolution function
uint32_t net_resolve_dns(const char* hostname) {
    // Check if hostname is already an IP address
    uint32_t ip = net_string_to_ip(hostname);
    if (ip != 0) {
        return ip;
    }

    // Create UDP socket for DNS queries
    int sock = net_socket_create(SOCKET_UDP);
    if (sock < 0) {
        dns_statistics.errors++;
        return 0;
    }

    // Prepare query buffer
    uint8_t query_buffer[512];
    int query_length = dns_build_query(hostname, query_buffer, sizeof(query_buffer));
    if (query_length < 0) {
        net_socket_close(sock);
        dns_statistics.errors++;
        return 0;
    }

    dns_statistics.queries_sent++;

    // Try each DNS server
    uint8_t response_buffer[512];
    for (size_t i = 0; i < num_dns_servers; i++) {
        // Connect to DNS server
        if (net_socket_connect(sock, dns_servers[i], DNS_PORT) < 0) {
            continue;
        }

        // Send query
        if (net_socket_send(sock, query_buffer, query_length) < 0) {
            continue;
        }

        // Receive response
        uint16_t response_length = sizeof(response_buffer);
        if (net_socket_receive(sock, response_buffer, &response_length) < 0) {
            dns_statistics.timeouts++;
            continue;
        }

        // Parse response
        ip = dns_parse_response(response_buffer, response_length);
        if (ip != 0) {
            break;
        }
    }

    net_socket_close(sock);
    return ip;
}

// DNS configuration functions
void dns_set_server(uint32_t server_ip) {
    if (num_dns_servers < MAX_DNS_SERVERS) {
        dns_servers[num_dns_servers++] = server_ip;
    }
}

void dns_reset_servers(void) {
    num_dns_servers = 0;
    // Restore default servers
    for (size_t i = 0; i < sizeof(default_dns_servers) / sizeof(default_dns_servers[0]); i++) {
        dns_servers[num_dns_servers++] = default_dns_servers[i];
    }
}

uint32_t dns_get_primary_server(void) {
    return num_dns_servers > 0 ? dns_servers[0] : 0;
}

// Statistics functions
void dns_get_stats(dns_stats_t* stats) {
    if (stats) {
        *stats = dns_statistics;
    }
}

void dns_reset_stats(void) {
    memset(&dns_statistics, 0, sizeof(dns_statistics));
}