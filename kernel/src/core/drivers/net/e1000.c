#include <core/drivers/net/e1000.h>
#include <core/drivers/net/netdev.h>
#include <core/drivers/pci.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <utils/io.h>
#include <utils/mem.h>
#include <core/idt.h>

// Structure for E1000-specific data
struct e1000_data {
    volatile uint32_t* mmio_base;
    struct e1000_rx_desc* rx_descriptors;
    struct e1000_tx_desc* tx_descriptors;
    void* rx_buffers[E1000_NUM_RX_DESC];
    void* tx_buffers[E1000_NUM_TX_DESC];
    uint16_t rx_cur;
    uint16_t tx_cur;
    struct pci_device* pci_dev;
};

// Helper function to read from MMIO
static inline uint32_t e1000_read_reg(struct e1000_data* data, uint32_t reg) {
    return data->mmio_base[reg / 4];
}

// Helper function to write to MMIO
static inline void e1000_write_reg(struct e1000_data* data, uint32_t reg, uint32_t value) {
    data->mmio_base[reg / 4] = value;
}

// Read from EEPROM
static bool e1000_eeprom_read(struct e1000_data* data, uint8_t addr, uint16_t* out) {
    e1000_write_reg(data, E1000_EERD, (addr << 8) | 0x1);

    // Wait for read to complete
    uint32_t val;
    do {
        val = e1000_read_reg(data, E1000_EERD);
    } while (!(val & (1 << 4)));

    *out = (uint16_t)((val >> 16) & 0xFFFF);
    return true;
}

// Initialize receive descriptors
static bool init_rx_descriptors(struct e1000_data* data) {
    // Allocate descriptor array
    data->rx_descriptors = (struct e1000_rx_desc*)pmm_alloc_page();
    if (!data->rx_descriptors) return false;
    memset(data->rx_descriptors, 0, PAGE_SIZE);

    // Allocate buffers for each descriptor
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        data->rx_buffers[i] = pmm_alloc_page();
        if (!data->rx_buffers[i]) return false;
        data->rx_descriptors[i].addr = (uint64_t)data->rx_buffers[i];
        data->rx_descriptors[i].status = 0;
    }

    // Setup receive descriptor registers
    e1000_write_reg(data, E1000_RDBAL, (uint32_t)(uint64_t)data->rx_descriptors);
    e1000_write_reg(data, E1000_RDBAH, (uint32_t)((uint64_t)data->rx_descriptors >> 32));
    e1000_write_reg(data, E1000_RDLEN, E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc));
    e1000_write_reg(data, E1000_RDH, 0);
    e1000_write_reg(data, E1000_RDT, E1000_NUM_RX_DESC - 1);

    return true;
}

// Initialize transmit descriptors
static bool init_tx_descriptors(struct e1000_data* data) {
    // Allocate descriptor array
    data->tx_descriptors = (struct e1000_tx_desc*)pmm_alloc_page();
    if (!data->tx_descriptors) return false;
    memset(data->tx_descriptors, 0, PAGE_SIZE);

    // Allocate buffers for each descriptor
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        data->tx_buffers[i] = pmm_alloc_page();
        if (!data->tx_buffers[i]) return false;
        data->tx_descriptors[i].addr = (uint64_t)data->tx_buffers[i];
        data->tx_descriptors[i].status = E1000_TXD_STAT_DD;  // Mark as done initially
    }

    // Setup transmit descriptor registers
    e1000_write_reg(data, E1000_TDBAL, (uint32_t)(uint64_t)data->tx_descriptors);
    e1000_write_reg(data, E1000_TDBAH, (uint32_t)((uint64_t)data->tx_descriptors >> 32));
    e1000_write_reg(data, E1000_TDLEN, E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));
    e1000_write_reg(data, E1000_TDH, 0);
    e1000_write_reg(data, E1000_TDT, 0);

    return true;
}

// Interrupt handler
static void e1000_interrupt_handler(struct interrupt_frame* frame) {
    // TODO: Get device context from somewhere
    (void)frame;
}

