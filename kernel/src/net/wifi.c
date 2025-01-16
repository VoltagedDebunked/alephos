#include "wifi.h"
#include <core/drivers/pci.h>
#include <core/idt.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <utils/io.h>
#include <utils/mem.h>

// Register access helpers
static inline uint32_t wifi_read32(struct wifi_device* dev, uint32_t reg) {
    return *(volatile uint32_t*)((uint8_t*)dev->mmio_base + reg);
}

static inline void wifi_write32(struct wifi_device* dev, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)((uint8_t*)dev->mmio_base + reg) = val;
}

// Initialize DMA rings
static bool setup_dma_rings(struct wifi_device* dev) {
    // Allocate TX ring
    dev->tx_ring = pmm_alloc_page();
    if (!dev->tx_ring) return false;
    memset(dev->tx_ring, 0, PAGE_SIZE);

    // Allocate RX ring
    dev->rx_ring = pmm_alloc_page();
    if (!dev->rx_ring) {
        pmm_free_page(dev->tx_ring);
        return false;
    }
    memset(dev->rx_ring, 0, PAGE_SIZE);

    // Allocate TX buffers
    for (int i = 0; i < TX_RING_SIZE; i++) {
        dev->tx_buffers[i] = pmm_alloc_page();
        if (!dev->tx_buffers[i]) {
            // Cleanup on failure
            for (int j = 0; j < i; j++) {
                pmm_free_page(dev->tx_buffers[j]);
            }
            pmm_free_page(dev->tx_ring);
            pmm_free_page(dev->rx_ring);
            return false;
        }

        // Initialize TX descriptor
        struct wifi_dma_desc* desc = &dev->tx_ring[i];
        desc->buffer_addr = (uint64_t)dev->tx_buffers[i];
        desc->buffer_len = TX_BUFFER_SIZE;
        desc->flags = DESC_FLAG_INT;  // Generate interrupt when done
        desc->next = &dev->tx_ring[(i + 1) % TX_RING_SIZE];
    }

    // Allocate RX buffers
    for (int i = 0; i < RX_RING_SIZE; i++) {
        dev->rx_buffers[i] = pmm_alloc_page();
        if (!dev->rx_buffers[i]) {
            // Cleanup on failure
            for (int j = 0; j < i; j++) {
                pmm_free_page(dev->rx_buffers[j]);
            }
            for (int j = 0; j < TX_RING_SIZE; j++) {
                pmm_free_page(dev->tx_buffers[j]);
            }
            pmm_free_page(dev->tx_ring);
            pmm_free_page(dev->rx_ring);
            return false;
        }

        // Initialize RX descriptor
        struct wifi_dma_desc* desc = &dev->rx_ring[i];
        desc->buffer_addr = (uint64_t)dev->rx_buffers[i];
        desc->buffer_len = RX_BUFFER_SIZE;
        desc->flags = DESC_FLAG_OWN | DESC_FLAG_INT;  // Give to hardware
        desc->next = &dev->rx_ring[(i + 1) % RX_RING_SIZE];
    }

    // Program DMA registers
    wifi_write32(dev, WIFI_REG_TX_BASE, (uint32_t)(uint64_t)dev->tx_ring);
    wifi_write32(dev, WIFI_REG_TX_SIZE, TX_RING_SIZE);
    wifi_write32(dev, WIFI_REG_RX_BASE, (uint32_t)(uint64_t)dev->rx_ring);
    wifi_write32(dev, WIFI_REG_RX_SIZE, RX_RING_SIZE);

    // Reset ring pointers
    dev->tx_head = dev->tx_tail = 0;
    dev->rx_head = dev->rx_tail = 0;

    return true;
}

// Reset the hardware
static bool reset_device(struct wifi_device* dev) {
    // Trigger reset
    wifi_write32(dev, WIFI_REG_RESET, 1);

    // Wait for reset completion
    int timeout = 1000;
    while (timeout--) {
        if (!(wifi_read32(dev, WIFI_REG_RESET) & 1)) {
            return true;
        }
        io_wait();
    }

    return false;
}

