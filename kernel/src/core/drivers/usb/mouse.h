#ifndef USB_MOUSE_H
#define USB_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

// USB HID mouse report descriptor constants
#define USB_MOUSE_BUTTON_LEFT    0x01
#define USB_MOUSE_BUTTON_RIGHT   0x02
#define USB_MOUSE_BUTTON_MIDDLE  0x04

// Mouse packet structure (similar to PS/2 mouse)
typedef struct {
    int8_t x_movement;
    int8_t y_movement;
    int8_t scroll;
    uint8_t button_state;
    bool left_button;
    bool right_button;
    bool middle_button;
} usb_mouse_packet_t;

// USB mouse device structure
typedef struct {
    uint8_t device_address;
    uint8_t endpoint;
    uint16_t max_packet_size;
    bool connected;
} usb_mouse_device_t;

struct usb_device;

// USB mouse initialization and configuration
void usb_mouse_init(void);
bool usb_mouse_probe(struct usb_device* dev);
bool usb_mouse_connect(struct usb_device* dev);
void usb_mouse_disconnect(struct usb_device* dev);

// Mouse packet reading and processing
bool usb_mouse_read_packet(usb_mouse_packet_t* packet);

// Mouse event handling
typedef void (*usb_mouse_event_handler_t)(usb_mouse_packet_t* packet);
void usb_mouse_register_event_handler(usb_mouse_event_handler_t handler);

// Check if a USB mouse is available
bool usb_mouse_available(void);

#endif // USB_MOUSE_H