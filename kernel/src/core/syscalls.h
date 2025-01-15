#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>
#include <stddef.h>

// Define ssize_t as a signed version of size_t
typedef intptr_t ssize_t;

// Define off_t as a 64-bit signed integer for file offsets
typedef int64_t off_t;

// Syscall numbers for file operations
#define SYS_read                0
#define SYS_write               1
#define SYS_open                2
#define SYS_close               3
#define SYS_stat                4
#define SYS_fstat               5
#define SYS_lseek               8

// Process management syscall numbers
#define SYS_fork                57
#define SYS_exec                59
#define SYS_exit                60
#define SYS_wait                61
#define SYS_kill                62

// Network syscalls
#define SYS_socket              41
#define SYS_connect             42
#define SYS_send                44
#define SYS_recv                45

// Signal definitions
#define SIGHUP                  1
#define SIGINT                  2
#define SIGQUIT                 3
#define SIGILL                  4
#define SIGTRAP                 5
#define SIGABRT                 6
#define SIGBUS                  7
#define SIGFPE                  8
#define SIGKILL                 9
#define SIGUSR1                 10
#define SIGSEGV                 11
#define SIGUSR2                 12
#define SIGPIPE                 13
#define SIGALRM                 14
#define SIGTERM                 15
#define SIGSTKFLT               16
#define SIGCHLD                 17
#define SIGCONT                 18
#define SIGSTOP                 19
#define SIGTSTP                 20
#define SIGTTIN                 21
#define SIGTTOU                 22
#define SIGURG                  23
#define SIGXCPU                 24
#define SIGXFSZ                 25
#define SIGVTALRM               26
#define SIGPROF                 27
#define SIGWINCH                28
#define SIGIO                   29
#define SIGPWR                  30
#define SIGSYS                  31

// Std I/O file descriptors
#define STDIN_FILENO            0
#define STDOUT_FILENO           1
#define STDERR_FILENO           2

// File-related constants
#define O_RDONLY                0x00
#define O_WRONLY                0x01
#define O_RDWR                  0x02
#define O_CREAT                 0x40
#define O_TRUNC                 0x200
#define O_APPEND                0x400
#define O_DIRECTORY             0x10000

// File mode flags
#define S_IRWXU                 0700    // User read/write/execute
#define S_IRUSR                 0400    // User read
#define S_IWUSR                 0200    // User write
#define S_IXUSR                 0100    // User execute

// Seek constants
#define SEEK_SET                0   // Beginning of file
#define SEEK_CUR                1   // Current position
#define SEEK_END                2   // End of file

// File stat structure (matches ext2 inode structure)
struct stat {
    uint16_t st_mode;       // File mode
    uint32_t st_size;       // File size
    uint32_t st_atime;      // Last access time
    uint32_t st_mtime;      // Last modification time
    uint32_t st_ctime;      // Last status change time
};

// Maximum process-related constants
#define MAX_ARGS                64
#define MAX_ENV                 64
#define MAX_ARG_LENGTH          256
#define MAX_PATH_LENGTH         256

// Syscall function prototypes for file operations
int sys_open(const char* pathname, int flags, uint16_t mode);
ssize_t sys_read(int fd, void* buf, size_t count);
ssize_t sys_write(int fd, const void* buf, size_t count);
int sys_close(int fd);
off_t sys_lseek(int fd, off_t offset, int whence);
int sys_stat(const char* pathname, struct stat* statbuf);
int sys_fstat(int fd, struct stat* statbuf);

// Syscall function prototypes for process management
int sys_fork(void);
int sys_exec(const char* path, char* const argv[], char* const envp[]);
void sys_exit(int status);
int sys_wait(int* status);
int sys_kill(int pid, int signal);

// Network syscall prototypes
int sys_socket(int domain, int type, int protocol);
int sys_connect(int sockfd, uint32_t ip, uint16_t port);
ssize_t sys_send(int sockfd, const void* buf, size_t len, int flags);
ssize_t sys_recv(int sockfd, void* buf, size_t len, int flags);

// Main syscall handler prototype
long syscall_handler(long syscall_num,
                     uint64_t arg1,
                     uint64_t arg2,
                     uint64_t arg3,
                     uint64_t arg4,
                     uint64_t arg5,
                     uint64_t arg6);

#endif // SYSCALLS_H