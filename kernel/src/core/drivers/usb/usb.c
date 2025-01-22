#include <core/drivers/usb/usb.h>
#include <core/drivers/usb/xhci.h>
#include <utils/mem.h>
#include <utils/log.h>

static struct xhci_controller* xhci_ctrl = NULL;

// Internal structures

// USB Hub Structure
struct usb_hub {
    struct usb_device* dev;
    uint32_t port_count;
    uint32_t characteristics;
    uint32_t* port_status;
    struct usb_hub* next;
};

// USB Setup Packet
struct usb_setup_packet {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

// Global USB state
static struct {
    bool initialized;
    struct usb_device* devices;
    struct usb_hub* hubs;
    uint8_t next_address;
} usb_state;

// Initialize USB subsystem
void usb_init(void) {
    if (usb_state.initialized) return;

    xhci_ctrl = xhci_init();

    // Initialize state
    memset(&usb_state, 0, sizeof(usb_state));
    usb_state.next_address = 1;

    // Initialize xHCI controller
    xhci_init();
    if (!xhci_probe()) {
        log_error("Failed to initialize xHCI controller");
        return;
    }

    // Start xHCI controller
    xhci_start(xhci_ctrl);
    if (!usb_state.initialized) {
        log_error("Failed to start xHCI controller");
        return;
    }

    usb_state.initialized = true;
    log_info("USB subsystem initialized");
}

// Allocate new USB device
static struct usb_device* usb_alloc_device(void) {
    struct usb_device* dev = malloc(sizeof(struct usb_device));
    if (!dev) return NULL;

