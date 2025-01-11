#include <core/acpi.h>
#include <limine.h>
#include <utils/mem.h>
#include <mm/vmm.h>

extern volatile struct limine_rsdp_request rsdp_request;
static struct acpi_rsdp* rsdp = NULL;
static struct acpi_header* rsdt = NULL;
static struct acpi_header* xsdt = NULL;
static struct acpi_madt* madt = NULL;
static bool acpi_initialized = false;

static bool validate_table(struct acpi_header* table) {
    if (!table) return false;

    uint8_t sum = 0;
    uint8_t* ptr = (uint8_t*)table;

    // Checksum the entire table
    for (uint32_t i = 0; i < table->length; i++) {
        sum += ptr[i];
    }

    return sum == 0;
}

static void* find_table_xsdt(const char* signature) {
    if (!xsdt) return NULL;

    uint64_t entries = (xsdt->length - sizeof(struct acpi_header)) / 8;
    uint64_t* ptr = (uint64_t*)((uintptr_t)xsdt + sizeof(struct acpi_header));

    for (uint64_t i = 0; i < entries; i++) {
        struct acpi_header* table = (struct acpi_header*)ptr[i];
        if (!memcmp(table->signature, signature, 4)) {
            if (validate_table(table)) {
                return table;
            }
        }
    }

    return NULL;
}

static void* find_table_rsdt(const char* signature) {
    if (!rsdt) return NULL;

    uint32_t entries = (rsdt->length - sizeof(struct acpi_header)) / 4;
    uint32_t* ptr = (uint32_t*)((uintptr_t)rsdt + sizeof(struct acpi_header));

    for (uint32_t i = 0; i < entries; i++) {
        struct acpi_header* table = (struct acpi_header*)(uintptr_t)ptr[i];
        if (!memcmp(table->signature, signature, 4)) {
            if (validate_table(table)) {
                return table;
            }
        }
    }

    return NULL;
}

void acpi_init(void) {
    if (acpi_initialized) return;

    // Get RSDP from bootloader
    rsdp = (struct acpi_rsdp*)rsdp_request.response->address;
    if (!rsdp) return;

    // Verify RSDP signature
    if (memcmp(rsdp->signature, ACPI_RSDP_SIGNATURE, 8) != 0) {
        return;
    }

    // Check version and get appropriate SDT
    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        xsdt = (struct acpi_header*)(uintptr_t)rsdp->xsdt_address;
        if (!validate_table(xsdt)) {
            xsdt = NULL;
        }
    }

    if (!xsdt && rsdp->rsdt_address) {
        rsdt = (struct acpi_header*)(uintptr_t)rsdp->rsdt_address;
        if (!validate_table(rsdt)) {
            rsdt = NULL;
            return;
        }
    }

    // Find and cache MADT
    madt = (struct acpi_madt*)acpi_find_table(ACPI_MADT_SIGNATURE);

    acpi_initialized = true;
}

bool acpi_is_initialized(void) {
    return acpi_initialized;
}

struct acpi_header* acpi_find_table(const char* signature) {
    if (!acpi_initialized) return NULL;

    // Try XSDT first
    void* table = find_table_xsdt(signature);
    if (table) return table;

    // Fall back to RSDT if necessary
    return find_table_rsdt(signature);
}

void* acpi_get_mcfg_base(void) {
    struct acpi_mcfg* mcfg = (struct acpi_mcfg*)acpi_find_table(ACPI_MCFG_SIGNATURE);
    if (!mcfg || mcfg->header.length < sizeof(struct acpi_mcfg)) {
        return NULL;
    }

    // Return base address of first PCI segment
    return (void*)(uintptr_t)mcfg->configurations[0].base_address;
}

struct acpi_madt* acpi_get_madt(void) {
    return madt;
}

void* acpi_get_local_apic_address(void) {
    if (!madt) return NULL;
    return (void*)(uintptr_t)madt->local_apic_address;
}

uint32_t acpi_get_madt_flags(void) {
    if (!madt) return 0;
    return madt->flags;
}

struct madt_local_apic* acpi_get_local_apic(uint8_t cpu_id) {
    if (!madt) return NULL;

    uint8_t* entry = madt->entries;
    uint8_t* end = (uint8_t*)madt + madt->header.length;

    while (entry < end) {
        struct madt_entry_header* header = (struct madt_entry_header*)entry;
        if (header->type == MADT_TYPE_LOCAL_APIC) {
            struct madt_local_apic* lapic = (struct madt_local_apic*)entry;
            if (lapic->processor_id == cpu_id) {
                return lapic;
            }
        }
        entry += header->length;
    }

    return NULL;
}

struct madt_io_apic* acpi_get_io_apic(uint8_t io_apic_id) {
    if (!madt) return NULL;

    uint8_t* entry = madt->entries;
    uint8_t* end = (uint8_t*)madt + madt->header.length;

    while (entry < end) {
        struct madt_entry_header* header = (struct madt_entry_header*)entry;
        if (header->type == MADT_TYPE_IO_APIC) {
            struct madt_io_apic* ioapic = (struct madt_io_apic*)entry;
            if (ioapic->io_apic_id == io_apic_id) {
                return ioapic;
            }
        }
        entry += header->length;
    }

    return NULL;
}