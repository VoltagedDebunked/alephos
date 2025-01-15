#include <core/drivers/usb/mouse.h>
#include <core/drivers/usb/usb.h>
#include <core/drivers/usb/xhci.h>
#include <utils/mem.h>
#include <mm/heap.h>

// Global USB mouse device
static usb_mouse_device_t usb_mouse = {0};

// Event handler for mouse events
static usb_mouse_event_handler_t mouse_event_handler = NULL;

// Most recent mouse packet
static usb_mouse_packet_t last_mouse_packet = {0};

// USB HID Boot Protocol Mouse Report Descriptor
static const uint8_t usb_mouse_report_descriptor[] = {
    0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
    0x09, 0x02,        // USAGE (Mouse)
    0xa1, 0x01,        // COLLECTION (Application)
    0x09, 0x01,        //   USAGE (Pointer)
    0xa1, 0x00,        //   COLLECTION (Physical)
    0x05, 0x09,        //     USAGE_PAGE (Button)
    0x19, 0x01,        //     USAGE_MINIMUM (Button 1)
    0x29, 0x03,        //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00,        //     LOGICAL_MINIMUM (0)
    0x25, 0x01,        //     LOGICAL_MAXIMUM (1)
    0x95, 0x03,        //     REPORT_COUNT (3)
    0x75, 0x01,        //     REPORT_SIZE (1)
    0x81, 0x02,        //     INPUT (Data,Var,Abs)
    0x95, 0x01,        //     REPORT_COUNT (1)
    0x75, 0x05,        //     REPORT_SIZE (5)
    0x81, 0x03,        //     INPUT (Const,Var,Abs)
    0x05, 0x01,        //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,        //     USAGE (X)
    0x09, 0x31,        //     USAGE (Y)
    0x09, 0x38,        //     USAGE (Wheel)
    0x15, 0x81,        //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,        //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,        //     REPORT_SIZE (8)
    0x95, 0x03,        //     REPORT_COUNT (3)
    0x81, 0x06,        //     INPUT (Data,Var,Rel)
    0xc0,              //   END_COLLECTION
    0xc0               // END_COLLECTION
};

// Process raw USB mouse report
static void process_mouse_report(uint8_t* report, uint16_t length) {
    if (length < 3) return;  // Invalid report

    usb_mouse_packet_t packet = {0};

    // Button states
    packet.button_state = report[0];
    packet.left_button = report[0] & USB_MOUSE_BUTTON_LEFT;
    packet.right_button = report[0] & USB_MOUSE_BUTTON_RIGHT;
    packet.middle_button = report[0] & USB_MOUSE_BUTTON_MIDDLE;

    // X movement (signed 8-bit relative movement)
    packet.x_movement = (int8_t)report[1];

    // Y movement (signed 8-bit relative movement, inverted)
    packet.y_movement = -(int8_t)report[2];

    // Optional scroll wheel (if 4th byte exists)
    if (length >= 4) {
        packet.scroll = (int8_t)report[3];
    }

    // Store last packet
    last_mouse_packet = packet;

    // Call event handler if registered
    if (mouse_event_handler) {
        mouse_event_handler(&packet);
    }
}

// Probe for USB mouse
bool usb_mouse_probe(struct usb_device* dev) {
    // Validate USB mouse device
    if (!dev || dev->class != USB_CLASS_HID) return false;

    // Check for mouse-specific protocol
    if (dev->subclass != 1 || dev->protocol != 2) return false;

    return true;
}

// Connect USB mouse device
bool usb_mouse_connect(struct usb_device* dev) {
    // Validate device
    if (!usb_mouse_probe(dev)) return false;

    // Configure device
    usb_mouse.device_address = dev->address;
    usb_mouse.max_packet_size = dev->max_packet_size;
    usb_mouse.connected = true;

    return true;
}

// Disconnect USB mouse device
void usb_mouse_disconnect(struct usb_device* dev) {
    if (dev->address == usb_mouse.device_address) {
        memset(&usb_mouse, 0, sizeof(usb_mouse_device_t));
    }
}

// USB mouse initialization
void usb_mouse_init(void) {
    // Ensure USB stack is initialized
    usb_init();
}

// Check if USB mouse is available
bool usb_mouse_available(void) {
    return usb_mouse.connected;
}

// Read most recent mouse packet
bool usb_mouse_read_packet(usb_mouse_packet_t* packet) {
    if (!usb_mouse.connected || !packet) return false;

    // Copy last packet
    memcpy(packet, &last_mouse_packet, sizeof(usb_mouse_packet_t));
    return true;
}

// Register mouse event handler
void usb_mouse_register_event_handler(usb_mouse_event_handler_t handler) {
    mouse_event_handler = handler;
}