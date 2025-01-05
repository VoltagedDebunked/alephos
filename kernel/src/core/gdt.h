#ifndef GDT_H
#define GDT_H

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

#if defined(ARCH_X64) || defined(ARCH_X86)
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    #ifdef ARCH_X64
        uint64_t base;
    #else
        uint32_t base;
    #endif
} __attribute__((packed));
#endif

void gdt_init(void);

#endif