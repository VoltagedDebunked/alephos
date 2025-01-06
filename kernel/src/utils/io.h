#ifndef IO_H
#define IO_H
#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
   uint8_t value;
   __asm__ volatile (
       "in al, dx"
       : "=a"(value)
       : "d"(port)
   );
   return value;
}

static inline void outb(uint16_t port, uint8_t value) {
   __asm__ volatile (
       "out dx, al"
       :
       : "a"(value), "d"(port)
   );
}

static inline uint16_t inw(uint16_t port) {
   uint16_t value;
   __asm__ volatile (
       "in ax, dx"
       : "=a"(value)
       : "d"(port)
   );
   return value;
}

static inline void outw(uint16_t port, uint16_t value) {
   __asm__ volatile (
       "out dx, ax"
       :
       : "a"(value), "d"(port)
   );
}

static inline uint32_t inl(uint16_t port) {
   uint32_t value;
   __asm__ volatile (
       "in eax, dx"
       : "=a"(value)
       : "d"(port)
   );
   return value;
}

static inline void outl(uint16_t port, uint32_t value) {
   __asm__ volatile (
       "out dx, eax"
       :
       : "a"(value), "d"(port)
   );
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif