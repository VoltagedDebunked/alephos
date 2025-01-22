#include <core/drivers/usb/xhci.h>
#include <core/drivers/pci.h>
#include <utils/io.h>
#include <utils/mem.h>
#include <utils/log.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <mm/vmm.h>

#define TRB_SIZE 16
#define EVENT_RING_SIZE 256
#define TRANSFER_RING_SIZE 256

// TRB types
enum {
    TRB_NORMAL = 1,
    TRB_SETUP = 2,
    TRB_DATA = 3,
    TRB_STATUS = 4,
    TRB_LINK = 6,
    TRB_EVENT = 32,
    TRB_PORT_STATUS = 34
};

// TRB completion codes
enum {
    TRB_SUCCESS = 1,
    TRB_DATA_BUFFER_ERROR = 2,
    TRB_BABBLE_ERROR = 3,
    TRB_USB_TRANSACTION_ERROR = 4,
    TRB_TRB_ERROR = 5,
    TRB_STALL_ERROR = 6,
    TRB_RESOURCE_ERROR = 7,
    TRB_BANDWIDTH_ERROR = 8,
    TRB_NO_SLOTS_ERROR = 9,
    TRB_INVALID_STREAM_ERROR = 10,
    TRB_SLOT_NOT_ENABLED_ERROR = 11,
    TRB_ENDPOINT_NOT_ENABLED_ERROR = 12,
    TRB_SHORT_PACKET = 13,
    TRB_RING_UNDERRUN = 14,
    TRB_RING_OVERRUN = 15,
    TRB_VF_EVENT_RING_FULL_ERROR = 16,
    TRB_PARAMETER_ERROR = 17,
    TRB_BANDWIDTH_OVERRUN = 18,
    TRB_CONTEXT_STATE_ERROR = 19,
    TRB_NO_PING_RESPONSE_ERROR = 20,
    TRB_EVENT_RING_FULL_ERROR = 21,
    TRB_INCOMPATIBLE_DEVICE_ERROR = 22,
    TRB_MISSED_SERVICE_ERROR = 23,
    TRB_COMMAND_RING_STOPPED = 24,
    TRB_COMMAND_ABORTED = 25,
    TRB_STOPPED = 26,
    TRB_STOPPED_LENGTH_INVALID = 27,
    TRB_MAX_EXIT_LATENCY_TOO_LARGE_ERROR = 29,
    TRB_ISOCH_BUFFER_OVERRUN = 31,
    TRB_EVENT_LOST_ERROR = 32,
    TRB_UNDEFINED_ERROR = 33,
    TRB_INVALID_STREAM_ID_ERROR = 34,
    TRB_SECONDARY_BANDWIDTH_ERROR = 35,
    TRB_SPLIT_TRANSACTION_ERROR = 36
};

struct transfer_ring {
    struct trb {
        uint64_t params;
        uint32_t status;
        uint32_t control;
    } __attribute__((packed)) *trbs;
    uint32_t enqueue_idx;
    uint32_t dequeue_idx;
    uint32_t cycle_bit;
    void* dma_buffer;
};

struct event_ring {
    struct trb* trbs;
    uint32_t* erst;
    uint32_t enqueue_idx;
    uint32_t dequeue_idx;
    uint32_t cycle_bit;
};

struct device_context {
    uint32_t* slots;
    struct transfer_ring* ep_rings[31];
};

static struct xhci_controller* xhci = NULL;
static struct event_ring* event_ring = NULL;
static struct device_context* device_contexts = NULL;

static struct transfer_ring* create_transfer_ring(void) {
    struct transfer_ring* ring = malloc(sizeof(struct transfer_ring));
    if (!ring) return NULL;

    ring->trbs = pmm_alloc_page();
    if (!ring->trbs) {
        free(ring);
        return NULL;
    }

    ring->enqueue_idx = 0;
    ring->dequeue_idx = 0;
    ring->cycle_bit = 1;
    ring->dma_buffer = NULL;

    // Create link TRB at the end
    struct trb* link_trb = &ring->trbs[TRANSFER_RING_SIZE - 1];
    link_trb->params = (uint64_t)ring->trbs;
    link_trb->status = 0;
    link_trb->control = (TRB_LINK << 10) | (1 << 1);

    return ring;
}

