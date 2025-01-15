#ifndef TTY_PIPE_H
#define TTY_PIPE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <core/syscalls.h>

// Pipe buffer size (must be power of 2)
#define PIPE_BUFFER_SIZE 4096

// Pipe states
#define PIPE_STATE_OPEN     0x1
#define PIPE_STATE_READABLE 0x2
#define PIPE_STATE_WRITABLE 0x4
#define PIPE_STATE_EOF      0x8

// Pipe structure
typedef struct {
    uint8_t* buffer;           // Circular buffer
    uint32_t read_pos;         // Read position
    uint32_t write_pos;        // Write position
    uint32_t count;            // Number of bytes in buffer
    uint32_t state;            // Pipe state flags
    uint32_t readers;          // Number of reading processes
    uint32_t writers;          // Number of writing processes
} pipe_t;

// Create a new pipe
pipe_t* pipe_create(void);

// Destroy a pipe and free its resources
void pipe_destroy(pipe_t* pipe);

// Read from pipe
ssize_t pipe_read(pipe_t* pipe, void* buf, size_t count);

// Write to pipe
ssize_t pipe_write(pipe_t* pipe, const void* buf, size_t count);

// Add reader to pipe
void pipe_add_reader(pipe_t* pipe);

// Add writer to pipe
void pipe_add_writer(pipe_t* pipe);

// Remove reader from pipe
void pipe_remove_reader(pipe_t* pipe);

// Remove writer from pipe
void pipe_remove_writer(pipe_t* pipe);

// Check if pipe is readable
bool pipe_is_readable(pipe_t* pipe);

// Check if pipe is writable
bool pipe_is_writable(pipe_t* pipe);

#endif // TTY_PIPE_H