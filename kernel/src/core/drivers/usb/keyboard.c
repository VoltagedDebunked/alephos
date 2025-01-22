#include <core/drivers/usb/keyboard.h>
#include <core/drivers/pci.h>
#include <core/drivers/usb/xhci.h>
#include <core/drivers/usb/usb.h>
#include <mm/heap.h>
#include <utils/mem.h>
#include <core/idt.h>

// Maximum number of supported USB keyboards
#define MAX_USB_KEYBOARDS 4
#define KEYBOARD_BUFFER_SIZE 128

// Keyboard state structure
typedef struct {
    struct usb_device* dev;
    uint8_t interface;
    uint8_t in_endpoint;
    uint16_t max_packet_size;
    void* report_buffer;
    bool initialized;
    keyboard_event_handler_t event_handler;
} usb_keyboard_device_t;

// Global keyboard state
static struct {
    usb_keyboard_device_t devices[MAX_USB_KEYBOARDS];
    uint8_t num_devices;
    bool initialized;
    char key_buffer[KEYBOARD_BUFFER_SIZE];
    uint32_t buffer_head;
    uint32_t buffer_tail;
} keyboard_state;

// US QWERTY keymap - unshifted
static const char keymap_us[128] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\n', 0, '\b', '\t',
    ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// US QWERTY keymap - shifted