static struct event_ring* create_event_ring(void) {
    struct event_ring* ring = malloc(sizeof(struct event_ring));
    if (!ring) return NULL;

    ring->trbs = pmm_alloc_page();
    if (!ring->trbs) {
        free(ring);
        return NULL;
    }

    ring->erst = pmm_alloc_page();
    if (!ring->erst) {
        pmm_free_page(ring->trbs);
        free(ring);
        return NULL;
    }

    ring->erst[0] = (uint32_t)(uint64_t)ring->trbs;
    ring->erst[1] = (uint32_t)((uint64_t)ring->trbs >> 32);
    ring->erst[2] = EVENT_RING_SIZE;
    ring->erst[3] = 0;

    ring->enqueue_idx = 0;
    ring->dequeue_idx = 0;
    ring->cycle_bit = 1;

    return ring;
}

static inline uint32_t xhci_cap_read32(struct xhci_controller* ctrl, uint32_t reg) {
    return ctrl->cap_regs[reg / 4];
}

static inline void xhci_op_write32(struct xhci_controller* ctrl, uint32_t reg, uint32_t val) {
    ctrl->op_regs[reg / 4] = val;
}

static inline uint32_t xhci_op_read32(struct xhci_controller* ctrl, uint32_t reg) {
    return ctrl->op_regs[reg / 4];
}

struct xhci_controller* xhci_init(void) {
    if (xhci) return xhci;

    xhci = malloc(sizeof(struct xhci_controller));
    if (!xhci) return NULL;

    memset(xhci, 0, sizeof(struct xhci_controller));
    return xhci;
}

bool xhci_probe(void) {
    if (!xhci) return false;

    struct pci_device* pci_dev = pci_scan_for_class(0x0C, 0x03);
    if (!pci_dev || pci_dev->prog_if != 0x30) {
        log_error("No xHCI controller found");
        return false;
    }

    uint64_t mmio_base = pci_get_bar(pci_dev, 0);
    if (!mmio_base) {
        log_error("Failed to get xHCI MMIO base");
        return false;
    }

    xhci->cap_regs = (volatile uint32_t*)mmio_base;
    xhci->cap_length = xhci_cap_read32(xhci, XHCI_CAP_HCIVERSION) >> 16;
    xhci->op_regs = (volatile uint32_t*)(mmio_base + xhci->cap_length);

    xhci->hcs_params1 = xhci_cap_read32(xhci, XHCI_CAP_HCSPARAMS1);
    xhci->hcs_params2 = xhci_cap_read32(xhci, XHCI_CAP_HCSPARAMS2);
    xhci->hcs_params3 = xhci_cap_read32(xhci, XHCI_CAP_HCSPARAMS3);
    xhci->hcc_params1 = xhci_cap_read32(xhci, XHCI_CAP_HCCPARAMS1);

    xhci->port_count = (xhci->hcs_params1 >> 24) & 0xFF;

    uint32_t cmd = pci_read_config(pci_dev->bus, pci_dev->slot, pci_dev->func, 0x04);
    cmd |= (1 << 2);
    pci_write_config(pci_dev->bus, pci_dev->slot, pci_dev->func, 0x04, cmd);

    event_ring = create_event_ring();
    if (!event_ring) {
        log_error("Failed to create event ring");
        return false;
    }

    device_contexts = malloc(sizeof(struct device_context));
    if (!device_contexts) {
        log_error("Failed to allocate device contexts");
        return false;
    }

    memset(device_contexts, 0, sizeof(struct device_context));

    xhci->initialized = true;
    return true;
}

