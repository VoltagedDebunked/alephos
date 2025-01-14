#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <limine.h>

// Basic drawing functions
void draw_pixel(struct limine_framebuffer *fb, uint32_t x, uint32_t y, uint32_t color);
void draw_rect(struct limine_framebuffer *fb, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void draw_char(struct limine_framebuffer *fb, char c, uint32_t x, uint32_t y, uint32_t color);
void draw_string(struct limine_framebuffer *fb, const char *str, uint32_t x, uint32_t y, uint32_t color);
void clear_screen(struct limine_framebuffer *fb);

#endif // DISPLAY_H