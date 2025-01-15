#ifndef PS2_MOUSE_H
#define PS2_MOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include <core/idt.h>

struct usb_device;
void usb_mouse_init(void);

// Mouse packet structure
typedef struct {
    int8_t x_movement;
    int8_t y_movement;
    uint8_t button_state;
    bool left_button;
    bool right_button;
    bool middle_button;
    bool x_sign;
    bool y_sign;
    bool overflow_x;
    bool overflow_y;
} mouse_packet_t;

// Mouse initialization and configuration functions
void mouse_init(void);
void mouse_enable(void);
void mouse_disable(void);

// Mouse packet reading and processing
bool mouse_read_packet(mouse_packet_t* packet);
void mouse_set_sample_rate(uint8_t rate);
void mouse_set_resolution(uint8_t resolution);

// Mouse event handling
typedef void (*mouse_event_handler_t)(mouse_packet_t* packet);
void mouse_register_event_handler(mouse_event_handler_t handler);

// Interrupt handler (internal use)
void mouse_interrupt_handler(struct interrupt_frame* frame);

#endif // PS2_MOUSE_H