// Load firmware to device
bool wifi_load_firmware(struct wifi_device* dev, const uint8_t* fw_data, size_t fw_size) {
    if (!dev || !fw_data || fw_size > FW_MAX_UCODE_SIZE) {
        return false;
    }

    // Reset device before loading firmware
    if (!reset_device(dev)) {
        return false;
    }

    // Load firmware in chunks
    for (size_t offset = 0; offset < fw_size; offset += FW_CHUNK_SIZE) {
        size_t chunk_size = (fw_size - offset < FW_CHUNK_SIZE) ?
                            fw_size - offset : FW_CHUNK_SIZE;

        // Write firmware chunk to device memory
        for (size_t i = 0; i < chunk_size; i += 4) {
            uint32_t dword;
            memcpy(&dword, fw_data + offset + i, 4);
            wifi_write32(dev, WIFI_REG_FW_BASE + offset + i, dword);
        }
    }

    // Start firmware execution
    wifi_write32(dev, WIFI_REG_CSR, wifi_read32(dev, WIFI_REG_CSR) | (1 << 15));

    // Wait for firmware ready indication
    int timeout = FW_READY_TIMEOUT;
    while (timeout--) {
        if (wifi_read32(dev, WIFI_REG_INT_STATUS) & WIFI_INT_FW_READY) {
            dev->fw_loaded = true;
            return true;
        }
        io_wait();
    }

    return false;
}

// EEPROM calibration read
static bool read_eeprom_calibration(struct wifi_device* dev, uint16_t* cal_data, size_t count) {
    // Enable EEPROM access
    wifi_write32(dev, WIFI_REG_EEPROM_CTRL, 1);

    // Read calibration data (starts at offset 0x100)
    for (size_t i = 0; i < count; i++) {
        // Set address
        wifi_write32(dev, WIFI_REG_EEPROM_CTRL, (0x100 + i) | (1 << 8));

        // Wait for read completion
        int timeout = 100;
        while (timeout--) {
            if (wifi_read32(dev, WIFI_REG_EEPROM_CTRL) & (1 << 9)) {
                cal_data[i] = wifi_read32(dev, WIFI_REG_EEPROM_DATA) & 0xFFFF;
                break;
            }
            io_wait();
        }
        if (timeout <= 0) return false;
    }

    return true;
}

// Configure RF subsystem
static bool configure_rf(struct wifi_device* dev, const uint16_t* cal_data) {
    // Apply frequency synthesizer calibration
    wifi_write32(dev, WIFI_REG_RF_FSK_CAL, cal_data[0]);

    // Set VCO current
    wifi_write32(dev, WIFI_REG_RF_VCO, cal_data[1]);

    // Configure RF filters
    wifi_write32(dev, WIFI_REG_RF_FILTER,
        (cal_data[2] << 0) |    // Low pass filter
        (cal_data[3] << 16)     // Band pass filter
    );

    // Set RF gain tables
    for (int i = 0; i < 4; i++) {
        wifi_write32(dev, WIFI_REG_RF_GAIN_BASE + i*4,
            (cal_data[4+i*2] << 0) |     // RX gain
            (cal_data[4+i*2+1] << 16)    // TX gain
        );
    }

    // Configure power amplifier
    wifi_write32(dev, WIFI_REG_RF_PA_CTRL,
        (cal_data[12] << 0) |   // Bias current
        (cal_data[13] << 8) |   // Gain
        (1 << 16)               // Enable
    );

    return true;
}

// Initialize baseband processor
static bool init_baseband(struct wifi_device* dev) {
    // Reset baseband processor
    wifi_write32(dev, WIFI_REG_BB_RESET, 1);
    io_wait();
    wifi_write32(dev, WIFI_REG_BB_RESET, 0);

    // Configure ADC/DAC
    wifi_write32(dev, WIFI_REG_BB_ADC_CTRL,
        (1 << 0) |     // Enable
        (0 << 1) |     // Normal mode
        (3 << 2)       // Max gain
    );

    wifi_write32(dev, WIFI_REG_BB_DAC_CTRL,
        (1 << 0) |     // Enable
        (0 << 1)       // Normal mode
    );

    // Configure AGC
    wifi_write32(dev, WIFI_REG_BB_AGC,
        (1 << 0) |     // Enable
        (2 << 1) |     // Medium attack rate
        (4 << 4) |     // Slow decay rate
        (70 << 8)      // Target power (-70 dBm)
    );

    // Set baseband filters
    wifi_write32(dev, WIFI_REG_BB_FILTER,
        (1 << 0) |     // Enable
        (0 << 1) |     // Normal bandwidth
        (0 << 2)       // No notch filter
    );

    return true;
}

// Configure antenna subsystem
static bool configure_antenna(struct wifi_device* dev, const uint16_t* cal_data) {
    // Configure diversity
    wifi_write32(dev, WIFI_REG_ANT_DIV,
        (1 << 0) |     // Enable diversity
        (2 << 1) |     // 2 antennas
        (4 << 4)       // Switch threshold
    );

    // Set antenna gains
    wifi_write32(dev, WIFI_REG_ANT_GAIN,
        (cal_data[14] << 0) |   // Antenna 0 gain
        (cal_data[15] << 8)     // Antenna 1 gain
    );

    // Configure MIMO
    wifi_write32(dev, WIFI_REG_MIMO_CTRL,
        (1 << 0) |     // Enable MIMO
        (1 << 1) |     // Enable beamforming
        (1 << 2)       // Enable spatial multiplexing
    );

    return true;
}

