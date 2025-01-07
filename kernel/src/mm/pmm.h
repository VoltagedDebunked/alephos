#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stdbool.h>
#include <limine.h>
#include <stddef.h>

// 4KB pages
#define PAGE_SIZE 4096
#define PAGE_ALIGN(addr) ((addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

void pmm_init(struct limine_memmap_response *memmap);
void *pmm_alloc_page(void);
void *pmm_alloc_pages(size_t count);
void pmm_free_page(void *addr);
void pmm_free_pages(void *addr, size_t count);
size_t pmm_get_free_pages(void);

#endif // PMM_H