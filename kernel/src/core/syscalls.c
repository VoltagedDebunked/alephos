#include <core/syscalls.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <fs/ext2.h>
#include <core/process.h>
#include <core/elf.h>
#include <core/acpi.h>
#include <core/drivers/pic.h>
#include <core/drivers/lapic.h>
#include <core/drivers/serial/serial.h>
#include <core/drivers/net/netdev.h>
#include <net/net.h>
#include <graphics/display.h>
#include <limine.h>
#include <utils/mem.h>
#include <utils/str.h>

extern volatile struct limine_framebuffer_request framebuffer_request;

// Expanded file descriptor tracking
#define MAX_FDS 256
#define MAX_PROCESSES 256

// File descriptor types
#define FD_TYPE_FILE    0
#define FD_TYPE_SOCKET  1
#define FD_TYPE_PIPE    2
#define FD_TYPE_DIR     3

// File descriptor management structure
typedef struct {
    uint32_t inode;
    uint32_t offset;
    uint32_t flags;
    uint16_t refcount;
    void* private_data;
    int type;
} file_descriptor_t;

// Pipe structure for pipe syscalls
typedef struct {
    uint8_t* buffer;
    size_t buffer_size;
    size_t read_pos;
    size_t write_pos;
    size_t data_size;
} pipe_t;

// Dirent structure for getdents syscall
struct linux_dirent {
    unsigned long  d_ino;     // Inode number
    unsigned long  d_off;     // Offset to next linux_dirent
    unsigned short d_reclen;  // Length of this linux_dirent
    char           d_name[];  // Filename (null-terminated)
} __attribute__((packed));

static file_descriptor_t* fd_table[MAX_FDS];
static void* program_break = NULL;
static void* next_mmap_addr = (void*)0x600000000000ULL;

// Existing method declarations
extern process_t* process_list;

// File descriptor management
static int alloc_fd(void) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (!fd_table[i]) {
            fd_table[i] = malloc(sizeof(file_descriptor_t));
            if (fd_table[i]) {
                memset(fd_table[i], 0, sizeof(file_descriptor_t));
                fd_table[i]->refcount = 1;
                return i;
            }
            return -ENOMEM;
        }
    }
    return -EMFILE;
}

static void free_fd(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd]) return;

    fd_table[fd]->refcount--;
    if (fd_table[fd]->refcount == 0) {
        // Close socket or other resources
        switch (fd_table[fd]->type) {
            case FD_TYPE_SOCKET:
                if (fd_table[fd]->private_data) {
                    net_socket_close(((net_socket*)fd_table[fd]->private_data)->fd);
                    free(fd_table[fd]->private_data);
                }
                break;
            case FD_TYPE_PIPE:
                {
                    pipe_t* pipe_data = (pipe_t*)fd_table[fd]->private_data;
                    if (pipe_data) {
                        free(pipe_data->buffer);
                        free(pipe_data);
                    }
                }
                break;
        }
        free(fd_table[fd]);
        fd_table[fd] = NULL;
    }
}

// Expanded system call implementations
int sys_open(const char* pathname, int flags, mode_t mode) {
    // Use EXT2 to find and open the file
    uint32_t inode = ext2_find_file(EXT2_ROOT_INO, pathname);

    // If file not found and O_CREAT is set, create it
    if (!inode && (flags & O_CREAT)) {
        inode = ext2_create_file(EXT2_ROOT_INO, pathname,
            mode & 0777 | S_IFREG);
        if (!inode) return -EACCES;
    } else if (!inode) {
        return -ENOENT;
    }

    int fd = alloc_fd();
    if (fd < 0) return fd;

    file_descriptor_t* file_desc = fd_table[fd];
    file_desc->inode = inode;
    file_desc->offset = 0;
    file_desc->flags = flags;
    file_desc->type = FD_TYPE_FILE;

    // Additional flag handling
    if (flags & O_TRUNC) {
        // Truncate file implementation would go here
        // TODO: Implement file truncation
    }

    return fd;
}

int sys_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd]) return -EBADF;
    free_fd(fd);
    return 0;
}

