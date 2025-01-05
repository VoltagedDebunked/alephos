#include <core/idt.h>

#if defined(ARCH_X64) || defined(ARCH_X86)
static struct idt_entry idt_entries[IDT_ENTRIES];
static struct idt_ptr idt_pointer;
#elif defined(ARCH_ARM64)
static struct vector_table_entry vector_table[IDT_ENTRIES] __attribute__((aligned(2048)));
#elif defined(ARCH_RISCV64)
static struct trap_entry trap_table[IDT_ENTRIES] __attribute__((aligned(8)));
#endif

static interrupt_handler_t interrupt_handlers[IDT_ENTRIES];

#if defined(ARCH_X64) || defined(ARCH_X86)
static void idt_set_gate(uint8_t num, interrupt_handler_t handler, uint16_t selector, uint8_t flags) {
    uint64_t base = (uint64_t)handler;
    
    idt_entries[num].base_low = base & 0xFFFF;
    idt_entries[num].selector = selector;
    idt_entries[num].ist = 0;
    idt_entries[num].flags = flags;
    idt_entries[num].base_middle = (base >> 16) & 0xFFFF;
    idt_entries[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt_entries[num].reserved = 0;
}
#endif

void idt_init(void) {
#if defined(ARCH_X64) || defined(ARCH_X86)
    idt_pointer.limit = sizeof(struct idt_entry) * IDT_ENTRIES - 1;
    idt_pointer.base = (uintptr_t)&idt_entries;

    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0x08, 0x8E);
        interrupt_handlers[i] = 0;
    }

    asm volatile ("lidt %0" :: "m"(idt_pointer));

#elif defined(ARCH_ARM64)
    for (int i = 0; i < IDT_ENTRIES; i++) {
        // Setup default vector table entries with trampoline code
        vector_table[i].instruction[0] = 0xd53b420f;  // mrs x15, esr_el1
        vector_table[i].instruction[1] = 0xd538d092;  // mrs x18, tpidr_el1
        vector_table[i].instruction[2] = 0xd61f0200;  // br x16 (handler address loaded in x16)
    }

    asm volatile (
        "msr vbar_el1, %0\n"
        "isb"
        :: "r"(&vector_table)
        : "memory"
    );

#elif defined(ARCH_RISCV64)
    for (int i = 0; i < IDT_ENTRIES; i++) {
        trap_table[i].handler = 0;
        trap_table[i].stack_top = 0;
    }

    asm volatile (
        "csrw stvec, %0"
        :: "r"(&trap_table)
        : "memory"
    );
#endif
}

void register_interrupt_handler(uint8_t vector, interrupt_handler_t handler) {
    interrupt_handlers[vector] = handler;

#if defined(ARCH_X64) || defined(ARCH_X86)
    idt_set_gate(vector, handler, 0x08, 0x8E);

#elif defined(ARCH_ARM64)
    // Load handler address into x16 before trampoline
    uint32_t *instructions = vector_table[vector].instruction;
    instructions[0] = 0xd2800010 | (((uint64_t)handler & 0xFFFF) << 5);  // movz x16, #imm16
    instructions[1] = 0xf2a00010 | (((uint64_t)handler >> 16) & 0xFFFF) << 5;  // movk x16, #imm16, lsl #16
    instructions[2] = 0xf2c00010 | (((uint64_t)handler >> 32) & 0xFFFF) << 5;  // movk x16, #imm16, lsl #32
    instructions[3] = 0xf2e00010 | (((uint64_t)handler >> 48) & 0xFFFF) << 5;  // movk x16, #imm16, lsl #48

#elif defined(ARCH_RISCV64)
    trap_table[vector].handler = (uint64_t)handler;
#endif
}

// Interrupt stub (architecture-specific context save/restore)
void interrupt_stub(void) {
#if defined(ARCH_X64)
    asm volatile (
        "push rax\n"
        "push rbx\n"
        "push rcx\n"
        "push rdx\n"
        "push rsi\n"
        "push rdi\n"
        "push rbp\n"
        "push r8\n"
        "push r9\n"
        "push r10\n"
        "push r11\n"
        "push r12\n"
        "push r13\n"
        "push r14\n"
        "push r15\n"
    );

    // Call handler (vector number should be passed in some way)
    uint8_t vector = 0; // This needs to be properly obtained
    if (interrupt_handlers[vector]) {
        interrupt_handlers[vector]();
    }

    asm volatile (
        "pop r15\n"
        "pop r14\n"
        "pop r13\n"
        "pop r12\n"
        "pop r11\n"
        "pop r10\n"
        "pop r9\n"
        "pop r8\n"
        "pop rbp\n"
        "pop rdi\n"
        "pop rsi\n"
        "pop rdx\n"
        "pop rcx\n"
        "pop rbx\n"
        "pop rax\n"
        "iretq\n"
    );
#endif
}