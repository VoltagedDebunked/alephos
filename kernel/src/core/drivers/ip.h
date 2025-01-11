#ifndef IP_DRIVER_H
#define IP_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

// IP packet structure
struct ip_packet {
    uint8_t version_ihl;     // Version (4 bits) and Internet Header Length (4 bits)
    uint8_t dscp_ecn;        // Differentiated Services Code Point and ECN
    uint16_t total_length;   // Total length of the packet
    uint16_t identification; // Identification for fragmentation
    uint16_t flags_fragment; // Flags and Fragment Offset
    uint8_t time_to_live;    // Time to Live
    uint8_t protocol;        // Protocol (TCP, UDP, etc.)
    uint16_t header_checksum;// Header checksum
    uint32_t source_ip;      // Source IP address
    uint32_t destination_ip; // Destination IP address
    uint8_t payload[];       // Payload data
} __attribute__((packed));

// IP protocol constants
#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP  6
#define IP_PROTOCOL_UDP  17
#define IP_PROTOCOL_RAW  255

// IP address manipulation functions
uint32_t ip_string_to_int(const char* ip_str);
void ip_int_to_string(uint32_t ip, char* ip_str);

// IP packet processing functions
int ip_init(void);
int ip_send_packet(uint32_t destination_ip, uint8_t protocol,
                   const void* data, uint16_t data_length);
int ip_receive_packet(const void* packet, uint16_t packet_length);

// IP fragmentation and reassembly
int ip_fragment_packet(const void* packet, uint16_t packet_length);
int ip_reassemble_packet(const void* fragment, uint16_t fragment_length);

// Checksum calculation
uint16_t ip_calculate_checksum(const void* data, uint16_t length);

#endif // IP_DRIVER_H