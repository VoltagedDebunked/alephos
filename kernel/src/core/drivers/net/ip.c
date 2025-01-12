#include <core/drivers/net/ip.h>
#include <core/drivers/pci.h>
#include <utils/mem.h>
#include <utils/io.h>

// Configuration Constants
#define MAX_IP_INTERFACES 8
#define MAX_IP_ROUTES 32
#define MAX_IP_CONNECTIONS 128
#define PACKET_BUFFER_SIZE 8192

// IP Connection States
typedef enum {
    IP_CONN_CLOSED,
    IP_CONN_LISTEN,
    IP_CONN_SYN_SENT,
    IP_CONN_SYN_RECEIVED,
    IP_CONN_ESTABLISHED,
    IP_CONN_FIN_WAIT_1,
    IP_CONN_FIN_WAIT_2,
    IP_CONN_CLOSE_WAIT,
    IP_CONN_CLOSING,
    IP_CONN_LAST_ACK
} ip_connection_state;

// Network Interface Structure
typedef struct {
    uint32_t ip_address;
    uint32_t subnet_mask;
    uint32_t gateway;
    struct pci_device* network_device;
    bool is_active;
} ip_interface;

// Routing Entry
typedef struct {
    uint32_t network;
    uint32_t netmask;
    uint32_t gateway;
    ip_interface* interface;
} ip_route;

// IP Connection Tracking
typedef struct {
    uint32_t local_ip;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    ip_connection_state state;
    uint8_t protocol;
    uint32_t sequence_number;
    uint32_t acknowledgement_number;
} ip_connection;

// ICMP Header
struct icmp_header {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    union {
        struct {
            uint16_t identifier;
            uint16_t sequence_number;
        } echo;
        uint32_t gateway;
    } data;
    uint8_t payload[];
} __attribute__((packed));

// TCP Header
struct tcp_header {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence_number;
    uint32_t acknowledgement_number;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;
    uint8_t payload[];
} __attribute__((packed));

// UDP Header
struct udp_header {
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t length;
    uint16_t checksum;
    uint8_t payload[];
} __attribute__((packed));

// Global Data Structures
static ip_interface ip_interfaces[MAX_IP_INTERFACES];
static ip_route ip_routing_table[MAX_IP_ROUTES];
static ip_connection ip_connections[MAX_IP_CONNECTIONS];
static uint32_t packet_sequence = 0;

// Static Function Prototypes
static ip_interface* find_interface_for_destination(uint32_t destination_ip);
static ip_route* find_route(uint32_t destination_ip);
static ip_connection* allocate_connection(void);
static ip_connection* find_connection(uint32_t local_ip, uint32_t remote_ip,
                                      uint16_t local_port, uint16_t remote_port);

// IP Address Conversion
uint32_t ip_string_to_int(const char* ip_str) {
    uint32_t ip = 0;
    uint8_t octet = 0;
    uint8_t shift = 24;

    while (*ip_str) {
        if (*ip_str >= '0' && *ip_str <= '9') {
            octet = octet * 10 + (*ip_str - '0');
        } else if (*ip_str == '.') {
            ip |= ((uint32_t)octet << shift);
            shift -= 8;
            octet = 0;
        }
        ip_str++;
    }

    ip |= octet;
    return ip;
}

