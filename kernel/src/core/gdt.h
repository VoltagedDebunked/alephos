#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// GDT Entry Access Byte flags
#define GDT_PRESENT        0x80
#define GDT_DPL_RING0     0x00
#define GDT_DPL_RING3     0x60
#define GDT_DESCRIPTOR    0x10
#define GDT_EXECUTABLE    0x08
#define GDT_READWRITE    0x02
#define GDT_ACCESSED     0x01

// GDT Entry Flags
#define GDT_GRANULARITY  0x80
#define GDT_SIZE_64      0x20
#define GDT_SIZE_32      0x40
#define GDT_AVAILABLE    0x10

// System Segment Types
#define GDT_TSS_AVAILABLE  0x09
#define GDT_TSS_BUSY       0x0B

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_tss_entry {
    uint16_t length;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t flags;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

void gdt_init(void);
void gdt_load_tss(uint64_t rsp0);

#endif