ssize_t sys_read(int fd, void* buf, size_t count) {
    if (!buf) return -EINVAL;
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd]) return -EBADF;

    // Handle stdin from serial
    if (fd == STDIN_FILENO) {
        char c = serial_read_char(COM1);
        if (count > 0) {
            ((char*)buf)[0] = c;
            return 1;
        }
        return 0;
    }

    // Handle pipes
    if (fd_table[fd]->type == FD_TYPE_PIPE) {
        pipe_t* pipe_data = (pipe_t*)fd_table[fd]->private_data;
        size_t to_read = (count < pipe_data->data_size) ? count : pipe_data->data_size;

        for (size_t i = 0; i < to_read; i++) {
            ((uint8_t*)buf)[i] = pipe_data->buffer[pipe_data->read_pos];
            pipe_data->read_pos = (pipe_data->read_pos + 1) % pipe_data->buffer_size;
        }

        pipe_data->data_size -= to_read;
        return to_read;
    }

    // Handle network sockets
    if (fd_table[fd]->type == FD_TYPE_SOCKET) {
        net_socket* sock = fd_table[fd]->private_data;
        uint16_t received = count;
        int result = net_socket_receive(sock->fd, buf, &received);
        return result == 0 ? received : -EIO;
    }

    // Handle regular files using EXT2
    if (!ext2_read_file(fd_table[fd]->inode, buf,
        fd_table[fd]->offset, count)) {
        return -EIO;
    }

    fd_table[fd]->offset += count;
    return count;
}

ssize_t sys_write(int fd, const void* buf, size_t count) {
    if (!buf) return -EINVAL;
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd]) return -EBADF;

    // Handle stdout/stderr to framebuffer
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        struct limine_framebuffer* fb = framebuffer_request.response->framebuffers[0];
        if (!fb) return -EIO;

        static uint32_t x = 0, y = 0;
        const char* cbuf = buf;

        for (size_t i = 0; i < count; i++) {
            if (cbuf[i] == '\n' || x >= fb->width - 8) {
                x = 0;
                y += 16;
                if (y >= fb->height - 16) {
                    memmove((void*)fb->address,
                           (void*)(fb->address + fb->pitch * 16),
                           fb->pitch * (fb->height - 16));
                    y = fb->height - 16;
                }
                if (cbuf[i] == '\n') continue;
            }
            draw_char(fb, cbuf[i], x, y, 0xFFFFFF);
            x += 8;
        }
        return count;
    }

    // Handle pipes
    if (fd_table[fd]->type == FD_TYPE_PIPE) {
        pipe_t* pipe_data = (pipe_t*)fd_table[fd]->private_data;
        size_t available = pipe_data->buffer_size - pipe_data->data_size;
        size_t to_write = (count < available) ? count : available;

        for (size_t i = 0; i < to_write; i++) {
            pipe_data->buffer[pipe_data->write_pos] = ((const uint8_t*)buf)[i];
            pipe_data->write_pos = (pipe_data->write_pos + 1) % pipe_data->buffer_size;
        }

        pipe_data->data_size += to_write;
        return to_write;
    }

    // Handle network sockets
    if (fd_table[fd]->type == FD_TYPE_SOCKET) {
        net_socket* sock = fd_table[fd]->private_data;
        return net_socket_send(sock->fd, buf, count);
    }

    // Handle regular file writes through EXT2
    if (!ext2_write_file(fd_table[fd]->inode, buf,
        fd_table[fd]->offset, count)) {
        return -EIO;
    }

    fd_table[fd]->offset += count;
    return count;
}

off_t sys_lseek(int fd, off_t offset, int whence) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd]) return -EBADF;

    // Cannot seek on pipes or sockets
    if (fd_table[fd]->type != FD_TYPE_FILE) return -ESPIPE;

    struct ext2_inode* inode = ext2_get_inode(fd_table[fd]->inode);
    if (!inode) return -EBADF;

    off_t new_offset;
    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = fd_table[fd]->offset + offset;
            break;
        case SEEK_END:
            new_offset = inode->i_size + offset;
            break;
        default:
            free(inode);
            return -EINVAL;
    }

    // Validate new offset
    if (new_offset < 0 || new_offset > inode->i_size) {
        free(inode);
        return -EINVAL;
    }

    fd_table[fd]->offset = new_offset;
    free(inode);
    return new_offset;
}

