#ifndef SMP_H
#define SMP_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_CPUS 256

// CPU state flags
#define CPU_STATE_PRESENT  (1 << 0)
#define CPU_STATE_ONLINE   (1 << 1)
#define CPU_STATE_BSP      (1 << 2)  // Bootstrap Processor
#define CPU_STATE_AP       (1 << 3)  // Application Processor

// Per-CPU data structure
struct cpu_data {
    uint32_t apic_id;      // Local APIC ID
    uint32_t cpu_number;   // Logical CPU number
    uint32_t state;        // CPU state flags
    void* kernel_stack;    // Kernel stack for this CPU
    void* ist_stacks[7];   // Interrupt stacks for this CPU
    struct tss* tss;       // TSS for this CPU
};

// SMP functions
void smp_init(void);
void smp_boot_aps(void);
uint32_t smp_get_cpu_count(void);
struct cpu_data* smp_get_cpu_data(uint32_t cpu_num);
uint32_t smp_get_current_cpu(void);
void smp_send_ipi(uint32_t cpu_num, uint32_t vector);

// Spinlock for SMP synchronization
typedef volatile uint32_t spinlock_t;

void spinlock_init(spinlock_t* lock);
void spinlock_acquire(spinlock_t* lock);
void spinlock_release(spinlock_t* lock);
bool spinlock_try_acquire(spinlock_t* lock);

// Inter-processor message functions
void smp_send_message(uint32_t cpu_num, uint32_t message, uint64_t data);
void smp_broadcast_message(uint32_t message, uint64_t data);

#endif // SMP_H