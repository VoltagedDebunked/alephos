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

typedef struct {
    uint32_t inode;
    uint32_t offset;
    uint32_t flags;
    uint16_t refcount;
    void* private_data;
} file_descriptor_t;

static file_descriptor_t* fd_table[MAX_FDS];
static void* program_break = NULL;
static void* next_mmap_addr = (void*)0x600000000000ULL;

typedef struct process_info {
    int pid;
    int parent_pid;
    int exit_status;
    bool terminated;
    struct process_info* next;
} process_info_t;

extern process_t* process_list;

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
    if (fd >= 0 && fd < MAX_FDS && fd_table[fd]) {
        fd_table[fd]->refcount--;
        if (fd_table[fd]->refcount == 0) {
            if (fd_table[fd]->private_data) {
                net_socket_close(((net_socket*)fd_table[fd]->private_data)->fd);
                free(fd_table[fd]->private_data);
            }
            free(fd_table[fd]);
            fd_table[fd] = NULL;
        }
    }
}

int sys_socket(int domain, int type, int protocol) {
    int fd = alloc_fd();
    if (fd < 0) return fd;

    // Properly cast the return value
    net_socket* sock = (net_socket*)net_socket_create(
        type == SOCK_STREAM ? SOCKET_TCP :
        type == SOCK_DGRAM ? SOCKET_UDP :
        SOCKET_RAW
    );

    if (!sock) {
        free_fd(fd);
        return -ENOMEM;
    }

    fd_table[fd]->private_data = sock;
    fd_table[fd]->flags = O_RDWR;
    return fd;
}

int sys_connect(int sockfd, const struct sockaddr* addr, uint32_t addrlen) {
    if (!addr || addrlen < sizeof(struct sockaddr_in)) return -EINVAL;
    if (sockfd < 0 || sockfd >= MAX_FDS || !fd_table[sockfd]) return -EBADF;

    net_socket* sock = fd_table[sockfd]->private_data;
    if (!sock) return -EBADF;

    const struct sockaddr_in* addr_in = (const struct sockaddr_in*)addr;
    return net_socket_connect(sock->fd, addr_in->sin_addr, addr_in->sin_port);
}

ssize_t sys_send(int sockfd, const void* buf, size_t len, int flags) {
    if (!buf) return -EINVAL;
    if (sockfd < 0 || sockfd >= MAX_FDS || !fd_table[sockfd]) return -EBADF;

    net_socket* sock = fd_table[sockfd]->private_data;
    if (!sock) return -EBADF;

    return net_socket_send(sock->fd, buf, len);
}

ssize_t sys_recv(int sockfd, void* buf, size_t len, int flags) {
    if (!buf) return -EINVAL;
    if (sockfd < 0 || sockfd >= MAX_FDS || !fd_table[sockfd]) return -EBADF;

    net_socket* sock = fd_table[sockfd]->private_data;
    if (!sock) return -EBADF;

    uint16_t received = len;
    int result = net_socket_receive(sock->fd, buf, &received);
    return result == 0 ? received : -EIO;
}

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
        if (fd_table[i]) fd_table[i]->refcount++;
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
    current->cpu_state->rdi = argv ? 1 : 0;
    current->cpu_state->rsi = (uint64_t)argv;
    current->cpu_state->rdx = (uint64_t)envp;

    return 0;
}

void sys_exit(int status) {
    process_t* current = get_current_process();
    if (!current) return;

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

void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (length == 0) return (void*)-EINVAL;

    length = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (!addr) {
        addr = next_mmap_addr;
        next_mmap_addr += length;
    }

    addr = (void*)(((uint64_t)addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

    uint64_t page_flags = PTE_PRESENT | PTE_USER;
    if (prot & PROT_WRITE) page_flags |= PTE_WRITABLE;
    if (!(prot & PROT_EXEC)) page_flags |= PTE_NX;

    if (!(flags & MAP_ANONYMOUS) && fd >= 0) {
        if (fd >= MAX_FDS || !fd_table[fd]) return (void*)-EBADF;

        for (size_t i = 0; i < length; i += PAGE_SIZE) {
            void* phys = pmm_alloc_page();
            if (!phys || !vmm_map_page((uint64_t)addr + i, (uint64_t)phys, page_flags)) {
                goto cleanup;
            }

            if (!ext2_read_file(fd_table[fd]->inode,
                              (void*)((uint64_t)addr + i),
                              offset + i, PAGE_SIZE)) {
                goto cleanup;
            }
        }
    } else {
        for (size_t i = 0; i < length; i += PAGE_SIZE) {
            void* phys = pmm_alloc_page();
            if (!phys || !vmm_map_page((uint64_t)addr + i, (uint64_t)phys, page_flags)) {
                goto cleanup;
            }
            memset((void*)((uint64_t)addr + i), 0, PAGE_SIZE);
        }
    }

    return addr;

cleanup:
    for (size_t i = 0; i < length; i += PAGE_SIZE) {
        vmm_unmap_page((uint64_t)addr + i);
    }
    return (void*)-ENOMEM;
}

ssize_t sys_read(int fd, void* buf, size_t count) {
    if (!buf) return -EINVAL;
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd]) return -EBADF;

    if (fd == STDIN_FILENO) {
        char c = serial_read_char(COM1);
        if (count > 0) {
            ((char*)buf)[0] = c;
            return 1;
        }
        return 0;
    }

    if (fd_table[fd]->private_data) {
        net_socket* sock = fd_table[fd]->private_data;
        uint16_t received = count;
        int result = net_socket_receive(sock->fd, buf, &received);
        return result == 0 ? received : -EIO;
    }

    if (!ext2_read_file(fd_table[fd]->inode, buf, fd_table[fd]->offset, count)) {
        return -EIO;
    }

    fd_table[fd]->offset += count;
    return count;
}

ssize_t sys_write(int fd, const void* buf, size_t count) {
    if (!buf) return -EINVAL;
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd]) return -EBADF;

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

    if (fd_table[fd]->private_data) {
        net_socket* sock = fd_table[fd]->private_data;
        return net_socket_send(sock->fd, buf, count);
    }

    if (!ext2_write_file(fd_table[fd]->inode, buf, fd_table[fd]->offset, count)) {
        return -EIO;
    }

    fd_table[fd]->offset += count;
    return count;
}

long syscall_handler(long syscall_num,
                    uint64_t arg1,
                    uint64_t arg2,
                    uint64_t arg3,
                    uint64_t arg4,
                    uint64_t arg5,
                    uint64_t arg6) {
    switch (syscall_num) {
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
        case SYS_fork:
            return sys_fork();
        case SYS_exit:
            sys_exit((int)arg1);
            return 0;
        case SYS_execve:
            return sys_execve((const char*)arg1, (char* const*)arg2, (char* const*)arg3);
        case SYS_brk:
            return (long)sys_brk((void*)arg1);
        case SYS_socket:
            return sys_socket((int)arg1, (int)arg2, (int)arg3);
        case SYS_connect:
            return sys_connect((int)arg1, (const struct sockaddr*)arg2, (uint32_t)arg3);
        case SYS_send:
            return sys_send((int)arg1, (const void*)arg2, (size_t)arg3, (int)arg4);
        case SYS_recv:
            return sys_recv((int)arg1, (void*)arg2, (size_t)arg3, (int)arg4);
        case SYS_mmap:
            return (long)sys_mmap((void*)arg1, (size_t)arg2, (int)arg3, (int)arg4, (int)arg5, (off_t)arg6);
        default:
            return -ENOSYS;
    }
}