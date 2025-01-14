#include <core/smp.h>
#include <core/acpi.h>
#include <core/drivers/lapic.h>
#include <core/gdt.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <core/pit.h>
#include <utils/mem.h>
#include <utils/asm.h>

// Stack size for each CPU (16KB)
#define KERNEL_STACK_SIZE 0x4000

// CPU data array
static struct cpu_data cpu_data[MAX_CPUS];
static uint32_t cpu_count = 0;
static uint32_t bsp_apic_id = 0;

// Memory location for AP trampoline code
#define AP_TRAMPOLINE_ADDR 0x8000

// Memory locations for AP startup data
#define AP_STACK_ADDR      0x7000
#define AP_DATA_PAGE       0x6000

// AP startup data structure (placed at AP_DATA_PAGE)
struct ap_startup_data {
    uint32_t apic_id;        // APIC ID of the AP
    void* stack_ptr;         // Stack pointer for the AP
    void* page_table;        // CR3 value to use
    void (*entry)(void);     // Entry point function
    spinlock_t startup_lock; // Lock for synchronization
};

// Assembly function declarations
extern void ap_trampoline(void);
extern void ap_trampoline_end(void);

// Current CPU pointer (accessed via GS base)
static __thread struct cpu_data* current_cpu;

// Get current CPU data
static struct cpu_data* get_current_cpu(void) {
    uint32_t apic_id = lapic_get_id();

    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpu_data[i].apic_id == apic_id) {
            return &cpu_data[i];
        }
    }
    return NULL;
}

// AP entry point (called after AP startup)
static void ap_main(void) {
    // Get our CPU data
    struct cpu_data* cpu = get_current_cpu();
    if (!cpu) {
        // Something went wrong
        while(1) hlt();
    }

    // Initialize this CPU's local APIC
    lapic_init();
    lapic_enable();

    // Set up GDT and TSS for this CPU
    gdt_init();
    gdt_load_tss((uint64_t)cpu->kernel_stack + KERNEL_STACK_SIZE);

    // Set up interrupt stacks in TSS
    if (cpu->tss) {
        for (int i = 0; i < 7; i++) {
            // IST entries are ist1 through ist7 in the TSS
            uint64_t* ist_ptr = &cpu->tss->ist1 + i;
            *ist_ptr = (uint64_t)cpu->ist_stacks[i] + KERNEL_STACK_SIZE;
        }
    }

    // Enable interrupts
    sti();

    // Mark CPU as online
    cpu->state |= CPU_STATE_ONLINE;

    // Wait for work
    while (1) {
        hlt();
    }
}

// Initialize SMP
void smp_init(void) {
    // Get BSP's APIC ID
    bsp_apic_id = lapic_get_id();

    // Initialize BSP data
    cpu_data[0].apic_id = bsp_apic_id;
    cpu_data[0].cpu_number = 0;
    cpu_data[0].state = CPU_STATE_PRESENT | CPU_STATE_ONLINE | CPU_STATE_BSP;
    cpu_data[0].kernel_stack = pmm_alloc_page();
    for (int i = 0; i < 7; i++) {
        cpu_data[0].ist_stacks[i] = pmm_alloc_page();
    }
    cpu_count = 1;

    // Parse MADT to find other CPUs
    struct acpi_madt* madt = acpi_get_madt();
    if (!madt) return;

    uint8_t* entry = madt->entries;
    uint8_t* end = (uint8_t*)madt + madt->header.length;

    while (entry < end) {
        struct madt_entry_header* header = (struct madt_entry_header*)entry;
        if (header->type == MADT_TYPE_LOCAL_APIC) {
            struct madt_local_apic* lapic = (struct madt_local_apic*)entry;

            // Skip BSP and disabled CPUs
            if (lapic->apic_id == bsp_apic_id || !(lapic->flags & 1)) {
                entry += header->length;
                continue;
            }

            // Initialize CPU data
            cpu_data[cpu_count].apic_id = lapic->apic_id;
            cpu_data[cpu_count].cpu_number = cpu_count;
            cpu_data[cpu_count].state = CPU_STATE_PRESENT | CPU_STATE_AP;
            cpu_data[cpu_count].kernel_stack = pmm_alloc_page();
            for (int i = 0; i < 7; i++) {
                cpu_data[cpu_count].ist_stacks[i] = pmm_alloc_page();
            }
            cpu_count++;

            if (cpu_count >= MAX_CPUS) break;
        }
        entry += header->length;
    }
}

