#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stdbool.h>

// ACPI table signatures
#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_RSDT_SIGNATURE "RSDT"
#define ACPI_XSDT_SIGNATURE "XSDT"
#define ACPI_FADT_SIGNATURE "FACP"
#define ACPI_MCFG_SIGNATURE "MCFG"
#define ACPI_MADT_SIGNATURE "APIC"

// MADT Entry Types
#define MADT_TYPE_LOCAL_APIC        0x0
#define MADT_TYPE_IO_APIC          0x1
#define MADT_TYPE_INT_SRC_OVERRIDE  0x2
#define MADT_TYPE_NMI_SOURCE        0x3
#define MADT_TYPE_LOCAL_APIC_NMI    0x4
#define MADT_TYPE_LOCAL_APIC_OVERRIDE 0x5
#define MADT_TYPE_IO_SAPIC         0x6
#define MADT_TYPE_LOCAL_SAPIC      0x7
#define MADT_TYPE_PLATFORM_INT_SRC  0x8

// ACPI table structures
struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    // Version 2.0 fields
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_mcfg {
    struct acpi_header header;
    uint64_t reserved;
    struct {
        uint64_t base_address;
        uint16_t pci_segment;
        uint8_t start_bus;
        uint8_t end_bus;
        uint32_t reserved;
    } __attribute__((packed)) configurations[];
} __attribute__((packed));

// MADT (Multiple APIC Description Table) structures
struct acpi_madt {
    struct acpi_header header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t entries[];
} __attribute__((packed));

struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct madt_local_apic {
    struct madt_entry_header header;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct madt_io_apic {
    struct madt_entry_header header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;
    uint32_t global_system_interrupt_base;
} __attribute__((packed));

struct madt_interrupt_override {
    struct madt_entry_header header;
    uint8_t bus;
    uint8_t source;
    uint32_t global_system_interrupt;
    uint16_t flags;
} __attribute__((packed));

struct madt_local_apic_nmi {
    struct madt_entry_header header;
    uint8_t processor_id;
    uint16_t flags;
    uint8_t lint;
} __attribute__((packed));

// Function declarations
void acpi_init(void);
bool acpi_is_initialized(void);
struct acpi_header* acpi_find_table(const char* signature);
void* acpi_get_mcfg_base(void);

// MADT-specific functions
struct acpi_madt* acpi_get_madt(void);
struct madt_local_apic* acpi_get_local_apic(uint8_t cpu_id);
struct madt_io_apic* acpi_get_io_apic(uint8_t io_apic_id);
void* acpi_get_local_apic_address(void);
uint32_t acpi_get_madt_flags(void);

#endif // ACPI_H