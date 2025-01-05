#include <stdbool.h>
#include <limine.h>
#include <core/attributes.h>
#include <stddef.h>

void check_fb() {
   // Ensure the bootloader actually understands our base revision.
   if (LIMINE_BASE_REVISION_SUPPORTED == false) {
       hcf();
   }

   // Ensure we got a framebuffer or else we're in trouble.
   if (framebuffer_request.response == NULL
       || framebuffer_request.response->framebuffer_count < 1) {
       hcf();
   }
}