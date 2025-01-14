#include <net/http/http.h>
#include <utils/mem.h>
#include <utils/str.h>
#include <mm/heap.h>

#define HTTP_MAX_HEADERS 32
#define HTTP_BUFFER_SIZE 4096

#define HTTP_MAX_CLIENTS 16

// Helper Function

static void append_string(char** buffer, size_t* capacity, size_t* length, const char* str) {
    size_t str_len = strlen(str);
    if (*length + str_len + 1 > *capacity) {
        *capacity = (*capacity + str_len) * 2;
        *buffer = realloc(*buffer, *capacity);
    }
    memcpy(*buffer + *length, str, str_len);
    *length += str_len;
    (*buffer)[*length] = '\0';
}

static bool http_initialized = false;
static http_client_t* active_clients[HTTP_MAX_CLIENTS] = {0};
static uint16_t client_count = 0;

bool http_init(void) {
    if (http_initialized) {
        return true;
    }

    memset(active_clients, 0, sizeof(active_clients));
    client_count = 0;
    http_initialized = true;
    return true;
}

void http_shutdown(void) {
    if (!http_initialized) {
        return;
    }

    // Clean up any active clients
    for (int i = 0; i < HTTP_MAX_CLIENTS; i++) {
        if (active_clients[i]) {
            http_client_destroy(active_clients[i]);
            active_clients[i] = NULL;
        }
    }

    client_count = 0;
    http_initialized = false;
}

// HTTP Client functions
http_client_t* http_client_create(void) {
    if (!http_initialized || client_count >= HTTP_MAX_CLIENTS) {
        return NULL;
    }

    http_client_t* client = malloc(sizeof(http_client_t));
    if (!client) return NULL;

    // Add to active clients
    for (int i = 0; i < HTTP_MAX_CLIENTS; i++) {
        if (!active_clients[i]) {
            active_clients[i] = client;
            client_count++;
            break;
        }
    }

    client->socket = NULL;
    client->host = NULL;
    client->port = 0;
    client->connected = false;

    return client;
}

void http_client_destroy(http_client_t* client) {
    if (!client || !http_initialized) return;

    // Remove from active clients
    for (int i = 0; i < HTTP_MAX_CLIENTS; i++) {
        if (active_clients[i] == client) {
            active_clients[i] = NULL;
            client_count--;
            break;
        }
    }

    if (client->connected) {
        http_client_disconnect(client);
    }

    if (client->host) {
        free(client->host);
    }

    free(client);
}

int http_client_connect(http_client_t* client, const char* host, uint16_t port) {
    if (!client || !host) return -1;

    // Create TCP socket
    int sock_fd = net_socket_create(SOCKET_TCP);
    if (sock_fd < 0) return -1;

    // Resolve hostname to IP
    uint32_t ip = net_resolve_hostname(host);
    if (!ip) {
        net_socket_close(sock_fd);
        return -1;
    }

    // Connect socket
    if (net_socket_connect(sock_fd, ip, port) < 0) {
        net_socket_close(sock_fd);
        return -1;
    }

    // Store connection info
    client->socket = net_socket_get(sock_fd);
    client->host = strdup(host);
    client->port = port;
    client->connected = true;

    return 0;
}

void http_client_disconnect(http_client_t* client) {
    if (!client || !client->connected) return;

    if (client->socket) {
        net_socket_close(client->socket->fd);
        client->socket = NULL;
    }

    client->connected = false;
}

// HTTP Request functions
http_request_t* http_request_create(http_method_t method, const char* url) {
    http_request_t* request = malloc(sizeof(http_request_t));
    if (!request) return NULL;

    request->method = method;
    request->url = strdup(url);
    request->version = strdup("HTTP/1.1");
    request->headers = malloc(sizeof(http_header_t) * HTTP_MAX_HEADERS);
    request->header_count = 0;
    request->body = NULL;
    request->body_length = 0;

    return request;
}

void http_request_destroy(http_request_t* request) {
    if (!request) return;

    if (request->url) free(request->url);
    if (request->version) free(request->version);

    for (uint32_t i = 0; i < request->header_count; i++) {
        free(request->headers[i].name);
        free(request->headers[i].value);
    }

    if (request->headers) free(request->headers);
    if (request->body) free(request->body);
    free(request);
}

void http_request_add_header(http_request_t* request, const char* name, const char* value) {
    if (!request || !name || !value || request->header_count >= HTTP_MAX_HEADERS) return;

    http_header_t* header = &request->headers[request->header_count];
    header->name = strdup(name);
    header->value = strdup(value);
    request->header_count++;
}