int sys_pipe(int pipefd[2]) {
    // Create a pipe
    pipe_t* pipe_buffer = malloc(sizeof(pipe_t));
    if (!pipe_buffer) return -ENOMEM;

    pipe_buffer->buffer_size = 4096;  // 4KB default
    pipe_buffer->buffer = malloc(pipe_buffer->buffer_size);
    if (!pipe_buffer->buffer) {
        free(pipe_buffer);
        return -ENOMEM;
    }

    pipe_buffer->read_pos = 0;
    pipe_buffer->write_pos = 0;
    pipe_buffer->data_size = 0;

    // Allocate file descriptors for read and write ends
    int read_fd = alloc_fd();
    if (read_fd < 0) {
        free(pipe_buffer->buffer);
        free(pipe_buffer);
        return read_fd;
    }

    int write_fd = alloc_fd();
    if (write_fd < 0) {
        free_fd(read_fd);
        free(pipe_buffer->buffer);
        free(pipe_buffer);
        return write_fd;
    }

    // Set up file descriptors
    fd_table[read_fd]->type = FD_TYPE_PIPE;
    fd_table[read_fd]->private_data = pipe_buffer;
    fd_table[read_fd]->flags = O_RDONLY;

    fd_table[write_fd]->type = FD_TYPE_PIPE;
    fd_table[write_fd]->private_data = pipe_buffer;
    fd_table[write_fd]->flags = O_WRONLY;

    pipefd[0] = read_fd;
    pipefd[1] = write_fd;

    return 0;
}

int sys_pipe2(int pipefd[2], int flags) {
    int ret = sys_pipe(pipefd);
    if (ret < 0) return ret;

    if (flags & O_NONBLOCK) {
        // Set non-blocking mode on both fds
        fd_table[pipefd[0]]->flags |= O_NONBLOCK;
        fd_table[pipefd[1]]->flags |= O_NONBLOCK;
    }

    if (flags & O_CLOEXEC) {
        // Set close-on-exec flag on both fds
        fd_table[pipefd[0]]->flags |= O_CLOEXEC;
        fd_table[pipefd[1]]->flags |= O_CLOEXEC;
    }

    return 0;
}

int sys_dup(int oldfd) {
    if (oldfd < 0 || oldfd >= MAX_FDS || !fd_table[oldfd]) return -EBADF;

    int newfd = alloc_fd();
    if (newfd < 0) return newfd;

    // Copy file descriptor
    memcpy(fd_table[newfd], fd_table[oldfd], sizeof(file_descriptor_t));
    fd_table[oldfd]->refcount++;

    return newfd;
}

int sys_dup2(int oldfd, int newfd) {
    // Close newfd if it's open
    if (newfd >= 0 && newfd < MAX_FDS) {
        if (fd_table[newfd]) {
            sys_close(newfd);
        }
    }

    // Validate oldfd
    if (oldfd < 0 || oldfd >= MAX_FDS || !fd_table[oldfd]) return -EBADF;

    if (newfd < 0 || newfd >= MAX_FDS) return -EBADF;

    // Copy file descriptor
    fd_table[newfd] = malloc(sizeof(file_descriptor_t));
    if (!fd_table[newfd]) return -ENOMEM;

    memcpy(fd_table[newfd], fd_table[oldfd], sizeof(file_descriptor_t));
    fd_table[oldfd]->refcount++;

    return newfd;
}

ssize_t sys_getdents(unsigned int fd, struct linux_dirent* dirp, unsigned int count) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd]) return -EBADF;
    if (fd_table[fd]->type != FD_TYPE_DIR) return -ENOTDIR;

    // TODO: Implement directory entry reading
    return -ENOSYS;
}

