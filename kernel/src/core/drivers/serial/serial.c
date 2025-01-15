#include <core/drivers/serial/serial.h>
#include <utils/io.h>

// Register offsets
#define DATA_REG        0x0
#define INT_ENABLE_REG  0x1
#define FIFO_CTRL_REG   0x2
#define LINE_CTRL_REG   0x3
#define MODEM_CTRL_REG  0x4
#define LINE_STATUS_REG 0x5
#define DIVISOR_LOW     0x0
#define DIVISOR_HIGH    0x1

// Line status flags
#define LSR_DATA_READY  0x01
#define LSR_THR_EMPTY   0x20

void serial_init(uint16_t port) {
    // Disable interrupts
    outb(port + INT_ENABLE_REG, 0x00);

    // Enable DLAB to set baud rate
    outb(port + LINE_CTRL_REG, 0x80);

    // Set divisor to 1 (115200 baud)
    outb(port + DIVISOR_LOW, 0x01);
    outb(port + DIVISOR_HIGH, 0x00);

    // 8 bits, no parity, one stop bit
    outb(port + LINE_CTRL_REG, 0x03);

    // Enable and clear FIFO, with 14-byte threshold
    outb(port + FIFO_CTRL_REG, 0xC7);

    // Enable RTS/DSR
    outb(port + MODEM_CTRL_REG, 0x0B);
}

void serial_write_char(uint16_t port, char c) {
    while ((inb(port + LINE_STATUS_REG) & LSR_THR_EMPTY) == 0);
    outb(port, c);
}

char serial_read_char(uint16_t port) {
    while ((inb(port + LINE_STATUS_REG) & LSR_DATA_READY) == 0);
    return inb(port);
}

bool serial_can_read(uint16_t port) {
    return (inb(port + LINE_STATUS_REG) & LSR_DATA_READY) != 0;
}

bool serial_can_write(uint16_t port) {
    return (inb(port + LINE_STATUS_REG) & LSR_THR_EMPTY) != 0;
}