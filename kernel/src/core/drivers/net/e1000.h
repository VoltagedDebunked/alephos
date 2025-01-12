#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <stdbool.h>
#include <core/drivers/net/netdev.h>

// PCI Device IDs for Intel E1000
#define E1000_VENDOR_ID         0x8086  // Intel
#define E1000_DEVICE_ID         0x100E  // 82540EM Gigabit Ethernet Controller
#define E1000_DEVICE_ID_I217    0x153A  // I217-LM Gigabit Ethernet Controller

// E1000 Register Offsets
#define E1000_CTRL        0x0000  // Device Control
#define E1000_STATUS      0x0008  // Device Status
#define E1000_EECD        0x0010  // EEPROM/Flash Control/Data
#define E1000_EERD        0x0014  // EEPROM Read
#define E1000_ICR         0x00C0  // Interrupt Cause Read
#define E1000_IMS         0x00D0  // Interrupt Mask Set
#define E1000_IMC         0x00D8  // Interrupt Mask Clear
#define E1000_RCTL        0x0100  // Receive Control
#define E1000_TCTL        0x0400  // Transmit Control
#define E1000_RDBAL       0x2800  // RX Descriptor Base Low
#define E1000_RDBAH       0x2804  // RX Descriptor Base High
#define E1000_RDLEN       0x2808  // RX Descriptor Length
#define E1000_RDH         0x2810  // RX Descriptor Head
#define E1000_RDT         0x2818  // RX Descriptor Tail
#define E1000_TDBAL       0x3800  // TX Descriptor Base Low
#define E1000_TDBAH       0x3804  // TX Descriptor Base High
#define E1000_TDLEN       0x3808  // TX Descriptor Length
#define E1000_TDH         0x3810  // TX Descriptor Head
#define E1000_TDT         0x3818  // TX Descriptor Tail
#define E1000_RAL         0x5400  // Receive Address Low
#define E1000_RAH         0x5404  // Receive Address High

// Control Register bits
#define E1000_CTRL_FD     0x00000001  // Full Duplex
#define E1000_CTRL_ASDE   0x00000020  // Auto-Speed Detection Enable
#define E1000_CTRL_SLU    0x00000040  // Set Link Up
#define E1000_CTRL_ILOS   0x00000080  // Invert Loss of Signal
#define E1000_CTRL_RST    0x04000000  // Device Reset
#define E1000_CTRL_VME    0x40000000  // VLAN Mode Enable

// Receive Control Register bits
#define E1000_RCTL_EN     0x00000002  // Receiver Enable
#define E1000_RCTL_SBP    0x00000004  // Store Bad Packets
#define E1000_RCTL_UPE    0x00000008  // Unicast Promiscuous Enable
#define E1000_RCTL_MPE    0x00000010  // Multicast Promiscuous Enable
#define E1000_RCTL_LBM    0x00000C00  // Loopback Mode
#define E1000_RCTL_RDMTS  0x00000300  // RX Descriptor Minimum Threshold Size
#define E1000_RCTL_BSIZE  0x00030000  // Buffer Size (2048 bytes)
#define E1000_RCTL_SECRC  0x04000000  // Strip Ethernet CRC

// Transmit Control Register bits
#define E1000_TCTL_EN     0x00000002  // Transmit Enable
#define E1000_TCTL_PSP    0x00000008  // Pad Short Packets
#define E1000_TCTL_CT     0x00000100  // Collision Threshold
#define E1000_TCTL_COLD   0x00040000  // Collision Distance

// Transmit Descriptor bits
#define E1000_TXD_STAT_DD    0x00000001  // Descriptor Done
#define E1000_TXD_CMD_EOP    0x00000001  // End of Packet
#define E1000_TXD_CMD_RS     0x00000008  // Report Status

// Buffer Sizes
#define E1000_BUFFER_SIZE 2048
#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 32

// Descriptor structure for receive and transmit
struct e1000_rx_desc {
    uint64_t addr;       // Buffer Address
    uint16_t length;     // Length of received data
    uint16_t checksum;   // Checksum
    uint8_t status;      // Descriptor status
    uint8_t errors;      // Descriptor Errors
    uint16_t special;    // Special field
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;       // Buffer Address
    uint16_t length;     // Data length
    uint8_t cso;        // Checksum offset
    uint8_t cmd;        // Descriptor command
    uint8_t status;     // Descriptor status
    uint8_t css;        // Checksum start
    uint16_t special;    // Special field
} __attribute__((packed));

// Driver functions
bool e1000_init(struct netdev* dev);
bool e1000_send_packet(struct netdev* dev, const void* data, uint16_t length);
bool e1000_receive_packet(struct netdev* dev, void* buffer, uint16_t* length);

#endif // E1000_H