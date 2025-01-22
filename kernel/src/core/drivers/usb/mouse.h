#ifndef USB_MOUSE_H
#define USB_MOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include <core/drivers/usb/usb.h>

// USB HID Mouse Report Descriptor defines
#define USB_HID_MOUSE_BUTTON_LEFT    0x01
#define USB_HID_MOUSE_BUTTON_RIGHT   0x02
#define USB_HID_MOUSE_BUTTON_MIDDLE  0x04

// USB HID Mouse Protocol
#define USB_HID_MOUSE_PROTOCOL_BOOT  0
#define USB_HID_MOUSE_PROTOCOL_REPORT 1

// USB HID Mouse Report structure
typedef struct {
    uint8_t buttons;      // Button states
    int8_t  x;           // X movement
    int8_t  y;           // Y movement
    int8_t  wheel;       // Vertical wheel
    int8_t  pan;         // Horizontal wheel (optional)
} __attribute__((packed)) usb_mouse_report_t;

// USB Mouse device structure
typedef struct {
    struct usb_device* dev;        // USB device handle
    uint8_t interface;             // HID Interface number
    uint8_t in_endpoint;           // IN endpoint for reports
    uint8_t protocol;              // Current protocol
    uint16_t max_packet_size;      // Max packet size for IN endpoint
    bool has_wheel;                // Mouse has scroll wheel
    bool has_pan;                  // Mouse has horizontal scroll
    void* report_buffer;           // Buffer for report data
} usb_mouse_device_t;

// Mouse event structure
typedef struct {
    bool left_button;              // Left button state
    bool right_button;             // Right button state
    bool middle_button;            // Middle button state
    int16_t rel_x;                // Relative X movement
    int16_t rel_y;                // Relative Y movement
    int8_t wheel_delta;           // Wheel movement
    int8_t pan_delta;             // Horizontal scroll movement
} usb_mouse_event_t;

// Mouse event callback type
typedef void (*usb_mouse_event_handler_t)(usb_mouse_event_t* event);

// Function declarations
bool usb_mouse_init(void);
void usb_mouse_cleanup(void);

// Device management
bool usb_mouse_probe(struct usb_device* dev);
bool usb_mouse_connect(struct usb_device* dev);
void usb_mouse_disconnect(struct usb_device* dev);

// Event handling
void usb_mouse_register_handler(usb_mouse_event_handler_t handler);
void usb_mouse_process_events(void);

#endif // USB_MOUSE_H