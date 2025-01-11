#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>
#include <stdbool.h>

// Local APIC registers (offsets from base)
#define LAPIC_ID                    0x020  // Local APIC ID
#define LAPIC_VERSION              0x030  // Local APIC Version
#define LAPIC_TPR                  0x080  // Task Priority
#define LAPIC_APR                  0x090  // Arbitration Priority
#define LAPIC_PPR                  0x0A0  // Processor Priority
#define LAPIC_EOI                  0x0B0  // EOI
#define LAPIC_RRD                  0x0C0  // Remote Read
#define LAPIC_LDR                  0x0D0  // Logical Destination
#define LAPIC_DFR                  0x0E0  // Destination Format
#define LAPIC_SVR                  0x0F0  // Spurious Interrupt Vector
#define LAPIC_ISR                  0x100  // In-Service (8 registers)
#define LAPIC_TMR                  0x180  // Trigger Mode (8 registers)
#define LAPIC_IRR                  0x200  // Interrupt Request (8 registers)
#define LAPIC_ESR                  0x280  // Error Status
#define LAPIC_ICRLO                0x300  // Interrupt Command Low
#define LAPIC_ICRHI                0x310  // Interrupt Command High
#define LAPIC_TIMER                0x320  // LVT Timer
#define LAPIC_THERMAL              0x330  // LVT Thermal Sensor
#define LAPIC_PERF                 0x340  // LVT Performance Counter
#define LAPIC_LINT0                0x350  // LVT LINT0
#define LAPIC_LINT1                0x360  // LVT LINT1
#define LAPIC_ERROR                0x370  // LVT Error
#define LAPIC_TICR                 0x380  // Timer Initial Count
#define LAPIC_TCCR                 0x390  // Timer Current Count
#define LAPIC_TDCR                 0x3E0  // Timer Divide Configuration

// Spurious Interrupt Vector Register bits
#define LAPIC_SVR_ENABLE           0x100
#define LAPIC_SVR_FOCUS_DISABLE    0x200

// Timer modes
#define LAPIC_TIMER_ONESHOT        0x00000000
#define LAPIC_TIMER_PERIODIC       0x00020000
#define LAPIC_TIMER_DEADLINE       0x00040000

// Interrupt Command Register bits
#define LAPIC_ICR_FIXED            0x00000000
#define LAPIC_ICR_LOWEST           0x00000100
#define LAPIC_ICR_SMI              0x00000200
#define LAPIC_ICR_NMI              0x00000400
#define LAPIC_ICR_INIT             0x00000500
#define LAPIC_ICR_STARTUP          0x00000600
#define LAPIC_ICR_PHYSICAL         0x00000000
#define LAPIC_ICR_LOGICAL          0x00000800
#define LAPIC_ICR_IDLE             0x00000000
#define LAPIC_ICR_SEND_PENDING     0x00001000
#define LAPIC_ICR_DEASSERT         0x00000000
#define LAPIC_ICR_ASSERT           0x00004000
#define LAPIC_ICR_EDGE             0x00000000
#define LAPIC_ICR_LEVEL            0x00008000
#define LAPIC_ICR_NO_SHORTHAND     0x00000000
#define LAPIC_ICR_SELF             0x00040000
#define LAPIC_ICR_ALL_INCLUDING    0x00080000
#define LAPIC_ICR_ALL_EXCLUDING    0x000C0000

// LVT Mask bit and common flags
#define LAPIC_ICR_MASKED           0x00010000
#define LAPIC_DELIVER_MODE_FIXED   0x000
#define LAPIC_DELIVER_MODE_NMI     0x400
#define LAPIC_DELIVER_MODE_EXTINT  0x700
#define LAPIC_DELIVER_STATUS       0x1000
#define LAPIC_INPUT_POLARITY      0x2000
#define LAPIC_REMOTE_IRR          0x4000
#define LAPIC_TRIGGER_MODE        0x8000
#define LAPIC_DELIVER_STATUS      0x1000

// Function declarations
void lapic_init(void);
void lapic_enable(void);
void lapic_disable(void);
void lapic_eoi(void);
uint32_t lapic_get_id(void);
void lapic_send_ipi(uint32_t cpu_id, uint32_t vector);
void lapic_timer_init(uint32_t frequency);
void lapic_timer_stop(void);

#endif // LAPIC_H