#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#if defined(__x86_64__)
    #define ARCH_X64
#elif defined(__i386__)
    #define ARCH_X86
#elif defined(__aarch64__)
    #define ARCH_ARM64
#elif defined(__riscv) && __riscv_xlen == 64
    #define ARCH_RISCV64
#endif

#define IDT_ENTRIES 256

#if defined(ARCH_X64) || defined(ARCH_X86)
struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t base_middle;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    #ifdef ARCH_X64
        uint64_t base;
    #else
        uint32_t base;
    #endif
} __attribute__((packed));

#elif defined(ARCH_ARM64)
struct vector_table_entry {
    uint32_t instruction[32];
} __attribute__((aligned(128)));

#elif defined(ARCH_RISCV64)
struct trap_entry {
    uint64_t handler;
    uint64_t stack_top;
} __attribute__((aligned(8)));
#endif

typedef void (*interrupt_handler_t)(void);
void idt_init(void);
void register_interrupt_handler(uint8_t vector, interrupt_handler_t handler);

#endif