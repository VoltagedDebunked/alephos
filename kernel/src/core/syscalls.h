#ifndef SYSCALLS_H
#define SYSCALLS_H

#include <stdint.h>
#include <stddef.h>

// Basic type definitions to match Linux ABI
typedef long long __kernel_long_t;
typedef unsigned long long __kernel_ulong_t;
typedef __kernel_long_t __kernel_ssize_t;
typedef __kernel_ulong_t __kernel_size_t;
typedef long time_t;
typedef long off_t;
typedef long ssize_t;
typedef unsigned int mode_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef unsigned int pid_t;
typedef uint16_t sa_family_t;
typedef uint16_t in_port_t;

// Maximum values
#define PATH_MAX 4096
#define NAME_MAX 255
#define MAX_CANON 255
#define MAX_INPUT 255

// Linux x86_64 Syscall Numbers
#define __NR_read        0
#define __NR_write       1
#define __NR_open        2
#define __NR_close       3
#define __NR_stat        4
#define __NR_fstat       5
#define __NR_lstat       6
#define __NR_poll        7
#define __NR_lseek       8
#define __NR_mmap        9
#define __NR_mprotect    10
#define __NR_munmap      11
#define __NR_brk         12
#define __NR_sigaction   13
#define __NR_sigprocmask 14
#define __NR_sigreturn   15
#define __NR_ioctl       16
#define __NR_pread64     17
#define __NR_pwrite64    18
#define __NR_readv       19
#define __NR_writev      20
#define __NR_access      21
#define __NR_pipe        22
#define __NR_select      23
#define __NR_sched_yield 24
#define __NR_mremap      25
#define __NR_msync       26
#define __NR_mincore     27
#define __NR_madvise     28
#define __NR_dup         32
#define __NR_dup2        33
#define __NR_socket      41
#define __NR_connect     42
#define __NR_accept      43
#define __NR_sendto      44
#define __NR_recvfrom    45
#define __NR_send        46
#define __NR_recv        47
#define __NR_sendmsg     48
#define __NR_recvmsg     49
#define __NR_shutdown    50
#define __NR_bind        51
#define __NR_listen      52
#define __NR_getsockname 53
#define __NR_getpeername 54
#define __NR_socketpair  55
#define __NR_setsockopt  56
#define __NR_getsockopt  57
#define __NR_clone       56
#define __NR_fork        57
#define __NR_vfork       58
#define __NR_execve      59
#define __NR_exit        60
#define __NR_wait4       61
#define __NR_kill        62
#define __NR_getdents    78
#define __NR_getpid      39
#define __NR_getppid     110

// File-related flags
#define O_RDONLY             00
#define O_WRONLY             01
#define O_RDWR               02
#define O_CREAT            0100
#define O_EXCL             0200
#define O_NOCTTY           0400
#define O_TRUNC           01000
#define O_APPEND          02000
#define O_NONBLOCK        04000
#define O_DIRECTORY     0200000

// Access mode flags
#define F_OK    0   // Test for existence
#define R_OK    4   // Test for read permission
#define W_OK    2   // Test for write permission
#define X_OK    1   // Test for execute permission

#define SEEK_SET    0   // Seek from beginning of file
#define SEEK_CUR    1   // Seek from current position
#define SEEK_END    2   // Seek from end of file

// File mode (permission) bits
#define S_IFMT   0170000 // Bit mask for file type
#define S_IFSOCK 0140000 // Socket
#define S_IFLNK  0120000 // Symbolic link
#define S_IFREG  0100000 // Regular file
#define S_IFBLK  0060000 // Block device
#define S_IFDIR  0040000 // Directory
#define S_IFCHR  0020000 // Character device
#define S_IFIFO  0010000 // FIFO/pipe

#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

// Permission bits
#define S_IRWXU 0700    // Owner read/write/execute
#define S_IRUSR 0400    // Owner read
#define S_IWUSR 0200    // Owner write
#define S_IXUSR 0100    // Owner execute
#define S_IRWXG 070     // Group read/write/execute
#define S_IRGRP 040     // Group read
#define S_IWGRP 020     // Group write
#define S_IXGRP 010     // Group execute
#define S_IRWXO 07      // Others read/write/execute
#define S_IROTH 04      // Others read
#define S_IWOTH 02      // Others write
#define S_IXOTH 01      // Others execute

// Socket-related definitions
#define AF_UNSPEC       0
#define AF_UNIX         1
#define AF_INET         2
#define AF_INET6        10

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3
#define SOCK_RDM        4
#define SOCK_SEQPACKET  5

// Standard file descriptors
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