// Read MAC address from EEPROM
static bool read_mac_address(struct wifi_device* dev) {
    // Enable EEPROM access
    wifi_write32(dev, WIFI_REG_EEPROM_CTRL, 1);

    // Read MAC address (usually at offset 0x0E-0x13 in EEPROM)
    for (int i = 0; i < 3; i++) {
        // Set EEPROM address
        wifi_write32(dev, WIFI_REG_EEPROM_CTRL, (0x0E + i) | (1 << 8));

        // Wait for read completion
        int timeout = 100;
        while (timeout--) {
            if (wifi_read32(dev, WIFI_REG_EEPROM_CTRL) & (1 << 9)) {
                uint16_t value = wifi_read32(dev, WIFI_REG_EEPROM_DATA) & 0xFFFF;
                dev->mac_addr[i*2] = value & 0xFF;
                dev->mac_addr[i*2 + 1] = (value >> 8) & 0xFF;
                break;
            }
            io_wait();
        }
        if (timeout <= 0) return false;
    }

    // Program MAC address registers
    uint32_t mac_low = (dev->mac_addr[3] << 24) | (dev->mac_addr[2] << 16) |
                      (dev->mac_addr[1] << 8) | dev->mac_addr[0];
    uint32_t mac_high = (dev->mac_addr[5] << 8) | dev->mac_addr[4];

    wifi_write32(dev, WIFI_REG_MAC_ADDR_0, mac_low);
    wifi_write32(dev, WIFI_REG_MAC_ADDR_1, mac_high);

    return true;
}

// Handle hardware interrupt
void wifi_handle_interrupt(struct wifi_device* dev) {
    uint32_t status = wifi_read32(dev, WIFI_REG_INT_STATUS);

    // Handle TX completions
    if (status & WIFI_INT_TX_DONE) {
        while (dev->tx_head != dev->tx_tail) {
            struct wifi_dma_desc* desc = &dev->tx_ring[dev->tx_head];
            if (desc->flags & DESC_FLAG_OWN) {
                break;  // Hardware still owns this descriptor
            }

            // Process TX completion
            if (desc->status & 0x1) { // Success bit
                dev->tx_packets++;
            } else {
                dev->tx_errors++;
            }

            // Clear descriptor for reuse
            desc->status = 0;
            desc->flags = DESC_FLAG_INT;

            dev->tx_head = (dev->tx_head + 1) % TX_RING_SIZE;
        }
    }

    // Handle RX packets
    if (status & WIFI_INT_RX_DONE) {
        while (1) {
            struct wifi_dma_desc* desc = &dev->rx_ring[dev->rx_head];
            if (desc->flags & DESC_FLAG_OWN) {
                break;  // No more packets
            }

            // Process received packet
            if (desc->status & 0x1) { // Success bit
                // Create network packet
                uint8_t* packet_data = dev->rx_buffers[dev->rx_head];
                uint16_t packet_len = desc->buffer_len;

                // Process 802.11 header
                struct wifi_80211_header* hdr = (struct wifi_80211_header*)packet_data;

                // Check if packet is for us
                if (memcmp(hdr->addr1, dev->mac_addr, 6) == 0 ||
                    memcmp(hdr->addr1, broadcast_addr, 6) == 0) {

                    // Extract payload
                    uint8_t* payload = packet_data + sizeof(struct wifi_80211_header);
                    uint16_t payload_len = packet_len - sizeof(struct wifi_80211_header);

                    if (payload_len > 0) {
                        // Pass to network stack
                        struct net_packet pkt;
                        pkt.data = malloc(payload_len);
                        if (pkt.data) {
                            memcpy(pkt.data, payload, payload_len);
                            pkt.length = payload_len;
                            pkt.source.ip = 0;  // Filled by upper layer
                            pkt.destination.ip = 0;  // Filled by upper layer

                            // Add packet to RX queue
                            if (netdev_receive(dev->netdev, &pkt)) {
                                dev->rx_packets++;
                            } else {
                                free(pkt.data);
                                dev->rx_dropped++;
                            }
                        }
                    }
                }
            } else {
                dev->rx_errors++;
            }

            // Reset descriptor and give back to hardware
            desc->status = 0;
            desc->flags = DESC_FLAG_OWN | DESC_FLAG_INT;

            dev->rx_head = (dev->rx_head + 1) % RX_RING_SIZE;
        }
    }

    // Handle firmware ready notification
    if (status & WIFI_INT_FW_READY) {
        dev->hw_ready = true;
    }

    // Handle errors
    if (status & (WIFI_INT_TX_ERR | WIFI_INT_RX_ERR)) {
        // Reset DMA rings
        reset_device(dev);
        setup_dma_rings(dev);
    }

    // Clear handled interrupts
    wifi_write32(dev, WIFI_REG_INT_STATUS, status);
}

