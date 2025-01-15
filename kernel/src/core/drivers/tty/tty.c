#include <core/drivers/tty/tty.h>
#include <utils/mem.h>
#include <utils/str.h>
#include <core/drivers/serial/serial.h>
#include <graphics/display.h>
#include <graphics/colors.h>

extern struct limine_framebuffer* global_framebuffer;

// Global TTY array
static tty_device_t tty_devices[TTY_MAX_DEVICES];
static uint32_t tty_count = 0;

// Wrapper functions to match function pointer signatures
static void console_write_char(char c) {
    draw_char(global_framebuffer, c,
             global_framebuffer->width / 8 * (c % 80),  // Basic wrapping
             global_framebuffer->height / 16 * ((c / 80) % 25),
             WHITE);
}

static char console_read_char(void) {
    return serial_read_char(COM1);
}

static void serial_write_wrapper(char c) {
    serial_write_char(COM1, c);
}

static char serial_read_wrapper(void) {
    return serial_read_char(COM1);
}

// Initialize TTY subsystem
void tty_init(void) {
    memset(tty_devices, 0, sizeof(tty_devices));
    tty_count = 0;

    // Initialize console TTY
    tty_device_t* console = tty_create("console");
    if (console) {
        console->rows = global_framebuffer->height / 16; // Assuming 16px font height
        console->cols = global_framebuffer->width / 8;   // Assuming 8px font width
        console->flags = TTY_FLAG_ECHO | TTY_FLAG_CANONICAL;

        // Set up console callbacks with wrapper functions
        console->write_char = console_write_char;
        console->read_char = console_read_char;
    }

    // Initialize serial TTY
    tty_device_t* serial = tty_create("ttyS0");
    if (serial) {
        serial->rows = 25;  // Standard terminal size
        serial->cols = 80;
        serial->flags = TTY_FLAG_ECHO | TTY_FLAG_CANONICAL;

        // Set up serial callbacks with wrapper functions
        serial->write_char = serial_write_wrapper;
        serial->read_char = serial_read_wrapper;
    }
}

// Create new TTY device
tty_device_t* tty_create(const char* name) {
    if (tty_count >= TTY_MAX_DEVICES) {
        return NULL;
    }

    tty_device_t* tty = &tty_devices[tty_count++];
    memset(tty, 0, sizeof(tty_device_t));

    strncpy(tty->name, name, TTY_NAME_MAX - 1);
    tty->state = TTY_STATE_ACTIVE;

    return tty;
}

// Destroy TTY device
void tty_destroy(tty_device_t* tty) {
    if (!tty) return;

    // Clear buffers
    memset(tty->input_buffer, 0, TTY_BUFFER_SIZE);
    memset(tty->output_buffer, 0, TTY_BUFFER_SIZE);

    tty->state = TTY_STATE_UNUSED;
}

// Write to TTY
int tty_write(tty_device_t* tty, const char* buf, size_t count) {
    if (!tty || !buf || tty->state != TTY_STATE_ACTIVE) {
        return -1;
    }

    size_t written = 0;
    for (size_t i = 0; i < count; i++) {
        char c = buf[i];

        // Handle special characters
        if (c == '\n') {
            tty->cursor_x = 0;
            tty->cursor_y++;
            if (tty->cursor_y >= tty->rows) {
                tty_scroll(tty, 1);
                tty->cursor_y = tty->rows - 1;
            }
        } else if (c == '\r') {
            tty->cursor_x = 0;
        } else if (c == '\b') {
            if (tty->cursor_x > 0) {
                tty->cursor_x--;
            }
        } else if (c == '\t') {
            tty->cursor_x = (tty->cursor_x + 8) & ~7;
        } else if (c == '\033') {
            // Handle escape sequences
            if (i + 1 < count) {
                size_t seq_len = 0;
                while (i + 1 + seq_len < count && buf[i + 1 + seq_len] != '\033') {
                    seq_len++;
                }
                if (seq_len > 0) {
                    char seq[32];
                    memcpy(seq, buf + i + 1, seq_len);
                    seq[seq_len] = '\0';
                    tty_process_escape_sequence(tty, seq);
                    i += seq_len;
                }
            }
        } else {
            // Regular character
            if (tty->write_char) {
                tty->write_char(c);
                written++;
            }

            tty->cursor_x++;
            if (tty->cursor_x >= tty->cols) {
                tty->cursor_x = 0;
                tty->cursor_y++;
                if (tty->cursor_y >= tty->rows) {
                    tty_scroll(tty, 1);
                    tty->cursor_y = tty->rows - 1;
                }
            }
        }
    }

    return written;
}

