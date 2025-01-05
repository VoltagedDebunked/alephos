// Base

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

// Extra

#include <utils/mem.h>
#include <utils/hcf.h>
#include <core/attributes.h>

// Graphics

#include <graphics/fbcheck.h>
#include <graphics/display.h>
#include <graphics/fbinit.h>
#include <graphics/colors.h>

// Core

#include <core/gdt.h>
#include <core/idt.h>

// Drivers

#include <core/drivers/keyboard.h>

void kmain(void) {
    
    fb_init();

    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    draw_string(framebuffer, "[ INFO ] Framebuffer Initialized.", 0, 0, WHITE);

    check_fb();

    draw_string(framebuffer, "[ STATUS ] Framebuffer Checked: Framebuffer state is good.", 0, 20, WHITE);

    gdt_init();

    draw_string(framebuffer, "[ INFO ] GDT Initialized.", 0, 40, WHITE);

    idt_init();

    draw_string(framebuffer, "[ INFO ] IDT Initialized.", 0, 60, WHITE);

    init_keyboard(framebuffer);

    draw_string(framebuffer, "[ INFO ] Keyboard Initialized.", 0, 80, WHITE);

    draw_string(framebuffer, "[ INFO ] Kernel Loaded.", 0, 100, GREEN);

    while (1) {
        keyboard_handler();
    }

    hcf();
}
