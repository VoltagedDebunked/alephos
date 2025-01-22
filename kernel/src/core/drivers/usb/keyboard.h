#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include <core/drivers/usb/usb.h>

// USB HID Keyboard Scan Codes
#define USB_HID_KEY_NONE        0x00
#define USB_HID_KEY_ERROR       0x01
#define USB_HID_KEY_A           0x04
#define USB_HID_KEY_B           0x05
#define USB_HID_KEY_Z           0x1D
#define USB_HID_KEY_1           0x1E
#define USB_HID_KEY_0           0x27
#define USB_HID_KEY_ENTER       0x28
#define USB_HID_KEY_ESC         0x29
#define USB_HID_KEY_BACKSPACE   0x2A
#define USB_HID_KEY_TAB         0x2B
#define USB_HID_KEY_SPACE       0x2C
#define USB_HID_KEY_CAPSLOCK    0x39
#define USB_HID_KEY_F1          0x3A
#define USB_HID_KEY_F12         0x45

// Modifier Keys
#define USB_HID_MOD_LEFT_CTRL   0x01
#define USB_HID_MOD_LEFT_SHIFT  0x02
#define USB_HID_MOD_LEFT_ALT    0x04
#define USB_HID_MOD_LEFT_GUI    0x08
#define USB_HID_MOD_RIGHT_CTRL  0x10
#define USB_HID_MOD_RIGHT_SHIFT 0x20
#define USB_HID_MOD_RIGHT_ALT   0x40
#define USB_HID_MOD_RIGHT_GUI   0x80

// USB HID Keyboard Report Structure
typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} __attribute__((packed)) usb_keyboard_report_t;

// Function declarations
void usb_keyboard_init(void);
void usb_keyboard_cleanup(void);
bool usb_keyboard_probe(struct usb_device* dev);
bool usb_keyboard_attach(struct usb_device* dev);
void usb_keyboard_detach(struct usb_device* dev);
char usb_keyboard_get_char(void);

// Keyboard event callback type
typedef void (*keyboard_event_handler_t)(uint8_t scancode, bool pressed);
void usb_keyboard_register_handler(keyboard_event_handler_t handler);

#endif // USB_KEYBOARD_H