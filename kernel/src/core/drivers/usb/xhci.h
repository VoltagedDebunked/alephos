#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>
#include <stdbool.h>

// xHCI capability registers
#define XHCI_CAP_HCIVERSION     0x00
#define XHCI_CAP_HCSPARAMS1     0x04
#define XHCI_CAP_HCSPARAMS2     0x08
#define XHCI_CAP_HCSPARAMS3     0x0C
#define XHCI_CAP_HCCPARAMS1     0x10
#define XHCI_CAP_DBOFF          0x14
#define XHCI_CAP_RTSOFF         0x18

// xHCI operational registers
#define XHCI_OP_USBCMD          0x00
#define XHCI_OP_USBSTS          0x04
#define XHCI_OP_PAGESIZE        0x08
#define XHCI_OP_DNCTRL          0x14
#define XHCI_OP_CRCR            0x18
#define XHCI_OP_DCBAAP          0x30
#define XHCI_OP_CONFIG          0x38

// Command register bits
#define XHCI_CMD_RUN            (1 << 0)
#define XHCI_CMD_HCRST          (1 << 1)
#define XHCI_CMD_INTE           (1 << 2)
#define XHCI_CMD_HSEE           (1 << 3)

// Status register bits
#define XHCI_STS_HCH            (1 << 0)
#define XHCI_STS_HSE            (1 << 2)
#define XHCI_STS_EINT           (1 << 3)
#define XHCI_STS_PCD            (1 << 4)
#define XHCI_STS_CNR            (1 << 11)

// Port register bits
#define XHCI_PORT_CCS           (1 << 0)
#define XHCI_PORT_PED           (1 << 1)
#define XHCI_PORT_OCA           (1 << 3)
#define XHCI_PORT_RESET         (1 << 4)
#define XHCI_PORT_PLS_MASK      (0xF << 5)
#define XHCI_PORT_PP            (1 << 9)
#define XHCI_PORT_SPEED_MASK    (0xF << 10)
#define XHCI_PORT_CSC           (1 << 17)
#define XHCI_PORT_PEC           (1 << 18)
#define XHCI_PORT_WRC           (1 << 19)
#define XHCI_PORT_OCC           (1 << 20)
#define XHCI_PORT_PRC           (1 << 21)
#define XHCI_PORT_CHANGE_BITS   (0x7FE000)

// xHCI controller structure
struct xhci_controller {
    volatile uint32_t* cap_regs;     // Capability registers
    volatile uint32_t* op_regs;      // Operational registers
    volatile uint32_t* run_regs;     // Runtime registers
    volatile uint32_t* db_regs;      // Doorbell registers
    uint32_t port_count;             // Number of root hub ports
    uint32_t cap_length;             // Length of capability registers
    uint32_t hcs_params1;            // Structural parameters 1
    uint32_t hcs_params2;            // Structural parameters 2
    uint32_t hcs_params3;            // Structural parameters 3
    uint32_t hcc_params1;            // Capability parameters 1
    bool initialized;                 // Initialization state
};

// xHCI setup packet structure
struct xhci_setup_packet {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

// Function declarations
struct xhci_controller* xhci_init(void);
bool xhci_probe(void);
bool xhci_start(struct xhci_controller* xhci);
void xhci_stop(struct xhci_controller* xhci);
void xhci_cleanup(struct xhci_controller* xhci);

// Device communication
int xhci_control_transfer(struct xhci_controller* xhci, uint8_t dev_addr,
                         struct xhci_setup_packet* setup,
                         void* data, uint16_t length);

int xhci_bulk_transfer(struct xhci_controller* xhci, uint8_t dev_addr,
                      uint8_t endpoint, void* data,
                      uint16_t length, uint32_t timeout);

int xhci_interrupt_transfer(struct xhci_controller* xhci, uint8_t dev_addr,
                           uint8_t endpoint, void* data,
                           uint16_t length, uint32_t timeout);

// Port management
uint32_t xhci_get_port_count(struct xhci_controller* xhci);
bool xhci_get_port_status_change(struct xhci_controller* xhci,
                                uint32_t* status, uint32_t num_ports);

#endif // XHCI_H