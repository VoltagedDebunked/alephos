#include <core/idt.h>
#include <utils/asm.h>
#include <graphics/display.h>
#include <graphics/colors.h>
#include <stddef.h>

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtr;

// Arrays to store interrupt handlers
static interrupt_handler_t interrupt_handlers[IDT_ENTRIES];
static interrupt_handler_error_t exception_handlers[32];  // First 32 vectors are exceptions

// Exception names for debugging
static const char* exception_names[] = {
    "Division Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

// External assembly routines
extern void* isr_stub_table[];

void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags, uint8_t ist) {
    uint64_t base = (uint64_t)isr;

    idt[vector].base_low = base & 0xFFFF;
    idt[vector].selector = 0x08;  // Kernel code segment
    idt[vector].ist = ist & 0x7;  // Only values 0-7 are valid
    idt[vector].flags = flags;
    idt[vector].base_middle = (base >> 16) & 0xFFFF;
    idt[vector].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;
}

void idt_init(void) {
    idtr.limit = (uint16_t)(sizeof(struct idt_entry) * IDT_ENTRIES - 1);
    idtr.base = (uint64_t)&idt[0];

    // Clear the interrupt descriptor table
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_descriptor(i, isr_stub_table[i], IDT_GATE_INTERRUPT, IST_NONE);
        interrupt_handlers[i] = NULL;
        if (i < 32) exception_handlers[i] = NULL;
    }

    // Set up special exception handlers with IST
    idt_set_descriptor(INT_DEBUG, isr_stub_table[INT_DEBUG],
                      IDT_GATE_INTERRUPT, IST_DEBUG);

    idt_set_descriptor(INT_NMI, isr_stub_table[INT_NMI],
                      IDT_GATE_INTERRUPT, IST_NMI);

    idt_set_descriptor(INT_DOUBLE_FAULT, isr_stub_table[INT_DOUBLE_FAULT],
                      IDT_GATE_INTERRUPT, IST_DOUBLE_FAULT);

    idt_set_descriptor(INT_MACHINE_CHECK, isr_stub_table[INT_MACHINE_CHECK],
                      IDT_GATE_INTERRUPT, IST_MCE);

    idt_set_descriptor(INT_STACK_FAULT, isr_stub_table[INT_STACK_FAULT],
                      IDT_GATE_INTERRUPT, IST_STACK_FAULT);

    idt_set_descriptor(INT_GENERAL_PROTECTION, isr_stub_table[INT_GENERAL_PROTECTION],
                      IDT_GATE_INTERRUPT, IST_GPF);

    // Load the IDT
    asm volatile ("lidt %0" : : "m"(idtr));
}

void register_interrupt_handler(uint8_t vector, interrupt_handler_t handler) {
    if (vector >= 32) {  // Only for non-exception interrupts
        interrupt_handlers[vector] = handler;
    }
}

void register_exception_handler(uint8_t vector, interrupt_handler_error_t handler) {
    if (vector < 32) {  // Only for exceptions
        exception_handlers[vector] = handler;
    }
}

const char* get_exception_name(uint8_t vector) {
    if (vector < 32) {
        return exception_names[vector];
    }
    return "Unknown Exception";
}

// This is called from our ASM interrupt handler stubs
void exception_handler_common(struct interrupt_frame_error* frame) {
    uint64_t vector = frame->error_code >> 3;  // Error code contains the vector in our setup

    // Handle CPU exceptions (vectors 0-31)
    if (vector < 32) {
        if (exception_handlers[vector]) {
            exception_handlers[vector](frame);
        } else {
            // Default exception handler
            cli();  // Disable interrupts

            // Get CR2 for page faults
            uint64_t cr2 = 0;
            if (vector == INT_PAGE_FAULT) {
                asm volatile("mov %%cr2, %0" : "=r"(cr2));
            }

            // TODO: Add proper display/logging of exception information
            // For now, just halt
            hlt();
        }
    }
    // Handle regular interrupts (vectors 32-255)
    else if (interrupt_handlers[vector]) {
        interrupt_handlers[vector]((struct interrupt_frame*)frame);
    }
}