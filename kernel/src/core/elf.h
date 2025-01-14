#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ELF Magic Number
#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little-endian

// ELF Class (32-bit or 64-bit)
#define ELFCLASS32 1
#define ELFCLASS64 2

// ELF Data Encoding
#define ELFDATA2LSB 1  // Little-endian
#define ELFDATA2MSB 2  // Big-endian

// ELF File Types
#define ET_NONE     0  // No file type
#define ET_REL      1  // Relocatable file
#define ET_EXEC     2  // Executable file
#define ET_DYN      3  // Shared object file
#define ET_CORE     4  // Core file

// ELF Machine Types
#define EM_386      3   // x86
#define EM_X86_64   62  // x86-64

// Program Header Types
#define PT_NULL     0   // Unused entry
#define PT_LOAD     1   // Loadable segment
#define PT_DYNAMIC  2   // Dynamic linking information
#define PT_INTERP   3   // Interpreter path
#define PT_NOTE     4   // Auxiliary information
#define PT_PHDR     6   // Program header table itself

// Program Segment Flags
#define PF_X        (1 << 0)  // Executable
#define PF_W        (1 << 1)  // Writable
#define PF_R        (1 << 2)  // Readable

// ELF 64-bit Header Structure
typedef struct {
    uint32_t e_magic;       // Magic number
    uint8_t  e_class;       // 32-bit or 64-bit
    uint8_t  e_data;        // Data encoding
    uint8_t  e_version;     // ELF version
    uint8_t  e_osabi;       // OS ABI identification
    uint8_t  e_abiversion;  // ABI version
    uint8_t  e_pad[7];      // Padding
    uint16_t e_type;        // Object file type
    uint16_t e_machine;     // Machine type
    uint32_t e_version2;    // Object file version
    uint64_t e_entry;       // Entry point virtual address
    uint64_t e_phoff;       // Program header table file offset
    uint64_t e_shoff;       // Section header table file offset
    uint32_t e_flags;       // Processor-specific flags
    uint16_t e_ehsize;      // ELF header size
    uint16_t e_phentsize;   // Program header entry size
    uint16_t e_phnum;       // Program header table entry count
    uint16_t e_shentsize;   // Section header entry size
    uint16_t e_shnum;       // Section header table entry count
    uint16_t e_shstrndx;    // Section header string table index
} __attribute__((packed)) Elf64_Ehdr;

// Program Header Structure
typedef struct {
    uint32_t p_type;        // Segment type
    uint32_t p_flags;       // Segment flags
    uint64_t p_offset;      // Segment file offset
    uint64_t p_vaddr;       // Segment virtual address
    uint64_t p_paddr;       // Segment physical address
    uint64_t p_filesz;      // Segment size in file
    uint64_t p_memsz;       // Segment size in memory
    uint64_t p_align;       // Segment alignment
} __attribute__((packed)) Elf64_Phdr;

// ELF Section Header Structure
typedef struct {
    uint32_t sh_name;       // Section name index in string table
    uint32_t sh_type;       // Section type
    uint64_t sh_flags;      // Section attributes
    uint64_t sh_addr;       // Virtual address in memory
    uint64_t sh_offset;     // Offset in file
    uint64_t sh_size;       // Size of section
    uint32_t sh_link;       // Section index link
    uint32_t sh_info;       // Additional information
    uint64_t sh_addralign;  // Address alignment
    uint64_t sh_entsize;    // Size of entries (if section has table)
} __attribute__((packed)) Elf64_Shdr;

// Section Types
#define SHT_NULL        0   // Inactive section header
#define SHT_PROGBITS    1   // Program data
#define SHT_SYMTAB      2   // Symbol table
#define SHT_STRTAB      3   // String table
#define SHT_RELA        4   // Relocation entries with addends
#define SHT_HASH        5   // Symbol hash table
#define SHT_DYNAMIC     6   // Dynamic linking information
#define SHT_NOTE        7   // Notes
#define SHT_NOBITS      8   // Uninitialized data
#define SHT_REL         9   // Relocation entries

// Section Attribute Flags
#define SHF_WRITE       (1 << 0)  // Writable section
#define SHF_ALLOC       (1 << 1)  // Occupies memory during execution
#define SHF_EXECINSTR   (1 << 2)  // Contains executable instructions

// ELF Validation Result Structure
typedef struct {
    bool valid;             // Overall validation status
    uint64_t entry_point;   // Program entry point
    uint16_t machine_type;  // Target machine architecture
    uint16_t file_type;     // ELF file type
} Elf_Validation_Result;

// ELF Symbol Table Entry (64-bit)
typedef struct {
    uint32_t st_name;       // Symbol name index in string table
    uint8_t  st_info;       // Symbol type and binding attributes
    uint8_t  st_other;      // Visibility
    uint16_t st_shndx;      // Section header index
    uint64_t st_value;      // Symbol value
    uint64_t st_size;       // Symbol size
} __attribute__((packed)) Elf64_Sym;

// Symbol Binding
#define STB_LOCAL   0   // Local symbol
#define STB_GLOBAL  1   // Global symbol
#define STB_WEAK    2   // Weak symbol

// Symbol Types
#define STT_NOTYPE   0   // No type
#define STT_OBJECT   1   // Data object
#define STT_FUNC     2   // Function
#define STT_SECTION  3   // Section
#define STT_FILE     4   // Source file

// Function declarations for ELF handling
int elf_load_executable(void* elf_data, size_t data_size, uint64_t* entry_point);
bool elf_validate(void* elf_data, size_t data_size, Elf_Validation_Result* result);
void* elf_get_interpreter(void* elf_data, size_t data_size);

#endif // ELF_H