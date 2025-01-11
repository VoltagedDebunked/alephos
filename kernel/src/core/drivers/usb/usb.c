#include <core/drivers/usb/usb.h>
#include <core/drivers/usb/xhci.h>
#include <utils/mem.h>

#define MAX_USB_DEVICES 16

static struct usb_device usb_devices[MAX_USB_DEVICES];
static int num_usb_devices = 0;

void usb_init(void) {
    xhci_init();
    num_usb_devices = 0;
    memset(usb_devices, 0, sizeof(usb_devices));
    xhci_start();
}

struct usb_device* usb_scan_devices(void) {
    if (num_usb_devices >= MAX_USB_DEVICES) {
        return NULL;
    }
    struct usb_device* dev = &usb_devices[num_usb_devices++];
    memset(dev, 0, sizeof(struct usb_device));
    return dev;
}

bool usb_configure_device(struct usb_device* dev) {
    if (!dev) return false;
    dev->address = num_usb_devices;
    dev->max_packet_size = 8;
    return true;
}