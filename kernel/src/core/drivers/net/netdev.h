#ifndef NETDEV_H
#define NETDEV_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_NET_DEVICES 4

// Forward declare netdev struct
struct netdev;

// Network device statistics
struct netdev_stats {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
};

// Network device operations
struct netdev_ops {
    bool (*init)(struct netdev *dev);
    bool (*start)(struct netdev *dev);
    void (*stop)(struct netdev *dev);
    bool (*transmit)(struct netdev *dev, const void *data, uint16_t len);
    bool (*receive)(struct netdev *dev, void *data, uint16_t *len);
    void (*get_mac)(struct netdev *dev, uint8_t mac[6]);
};

// Network device structure
struct netdev {
    char name[16];
    uint8_t mac[6];
    bool active;
    void *priv;  // Private driver data
    struct netdev_ops *ops;
    struct netdev_stats stats;
};

// Function declarations
void netdev_init(void);
bool netdev_register(struct netdev *dev);
void netdev_unregister(struct netdev *dev);
struct netdev *netdev_get_by_name(const char *name);
struct netdev *netdev_get_default(void);

#endif // NETDEV_H