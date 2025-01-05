#include <core/attributes.h>
#include <limine.h>
#include <utils/mem.h>

void fb_init() {
    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    memset((void *)framebuffer->address, 0, framebuffer->pitch * framebuffer->height);;
}