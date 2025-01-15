#ifndef TTY_H
#define TTY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// TTY IOCTL Commands
#define TCGETS          0x5401
#define TCSETS          0x5402
#define TCSETSW         0x5403
#define TCSETSF         0x5404
#define TCGETA          0x5405
#define TCSETA          0x5406
#define TCSETAW         0x5407
#define TCSETAF         0x5408
#define TCSBRK          0x5409
#define TCXONC          0x540A
#define TCFLSH          0x540B
#define TIOCEXCL        0x540C
#define TIOCNXCL        0x540D
#define TIOCSCTTY       0x540E
#define TIOCGPGRP       0x540F
#define TIOCSPGRP       0x5410
#define TIOCOUTQ        0x5411
#define TIOCSTI         0x5412
#define TIOCGWINSZ      0x5413
#define TIOCSWINSZ      0x5414
#define TIOCMGET        0x5415
#define TIOCMBIS        0x5416
#define TIOCMBIC        0x5417
#define TIOCMSET        0x5418

// Window size structure
struct winsize {
    uint16_t ws_row;    // Rows, in characters
    uint16_t ws_col;    // Columns, in characters
    uint16_t ws_xpixel; // Horizontal size, pixels
    uint16_t ws_ypixel; // Vertical size, pixels
};

// TTY Constants
#define TTY_MAX_DEVICES     8
#define TTY_BUFFER_SIZE     4096
#define TTY_NAME_MAX        32

// TTY Device States
#define TTY_STATE_UNUSED    0
#define TTY_STATE_ACTIVE    1
#define TTY_STATE_STOPPED   2

// TTY Flags
#define TTY_FLAG_ECHO       (1 << 0)
#define TTY_FLAG_CANONICAL  (1 << 1)
#define TTY_FLAG_RAW       (1 << 2)

// TTY Structure
typedef struct {
    char name[TTY_NAME_MAX];
    uint8_t state;
    uint32_t flags;

    // Buffers
    char input_buffer[TTY_BUFFER_SIZE];
    uint32_t input_head;
    uint32_t input_tail;
    uint32_t input_count;

    char output_buffer[TTY_BUFFER_SIZE];
    uint32_t output_head;
    uint32_t output_tail;
    uint32_t output_count;

    // Terminal size
    uint32_t rows;
    uint32_t cols;

    // Cursor position
    uint32_t cursor_x;
    uint32_t cursor_y;

    // Callbacks
    void (*write_char)(char c);
    char (*read_char)(void);
} tty_device_t;

// Function declarations
void tty_init(void);
tty_device_t* tty_create(const char* name);
void tty_destroy(tty_device_t* tty);

// Device operations
int tty_write(tty_device_t* tty, const char* buf, size_t count);
int tty_read(tty_device_t* tty, char* buf, size_t count);
int tty_ioctl(tty_device_t* tty, uint32_t cmd, uint64_t arg);

// Buffer operations
void tty_input_putc(tty_device_t* tty, char c);
char tty_input_getc(tty_device_t* tty);
void tty_output_putc(tty_device_t* tty, char c);
char tty_output_getc(tty_device_t* tty);

// Terminal operations
void tty_clear(tty_device_t* tty);
void tty_set_cursor(tty_device_t* tty, uint32_t x, uint32_t y);
void tty_scroll(tty_device_t* tty, int lines);
void tty_process_escape_sequence(tty_device_t* tty, const char* seq);

#endif // TTY_H