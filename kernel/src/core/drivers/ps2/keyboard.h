#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

void keyboard_init(void);
char keyboard_get_char(void);

#endif