void* sys_brk(void* addr) {
    // Implement simple brk memory management
    if (!program_break) {
        program_break = (void*)PAGE_ALIGN((uint64_t)addr);
    }

    if (!addr) {
        return program_break;
    }

    void* new_break = (void*)PAGE_ALIGN((uint64_t)addr);

    if (new_break > program_break) {
        // Allocate new pages
        uint64_t pages_needed = ((uint64_t)new_break - (uint64_t)program_break) / PAGE_SIZE;

        for (uint64_t i = 0; i < pages_needed; i++) {
            void* phys = pmm_alloc_page();
            if (!phys || !vmm_map_page((uint64_t)(program_break + i * PAGE_SIZE),
                                       (uint64_t)phys,
                                       PTE_PRESENT | PTE_WRITABLE | PTE_USER)) {
                // Cleanup allocated pages on failure
                for (uint64_t j = 0; j < i; j++) {
                    vmm_unmap_page((uint64_t)(program_break + j * PAGE_SIZE));
                }
                return (void*)-1;
            }
        }

        program_break = new_break;
    } else if (new_break < program_break) {
        // Free pages
        uint64_t pages_to_free = ((uint64_t)program_break - (uint64_t)new_break) / PAGE_SIZE;

        for (uint64_t i = 0; i < pages_to_free; i++) {
            void* page_addr = program_break - (i + 1) * PAGE_SIZE;
            uint64_t phys_addr = vmm_get_phys_addr((uint64_t)page_addr);

            if (phys_addr) {
                vmm_unmap_page((uint64_t)page_addr);
                pmm_free_page((void*)phys_addr);
            }
        }

        program_break = new_break;
    }

    return program_break;
}

void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    // Validate inputs
    if (length == 0) return (void*)-EINVAL;

    // Round length to page size
    length = PAGE_ALIGN(length);

    // Determine address
    if (!addr) {
        addr = next_mmap_addr;
        next_mmap_addr += length;
    }

    // Align address to page boundary
    addr = (void*)(PAGE_ALIGN((uint64_t)addr));

    // Determine page protection flags
    uint64_t page_flags = PTE_PRESENT | PTE_USER;
    if (prot & PROT_WRITE) page_flags |= PTE_WRITABLE;
    if (!(prot & PROT_EXEC)) page_flags |= PTE_NX;

    // Anonymous mapping
    if (flags & MAP_ANONYMOUS) {
        for (size_t i = 0; i < length; i += PAGE_SIZE) {
            void* phys = pmm_alloc_page();
            if (!phys || !vmm_map_page((uint64_t)addr + i, (uint64_t)phys, page_flags)) {
                // Rollback mapping on failure
                for (size_t j = 0; j < i; j += PAGE_SIZE) {
                    vmm_unmap_page((uint64_t)addr + j);
                }
                return (void*)-ENOMEM;
            }
            // Zero out the page
            memset((void*)((uint64_t)addr + i), 0, PAGE_SIZE);
        }
    }
    // File-backed mapping
    else if (fd >= 0 && fd < MAX_FDS && fd_table[fd]) {
        struct ext2_inode* inode = ext2_get_inode(fd_table[fd]->inode);
        if (!inode) return (void*)-EBADF;

        for (size_t i = 0; i < length; i += PAGE_SIZE) {
            void* phys = pmm_alloc_page();
            if (!phys || !vmm_map_page((uint64_t)addr + i, (uint64_t)phys, page_flags)) {
                // Rollback mapping on failure
                for (size_t j = 0; j < i; j += PAGE_SIZE) {
                    vmm_unmap_page((uint64_t)addr + j);
                }
                free(inode);
                return (void*)-ENOMEM;
            }

            // Read file data into the mapped page
            if (!ext2_read_file(fd_table[fd]->inode,
                               (void*)((uint64_t)addr + i),
                               offset + i,
                               PAGE_SIZE)) {
                // Rollback mapping on read failure
                for (size_t j = 0; j < i; j += PAGE_SIZE) {
                    vmm_unmap_page((uint64_t)addr + j);
                }
                free(inode);
                return (void*)-EIO;
            }
        }
        free(inode);
    }

    return addr;
}

int sys_munmap(void* addr, size_t length) {
    // Validate inputs
    if (!addr || length == 0) return -EINVAL;

    // Round length to page size
    length = PAGE_ALIGN(length);

    // Unmap pages
    for (size_t i = 0; i < length; i += PAGE_SIZE) {
        uint64_t phys_addr = vmm_get_phys_addr((uint64_t)addr + i);
        if (phys_addr) {
            vmm_unmap_page((uint64_t)addr + i);
            pmm_free_page((void*)phys_addr);
        }
    }

    return 0;
}

