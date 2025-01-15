#include <core/syscalls.h>
#include <core/process.h>
#include <core/elf.h>
#include <fs/ext2.h>
#include <mm/heap.h>
#include <mm/vmm.h>
#include <utils/mem.h>
#include <utils/str.h>
#include <core/drivers/serial/serial.h>
#include <core/drivers/net/netdev.h>
#include <net/net.h>

#undef MAX_PROCESSES
#define MAX_PROCESSES 256

// Process tracking structure
typedef struct {
    int pid;                // Process ID
    int parent_pid;         // Parent Process ID
    int status;             // Exit status
    bool exited;            // Has the process exited?
    struct process* proc;   // Pointer to process structure
    int signal;             // Pending signal
} process_info_t;

// Global process tracking array
static process_info_t process_tracking[MAX_PROCESSES];

// Find a free slot in process tracking
static int find_free_process_slot() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (!process_tracking[i].proc) {
            return i;
        }
    }
    return -1;
}

// Find process info by PID
static process_info_t* find_process_by_pid(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_tracking[i].proc && process_tracking[i].pid == pid) {
            return &process_tracking[i];
        }
    }
    return NULL;
}

// Find child processes of a given parent PID
static int find_child_processes(int parent_pid, int* child_pids, int max_children) {
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_tracking[i].proc &&
            process_tracking[i].parent_pid == parent_pid) {

            if (count < max_children) {
                child_pids[count] = process_tracking[i].pid;
            }
            count++;
        }
    }
    return count;
}

// Memory mapping for arguments and environment
typedef struct {
    char* argv[MAX_ARGS];
    char* envp[MAX_ENV];
    char arg_buffer[MAX_ARGS * MAX_ARG_LENGTH];
    char env_buffer[MAX_ENV * MAX_ARG_LENGTH];
    int argc;
    int envc;
} exec_args_t;

// Prepare arguments for exec
static bool prepare_exec_args(exec_args_t* args, char* const argv[], char* const envp[]) {
    // Reset all pointers and buffers
    memset(args, 0, sizeof(exec_args_t));

    // Copy arguments
    if (argv) {
        for (args->argc = 0;
             argv[args->argc] && args->argc < MAX_ARGS - 1;
             args->argc++) {

            size_t len = strlen(argv[args->argc]);
            if (len >= MAX_ARG_LENGTH) {
                return false;  // Argument too long
            }

            // Copy argument to buffer
            args->argv[args->argc] = args->arg_buffer + (args->argc * MAX_ARG_LENGTH);
            strncpy(args->argv[args->argc], argv[args->argc], MAX_ARG_LENGTH - 1);
        }
        args->argv[args->argc] = NULL;  // Null terminate
    }

    // Copy environment variables
    if (envp) {
        for (args->envc = 0;
             envp[args->envc] && args->envc < MAX_ENV - 1;
             args->envc++) {

            size_t len = strlen(envp[args->envc]);
            if (len >= MAX_ARG_LENGTH) {
                return false;  // Environment variable too long
            }

            // Copy environment variable to buffer
            args->envp[args->envc] = args->env_buffer + (args->envc * MAX_ARG_LENGTH);
            strncpy(args->envp[args->envc], envp[args->envc], MAX_ARG_LENGTH - 1);
        }
        args->envp[args->envc] = NULL;  // Null terminate
    }

    return true;
}

// Signal handling function
static void handle_signal(process_info_t* proc_info, int signal) {
    if (!proc_info) return;

    switch (signal) {
        case SIGKILL:
            // Terminate the process immediately
            if (proc_info->proc) {
                proc_info->exited = true;
                proc_info->status = -1;  // Killed

                // Remove from scheduler
                scheduler_remove(proc_info->proc);

                // Free process resources
                process_destroy(proc_info->proc);
                proc_info->proc = NULL;
            }
            break;

        case SIGSTOP:
            // Pause the process
            if (proc_info->proc) {
                proc_info->proc->state = PROCESS_STATE_BLOCKED;
            }
            break;

        case SIGCONT:
            // Resume a stopped process
            if (proc_info->proc) {
                proc_info->proc->state = PROCESS_STATE_READY;
                scheduler_add(proc_info->proc);
            }
            break;

        default:
            // Store pending signal
            proc_info->signal = signal;
            break;
    }
}

// File operations syscalls
int sys_open(const char* pathname, int flags, uint16_t mode) {
    // Find the file in the filesystem
    uint32_t root_inode = EXT2_ROOT_INO;

    // Find the file in the root directory
    uint32_t file_inode = ext2_find_file(root_inode, pathname);

    // If file doesn't exist and O_CREAT is set, create it
    if (!file_inode && (flags & O_CREAT)) {
        file_inode = ext2_create_file(root_inode, pathname,
            (mode & 0777) | ((flags & O_DIRECTORY) ? EXT2_S_IFDIR : EXT2_S_IFREG));
    }

    if (!file_inode) {
        return -1;  // File not found and couldn't be created
    }

    return file_inode;  // Use inode as file descriptor
}

