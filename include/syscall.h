#ifndef SP_SYSCALL_H
#define SP_SYSCALL_H

#include "types.h"

#define SYS_EXIT     1
#define SYS_WRITE    2
#define SYS_READ     3
#define SYS_OPEN     4
#define SYS_CLOSE    5
#define SYS_LSEEK    6
#define SYS_MKDIR    7
#define SYS_UNLINK   8
#define SYS_RENAME   9
#define SYS_STAT     10
#define SYS_READDIR  11
#define SYS_CHDIR    12
#define SYS_GETCWD   13
#define SYS_BRK      14
#define SYS_MMAP     15
#define SYS_MUNMAP   16
#define SYS_SPAWN    17
#define SYS_WAIT     18
#define SYS_GETPID   19
#define SYS_SLEEP    20
#define SYS_TIME     21
#define SYS_KBHIT    22
#define SYS_GOTOXY   23
#define SYS_CLRSCR   24
#define SYS_SETCOLOR 25
#define SYS_BLIT     26
#define SYS_YIELD    27
#define SYS_UNAME    28
#define SYS_SYSINFO  29
#define SYS_PS       30
#define SYS_KILL     31
#define SYS_REBOOT   32
#define SYS_GETKEY   33
#define SYS_FSTAT    34
#define SYS_TICKS    35
#define SYS_ACCESS   36
#define SYS_DUP2     37
#define SYS_FBINFO   38
#define SYS_FBMAP    39
#define SYS_MOUSE    40
#define SYS_WIN_PRESENT 41
#define SYS_WIN_INFO    42
#define SYS_NET_INFO    43
#define SYS_NET_SEND    44
#define SYS_NET_RECV    45
#define SYS_IOCTL       46

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_CREAT  0x0040
#define O_TRUNC  0x0200
#define O_APPEND 0x0400

#define SP_MODE_FILE 0x8000
#define SP_MODE_DIR  0x4000

typedef struct PACKED {
    uint32_t ino;
    uint32_t mode;
    uint32_t size;
    uint32_t blocks;
    uint32_t mtime;
    uint32_t nlink;
} sp_stat_t;

typedef struct PACKED {
    uint32_t ino;
    uint8_t  type;
    char     name[60];
} sp_dirent_t;

typedef struct PACKED {
    uint32_t total_frames;
    uint32_t free_frames;
    uint32_t uptime_sec;
    uint32_t procs;
} sp_sysinfo_t;

typedef struct PACKED {
    int32_t  pid;
    int32_t  ppid;
    int32_t  state;
    char     name[32];
} sp_ps_t;

typedef struct PACKED {
    uint8_t mac[6];
    uint8_t present;
    uint8_t link;
} sp_netinfo_t;

#endif
