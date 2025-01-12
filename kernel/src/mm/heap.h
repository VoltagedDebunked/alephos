#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Size constants
#define HEAP_BLOCK_MAGIC    0x1BADB002
#define HEAP_INITIAL_SIZE   0x100000    // 1MB initial heap
#define HEAP_MIN_BLOCK_SIZE sizeof(struct heap_block)
#define HEAP_PAGE_SIZE      4096

// Block flags
#define BLOCK_FREE          0x1
#define BLOCK_LAST          0x2

// Heap block structure
struct heap_block {
    uint32_t magic;           // Magic number for validation
    uint32_t size;            // Size of the block including header
    uint32_t flags;           // Block flags (free/used, last block)
    struct heap_block* next;  // Next block in list
    struct heap_block* prev;  // Previous block in list
    uint8_t data[];          // Actual data starts here
} __attribute__((packed));

// Heap statistics
struct heap_stats {
    size_t total_size;        // Total heap size
    size_t used_size;         // Used memory size
    size_t free_size;         // Free memory size
    size_t total_blocks;      // Total number of blocks
    size_t free_blocks;       // Number of free blocks
};

// Initialize the heap allocator
bool heap_init(void);

// Allocate memory from heap
void* heap_alloc(size_t size);

// Free memory back to heap
void heap_free(void* ptr);

// Reallocate memory block
void* heap_realloc(void* ptr, size_t size);

// Get heap statistics
void heap_get_stats(struct heap_stats* stats);

// Debug function to check heap consistency
bool heap_check(void);

#endif // HEAP_H