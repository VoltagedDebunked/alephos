#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stdbool.h>

// COM port addresses
#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

// Initialize serial port with standard parameters
void serial_init(uint16_t port);

// Basic I/O for debug purposes
void serial_write_char(uint16_t port, char c);
char serial_read_char(uint16_t port);
bool serial_can_read(uint16_t port);
bool serial_can_write(uint16_t port);

#endif // SERIAL_H