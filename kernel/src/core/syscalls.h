#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>
#include <stddef.h>

// Define basic types
typedef intptr_t ssize_t;
typedef int64_t off_t;

// Maximum values
#define MAX_FDS 256
#define MAX_PROCESSES 256

// Linux syscall numbers
#define SYS_read        0
#define SYS_write       1
#define SYS_open        2
#define SYS_close       3
#define SYS_stat        4
#define SYS_fstat       5
#define SYS_lseek       8
#define SYS_mmap        9
#define SYS_mprotect    10
#define SYS_munmap      11
#define SYS_brk         12
#define SYS_ioctl       16
#define SYS_pipe        22
#define SYS_select      23
#define SYS_socket      41
#define SYS_connect     42
#define SYS_accept      43
#define SYS_sendto      44
#define SYS_recvfrom    45
#define SYS_send        46
#define SYS_recv        47
#define SYS_shutdown    48
#define SYS_bind        49
#define SYS_listen      50
#define SYS_fork        57
#define SYS_execve      59
#define SYS_exit        60
#define SYS_kill        62
#define SYS_fcntl       72
#define SYS_getpid      39
#define SYS_getppid     110

// File open flags
#define O_RDONLY     0x0000
#define O_WRONLY     0x0001
#define O_RDWR       0x0002
#define O_CREAT      0x0040
#define O_TRUNC      0x0200
#define O_APPEND     0x0400
#define O_DIRECTORY  0x10000

// Mode flags
#define S_IRWXU    0700
#define S_IRUSR    0400
#define S_IWUSR    0200
#define S_IXUSR    0100

// Error numbers
#define EPERM        1
#define ENOENT       2
#define ESRCH        3
#define EINTR        4
#define EIO          5
#define ENXIO        6
#define E2BIG        7
#define ENOEXEC      8
#define EBADF        9
#define ECHILD       10
#define EAGAIN       11
#define ENOMEM       12
#define EACCES       13
#define EFAULT       14
#define ENOTBLK      15
#define EBUSY        16
#define EEXIST       17
#define EXDEV        18
#define ENODEV       19
#define ENOTDIR      20
#define EISDIR       21
#define EINVAL       22
#define ENFILE       23
#define EMFILE       24
#define ENOTTY       25
#define ETXTBSY      26
#define EFBIG        27
#define ENOSPC       28
#define ESPIPE       29
#define EROFS        30
#define EMLINK       31
#define EPIPE        32
#define ENOSYS       38

// Memory protection flags
#define PROT_NONE     0x0
#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x4

// Memory mapping flags
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

// Socket types
#define SOCK_STREAM    1
#define SOCK_DGRAM     2
#define SOCK_RAW       3

// Socket families
#define AF_UNSPEC      0
#define AF_INET        2

// Standard file descriptors
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

// Network address structure
struct sockaddr {
    uint16_t sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    char sin_zero[8];
};

// Syscall handler prototype
long syscall_handler(long syscall_num,
                    uint64_t arg1,
                    uint64_t arg2,
                    uint64_t arg3,
                    uint64_t arg4,
                    uint64_t arg5,
                    uint64_t arg6);

// File operations
int sys_open(const char* pathname, int flags, uint16_t mode);
ssize_t sys_read(int fd, void* buf, size_t count);
ssize_t sys_write(int fd, const void* buf, size_t count);
int sys_close(int fd);
off_t sys_lseek(int fd, off_t offset, int whence);

// Process management
int sys_fork(void);
int sys_execve(const char* pathname, char* const argv[], char* const envp[]);
void sys_exit(int status);
int sys_getpid(void);
int sys_getppid(void);

// Memory management
void* sys_brk(void* addr);
void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);

// Network operations
int sys_socket(int domain, int type, int protocol);
int sys_connect(int sockfd, const struct sockaddr* addr, uint32_t addrlen);
ssize_t sys_send(int sockfd, const void* buf, size_t len, int flags);
ssize_t sys_recv(int sockfd, void* buf, size_t len, int flags);

#endif // SYSCALLS_H