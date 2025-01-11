#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>
#include <stdbool.h>

// xHCI PCI Configuration
#define PCI_CLASS_SERIAL_USB     0x0C
#define PCI_SUBCLASS_USB         0x03
#define PCI_PROGIF_XHCI         0x30

// xHCI Capability Registers
#define XHCI_CAP_HCIVERSION     0x00
#define XHCI_CAP_HCSPARAMS1     0x04
#define XHCI_CAP_HCSPARAMS2     0x08
#define XHCI_CAP_HCSPARAMS3     0x0C
#define XHCI_CAP_HCCPARAMS1     0x10
#define XHCI_CAP_DBOFF          0x14
#define XHCI_CAP_RTSOFF         0x18

// xHCI Operational Registers
#define XHCI_OP_USBCMD          0x00
#define XHCI_OP_USBSTS          0x04
#define XHCI_OP_DNCTRL          0x14
#define XHCI_OP_CRCR            0x18
#define XHCI_OP_DCBAAP          0x30
#define XHCI_OP_CONFIG          0x38

// xHCI Command Register bits
#define XHCI_CMD_RUN            (1 << 0)
#define XHCI_CMD_HCRST          (1 << 1)
#define XHCI_CMD_INTE           (1 << 2)
#define XHCI_CMD_HSEE           (1 << 3)

// xHCI Status Register bits
#define XHCI_STS_HCH            (1 << 0)
#define XHCI_STS_HSE            (1 << 2)
#define XHCI_STS_EINT           (1 << 3)
#define XHCI_STS_PCD            (1 << 4)
#define XHCI_STS_CNR            (1 << 11)

void xhci_init(void);
bool xhci_probe(void);
void xhci_start(void);
void xhci_stop(void);

#endif // XHCI_H