ssize_t sys_read(int fd, void* buf, size_t count) {
    // Handle standard input
    if (fd == STDIN_FILENO) {
        // Basic character read from serial
        if (count > 0) {
            ((char*)buf)[0] = serial_read_char(COM1);
            return 1;
        }
        return 0;
    }

    // Read from file system
    if (count == 0) return 0;

    return ext2_read_file(fd, buf, 0, count) ? count : -1;
}

ssize_t sys_write(int fd, const void* buf, size_t count) {
    // Handle standard output/error
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        // Write each character to serial
        const char* cbuf = (const char*)buf;
        for (size_t i = 0; i < count; i++) {
            serial_write_char(COM1, cbuf[i]);
        }
        return count;
    }

    // Write to file system
    return ext2_write_file(fd, buf, 0, count) ? count : -1;
}

int sys_close(int fd) {
    // Simple close for now
    // In a real implementation, this would free resources
    return 0;
}

off_t sys_lseek(int fd, off_t offset, int whence) {
    // Placeholder implementation
    // Would need more complex file tracking in a full implementation
    return offset;
}

int sys_stat(const char* pathname, struct stat* statbuf) {
    // Find the file in the filesystem
    uint32_t root_inode = EXT2_ROOT_INO;
    uint32_t file_inode = ext2_find_file(root_inode, pathname);

    if (!file_inode) {
        return -1;
    }

    // Get inode information
    struct ext2_inode* inode = ext2_get_inode(file_inode);
    if (!inode) {
        return -1;
    }

    // Copy relevant information
    statbuf->st_mode = inode->i_mode;
    statbuf->st_size = inode->i_size;
    statbuf->st_atime = inode->i_atime;
    statbuf->st_mtime = inode->i_mtime;
    statbuf->st_ctime = inode->i_ctime;

    free(inode);
    return 0;
}

int sys_fstat(int fd, struct stat* statbuf) {
    // Get inode information
    struct ext2_inode* inode = ext2_get_inode(fd);
    if (!inode) {
        return -1;
    }

    // Copy relevant information
    statbuf->st_mode = inode->i_mode;
    statbuf->st_size = inode->i_size;
    statbuf->st_atime = inode->i_atime;
    statbuf->st_mtime = inode->i_ctime;

    free(inode);
    return 0;
}

// Stub function for forked processes to use the saved RIP
static void forked_process_entry(void) {
    // Get the current process
    process_t* current = get_current_process();
    if (!current) {
        // Something went wrong, just halt
        while(1) {
            __asm__ volatile("hlt");
        }
    }

    // Jump to the saved instruction pointer
    void (*entry)(void) = (void (*)(void))current->cpu_state->rip;
    entry();
}

// Fork syscall with advanced tracking
int sys_fork(void) {
    process_t* current = get_current_process();
    if (!current) {
        return -1;
    }

    // Find a free process tracking slot
    int slot = find_free_process_slot();
    if (slot == -1) {
        return -1;  // No free slots
    }

    // Create child process using a stub entry point
    process_t* child_process = process_create(
        forked_process_entry,  // Use stub entry point
        current->priority,
        current->name
    );

    if (!child_process) {
        return -1;
    }

    // Complete copy of parent's CPU state
    memcpy(child_process->cpu_state, current->cpu_state, sizeof(cpu_state_t));

    // Restore the forked process's RIP through the stub entry point
    // The stub will call the actual instruction pointer
    child_process->cpu_state->rip = (uint64_t)forked_process_entry;

    // Set child's return value to 0
    child_process->cpu_state->rax = 0;

    // Add to scheduler
    scheduler_add(child_process);

    // Update process tracking
    process_tracking[slot].pid = child_process->pid;
    process_tracking[slot].parent_pid = current->pid;
    process_tracking[slot].proc = child_process;
    process_tracking[slot].exited = false;
    process_tracking[slot].status = 0;
    process_tracking[slot].signal = 0;

    return child_process->pid;
}

// Exec syscall with comprehensive argument handling
int sys_exec(const char* path, char* const argv[], char* const envp[]) {
    // Validate path
    if (!path || strlen(path) >= MAX_PATH_LENGTH) {
        return -1;
    }

    // Prepare arguments
    exec_args_t args;
    if (!prepare_exec_args(&args, argv, envp)) {
        return -1;
    }

    // Find the file in the filesystem
    uint32_t file_inode = ext2_find_file(EXT2_ROOT_INO, path);
    if (!file_inode) {
        return -1;
    }

    // Read the file
    struct ext2_inode* inode = ext2_get_inode(file_inode);
    if (!inode) {
        return -1;
    }

    // Allocate buffer for executable
    void* elf_data = malloc(inode->i_size);
    if (!elf_data) {
        free(inode);
        return -1;
    }

    // Read file contents
    if (!ext2_read_file(file_inode, elf_data, 0, inode->i_size)) {
        free(elf_data);
        free(inode);
        return -1;
    }
    free(inode);

    // Attempt to load ELF executable
    uint64_t entry_point;
    int result = elf_load_executable(elf_data, inode->i_size, &entry_point);
    free(elf_data);

    if (result != 0) {
        return -1;
    }

    // Get current process
    process_t* current = get_current_process();
    if (!current) {
        return -1;
    }

    // Update current process's instruction pointer
    current->cpu_state->rip = entry_point;

    // Setup argument and environment pointer on the stack
    // Note: In a real implementation, this would involve more complex stack manipulation
    current->cpu_state->rdi = (uint64_t)args.argc;  // argc in rdi
    current->cpu_state->rsi = (uint64_t)args.argv;  // argv in rsi
    current->cpu_state->rdx = (uint64_t)args.envp;  // envp in rdx

    return 0;
}

