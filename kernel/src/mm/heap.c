#include <mm/heap.h>
#include <utils/mem.h>
#include <mm/pmm.h>
#include <mm/vmm.h>

static struct heap_block* heap_start = NULL;
static struct heap_stats heap_statistics;

static struct heap_block* find_free_block(size_t size) {
    struct heap_block* current = heap_start;

    while (current) {
        if ((current->flags & BLOCK_FREE) && current->size >= size + sizeof(struct heap_block)) {
            return current;
        }
        if (current->flags & BLOCK_LAST) break;
        current = current->next;
    }

    return NULL;
}

static struct heap_block* expand_heap(size_t size) {
    size_t pages_needed = (size + sizeof(struct heap_block) + HEAP_PAGE_SIZE - 1) / HEAP_PAGE_SIZE;
    void* new_mem = pmm_alloc_page();

    if (!new_mem) return NULL;

    struct heap_block* last = heap_start;
    while (!(last->flags & BLOCK_LAST)) {
        last = last->next;
    }

    struct heap_block* new_block = (struct heap_block*)((uint8_t*)last + last->size);

    // Map the new pages
    for (size_t i = 0; i < pages_needed; i++) {
        uint64_t phys = (uint64_t)pmm_alloc_page();
        if (!phys || !vmm_map_page((uint64_t)new_block + (i * HEAP_PAGE_SIZE), phys, PTE_PRESENT | PTE_WRITABLE)) {
            // Failed to allocate/map pages - cleanup
            for (size_t j = 0; j < i; j++) {
                vmm_unmap_page((uint64_t)new_block + (j * HEAP_PAGE_SIZE));
            }
            return NULL;
        }
    }

    // Setup new block
    new_block->magic = HEAP_BLOCK_MAGIC;
    new_block->size = pages_needed * HEAP_PAGE_SIZE;
    new_block->flags = BLOCK_FREE | BLOCK_LAST;
    new_block->prev = last;
    new_block->next = NULL;

    // Update old last block
    last->flags &= ~BLOCK_LAST;
    last->next = new_block;

    // Update statistics
    heap_statistics.total_size += new_block->size;
    heap_statistics.free_size += new_block->size;
    heap_statistics.total_blocks++;
    heap_statistics.free_blocks++;

    return new_block;
}

static void split_block(struct heap_block* block, size_t size) {
    size_t total_size = block->size;
    size_t remaining = total_size - size - sizeof(struct heap_block);

    if (remaining < HEAP_MIN_BLOCK_SIZE) return;  // Too small to split

    struct heap_block* new_block = (struct heap_block*)((uint8_t*)block + size + sizeof(struct heap_block));

    new_block->magic = HEAP_BLOCK_MAGIC;
    new_block->size = remaining;
    new_block->flags = BLOCK_FREE;
    if (block->flags & BLOCK_LAST) {
        new_block->flags |= BLOCK_LAST;
    }
    new_block->next = block->next;
    new_block->prev = block;

    if (block->next) {
        block->next->prev = new_block;
    }

    block->next = new_block;
    block->size = size + sizeof(struct heap_block);
    block->flags &= ~BLOCK_LAST;

    // Update statistics
    heap_statistics.total_blocks++;
    heap_statistics.free_blocks++;
}

static void merge_free_blocks(struct heap_block* block) {
    // Merge with next block if it's free
    while (block->next && (block->next->flags & BLOCK_FREE)) {
        struct heap_block* next = block->next;
        block->size += next->size;
        block->flags |= (next->flags & BLOCK_LAST);
        block->next = next->next;

        if (next->next) {
            next->next->prev = block;
        }

        // Update statistics
        heap_statistics.total_blocks--;
        heap_statistics.free_blocks--;
    }
}