    memset(dev, 0, sizeof(struct usb_device));
    dev->address = usb_state.next_address++;
    return dev;
}

// Send control transfer to USB device
int usb_control_transfer(struct usb_device* dev, uint8_t request_type, uint8_t request,
                        uint16_t value, uint16_t index, void* data, uint16_t length) {
    struct usb_setup_packet setup = {
        .bmRequestType = request_type,
        .bRequest = request,
        .wValue = value,
        .wIndex = index,
        .wLength = length
    };

    // Execute transfer through xHCI controller
    // For now, simulate success since we don't have full xHCI transfer support yet
    return 0;
}

// Send bulk transfer to USB device
int usb_bulk_transfer(struct usb_device* dev, uint8_t endpoint,
                     void* data, uint16_t length, uint32_t timeout) {
    // For now, simulate success since we don't have full xHCI transfer support yet
    return 0;
}

// Send interrupt transfer to USB device
int usb_interrupt_transfer(struct usb_device* dev, uint8_t endpoint,
                          void* data, uint16_t length, uint32_t timeout) {
    // For now, simulate success since we don't have full xHCI transfer support yet
    return 0;
}

// Get device descriptor
static bool usb_get_device_descriptor(struct usb_device* dev) {
    int ret = usb_control_transfer(dev, 0x80, USB_REQ_GET_DESCRIPTOR,
                                 (USB_DT_DEVICE << 8), 0,
                                 &dev->descriptor, sizeof(struct usb_device_descriptor));
    return ret >= 0;
}

// Get configuration descriptor
static bool usb_get_config_descriptor(struct usb_device* dev) {
    // First get just the configuration descriptor to determine total size
    struct usb_config_descriptor config;
    int ret = usb_control_transfer(dev, 0x80, USB_REQ_GET_DESCRIPTOR,
                                 (USB_DT_CONFIG << 8), 0,
                                 &config, sizeof(config));
    if (ret < 0) return false;

    // Allocate space for full configuration
    dev->config = malloc(config.total_length);
    if (!dev->config) return false;

    // Get full configuration descriptor
    ret = usb_control_transfer(dev, 0x80, USB_REQ_GET_DESCRIPTOR,
                             (USB_DT_CONFIG << 8), 0,
                             dev->config, config.total_length);
    return ret >= 0;
}

// Set device address
static bool usb_set_address(struct usb_device* dev) {
    int ret = usb_control_transfer(dev, 0x00, USB_REQ_SET_ADDRESS,
                                 dev->address, 0, NULL, 0);
    return ret >= 0;
}

// Set device configuration
static bool usb_set_configuration(struct usb_device* dev, uint8_t config) {
    int ret = usb_control_transfer(dev, 0x00, USB_REQ_SET_CONFIG,
                                 config, 0, NULL, 0);
    if (ret >= 0) {
        dev->configured = true;
    }
    return ret >= 0;
}

// Initialize USB hub
static bool usb_init_hub(struct usb_hub* hub) {
    // Get hub descriptor
    struct {
        uint8_t length;
        uint8_t type;
        uint8_t port_count;
        uint16_t characteristics;
        uint8_t power_time;
        uint8_t current;
    } __attribute__((packed)) hub_desc;

    int ret = usb_control_transfer(hub->dev, 0xA0, USB_REQ_GET_DESCRIPTOR,
                                 (USB_DT_HUB << 8), 0,
                                 &hub_desc, sizeof(hub_desc));
    if (ret < 0) return false;

    // Allocate port status array
    hub->port_count = hub_desc.port_count;
    hub->characteristics = hub_desc.characteristics;
    hub->port_status = malloc(sizeof(uint32_t) * hub->port_count);
    if (!hub->port_status) return false;

    // Power on all ports
    for (uint8_t port = 1; port <= hub->port_count; port++) {
        usb_control_transfer(hub->dev, 0x23, USB_REQ_SET_FEATURE,
                           1, port, NULL, 0);  // SetPortFeature(PORT_POWER)
    }

    return true;
}

// Handle newly connected device
static bool usb_enumerate_device(struct usb_device* dev) {
    // Get device descriptor
    if (!usb_get_device_descriptor(dev)) {
        log_error("Failed to get device descriptor");
        return false;
    }

    // Set device address
    if (!usb_set_address(dev)) {
        log_error("Failed to set device address");
        return false;
    }

    // Get configuration descriptor
    if (!usb_get_config_descriptor(dev)) {
        log_error("Failed to get configuration descriptor");
        return false;
    }

    // Configure device
    if (!usb_set_configuration(dev, 1)) {
        log_error("Failed to configure device");
        return false;
    }

    // Add to device list
    dev->next = usb_state.devices;
    usb_state.devices = dev;

    log_info("USB device enumerated: VID=%04x PID=%04x",
             dev->descriptor.vendor_id,
             dev->descriptor.product_id);

    // If it's a hub, initialize it
    if (dev->descriptor.device_class == USB_CLASS_HUB) {
        struct usb_hub* hub = malloc(sizeof(struct usb_hub));
        if (hub) {
            hub->dev = dev;
            if (usb_init_hub(hub)) {
                hub->next = usb_state.hubs;
                usb_state.hubs = hub;
            } else {
                free(hub);
            }
        }
    }

    return true;
}

// Handle hub port status change
static void usb_handle_port_change(struct usb_hub* hub, uint8_t port) {
    uint32_t status = hub->port_status[port-1];

    // Device connected
    if (status & 0x1) {  // Port connection status
        // Reset port
        usb_control_transfer(hub->dev, 0x23, USB_REQ_SET_FEATURE,
                           4, port, NULL, 0);  // SetPortFeature(PORT_RESET)

        // Wait for reset completion and get port status
        for (int i = 0; i < 10; i++) {
            int ret = usb_control_transfer(hub->dev, 0xA3, USB_REQ_GET_STATUS,
                                       0, port, &status, 4);
            if (ret >= 0 && !(status & 0x10)) break;  // PORT_RESET cleared
        }

        // Create new device
        struct usb_device* dev = usb_alloc_device();
        if (!dev) return;

        dev->hub_addr = hub->dev->address;
        dev->port = port;

        // Set device speed based on port status
        if (status & 0x200)      dev->speed = USB_SPEED_HIGH;  // PORT_HIGH_SPEED
        else if (status & 0x400) dev->speed = USB_SPEED_LOW;   // PORT_LOW_SPEED
        else                     dev->speed = USB_SPEED_FULL;   // Full speed by default

        // Set initial max packet size based on speed
        switch (dev->speed) {
            case USB_SPEED_LOW:  dev->max_packet_size = 8;  break;
            case USB_SPEED_HIGH: dev->max_packet_size = 64; break;
            default:            dev->max_packet_size = 8;  break;
        }

        // Enable port
        usb_control_transfer(hub->dev, 0x23, USB_REQ_SET_FEATURE,
                           8, port, NULL, 0);  // SetPortFeature(PORT_ENABLE)

        // Enumerate device
        if (!usb_enumerate_device(dev)) {
            free(dev);
        }
    }
    // Device disconnected
    else {
        // Find and remove device with matching hub address and port
        struct usb_device** dev_ptr = &usb_state.devices;
        while (*dev_ptr) {
            struct usb_device* dev = *dev_ptr;
            if (dev->hub_addr == hub->dev->address && dev->port == port) {
                *dev_ptr = dev->next;

                // If it's a hub, remove it from hub list
                if (dev->descriptor.device_class == USB_CLASS_HUB) {
                    struct usb_hub** hub_ptr = &usb_state.hubs;
                    while (*hub_ptr) {
                        if ((*hub_ptr)->dev == dev) {
                            struct usb_hub* removed_hub = *hub_ptr;
                            *hub_ptr = removed_hub->next;
                            if (removed_hub->port_status) {
                                free(removed_hub->port_status);
                            }
                            free(removed_hub);
                            break;
                        }
                        hub_ptr = &(*hub_ptr)->next;
                    }
                }

                // Free device
                if (dev->config) free(dev->config);
                free(dev);
                break;
            }
            dev_ptr = &(*dev_ptr)->next;
        }
    }
}

// Scan for new devices and handle events
void usb_process_events(void) {
    if (!usb_state.initialized) return;

    // Get root hub status from xHCI controller
    // For now, simulate no port changes since we don't have full xHCI port status support yet
    uint32_t ports = 4; // Assume 4 root ports
    uint32_t* port_change = malloc(sizeof(uint32_t) * ports);

    if (port_change) {
        memset(port_change, 0, sizeof(uint32_t) * ports);
        // Later we'll implement actual port status checking through xHCI
            // Handle root hub port changes
            for (uint32_t i = 0; i < ports; i++) {
                if (port_change[i]) {
                    // Create temporary hub structure for root hub
                    struct usb_hub root_hub = {
                        .dev = NULL,  // Root hub has no device
                        .port_count = ports,
                        .port_status = port_change
                    };
                    usb_handle_port_change(&root_hub, i + 1);
                }
            }
        }
        free(port_change);

    // Check other hubs
    struct usb_hub* hub = usb_state.hubs;
    while (hub) {
        // Get hub port status
        for (uint8_t port = 1; port <= hub->port_count; port++) {
            int ret = usb_control_transfer(hub->dev, 0xA3, USB_REQ_GET_STATUS,
                                       0, port, &hub->port_status[port-1], 4);

            if (ret >= 0 && (hub->port_status[port-1] & 0xFFFF0000)) {  // Status change bits
                usb_handle_port_change(hub, port);

                // Clear status change bits
                uint32_t changes = hub->port_status[port-1] >> 16;
                for (int bit = 0; bit < 16; bit++) {
                    if (changes & (1 << bit)) {
                        usb_control_transfer(hub->dev, 0x23, USB_REQ_CLEAR_FEATURE,
                                         (bit << 16), port, NULL, 0);
                    }
                }
            }
        }
        hub = hub->next;
    }
}

// Get device by address
struct usb_device* usb_get_device(uint8_t address) {
    struct usb_device* dev = usb_state.devices;
    while (dev) {
        if (dev->address == address) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

// Clean up USB subsystem
void usb_cleanup(void) {
    if (!usb_state.initialized) return;

    // Free all devices
    struct usb_device* dev = usb_state.devices;
    while (dev) {
        struct usb_device* next = dev->next;
        if (dev->config) free(dev->config);
        if (dev->driver_data) free(dev->driver_data);
        free(dev);
        dev = next;
    }

    // Free all hubs
    struct usb_hub* hub = usb_state.hubs;
    while (hub) {
        struct usb_hub* next = hub->next;
        if (hub->port_status) free(hub->port_status);
        free(hub);
        hub = next;
    }

    // Stop xHCI controller
    xhci_stop(xhci_ctrl);

    memset(&usb_state, 0, sizeof(usb_state));
}