// HTTP Response functions
static http_response_t* parse_response(const char* response_data, size_t length) {
    http_response_t* response = malloc(sizeof(http_response_t));
    if (!response) return NULL;

    // Initialize response structure
    response->version = NULL;
    response->status_text = NULL;
    response->headers = malloc(sizeof(http_header_t) * HTTP_MAX_HEADERS);
    response->header_count = 0;
    response->body = NULL;
    response->body_length = 0;

    // Parse status line
    const char* status_end = strchr(response_data, '\r');
    if (!status_end) {
        http_response_destroy(response);
        return NULL;
    }

    // Parse headers and body
    const char* current = status_end + 2;
    const char* headers_end = strstr(current, "\r\n\r\n");
    if (!headers_end) {
        http_response_destroy(response);
        return NULL;
    }

    // Parse body
    const char* body_start = headers_end + 4;
    size_t body_length = length - (body_start - response_data);

    if (body_length > 0) {
        response->body = malloc(body_length);
        if (response->body) {
            memcpy(response->body, body_start, body_length);
            response->body_length = body_length;
        }
    }

    return response;
}

http_response_t* http_send_request(http_client_t* client, http_request_t* request) {
    if (!client || !request || !client->connected) return NULL;

    // Build request string
    char* request_str = NULL;
    size_t capacity = HTTP_BUFFER_SIZE;
    size_t length = 0;

    request_str = malloc(capacity);
    if (!request_str) return NULL;

    // Add request line
    append_string(&request_str, &capacity, &length, http_method_string(request->method));
    append_string(&request_str, &capacity, &length, " ");
    append_string(&request_str, &capacity, &length, request->url);
    append_string(&request_str, &capacity, &length, " ");
    append_string(&request_str, &capacity, &length, request->version);
    append_string(&request_str, &capacity, &length, "\r\n");

    // Add headers
    for (uint32_t i = 0; i < request->header_count; i++) {
        append_string(&request_str, &capacity, &length, request->headers[i].name);
        append_string(&request_str, &capacity, &length, ": ");
        append_string(&request_str, &capacity, &length, request->headers[i].value);
        append_string(&request_str, &capacity, &length, "\r\n");
    }

    // Add blank line to separate headers from body
    append_string(&request_str, &capacity, &length, "\r\n");

    // Send request
    if (net_socket_send(client->socket->fd, request_str, length) < 0) {
        free(request_str);
        return NULL;
    }

    // Add body if present
    if (request->body && request->body_length > 0) {
        if (net_socket_send(client->socket->fd, request->body, request->body_length) < 0) {
            free(request_str);
            return NULL;
        }
    }

    free(request_str);

    // Receive response
    char response_buffer[HTTP_BUFFER_SIZE];
    uint16_t received = HTTP_BUFFER_SIZE;

    if (net_socket_receive(client->socket->fd, response_buffer, &received) < 0) {
        return NULL;
    }

    // Parse response
    return parse_response(response_buffer, received);
}

void http_response_destroy(http_response_t* response) {
    if (!response) return;

    if (response->version) free(response->version);
    if (response->status_text) free(response->status_text);

    for (uint32_t i = 0; i < response->header_count; i++) {
        free(response->headers[i].name);
        free(response->headers[i].value);
    }

    if (response->headers) free(response->headers);
    if (response->body) free(response->body);
    free(response);
}

// Helper functions
const char* http_status_text(http_status_t status) {
    switch (status) {
        case HTTP_OK: return "OK";
        case HTTP_CREATED: return "Created";
        case HTTP_ACCEPTED: return "Accepted";
        case HTTP_NO_CONTENT: return "No Content";
        case HTTP_BAD_REQUEST: return "Bad Request";
        case HTTP_UNAUTHORIZED: return "Unauthorized";
        case HTTP_FORBIDDEN: return "Forbidden";
        case HTTP_NOT_FOUND: return "Not Found";
        case HTTP_SERVER_ERROR: return "Internal Server Error";
        default: return "Unknown Status";
    }
}

const char* http_method_string(http_method_t method) {
    switch (method) {
        case HTTP_GET: return "GET";
        case HTTP_POST: return "POST";
        case HTTP_PUT: return "PUT";
        case HTTP_DELETE: return "DELETE";
        case HTTP_HEAD: return "HEAD";
        case HTTP_OPTIONS: return "OPTIONS";
        case HTTP_PATCH: return "PATCH";
        default: return "UNKNOWN";
    }
}