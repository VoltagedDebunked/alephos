// Base
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

// Extra
#include <utils/mem.h>
#include <utils/hcf.h>
#include <utils/io.h>
#include <utils/asm.h>
#include <core/attributes.h>

// Graphics
#include <graphics/fbcheck.h>
#include <graphics/display.h>
#include <graphics/fbinit.h>
#include <graphics/colors.h>

// Core
#include <core/gdt.h>
#include <core/idt.h>
#include <core/acpi.h>

// Memory, yes this is the part that REALLY fucked me over.
#include <mm/pmm.h>
#include <mm/vmm.h>

// Drivers
#include <core/drivers/ps2/keyboard.h>
#include <core/drivers/pic.h>
#include <core/drivers/pci.h>
#include <core/drivers/usb/usb.h>
#include <core/drivers/usb/xhci.h>
#include <core/drivers/usb/keyboard.h>
#include <core/drivers/ioapic.h>
#include <core/drivers/lapic.h>
#include <core/drivers/ip.h>

// Net
#include <net/net.h>

// Stack definitions
#define STACK_SIZE 16384 // 16 KB for each stack

// Stacks for different CPU modes and interrupts
static uint8_t kernel_stack[STACK_SIZE] __attribute__((aligned(16)));
static uint8_t ist1_stack[STACK_SIZE] __attribute__((aligned(16))); // Debug exceptions
static uint8_t ist2_stack[STACK_SIZE] __attribute__((aligned(16))); // NMI
static uint8_t ist3_stack[STACK_SIZE] __attribute__((aligned(16))); // Double Fault
static uint8_t ist4_stack[STACK_SIZE] __attribute__((aligned(16))); // Machine Check
static uint8_t ist5_stack[STACK_SIZE] __attribute__((aligned(16))); // Stack Fault
static uint8_t ist6_stack[STACK_SIZE] __attribute__((aligned(16))); // GPF
static uint8_t ist7_stack[STACK_SIZE] __attribute__((aligned(16))); // General interrupts

// Global framebuffer pointer for exception handler
struct limine_framebuffer* global_framebuffer;

// Function to get the top of a stack (stack grows downward on x86)
static inline uint64_t stack_top(uint8_t* stack) {
    return (uint64_t)(stack + STACK_SIZE);
}

// Exception handler that displays information
void general_exception_handler(struct interrupt_frame_error* frame) {
    uint8_t vector = frame->error_code >> 3;  // Get vector from error code
    uint64_t cr2;
    char buffer[256];

    // Get CR2 for page faults
    if (vector == INT_PAGE_FAULT) {
        asm volatile ("mov cr2, rax" : "=a"(cr2));
    }

    // Clear a portion of the screen
    draw_rect(global_framebuffer, 0, 140, global_framebuffer->width, 100, BLACK);

    // Display exception information
    draw_string(global_framebuffer, "EXCEPTION: ", 0, 140, RED);
    draw_string(global_framebuffer, get_exception_name(vector), 100, 140, RED);

    // Format and display register values
    draw_string(global_framebuffer, "RIP: ", 0, 160, WHITE);
    memset(buffer, 0, sizeof(buffer));
    // Simple hex to string conversion for RIP
    uint64_t rip = frame->rip;
    char hex_chars[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        buffer[i] = hex_chars[rip & 0xF];
        rip >>= 4;
    }
    draw_string(global_framebuffer, buffer, 50, 160, WHITE);

    // If it's a page fault, display CR2
    if (vector == INT_PAGE_FAULT) {
        draw_string(global_framebuffer, "CR2: ", 0, 180, WHITE);
        memset(buffer, 0, sizeof(buffer));
        for (int i = 15; i >= 0; i--) {
            buffer[i] = hex_chars[cr2 & 0xF];
            cr2 >>= 4;
        }
        draw_string(global_framebuffer, buffer, 50, 180, WHITE);
    }

    // Halt the system
    while (1) {
        hlt();
    }
}