// Error numbers (matching Linux)
#define EPERM            1   // Operation not permitted
#define ENOENT           2   // No such file or directory
#define ESRCH            3   // No such process
#define EINTR            4   // Interrupted system call
#define EIO              5   // I/O error
#define ENXIO            6   // No such device or address
#define E2BIG            7   // Argument list too long
#define ENOEXEC          8   // Exec format error
#define EBADF            9   // Bad file number
#define ECHILD          10   // No child processes
#define EAGAIN          11   // Try again
#define ENOMEM          12   // Out of memory
#define EACCES          13   // Permission denied
#define EFAULT          14   // Bad address
#define ENOTBLK         15   // Block device required
#define EBUSY           16   // Device or resource busy
#define EEXIST          17   // File exists
#define EXDEV           18   // Cross-device link
#define ENODEV          19   // No such device
#define ENOTDIR         20   // Not a directory
#define EISDIR          21   // Is a directory
#define EINVAL          22   // Invalid argument
#define ENFILE          23   // File table overflow
#define EMFILE          24   // Too many open files
#define ENOTTY          25   // Not a typewriter
#define ETXTBSY         26   // Text file busy
#define EFBIG           27   // File too large
#define ENOSPC          28   // No space left on device
#define ESPIPE          29   // Illegal seek
#define EROFS           30   // Read-only file system
#define EMLINK          31   // Too many links
#define EPIPE           32   // Broken pipe
#define EDOM            33   // Math argument out of domain of func
#define ERANGE          34   // Math result not representable
#define ENOSYS          38   //

struct in_addr {
    uint32_t s_addr;  // IP address in network byte order
};

// Network address structures
struct sockaddr {
    sa_family_t sa_family;    // Address family
    char sa_data[14];         // Protocol-specific address
};

struct sockaddr_in {
    sa_family_t    sin_family;   // Address family (AF_INET)
    in_port_t      sin_port;     // Port in network byte order
    struct in_addr sin_addr;     // Internet address
    unsigned char  sin_zero[8];  // Same size as struct sockaddr
};

struct sockaddr_un {
    sa_family_t sun_family;      // AF_UNIX
    char sun_path[108];          // Pathname
};

struct timespec {
    time_t tv_sec;   // Seconds
    long tv_nsec;    // Nanoseconds
};

// Socket operation flags
#define MSG_OOB         0x1     // Out-of-band data
#define MSG_PEEK        0x2     // Peek at incoming data
#define MSG_DONTROUTE   0x4     // Send without routing
#define MSG_CTRUNC      0x8     // Control data truncated
#define MSG_PROXY       0x10    // Packet is forwarded

// Memory protection and mapping flags
#define PROT_NONE       0x0     // No access
#define PROT_READ       0x1     // Pages can be read
#define PROT_WRITE      0x2     // Pages can be written
#define PROT_EXEC       0x4     // Pages can be executed

#define MAP_SHARED      0x01    // Share changes
#define MAP_PRIVATE     0x02    // Changes are private
#define MAP_FIXED       0x10    // Interpret addr exactly
#define MAP_ANONYMOUS   0x20    // Don't use a file

// Signal-related definitions (basic set)
#define SIGHUP      1   // Hangup
#define SIGINT      2   // Interrupt
#define SIGQUIT     3   // Quit
#define SIGILL      4   // Illegal instruction
#define SIGTRAP     5   // Trace/breakpoint trap
#define SIGABRT     6   // Abort
#define SIGBUS      7   // Bus error
#define SIGFPE      8   // Floating point exception
#define SIGKILL     9   // Kill (cannot be caught or ignored)
#define SIGUSR1     10  // User-defined signal 1
#define SIGSEGV     11  // Segmentation violation
#define SIGUSR2     12  // User-defined signal 2
#define SIGPIPE     13  // Broken pipe
#define SIGALRM     14  // Alarm clock
#define SIGTERM     15  // Termination
#define SIGSTKFLT   16  // Stack fault
#define SIGCHLD     17  // Child status changed
#define SIGCONT     18  // Continue
#define SIGSTOP     19  // Stop (cannot be caught or ignored)
#define SIGTSTP     20  // Terminal stop signal
#define SIGTTIN     21  // Background process attempting read
#define SIGTTOU     22  // Background process attempting write
#define SIGURG      23  // Urgent data is available at a socket
#define SIGXCPU     24  // CPU time limit exceeded
#define SIGXFSZ     25  // File size limit exceeded
#define SIGVTALRM   26  // Virtual timer expired
#define SIGPROF     27  // Profiling timer expired
#define SIGWINCH    28  // Window size changed
#define SIGIO       29  // I/O now possible
#define SIGPWR      30  // Power failure
#define SIGSYS      31  // Bad system call

// Syscall declaration for kernel use
long syscall_handler(long syscall_num,
                    uint64_t arg1,
                    uint64_t arg2,
                    uint64_t arg3,
                    uint64_t arg4,
                    uint64_t arg5,
                    uint64_t arg6);

#endif // SYSCALLS_H