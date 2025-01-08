#include <mm/vmm.h>
#include <mm/pmm.h>
#include <utils/mem.h>
#include <core/attributes.h>

extern volatile struct limine_hhdm_request hhdm_request;

typedef struct {
    uint64_t value;
} __attribute__((packed)) pte_t;

typedef struct {
    pte_t entries[512];
} __attribute__((aligned(PAGE_SIZE_4K))) page_table_t;

static inline void* phys_to_virt(uint64_t phys) {
    if (!phys) return NULL;
    return (void*)(phys + hhdm_request.response->offset);
}

void vmm_init(void) {
    // Get the current CR3 value
    uint64_t current_cr3;
    __asm volatile ("mov %0, cr3" : "=r"(current_cr3));
    
    // Allocate a new PML4
    uint64_t pml4_phys = (uint64_t)pmm_alloc_page();
    if (!pml4_phys) return;
    
    page_table_t* pml4 = phys_to_virt(pml4_phys);
    page_table_t* cur_pml4 = phys_to_virt(current_cr3);
    
    // Copy the entire existing PML4 first
    memcpy(pml4, cur_pml4, PAGE_SIZE_4K);
    
    // Switch to the new PML4
    __asm volatile ("mov cr3, %0" :: "r"(pml4_phys) : "memory");
}

// Minimal required functions
bool vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    return true;
}

void vmm_switch_pagemap(uint64_t pml4_phys) {
    __asm volatile ("mov cr3, %0" :: "r"(pml4_phys) : "memory");
}

uint64_t vmm_get_cr3(void) {
    uint64_t cr3;
    __asm volatile ("mov %0, cr3" : "=r"(cr3));
    return cr3;
}
