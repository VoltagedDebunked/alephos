#include <core/process.h>
#include <mm/heap.h>
#include <mm/pmm.h>
#include <utils/mem.h>
#include <utils/str.h>
#include <core/idt.h>

#define MAX_PROCESSES 256
#define STACK_SIZE    16384  // 16KB stack

static process_t *current_process = NULL;
static process_t *process_list = NULL;
static uint32_t next_pid = 1;
static uint32_t process_count = 0;

// Round-robin scheduler queue
static process_t *ready_queue_head = NULL;
static process_t *ready_queue_tail = NULL;

// Initialize process management
void process_init(void) {
    current_process = NULL;
    process_list = NULL;
    next_pid = 1;
    process_count = 0;
    ready_queue_head = NULL;
    ready_queue_tail = NULL;
}

// Create a new process
process_t *process_create(void (*entry)(void), uint32_t priority, const char *name) {
    if (process_count >= MAX_PROCESSES) {
        return NULL;
    }

    // Allocate process structure
    process_t *process = malloc(sizeof(process_t));
    if (!process) {
        return NULL;
    }

    // Initialize process control block
    process->pid = next_pid++;
    process->state = PROCESS_STATE_NEW;
    process->stack_size = STACK_SIZE;
    process->next = NULL;
    process->time_slice = 100;  // 100ms default time slice
    process->time_used = 0;
    process->priority = priority;
    strncpy(process->name, name, 31);
    process->name[31] = '\0';

    // Allocate stack
    process->stack = pmm_alloc_page();
    if (!process->stack) {
        free(process);
        return NULL;
    }

    // Allocate and initialize CPU state at top of stack
    process->cpu_state = (cpu_state_t*)(process->stack + STACK_SIZE - sizeof(cpu_state_t));
    memset(process->cpu_state, 0, sizeof(cpu_state_t));

    // Set initial CPU state
    process->cpu_state->rip = (uint64_t)entry;
    process->cpu_state->rflags = 0x202;  // Interrupts enabled
    process->cpu_state->cs = 0x08;       // Kernel code segment
    process->cpu_state->ss = 0x10;       // Kernel data segment
    process->cpu_state->rsp = (uint64_t)(process->stack + STACK_SIZE);

    // Add to process list
    process->next = process_list;
    process_list = process;
    process_count++;

    return process;
}

// Destroy a process
void process_destroy(process_t *process) {
    if (!process) return;

    // Remove from scheduler queue
    scheduler_remove(process);

    // Remove from process list
    if (process_list == process) {
        process_list = process->next;
    } else {
        process_t *prev = process_list;
        while (prev && prev->next != process) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = process->next;
        }
    }

    // Free resources
    pmm_free_page(process->stack);
    free(process);
    process_count--;
}

// Assembly function declarations
extern void context_switch(cpu_state_t *old_state, cpu_state_t *new_state);

// Switch to another process
void process_switch(process_t *next) {
    if (!next || next == current_process) return;

    process_t *prev = current_process;
    current_process = next;

    if (prev) {
        context_switch(prev->cpu_state, next->cpu_state);
    } else {
        context_switch(NULL, next->cpu_state);
    }
}

// Initialize scheduler
void scheduler_init(void) {
    ready_queue_head = NULL;
    ready_queue_tail = NULL;
}

// Add process to scheduler queue
void scheduler_add(process_t *process) {
    if (!process) return;

    process->state = PROCESS_STATE_READY;
    process->next = NULL;

    if (!ready_queue_tail) {
        ready_queue_head = ready_queue_tail = process;
    } else {
        ready_queue_tail->next = process;
        ready_queue_tail = process;
    }
}

// Remove process from scheduler queue
void scheduler_remove(process_t *process) {
    if (!process) return;

    if (ready_queue_head == process) {
        ready_queue_head = process->next;
        if (!ready_queue_head) {
            ready_queue_tail = NULL;
        }
    } else {
        process_t *prev = ready_queue_head;
        while (prev && prev->next != process) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = process->next;
            if (ready_queue_tail == process) {
                ready_queue_tail = prev;
            }
        }
    }

    process->next = NULL;
}

// Timer tick handler for scheduler
void scheduler_tick(void) {
    if (!current_process) return;

    current_process->time_used++;
    if (current_process->time_used >= current_process->time_slice) {
        // Time slice expired, move to back of queue
        process_t *next = scheduler_next();
        if (next) {
            current_process->time_used = 0;
            scheduler_add(current_process);
            process_switch(next);
        }
    }
}

// Get next process to run
process_t *scheduler_next(void) {
    if (!ready_queue_head) return NULL;

    process_t *next = ready_queue_head;
    ready_queue_head = ready_queue_head->next;
    if (!ready_queue_head) {
        ready_queue_tail = NULL;
    }
    next->next = NULL;

    return next;
}

// Get current running process
process_t *get_current_process(void) {
    return current_process;
}
