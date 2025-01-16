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
#include <utils/log.h>  // Added log header

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
#include <core/drivers/tty/tty.h>
#include <core/drivers/tty/pipe.h>
#include <core/drivers/rtc/rtc.h>

// Net
#include <net/net.h>
#include <net/http/http.h>
#include <net/http/https.h>
#include <net/dns.h>
#include <net/wifi.h>

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
    log_error("EXCEPTION: %s", get_exception_name(vector));

    // Log register values
    log_error("RIP: 0x%x", frame->rip);

    // If it's a page fault, log CR2
    if (vector == INT_PAGE_FAULT) {
        log_error("CR2: 0x%x", cr2);
    }

    // Halt the system
    while (1) {
        hlt();
    }
}

void kmain(void) {
    // Initialize logging first
    log_init();
    log_info("Kernel Initialization Started");

    // Disable interrupts until we're fully initialized
    cli();
    log_debug("Interrupts Disabled");

    // Initialize framebuffer first for debug output
    fb_init();
    log_debug("Framebuffer Initialized");
    global_framebuffer = framebuffer_request.response->framebuffers[0];
    check_fb();
    log_debug("Framebuffer Check Complete");

    // Initialize memory management first
    log_info("Initializing Physical Memory Manager");
    pmm_init(memmap_request.response);
    log_info("Physical Memory Manager Initialized");

    log_info("Initializing Virtual Memory Manager");
    vmm_init();
    log_info("Virtual Memory Manager Initialized");

    log_info("Initializing Heap");
    heap_init();
    log_info("Heap Initialized");

    // Initialize GDT with proper stacks
    log_info("Initializing Global Descriptor Table");
    gdt_init();
    log_info("Global Descriptor Table Initialized");

    // Set up TSS stacks
    log_debug("Setting up TSS Stacks");
    gdt_load_tss(stack_top(kernel_stack)); // RSP0 - Kernel stack for privilege changes

    // Set up IST entries in TSS for exception handling
    log_debug("Setting up Interrupt Stack Table Entries");
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
    log_info("Initializing Interrupt Descriptor Table");
    idt_init();
    log_info("Interrupt Descriptor Table Initialized");

    // Register exception handlers for all CPU exceptions
    log_debug("Registering Exception Handlers");
    for (int i = 0; i < 32; i++) {
        register_exception_handler(i, general_exception_handler);
    }

    // Enable interrupts
    log_debug("Enabling Interrupts");
    sti();

    // Initialize peripheral devices
    log_info("Initializing PIC");
    pic_init();
    log_info("PIC Initialized");

    log_info("Initializing IOAPIC");
    ioapic_init();
    log_info("IOAPIC Initialized");

    log_info("Initializing Local APIC");
    lapic_init();
    lapic_enable();
    log_info("Local APIC Initialized");

    log_info("Initializing USB");
    usb_init();
    log_info("USB Initialized");

    log_info("Initializing Keyboard");
    keyboard_init();
    log_info("Keyboard Initialized");

    log_info("Initializing PS/2 Mouse");
    mouse_init();
    mouse_enable();
    log_info("PS/2 Mouse Initialized");

    log_info("Initializing USB Mouse");
    usb_mouse_init();
    log_info("USB Mouse Initialized");

    log_info("Initializing USB Keyboard");
    usb_keyboard_init();
    log_info("USB Keyboard Initialized");

    log_info("Initializing ACPI");
    acpi_init();
    log_info("ACPI Initialized");

    log_info("Initializing PCI");
    pci_init();
    log_info("PCI Initialized");

    // Networking Initialization
    log_info("Initializing IP Stack");
    ip_init();
    log_info("IP Stack Initialized");

    log_info("Initializing Network Devices");
    netdev_init();
    struct netdev* net = netdev_get_default();
    log_info("Network Devices Initialized");

    log_info("Initializing Network Stack");
    net_init();
    log_info("Network Stack Initialized");

    log_info("Initializing NVMe");
    nvme_init();
    log_info("NVMe Initialized");

    log_info("Initializing HTTP Support");
    http_init();
    log_info("HTTP Initialized");

    log_info("Initializing HTTPS");
    https_init();
    log_info("HTTPS Initialized");

    log_info("Initializing TLS");
    tls_init();
    log_info("TLS Initialized");

    log_info("Initializing DNS");
    dns_init();
    log_info("DNS Initialized");

    log_info("Initializing WiFi");
    wifi_init();
    log_info("WiFi Initialized");

    log_info("Initializing Filesystem");
    ext2_init(0);
    log_info("Filesystem Initialized");

    log_info("Initializing Serial Communication");
    serial_init(COM1);
    log_info("Serial Communication Initialized");

    log_info("Initializing Process Management");
    process_init();
    log_info("Process Management Initialized");

    log_info("Initializing Scheduler");
    scheduler_init();
    log_info("Scheduler Initialized");

    log_info("Initializing Programmable Interval Timer");
    pit_init();
    log_info("PIT Initialized");

    log_info("Initializing Symmetric Multi-Processing");
    smp_init();
    smp_boot_aps();
    log_info("SMP Initialized");

    log_info("Initializing TTY");
    tty_init();
    log_info("TTY Initialized");

    log_info("Creating Pipe");
    pipe_create();
    log_info("Pipe Created");

    log_info("Initializing RTC");
    rtc_init();
    log_info("RTC Initialized");

    // Draw a welcome message to the framebuffer
    draw_string(global_framebuffer, "Welcome to AlephOS!", 0, 0, WHITE);
    log_info("Welcome message displayed");

    // Log the final initialization message
    log_info("Kernel Initialization Complete");

    // Main kernel loop
    while (1) {
        log_debug("Kernel Idle Loop");
        // Add any periodic kernel maintenance tasks here
    }

    // BRUH :dd:
    hcf();
}