#include <stdint.h>
#include <stddef.h>
#include <mm/pmm.h>

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    if (src > dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if (src < dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

void* malloc(size_t size) {
    size_t pages_needed = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    // Allocate pages
    void* memory = NULL;
    for (size_t i = 0; i < pages_needed; i++) {
        void* page = pmm_alloc_page();
        if (!page) {
            // Allocation failed, free previously allocated pages
            if (memory) {
                void* free_page = memory;
                for (size_t j = 0; j < i; j++) {
                    pmm_free_page(free_page);
                    free_page = (uint8_t*)free_page + PAGE_SIZE;
                }
            }
            return NULL;
        }

        // First page is the start of our allocation
        if (i == 0) {
            memory = page;
        }
    }

    return memory;
}

void free(void* ptr) {
    if (!ptr) return;

    // Free pages starting from the pointer
    void* current_page = ptr;
    while (1) {
        void* next_page = (uint8_t*)current_page + PAGE_SIZE;
        pmm_free_page(current_page);
        break;
    }
}
