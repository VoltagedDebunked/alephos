#include <net/net.h>
#include <core/drivers/ip.h>
#include <core/drivers/pci.h>
#include <utils/mem.h>
#include <utils/str.h>

// Internal data structures
static net_socket socket_pool[NET_MAX_SOCKETS];
static net_interface interfaces[NET_MAX_INTERFACES];
static net_packet packet_queue[NET_MAX_SOCKETS];
static uint32_t packet_queue_count = 0;

// Protocol-specific data structures
typedef struct {
    uint32_t sequence_number;
    uint32_t acknowledgement_number;
    uint16_t window_size;
} tcp_connection_data;

// Static function prototypes
static net_socket* get_socket(int fd);
static int allocate_socket_fd(void);
static void free_socket_fd(int fd);
static net_interface* find_interface_for_ip(uint32_t ip);

// Helper function to format the interface name
static void fmt_interface_name(char* buffer, size_t buffer_size, const char* format, int value) {
    size_t pos = 0;

    // Iterate through format string
    for (size_t i = 0; format[i] && pos < buffer_size - 1; i++) {
        if (format[i] == '%' && format[i+1] == 'd') {
            // Convert integer to string
            int temp_value = value;
            int digit_count = 0;
            char digit_buffer[16];

            // Handle zero
            if (temp_value == 0) {
                digit_buffer[digit_count++] = '0';
            } else {
                // Convert to string (reverse order)
                while (temp_value > 0) {
                    digit_buffer[digit_count++] = (temp_value % 10) + '0';
                    temp_value /= 10;
                }
            }

            // Copy digits in correct order
            while (digit_count > 0 && pos < buffer_size - 1) {
                buffer[pos++] = digit_buffer[--digit_count];
            }

            // Skip %d
            i++;
        } else {
            // Copy non-format characters
            buffer[pos++] = format[i];
        }
    }

    // Null-terminate
    buffer[pos] = '\0';
}

// Network initialization
int net_init(void) {
    // Clear all data structures
    memset(socket_pool, 0, sizeof(socket_pool));
    memset(interfaces, 0, sizeof(interfaces));
    memset(packet_queue, 0, sizeof(packet_queue));
    packet_queue_count = 0;

    // Initialize IP layer
    if (ip_init() < 0) {
        return -1;
    }

    // Auto-configure default network interfaces
    struct pci_device* network_device = NULL;
    int interface_count = 0;

    while ((network_device = pci_scan_for_class(PCI_CLASS_NETWORK, 0)) &&
           interface_count < NET_MAX_INTERFACES) {
        net_interface* interface = &interfaces[interface_count];

        // Generate interface name
        fmt_interface_name(interface->name, sizeof(interface->name), "eth%d", interface_count);

        // Set default MAC address
        interface->mac[0] = 0x02;
        interface->mac[1] = 0x00;
        interface->mac[2] = 0x00;
        interface->mac[3] = network_device->bus;
        interface->mac[4] = network_device->slot;
        interface->mac[5] = network_device->func;

        // Default IP configuration
        interface->ip = ip_string_to_int("192.168.1.100") + interface_count;
        interface->subnet = ip_string_to_int("255.255.255.0");
        interface->gateway = ip_string_to_int("192.168.1.1");
        interface->is_active = true;

        interface_count++;
    }

    return interface_count;
}

// Shutdown network stack
void net_shutdown(void) {
    // Close all open sockets
    for (int i = 0; i < NET_MAX_SOCKETS; i++) {
        if (socket_pool[i].state != SOCKET_STATE_CLOSED) {
            net_socket_close(i);
        }
    }
}

// Socket creation
int net_socket_create(net_socket_type type) {
    int fd = allocate_socket_fd();
    if (fd < 0) return -1;

    net_socket* sock = &socket_pool[fd];
    sock->fd = fd;
    sock->type = type;
    sock->state = SOCKET_STATE_CLOSED;

    // Allocate protocol-specific data
    if (type == SOCKET_TCP) {
        sock->protocol_data = malloc(sizeof(tcp_connection_data));
        if (!sock->protocol_data) {
            free_socket_fd(fd);
            return -1;
        }
        memset(sock->protocol_data, 0, sizeof(tcp_connection_data));
    }

    return fd;
}

// Bind socket to local address
int net_socket_bind(int socket, uint32_t ip, uint16_t port) {
    net_socket* sock = get_socket(socket);
    if (!sock) return -1;

    sock->local_addr.ip = ip;
    sock->local_addr.port = port;

    return 0;
}