int sys_socket(int domain, int type, int protocol) {
    int fd = alloc_fd();
    if (fd < 0) return fd;

    // Convert socket type
    net_socket_type sock_type = SOCKET_TCP;
    switch (type) {
        case SOCK_STREAM: sock_type = SOCKET_TCP; break;
        case SOCK_DGRAM:  sock_type = SOCKET_UDP; break;
        case SOCK_RAW:    sock_type = SOCKET_RAW; break;
        default:
            free_fd(fd);
            return -EINVAL;
    }

    // Change this to handle potential integer return
    int sock_fd = net_socket_create(sock_type);
    if (sock_fd < 0) {
        free_fd(fd);
        return sock_fd;
    }

    // Create a net_socket structure manually
    net_socket* sock = malloc(sizeof(net_socket));
    if (!sock) {
        net_socket_close(sock_fd);
        free_fd(fd);
        return -ENOMEM;
    }

    // Initialize the socket structure
    sock->fd = sock_fd;
    sock->type = sock_type;
    // Initialize other fields as needed

    file_descriptor_t* file_desc = fd_table[fd];
    file_desc->type = FD_TYPE_SOCKET;
    file_desc->private_data = sock;
    file_desc->flags = O_RDWR;

    return fd;
}

int sys_connect(int sockfd, const struct sockaddr* addr, uint32_t addrlen) {
    if (!addr || addrlen < sizeof(struct sockaddr_in)) return -EINVAL;
    if (sockfd < 0 || sockfd >= MAX_FDS || !fd_table[sockfd]) return -EBADF;

    net_socket* sock = fd_table[sockfd]->private_data;
    if (!sock || fd_table[sockfd]->type != FD_TYPE_SOCKET) return -EBADF;

    const struct sockaddr_in* addr_in = (const struct sockaddr_in*)addr;
    return net_socket_connect(sock->fd, addr_in->sin_addr.s_addr, addr_in->sin_port);
}

ssize_t sys_send(int sockfd, const void* buf, size_t len, int flags) {
    if (!buf) return -EINVAL;
    if (sockfd < 0 || sockfd >= MAX_FDS || !fd_table[sockfd]) return -EBADF;

    net_socket* sock = fd_table[sockfd]->private_data;
    if (!sock || fd_table[sockfd]->type != FD_TYPE_SOCKET) return -EBADF;

    return net_socket_send(sock->fd, buf, len);
}

ssize_t sys_recv(int sockfd, void* buf, size_t len, int flags) {
    if (!buf) return -EINVAL;
    if (sockfd < 0 || sockfd >= MAX_FDS || !fd_table[sockfd]) return -EBADF;

    net_socket* sock = fd_table[sockfd]->private_data;
    if (!sock || fd_table[sockfd]->type != FD_TYPE_SOCKET) return -EBADF;

    uint16_t received = len;
    int result = net_socket_receive(sock->fd, buf, &received);
    return result == 0 ? received : -EIO;
}

// Process management syscalls
int sys_fork(void) {
    process_t* current = get_current_process();
    if (!current) return -EAGAIN;

    process_t* child = process_create(
        (void*)current->cpu_state->rip,
        current->priority,
        current->name
    );

    if (!child) return -EAGAIN;

    memcpy(child->page_directory, current->page_directory, PAGE_SIZE);
    memcpy(child->cpu_state, current->cpu_state, sizeof(cpu_state_t));

    for (int i = 0; i < MAX_FDS; i++) {
        if (fd_table[i]) {
            fd_table[i]->refcount++;
        }
    }

    child->cpu_state->rax = 0;
    scheduler_add(child);

    return child->pid;
}

