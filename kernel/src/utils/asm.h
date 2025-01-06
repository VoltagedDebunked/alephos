#ifndef ASM_H
#define ASM_H

static inline void cli(void) {
    asm volatile ("cli" ::: "memory");
}

static inline void sti(void) {
    asm volatile ("sti" ::: "memory");
}

static inline void hlt(void) {
    asm volatile ("hlt" ::: "memory");
}

#endif