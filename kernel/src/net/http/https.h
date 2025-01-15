#ifndef HTTPS_H
#define HTTPS_H

#include <stdint.h>
#include <stdbool.h>
#include <net/http/http.h>
#include <stddef.h>

typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v,l) __builtin_va_arg(v,l)

// TLS Record Types
#define TLS_RECORD_CHANGE_CIPHER_SPEC 20
#define TLS_RECORD_ALERT              21
#define TLS_RECORD_HANDSHAKE          22
#define TLS_RECORD_APPLICATION_DATA   23

// TLS Handshake Types
#define TLS_CLIENT_HELLO           1
#define TLS_SERVER_HELLO           2
#define TLS_CERTIFICATE            11
#define TLS_SERVER_HELLO_DONE      14
#define TLS_CLIENT_KEY_EXCHANGE    16
#define TLS_FINISHED               20

// Basic TLS context
typedef struct {
    http_client_t* http;
    net_socket* socket;

    // Handshake state
    uint8_t* client_random;
    uint8_t* server_random;
    uint16_t cipher_suite;

    // Encryption
    uint8_t* session_key;
    uint8_t* master_secret;

    // Connection state
    bool is_client;
    bool handshake_complete;
    bool connected;
} tls_context_t;

// TLS Record Header (standard format)
typedef struct {
    uint8_t  type;
    uint16_t version;
    uint16_t length;
} __attribute__((packed)) tls_record_header_t;

// TLS Handshake Header
typedef struct {
    uint8_t  type;
    uint8_t  length[3];  // 24-bit length
} __attribute__((packed)) tls_handshake_header_t;

// TLS Connection Configuration
typedef struct {
    const char* hostname;
    uint16_t port;
    bool verify_peer;
    const char* ca_cert_path;
} tls_config_t;

// TLS Initialization and Cleanup
bool tls_init(void);
void tls_cleanup(void);

// TLS Context Management
tls_context_t* tls_create_context(void);
void tls_destroy_context(tls_context_t* context);
void tls_reset_context(tls_context_t* context);

// TLS Handshake Functions
bool tls_handshake(tls_context_t* context, const tls_config_t* config);
bool tls_client_hello(tls_context_t* context);
bool tls_parse_server_hello(tls_context_t* context, const uint8_t* data, size_t length);
bool tls_generate_premaster_secret(tls_context_t* context);

// TLS Record Layer Operations
int tls_send_record(tls_context_t* context, uint8_t type, const void* data, size_t length);
int tls_receive_record(tls_context_t* context, uint8_t* type, void* buffer, size_t* length);

// Cryptographic Operations
bool tls_generate_random(uint8_t* random_bytes, size_t length);
bool tls_derive_master_secret(tls_context_t* context);
bool tls_generate_session_keys(tls_context_t* context);

// Certificate Handling
bool tls_validate_certificate(void* certificate, size_t cert_length);

// Public HTTPS Client Functions
bool https_init(void);
bool https_is_initialized(void);
void https_shutdown(void);
http_client_t* https_client_create(void);
void https_client_destroy(http_client_t* client);
int https_client_connect(http_client_t* client, const char* host, uint16_t port);
http_response_t* https_send_request(http_client_t* client, http_request_t* request);

#endif // HTTPS_H