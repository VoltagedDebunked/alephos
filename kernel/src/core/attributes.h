#ifndef ATTRIBUTES_H
#define ATTRIBUTES_H

#include <limine.h>

// Add section attribute to all Limine requests
__attribute__((section(".limine_requests")))
static LIMINE_BASE_REVISION(0);

extern volatile struct limine_base_revision base_revision;
extern volatile struct limine_framebuffer_request framebuffer_request;
extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_hhdm_request hhdm_request;
extern volatile struct limine_rsdp_request rsdp_request;
extern volatile struct limine_kernel_address_request kernel_address_request;

#endif