// Read from TTY
int tty_read(tty_device_t* tty, char* buf, size_t count) {
    if (!tty || !buf || tty->state != TTY_STATE_ACTIVE) {
        return -1;
    }

    size_t read = 0;
    if (tty->flags & TTY_FLAG_CANONICAL) {
        // Canonical mode: read until newline
        while (read < count) {
            char c = tty->read_char ? tty->read_char() : 0;
            if (!c) break;

            if (tty->flags & TTY_FLAG_ECHO) {
                if (tty->write_char) {
                    tty->write_char(c);
                }
            }

            if (c == '\n') {
                buf[read++] = c;
                break;
            }

            buf[read++] = c;
        }
    } else {
        // Raw mode: read available characters
        while (read < count && tty->input_count > 0) {
            buf[read++] = tty_input_getc(tty);
        }
    }

    return read;
}

// TTY ioctl operations
int tty_ioctl(tty_device_t* tty, uint32_t cmd, uint64_t arg) {
    if (!tty) return -1;

    switch (cmd) {
        case TIOCGWINSZ:
            // Get window size
            if (arg) {
                struct winsize* ws = (struct winsize*)arg;
                ws->ws_row = tty->rows;
                ws->ws_col = tty->cols;
                ws->ws_xpixel = global_framebuffer->width;
                ws->ws_ypixel = global_framebuffer->height;
                return 0;
            }
            break;

        case TIOCSWINSZ:
            // Set window size
            if (arg) {
                struct winsize* ws = (struct winsize*)arg;
                tty->rows = ws->ws_row;
                tty->cols = ws->ws_col;
                return 0;
            }
            break;
    }

    return -1;
}

// Buffer operations
void tty_input_putc(tty_device_t* tty, char c) {
    if (!tty || tty->input_count >= TTY_BUFFER_SIZE) return;

    tty->input_buffer[tty->input_tail] = c;
    tty->input_tail = (tty->input_tail + 1) % TTY_BUFFER_SIZE;
    tty->input_count++;
}

char tty_input_getc(tty_device_t* tty) {
    if (!tty || tty->input_count == 0) return 0;

    char c = tty->input_buffer[tty->input_head];
    tty->input_head = (tty->input_head + 1) % TTY_BUFFER_SIZE;
    tty->input_count--;

    return c;
}

void tty_output_putc(tty_device_t* tty, char c) {
    if (!tty || tty->output_count >= TTY_BUFFER_SIZE) return;

    tty->output_buffer[tty->output_tail] = c;
    tty->output_tail = (tty->output_tail + 1) % TTY_BUFFER_SIZE;
    tty->output_count++;
}

char tty_output_getc(tty_device_t* tty) {
    if (!tty || tty->output_count == 0) return 0;

    char c = tty->output_buffer[tty->output_head];
    tty->output_head = (tty->output_head + 1) % TTY_BUFFER_SIZE;
    tty->output_count--;

    return c;
}

// Terminal operations
void tty_clear(tty_device_t* tty) {
    if (!tty) return;

    // Clear screen using framebuffer
    if (global_framebuffer) {
        clear_screen(global_framebuffer);
    }

    tty->cursor_x = 0;
    tty->cursor_y = 0;
}

void tty_set_cursor(tty_device_t* tty, uint32_t x, uint32_t y) {
    if (!tty) return;

    if (x < tty->cols) tty->cursor_x = x;
    if (y < tty->rows) tty->cursor_y = y;
}

void tty_scroll(tty_device_t* tty, int lines) {
    if (!tty || lines <= 0) return;

    if (global_framebuffer) {
        // Scroll framebuffer content up
        uint32_t line_height = 16;  // Assuming 16px font height
        uint32_t scroll_size = lines * line_height;

        if (scroll_size < global_framebuffer->height) {
            memmove((void*)global_framebuffer->address,
                   (void*)(global_framebuffer->address + scroll_size * global_framebuffer->pitch),
                   (global_framebuffer->height - scroll_size) * global_framebuffer->pitch);

            // Clear new lines at bottom
            memset((void*)(global_framebuffer->address +
                   (global_framebuffer->height - scroll_size) * global_framebuffer->pitch),
                   0,
                   scroll_size * global_framebuffer->pitch);
        }
    }
}

void tty_process_escape_sequence(tty_device_t* tty, const char* seq) {
    if (!tty || !seq) return;

    // Handle basic VT100 escape sequences
    if (seq[0] == '[') {
        char cmd = seq[strlen(seq) - 1];
        switch (cmd) {
            case 'H':  // Home
                tty_set_cursor(tty, 0, 0);
                break;

            case 'J':  // Clear screen
                if (seq[1] == '2') {
                    tty_clear(tty);
                }
                break;

            case 'A':  // Cursor up
                if (tty->cursor_y > 0) tty->cursor_y--;
                break;

            case 'B':  // Cursor down
                if (tty->cursor_y < tty->rows - 1) tty->cursor_y++;
                break;

            case 'C':  // Cursor right
                if (tty->cursor_x < tty->cols - 1) tty->cursor_x++;
                break;

            case 'D':  // Cursor left
                if (tty->cursor_x > 0) tty->cursor_x--;
                break;
        }
    }
}