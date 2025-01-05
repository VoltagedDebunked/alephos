#include <core/gdt.h>

#if defined(ARCH_X64) || defined(ARCH_X86)
static struct gdt_entry gdt_entries[5];
static struct gdt_ptr gdt_pointer __attribute__((aligned(16)));

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low = base & 0xFFFF;
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;
    gdt_entries[num].limit_low = limit & 0xFFFF;
    gdt_entries[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt_entries[num].access = access;
}
#endif

void gdt_init(void) {
#if defined(ARCH_X64)
    gdt_pointer.limit = (sizeof(struct gdt_entry) * 5) - 1;
    gdt_pointer.base = (uint64_t)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xAF);
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    asm volatile (
        "lgdt %0\n"
        "push 0x08\n"
        "lea rax, [rip + 1f]\n"
        "push rax\n"
        "retfq\n"
        "1:\n"
        "mov ax, 0x10\n"
        "mov ds, ax\n"
        "mov es, ax\n"
        "mov fs, ax\n"
        "mov gs, ax\n"
        "mov ss, ax"
        :: "m"(gdt_pointer)
        : "memory", "rax"
    );

#elif defined(ARCH_X86)
    gdt_pointer.limit = (sizeof(struct gdt_entry) * 5) - 1;
    gdt_pointer.base = (uint32_t)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    asm volatile (
        "lgdt %0\n"
        "mov ax, 0x10\n"
        "mov ds, ax\n"
        "mov es, ax\n"
        "mov fs, ax\n"
        "mov gs, ax\n"
        "mov ss, ax\n"
        "jmp 0x08:.flush\n"
        ".flush:\n"
        "ret"
        :: "m"(gdt_pointer)
        : "memory", "ax"
    );

#elif defined(ARCH_ARM64)
    asm volatile (
        "msr ttbr0_el1, %0\n"
        "dsb sy\n"
        "isb"
        :: "r"(0)
        : "memory"
    );

#elif defined(ARCH_RISCV64)
    asm volatile (
        "csrw satp, %0\n"
        "sfence.vma"
        :: "r"(0)
        : "memory"
    );
#endif
}