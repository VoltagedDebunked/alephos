#include <core/drivers/storage/nvme.h>
#include <core/drivers/pci.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <utils/mem.h>
#include <core/idt.h>

// Global NVMe devices array
static nvme_device_t nvme_devices[NVME_MAX_DEVICES];
static uint32_t num_nvme_devices = 0;

// Internal helper macros
#define NVME_READ_REG32(device, offset) \
    (*(volatile uint32_t*)((uint8_t*)(device)->mmio_base + (offset)))
#define NVME_WRITE_REG32(device, offset, value) \
    (*(volatile uint32_t*)((uint8_t*)(device)->mmio_base + (offset)) = (value))
#define NVME_READ_REG64(device, offset) \
    (*(volatile uint64_t*)((uint8_t*)(device)->mmio_base + (offset)))
#define NVME_WRITE_REG64(device, offset, value) \
    (*(volatile uint64_t*)((uint8_t*)(device)->mmio_base + (offset)) = (value))

// NVMe Register Offsets
#define NVME_REG_CAP        0x0000  // Controller Capabilities
#define NVME_REG_VS         0x0008  // Version
#define NVME_REG_INTMS      0x000C  // Interrupt Mask Set
#define NVME_REG_INTMC      0x0010  // Interrupt Mask Clear
#define NVME_REG_CC         0x0014  // Controller Configuration
#define NVME_REG_CSTS       0x001C  // Controller Status
#define NVME_REG_AQA        0x0024  // Admin Queue Attributes
#define NVME_REG_ASQ        0x0028  // Admin Submission Queue Base Address
#define NVME_REG_ACQ        0x0030  // Admin Completion Queue Base Address
#define NVME_REG_SQ0TDBL    0x1000  // Submission Queue 0 Tail Doorbell
#define NVME_REG_CQ0HDBL    0x1004  // Completion Queue 0 Head Doorbell
#define NVME_REG_SQ0TDT     0x1000  // Submission Queue 0 Tail Doorbell
#define NVME_REG_CQ0HDT     0x1004  // Completion Queue 0 Head Doorbell

// Allocate memory for NVMe queues
static void* nvme_alloc_queue(size_t size) {
    void* queue = pmm_alloc_page();
    if (!queue) return NULL;
    memset(queue, 0, PAGE_SIZE);
    return queue;
}

// Submit an NVMe command
static nvme_result_t nvme_submit_command(nvme_device_t* device, nvme_sq_entry_t* cmd) {
    nvme_queue_t* sq = &device->admin_sq;
    nvme_queue_t* cq = &device->admin_cq;

    // Copy command to submission queue
    nvme_sq_entry_t* sq_entry = (nvme_sq_entry_t*)sq->sq_base + sq->sq_tail;
    memcpy(sq_entry, cmd, sizeof(nvme_sq_entry_t));

    // Update submission queue tail
    sq->sq_tail = (sq->sq_tail + 1) % sq->queue_size;
    NVME_WRITE_REG32(device, NVME_REG_SQ0TDT, sq->sq_tail);

    // Wait for completion (simplistic approach)
    uint64_t timeout = 100000;
    while (timeout--) {
        nvme_cq_entry_t* cq_entry = (nvme_cq_entry_t*)cq->cq_base + cq->cq_head;

        // Check if command is complete
        if (cq_entry->status != 0) {
            // Check for successful completion
            if ((cq_entry->status & 0x7F00) == 0) {
                // Clear completion queue head
                cq->cq_head = (cq->cq_head + 1) % cq->queue_size;
                NVME_WRITE_REG32(device, NVME_REG_CQ0HDT, cq->cq_head);
                return NVME_SUCCESS;
            }
            return NVME_ERR_IO;
        }
    }

    return NVME_ERR_TIMEOUT;
}

// Initialize NVMe subsystem
nvme_result_t nvme_init(void) {
    struct pci_device* dev;
    while ((dev = pci_scan_for_class(PCI_CLASS_STORAGE, 0x08)) != NULL) {
        nvme_result_t result = nvme_probe_device(dev);
        if (result != NVME_SUCCESS) {
            continue;
        }
    }

    return num_nvme_devices > 0 ? NVME_SUCCESS : NVME_ERR_NOT_FOUND;
}