// Initialize the WiFi subsystem
bool wifi_init(void) {
    // Initialize hardware detection
    struct pci_device* pci_dev = NULL;

    // Scan for supported WiFi cards
    while ((pci_dev = pci_scan_for_class(PCI_CLASS_NETWORK, 0x80)) != NULL) {
        // Check vendor/device IDs for supported cards
        switch (pci_dev->vendor_id) {
            case 0x8086:  // Intel
                switch (pci_dev->device_id) {
                    case 0x0082:  // Intel WiFi 6 AX200
                    case 0x2723:  // Intel WiFi 6E AX211
                    case 0x0085:  // Intel WiFi 6E AX210
                        if (wifi_probe(pci_dev)) {
                            return true;
                        }
                        break;
                }
                break;

            case 0x168C:  // Atheros/Qualcomm
                switch (pci_dev->device_id) {
                    case 0x003E:  // QCA6174
                    case 0x0042:  // QCA9377
                    case 0x0046:  // QCA6164
                        if (wifi_probe(pci_dev)) {
                            return true;
                        }
                        break;
                }
                break;

            case 0x14E4:  // Broadcom
                switch (pci_dev->device_id) {
                    case 0x43B1:  // BCM4352
                    case 0x43DC:  // BCM4355
                    case 0x4365:  // BCM43142
                        if (wifi_probe(pci_dev)) {
                            return true;
                        }
                        break;
                }
                break;
        }
    }

    return false;
}

// Probe and initialize a WiFi device
struct wifi_device* wifi_probe(struct pci_device* pci_dev) {
    if (!pci_dev) return NULL;

    // Allocate device structure
    struct wifi_device* dev = malloc(sizeof(struct wifi_device));
    if (!dev) return NULL;
    memset(dev, 0, sizeof(struct wifi_device));

    // Store PCI device info
    dev->pci_dev = pci_dev;

    // Map PCI BAR0 (device registers)
    uint64_t mmio_base = pci_get_bar(pci_dev, 0);
    if (!mmio_base) {
        free(dev);
        return NULL;
    }
    dev->mmio_base = (volatile void*)mmio_base;

    // Perform full hardware initialization sequence

    // 1. Initial reset
    if (!reset_device(dev)) {
        free(dev);
        return NULL;
    }

    // 2. Load calibration data from EEPROM
    uint16_t cal_data[128];
    if (!read_eeprom_calibration(dev, cal_data, sizeof(cal_data)/sizeof(cal_data[0]))) {
        free(dev);
        return NULL;
    }

    // 3. Configure RF subsystem
    if (!configure_rf(dev, cal_data)) {
        free(dev);
        return NULL;
    }

    // 4. Initialize baseband processor
    if (!init_baseband(dev)) {
        free(dev);
        return NULL;
    }

    // 5. Configure default settings
    wifi_write32(dev, WIFI_REG_MAC_CONFIG,
        (1 << 0) |  // Enable MAC layer
        (1 << 1) |  // Enable RX
        (1 << 2) |  // Enable TX
        (1 << 3) |  // Enable retries
        (1 << 4)    // Enable ACKs
    );

    // 6. Configure rate adaptation
    wifi_write32(dev, WIFI_REG_RATE_CTRL,
        (1 << 0) |     // Enable rate control
        (4 << 1) |     // Max retries
        (0xFF << 8)    // Rate mask (all rates enabled)
    );

    // 7. Configure power management
    wifi_write32(dev, WIFI_REG_POWER_CTRL,
        (1 << 0) |     // Enable power management
        (1 << 1) |     // Enable deep sleep
        (3 << 2)       // Power save level
    );

    // 8. Configure interrupt coalescing
    wifi_write32(dev, WIFI_REG_INT_COAL,
        (16 << 0) |    // RX max coalesce
        (16 << 8) |    // TX max coalesce
        (50 << 16)     // Timeout in μs
    );

    // 9. Set regulatory domain
    wifi_write32(dev, WIFI_REG_REGULATORY,
        (0x6A << 0) |  // Country code (US)
        (1 << 8)       // Indoor operation
    );