// Exit syscall with status tracking
void sys_exit(int status) {
    process_t* current = get_current_process();
    if (!current) {
        return;
    }

    // Find process tracking info
    process_info_t* proc_info = find_process_by_pid(current->pid);
    if (proc_info) {
        proc_info->exited = true;
        proc_info->status = status;
    }

    // Mark process as terminated
    current->state = PROCESS_STATE_TERMINATED;

    // Remove from scheduler
    scheduler_remove(current);

    // Free process resources
    process_destroy(current);

    // Switch to next process
    process_t* next = scheduler_next();
    if (next) {
        process_switch(next);
    }
}

// Wait syscall with comprehensive child process tracking
int sys_wait(int* status) {
    process_t* current = get_current_process();
    if (!current) {
        return -1;
    }

    // Find child processes
    int child_pids[MAX_PROCESSES];
    int child_count = find_child_processes(current->pid, child_pids, MAX_PROCESSES);

    if (child_count == 0) {
        // No child processes
        return 0;
    }

    // Find first terminated child
    for (int i = 0; i < child_count; i++) {
        process_info_t* child_info = find_process_by_pid(child_pids[i]);

        if (child_info && child_info->exited) {
            // If status pointer is valid, set status
            if (status) {
                *status = child_info->status;
            }

            // Remove process tracking entry
            child_info->proc = NULL;

            // Return PID of terminated child
            return child_pids[i];
        }
    }

    // No terminated children
    return 0;
}

// Kill syscall with signal handling
int sys_kill(int pid, int signal) {
    process_info_t* target_info = find_process_by_pid(pid);
    if (!target_info) {
        return -1;
    }

    // Handle the signal
    handle_signal(target_info, signal);

    return 0;
}

// Network syscalls
int sys_socket(int domain, int type, int protocol) {
    // Create a network socket
    int sock_fd = net_socket_create(
        type == 1 ? SOCKET_TCP :  // SOCK_STREAM
        type == 2 ? SOCKET_UDP :  // SOCK_DGRAM
        SOCKET_RAW
    );

    return sock_fd;
}

int sys_connect(int sockfd, uint32_t ip, uint16_t port) {
    // Connect to a network endpoint
    return net_socket_connect(sockfd, ip, port);
}

ssize_t sys_send(int sockfd, const void* buf, size_t len, int flags) {
    // Send data over socket
    return net_socket_send(sockfd, buf, len);
}

ssize_t sys_recv(int sockfd, void* buf, size_t len, int flags) {
    uint16_t recv_len = len;
    int result = net_socket_receive(sockfd, buf, &recv_len);
    return result == 0 ? recv_len : -1;
}

// Central syscall handler
long syscall_handler(long syscall_num,
                     uint64_t arg1,
                     uint64_t arg2,
                     uint64_t arg3,
                     uint64_t arg4,
                     uint64_t arg5,
                     uint64_t arg6) {
    switch (syscall_num) {
        // File operations
        case SYS_read:
            return sys_read((int)arg1, (void*)arg2, (size_t)arg3);
        case SYS_write:
            return sys_write((int)arg1, (const void*)arg2, (size_t)arg3);
        case SYS_open:
            return sys_open((const char*)arg1, (int)arg2, (uint16_t)arg3);
        case SYS_close:
            return sys_close((int)arg1);
        case SYS_lseek:
            return sys_lseek((int)arg1, (off_t)arg2, (int)arg3);
        case SYS_stat:
            return sys_stat((const char*)arg1, (struct stat*)arg2);
        case SYS_fstat:
            return sys_fstat((int)arg1, (struct stat*)arg2);

        // Process management
        case SYS_fork:
            return sys_fork();
        case SYS_exec:
            return sys_exec((const char*)arg1, (char* const*)arg2, (char* const*)arg3);
        case SYS_exit:
            sys_exit((int)arg1);
            return 0;
        case SYS_wait:
            return sys_wait((int*)arg1);
        case SYS_kill:
            return sys_kill((int)arg1, (int)arg2);

        // Network operations
        case SYS_socket:
            return sys_socket((int)arg1, (int)arg2, (int)arg3);
        case SYS_connect:
            return sys_connect((int)arg1, (uint32_t)arg2, (uint16_t)arg3);
        case SYS_send:
            return sys_send((int)arg1, (const void*)arg2, (size_t)arg3, (int)arg4);
        case SYS_recv:
            return sys_recv((int)arg1, (void*)arg2, (size_t)arg3, (int)arg4);

        default:
            // Unsupported syscall
            return -1;
    }
}