// Probe and initialize a single NVMe device
nvme_result_t nvme_probe_device(struct pci_device* pci_dev) {
   // Check if we've reached maximum device limit
   if (num_nvme_devices >= NVME_MAX_DEVICES) {
       return NVME_ERR_QUEUE_FULL;
   }

   // Get a reference to the next available NVMe device
   nvme_device_t* device = &nvme_devices[num_nvme_devices];

   // Clear device structure
   memset(device, 0, sizeof(nvme_device_t));

   // Store PCI device information
   device->pci_dev = pci_dev;

   // Enable PCI device
   uint32_t cmd = pci_read_config(pci_dev->bus, pci_dev->slot, pci_dev->func, 0x04);
   cmd |= (1 << 2) | (1 << 1);  // Bus Master Enable, Memory Space Enable
   pci_write_config(pci_dev->bus, pci_dev->slot, pci_dev->func, 0x04, cmd);

   // Get memory-mapped base address
   uint64_t mmio_base = pci_get_bar(pci_dev, 0);
   if (!mmio_base) {
       return NVME_ERR_INITIALIZATION;
   }

   // Map MMIO space
   device->mmio_base = (volatile void*)vmm_get_phys_addr(mmio_base);

   // Reset the controller
   NVME_WRITE_REG32(device, NVME_REG_CC, 0);

   // Wait for controller to become disabled
   uint64_t timeout = 100000;
   while ((NVME_READ_REG32(device, NVME_REG_CSTS) & 0x1) && timeout--) {
       for (int i = 0; i < 1000; i++) {
           __asm__ volatile("pause");
       }
   }

   // Allocate admin submission queue
   device->admin_sq.sq_base = nvme_alloc_queue(PAGE_SIZE);
   if (!device->admin_sq.sq_base) {
       return NVME_ERR_INITIALIZATION;
   }

   // Allocate admin completion queue
   device->admin_cq.cq_base = nvme_alloc_queue(PAGE_SIZE);
   if (!device->admin_cq.cq_base) {
       return NVME_ERR_INITIALIZATION;
   }

   // Allocate page-aligned physical buffer for controller identify data
   void* identify_buffer = pmm_alloc_page();
   if (!identify_buffer) {
       return NVME_ERR_INITIALIZATION;
   }

   // Configure queue parameters
   device->admin_sq.queue_size = NVME_QUEUE_DEPTH;
   device->admin_cq.queue_size = NVME_QUEUE_DEPTH;

   // Set queue base addresses
   NVME_WRITE_REG64(device, NVME_REG_ASQ, (uint64_t)device->admin_sq.sq_base);
   NVME_WRITE_REG64(device, NVME_REG_ACQ, (uint64_t)device->admin_cq.cq_base);

   // Set queue attributes
   uint32_t aqa = ((device->admin_sq.queue_size - 1) << 16) |
                  (device->admin_cq.queue_size - 1);
   NVME_WRITE_REG32(device, NVME_REG_AQA, aqa);

   // Enable controller
   uint32_t cc = (3 << 14) |   // CSS: NVMe command set
                 (0 << 11) |   // AMS: Round Robin
                 (6 << 7)  |   // MPS: Page Size
                 (0 << 4)  |   // CSS: NVMe command set
                 (1 << 0);     // Enable
   NVME_WRITE_REG32(device, NVME_REG_CC, cc);

   // Wait for controller to become ready
   timeout = 100000;
   while (!(NVME_READ_REG32(device, NVME_REG_CSTS) & 0x1) && timeout--) {
       for (int i = 0; i < 1000; i++) {
           __asm__ volatile("pause");
       }
   }

   // Check if controller initialization timed out
   if (timeout == 0) {
       pmm_free_page(identify_buffer);
       return NVME_ERR_TIMEOUT;
   }

   // Prepare Identify command
   nvme_sq_entry_t identify_cmd = {0};
   identify_cmd.cdw0 = (NVME_ADMIN_IDENTIFY << 16);
   identify_cmd.dptr = (uint64_t)identify_buffer;  // Use physical address directly
   identify_cmd.cdw10 = NVME_IDENTIFY_CONTROLLER;

   // Submit Identify command
   nvme_result_t result = nvme_submit_command(device, &identify_cmd);
   if (result != NVME_SUCCESS) {
       pmm_free_page(identify_buffer);
       return NVME_ERR_INITIALIZATION;
   }

   // Copy identify data to controller_info
   memcpy(&device->controller_info, identify_buffer, sizeof(nvme_controller_info_t));

   // Free the temporary buffer
   pmm_free_page(identify_buffer);

   // Mark device as initialized
   device->initialized = true;
   num_nvme_devices++;

   return NVME_SUCCESS;
}

// Read from NVMe Namespace
nvme_result_t nvme_read(nvme_device_t* device, uint32_t nsid,
                        uint64_t lba, uint32_t blocks, void* buffer) {
    if (!device || !device->initialized || !buffer) {
        return NVME_ERR_INVALID_PARAM;
    }

    nvme_sq_entry_t read_cmd = {0};
    read_cmd.cdw0 = (NVME_CMD_READ << 16);
    read_cmd.nsid = nsid;
    read_cmd.dptr = (uint64_t)buffer;
    read_cmd.cdw10 = (uint32_t)lba;
    read_cmd.cdw11 = (uint32_t)(lba >> 32);
    read_cmd.cdw12 = blocks - 1;

    return nvme_submit_command(device, &read_cmd);
}

// Write to NVMe Namespace
nvme_result_t nvme_write(nvme_device_t* device, uint32_t nsid,
                         uint64_t lba, uint32_t blocks, const void* buffer) {
    if (!device || !device->initialized || !buffer) {
        return NVME_ERR_INVALID_PARAM;
    }

    nvme_sq_entry_t write_cmd = {0};
    write_cmd.cdw0 = (NVME_CMD_WRITE << 16);
    write_cmd.nsid = nsid;
    write_cmd.dptr = (uint64_t)buffer;
    write_cmd.cdw10 = (uint32_t)lba;
    write_cmd.cdw11 = (uint32_t)(lba >> 32);
    write_cmd.cdw12 = blocks - 1;

    return nvme_submit_command(device, &write_cmd);
}

// Get NVMe Device by Index
nvme_device_t* nvme_get_device(uint32_t index) {
    return (index < num_nvme_devices) ? &nvme_devices[index] : NULL;
}