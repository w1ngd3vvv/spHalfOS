#ifndef _SP_SYSCALL_H
#define _SP_SYSCALL_H
#include <stdint.h>
#include <fb.h>

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

typedef struct __attribute__((packed)) {
    int32_t x, y, w, h;
    int32_t screen_w, screen_h;
    int32_t win_x, win_y, win_w, win_h;
} sp_wininfo_t;

typedef struct __attribute__((packed)) {
    uint8_t mac[6];
    uint8_t present;
    uint8_t link;
} sp_netinfo_t;

typedef struct __attribute__((packed)) {
    uint32_t ino, mode, size, blocks, mtime, nlink;
} sp_stat_t;
typedef struct __attribute__((packed)) {
    uint32_t ino;
    uint8_t type;
    char name[60];
} sp_dirent_t;
typedef struct __attribute__((packed)) {
    uint32_t total_frames, free_frames, uptime_sec, procs;
} sp_sysinfo_t;
typedef struct __attribute__((packed)) {
    int32_t pid, ppid, state;
    char name[32];
} sp_ps_t;

int syscall0(int n);
int syscall1(int n, int a);
int syscall2(int n, int a, int b);
int syscall3(int n, int a, int b, int c);
int syscall4(int n, int a, int b, int c, int d);
int syscall5(int n, int a, int b, int c, int d, int e);

/* convenience */
int sp_kbhit(void);
int sp_getkey(void);
int sp_ticks(void);
void sp_gotoxy(int x, int y);
void sp_clrscr(void);
void sp_setcolor(int attr);
void sp_blit(int x, int y, int w, int h, const uint16_t *cells);
int sp_spawn(const char *path, char *const argv[], int argc);
int sp_wait(int pid);
int sp_reboot(void);
int sp_getcwd(char *buf, int size);
int sp_chdir(const char *path);
int sp_win_present(const void *pixels, int src_w, int src_h);
int sp_win_info(sp_wininfo_t *info);
int sp_net_info(sp_netinfo_t *info);
int sp_net_send(const void *buf, int len);
int sp_net_recv(void *buf, int maxlen);
#endif
