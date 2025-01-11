#include <core/drivers/usb/keyboard.h>
#include <core/drivers/usb/usb.h>
#include <utils/mem.h>
#include <stddef.h>

static struct usb_device* keyboard_device = NULL;

bool usb_keyboard_available(void) {
    return keyboard_device != NULL;
}

void usb_keyboard_init(void) {
    usb_init();

    struct usb_device* dev;
    while ((dev = usb_scan_devices()) != NULL) {
        if (dev->class == USB_CLASS_HID && dev->subclass == 1 && dev->protocol == 1) {
            keyboard_device = dev;
            usb_configure_device(dev);
            return;
        }
    }
}