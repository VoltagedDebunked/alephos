#include <core/syscalls.h>
#include <fs/ext2.h>
#include <graphics/display.h>
#include <limine.h>
#include <mm/heap.h>
#include <net/net.h>
#include <net/http/http.h>
#include <utils/str.h>
#include <core/drivers/storage/nvme.h>

extern struct limine_framebuffer* global_framebuffer;

// File descriptor management
#define MAX_FDS 256
static struct {
    bool used;
    uint32_t inode;     // EXT2 inode number
    uint32_t pos;       // Current file position
    bool read_allowed;
    bool write_allowed;
} fd_table[MAX_FDS];

// Allocate a new file descriptor
static int allocate_fd() {
    for (int i = 3; i < MAX_FDS; i++) {  // Start from 3 to reserve stdin/stdout/stderr
        if (!fd_table[i].used) {
            fd_table[i].used = true;
            return i;
        }
    }
    return -1;  // No free file descriptors
}

// Free a file descriptor
static void free_fd(int fd) {
    if (fd >= 0 && fd < MAX_FDS) {
        fd_table[fd].used = false;
    }
}

// Open a file via EXT2
int sys_open(const char* pathname, int flags, uint16_t mode) {
    // Root directory inode
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

    // Allocate file descriptor
    int fd = allocate_fd();
    if (fd < 0) {
        return -1;  // No free file descriptors
    }

    // Setup file descriptor
    fd_table[fd].inode = file_inode;
    fd_table[fd].pos = 0;
    fd_table[fd].read_allowed = (flags & (O_RDONLY | O_RDWR)) != 0;
    fd_table[fd].write_allowed = (flags & (O_WRONLY | O_RDWR)) != 0;

    return fd;
}

// Read from a file
ssize_t sys_read(int fd, void* buf, size_t count) {
    // Validate file descriptor
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used || !fd_table[fd].read_allowed) {
        return -1;
    }

    // Get inode
    struct ext2_inode* inode = ext2_get_inode(fd_table[fd].inode);
    if (!inode) {
        return -1;
    }

    // Check if we're at or past end of file
    if (fd_table[fd].pos >= inode->i_size) {
        free(inode);
        return 0;
    }

    // Adjust count if it would read past end of file
    if (fd_table[fd].pos + count > inode->i_size) {
        count = inode->i_size - fd_table[fd].pos;
    }

    // Read data
    if (!ext2_read_file(fd_table[fd].inode, buf, fd_table[fd].pos, count)) {
        free(inode);
        return -1;
    }

    // Update file position
    fd_table[fd].pos += count;

    free(inode);
    return count;
}

// Write to a file or framebuffer
ssize_t sys_write(int fd, const void* buf, size_t count) {
    // Stdout and stderr go to framebuffer
    if (fd == 1 || fd == 2) {
        static uint32_t current_y = 0;
        draw_string(global_framebuffer, (const char*)buf, 0, current_y, 0xFFFFFF);
        current_y += 16;  // Move to next line
        return count;
    }

    // Validate file descriptor
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used || !fd_table[fd].write_allowed) {
        return -1;
    }

    // Write to file using existing ext2_write_file
    if (!ext2_write_file(fd_table[fd].inode, buf, fd_table[fd].pos, count)) {
        return -1;
    }

    // Update file position
    fd_table[fd].pos += count;

    return count;
}

// Close a file descriptor
int sys_close(int fd) {
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) {
        return -1;
    }

    free_fd(fd);
    return 0;
}

// Seek in a file
off_t sys_lseek(int fd, off_t offset, int whence) {
    // Validate file descriptor
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) {
        return -1;
    }

    // Get inode to check file size
    struct ext2_inode* inode = ext2_get_inode(fd_table[fd].inode);
    if (!inode) {
        return -1;
    }

    // Compute new position based on whence
    off_t new_pos;
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = fd_table[fd].pos + offset;
            break;
        case SEEK_END:
            new_pos = inode->i_size + offset;
            break;
        default:
            free(inode);
            return -1;
    }

    // Validate new position
    if (new_pos < 0 || new_pos > inode->i_size) {
        free(inode);
        return -1;
    }

    // Update position
    fd_table[fd].pos = new_pos;

    free(inode);
    return new_pos;
}

// Get file status
int sys_stat(const char* pathname, struct stat* statbuf) {
    // Find file in root directory
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

// Get file descriptor status
int sys_fstat(int fd, struct stat* statbuf) {
    // Validate file descriptor
    if (fd < 0 || fd >= MAX_FDS || !fd_table[fd].used) {
        return -1;
    }

    // Get inode information
    struct ext2_inode* inode = ext2_get_inode(fd_table[fd].inode);
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

// Network-related syscalls
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
        case SYS_write:
            return sys_write((int)arg1, (const void*)arg2, (size_t)arg3);
        case SYS_read:
            return sys_read((int)arg1, (void*)arg2, (size_t)arg3);
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

        // Network syscalls
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