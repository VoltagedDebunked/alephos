#include <core/drivers/keyboard.h>
#include <utils/io.h>
#include <core/idt.h>
#include <graphics/display.h>
#include <graphics/colors.h>

static struct limine_framebuffer *framebuffer;
static volatile bool keyboard_initialized = false;

static const char scancode_to_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const char scancode_to_ascii_shift[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

static bool keyboard_wait_input(void) {
    uint16_t timeout = 1000;
    while (timeout-- && (inb(KB_STATUS_PORT) & KB_STATUS_INPUT_FULL));
    return timeout > 0;
}

static bool keyboard_wait_output(void) {
    uint16_t timeout = 1000;
    while (timeout-- && !(inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL));
    return timeout > 0;
}

static bool keyboard_send_command(uint8_t cmd, uint8_t data) {
    keyboard_wait_input();
    outb(KB_COMMAND_PORT, cmd);
    
    keyboard_wait_input();
    outb(KB_DATA_PORT, data);
    
    return keyboard_wait_output();
}

static bool keyboard_self_test(void) {
    keyboard_wait_input();
    outb(KB_COMMAND_PORT, KB_CMD_SELF_TEST);
    
    keyboard_wait_output();
    return inb(KB_DATA_PORT) == 0x55;
}

uint8_t keyboard_read_scancode(void) {
    keyboard_wait_output();
    return inb(KB_DATA_PORT);
}

bool init_keyboard(struct limine_framebuffer *fb) {
    framebuffer = fb;

    outb(KB_COMMAND_PORT, KB_CMD_DISABLE);
    
    while (inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT_FULL) {
        inb(KB_DATA_PORT);
    }
    
    if (!keyboard_self_test()) {
        return false;
    }
    
    keyboard_send_command(KB_CMD_WRITE_CONFIG, KB_CONFIG_INT | KB_CONFIG_TRANSLATE);
    
    outb(KB_COMMAND_PORT, KB_CMD_ENABLE);
    
    register_interrupt_handler(KB_IRQ, keyboard_handler);
    
    uint8_t mask = inb(0x21);
    outb(0x21, mask & ~(1 << KB_IRQ));
    
    keyboard_initialized = true;
    
    return true;
}

void keyboard_handler(void) {
    if (!keyboard_initialized) return;

    uint8_t scancode = keyboard_read_scancode();
    static char keybuffer[128] = {0};
    static uint8_t buffer_pos = 0;
    static bool shift = false;
    static bool caps = false;
    static bool ctrl = false;
    static bool alt = false;
    static bool num = false;
    static bool scroll = false;
    
    switch(scancode) {
        case 0x2A: case 0x36: shift = true; break;
        case 0xAA: case 0xB6: shift = false; break;
        case 0x1D: ctrl = true; break;
        case 0x9D: ctrl = false; break;
        case 0x38: alt = true; break;
        case 0x80 | 0x38: alt = false; break;
        case 0x3A: caps = !caps; break;
        case 0x45: num = !num; break;
        case 0x46: scroll = !scroll; break;
        default:
            if (!(scancode & 0x80)) {
                char c = 0;
                if (scancode < 58) {
                    if (shift || caps) {
                        c = scancode_to_ascii_shift[scancode];
                    } else {
                        c = scancode_to_ascii[scancode];
                    }
                }
                
                if (ctrl && c) {
                    if (c == 'l' || c == 'L') {
                        clear_screen(framebuffer);
                        buffer_pos = 0;
                        keybuffer[0] = 0;
                    }
                } else if (c) {
                    if (c >= 32 && c < 127) {
                        if (buffer_pos < sizeof(keybuffer)-1) {
                            keybuffer[buffer_pos++] = c;
                            keybuffer[buffer_pos] = 0;
                            char str[2] = {c, 0};
                            draw_string(framebuffer, str, 12 * (buffer_pos-1), 140, WHITE);
                        }
                    } else if (c == '\b' && buffer_pos > 0) {
                        buffer_pos--;
                        keybuffer[buffer_pos] = 0;
                        draw_rect(framebuffer, 12 * buffer_pos, 140, 12, 16, BLACK);
                    } else if (c == '\n') {
                        buffer_pos = 0;
                        keybuffer[0] = 0;
                    }
                }
            }
    }
    
    outb(0x20, 0x20);
}