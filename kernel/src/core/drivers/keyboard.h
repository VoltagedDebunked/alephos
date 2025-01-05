#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <limine.h>

#define KB_DATA_PORT     0x60
#define KB_STATUS_PORT   0x64
#define KB_COMMAND_PORT  0x64
#define KB_IRQ           1

#define KB_STATUS_OUTPUT_FULL  0x01
#define KB_STATUS_INPUT_FULL   0x02

#define KB_CMD_READ_CONFIG     0x20
#define KB_CMD_WRITE_CONFIG    0x60
#define KB_CMD_SELF_TEST      0xAA
#define KB_CMD_INTERFACE_TEST 0xAB
#define KB_CMD_DISABLE        0xAD
#define KB_CMD_ENABLE         0xAE

#define KB_CONFIG_INT         0x01
#define KB_CONFIG_TRANSLATE   0x40

bool init_keyboard(struct limine_framebuffer *fb);
uint8_t keyboard_read_scancode(void);
void keyboard_handler(void);

#endif