bool xhci_start(struct xhci_controller* ctrl) {
    if (!ctrl || !ctrl->initialized) return false;

    uint32_t cmd = xhci_op_read32(ctrl, XHCI_OP_USBCMD);
    if (cmd & XHCI_CMD_RUN) {
        cmd &= ~XHCI_CMD_RUN;
        xhci_op_write32(ctrl, XHCI_OP_USBCMD, cmd);
        while (!(xhci_op_read32(ctrl, XHCI_OP_USBSTS) & XHCI_STS_HCH));
    }

    cmd = xhci_op_read32(ctrl, XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_HCRST;
    xhci_op_write32(ctrl, XHCI_OP_USBCMD, cmd);

    int timeout = 1000000;
    while (--timeout) {
        if (!(xhci_op_read32(ctrl, XHCI_OP_USBCMD) & XHCI_CMD_HCRST)) {
            break;
        }
    }
    if (!timeout) {
        log_error("xHCI reset timeout");
        return false;
    }

    if (event_ring) {
        xhci_op_write32(ctrl, XHCI_OP_CRCR, (uint32_t)(uint64_t)event_ring->erst);
        xhci_op_write32(ctrl, XHCI_OP_CRCR + 4, (uint32_t)((uint64_t)event_ring->erst >> 32));
    }

    cmd = xhci_op_read32(ctrl, XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_RUN;
    xhci_op_write32(ctrl, XHCI_OP_USBCMD, cmd);

    return true;
}

void xhci_stop(struct xhci_controller* ctrl) {
    if (!ctrl || !ctrl->initialized) return;

    uint32_t cmd = xhci_op_read32(ctrl, XHCI_OP_USBCMD);
    cmd &= ~XHCI_CMD_RUN;
    xhci_op_write32(ctrl, XHCI_OP_USBCMD, cmd);

    while (!(xhci_op_read32(ctrl, XHCI_OP_USBSTS) & XHCI_STS_HCH));
}

void xhci_cleanup(struct xhci_controller* ctrl) {
    if (!ctrl) return;

    if (ctrl->initialized) {
        xhci_stop(ctrl);

        if (event_ring) {
            if (event_ring->trbs) pmm_free_page(event_ring->trbs);
            if (event_ring->erst) pmm_free_page(event_ring->erst);
            free(event_ring);
            event_ring = NULL;
        }

        if (device_contexts) {
            for (int i = 0; i < 31; i++) {
                if (device_contexts->ep_rings[i]) {
                    if (device_contexts->ep_rings[i]->trbs) {
                        pmm_free_page(device_contexts->ep_rings[i]->trbs);
                    }
                    if (device_contexts->ep_rings[i]->dma_buffer) {
                        pmm_free_page(device_contexts->ep_rings[i]->dma_buffer);
                    }
                    free(device_contexts->ep_rings[i]);
                }
            }
            free(device_contexts);
            device_contexts = NULL;
        }
    }

    free(ctrl);
    if (ctrl == xhci) xhci = NULL;
}

static void submit_transfer_trb(struct transfer_ring* ring,
                             uint64_t params, uint32_t status, uint32_t control) {
    struct trb* trb = &ring->trbs[ring->enqueue_idx];
    trb->params = params;
    trb->status = status;
    trb->control = (control & ~1) | ring->cycle_bit;

    ring->enqueue_idx = (ring->enqueue_idx + 1) % (TRANSFER_RING_SIZE - 1);
    if (ring->enqueue_idx == 0) {
        ring->cycle_bit ^= 1;
    }
}

int xhci_control_transfer(struct xhci_controller* ctrl, uint8_t dev_addr,
                         struct xhci_setup_packet* setup,
                         void* data, uint16_t length) {
    if (!ctrl || !ctrl->initialized || !setup) return -1;

    struct transfer_ring* ring = device_contexts->ep_rings[0];
    if (!ring) {
        ring = create_transfer_ring();
        if (!ring) return -1;
        device_contexts->ep_rings[0] = ring;
    }

    // Setup stage
    submit_transfer_trb(ring,
        *(uint64_t*)setup,
        (8 << 16) | (TRB_SETUP << 10),
        (3 << 16) | (1 << 6)
    );

    // Data stage (if any)
    if (length > 0 && data) {
        ring->dma_buffer = pmm_alloc_page();
        if (!ring->dma_buffer) return -1;
        memcpy(ring->dma_buffer, data, length);

        submit_transfer_trb(ring,
            (uint64_t)ring->dma_buffer,
            (length << 16) | (TRB_DATA << 10),
            (1 << 16) | (1 << 6) | ((setup->bmRequestType & 0x80) ? 3 : 2)
        );
    }

    // Status stage
    submit_transfer_trb(ring,
        0,
        (TRB_STATUS << 10),
        (1 << 5) | ((setup->bmRequestType & 0x80) ? 2 : 3)
    );

    // Ring doorbell
    if (ctrl->db_regs) {
        ctrl->db_regs[dev_addr] = 1;
    }

    // Wait for completion
    int timeout = 1000000;
    while (timeout--) {
        if (event_ring->trbs[event_ring->dequeue_idx].control & 1) {
            uint32_t completion = event_ring->trbs[event_ring->dequeue_idx].status >> 24;
            event_ring->dequeue_idx = (event_ring->dequeue_idx + 1) % EVENT_RING_SIZE;

            // Cleanup
            if (ring->dma_buffer) {
                if (completion == TRB_SUCCESS && (setup->bmRequestType & 0x80)) {
                    memcpy(data, ring->dma_buffer, length);
                }
                pmm_free_page(ring->dma_buffer);
                ring->dma_buffer = NULL;
            }

            return (completion == TRB_SUCCESS) ? 0 : -1;
        }
    }

    // Timeout cleanup
    if (ring->dma_buffer) {
        pmm_free_page(ring->dma_buffer);
        ring->dma_buffer = NULL;
    }
    return -1;
}

int xhci_bulk_transfer(struct xhci_controller* ctrl, uint8_t dev_addr,
                      uint8_t endpoint, void* data,
                      uint16_t length, uint32_t timeout) {
    if (!ctrl || !ctrl->initialized || !data || length == 0) return -1;

    uint8_t ep_idx = ((endpoint & 0x80) ? 1 : 2) + (endpoint & 0x0F);
    struct transfer_ring* ring = device_contexts->ep_rings[ep_idx];
    if (!ring) {
        ring = create_transfer_ring();
        if (!ring) return -1;
        device_contexts->ep_rings[ep_idx] = ring;
    }

    // Prepare data buffer
    ring->dma_buffer = pmm_alloc_page();
    if (!ring->dma_buffer) return -1;

    if (!(endpoint & 0x80)) {  // OUT endpoint
        memcpy(ring->dma_buffer, data, length);
    }

    // Submit normal TRB
    submit_transfer_trb(ring,
        (uint64_t)ring->dma_buffer,
        (length << 16) | (TRB_NORMAL << 10),
        (1 << 16) | ((endpoint & 0x80) ? 3 : 2)
    );

    // Ring doorbell
    if (ctrl->db_regs) {
        ctrl->db_regs[dev_addr] = ep_idx;
    }

    // Wait for completion
    while (timeout--) {
        if (event_ring->trbs[event_ring->dequeue_idx].control & 1) {
            uint32_t completion = event_ring->trbs[event_ring->dequeue_idx].status >> 24;
            event_ring->dequeue_idx = (event_ring->dequeue_idx + 1) % EVENT_RING_SIZE;

            // Cleanup and copy data for IN transfers
            if (ring->dma_buffer) {
                if (completion == TRB_SUCCESS && (endpoint & 0x80)) {
                    memcpy(data, ring->dma_buffer, length);
                }
                pmm_free_page(ring->dma_buffer);
                ring->dma_buffer = NULL;
            }

            return (completion == TRB_SUCCESS) ? 0 : -1;
        }
    }

    // Timeout cleanup
    if (ring->dma_buffer) {
        pmm_free_page(ring->dma_buffer);
        ring->dma_buffer = NULL;
    }
    return -1;
}

int xhci_interrupt_transfer(struct xhci_controller* ctrl, uint8_t dev_addr,
                           uint8_t endpoint, void* data,
                           uint16_t length, uint32_t timeout) {
    if (!ctrl || !ctrl->initialized || !data || length == 0) return -1;

    // Interrupt transfers use the same mechanism as bulk transfers
    return xhci_bulk_transfer(ctrl, dev_addr, endpoint, data, length, timeout);
}

uint32_t xhci_get_port_count(struct xhci_controller* ctrl) {
    if (!ctrl || !ctrl->initialized) return 0;
    return ctrl->port_count;
}

bool xhci_get_port_status_change(struct xhci_controller* ctrl,
                                uint32_t* status, uint32_t num_ports) {
    if (!ctrl || !ctrl->initialized || !status ||
        num_ports > ctrl->port_count) return false;

    volatile uint32_t* port_regs = (volatile uint32_t*)((uint8_t*)ctrl->op_regs + 0x400);

    for (uint32_t i = 0; i < num_ports; i++) {
        status[i] = port_regs[i * 4] & XHCI_PORT_CHANGE_BITS;
        if (status[i]) {
            // Clear change bits by writing them back
            port_regs[i * 4] = (port_regs[i * 4] & ~XHCI_PORT_CHANGE_BITS) | status[i];
        }
    }

    return true;
}