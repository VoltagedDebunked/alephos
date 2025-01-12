#include <core/drivers/net/netdev.h>
#include <core/drivers/net/e1000.h>
#include <utils/mem.h>
#include <utils/str.h>

static struct netdev net_devices[MAX_NET_DEVICES];
static int num_net_devices = 0;

// E1000 device wrapper functions
static bool e1000_init_wrap(struct netdev* dev) {
    return e1000_init(dev);
}

static bool e1000_transmit_wrap(struct netdev* dev, const void* data, uint16_t len) {
    return e1000_send_packet(dev, data, len);
}

static bool e1000_receive_wrap(struct netdev* dev, void* data, uint16_t* len) {
    return e1000_receive_packet(dev, data, len);
}

// E1000 device operations
static struct netdev_ops e1000_ops = {
    .init = e1000_init_wrap,
    .transmit = e1000_transmit_wrap,
    .receive = e1000_receive_wrap,
    .start = NULL,  // Not implemented yet
    .stop = NULL,   // Not implemented yet
    .get_mac = NULL // Not implemented yet
};

void netdev_init(void) {
    memset(net_devices, 0, sizeof(net_devices));

    // Create E1000 network device
    struct netdev *e1000_dev = &net_devices[num_net_devices];
    memcpy(e1000_dev->name, "eth0", 5);
    e1000_dev->ops = &e1000_ops;
    memset(&e1000_dev->stats, 0, sizeof(struct netdev_stats));

    // Initialize the device
    if (e1000_dev->ops->init(e1000_dev)) {
        e1000_dev->active = true;
        num_net_devices++;
    }
}

bool netdev_register(struct netdev *dev) {
    if (num_net_devices >= MAX_NET_DEVICES) {
        return false;
    }

    memcpy(&net_devices[num_net_devices], dev, sizeof(struct netdev));
    num_net_devices++;
    return true;
}

void netdev_unregister(struct netdev *dev) {
    for (int i = 0; i < num_net_devices; i++) {
        if (&net_devices[i] == dev) {
            if (dev->priv) {
                free(dev->priv);
            }
            // Move remaining devices down
            memmove(&net_devices[i], &net_devices[i + 1],
                    (num_net_devices - i - 1) * sizeof(struct netdev));
            num_net_devices--;
            break;
        }
    }
}

struct netdev *netdev_get_by_name(const char *name) {
    for (int i = 0; i < num_net_devices; i++) {
        if (strcmp(net_devices[i].name, name) == 0) {
            return &net_devices[i];
        }
    }
    return NULL;
}

struct netdev *netdev_get_default(void) {
    if (num_net_devices > 0) {
        return &net_devices[0];
    }
    return NULL;
}