static const char keymap_us_shift[128] = {
    0, 0, 0, 0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '\n', 0, '\b', '\t',
    ' ', '_', '+', '{', '}', '|', 0, ':', '"', '~', '<', '>', '?', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Initialize USB keyboard support
void usb_keyboard_init(void) {
    if (keyboard_state.initialized) {
        return;
    }

    memset(&keyboard_state, 0, sizeof(keyboard_state));
    keyboard_state.initialized = true;
}

// Cleanup USB keyboard resources
void usb_keyboard_cleanup(void) {
    for (uint8_t i = 0; i < keyboard_state.num_devices; i++) {
        if (keyboard_state.devices[i].report_buffer) {
            free(keyboard_state.devices[i].report_buffer);
        }
    }
    keyboard_state.initialized = false;
    keyboard_state.num_devices = 0;
}

// Check if device is a USB keyboard
bool usb_keyboard_probe(struct usb_device* dev) {
    if (!dev || dev->descriptor.device_class != USB_CLASS_HID ||
        dev->config == NULL) {
        return false;
    }

    // Look for keyboard interface
    struct usb_interface_descriptor* intf = NULL;
    uint8_t* ptr = (uint8_t*)dev->config;
    uint16_t len = dev->config->total_length;

    for (uint16_t i = 0; i < len;) {
        struct usb_interface_descriptor* desc = (struct usb_interface_descriptor*)(ptr + i);

        if (desc->type == USB_DT_INTERFACE &&
            desc->interface_class == USB_CLASS_HID &&
            desc->interface_protocol == 1) {  // Keyboard protocol
            return true;
        }

        i += desc->length;
    }

    return false;
}

// Convert USB HID key code to ASCII character
static char convert_keycode(uint8_t keycode, bool shift) {
    if (keycode >= sizeof(keymap_us)) {
        return 0;
    }
    return shift ? keymap_us_shift[keycode] : keymap_us[keycode];
}

// Process keyboard report
static void process_keyboard_report(usb_keyboard_device_t* kbd, usb_keyboard_report_t* report) {
    static uint8_t prev_keys[6] = {0};
    bool shift = report->modifiers & (USB_HID_MOD_LEFT_SHIFT | USB_HID_MOD_RIGHT_SHIFT);

    // Check for pressed keys
    for (int i = 0; i < 6; i++) {
        if (report->keys[i] != 0) {
            bool was_pressed = false;
            for (int j = 0; j < 6; j++) {
                if (report->keys[i] == prev_keys[j]) {
                    was_pressed = true;
                    break;
                }
            }

            if (!was_pressed) {
                // New key press
                char c = convert_keycode(report->keys[i], shift);
                if (c != 0) {
                    // Add to key buffer if space available
                    uint32_t next_tail = (keyboard_state.buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
                    if (next_tail != keyboard_state.buffer_head) {
                        keyboard_state.key_buffer[keyboard_state.buffer_tail] = c;
                        keyboard_state.buffer_tail = next_tail;
                    }
                }

                // Call event handler if registered
                if (kbd->event_handler) {
                    kbd->event_handler(report->keys[i], true);
                }
            }
        }
    }

    // Check for released keys
    for (int i = 0; i < 6; i++) {
        if (prev_keys[i] != 0) {
            bool still_pressed = false;
            for (int j = 0; j < 6; j++) {
                if (prev_keys[i] == report->keys[j]) {
                    still_pressed = true;
                    break;
                }
            }

            if (!still_pressed && kbd->event_handler) {
                kbd->event_handler(prev_keys[i], false);
            }
        }
    }

    // Update previous keys state
    memcpy(prev_keys, report->keys, 6);
}

// Attach USB keyboard device
bool usb_keyboard_attach(struct usb_device* dev) {
    if (!keyboard_state.initialized || keyboard_state.num_devices >= MAX_USB_KEYBOARDS ||
        !usb_keyboard_probe(dev)) {
        return false;
    }

    usb_keyboard_device_t* kbd = &keyboard_state.devices[keyboard_state.num_devices];
    memset(kbd, 0, sizeof(usb_keyboard_device_t));

    kbd->dev = dev;

    // Find HID interface and endpoints
    uint8_t* ptr = (uint8_t*)dev->config;
    uint16_t len = dev->config->total_length;

    for (uint16_t i = 0; i < len;) {
        uint8_t desc_type = ptr[i + 1];
        uint8_t desc_len = ptr[i];

        if (desc_type == USB_DT_INTERFACE) {
            struct usb_interface_descriptor* intf = (struct usb_interface_descriptor*)(ptr + i);
            if (intf->interface_class == USB_CLASS_HID &&
                intf->interface_protocol == 1) {
                kbd->interface = intf->interface_number;
            }
        } else if (desc_type == USB_DT_ENDPOINT) {
            struct usb_endpoint_descriptor* ep = (struct usb_endpoint_descriptor*)(ptr + i);
            if ((ep->endpoint_address & 0x80) &&  // IN endpoint
                (ep->attributes & 0x03) == 3) {   // Interrupt transfer
                kbd->in_endpoint = ep->endpoint_address;
                kbd->max_packet_size = ep->max_packet_size;
            }
        }

        i += desc_len;
    }

    // Allocate report buffer
    kbd->report_buffer = malloc(kbd->max_packet_size);
    if (!kbd->report_buffer) {
        return false;
    }

    kbd->initialized = true;
    keyboard_state.num_devices++;

    return true;
}

// Detach USB keyboard device
void usb_keyboard_detach(struct usb_device* dev) {
    for (uint8_t i = 0; i < keyboard_state.num_devices; i++) {
        if (keyboard_state.devices[i].dev == dev) {
            if (keyboard_state.devices[i].report_buffer) {
                free(keyboard_state.devices[i].report_buffer);
            }

            // Shift remaining devices
            if (i < keyboard_state.num_devices - 1) {
                memmove(&keyboard_state.devices[i],
                       &keyboard_state.devices[i + 1],
                       sizeof(usb_keyboard_device_t) * (keyboard_state.num_devices - i - 1));
            }

            keyboard_state.num_devices--;
            break;
        }
    }
}

// Get character from keyboard buffer
char usb_keyboard_get_char(void) {
    if (keyboard_state.buffer_head == keyboard_state.buffer_tail) {
        return 0;  // No character available
    }

    char c = keyboard_state.key_buffer[keyboard_state.buffer_head];
    keyboard_state.buffer_head = (keyboard_state.buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

// Register keyboard event handler
void usb_keyboard_register_handler(keyboard_event_handler_t handler) {
    // Register handler for all attached keyboards
    for (uint8_t i = 0; i < keyboard_state.num_devices; i++) {
        keyboard_state.devices[i].event_handler = handler;
    }
}