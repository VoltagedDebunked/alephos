#include <core/gdt.h>

static struct gdt_entry gdt_entries[7];
static struct gdt_ptr gdt_pointer __attribute__((aligned(16)));
static struct tss tss __attribute__((aligned(16)));

// External assembly function to load GDT and TSS
extern void gdt_flush(uint64_t gdt_ptr);

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low = base & 0xFFFF;
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;
    gdt_entries[num].limit_low = limit & 0xFFFF;
    gdt_entries[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt_entries[num].access = access;
}

static void gdt_set_tss(uint32_t num, uint64_t base, uint32_t limit) {
    struct gdt_tss_entry* tss_entry = (struct gdt_tss_entry*)&gdt_entries[num];
    
    // Set base address
    tss_entry->base_low = base & 0xFFFF;
    tss_entry->base_middle = (base >> 16) & 0xFF;
    tss_entry->base_high = (base >> 24) & 0xFF;
    tss_entry->base_upper = (base >> 32) & 0xFFFFFFFF;
    
    // Set limit
    tss_entry->length = limit;
    
    // Set flags
    tss_entry->flags = 0x89;        // Present, DPL=0, Type=TSS Available
    tss_entry->granularity = 0x0;   // No granularity for TSS
    
    tss_entry->reserved = 0;
}

void gdt_init(void) {
    // Setup GDT pointer
    gdt_pointer.limit = (sizeof(struct gdt_entry) * 7) - 1;
    gdt_pointer.base = (uint64_t)&gdt_entries;

    // Initialize TSS
    for (int i = 0; i < sizeof(tss); i++) {
        ((uint8_t*)&tss)[i] = 0;
    }
    
    // Set up stack pointers in TSS
    // RSP0 is set later via gdt_load_tss
    tss.ist1 = 0x50000;  // Debug stack
    tss.ist2 = 0x51000;  // NMI stack
    tss.ist3 = 0x52000;  // Double fault stack
    tss.ist4 = 0x53000;  // General stack for interrupts
    tss.iopb_offset = sizeof(tss);  // No I/O permission bitmap

    // NULL descriptor
    gdt_set_gate(0, 0, 0, 0, 0);

    // Kernel mode code segment
    gdt_set_gate(1, 0, 0xFFFFFFFF,
        GDT_PRESENT | GDT_DESCRIPTOR | GDT_EXECUTABLE | GDT_READWRITE,
        GDT_GRANULARITY | GDT_SIZE_64);

    // Kernel mode data segment
    gdt_set_gate(2, 0, 0xFFFFFFFF,
        GDT_PRESENT | GDT_DESCRIPTOR | GDT_READWRITE,
        GDT_GRANULARITY | GDT_SIZE_64);

    // User mode code segment
    gdt_set_gate(3, 0, 0xFFFFFFFF,
        GDT_PRESENT | GDT_DPL_RING3 | GDT_DESCRIPTOR | GDT_EXECUTABLE | GDT_READWRITE,
        GDT_GRANULARITY | GDT_SIZE_64);

    // User mode data segment
    gdt_set_gate(4, 0, 0xFFFFFFFF,
        GDT_PRESENT | GDT_DPL_RING3 | GDT_DESCRIPTOR | GDT_READWRITE,
        GDT_GRANULARITY | GDT_SIZE_64);

    // TSS entries (we need two entries for x86_64 TSS)
    gdt_set_tss(5, (uint64_t)&tss, sizeof(tss));

    // Load GDT and TSS
    gdt_flush((uint64_t)&gdt_pointer);
}

void gdt_load_tss(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}