// Boot all Application Processors
void smp_boot_aps(void) {
    // Copy AP trampoline code to low memory
    uint32_t trampoline_size = (uint32_t)ap_trampoline_end - (uint32_t)ap_trampoline;
    memcpy((void*)AP_TRAMPOLINE_ADDR, ap_trampoline, trampoline_size);

    // Initialize startup data page
    struct ap_startup_data* startup_data = (struct ap_startup_data*)AP_DATA_PAGE;
    startup_data->page_table = (void*)vmm_get_cr3();
    startup_data->entry = ap_main;

    // Boot each AP
    for (uint32_t i = 1; i < cpu_count; i++) {
        if (!(cpu_data[i].state & CPU_STATE_PRESENT)) continue;

        // Set up startup data for this AP
        startup_data->apic_id = cpu_data[i].apic_id;
        startup_data->stack_ptr = cpu_data[i].kernel_stack + KERNEL_STACK_SIZE;
        spinlock_init(&startup_data->startup_lock);
        spinlock_acquire(&startup_data->startup_lock);

        // Send INIT IPI
        lapic_send_ipi(cpu_data[i].apic_id, 0x500);  // Assert INIT
        pit_wait(10);  // Wait 10ms
        lapic_send_ipi(cpu_data[i].apic_id, 0x600);  // Deassert INIT
        pit_wait(10);  // Wait 10ms

        // Send STARTUP IPI (twice as per Intel docs)
        for (int j = 0; j < 2; j++) {
            lapic_send_ipi(cpu_data[i].apic_id, 0x600 | (AP_TRAMPOLINE_ADDR >> 12));
            pit_wait(1);  // Wait 1ms
        }

        // Wait for AP to start up
        spinlock_acquire(&startup_data->startup_lock);
        spinlock_release(&startup_data->startup_lock);

        // Check if AP is online
        pit_wait(100);  // Wait 100ms max
        if (!(cpu_data[i].state & CPU_STATE_ONLINE)) {
            // AP failed to start
            cpu_data[i].state &= ~CPU_STATE_PRESENT;
            cpu_count--;
        }
    }
}

// Get number of CPUs
uint32_t smp_get_cpu_count(void) {
    return cpu_count;
}

// Get CPU data structure
struct cpu_data* smp_get_cpu_data(uint32_t cpu_num) {
    if (cpu_num >= cpu_count) return NULL;
    return &cpu_data[cpu_num];
}

// Get current CPU number
uint32_t smp_get_current_cpu(void) {
    struct cpu_data* cpu = get_current_cpu();
    return cpu ? cpu->cpu_number : 0;
}

// Send IPI to specific CPU
void smp_send_ipi(uint32_t cpu_num, uint32_t vector) {
    if (cpu_num >= cpu_count) return;
    lapic_send_ipi(cpu_data[cpu_num].apic_id, vector);
}

// Spinlock implementation
void spinlock_init(spinlock_t* lock) {
    *lock = 0;
}

void spinlock_acquire(spinlock_t* lock) {
    while (!spinlock_try_acquire(lock)) {
        __asm__ volatile("pause");
    }
}

void spinlock_release(spinlock_t* lock) {
    __asm__ volatile("" ::: "memory");
    *lock = 0;
}

bool spinlock_try_acquire(spinlock_t* lock) {
    return __sync_bool_compare_and_swap(lock, 0, 1);
}