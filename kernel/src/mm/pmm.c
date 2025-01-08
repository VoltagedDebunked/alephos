#include <mm/pmm.h>
#include <utils/mem.h>
#include <limine.h>

extern struct limine_hhdm_request hhdm_request;

#define BITMAP_ENTRY uint64_t
#define BITS_PER_ENTRY 64
#define MAX_MEMORY_GB 4

static BITMAP_ENTRY *bitmap = NULL;
static uint64_t bitmap_size = 0;
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static uint64_t hhdm_offset = 0;

static inline void bitmap_set(uint64_t page) {
    BITMAP_ENTRY mask = ((BITMAP_ENTRY)1 << (page % BITS_PER_ENTRY));
    bitmap[page / BITS_PER_ENTRY] |= mask;
}

static inline void bitmap_clear(uint64_t page) {
    BITMAP_ENTRY mask = ((BITMAP_ENTRY)1 << (page % BITS_PER_ENTRY));
    bitmap[page / BITS_PER_ENTRY] &= ~mask;
}

static inline bool bitmap_test(uint64_t page) {
    BITMAP_ENTRY mask = ((BITMAP_ENTRY)1 << (page % BITS_PER_ENTRY));
    return (bitmap[page / BITS_PER_ENTRY] & mask) != 0;
}

static inline void* phys_to_virt(void *phys) {
    return (void*)((uint64_t)phys + hhdm_offset);
}

void pmm_init(struct limine_memmap_response *memmap) {
    hhdm_offset = hhdm_request.response->offset;
    uint64_t max_addr = MAX_MEMORY_GB * 1024 * 1024 * 1024ULL;

    total_pages = max_addr / PAGE_SIZE;
    bitmap_size = (total_pages + BITS_PER_ENTRY - 1) / BITS_PER_ENTRY;

    // Find usable region for bitmap
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) continue;

        uint64_t required_size = bitmap_size * sizeof(BITMAP_ENTRY);
        if (entry->length >= required_size) {
            bitmap = phys_to_virt((void*)entry->base);
            // Mark all pages as used initially
            memset(bitmap, 0xFF, bitmap_size * sizeof(BITMAP_ENTRY));
            
            entry->base += required_size;
            entry->length -= required_size;
            break;
        }
    }

    // Mark available memory as free
    free_pages = 0;
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) continue;

        uint64_t start_page = (entry->base + PAGE_SIZE - 1) / PAGE_SIZE;  // Round up
        uint64_t end_page = entry->length / PAGE_SIZE;                    // Round down

        if (start_page >= total_pages) continue;
        if (end_page > total_pages) end_page = total_pages;

        for (uint64_t page = start_page; page < end_page; page++) {
            bitmap_clear(page);
            free_pages++;
        }
    }
}

void *pmm_alloc_page(void) {
    if (free_pages == 0) return NULL;

    // Start looking from entry 1 to avoid the first pages
    for (uint64_t idx = 1; idx < bitmap_size; idx++) {
        BITMAP_ENTRY entry = bitmap[idx];
        if (entry == (BITMAP_ENTRY)-1) continue;  // Skip if fully used

        // Find first free bit
        for (uint64_t bit = 0; bit < BITS_PER_ENTRY; bit++) {
            if (!(entry & ((BITMAP_ENTRY)1 << bit))) {
                uint64_t page = idx * BITS_PER_ENTRY + bit;
                if (page >= total_pages) return NULL;

                bitmap_set(page);
                free_pages--;
                return (void*)(page * PAGE_SIZE);
            }
        }
    }
    
    return NULL;
}

void *pmm_alloc_pages(size_t count) {
    if (count == 0 || count > free_pages) return NULL;
    if (count == 1) return pmm_alloc_page();

    // Start from entry 1 to avoid low pages
    for (uint64_t idx = 1; idx < bitmap_size; idx++) {
        uint64_t consecutive = 0;
        uint64_t start_bit = 0;

        for (uint64_t bit = 0; bit < BITS_PER_ENTRY; bit++) {
            uint64_t page = idx * BITS_PER_ENTRY + bit;
            
            if (page >= total_pages) return NULL;

            if (!bitmap_test(page)) {
                if (consecutive == 0) start_bit = bit;
                consecutive++;
                if (consecutive == count) {
                    // Found enough consecutive pages
                    for (uint64_t i = 0; i < count; i++) {
                        bitmap_set(idx * BITS_PER_ENTRY + start_bit + i);
                    }
                    free_pages -= count;
                    return (void*)((idx * BITS_PER_ENTRY + start_bit) * PAGE_SIZE);
                }
            } else {
                consecutive = 0;
            }
        }
    }

    return NULL;
}

void pmm_free_page(void *addr) {
    uint64_t page = (uint64_t)addr / PAGE_SIZE;
    if (page >= total_pages) return;

    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_pages++;
    }
}

void pmm_free_pages(void *addr, size_t count) {
    uint64_t start_page = (uint64_t)addr / PAGE_SIZE;
    if (start_page >= total_pages) return;

    for (size_t i = 0; i < count; i++) {
        uint64_t page = start_page + i;
        if (page >= total_pages) break;
        pmm_free_page((void*)(page * PAGE_SIZE));
    }
}

size_t pmm_get_free_pages(void) {
    return free_pages;
}