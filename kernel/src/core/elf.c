#include <core/elf.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <utils/mem.h>
#include <core/idt.h>

// Logging and debug
#include <graphics/display.h>
#include <graphics/colors.h>

#define DEBUG_ELF 1

extern struct limine_framebuffer* global_framebuffer;

// Macros for page alignment
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE_4K - 1))
#define PAGE_ALIGN_UP(addr) (((addr) + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1))
#define PAGE_OFFSET(addr) ((addr) & (PAGE_SIZE_4K - 1))

// ELF validation and loading errors
typedef enum {
    ELF_LOAD_SUCCESS = 0,
    ELF_ERR_INVALID_MAGIC = -1,
    ELF_ERR_UNSUPPORTED_CLASS = -2,
    ELF_ERR_UNSUPPORTED_MACHINE = -3,
    ELF_ERR_INVALID_FILE_TYPE = -4,
    ELF_ERR_SEGMENT_OVERLAP = -5,
    ELF_ERR_MEMORY_ALLOCATION = -6,
    ELF_ERR_SEGMENT_MAPPING = -7,
    ELF_ERR_DATA_TRUNCATED = -8
} Elf_Load_Error;

// Segment information for tracking
typedef struct {
    uint64_t vaddr_start;
    uint64_t vaddr_end;
    uint64_t file_offset;
    uint64_t file_size;
    uint64_t mem_size;
    uint32_t flags;
} Elf_Segment_Info;

// Debug logging
static void log_elf_error(Elf_Load_Error error) {
#if DEBUG_ELF
    const char* error_msg;
    switch (error) {
        case ELF_ERR_INVALID_MAGIC:
            error_msg = "Invalid ELF magic number";
            break;
        case ELF_ERR_UNSUPPORTED_CLASS:
            error_msg = "Unsupported ELF class (not 64-bit)";
            break;
        case ELF_ERR_UNSUPPORTED_MACHINE:
            error_msg = "Unsupported machine type";
            break;
        case ELF_ERR_INVALID_FILE_TYPE:
            error_msg = "Invalid ELF file type";
            break;
        case ELF_ERR_SEGMENT_OVERLAP:
            error_msg = "ELF segments overlap";
            break;
        case ELF_ERR_MEMORY_ALLOCATION:
            error_msg = "Failed to allocate memory for ELF segments";
            break;
        case ELF_ERR_SEGMENT_MAPPING:
            error_msg = "Failed to map ELF segment";
            break;
        case ELF_ERR_DATA_TRUNCATED:
            error_msg = "ELF file data is truncated";
            break;
        default:
            error_msg = "Unknown ELF loading error";
    }

    draw_string(global_framebuffer, "ELF Loading Error: ", 0, 220, RED);
    draw_string(global_framebuffer, error_msg, 0, 240, RED);
#endif
}

// Validate ELF header integrity and basic requirements
static Elf_Load_Error validate_elf_header(Elf64_Ehdr* header, size_t data_size) {
    // Check magic number
    if (header->e_magic != ELF_MAGIC) {
        return ELF_ERR_INVALID_MAGIC;
    }

    // Check 64-bit class
    if (header->e_class != ELFCLASS64) {
        return ELF_ERR_UNSUPPORTED_CLASS;
    }

    // Validate header fits within file
    if (sizeof(Elf64_Ehdr) > data_size) {
        return ELF_ERR_DATA_TRUNCATED;
    }

    // Validate program header count and offset
    if (header->e_phnum == 0 ||
        header->e_phoff == 0 ||
        header->e_phoff + (header->e_phnum * sizeof(Elf64_Phdr)) > data_size) {
        return ELF_ERR_DATA_TRUNCATED;
    }

    // Currently only supporting x86_64 executable files
    if (header->e_machine != EM_X86_64) {
        return ELF_ERR_UNSUPPORTED_MACHINE;
    }

    // Only support executable files
    if (header->e_type != ET_EXEC) {
        return ELF_ERR_INVALID_FILE_TYPE;
    }

    return ELF_LOAD_SUCCESS;
}

// Gather and validate segment information
static Elf_Load_Error collect_segment_info(
    Elf64_Ehdr* header,
    size_t data_size,
    Elf_Segment_Info* segments,
    uint16_t* segment_count
) {
    Elf64_Phdr* pheaders = (Elf64_Phdr*)((uint8_t*)header + header->e_phoff);
    *segment_count = 0;

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        // Only process loadable segments
        if (pheaders[i].p_type != PT_LOAD) {
            continue;
        }

        // Validate segment data fits in file
        if (pheaders[i].p_offset + pheaders[i].p_filesz > data_size) {
            return ELF_ERR_DATA_TRUNCATED;
        }

        // Track segment info
        Elf_Segment_Info* seg = &segments[*segment_count];
        seg->vaddr_start = PAGE_ALIGN_DOWN(pheaders[i].p_vaddr);
        seg->vaddr_end = PAGE_ALIGN_UP(pheaders[i].p_vaddr + pheaders[i].p_memsz);
        seg->file_offset = pheaders[i].p_offset;
        seg->file_size = pheaders[i].p_filesz;
        seg->mem_size = pheaders[i].p_memsz;
        seg->flags = pheaders[i].p_flags;

        (*segment_count)++;
    }

    // Check for segment overlaps
    for (uint16_t i = 0; i < *segment_count; i++) {
        for (uint16_t j = i + 1; j < *segment_count; j++) {
            if (!(segments[j].vaddr_end <= segments[i].vaddr_start ||
                  segments[j].vaddr_start >= segments[i].vaddr_end)) {
                return ELF_ERR_SEGMENT_OVERLAP;
            }
        }
    }

    return ELF_LOAD_SUCCESS;
}

