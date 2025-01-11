#include <core/drivers/ioapic.h>
#include <core/acpi.h>
#include <core/drivers/pic.h>
#include <utils/mem.h>
#include <mm/vmm.h>
#include <limine.h>

#define MAX_IOAPICS 8

extern volatile struct limine_hhdm_request hhdm_request;

static struct ioapic ioapics[MAX_IOAPICS];
static int num_ioapics = 0;

static inline void* phys_to_virt(uint64_t phys) {
    return (void*)(phys + hhdm_request.response->offset);
}

uint32_t ioapic_read(struct ioapic* ioapic, uint32_t reg) {
    volatile uint32_t* base = ioapic->base;
    base[0] = reg;
    return base[4 >> 2];
}

void ioapic_write(struct ioapic* ioapic, uint32_t reg, uint32_t value) {
    volatile uint32_t* base = ioapic->base;
    base[0] = reg;
    base[4 >> 2] = value;
}

uint64_t ioapic_read_redirection(struct ioapic* ioapic, uint32_t index) {
    uint64_t value;
    value = ioapic_read(ioapic, IOAPIC_REG_REDTBL_BASE + index * 2 + 1);
    value <<= 32;
    value |= ioapic_read(ioapic, IOAPIC_REG_REDTBL_BASE + index * 2);
    return value;
}

void ioapic_write_redirection(struct ioapic* ioapic, uint32_t index, uint64_t data) {
    ioapic_write(ioapic, IOAPIC_REG_REDTBL_BASE + index * 2, (uint32_t)data);
    ioapic_write(ioapic, IOAPIC_REG_REDTBL_BASE + index * 2 + 1, (uint32_t)(data >> 32));
}

uint32_t ioapic_get_version(struct ioapic* ioapic) {
    return ioapic_read(ioapic, IOAPIC_REG_VER);
}

void ioapic_init(void) {
    struct acpi_madt* madt = acpi_get_madt();
    if (!madt) return;

    // Parse MADT entries to find I/O APICs
    uint8_t* entry = madt->entries;
    uint8_t* end = (uint8_t*)madt + madt->header.length;

    while (entry < end && num_ioapics < MAX_IOAPICS) {
        struct madt_entry_header* header = (struct madt_entry_header*)entry;

        if (header->type == MADT_TYPE_IO_APIC) {
            struct madt_io_apic* ioapic_entry = (struct madt_io_apic*)entry;
            struct ioapic* ioapic = &ioapics[num_ioapics];

            // Map the I/O APIC registers to virtual memory
            uint64_t phys_addr = ioapic_entry->io_apic_address;
            ioapic->base = phys_to_virt(phys_addr);

            // Ensure the memory mapping succeeded
            if (!ioapic->base) {
                entry += header->length;
                continue;
            }

            ioapic->id = ioapic_entry->io_apic_id;
            ioapic->gsi_base = ioapic_entry->global_system_interrupt_base;

            // Get maximum redirection entries from version register
            uint32_t ver = ioapic_get_version(ioapic);
            ioapic->max_redirections = ((ver >> 16) & 0xFF) + 1;

            // Initialize all redirection entries as masked
            for (uint32_t i = 0; i < ioapic->max_redirections; i++) {
                ioapic_write_redirection(ioapic, i, IOAPIC_MASKED);
            }

            num_ioapics++;
        }

        entry += header->length;
    }

    // Do not disable PIC yet - we'll do this when we're ready to switch
    // This helps with debugging as the PIC still works if IOAPIC setup fails
}

struct ioapic* ioapic_get_for_gsi(uint32_t gsi) {
    for (int i = 0; i < num_ioapics; i++) {
        struct ioapic* ioapic = &ioapics[i];
        if (gsi >= ioapic->gsi_base &&
            gsi < ioapic->gsi_base + ioapic->max_redirections) {
            return ioapic;
        }
    }
    return NULL;
}

void ioapic_set_irq(struct ioapic* ioapic, uint8_t irq, uint8_t vector,
                    uint8_t delivery_mode, bool mask, uint8_t dest) {
    if (!ioapic || irq >= ioapic->max_redirections) return;

    uint64_t redirection = 0;
    redirection |= vector;
    redirection |= (uint64_t)delivery_mode << 8;
    redirection |= mask ? IOAPIC_MASKED : 0;
    redirection |= (uint64_t)dest << 56;

    ioapic_write_redirection(ioapic, irq, redirection);
}

void ioapic_mask_irq(struct ioapic* ioapic, uint8_t irq) {
    if (!ioapic || irq >= ioapic->max_redirections) return;

    uint64_t redirection = ioapic_read_redirection(ioapic, irq);
    redirection |= IOAPIC_MASKED;
    ioapic_write_redirection(ioapic, irq, redirection);
}

void ioapic_unmask_irq(struct ioapic* ioapic, uint8_t irq) {
    if (!ioapic || irq >= ioapic->max_redirections) return;

    uint64_t redirection = ioapic_read_redirection(ioapic, irq);
    redirection &= ~IOAPIC_MASKED;
    ioapic_write_redirection(ioapic, irq, redirection);
}