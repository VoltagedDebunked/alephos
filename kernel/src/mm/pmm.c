#include <mm/pmm.h>
#include <utils/mem.h>
#include <limine.h>

extern struct limine_hhdm_request hhdm_request;

#define BITMAP_ENTRY uint64_t
#define BITS_PER_ENTRY (sizeof(BITMAP_ENTRY) * 8)
#define MAX_MEMORY_GB 4

static BITMAP_ENTRY *bitmap = NULL;
static uint64_t bitmap_size = 0;
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static uint64_t hhdm_offset = 0;

static void bitmap_set(uint64_t page) {
    uint64_t idx = page / BITS_PER_ENTRY;
    uint64_t bit = page % BITS_PER_ENTRY;
    bitmap[idx] |= (1ULL << bit);
}

static void bitmap_clear(uint64_t page) {
    uint64_t idx = page / BITS_PER_ENTRY;
    uint64_t bit = page % BITS_PER_ENTRY;
    bitmap[idx] &= ~(1ULL << bit);
}

static bool bitmap_test(uint64_t page) {
    uint64_t idx = page / BITS_PER_ENTRY;
    uint64_t bit = page % BITS_PER_ENTRY;
    return (bitmap[idx] & (1ULL << bit)) != 0;
}

static void *phys_to_virt(void *phys) {
    return (void*)((uint64_t)phys + hhdm_offset);
}

void pmm_init(struct limine_memmap_response *memmap) {
    hhdm_offset = hhdm_request.response->offset;
    uint64_t max_addr = MAX_MEMORY_GB * 1024 * 1024 * 1024ULL;

    // Limit total pages to max 4GB
    total_pages = max_addr / PAGE_SIZE;
    bitmap_size = (total_pages + BITS_PER_ENTRY - 1) / BITS_PER_ENTRY;

    // Find usable region for bitmap
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) continue;

        uint64_t required_size = bitmap_size * sizeof(BITMAP_ENTRY);
        if (entry->length >= required_size) {
            bitmap = phys_to_virt((void*)entry->base);
            memset(bitmap, 0xFF, bitmap_size * sizeof(BITMAP_ENTRY));

            entry->base += required_size;
            entry->length -= required_size;
            break;
        }
    }

    // Mark available memory up to max_addr
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) continue;

        uint64_t start_page = entry->base / PAGE_SIZE;
        uint64_t end_page = (entry->base + entry->length) / PAGE_SIZE;

        // Limit to max address
        if (start_page >= total_pages) continue;
        if (end_page > total_pages) end_page = total_pages;

        for (uint64_t page = start_page; page < end_page; page++) {
            bitmap_clear(page);
            free_pages++;
        }
    }
}

void *pmm_alloc_page(void) {
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
            return (void*)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void *pmm_alloc_pages(size_t count) {
    if (count == 0) return NULL;
    if (count == 1) return pmm_alloc_page();

    uint64_t consecutive = 0;
    uint64_t start_page = 0;

    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            if (consecutive == 0) start_page = i;
            consecutive++;
            if (consecutive == count) {
                // Found enough consecutive pages
                for (uint64_t j = 0; j < count; j++) {
                    bitmap_set(start_page + j);
                }
                free_pages -= count;
                return (void*)(start_page * PAGE_SIZE);
            }
        } else {
            consecutive = 0;
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
    for (size_t i = 0; i < count; i++) {
        pmm_free_page((void*)((start_page + i) * PAGE_SIZE));
    }
}

size_t pmm_get_free_pages(void) {
    return free_pages;
}