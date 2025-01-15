#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stdbool.h>

// Process states
typedef enum {
    PROCESS_STATE_NEW,
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_TERMINATED
} process_state_t;

// Process control block structure
typedef struct process {
    uint32_t pid;                    // Process ID
    process_state_t state;           // Process state
    uint64_t *stack;                 // Kernel stack pointer
    uint64_t stack_size;             // Stack size
    struct cpu_state *cpu_state;     // Saved CPU state
    struct process *next;            // Next process in queue
    uint32_t time_slice;             // Time slice for scheduling
    uint32_t time_used;              // Time used in current slice
    char name[32];                   // Process name
    uint32_t priority;               // Process priority
    void *page_directory;            // Process page directory
} process_t;

// CPU state structure (saved during context switch)
typedef struct cpu_state {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rdi, rsi, rbp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, cs;
    uint64_t rflags;
    uint64_t rsp, ss;
} __attribute__((packed)) cpu_state_t;

// Function declarations
void process_init(void);
process_t *process_create(void (*entry)(void), uint32_t priority, const char *name);
void process_destroy(process_t *process);
void process_switch(process_t *next);
void scheduler_init(void);
void scheduler_add(process_t *process);
void scheduler_remove(process_t *process);
void scheduler_tick(void);
process_t *scheduler_next(void);
process_t *get_current_process(void);

#endif // PROCESS_H