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
#include <core/pit.h>
#include <core/smp.h>
#include <core/process.h>

// Memory
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>

// Drivers
#include <core/drivers/ps2/keyboard.h>
#include <core/drivers/pic.h>
#include <core/drivers/pci.h>
#include <core/drivers/usb/usb.h>
#include <core/drivers/usb/xhci.h>
#include <core/drivers/usb/keyboard.h>
#include <core/drivers/ioapic.h>
#include <core/drivers/lapic.h>
#include <core/drivers/net/ip.h>
#include <core/drivers/net/e1000.h>
#include <core/drivers/net/netdev.h>
#include <core/drivers/storage/nvme.h>
#include <core/drivers/serial/serial.h>
#include <core/drivers/ps2/mouse.h>
#include <core/drivers/usb/mouse.h>

// Net
#include <net/net.h>
#include <net/http/http.h>

// Filesystem
#include <fs/ext2.h>

// Global framebuffer pointer for exception handler
struct limine_framebuffer* global_framebuffer;

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
    check_fb();

    // Initialize memory management first
    pmm_init(memmap_request.response); // Initialize Physical Memory Manager (PMM)
    vmm_init(); // Initialize Virtual Memory Manager (VMM)
    heap_init(); // Initialize the heap for dynamic memory allocation

    // Initialize GDT with proper stacks
    gdt_init();

    // Set up TSS stacks
    gdt_load_tss(stack_top(kernel_stack)); // RSP0 - Kernel stack for privilege changes

    // Set up IST entries in TSS for exception handling
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

    // Initialize IDT after GDT and TSS are set up
    idt_init();

    // Register exception handlers for all CPU exceptions
    for (int i = 0; i < 32; i++) {
        register_exception_handler(i, general_exception_handler);
    }

    // Enable interrupts
    sti();

    // Initialize peripheral devices (PIC, IOAPIC, LAPIC, USB, Keyboard, Mouse, etc.)
    pic_init();
    ioapic_init();
    lapic_init();
    lapic_enable();
    usb_init();
    keyboard_init();
    mouse_init();
    mouse_enable();
    usb_mouse_init();
    usb_keyboard_init();

    // Initialize ACPI (Advanced Configuration and Power Interface)
    acpi_init();

    // Initialize PCI (Peripheral Component Interconnect)
    pci_init();

    // Networking Initialization (IP stack, network devices, etc.)
    ip_init();
    netdev_init();
    struct netdev* net = netdev_get_default();
    net_init();

    // Initialize NVMe (Non-Volatile Memory Express)
    nvme_init();

    // Initialize HTTP support
    http_init();

    // Initialize filesystems (EXT2, etc.)
    ext2_init(0);

    // Initialize serial communication (COM1)
    serial_init(COM1);

    // Initialize process management and the scheduler
    process_init();
    scheduler_init();

    // Initialize the Programmable Interval Timer (PIT)
    pit_init();

    // Initialize SMP (Symmetric Multi-Processing)
    smp_init();
    smp_boot_aps();

    // Draw a welcome message to the framebuffer
    draw_string(global_framebuffer, "Welcome to AlephOS!", 0, 0, WHITE);

    // Main kernel loop
    while (1) {}

    // Halt the CPU if the kernel ever reaches here
    hcf();
}
