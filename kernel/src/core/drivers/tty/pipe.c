#include <core/drivers/tty/pipe.h>
#include <mm/heap.h>
#include <utils/mem.h>

pipe_t* pipe_create(void) {
    pipe_t* pipe = malloc(sizeof(pipe_t));
    if (!pipe) return NULL;

    pipe->buffer = malloc(PIPE_BUFFER_SIZE);
    if (!pipe->buffer) {
        free(pipe);
        return NULL;
    }

    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->count = 0;
    pipe->state = PIPE_STATE_OPEN | PIPE_STATE_READABLE | PIPE_STATE_WRITABLE;
    pipe->readers = 0;
    pipe->writers = 0;

    return pipe;
}

void pipe_destroy(pipe_t* pipe) {
    if (!pipe) return;

    if (pipe->buffer) {
        free(pipe->buffer);
    }
    free(pipe);
}

ssize_t pipe_read(pipe_t* pipe, void* buf, size_t count) {
    if (!pipe || !buf || !(pipe->state & PIPE_STATE_READABLE)) {
        return -1;
    }

    // Block if pipe empty and has writers
    while (pipe->count == 0) {
        if (pipe->writers == 0 || (pipe->state & PIPE_STATE_EOF)) {
            return 0; // EOF
        }
        // Would normally sleep here
    }

    size_t bytes_read = 0;
    uint8_t* dest = buf;

    // Read either requested bytes or available bytes, whichever is smaller
    size_t to_read = (count < pipe->count) ? count : pipe->count;

    while (bytes_read < to_read) {
        dest[bytes_read++] = pipe->buffer[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) & (PIPE_BUFFER_SIZE - 1);
        pipe->count--;
    }

    return bytes_read;
}

ssize_t pipe_write(pipe_t* pipe, const void* buf, size_t count) {
    if (!pipe || !buf || !(pipe->state & PIPE_STATE_WRITABLE)) {
        return -1;
    }

    // Check if anyone is reading
    if (pipe->readers == 0) {
        // SIGPIPE would be sent here
        return -1;
    }

    const uint8_t* src = buf;
    size_t bytes_written = 0;

    while (bytes_written < count) {
        // Block if pipe is full
        while (pipe->count == PIPE_BUFFER_SIZE) {
            if (pipe->readers == 0) {
                return bytes_written > 0 ? bytes_written : -1;
            }
            // Would normally sleep here
        }

        // Write data
        pipe->buffer[pipe->write_pos] = src[bytes_written++];
        pipe->write_pos = (pipe->write_pos + 1) & (PIPE_BUFFER_SIZE - 1);
        pipe->count++;
    }

    return bytes_written;
}

void pipe_add_reader(pipe_t* pipe) {
    if (!pipe) return;
    pipe->readers++;
    pipe->state |= PIPE_STATE_READABLE;
}

void pipe_add_writer(pipe_t* pipe) {
    if (!pipe) return;
    pipe->writers++;
    pipe->state |= PIPE_STATE_WRITABLE;
}

void pipe_remove_reader(pipe_t* pipe) {
    if (!pipe || pipe->readers == 0) return;
    pipe->readers--;
    if (pipe->readers == 0) {
        pipe->state &= ~PIPE_STATE_READABLE;
    }
}

void pipe_remove_writer(pipe_t* pipe) {
    if (!pipe || pipe->writers == 0) return;
    pipe->writers--;
    if (pipe->writers == 0) {
        pipe->state &= ~PIPE_STATE_WRITABLE;
        pipe->state |= PIPE_STATE_EOF;
    }
}

bool pipe_is_readable(pipe_t* pipe) {
    return pipe && (pipe->state & PIPE_STATE_READABLE) &&
           (pipe->count > 0 || pipe->writers > 0);
}

bool pipe_is_writable(pipe_t* pipe) {
    return pipe && (pipe->state & PIPE_STATE_WRITABLE) &&
           pipe->readers > 0 && pipe->count < PIPE_BUFFER_SIZE;
}