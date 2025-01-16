#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <core/drivers/pci.h>

// Hardware Registers
#define WIFI_REG_CSR             0x0000  // Control and Status
#define WIFI_REG_INT_ENABLE      0x0008  // Interrupt Enable
#define WIFI_REG_INT_STATUS      0x000C  // Interrupt Status
#define WIFI_REG_RESET           0x0020  // Device Reset
#define WIFI_REG_EEPROM_CTRL     0x0024  // EEPROM Control
#define WIFI_REG_EEPROM_DATA     0x0028  // EEPROM Data
#define WIFI_REG_GPIO_CTRL       0x002C  // GPIO Control
#define WIFI_REG_MEM_CTRL        0x0030  // Memory Control
#define WIFI_REG_POWER_CTRL      0x0034  // Power Management
#define WIFI_REG_MAC_ADDR_0      0x0040  // MAC Address Low 32 bits
#define WIFI_REG_MAC_ADDR_1      0x0044  // MAC Address High 16 bits
#define WIFI_REG_TX_BASE         0x0100  // TX DMA Base Address
#define WIFI_REG_TX_SIZE         0x0104  // TX Ring Size
#define WIFI_REG_RX_BASE         0x0108  // RX DMA Base Address
#define WIFI_REG_RX_SIZE         0x010C  // RX Ring Size
#define WIFI_REG_TX_HEAD         0x0110  // TX Ring Head
#define WIFI_REG_TX_TAIL         0x0114  // TX Ring Tail
#define WIFI_REG_RX_HEAD         0x0118  // RX Ring Head
#define WIFI_REG_RX_TAIL         0x011C  // RX Ring Tail
#define WIFI_REG_FW_BASE         0x1000  // Firmware Base Address

// RF/Baseband Registers
#define WIFI_REG_RF_FSK_CAL      0x0200  // RF FSK Calibration
#define WIFI_REG_RF_VCO          0x0204  // RF VCO Control
#define WIFI_REG_RF_FILTER       0x0208  // RF Filter Control
#define WIFI_REG_RF_GAIN_BASE    0x0210  // RF Gain Tables Base
#define WIFI_REG_RF_PA_CTRL      0x0220  // Power Amplifier Control
#define WIFI_REG_BB_RESET        0x0300  // Baseband Reset
#define WIFI_REG_BB_ADC_CTRL     0x0304  // ADC Control
#define WIFI_REG_BB_DAC_CTRL     0x0308  // DAC Control
#define WIFI_REG_BB_AGC          0x030C  // AGC Control
#define WIFI_REG_BB_FILTER       0x0310  // Baseband Filter

// MAC Layer Registers
#define WIFI_REG_MAC_CONFIG      0x0400  // MAC Configuration
#define WIFI_REG_RATE_CTRL       0x0404  // Rate Control
#define WIFI_REG_INT_COAL        0x0408  // Interrupt Coalescing
#define WIFI_REG_REGULATORY      0x040C  // Regulatory Domain
#define WIFI_REG_ANT_DIV         0x0500  // Antenna Diversity
#define WIFI_REG_ANT_GAIN        0x0504  // Antenna Gain
#define WIFI_REG_MIMO_CTRL       0x0508  // MIMO Control

// Interrupt Bits
#define WIFI_INT_TX_DONE         (1 << 0)
#define WIFI_INT_RX_DONE         (1 << 1)
#define WIFI_INT_TX_ERR          (1 << 2)
#define WIFI_INT_RX_ERR          (1 << 3)
#define WIFI_INT_FW_READY        (1 << 4)
#define WIFI_INT_TEMP_WARNING    (1 << 5)
#define WIFI_INT_RF_KILL         (1 << 6)
#define WIFI_INT_BEACON          (1 << 7)

// DMA Descriptor Flags
#define DESC_FLAG_OWN            (1 << 31)  // Hardware owns descriptor
#define DESC_FLAG_INT            (1 << 30)  // Generate interrupt
#define DESC_FLAG_FIRST          (1 << 29)  // First segment
#define DESC_FLAG_LAST           (1 << 28)  // Last segment
#define DESC_FLAG_EOP            (1 << 27)  // End of packet

// Ring sizes and buffer sizes
#define TX_RING_SIZE             256
#define RX_RING_SIZE             256
#define TX_BUFFER_SIZE           2048
#define RX_BUFFER_SIZE           2048

// Firmware constants
#define FW_CHUNK_SIZE            4096
#define FW_MAX_UCODE_SIZE        (128*1024)
#define FW_READY_TIMEOUT         1000

// DMA descriptor structure
struct wifi_dma_desc {
    uint64_t buffer_addr;    // Physical address of buffer
    uint32_t buffer_len;     // Buffer length
    uint32_t flags;          // Control flags
    uint32_t status;         // Status flags
    uint32_t timestamp;      // Packet timestamp
    struct wifi_dma_desc* next;  // Next descriptor
} __attribute__((packed));

// 802.11 frame header
struct wifi_80211_header {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t addr1[6];         // Destination address
    uint8_t addr2[6];         // Source address
    uint8_t addr3[6];         // BSSID
    uint16_t seq_ctrl;
} __attribute__((packed));

// Broadcast MAC address
extern const uint8_t broadcast_addr[6];

// Network packet structure
struct net_packet {
    uint8_t* data;
    uint16_t length;
    struct {
        uint32_t ip;
        uint16_t port;
    } source;
    struct {
        uint32_t ip;
        uint16_t port;
    } destination;
};

// Device structure
struct wifi_device {
    // PCI information
    struct pci_device* pci_dev;
    volatile void* mmio_base;

    // DMA rings
    struct wifi_dma_desc* tx_ring;
    struct wifi_dma_desc* rx_ring;
    void* tx_buffers[TX_RING_SIZE];
    void* rx_buffers[RX_RING_SIZE];
    uint32_t tx_head;
    uint32_t tx_tail;
    uint32_t rx_head;
    uint32_t rx_tail;

    // Device state
    uint8_t mac_addr[6];
    bool hw_ready;
    bool fw_loaded;
    struct netdev* netdev;

    // Statistics
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t rx_dropped;
};

// Core function declarations
bool wifi_init(void);
struct wifi_device* wifi_probe(struct pci_device* pci_dev);
bool wifi_start(struct wifi_device* dev);
void wifi_stop(struct wifi_device* dev);

// Packet handling functions
void wifi_handle_interrupt(struct wifi_device* dev);
bool wifi_transmit_packet(struct wifi_device* dev, const void* data, size_t length);
bool wifi_receive_packet(struct wifi_device* dev, void* buffer, size_t* length);

// Firmware and configuration functions
bool wifi_load_firmware(struct wifi_device* dev, const uint8_t* fw_data, size_t fw_size);

// Internal helper function declarations (used by wifi.c)
static bool setup_dma_rings(struct wifi_device* dev);
static bool reset_device(struct wifi_device* dev);
static bool read_eeprom_calibration(struct wifi_device* dev, uint16_t* cal_data, size_t count);
static bool configure_rf(struct wifi_device* dev, const uint16_t* cal_data);
static bool init_baseband(struct wifi_device* dev);
static bool configure_antenna(struct wifi_device* dev, const uint16_t* cal_data);
static bool read_mac_address(struct wifi_device* dev);

// Network integration
bool netdev_receive(struct netdev* dev, struct net_packet* packet);

#endif // WIFI_H