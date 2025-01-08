#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>
#include <mm/pmm.h>

// Page table entry flags
#define PTE_PRESENT          (1ULL << 0)
#define PTE_WRITABLE        (1ULL << 1)
#define PTE_USER            (1ULL << 2)
#define PTE_WRITETHROUGH    (1ULL << 3)
#define PTE_NOCACHE         (1ULL << 4)
#define PTE_ACCESSED        (1ULL << 5)
#define PTE_DIRTY           (1ULL << 6)
#define PTE_HUGE            (1ULL << 7)
#define PTE_GLOBAL          (1ULL << 8)
#define PTE_NX              (1ULL << 63)

// Page sizes
#define PAGE_SIZE_4K    0x1000

// Virtual memory regions
#define KERNEL_VIRT_BASE     0xFFFFFFFF80000000ULL
#define KERNEL_PHYS_BASE     0x0000000000100000ULL

void vmm_init(void);
bool vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_switch_pagemap(uint64_t pml4_phys);
uint64_t vmm_get_cr3(void);

#endif