#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

// USB HID keyboard modifiers
#define USB_MOD_LCTRL   (1 << 0)
#define USB_MOD_LSHIFT  (1 << 1)
#define USB_MOD_LALT    (1 << 2)
#define USB_MOD_LGUI    (1 << 3)
#define USB_MOD_RCTRL   (1 << 4)
#define USB_MOD_RSHIFT  (1 << 5)
#define USB_MOD_RALT    (1 << 6)
#define USB_MOD_RGUI    (1 << 7)

void usb_keyboard_init(void);
bool usb_keyboard_available(void);

#endif // USB_KEYBOARD_H