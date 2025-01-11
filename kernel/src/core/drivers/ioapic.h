#ifndef IOAPIC_H
#define IOAPIC_H

#include <stdint.h>
#include <stdbool.h>

// I/O APIC registers
#define IOAPIC_REG_ID           0x00    // ID Register
#define IOAPIC_REG_VER          0x01    // Version Register
#define IOAPIC_REG_ARB          0x02    // Arbitration ID Register
#define IOAPIC_REG_REDTBL_BASE  0x10    // First Redirection Table Entry

// Redirection table entry flags
#define IOAPIC_MASKED       (1 << 16)  // Interrupt masked
#define IOAPIC_TRIGGER_LEVEL (1 << 15) // Level triggered
#define IOAPIC_REMOTE_IRR   (1 << 14)  // Remote IRR
#define IOAPIC_PIN_POLARITY (1 << 13)  // Pin polarity
#define IOAPIC_DELIVERY_STATUS (1 << 12) // Delivery status
#define IOAPIC_DEST_MODE_LOGICAL (1 << 11) // Destination mode
#define IOAPIC_DELIVERY_MODE_FIXED (0 << 8) // Fixed delivery mode
#define IOAPIC_DELIVERY_MODE_LOWEST (1 << 8) // Lowest priority delivery
#define IOAPIC_DELIVERY_MODE_SMI (2 << 8) // SMI delivery
#define IOAPIC_DELIVERY_MODE_NMI (4 << 8) // NMI delivery
#define IOAPIC_DELIVERY_MODE_INIT (5 << 8) // INIT delivery
#define IOAPIC_DELIVERY_MODE_EXTINT (7 << 8) // ExtINT delivery

// Structure to represent an I/O APIC
struct ioapic {
    uint32_t id;                // I/O APIC ID
    volatile uint32_t* base;    // Memory-mapped base address
    uint32_t gsi_base;         // Global System Interrupt base
    uint32_t max_redirections; // Maximum number of redirection entries
};

// Function declarations
void ioapic_init(void);
uint32_t ioapic_get_version(struct ioapic* ioapic);
void ioapic_set_irq(struct ioapic* ioapic, uint8_t irq, uint8_t vector,
                    uint8_t delivery_mode, bool mask, uint8_t dest);
void ioapic_mask_irq(struct ioapic* ioapic, uint8_t irq);
void ioapic_unmask_irq(struct ioapic* ioapic, uint8_t irq);
struct ioapic* ioapic_get_for_gsi(uint32_t gsi);

// Internal functions (exposed for testing)
uint32_t ioapic_read(struct ioapic* ioapic, uint32_t reg);
void ioapic_write(struct ioapic* ioapic, uint32_t reg, uint32_t value);
void ioapic_write_redirection(struct ioapic* ioapic, uint32_t index, uint64_t data);
uint64_t ioapic_read_redirection(struct ioapic* ioapic, uint32_t index);

#endif // IOAPIC_H