#include <core/drivers/usb/xhci.h>
#include <core/drivers/pci.h>
#include <utils/io.h>
#include <stddef.h>

static volatile uint32_t* xhci_mmio = NULL;
static struct pci_device* xhci_device = NULL;

static inline uint32_t xhci_read32(uint32_t reg) {
    if (!xhci_mmio) return 0;
    return xhci_mmio[reg/4];
}

static inline void xhci_write32(uint32_t reg, uint32_t val) {
    if (!xhci_mmio) return;
    xhci_mmio[reg/4] = val;
}

bool xhci_probe(void) {
    xhci_device = pci_scan_for_class(PCI_CLASS_SERIAL_USB, PCI_SUBCLASS_USB);
    if (!xhci_device || xhci_device->prog_if != PCI_PROGIF_XHCI) {
        return false;
    }

    uint32_t mmio_base = pci_get_bar(xhci_device, 0);
    if (!mmio_base) {
        return false;
    }

    xhci_mmio = (volatile uint32_t*)mmio_base;
    return true;
}

void xhci_init(void) {
    if (!xhci_probe()) {
        return;
    }

    // Basic initialization
    xhci_write32(XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    uint32_t cmd = xhci_read32(XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_RUN | XHCI_CMD_INTE;
    xhci_write32(XHCI_OP_USBCMD, cmd);
}

void xhci_start(void) {
    if (!xhci_mmio) return;
    uint32_t cmd = xhci_read32(XHCI_OP_USBCMD);
    if (!(cmd & XHCI_CMD_RUN)) {
        cmd |= XHCI_CMD_RUN;
        xhci_write32(XHCI_OP_USBCMD, cmd);
    }
}

void xhci_stop(void) {
    if (!xhci_mmio) return;
    uint32_t cmd = xhci_read32(XHCI_OP_USBCMD);
    cmd &= ~XHCI_CMD_RUN;
    xhci_write32(XHCI_OP_USBCMD, cmd);
}