// Allocate and map memory for segments
static Elf_Load_Error map_segments(
    void* elf_data,
    Elf_Segment_Info* segments,
    uint16_t segment_count
) {
    for (uint16_t i = 0; i < segment_count; i++) {
        Elf_Segment_Info* seg = &segments[i];

        // Allocate physical pages for the entire segment range
        for (uint64_t page = seg->vaddr_start; page < seg->vaddr_end; page += PAGE_SIZE_4K) {
            uint64_t phys_page = (uint64_t)pmm_alloc_page();
            if (!phys_page) {
                return ELF_ERR_MEMORY_ALLOCATION;
            }

            // Determine page permissions
            uint64_t page_flags = PTE_PRESENT | PTE_USER;
            if (seg->flags & PF_W) page_flags |= PTE_WRITABLE;
            if (seg->flags & PF_X) page_flags |= PTE_WRITABLE;  // Executable segments require write for now

            // Map page
            if (!vmm_map_page(page, phys_page, page_flags)) {
                return ELF_ERR_SEGMENT_MAPPING;
            }
        }

        // Zero out the memory first (including uninitialized parts)
        memset((void*)seg->vaddr_start, 0, seg->vaddr_end - seg->vaddr_start);

        // Copy segment data if it exists
        if (seg->file_size > 0) {
            memcpy(
                (void*)(seg->vaddr_start + PAGE_OFFSET(segments[i].vaddr_start)),
                (uint8_t*)elf_data + seg->file_offset,
                seg->file_size
            );
        }
    }

    return ELF_LOAD_SUCCESS;
}

// Main ELF executable loading function
int elf_load_executable(void* elf_data, size_t data_size, uint64_t* entry_point) {
    Elf64_Ehdr* header = (Elf64_Ehdr*)elf_data;

    // Validate ELF header
    Elf_Load_Error validation_result = validate_elf_header(header, data_size);
    if (validation_result != ELF_LOAD_SUCCESS) {
        log_elf_error(validation_result);
        return validation_result;
    }

    // Collect and validate segment information
    Elf_Segment_Info segments[16];  // Limit to 16 loadable segments
    uint16_t segment_count = 0;
    validation_result = collect_segment_info(header, data_size, segments, &segment_count);
    if (validation_result != ELF_LOAD_SUCCESS) {
        log_elf_error(validation_result);
        return validation_result;
    }

    // Map and load segments
    validation_result = map_segments(elf_data, segments, segment_count);
    if (validation_result != ELF_LOAD_SUCCESS) {
        log_elf_error(validation_result);
        return validation_result;
    }

    // Set entry point if requested
    if (entry_point) {
        *entry_point = header->e_entry;
    }

    return ELF_LOAD_SUCCESS;
}

// Extended validation function
bool elf_validate(void* elf_data, size_t data_size, Elf_Validation_Result* result) {
    if (!elf_data || !result || data_size < sizeof(Elf64_Ehdr)) {
        return false;
    }

    Elf64_Ehdr* header = (Elf64_Ehdr*)elf_data;

    // Do full header validation
    Elf_Load_Error validation_result = validate_elf_header(header, data_size);

    result->valid = (validation_result == ELF_LOAD_SUCCESS);

    if (result->valid) {
        result->entry_point = header->e_entry;
        result->machine_type = header->e_machine;
        result->file_type = header->e_type;
    }

    return result->valid;
}

// Find interpreter for dynamically linked executables
void* elf_get_interpreter(void* elf_data, size_t data_size) {
    Elf64_Ehdr* header = (Elf64_Ehdr*)elf_data;

    // Validate header first
    if (validate_elf_header(header, data_size) != ELF_LOAD_SUCCESS) {
        return NULL;
    }

    // Scan program headers for interpreter
    Elf64_Phdr* pheaders = (Elf64_Phdr*)((uint8_t*)elf_data + header->e_phoff);

    for (uint16_t i = 0; i < header->e_phnum; i++) {
        if (pheaders[i].p_type == PT_INTERP) {
            // Check interpreter string is within bounds
            if (pheaders[i].p_offset + pheaders[i].p_filesz > data_size) {
                return NULL;
            }

            // Return pointer to interpreter path
            return (void*)((uint8_t*)elf_data + pheaders[i].p_offset);
        }
    }

    return NULL;
}