#include <core/drivers/pci.h>
#include <core/acpi.h>
#include <utils/io.h>
#include <utils/mem.h>

// Maximum values for PCI bus/device/function
#define PCI_MAX_BUS    256
#define PCI_MAX_SLOT   32
#define PCI_MAX_FUNC   8

// Array to store found PCI devices
#define MAX_PCI_DEVICES 32
static struct pci_device pci_devices[MAX_PCI_DEVICES];
static int num_pci_devices = 0;

// MMIO base for PCI config space
static volatile uint8_t* pci_mcfg_base = NULL;

// Helper function to get MMIO address for PCI config space
static void* get_device_addr(uint8_t bus, uint8_t slot, uint8_t func) {
    if (!pci_mcfg_base) return NULL;
    return (void*)(pci_mcfg_base + ((bus << 20) | (slot << 15) | (func << 12)));
}

uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    // Try MCFG (memory-mapped) access first
    void* addr = get_device_addr(bus, slot, func);
    if (addr) {
        return *(volatile uint32_t*)((uintptr_t)addr + offset);
    }

    // Fall back to legacy I/O ports if no MCFG
    uint32_t address = 0x80000000 | (bus << 16) | (slot << 11) |
                      (func << 8) | (offset & 0xFC);
    outl(0xCF8, address);
    return inl(0xCFC);
}

void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    void* addr = get_device_addr(bus, slot, func);
    if (addr) {
        *(volatile uint32_t*)((uintptr_t)addr + offset) = value;
    } else {
        uint32_t address = 0x80000000 | (bus << 16) | (slot << 11) |
                          (func << 8) | (offset & 0xFC);
        outl(0xCF8, address);
        outl(0xCFC, value);
    }
}

static void pci_scan_function(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t vendor_device = pci_read_config(bus, slot, func, 0);
    uint16_t vendor = vendor_device & 0xFFFF;
    uint16_t device = (vendor_device >> 16) & 0xFFFF;

    if (vendor == 0xFFFF) return;  // Invalid vendor
    if (num_pci_devices >= MAX_PCI_DEVICES) return;

    struct pci_device* dev = &pci_devices[num_pci_devices];
    dev->vendor_id = vendor;
    dev->device_id = device;

    // Read class info
    uint32_t class_info = pci_read_config(bus, slot, func, 0x8);
    dev->class_code = (class_info >> 24) & 0xFF;
    dev->subclass = (class_info >> 16) & 0xFF;
    dev->prog_if = (class_info >> 8) & 0xFF;
    dev->revision = class_info & 0xFF;

    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;

    // Read BARs
    for (int i = 0; i < 6; i++) {
        dev->bar[i] = pci_read_config(bus, slot, func, PCI_BAR0 + (i * 4));
    }

    num_pci_devices++;
}

static void pci_scan_slot(uint8_t bus, uint8_t slot) {
    uint32_t vendor_device = pci_read_config(bus, slot, 0, 0);
    if ((vendor_device & 0xFFFF) == 0xFFFF) return;

    pci_scan_function(bus, slot, 0);

    // Check if multi-function device
    uint32_t header_type = pci_read_config(bus, slot, 0, PCI_HEADER_TYPE);
    if ((header_type & 0x80) != 0) {
        for (uint8_t func = 1; func < PCI_MAX_FUNC; func++) {
            vendor_device = pci_read_config(bus, slot, func, 0);
            if ((vendor_device & 0xFFFF) != 0xFFFF) {
                pci_scan_function(bus, slot, func);
            }
        }
    }
}

void pci_init(void) {
    num_pci_devices = 0;
    memset(pci_devices, 0, sizeof(pci_devices));

    // Try to get MCFG base from ACPI
    pci_mcfg_base = acpi_get_mcfg_base();

    // Scan all PCI buses
    for (uint16_t bus = 0; bus < PCI_MAX_BUS; bus++) {
        for (uint8_t slot = 0; slot < PCI_MAX_SLOT; slot++) {
            pci_scan_slot(bus, slot);
        }
    }
}

struct pci_device* pci_scan_for_class(uint8_t class, uint8_t subclass) {
    for (int i = 0; i < num_pci_devices; i++) {
        if (pci_devices[i].class_code == class &&
            pci_devices[i].subclass == subclass) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

uint32_t pci_get_bar(struct pci_device* dev, int bar_num) {
    if (!dev || bar_num >= 6) return 0;

    uint32_t bar = dev->bar[bar_num];

    // Check if I/O or Memory BAR
    if (bar & 1) {
        return bar & 0xFFFFFFFC;  // I/O BAR
    }
    return bar & 0xFFFFFFF0;  // Memory BAR
}