    // 10. Apply antenna configuration
    if (!configure_antenna(dev, cal_data)) {
        free(dev);
        return NULL;
    }

    // Setup DMA rings
    if (!setup_dma_rings(dev)) {
        free(dev);
        return NULL;
    }

    // Read MAC address
    if (!read_mac_address(dev)) {
        // Cleanup DMA rings
        for (int i = 0; i < TX_RING_SIZE; i++) {
            if (dev->tx_buffers[i]) pmm_free_page(dev->tx_buffers[i]);
        }
        for (int i = 0; i < RX_RING_SIZE; i++) {
            if (dev->rx_buffers[i]) pmm_free_page(dev->rx_buffers[i]);
        }
        if (dev->tx_ring) pmm_free_page(dev->tx_ring);
        if (dev->rx_ring) pmm_free_page(dev->rx_ring);
        free(dev);
        return NULL;
    }

    // Enable bus mastering
    uint32_t command = pci_read_config(pci_dev->bus, pci_dev->slot, pci_dev->func, 0x04);
    command |= (1 << 2); // Bus Master Enable
    pci_write_config(pci_dev->bus, pci_dev->slot, pci_dev->func, 0x04, command);

    return dev;
}

// Start the WiFi device
bool wifi_start(struct wifi_device* dev) {
    if (!dev) return false;

    // Enable interrupts
    wifi_write32(dev, WIFI_REG_INT_ENABLE,
        WIFI_INT_TX_DONE | WIFI_INT_RX_DONE | WIFI_INT_FW_READY |
        WIFI_INT_TX_ERR | WIFI_INT_RX_ERR | WIFI_INT_TEMP_WARNING |
        WIFI_INT_RF_KILL | WIFI_INT_BEACON);

    // Enable DMA
    wifi_write32(dev, WIFI_REG_CSR, wifi_read32(dev, WIFI_REG_CSR) | 0x3); // TX/RX enable

    // Power up radio
    wifi_write32(dev, WIFI_REG_POWER_CTRL, 0x0); // Full power mode

    return true;
}

// Stop the WiFi device
void wifi_stop(struct wifi_device* dev) {
    if (!dev) return;

    // Disable interrupts
    wifi_write32(dev, WIFI_REG_INT_ENABLE, 0);

    // Disable DMA
    wifi_write32(dev, WIFI_REG_CSR, wifi_read32(dev, WIFI_REG_CSR) & ~0x3);

    // Power down radio
    wifi_write32(dev, WIFI_REG_POWER_CTRL, 0x1); // Power save mode
}

// Transmit a packet
bool wifi_transmit_packet(struct wifi_device* dev, const void* data, size_t length) {
    if (!dev || !data || length > TX_BUFFER_SIZE) return false;

    // Check if TX ring is full
    if (((dev->tx_tail + 1) % TX_RING_SIZE) == dev->tx_head) {
        return false;
    }

    // Get next TX descriptor
    struct wifi_dma_desc* desc = &dev->tx_ring[dev->tx_tail];

    // Copy packet data to DMA buffer
    memcpy(dev->tx_buffers[dev->tx_tail], data, length);

    // Setup descriptor
    desc->buffer_len = length;
    desc->status = 0;
    desc->flags = DESC_FLAG_OWN | DESC_FLAG_INT | DESC_FLAG_FIRST |
                 DESC_FLAG_LAST | DESC_FLAG_EOP;

    // Advance tail pointer
    dev->tx_tail = (dev->tx_tail + 1) % TX_RING_SIZE;

    // Notify hardware of new packet
    wifi_write32(dev, WIFI_REG_TX_TAIL, dev->tx_tail);

    return true;
}

// Receive a packet
bool wifi_receive_packet(struct wifi_device* dev, void* buffer, size_t* length) {
    if (!dev || !buffer || !length) return false;

    // Check if packet available
    struct wifi_dma_desc* desc = &dev->rx_ring[dev->rx_head];
    if (desc->flags & DESC_FLAG_OWN) {
        return false;
    }

    // Get packet length and validate
    size_t packet_len = desc->buffer_len;
    if (packet_len > *length) {
        return false;
    }

    // Copy packet data
    memcpy(buffer, dev->rx_buffers[dev->rx_head], packet_len);
    *length = packet_len;

    // Reset descriptor and return to hardware
    desc->status = 0;
    desc->flags = DESC_FLAG_OWN | DESC_FLAG_INT;

    // Advance head pointer
    dev->rx_head = (dev->rx_head + 1) % RX_RING_SIZE;

    // Update hardware head pointer
    wifi_write32(dev, WIFI_REG_RX_HEAD, dev->rx_head);

    return true;
}