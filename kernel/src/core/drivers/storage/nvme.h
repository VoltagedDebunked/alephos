#ifndef NVME_H
#define NVME_H

#include <stdint.h>
#include <stdbool.h>
#include <core/drivers/pci.h>

// Maximum number of NVMe devices and queues
#define NVME_MAX_DEVICES     16
#define NVME_MAX_NAMESPACES  32
#define NVME_MAX_QUEUES      64
#define NVME_QUEUE_DEPTH     256

// NVMe Command Opcodes
typedef enum {
    NVME_CMD_FLUSH            = 0x00,
    NVME_CMD_WRITE            = 0x01,
    NVME_CMD_READ             = 0x02,
    NVME_CMD_WRITE_UNCORRECT  = 0x04,
    NVME_CMD_COMPARE          = 0x05,
    NVME_CMD_WRITE_ZEROES     = 0x08,
    NVME_CMD_DATASET_MGMT     = 0x09,
} nvme_io_command_t;

// NVMe Admin Command Opcodes
typedef enum {
    NVME_ADMIN_DELETE_SQ       = 0x00,
    NVME_ADMIN_CREATE_SQ       = 0x01,
    NVME_ADMIN_GET_LOG_PAGE    = 0x02,
    NVME_ADMIN_DELETE_CQ       = 0x04,
    NVME_ADMIN_CREATE_CQ       = 0x05,
    NVME_ADMIN_IDENTIFY        = 0x06,
    NVME_ADMIN_ABORT           = 0x08,
    NVME_ADMIN_SET_FEATURES    = 0x09,
    NVME_ADMIN_GET_FEATURES    = 0x0A,
} nvme_admin_command_t;

// Identify CNS (Controller or Namespace Structure) Types
typedef enum {
    NVME_IDENTIFY_NAMESPACE    = 0x00,
    NVME_IDENTIFY_CONTROLLER   = 0x01,
    NVME_IDENTIFY_ACTIVE_NSIDS = 0x02,
} nvme_identify_type_t;

// NVMe Namespace Attributes
typedef struct {
    uint64_t nsize;     // Total Namespace Size
    uint64_t ncap;      // Namespace Capacity
    uint64_t nuse;      // Namespace Utilization
    uint8_t nsfeat;     // Namespace Features
    uint8_t nlbaf;      // Number of LBA Formats
    uint8_t flbas;      // Formatted LBA Size
    uint8_t mc;         // Metadata Capabilities
    uint8_t dpc;        // Data Protection Capabilities
    uint8_t dps;        // Data Protection Settings
    uint8_t nmic;       // Namespace Multipath
    uint8_t rescap;     // Reservation Capabilities
    uint8_t fpi;        // Format Progress Indicator
} __attribute__((packed)) nvme_namespace_info_t;

// NVMe Controller Attributes
typedef struct {
    uint16_t vid;       // PCI Vendor ID
    uint16_t ssvid;     // PCI Subsystem Vendor ID
    char sn[20];        // Serial Number
    char mn[40];        // Model Number
    char fr[8];         // Firmware Revision
    uint8_t rab;        // Recommended Arbitration Burst
    uint8_t ieee[3];    // IEEE OUI Identifier
    uint8_t cmic;       // Controller Multi-Path ID
    uint8_t mdts;       // Maximum Data Transfer Size
    uint16_t cntlid;    // Controller ID
    uint32_t version;   // Version
} __attribute__((packed)) nvme_controller_info_t;

// NVMe Queue Structure
typedef struct {
    void* sq_base;      // Submission Queue Base Address
    void* cq_base;      // Completion Queue Base Address
    uint16_t sq_head;   // Submission Queue Head Pointer
    uint16_t sq_tail;   // Submission Queue Tail Pointer
    uint16_t cq_head;   // Completion Queue Head Pointer
    uint16_t queue_size;// Queue Depth
    bool initialized;   // Queue Initialization Status
} nvme_queue_t;

// NVMe Device Structure
typedef struct {
    struct pci_device* pci_dev;     // PCI Device Information
    volatile void* mmio_base;       // Memory-Mapped I/O Base Address

    // Controller Information
    nvme_controller_info_t controller_info;

    // Namespace Information
    nvme_namespace_info_t namespaces[NVME_MAX_NAMESPACES];
    uint32_t num_namespaces;

    // Queues
    nvme_queue_t admin_sq;          // Admin Submission Queue
    nvme_queue_t admin_cq;          // Admin Completion Queue
    nvme_queue_t io_queues[NVME_MAX_QUEUES];

    // Capabilities
    uint64_t capabilities;
    uint32_t page_size;
    uint32_t max_data_transfer;

    // Status
    bool initialized;
    uint8_t interrupt_vector;
} nvme_device_t;

// NVMe Command Submission Structures
typedef struct {
    uint32_t cdw0;      // Command Dword 0 (Opcode, Flag, Command ID)
    uint32_t nsid;      // Namespace ID
    uint64_t rsvd;      // Reserved
    uint64_t mptr;      // Metadata Pointer
    uint64_t dptr;      // Data Pointer
    uint32_t cdw10;     // Command Dword 10
    uint32_t cdw11;     // Command Dword 11
    uint32_t cdw12;     // Command Dword 12
    uint32_t cdw13;     // Command Dword 13
    uint32_t cdw14;     // Command Dword 14
    uint32_t cdw15;     // Command Dword 15
} __attribute__((packed)) nvme_sq_entry_t;

typedef struct {
    uint32_t result;    // Command-specific result
    uint32_t rsvd;      // Reserved
    uint16_t sq_head;   // Submission Queue Head Pointer
    uint16_t sq_id;     // Submission Queue ID
    uint16_t command_id;// Command ID
    uint16_t status;    // Status Field
} __attribute__((packed)) nvme_cq_entry_t;

// Error codes for NVMe operations
typedef enum {
    NVME_SUCCESS = 0,
    NVME_ERR_NOT_FOUND = -1,
    NVME_ERR_INITIALIZATION = -2,
    NVME_ERR_QUEUE_FULL = -3,
    NVME_ERR_INVALID_PARAM = -4,
    NVME_ERR_TIMEOUT = -5,
    NVME_ERR_IO = -6
} nvme_result_t;

// Public API Function Declarations
nvme_result_t nvme_init(void);
nvme_device_t* nvme_get_device(uint32_t index);
nvme_result_t nvme_read(nvme_device_t* device, uint32_t nsid,
                        uint64_t lba, uint32_t blocks, void* buffer);
nvme_result_t nvme_write(nvme_device_t* device, uint32_t nsid,
                         uint64_t lba, uint32_t blocks, const void* buffer);
nvme_result_t nvme_identify_namespace(nvme_device_t* device, uint32_t nsid,
                                      nvme_namespace_info_t* info);
nvme_result_t nvme_identify_controller(nvme_device_t* device,
                                       nvme_controller_info_t* info);
nvme_result_t nvme_probe_device(struct pci_device* pci_dev);

#endif // NVME_H