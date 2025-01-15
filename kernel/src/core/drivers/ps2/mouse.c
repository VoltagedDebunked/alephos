#include <utils/mem.h>
#include <core/drivers/ps2/mouse.h>
#include <core/drivers/pic.h>
#include <core/idt.h>
#include <utils/io.h>
#include <mm/heap.h>

// PS/2 Mouse I/O Ports
#define MOUSE_DATA_PORT     0x60
#define MOUSE_STATUS_PORT   0x64
#define MOUSE_COMMAND_PORT  0x64

// Mouse commands
#define MOUSE_CMD_RESET            0xFF
#define MOUSE_CMD_RESEND           0xFE
#define MOUSE_CMD_SET_DEFAULTS     0xF6
#define MOUSE_CMD_DISABLE_STREAM   0xF5
#define MOUSE_CMD_ENABLE_STREAM    0xF4
#define MOUSE_CMD_SET_SAMPLE_RATE  0xF3
#define MOUSE_CMD_SET_RESOLUTION   0xE8
#define MOUSE_CMD_GET_DEVICE_ID    0xF2

// Controller commands
#define CONTROLLER_CMD_WRITE_MOUSE 0xD4
#define CONTROLLER_CMD_ENABLE_AUX  0xA8
#define CONTROLLER_CMD_DISABLE_AUX 0xA7

// Mouse state tracking
typedef struct {
    uint8_t cycle;
    uint8_t packet_bytes[4];
    bool initialized;
    mouse_event_handler_t event_handler;
} mouse_state_t;

static mouse_state_t mouse_state = {0};

// Wait for controller to be ready
static void mouse_wait(bool is_write) {
    uint32_t timeout = 100000;
    if (is_write) {
        while (timeout-- && (inb(MOUSE_STATUS_PORT) & 0x2)) {
            __asm__ volatile("pause");
        }
    } else {
        while (timeout-- && !(inb(MOUSE_STATUS_PORT) & 0x1)) {
            __asm__ volatile("pause");
        }
    }
}

// Send command to mouse controller
static void mouse_send_command(uint8_t cmd) {
    mouse_wait(true);
    outb(MOUSE_COMMAND_PORT, CONTROLLER_CMD_WRITE_MOUSE);
    mouse_wait(true);
    outb(MOUSE_DATA_PORT, cmd);
}

// Send data to mouse
static void mouse_send_data(uint8_t data) {
    mouse_wait(true);
    outb(MOUSE_COMMAND_PORT, CONTROLLER_CMD_WRITE_MOUSE);
    mouse_wait(true);
    outb(MOUSE_DATA_PORT, data);
}

// Read data from mouse
static uint8_t mouse_read_data(void) {
    mouse_wait(false);
    return inb(MOUSE_DATA_PORT);
}

// Process mouse packet
static void process_mouse_packet(void) {
    // Validate packet
    if (mouse_state.cycle == 0 &&
        (mouse_state.packet_bytes[0] & 0x8) == 0) {
        return;  // Invalid first byte
    }

    // Construct packet
    mouse_packet_t packet;
    packet.button_state = mouse_state.packet_bytes[0];

    // Decode buttons
    packet.left_button = mouse_state.packet_bytes[0] & 0x1;
    packet.right_button = mouse_state.packet_bytes[0] & 0x2;
    packet.middle_button = mouse_state.packet_bytes[0] & 0x4;

    // X movement
    packet.x_sign = mouse_state.packet_bytes[0] & 0x10;
    packet.x_movement = (packet.x_sign) ?
        (mouse_state.packet_bytes[1] | 0xFFFFFF00) :
        mouse_state.packet_bytes[1];

    // Y movement (y is inverted in mouse coordinate system)
    packet.y_sign = mouse_state.packet_bytes[0] & 0x20;
    packet.y_movement = (packet.y_sign) ?
        -(mouse_state.packet_bytes[2] | 0xFFFFFF00) :
        -mouse_state.packet_bytes[2];

    // Overflow detection
    packet.overflow_x = mouse_state.packet_bytes[0] & 0x40;
    packet.overflow_y = mouse_state.packet_bytes[0] & 0x80;

    // Call event handler if registered
    if (mouse_state.event_handler) {
        mouse_state.event_handler(&packet);
    }
}

// Mouse interrupt handler
void mouse_interrupt_handler(struct interrupt_frame* frame) {
    (void)frame;  // Unused parameter

    uint8_t status = inb(MOUSE_STATUS_PORT);

    // Ensure this is a mouse interrupt
    if (!(status & 0x20)) {
        return;
    }

    // Read mouse data
    uint8_t data = inb(MOUSE_DATA_PORT);

    // Handle packet bytes
    mouse_state.packet_bytes[mouse_state.cycle++] = data;

    // Standard PS/2 mouse packet is 3 bytes
    if (mouse_state.cycle == 3) {
        process_mouse_packet();
        mouse_state.cycle = 0;
    }

    // Acknowledge the interrupt
    pic_send_eoi(IRQ12);
}

// Public functions
void mouse_init(void) {
    // Reset mouse state
    memset(&mouse_state, 0, sizeof(mouse_state));

    // Enable auxiliary device (mouse)
    mouse_wait(true);
    outb(MOUSE_COMMAND_PORT, CONTROLLER_CMD_ENABLE_AUX);

    // Reset mouse
    mouse_send_command(MOUSE_CMD_RESET);
    mouse_read_data();  // Acknowledge byte

    // Check if reset was successful
    uint8_t status = mouse_read_data();
    if (status != 0xAA) {
        // Mouse initialization failed
        return;
    }

    // Set default settings
    mouse_send_command(MOUSE_CMD_SET_DEFAULTS);
    mouse_read_data();  // Acknowledge byte

    // Set sample rate (100 samples/sec is a good default)
    mouse_set_sample_rate(100);

    // Set resolution to 4 counts per mm (standard)
    mouse_set_resolution(3);

    // Enable data reporting
    mouse_send_command(MOUSE_CMD_ENABLE_STREAM);
    mouse_read_data();  // Acknowledge byte

    // Register mouse interrupt handler
    register_interrupt_handler(IRQ12, mouse_interrupt_handler);

    // Unmask IRQ12
    pic_clear_mask(12);

    // Mark as initialized
    mouse_state.initialized = true;
}

void mouse_enable(void) {
    if (!mouse_state.initialized) {
        return;
    }
    mouse_send_command(MOUSE_CMD_ENABLE_STREAM);
}

void mouse_disable(void) {
    if (!mouse_state.initialized) {
        return;
    }
    mouse_send_command(MOUSE_CMD_DISABLE_STREAM);
}

bool mouse_read_packet(mouse_packet_t* packet) {
    if (!mouse_state.initialized || !packet) {
        return false;
    }

    // Packet is already processed in interrupt handler
    // This function is more for explicit synchronous reading
    return false;  // Synchronous reading not yet implemented
}

void mouse_set_sample_rate(uint8_t rate) {
    if (!mouse_state.initialized) {
        return;
    }
    mouse_send_command(MOUSE_CMD_SET_SAMPLE_RATE);
    mouse_send_data(rate);
}

void mouse_set_resolution(uint8_t resolution) {
    if (!mouse_state.initialized) {
        return;
    }
    mouse_send_command(MOUSE_CMD_SET_RESOLUTION);
    mouse_send_data(resolution);
}

void mouse_register_event_handler(mouse_event_handler_t handler) {
    mouse_state.event_handler = handler;
}