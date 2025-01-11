#include <core/drivers/lapic.h>
#include <core/acpi.h>
#include <mm/vmm.h>
#include <utils/io.h>
#include <utils/asm.h>
#include <limine.h>

extern volatile struct limine_hhdm_request hhdm_request;

static volatile uint32_t* lapic_base = NULL;

static inline void lapic_write(uint32_t reg, uint32_t value) {
    if (!lapic_base) return;
    lapic_base[reg / 4] = value;
    // Read back to ensure write is complete (required by Intel manual)
    (void)lapic_base[LAPIC_ID / 4];
}

static inline uint32_t lapic_read(uint32_t reg) {
    if (!lapic_base) return 0;
    return lapic_base[reg / 4];
}

static bool lapic_check_present(void) {
    uint32_t eax, edx;

    // Use CPUID to check for APIC presence
    asm volatile(
        "cpuid"
        : "=a"(eax), "=d"(edx)
        : "a"(1)
        : "ebx", "ecx"
    );

    return (edx & (1 << 9)) != 0;
}

static void enable_apic_msr(void) {
    uint32_t low, high;

    // Read current MSR value
    asm volatile(
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(0x1B)  // IA32_APIC_BASE MSR
    );

    // Set enable bit (bit 11)
    low |= (1 << 11);

    // Write back MSR value
    asm volatile(
        "wrmsr"
        :
        : "a"(low), "d"(high), "c"(0x1B)
    );
}

void lapic_init(void) {
    // Check if APIC is present
    if (!lapic_check_present()) {
        return;
    }

    // Get APIC base address from ACPI MADT
    uint64_t apic_base = (uint64_t)acpi_get_local_apic_address();
    if (!apic_base) {
        return;
    }

    // Map LAPIC registers to virtual memory
    lapic_base = (volatile uint32_t*)(apic_base + hhdm_request.response->offset);

    // Enable APIC in MSR
    enable_apic_msr();

    // Enable the local APIC
    lapic_enable();

    // Set up Spurious Interrupt Vector Register
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | 0xFF);

    // Initialize LINT0 and LINT1
    lapic_write(LAPIC_LINT0, LAPIC_ICR_MASKED);
    lapic_write(LAPIC_LINT1, LAPIC_ICR_MASKED);

    // Initialize error handling
    lapic_write(LAPIC_ERROR, 0xFE);

    // Clear error status
    lapic_write(LAPIC_ESR, 0);
    lapic_write(LAPIC_ESR, 0);

    // Acknowledge any outstanding interrupts
    lapic_eoi();

    // Set task priority to 0 to accept all interrupts
    lapic_write(LAPIC_TPR, 0);
}

void lapic_enable(void) {
    lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) | LAPIC_SVR_ENABLE);
}

void lapic_disable(void) {
    lapic_write(LAPIC_SVR, lapic_read(LAPIC_SVR) & ~LAPIC_SVR_ENABLE);
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

uint32_t lapic_get_id(void) {
    return lapic_read(LAPIC_ID) >> 24;
}

void lapic_send_ipi(uint32_t cpu_id, uint32_t vector) {
    lapic_write(LAPIC_ICRHI, cpu_id << 24);
    lapic_write(LAPIC_ICRLO, vector);

    // Wait for delivery
    while (lapic_read(LAPIC_ICRLO) & LAPIC_ICR_SEND_PENDING)
        ;
}

void lapic_timer_init(uint32_t frequency) {
    // Set divider
    lapic_write(LAPIC_TDCR, 0x3);

    // Set periodic mode and vector
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_PERIODIC | 32);

    // Set initial count (calibration would be needed for precise frequency)
    lapic_write(LAPIC_TICR, frequency);
}

void lapic_timer_stop(void) {
    lapic_write(LAPIC_TIMER, LAPIC_ICR_MASKED);
    lapic_write(LAPIC_TICR, 0);
}