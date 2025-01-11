#include <core/drivers/ps2/keyboard.h>
#include <core/drivers/pic.h>
#include <core/idt.h>
#include <utils/io.h>
#include <graphics/display.h>

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64

static const char scancode_to_ascii[] = {
    0,  0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

extern struct limine_framebuffer* global_framebuffer;
static uint32_t x = 0;
static uint32_t y = 200;  // Start below kernel messages

static void keyboard_handler(struct interrupt_frame* frame) {
    (void)frame;  // Unused parameter

    uint8_t scancode = inb(PS2_DATA_PORT);

    // Handle printable characters only
    if (scancode < sizeof(scancode_to_ascii) && scancode_to_ascii[scancode]) {
        char c = scancode_to_ascii[scancode];
        draw_char(global_framebuffer, c, x, y, 0xFFFFFF);
        x += 12;

        // Basic line wrapping
        if (x >= global_framebuffer->width - 12) {
            x = 0;
            y += 16;
        }
    }

    pic_send_eoi(IRQ1);
}

void keyboard_init(void) {
    register_interrupt_handler(IRQ1, keyboard_handler);
    pic_clear_mask(1);
}