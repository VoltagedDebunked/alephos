#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>
#include <stdbool.h>
#include <net/net.h>

// HTTP Methods
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_HEAD,
    HTTP_OPTIONS,
    HTTP_PATCH
} http_method_t;

// HTTP Status Codes
typedef enum {
    HTTP_OK = 200,
    HTTP_CREATED = 201,
    HTTP_ACCEPTED = 202,
    HTTP_NO_CONTENT = 204,
    HTTP_BAD_REQUEST = 400,
    HTTP_UNAUTHORIZED = 401,
    HTTP_FORBIDDEN = 403,
    HTTP_NOT_FOUND = 404,
    HTTP_SERVER_ERROR = 500
} http_status_t;

// HTTP Header structure
typedef struct {
    char* name;
    char* value;
} http_header_t;

// HTTP Request structure
typedef struct {
    http_method_t method;
    char* url;
    char* version;
    http_header_t* headers;
    uint32_t header_count;
    uint8_t* body;
    uint32_t body_length;
} http_request_t;

// HTTP Response structure
typedef struct {
    http_status_t status;
    char* status_text;
    char* version;
    http_header_t* headers;
    uint32_t header_count;
    uint8_t* body;
    uint32_t body_length;
} http_response_t;

// HTTP Client context
typedef struct {
    net_socket* socket;
    char* host;
    uint16_t port;
    bool connected;
} http_client_t;

// Function declarations
bool http_init(void);
void http_shutdown(void);

http_client_t* http_client_create(void);
void http_client_destroy(http_client_t* client);
int http_client_connect(http_client_t* client, const char* host, uint16_t port);
void http_client_disconnect(http_client_t* client);

http_request_t* http_request_create(http_method_t method, const char* url);
void http_request_destroy(http_request_t* request);
void http_request_add_header(http_request_t* request, const char* name, const char* value);

http_response_t* http_send_request(http_client_t* client, http_request_t* request);
void http_response_destroy(http_response_t* response);

// Helper functions
const char* http_status_text(http_status_t status);
const char* http_method_string(http_method_t method);

#endif // HTTP_H