// Checksum Calculation
uint16_t ip_calculate_checksum(const void* data, uint16_t length) {
    uint32_t sum = 0;
    const uint16_t* words = (const uint16_t*)data;

    for (uint16_t i = 0; i < length / 2; i++) {
        sum += words[i];
    }

    if (length & 1) {
        const uint8_t* last_byte = (const uint8_t*)data + length - 1;
        sum += *last_byte << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}

// IP Initialization
int ip_init(void) {
    memset(ip_interfaces, 0, sizeof(ip_interfaces));
    memset(ip_routing_table, 0, sizeof(ip_routing_table));
    memset(ip_connections, 0, sizeof(ip_connections));

    // Automatically detect network devices
    struct pci_device* network_device = NULL;
    int interface_count = 0;

    while ((network_device = pci_scan_for_class(PCI_CLASS_NETWORK, 0)) &&
           interface_count < MAX_IP_INTERFACES) {
        ip_interface* interface = &ip_interfaces[interface_count];

        // Default private network configuration
        interface->ip_address = ip_string_to_int("192.168.1.100") + interface_count;
        interface->subnet_mask = ip_string_to_int("255.255.255.0");
        interface->gateway = ip_string_to_int("192.168.1.1");
        interface->network_device = network_device;
        interface->is_active = true;

        // Create default route
        ip_route* route = &ip_routing_table[interface_count];
        route->network = interface->ip_address & interface->subnet_mask;
        route->netmask = interface->subnet_mask;
        route->gateway = interface->gateway;
        route->interface = interface;

        interface_count++;
    }

    return interface_count;
}

// Find Interface for Destination IP
static ip_interface* find_interface_for_destination(uint32_t destination_ip) {
    ip_route* route = find_route(destination_ip);
    return route ? route->interface : NULL;
}

// Find Route for IP
static ip_route* find_route(uint32_t destination_ip) {
    for (int i = 0; i < MAX_IP_ROUTES; i++) {
        ip_route* route = &ip_routing_table[i];
        if (route->interface &&
            (destination_ip & route->netmask) == route->network) {
            return route;
        }
    }
    return NULL;
}

// Allocate Connection
static ip_connection* allocate_connection(void) {
    for (int i = 0; i < MAX_IP_CONNECTIONS; i++) {
        if (ip_connections[i].state == IP_CONN_CLOSED) {
            ip_connections[i].state = IP_CONN_LISTEN;
            return &ip_connections[i];
        }
    }
    return NULL;
}

// Find Existing Connection
static ip_connection* find_connection(uint32_t local_ip, uint32_t remote_ip,
                                      uint16_t local_port, uint16_t remote_port) {
    for (int i = 0; i < MAX_IP_CONNECTIONS; i++) {
        ip_connection* conn = &ip_connections[i];
        if (conn->state != IP_CONN_CLOSED &&
            conn->local_ip == local_ip &&
            conn->remote_ip == remote_ip &&
            conn->local_port == local_port &&
            conn->remote_port == remote_port) {
            return conn;
        }
    }
    return NULL;
}

// Send IP Packet
int ip_send_packet(uint32_t destination_ip, uint8_t protocol, const void* data, uint16_t data_length) {
    ip_interface* source_interface = find_interface_for_destination(destination_ip);
    if (!source_interface) return -1;

    // Allocate packet buffer
    uint16_t total_length = sizeof(struct ip_packet) + data_length;
    struct ip_packet* packet = malloc(total_length);
    if (!packet) return -1;

    // Populate IP Header
    packet->version_ihl = 0x45;  // IPv4, 20-byte header
    packet->dscp_ecn = 0;
    packet->total_length = total_length;
    packet->identification = packet_sequence++;
    packet->flags_fragment = 0;
    packet->time_to_live = 64;
    packet->protocol = protocol;
    packet->source_ip = source_interface->ip_address;
    packet->destination_ip = destination_ip;

    // Copy payload
    memcpy(packet->payload, data, data_length);

    // Calculate checksum
    packet->header_checksum = 0;
    packet->header_checksum = ip_calculate_checksum(packet, sizeof(struct ip_packet));

    // Simulate packet transmission (replace with actual device transmission)
    int result = 0;

    free(packet);
    return result;
}

// Receive IP Packet
int ip_receive_packet(const void* packet_data, uint16_t packet_length) {
    const struct ip_packet* packet = (const struct ip_packet*)packet_data;

    // Validate IP packet
    if ((packet->version_ihl & 0xF0) != 0x40 ||
        ((packet->version_ihl & 0x0F) * 4) != 20) {
        return -1;
    }

    // Verify checksum
    uint16_t original_checksum = packet->header_checksum;
    struct ip_packet* mutable_packet = (struct ip_packet*)packet_data;
    mutable_packet->header_checksum = 0;
    uint16_t calculated_checksum = ip_calculate_checksum(packet, sizeof(struct ip_packet));

    if (original_checksum != calculated_checksum) {
        return -1;
    }

    // Find destination interface
    ip_interface* dest_interface = find_interface_for_destination(packet->destination_ip);
    if (!dest_interface) {
        return -1;  // Packet not for this host
    }

    // Extract payload
    uint16_t payload_length = packet->total_length - sizeof(struct ip_packet);
    const uint8_t* payload = packet->payload;

    // Protocol-specific handling
    switch (packet->protocol) {
        case IP_PROTOCOL_ICMP: {
            struct icmp_header* icmp = (struct icmp_header*)payload;

            // Echo Request handling
            if (icmp->type == 8) {  // Echo Request
                struct icmp_header* reply = malloc(payload_length);
                if (!reply) return -1;

                // Copy original payload
                memcpy(reply, icmp, payload_length);

                // Modify for echo reply
                reply->type = 0;  // Echo Reply
                reply->checksum = 0;
                reply->checksum = ip_calculate_checksum(reply, payload_length);

                // Send reply
                ip_send_packet(packet->source_ip, IP_PROTOCOL_ICMP,
                               reply, payload_length);

                free(reply);
            }
            break;
        }

        case IP_PROTOCOL_TCP: {
            struct tcp_header* tcp = (struct tcp_header*)payload;

            // Find or create connection
            ip_connection* conn = find_connection(
                packet->destination_ip, packet->source_ip,
                tcp->destination_port, tcp->source_port
            );

            if (!conn) {
                conn = allocate_connection();
                if (!conn) return -1;
            }

            // Connection state management
            switch (tcp->flags & 0x17) {  // SYN, FIN, ACK flags
                case 0x02: {  // SYN: New connection request
                    conn->local_ip = packet->destination_ip;
                    conn->remote_ip = packet->source_ip;
                    conn->local_port = tcp->destination_port;
                    conn->remote_port = tcp->source_port;
                    conn->state = IP_CONN_SYN_RECEIVED;
                    conn->sequence_number = packet_sequence++;

                    // Prepare SYN-ACK response
                    struct tcp_header* syn_ack = malloc(sizeof(struct tcp_header));
                    if (!syn_ack) return -1;

                    syn_ack->source_port = tcp->destination_port;
                    syn_ack->destination_port = tcp->source_port;
                    syn_ack->sequence_number = conn->sequence_number;
                    syn_ack->acknowledgement_number = tcp->sequence_number + 1;
                    syn_ack->flags = 0x12;  // SYN-ACK
                    syn_ack->window_size = 65535;

                    ip_send_packet(packet->source_ip, IP_PROTOCOL_TCP,
                                   syn_ack, sizeof(struct tcp_header));

                    free(syn_ack);
                    break;
                }

                case 0x10: {  // ACK: Connection established
                    conn->state = IP_CONN_ESTABLISHED;
                    break;
                }

                case 0x11: {  // FIN-ACK: Connection closing
                    conn->state = IP_CONN_CLOSE_WAIT;
                    break;
                }
            }
            break;
        }

        case IP_PROTOCOL_UDP: {
            struct udp_header* udp = (struct udp_header*)payload;

            // Basic UDP port handling
            switch (udp->destination_port) {
                case 53:  // DNS
                case 67:  // DHCP Server
                case 68:  // DHCP Client
                    // Basic service port recognition
                    break;

                default:
                    // Unhandled UDP ports
                    break;
            }
            break;
        }

        default:
            return -1;  // Unsupported protocol
    }

    return 0;
}