// Listen for incoming connections
int net_socket_listen(int socket, int backlog) {
    net_socket* sock = get_socket(socket);
    if (!sock || sock->type != SOCKET_TCP) return -1;

    sock->state = SOCKET_STATE_LISTEN;
    return 0;
}

// Connect to remote address
int net_socket_connect(int socket, uint32_t ip, uint16_t port) {
    net_socket* sock = get_socket(socket);
    if (!sock || sock->type != SOCKET_TCP) return -1;

    sock->remote_addr.ip = ip;
    sock->remote_addr.port = port;
    sock->state = SOCKET_STATE_SYN_SENT;

    // Prepare TCP SYN packet
    struct {
        uint16_t source_port;
        uint16_t dest_port;
        uint32_t sequence;
        uint32_t ack;
        uint8_t flags;
    } __attribute__((packed)) syn_packet;

    syn_packet.source_port = sock->local_addr.port;
    syn_packet.dest_port = port;
    syn_packet.sequence = 12345;  // Random initial sequence
    syn_packet.flags = 0x02;  // SYN flag

    // Send via IP layer
    return ip_send_packet(ip, IP_PROTOCOL_TCP, &syn_packet, sizeof(syn_packet));
}

// Accept incoming connection
int net_socket_accept(int socket, net_address* client_addr) {
    net_socket* listen_sock = get_socket(socket);
    if (!listen_sock || listen_sock->state != SOCKET_STATE_LISTEN) return -1;

    // Find incoming connection in packet queue
    for (uint32_t i = 0; i < packet_queue_count; i++) {
        net_packet* packet = &packet_queue[i];
        // Check for TCP SYN packet
        if (packet->destination.port == listen_sock->local_addr.port) {
            // Create new socket for this connection
            int new_socket = net_socket_create(SOCKET_TCP);
            if (new_socket < 0) return -1;

            net_socket* new_sock = get_socket(new_socket);
            new_sock->local_addr = listen_sock->local_addr;
            new_sock->remote_addr = packet->source;
            new_sock->state = SOCKET_STATE_SYN_RECEIVED;

            // Copy client address if requested
            if (client_addr) {
                *client_addr = packet->source;
            }

            // Remove packet from queue
            memmove(&packet_queue[i], &packet_queue[i+1],
                    (packet_queue_count - i - 1) * sizeof(net_packet));
            packet_queue_count--;

            return new_socket;
        }
    }

    return -1;
}

// Send data
int net_socket_send(int socket, const void* data, uint16_t length) {
    net_socket* sock = get_socket(socket);
    if (!sock || sock->state != SOCKET_STATE_ESTABLISHED) return -1;

    // Send via IP layer based on socket type
    switch (sock->type) {
        case SOCKET_TCP:
            return ip_send_packet(sock->remote_addr.ip, IP_PROTOCOL_TCP, data, length);
        case SOCKET_UDP:
            return ip_send_packet(sock->remote_addr.ip, IP_PROTOCOL_UDP, data, length);
        case SOCKET_RAW:
            return ip_send_packet(sock->remote_addr.ip, IP_PROTOCOL_RAW, data, length);
        default:
            return -1;
    }
}

// Receive data
int net_socket_receive(int socket, void* buffer, uint16_t* length) {
    net_socket* sock = get_socket(socket);
    if (!sock || !buffer || !length) return -1;

    // Search packet queue for matching socket
    for (uint32_t i = 0; i < packet_queue_count; i++) {
        net_packet* packet = &packet_queue[i];

        // Match destination port and IP
        if (packet->destination.port == sock->local_addr.port &&
            packet->destination.ip == sock->local_addr.ip) {

            // Copy data
            uint16_t copy_length = (*length < packet->length) ? *length : packet->length;
            memcpy(buffer, packet->data, copy_length);
            *length = copy_length;

            // Remove packet from queue
            free(packet->data);
            memmove(&packet_queue[i], &packet_queue[i+1],
                    (packet_queue_count - i - 1) * sizeof(net_packet));
            packet_queue_count--;

            return 0;
        }
    }

    return -1;
}