bool heap_init(void) {
    // Allocate initial heap space
    void* heap_mem = pmm_alloc_page();
    if (!heap_mem) return false;

    if (!vmm_map_page((uint64_t)heap_mem, (uint64_t)heap_mem, PTE_PRESENT | PTE_WRITABLE)) {
        pmm_free_page(heap_mem);
        return false;
    }

    // Initialize first block
    heap_start = (struct heap_block*)heap_mem;
    heap_start->magic = HEAP_BLOCK_MAGIC;
    heap_start->size = HEAP_PAGE_SIZE;
    heap_start->flags = BLOCK_FREE | BLOCK_LAST;
    heap_start->next = NULL;
    heap_start->prev = NULL;

    // Initialize statistics
    heap_statistics.total_size = HEAP_PAGE_SIZE;
    heap_statistics.used_size = 0;
    heap_statistics.free_size = HEAP_PAGE_SIZE;
    heap_statistics.total_blocks = 1;
    heap_statistics.free_blocks = 1;

    return true;
}

void* heap_alloc(size_t size) {
    if (!size) return NULL;

    // Align size to ensure proper alignment of subsequent blocks
    size = (size + 7) & ~7;

    struct heap_block* block = find_free_block(size);
    if (!block) {
        block = expand_heap(size);
        if (!block) return NULL;
    }

    // Split block if it's too large
    split_block(block, size);

    // Mark block as used
    block->flags &= ~BLOCK_FREE;

    // Update statistics
    heap_statistics.used_size += block->size;
    heap_statistics.free_size -= block->size;
    heap_statistics.free_blocks--;

    return block->data;
}

void heap_free(void* ptr) {
    if (!ptr) return;

    struct heap_block* block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));

    // Validate block
    if (block->magic != HEAP_BLOCK_MAGIC) return;

    // Mark block as free
    block->flags |= BLOCK_FREE;

    // Update statistics
    heap_statistics.used_size -= block->size;
    heap_statistics.free_size += block->size;
    heap_statistics.free_blocks++;

    // Try to merge with adjacent free blocks
    merge_free_blocks(block);
    if (block->prev && (block->prev->flags & BLOCK_FREE)) {
        merge_free_blocks(block->prev);
    }
}

void* heap_realloc(void* ptr, size_t size) {
    if (!ptr) return heap_alloc(size);
    if (!size) {
        heap_free(ptr);
        return NULL;
    }

    struct heap_block* block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));

    // Validate block
    if (block->magic != HEAP_BLOCK_MAGIC) return NULL;

    size_t old_size = block->size - sizeof(struct heap_block);

    // If new size is smaller, we can simply shrink the block
    if (size <= old_size) {
        split_block(block, size);
        return ptr;
    }

    // If next block is free and has enough space, merge with it
    if (block->next && (block->next->flags & BLOCK_FREE) &&
        (block->size + block->next->size >= size + sizeof(struct heap_block))) {
        merge_free_blocks(block);
        split_block(block, size);
        return ptr;
    }

    // Otherwise, allocate new block and copy data
    void* new_ptr = heap_alloc(size);
    if (!new_ptr) return NULL;

    memcpy(new_ptr, ptr, old_size);
    heap_free(ptr);

    return new_ptr;
}

void heap_get_stats(struct heap_stats* stats) {
    if (stats) {
        *stats = heap_statistics;
    }
}

bool heap_check(void) {
    struct heap_block* current = heap_start;
    size_t total_size = 0;
    size_t used_size = 0;
    size_t free_size = 0;
    size_t total_blocks = 0;
    size_t free_blocks = 0;

    while (current) {
        // Check magic number
        if (current->magic != HEAP_BLOCK_MAGIC) return false;

        // Check pointers
        if (current->next && current->next->prev != current) return false;
        if (current->prev && current->prev->next != current) return false;

        total_size += current->size;
        total_blocks++;

        if (current->flags & BLOCK_FREE) {
            free_size += current->size;
            free_blocks++;
        } else {
            used_size += current->size;
        }

        if (current->flags & BLOCK_LAST) break;
        current = current->next;
    }

    // Verify statistics
    return (total_size == heap_statistics.total_size &&
            used_size == heap_statistics.used_size &&
            free_size == heap_statistics.free_size &&
            total_blocks == heap_statistics.total_blocks &&
            free_blocks == heap_statistics.free_blocks);
}