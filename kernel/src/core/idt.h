#ifndef IDT_H
#define IDT_H

#include <stdint.h>
#include <stdbool.h>

// Number of IDT entries
#define IDT_ENTRIES 256

// Interrupt Stack Table (IST) indices
#define IST_NONE        0
#define IST_DEBUG       1
#define IST_NMI         2
#define IST_DOUBLE_FAULT 3
#define IST_MCE         4
#define IST_STACK_FAULT 5
#define IST_GPF         6
#define IST_DEFAULT     7

// CPU Exception Numbers
#define INT_DIVIDE_ERROR          0   // Division by zero
#define INT_DEBUG                 1   // Debug
#define INT_NMI                   2   // Non-maskable Interrupt
#define INT_BREAKPOINT            3   // Breakpoint
#define INT_OVERFLOW              4   // Overflow
#define INT_BOUND_RANGE_EXCEEDED  5   // Bound Range Exceeded
#define INT_INVALID_OPCODE        6   // Invalid Opcode
#define INT_DEVICE_NOT_AVAILABLE  7   // Device Not Available
#define INT_DOUBLE_FAULT          8   // Double Fault
#define INT_COPROCESSOR_SEGMENT   9   // Coprocessor Segment Overrun
#define INT_INVALID_TSS          10   // Invalid TSS
#define INT_SEGMENT_NOT_PRESENT  11   // Segment Not Present
#define INT_STACK_FAULT         12   // Stack-Segment Fault
#define INT_GENERAL_PROTECTION   13   // General Protection Fault
#define INT_PAGE_FAULT          14   // Page Fault
#define INT_RESERVED_15         15   // Reserved
#define INT_X87_FPU_ERROR       16   // x87 FPU Error
#define INT_ALIGNMENT_CHECK     17   // Alignment Check
#define INT_MACHINE_CHECK       18   // Machine Check
#define INT_SIMD_EXCEPTION      19   // SIMD Floating-Point Exception
#define INT_VIRTUALIZATION      20   // Virtualization Exception
#define INT_CONTROL_PROTECTION  21   // Control Protection Exception
#define INT_RESERVED_22         22   // Reserved
#define INT_RESERVED_23         23   // Reserved
#define INT_RESERVED_24         24   // Reserved
#define INT_RESERVED_25         25   // Reserved
#define INT_RESERVED_26         26   // Reserved
#define INT_RESERVED_27         27   // Reserved
#define INT_HYPERVISOR          28   // Hypervisor Injection Exception
#define INT_VMM_COMMUNICATION   29   // VMM Communication Exception
#define INT_SECURITY_EXCEPTION  30   // Security Exception
#define INT_RESERVED_31         31   // Reserved

// Hardware Interrupt Numbers
#define IRQ0                    32   // Programmable Interrupt Timer
#define IRQ1                    33   // Keyboard
#define IRQ2                    34   // Cascade for 8259A Slave controller
#define IRQ3                    35   // Serial Port 2
#define IRQ4                    36   // Serial Port 1
#define IRQ5                    37   // Parallel Port 2 / Sound Card
#define IRQ6                    38   // Floppy Disk
#define IRQ7                    39   // Parallel Port 1
#define IRQ8                    40   // Real Time Clock
#define IRQ9                    41   // ACPI
#define IRQ10                   42   // Available
#define IRQ11                   43   // Available
#define IRQ12                   44   // PS/2 Mouse
#define IRQ13                   45   // FPU / Coprocessor
#define IRQ14                   46   // Primary ATA Hard Disk
#define IRQ15                   47   // Secondary ATA Hard Disk

// IDT Gate Types
#define IDT_GATE_INTERRUPT      0x8E    // Present=1, DPL=0, Type=1110 (64-bit Interrupt Gate)
#define IDT_GATE_TRAP          0x8F    // Present=1, DPL=0, Type=1111 (64-bit Trap Gate)
#define IDT_GATE_CALL          0xEC    // Present=1, DPL=3, Type=1100 (64-bit Call Gate)
#define IDT_GATE_USER_INT      0xEE    // Present=1, DPL=3, Type=1110 (64-bit User Interrupt Gate)

// Interrupt Handler Frames
struct interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

struct interrupt_frame_error {
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

// IDT Structures
struct idt_entry {
    uint16_t base_low;        // Offset bits 0..15
    uint16_t selector;        // Code segment selector
    uint8_t  ist;            // Interrupt Stack Table offset
    uint8_t  flags;          // Type and attributes
    uint16_t base_middle;    // Offset bits 16..31
    uint32_t base_high;      // Offset bits 32..63
    uint32_t reserved;       // Reserved/zero
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;          // Size of IDT - 1
    uint64_t base;           // Base address of IDT
} __attribute__((packed));

// Function pointer types for handlers
typedef void (*interrupt_handler_t)(struct interrupt_frame*);
typedef void (*interrupt_handler_error_t)(struct interrupt_frame_error*);

// Function declarations
void idt_init(void);
void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags, uint8_t ist);
void register_interrupt_handler(uint8_t vector, interrupt_handler_t handler);
void register_exception_handler(uint8_t vector, interrupt_handler_error_t handler);
const char* get_exception_name(uint8_t vector);
void exception_handler_common(struct interrupt_frame_error* frame);

#endif