// Close socket
void net_socket_close(int socket) {
    net_socket* sock = get_socket(socket);
    if (!sock) return;

    // Send FIN if TCP
    if (sock->type == SOCKET_TCP && sock->state == SOCKET_STATE_ESTABLISHED) {
        struct {
            uint16_t source_port;
            uint16_t dest_port;
            uint8_t flags;
        } __attribute__((packed)) fin_packet;

        fin_packet.source_port = sock->local_addr.port;
        fin_packet.dest_port = sock->remote_addr.port;
        fin_packet.flags = 0x11;  // FIN-ACK

        ip_send_packet(sock->remote_addr.ip, IP_PROTOCOL_TCP, &fin_packet, sizeof(fin_packet));
    }

    // Free protocol-specific data
    if (sock->protocol_data) {
        free(sock->protocol_data);
        sock->protocol_data = NULL;
    }

    // Reset socket state
    memset(sock, 0, sizeof(net_socket));
}

// Packet processing
void net_process_packets(void) {
    net_packet packet;
    while (net_receive_packet(&packet) == 0) {
        // Add to packet queue
        if (packet_queue_count < NET_MAX_SOCKETS) {
            packet_queue[packet_queue_count++] = packet;
        } else {
            // Queue full, drop packet
            free(packet.data);
        }
    }
}

// Helper functions
static net_socket* get_socket(int fd) {
    if (fd < 0 || fd >= NET_MAX_SOCKETS) return NULL;
    return &socket_pool[fd];
}

static int allocate_socket_fd(void) {
    for (int i = 0; i < NET_MAX_SOCKETS; i++) {
        if (socket_pool[i].state == SOCKET_STATE_CLOSED) {
            return i;
        }
    }
    return -1;
}

static void free_socket_fd(int fd) {
    if (fd >= 0 && fd < NET_MAX_SOCKETS) {
        memset(&socket_pool[fd], 0, sizeof(net_socket));
    }
}

// Interface management
int net_interface_add(const net_interface* interface) {
    for (int i = 0; i < NET_MAX_INTERFACES; i++) {
        if (!interfaces[i].is_active) {
            memcpy(&interfaces[i], interface, sizeof(net_interface));
            interfaces[i].is_active = true;
            return 0;
        }
    }
    return -1;
}

int net_interface_remove(const char* name) {
    for (int i = 0; i < NET_MAX_INTERFACES; i++) {
        if (interfaces[i].is_active &&
            strcmp(interfaces[i].name, name) == 0) {
            interfaces[i].is_active = false;
            return 0;
        }
    }
    return -1;
}

net_interface* net_interface_get(const char* name) {
    for (int i = 0; i < NET_MAX_INTERFACES; i++) {
        if (interfaces[i].is_active &&
            strcmp(interfaces[i].name, name) == 0) {
            return &interfaces[i];
        }
    }
    return NULL;
}

// Packet handling
int net_send_packet(net_packet* packet) {
    // Find appropriate interface
    net_interface* interface = find_interface_for_ip(packet->source.ip);
    if (!interface) return -1;

    // Send via IP layer
    return ip_send_packet(packet->destination.ip, IP_PROTOCOL_TCP,
                           packet->data, packet->length);
}

int net_receive_packet(net_packet* packet) {
    // Use IP layer to receive packet
    uint8_t buffer[NET_MAX_PACKET_SIZE];
    uint16_t length = sizeof(buffer);

    int result = ip_receive_packet(buffer, length);
    if (result < 0) return -1;

    // Allocate packet data
    packet->data = malloc(length);
    if (!packet->data) return -1;

    memcpy(packet->data, buffer, length);
    packet->length = length;

    // Populate source and destination
    // This would typically be extracted from the IP packet header
    packet->source.ip = 0;  // Placeholder
    packet->source.port = 0;
    packet->destination.ip = 0;  // Placeholder
    packet->destination.port = 0;

    return 0;
}

// Network utility functions
uint32_t net_resolve_hostname(const char* hostname) {
    // Basic implementation - just convert string IP to integer
    return net_string_to_ip(hostname);
}

void net_ip_to_string(uint32_t ip, char* str) {
    // Use IP driver's conversion
    ip_int_to_string(ip, str);
}

uint32_t net_string_to_ip(const char* str) {
    // Use IP driver's conversion
    return ip_string_to_int(str);
}

// Helper to find interface for a given IP
static net_interface* find_interface_for_ip(uint32_t ip) {
    for (int i = 0; i < NET_MAX_INTERFACES; i++) {
        if (interfaces[i].is_active) {
            // Check if IP is in the same subnet
            if ((ip & interfaces[i].subnet) ==
                (interfaces[i].ip & interfaces[i].subnet)) {
                return &interfaces[i];
            }
        }
    }
    return NULL;
}