void kmain(void) {
    // Disable interrupts until we're fully initialized
    cli();

    // Initialize framebuffer first for debug output
    fb_init();
    global_framebuffer = framebuffer_request.response->framebuffers[0];
    draw_string(global_framebuffer, "[ INFO ] Framebuffer Initialized.", 0, 0, WHITE);

    check_fb();
    draw_string(global_framebuffer, "[ STATUS ] Framebuffer Checked: Framebuffer state is good.", 0, 20, WHITE);

    // Initialize GDT with proper stacks
    gdt_init();

    // Set up TSS stacks
    gdt_load_tss(stack_top(kernel_stack)); // RSP0 - Kernel stack for privilege changes

    // Set up IST entries in TSS
    uint64_t* ist_ptr;
    ist_ptr = (uint64_t*)(stack_top(ist1_stack));
    *(ist_ptr - 1) = stack_top(ist1_stack);  // Debug exceptions
    ist_ptr = (uint64_t*)(stack_top(ist2_stack));
    *(ist_ptr - 1) = stack_top(ist2_stack);  // NMI
    ist_ptr = (uint64_t*)(stack_top(ist3_stack));
    *(ist_ptr - 1) = stack_top(ist3_stack);  // Double fault
    ist_ptr = (uint64_t*)(stack_top(ist4_stack));
    *(ist_ptr - 1) = stack_top(ist4_stack);  // Machine check
    ist_ptr = (uint64_t*)(stack_top(ist5_stack));
    *(ist_ptr - 1) = stack_top(ist5_stack);  // Stack fault
    ist_ptr = (uint64_t*)(stack_top(ist6_stack));
    *(ist_ptr - 1) = stack_top(ist6_stack);  // GPF
    ist_ptr = (uint64_t*)(stack_top(ist7_stack));
    *(ist_ptr - 1) = stack_top(ist7_stack);  // General interrupts

    draw_string(global_framebuffer, "[ INFO ] GDT and TSS Initialized.", 0, 40, WHITE);

    // Initialize IDT after GDT is set up
    idt_init();

    // Register exception handlers for all CPU exceptions
    for (int i = 0; i < 32; i++) {
        register_exception_handler(i, general_exception_handler);
    }

    draw_string(global_framebuffer, "[ INFO ] IDT Initialized.", 0, 60, WHITE);

    // Enable interrupts
    sti();

    draw_string(global_framebuffer, "[ INFO ] Interrupts Enabled.", 0, 80, WHITE);

    pmm_init(memmap_request.response);

    draw_string(global_framebuffer, "[ INFO ] PMM Initialized.", 0, 100, WHITE);

    vmm_init();

    draw_string(global_framebuffer, "[ INFO ] VMM Initialized.", 0, 120, WHITE);

    pic_init();

    draw_string(global_framebuffer, "[ INFO ] PIC Initialized.", 0, 140, WHITE);

    keyboard_init();

    draw_string(global_framebuffer, "[ INFO ] Keyboard Initialized.", 0, 160, WHITE);

    acpi_init();

    draw_string(global_framebuffer, "[ INFO ] ACPI Initialized.", 0, 180, WHITE);

    pci_init();

    draw_string(global_framebuffer, "[ INFO ] PCI Initialized.", 0, 200, WHITE);

    usb_keyboard_init();

    draw_string(global_framebuffer, "[ INFO ] USB Support Initialized.", 0, 220, WHITE);

    ioapic_init();

    draw_string(global_framebuffer, "[ INFO ] I/O APIC Initialized.", 0, 240, WHITE);

    lapic_init();
    lapic_enable();

    draw_string(global_framebuffer, "[ INFO ] LAPIC Initialized and Enabled.", 0, 260, WHITE);

    ip_init();

    draw_string(global_framebuffer, "[ INFO ] IP Driver Initialized.", 0, 280, WHITE);

    net_init();

    draw_string(global_framebuffer, "[ INFO ] Networking Initialized.", 0, 300, WHITE);

    draw_string(global_framebuffer, "[ INFO ] Kernel Loaded.", 0, 320, GREEN);

    // Main kernel loop
    while (1) {}

    // If this stupid little compiler fucks with this infrastructure
    // by "optimizing it" and making it reach here, im gonna crash out.
    hcf();
}