// Initialize the E1000 NIC
bool e1000_init(struct netdev* dev) {
    struct e1000_data* data = (struct e1000_data*)malloc(sizeof(struct e1000_data));
    if (!data) return false;
    memset(data, 0, sizeof(struct e1000_data));

    // Find the E1000 PCI device
    data->pci_dev = pci_scan_for_class(0x02, 0x00);  // Network Controller
    if (!data->pci_dev || data->pci_dev->vendor_id != E1000_VENDOR_ID) {
        free(data);
        return false;
    }

    // Map the MMIO region
    uint64_t mmio_base = pci_get_bar(data->pci_dev, 0);
    if (!mmio_base) {
        free(data);
        return false;
    }

    data->mmio_base = (volatile uint32_t*)vmm_get_phys_addr(mmio_base);

    // Reset the device
    e1000_write_reg(data, E1000_CTRL, e1000_read_reg(data, E1000_CTRL) | E1000_CTRL_RST);
    for (int i = 0; i < 1000; i++) {
        if (!(e1000_read_reg(data, E1000_CTRL) & E1000_CTRL_RST)) break;
    }

    // General initialization
    uint32_t ctrl = e1000_read_reg(data, E1000_CTRL);
    ctrl |= E1000_CTRL_SLU;   // Set Link Up
    ctrl |= E1000_CTRL_ASDE;  // Auto-speed detection
    ctrl |= E1000_CTRL_FD;    // Full duplex
    e1000_write_reg(data, E1000_CTRL, ctrl);

    // Initialize receive and transmit descriptors
    if (!init_rx_descriptors(data) || !init_tx_descriptors(data)) {
        free(data);
        return false;
    }

    // Setup receive control register
    uint32_t rctl = E1000_RCTL_EN;      // Enable receiver
    rctl |= E1000_RCTL_SBP;             // Store bad packets
    rctl |= E1000_RCTL_UPE;             // Unicast promiscuous enable
    rctl |= E1000_RCTL_MPE;             // Multicast promiscuous enable
    rctl |= E1000_RCTL_BSIZE;           // Buffer size = 2048
    rctl |= E1000_RCTL_SECRC;           // Strip CRC
    e1000_write_reg(data, E1000_RCTL, rctl);

    // Setup transmit control register
    uint32_t tctl = E1000_TCTL_EN;      // Enable transmitter
    tctl |= E1000_TCTL_PSP;             // Pad short packets
    tctl |= E1000_TCTL_CT;              // Collision threshold
    tctl |= E1000_TCTL_COLD;            // Collision distance
    e1000_write_reg(data, E1000_TCTL, tctl);

    // Store private data
    dev->priv = data;

    return true;
}

// Send a packet
bool e1000_send_packet(struct netdev* dev, const void* data, uint16_t length) {
    struct e1000_data* priv = (struct e1000_data*)dev->priv;
    if (!priv || !priv->tx_descriptors || length > E1000_BUFFER_SIZE) {
        return false;
    }

    // Wait for a free descriptor
    while (!(priv->tx_descriptors[priv->tx_cur].status & E1000_TXD_STAT_DD)) {
        // Could add timeout here
    }

    // Copy data to buffer
    memcpy(priv->tx_buffers[priv->tx_cur], data, length);

    // Setup the descriptor
    priv->tx_descriptors[priv->tx_cur].length = length;
    priv->tx_descriptors[priv->tx_cur].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
    priv->tx_descriptors[priv->tx_cur].status = 0;

    // Update tail pointer to start transmission
    uint16_t old_tail = priv->tx_cur;
    priv->tx_cur = (priv->tx_cur + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(priv, E1000_TDT, priv->tx_cur);

    // Wait for transmission to complete
    while (!(priv->tx_descriptors[old_tail].status & E1000_TXD_STAT_DD)) {
        // Could add timeout here
    }

    return true;
}

// Receive a packet
bool e1000_receive_packet(struct netdev* dev, void* buffer, uint16_t* length) {
    struct e1000_data* priv = (struct e1000_data*)dev->priv;
    if (!priv || !priv->rx_descriptors || !buffer || !length) {
        return false;
    }

    // Check if we have a packet
    if (!(priv->rx_descriptors[priv->rx_cur].status & 0x1)) {
        return false;  // No packet available
    }

    // Get packet length and copy data
    *length = priv->rx_descriptors[priv->rx_cur].length;
    if (*length > E1000_BUFFER_SIZE) {
        *length = E1000_BUFFER_SIZE;
    }
    memcpy(buffer, priv->rx_buffers[priv->rx_cur], *length);

    // Reset descriptor
    priv->rx_descriptors[priv->rx_cur].status = 0;

    // Update tail pointer
    uint16_t old_rx_cur = priv->rx_cur;
    priv->rx_cur = (priv->rx_cur + 1) % E1000_NUM_RX_DESC;
    e1000_write_reg(priv, E1000_RDT, old_rx_cur);

    // Update statistics
    dev->stats.rx_packets++;
    dev->stats.rx_bytes += *length;

    return true;
}

// Read MAC address from EEPROM
static bool e1000_read_mac_address(struct e1000_data* data, uint8_t mac[6]) {
    uint16_t word1, word2, word3;

    // Read MAC address from EEPROM
    if (!e1000_eeprom_read(data, 0, &word1) ||
        !e1000_eeprom_read(data, 1, &word2) ||
        !e1000_eeprom_read(data, 2, &word3)) {
        return false;
    }

    mac[0] = word1 & 0xFF;
    mac[1] = word1 >> 8;
    mac[2] = word2 & 0xFF;
    mac[3] = word2 >> 8;
    mac[4] = word3 & 0xFF;
    mac[5] = word3 >> 8;

    return true;
}

// Set MAC address in device registers
static void e1000_set_mac_address(struct e1000_data* data, const uint8_t mac[6]) {
    uint32_t low = mac[0] | (mac[1] << 8) | (mac[2] << 16) | (mac[3] << 24);
    uint32_t high = mac[4] | (mac[5] << 8) | (1 << 31); // Set valid bit

    e1000_write_reg(data, E1000_RAL, low);
    e1000_write_reg(data, E1000_RAH, high);
}