int sys_execve(const char* pathname, char* const argv[], char* const envp[]) {
    if (!pathname) return -EINVAL;

    uint32_t inode = ext2_find_file(EXT2_ROOT_INO, pathname);
    if (!inode) return -ENOENT;

    struct ext2_inode* file_inode = ext2_get_inode(inode);
    if (!file_inode) return -EIO;

    void* program_data = malloc(file_inode->i_size);
    if (!program_data) {
        free(file_inode);
        return -ENOMEM;
    }

    if (!ext2_read_file(inode, program_data, 0, file_inode->i_size)) {
        free(program_data);
        free(file_inode);
        return -EIO;
    }

    uint64_t entry_point;
    int result = elf_load_executable(program_data, file_inode->i_size, &entry_point);

    free(program_data);
    free(file_inode);

    if (result != 0) return -ENOEXEC;

    process_t* current = get_current_process();
    if (!current) return -ESRCH;

    current->cpu_state->rip = entry_point;
    current->cpu_state->rdi = (uint64_t)argv;
    current->cpu_state->rsi = (uint64_t)envp;

    return 0;
}

void sys_exit(int status) {
    process_t* current = get_current_process();
    if (!current) return;

    // Close all file descriptors
    for (int i = 0; i < MAX_FDS; i++) {
        free_fd(i);
    }

    current->state = PROCESS_STATE_TERMINATED;
    scheduler_remove(current);

    process_t* next = scheduler_next();
    if (next) {
        process_switch(next);
    }

    while (1) __asm__ volatile("hlt");
}

int sys_getpid(void) {
    process_t* current = get_current_process();
    return current ? current->pid : -ESRCH;
}

int sys_getppid(void) {
    process_t* current = get_current_process();
    if (!current) return -ESRCH;

    // TODO: Implement parent tracking in process structure
    // For now, return 0
    return 0;
}

// Final syscall handler
long syscall_handler(long syscall_num,
                    uint64_t arg1,
                    uint64_t arg2,
                    uint64_t arg3,
                    uint64_t arg4,
                    uint64_t arg5,
                    uint64_t arg6) {
    switch (syscall_num) {
        // File operations
        case __NR_read:
            return sys_read((int)arg1, (void*)arg2, (size_t)arg3);
        case __NR_write:
            return sys_write((int)arg1, (const void*)arg2, (size_t)arg3);
        case __NR_open:
            return sys_open((const char*)arg1, (int)arg2, (mode_t)arg3);
        case __NR_close:
            return sys_close((int)arg1);
        case __NR_lseek:
            return sys_lseek((int)arg1, (off_t)arg2, (int)arg3);
        case __NR_pipe:
            return sys_pipe((int*)arg1);
        case __NR_dup:
            return sys_dup((int)arg1);
        case __NR_dup2:
            return sys_dup2((int)arg1, (int)arg2);
        case __NR_getdents:
            return sys_getdents((unsigned int)arg1,
                                (struct linux_dirent*)arg2,
                                (unsigned int)arg3);

        // Memory management
        case __NR_brk:
            return (long)sys_brk((void*)arg1);
        case __NR_mmap:
            return (long)sys_mmap((void*)arg1, (size_t)arg2, (int)arg3,
                                  (int)arg4, (int)arg5, (off_t)arg6);
        case __NR_munmap:
            return sys_munmap((void*)arg1, (size_t)arg2);

        // Network operations
        case __NR_socket:
            return sys_socket((int)arg1, (int)arg2, (int)arg3);
        case __NR_connect:
            return sys_connect((int)arg1, (const struct sockaddr*)arg2, (uint32_t)arg3);
        case __NR_send:
            return sys_send((int)arg1, (const void*)arg2, (size_t)arg3, (int)arg4);
        case __NR_recv:
            return sys_recv((int)arg1, (void*)arg2, (size_t)arg3, (int)arg4);

        // Process management
        case __NR_fork:
            return sys_fork();
        case __NR_execve:
            return sys_execve((const char*)arg1, (char* const*)arg2, (char* const*)arg3);
        case __NR_exit:
            sys_exit((int)arg1);
            return 0;
        case __NR_getpid:
            return sys_getpid();
        case __NR_getppid:
            return sys_getppid();
        case __NR_pipe2:
            return sys_pipe2((int*)arg1, (int)arg2);

        // Default case
        default:
            return -ENOSYS;
    }
}