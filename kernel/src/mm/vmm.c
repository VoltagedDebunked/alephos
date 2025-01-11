#include <mm/vmm.h>
#include <mm/pmm.h>
#include <utils/mem.h>
#include <core/attributes.h>
#include <utils/asm.h>

extern volatile struct limine_hhdm_request hhdm_request;

// Page table entry structure
typedef uint64_t page_entry_t;

// Page table structure (4KB aligned)
typedef struct {
    page_entry_t entries[512];
} __attribute__((aligned(0x1000))) page_table_t;

// Current page map level 4 table
static page_table_t* current_pml4 = NULL;
static uint64_t hhdm_offset = 0;

// Convert physical address to virtual using HHDM
static inline void* phys_to_virt(uint64_t phys) {
    if (!phys) return NULL;
    return (void*)(phys + hhdm_offset);
}

// Convert virtual address to physical
static inline uint64_t virt_to_phys(void* virt) {
    if (!virt) return 0;
    return (uint64_t)virt - hhdm_offset;
}

// Get indices for different page table levels
static inline void get_page_indices(uint64_t vaddr,
                                  uint64_t* pml4_index,
                                  uint64_t* pdp_index,
                                  uint64_t* pd_index,
                                  uint64_t* pt_index) {
    *pml4_index = (vaddr >> 39) & 0x1FF;
    *pdp_index = (vaddr >> 30) & 0x1FF;
    *pd_index = (vaddr >> 21) & 0x1FF;
    *pt_index = (vaddr >> 12) & 0x1FF;
}

// Create a new page table
static page_table_t* create_page_table(void) {
    void* phys = pmm_alloc_page();
    if (!phys) return NULL;

    page_table_t* table = phys_to_virt((uint64_t)phys);
    memset(table, 0, sizeof(page_table_t));
    return table;
}

#define IDENTITY_MAP_SIZE 0x100000  // First 1MB

void vmm_init(void) {
    hhdm_offset = hhdm_request.response->offset;

    // Get current PML4
    uint64_t cr3;
    asm volatile(
        "mov rax, cr3"
        : "=a"(cr3)
        :: "memory"
    );

    current_pml4 = phys_to_virt(cr3 & ~0xFFF);

    // Create new PML4
    page_table_t* new_pml4 = create_page_table();
    if (!new_pml4) {
        return;
    }

    // Copy existing mappings
    memcpy(new_pml4, current_pml4, sizeof(page_table_t));

    // Identity map first 1MB for hardware access
    for (uint64_t addr = 0; addr < IDENTITY_MAP_SIZE; addr += PAGE_SIZE) {
        vmm_map_page(addr, addr, PTE_PRESENT | PTE_WRITABLE);
    }

    // Switch to new PML4
    vmm_switch_pagemap(virt_to_phys(new_pml4));
    current_pml4 = new_pml4;
}

bool vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t pml4_index, pdp_index, pd_index, pt_index;
    get_page_indices(virt, &pml4_index, &pdp_index, &pd_index, &pt_index);

    // Get or create PDP
    page_table_t* pdp;
    if (current_pml4->entries[pml4_index] & PTE_PRESENT) {
        pdp = phys_to_virt(current_pml4->entries[pml4_index] & ~0xFFF);
    } else {
        pdp = create_page_table();
        if (!pdp) return false;

        current_pml4->entries[pml4_index] = virt_to_phys(pdp) | flags | PTE_PRESENT;
    }

    // Get or create PD
    page_table_t* pd;
    if (pdp->entries[pdp_index] & PTE_PRESENT) {
        pd = phys_to_virt(pdp->entries[pdp_index] & ~0xFFF);
    } else {
        pd = create_page_table();
        if (!pd) return false;

        pdp->entries[pdp_index] = virt_to_phys(pd) | flags | PTE_PRESENT;
    }

    // Get or create PT
    page_table_t* pt;
    if (pd->entries[pd_index] & PTE_PRESENT) {
        pt = phys_to_virt(pd->entries[pd_index] & ~0xFFF);
    } else {
        pt = create_page_table();
        if (!pt) return false;

        pd->entries[pd_index] = virt_to_phys(pt) | flags | PTE_PRESENT;
    }

    // Map the actual page
    pt->entries[pt_index] = phys | flags | PTE_PRESENT;

    // Invalidate TLB for this page
    asm volatile(
        "invlpg [%0]"
        :
        : "r"(virt)
        : "memory"
    );

    return true;
}

void vmm_switch_pagemap(uint64_t pml4_phys) {
    asm volatile(
        "mov rax, %0\n"
        "mov cr3, rax"
        :
        : "r"(pml4_phys)
        : "rax", "memory"
    );
}

uint64_t vmm_get_cr3(void) {
    uint64_t cr3;
    asm volatile(
        "mov rax, cr3"
        : "=a"(cr3)
        :: "memory"
    );
    return cr3;
}

bool vmm_unmap_page(uint64_t virt) {
    uint64_t pml4_index, pdp_index, pd_index, pt_index;
    get_page_indices(virt, &pml4_index, &pdp_index, &pd_index, &pt_index);

    // Walk the page tables
    if (!(current_pml4->entries[pml4_index] & PTE_PRESENT)) {
        return false;
    }

    page_table_t* pdp = phys_to_virt(current_pml4->entries[pml4_index] & ~0xFFF);
    if (!(pdp->entries[pdp_index] & PTE_PRESENT)) {
        return false;
    }

    page_table_t* pd = phys_to_virt(pdp->entries[pdp_index] & ~0xFFF);
    if (!(pd->entries[pd_index] & PTE_PRESENT)) {
        return false;
    }

    page_table_t* pt = phys_to_virt(pd->entries[pd_index] & ~0xFFF);
    if (!(pt->entries[pt_index] & PTE_PRESENT)) {
        return false;
    }

    // Clear the page table entry
    pt->entries[pt_index] = 0;

    // Invalidate TLB for this page
    asm volatile(
        "invlpg [%0]"
        :
        : "r"(virt)
        : "memory"
    );

    return true;
}

uint64_t vmm_get_phys_addr(uint64_t virt) {
    uint64_t pml4_index, pdp_index, pd_index, pt_index;
    get_page_indices(virt, &pml4_index, &pdp_index, &pd_index, &pt_index);

    // Walk the page tables
    if (!(current_pml4->entries[pml4_index] & PTE_PRESENT)) {
        return 0;
    }

    page_table_t* pdp = phys_to_virt(current_pml4->entries[pml4_index] & ~0xFFF);
    if (!(pdp->entries[pdp_index] & PTE_PRESENT)) {
        return 0;
    }

    page_table_t* pd = phys_to_virt(pdp->entries[pdp_index] & ~0xFFF);
    if (!(pd->entries[pd_index] & PTE_PRESENT)) {
        return 0;
    }

    page_table_t* pt = phys_to_virt(pd->entries[pd_index] & ~0xFFF);
    if (!(pt->entries[pt_index] & PTE_PRESENT)) {
        return 0;
    }

    // Get the physical address from the page table entry
    return (pt->entries[pt_index] & ~0xFFF) | (virt & 0xFFF);
}