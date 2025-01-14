#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>

// Maximum network constants
#define NET_MAX_INTERFACES 8
#define NET_MAX_SOCKETS 128
#define NET_MAX_PACKET_SIZE 65536
#define NET_MAX_HOSTNAME 256

// Socket types
typedef enum {
    SOCKET_TCP,
    SOCKET_UDP,
    SOCKET_RAW
} net_socket_type;

// Socket states
typedef enum {
    SOCKET_STATE_CLOSED,
    SOCKET_STATE_LISTEN,
    SOCKET_STATE_SYN_SENT,
    SOCKET_STATE_SYN_RECEIVED,
    SOCKET_STATE_ESTABLISHED,
    SOCKET_STATE_FIN_WAIT_1,
    SOCKET_STATE_FIN_WAIT_2,
    SOCKET_STATE_CLOSE_WAIT,
    SOCKET_STATE_CLOSING,
    SOCKET_STATE_LAST_ACK,
    SOCKET_STATE_TIME_WAIT
} net_socket_state;

// Network address structure
typedef struct {
    uint32_t ip;
    uint16_t port;
} net_address;

// Socket structure
typedef struct {
    int fd;
    net_socket_type type;
    net_socket_state state;
    net_address local_addr;
    net_address remote_addr;

    // Protocol-specific data
    void* protocol_data;
} net_socket;

// Network interface configuration
typedef struct {
    char name[16];
    uint8_t mac[6];
    uint32_t ip;
    uint32_t subnet;
    uint32_t gateway;
    bool is_active;
} net_interface;

// Packet structure
typedef struct {
    uint8_t* data;
    uint16_t length;
    net_address source;
    net_address destination;
} net_packet;

// Main network stack functions
int net_init(void);
void net_shutdown(void);
void net_process_packets(void);

// Socket management functions
int net_socket_create(net_socket_type type);
int net_socket_bind(int socket, uint32_t ip, uint16_t port);
int net_socket_listen(int socket, int backlog);
int net_socket_connect(int socket, uint32_t ip, uint16_t port);
int net_socket_accept(int socket, net_address* client_addr);
bool http_is_initialized(void);
net_socket* net_socket_get(int fd);
int net_socket_send(int socket, const void* data, uint16_t length);
int net_socket_receive(int socket, void* buffer, uint16_t* length);
void net_socket_close(int socket);

// Network utility functions
uint32_t net_resolve_hostname(const char* hostname);
void net_ip_to_string(uint32_t ip, char* str);
uint32_t net_string_to_ip(const char* str);

// Interface management
int net_interface_add(const net_interface* interface);
int net_interface_remove(const char* name);
net_interface* net_interface_get(const char* name);

// Packet handling
int net_send_packet(net_packet* packet);
int net_receive_packet(net_packet* packet);

#endif // NET_H