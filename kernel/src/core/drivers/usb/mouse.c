#include <core/drivers/usb/mouse.h>
#include <utils/mem.h>
#include <utils/log.h>

// HID Class-Specific Requests
#define HID_GET_REPORT      0x01
#define HID_GET_IDLE        0x02
#define HID_GET_PROTOCOL    0x03
#define HID_SET_REPORT      0x09
#define HID_SET_IDLE        0x0A
#define HID_SET_PROTOCOL    0x0B

// HID Descriptor Types
#define HID_DESCRIPTOR           0x21
#define HID_REPORT_DESCRIPTOR   0x22

#define MAX_MICE 4  // Maximum number of connected mice

static struct {
    usb_mouse_device_t devices[MAX_MICE];
    uint8_t num_devices;
    usb_mouse_event_handler_t event_handler;
    bool initialized;
} mouse_state;

static bool parse_mouse_descriptor(usb_mouse_device_t* mouse) {
    uint8_t desc[256];
    int ret;

    // Get HID descriptor
    ret = usb_control_transfer(mouse->dev, 0x81,
                             USB_REQ_GET_DESCRIPTOR,
                             (HID_DESCRIPTOR << 8), mouse->interface,
                             desc, sizeof(desc));
    if (ret < 0) return false;

    uint16_t report_desc_len = *(uint16_t*)(desc + 7);

    // Get report descriptor
    ret = usb_control_transfer(mouse->dev, 0x81,
                             USB_REQ_GET_DESCRIPTOR,
                             (HID_REPORT_DESCRIPTOR << 8), mouse->interface,
                             desc, report_desc_len);
    if (ret < 0) return false;

    // Parse report descriptor to determine mouse capabilities
    mouse->has_wheel = false;
    mouse->has_pan = false;

    for (int i = 0; i < ret; i++) {
        if (desc[i] == 0x09) {  // Usage
            if (desc[i + 1] == 0x38) {  // Wheel
                mouse->has_wheel = true;
            } else if (desc[i + 1] == 0x37) {  // Pan
                mouse->has_pan = true;
            }
        }
    }

    return true;
}

bool usb_mouse_init(void) {
    if (mouse_state.initialized) {
        return true;
    }

    memset(&mouse_state, 0, sizeof(mouse_state));
    mouse_state.initialized = true;

    log_info("USB Mouse driver initialized");
    return true;
}

void usb_mouse_cleanup(void) {
    if (!mouse_state.initialized) {
        return;
    }

    // Clean up each mouse device
    for (int i = 0; i < mouse_state.num_devices; i++) {
        usb_mouse_device_t* mouse = &mouse_state.devices[i];
        if (mouse->report_buffer) {
            free(mouse->report_buffer);
        }
    }

    memset(&mouse_state, 0, sizeof(mouse_state));
}

bool usb_mouse_probe(struct usb_device* dev) {
    if (!dev || dev->descriptor.device_class != USB_CLASS_HID ||
        dev->config == NULL) {
        return false;
    }

    // Look for mouse interface
    struct usb_interface_descriptor* intf = NULL;
    uint8_t* ptr = (uint8_t*)dev->config;
    uint16_t len = dev->config->total_length;

    for (uint16_t i = 0; i < len;) {
        struct usb_interface_descriptor* desc = (struct usb_interface_descriptor*)(ptr + i);

        if (desc->type == USB_DT_INTERFACE &&
            desc->interface_class == USB_CLASS_HID &&
            desc->interface_protocol == 2) {  // Mouse protocol
            intf = desc;
            break;
        }

        i += desc->length;
    }

    return intf != NULL;
}

bool usb_mouse_connect(struct usb_device* dev) {
    if (!mouse_state.initialized || mouse_state.num_devices >= MAX_MICE ||
        !usb_mouse_probe(dev)) {
        return false;
    }

    usb_mouse_device_t* mouse = &mouse_state.devices[mouse_state.num_devices];
    memset(mouse, 0, sizeof(usb_mouse_device_t));

    mouse->dev = dev;

    // Find HID interface and endpoints
    uint8_t* ptr = (uint8_t*)dev->config;
    uint16_t len = dev->config->total_length;

    for (uint16_t i = 0; i < len;) {
        uint8_t desc_type = ptr[i + 1];
        uint8_t desc_len = ptr[i];

        if (desc_type == USB_DT_INTERFACE) {
            struct usb_interface_descriptor* intf = (struct usb_interface_descriptor*)(ptr + i);
            if (intf->interface_class == USB_CLASS_HID &&
                intf->interface_protocol == 2) {
                mouse->interface = intf->interface_number;
            }
        } else if (desc_type == USB_DT_ENDPOINT) {
            struct usb_endpoint_descriptor* ep = (struct usb_endpoint_descriptor*)(ptr + i);
            if ((ep->endpoint_address & 0x80) &&  // IN endpoint
                (ep->attributes & 0x03) == 3) {   // Interrupt transfer
                mouse->in_endpoint = ep->endpoint_address;
                mouse->max_packet_size = ep->max_packet_size;
            }
        }

        i += desc_len;
    }

    // Parse HID descriptors
    if (!parse_mouse_descriptor(mouse)) {
        return false;
    }

    // Allocate report buffer
    mouse->report_buffer = malloc(mouse->max_packet_size);
    if (!mouse->report_buffer) {
        return false;
    }

    // Set boot protocol
    if (usb_control_transfer(dev, 0x21, HID_SET_PROTOCOL,
                           USB_HID_MOUSE_PROTOCOL_BOOT, mouse->interface,
                           NULL, 0) < 0) {
        free(mouse->report_buffer);
        return false;
    }

    mouse_state.num_devices++;
    log_info("USB Mouse connected");
    return true;
}

void usb_mouse_disconnect(struct usb_device* dev) {
    for (int i = 0; i < mouse_state.num_devices; i++) {
        if (mouse_state.devices[i].dev == dev) {
            if (mouse_state.devices[i].report_buffer) {
                free(mouse_state.devices[i].report_buffer);
            }

            // Shift remaining devices
            if (i < mouse_state.num_devices - 1) {
                memmove(&mouse_state.devices[i],
                       &mouse_state.devices[i + 1],
                       (mouse_state.num_devices - i - 1) * sizeof(usb_mouse_device_t));
            }

            mouse_state.num_devices--;
            log_info("USB Mouse disconnected");
            break;
        }
    }
}

void usb_mouse_register_handler(usb_mouse_event_handler_t handler) {
    mouse_state.event_handler = handler;
}

void usb_mouse_process_events(void) {
    if (!mouse_state.initialized || !mouse_state.event_handler) {
        return;
    }

    for (int i = 0; i < mouse_state.num_devices; i++) {
        usb_mouse_device_t* mouse = &mouse_state.devices[i];
        uint16_t length = mouse->max_packet_size;

        if (usb_interrupt_transfer(mouse->dev, mouse->in_endpoint,
                                mouse->report_buffer, length, 0) == 0) {
            usb_mouse_report_t* report = mouse->report_buffer;
            usb_mouse_event_t event;

            event.left_button = report->buttons & USB_HID_MOUSE_BUTTON_LEFT;
            event.right_button = report->buttons & USB_HID_MOUSE_BUTTON_RIGHT;
            event.middle_button = report->buttons & USB_HID_MOUSE_BUTTON_MIDDLE;
            event.rel_x = report->x;
            event.rel_y = report->y;
            event.wheel_delta = mouse->has_wheel ? report->wheel : 0;
            event.pan_delta = mouse->has_pan ? report->pan : 0;

            mouse_state.event_handler(&event);
        }
    }
}