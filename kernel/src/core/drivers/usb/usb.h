#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stdbool.h>

// USB Device Classes
#define USB_CLASS_INTERFACE     0x00
#define USB_CLASS_AUDIO        0x01
#define USB_CLASS_CDC          0x02
#define USB_CLASS_HID          0x03
#define USB_CLASS_PHYSICAL     0x05
#define USB_CLASS_IMAGE        0x06
#define USB_CLASS_PRINTER      0x07
#define USB_CLASS_MASS_STORAGE 0x08
#define USB_CLASS_HUB          0x09
#define USB_CLASS_CDC_DATA     0x0A
#define USB_CLASS_SMART_CARD   0x0B
#define USB_CLASS_VIDEO        0x0E
#define USB_CLASS_WIRELESS     0xE0
#define USB_CLASS_MISC         0xEF
#define USB_CLASS_APP_SPEC     0xFE
#define USB_CLASS_VENDOR_SPEC  0xFF

// USB Device Descriptors
#define USB_DT_DEVICE          0x01
#define USB_DT_CONFIG          0x02
#define USB_DT_STRING          0x03
#define USB_DT_INTERFACE       0x04
#define USB_DT_ENDPOINT        0x05
#define USB_DT_HID             0x21
#define USB_DT_REPORT          0x22
#define USB_DT_PHYSICAL        0x23
#define USB_DT_HUB             0x29

struct usb_device {
    uint8_t address;
    uint8_t port;
    uint8_t speed;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t class;
    uint8_t subclass;
    uint8_t protocol;
    uint8_t max_packet_size;
};

void usb_init(void);
struct usb_device* usb_scan_devices(void);
bool usb_configure_device(struct usb_device* dev);

#endif // USB_H