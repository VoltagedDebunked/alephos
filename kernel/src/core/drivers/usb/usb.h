#ifndef USB_H
#define USB_H

#include <stdint.h>
#include <stdbool.h>

// USB Descriptor Types
#define USB_DT_DEVICE             0x01
#define USB_DT_CONFIG             0x02
#define USB_DT_STRING             0x03
#define USB_DT_INTERFACE          0x04
#define USB_DT_ENDPOINT           0x05
#define USB_DT_HUB               0x29

// USB Device Classes
#define USB_CLASS_AUDIO           0x01
#define USB_CLASS_CDC             0x02
#define USB_CLASS_HID             0x03
#define USB_CLASS_PHYSICAL        0x05
#define USB_CLASS_IMAGE           0x06
#define USB_CLASS_PRINTER         0x07
#define USB_CLASS_MASS_STORAGE    0x08
#define USB_CLASS_HUB             0x09
#define USB_CLASS_CDC_DATA        0x0A
#define USB_CLASS_VIDEO           0x0E
#define USB_CLASS_VENDOR_SPEC     0xFF

// USB Request Types
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIG        0x08
#define USB_REQ_SET_CONFIG        0x09

// USB Device State
#define USB_STATE_DEFAULT         0x00
#define USB_STATE_ADDRESS         0x01
#define USB_STATE_CONFIGURED      0x02
#define USB_STATE_SUSPENDED       0x03

// USB Transfer Types
#define USB_TRANSFER_CONTROL      0x00
#define USB_TRANSFER_ISOCHRONOUS  0x01
#define USB_TRANSFER_BULK         0x02
#define USB_TRANSFER_INTERRUPT    0x03

// USB Speed IDs
#define USB_SPEED_LOW             0x00
#define USB_SPEED_FULL            0x01
#define USB_SPEED_HIGH            0x02
#define USB_SPEED_SUPER           0x03

// USB Device Descriptor
struct usb_device_descriptor {
    uint8_t  length;
    uint8_t  type;
    uint16_t usb_version;
    uint8_t  device_class;
    uint8_t  device_subclass;
    uint8_t  device_protocol;
    uint8_t  max_packet_size;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t  manufacturer_index;
    uint8_t  product_index;
    uint8_t  serial_index;
    uint8_t  num_configurations;
} __attribute__((packed));

// USB Configuration Descriptor
struct usb_config_descriptor {
    uint8_t  length;
    uint8_t  type;
    uint16_t total_length;
    uint8_t  num_interfaces;
    uint8_t  configuration_value;
    uint8_t  configuration_index;
    uint8_t  attributes;
    uint8_t  max_power;
} __attribute__((packed));

// USB Interface Descriptor
struct usb_interface_descriptor {
    uint8_t length;
    uint8_t type;
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t num_endpoints;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t interface_index;
} __attribute__((packed));

// USB Endpoint Descriptor
struct usb_endpoint_descriptor {
    uint8_t  length;
    uint8_t  type;
    uint8_t  endpoint_address;
    uint8_t  attributes;
    uint16_t max_packet_size;
    uint8_t  interval;
} __attribute__((packed));

// USB Device Structure
struct usb_device {
    uint8_t address;
    uint8_t port;
    uint8_t hub_addr;
    uint8_t speed;
    uint16_t max_packet_size;
    struct usb_device_descriptor descriptor;
    struct usb_config_descriptor* config;
    void* driver_data;
    bool configured;
    struct usb_device* next;
};

// Public API Functions
void usb_init(void);
void usb_cleanup(void);
void usb_process_events(void);
struct usb_device* usb_get_device(uint8_t address);

// USB Transfer Functions
int usb_control_transfer(struct usb_device* dev, uint8_t request_type, uint8_t request,
                        uint16_t value, uint16_t index, void* data, uint16_t length);
int usb_bulk_transfer(struct usb_device* dev, uint8_t endpoint,
                     void* data, uint16_t length, uint32_t timeout);
int usb_interrupt_transfer(struct usb_device* dev, uint8_t endpoint,
                          void* data, uint16_t length, uint32_t timeout);

#endif // USB_H