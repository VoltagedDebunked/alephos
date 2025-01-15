#ifndef SHARED_DEFS_H
#define SHARED_DEFS_H

#include <stdint.h>
#include <stdbool.h>

// Maximum values
#define MAX_FDS 256
#define MAX_PROCESSES 256

// File descriptor structure
typedef struct {
    uint32_t inode;      // EXT2 inode number
    uint32_t offset;     // Current file offset
    uint32_t flags;      // Open flags
    uint16_t refcount;   // Number of references (for dup)
} file_descriptor_t;

// Process tracking structure
typedef struct process_info {
    int pid;                    // Process ID
    int parent_pid;            // Parent process ID
    int exit_status;           // Exit status if terminated
    bool terminated;           // Whether process has terminated
    struct process_info* next; // Next in process list
} process_info_t;

// Select syscall fd_set structure
typedef struct {
    uint32_t fds_bits[MAX_FDS/(8*sizeof(uint32_t))];
} fd_set;

// Timeval structure for select
struct timeval {
    long tv_sec;     // seconds
    long tv_usec;    // microseconds
};

// System information structure
struct sysinfo {
    long uptime;             // Seconds since boot
    unsigned long loads[3];  // 1, 5, and 15 minute load averages
    unsigned long totalram;  // Total usable main memory size
    unsigned long freeram;   // Available memory size
    unsigned long sharedram; // Amount of shared memory
    unsigned long bufferram; // Memory used by buffers
    unsigned long totalswap; // Total swap space size
    unsigned long freeswap;  // Swap space still available
    uint16_t procs;         // Number of current processes
    uint16_t pad;           // Padding
    unsigned long totalhigh; // Total high memory size
    unsigned long freehigh;  // Available high memory size
    uint32_t mem_unit;      // Memory unit size in bytes
};

#endif // SHARED_DEFS_H