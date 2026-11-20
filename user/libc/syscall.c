#include <syscall.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <fb.h>

int errno;

int syscall0(int n) {
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n) : "memory");
    return r;
}
int syscall1(int n, int a) {
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a) : "memory");
    return r;
}
int syscall2(int n, int a, int b) {
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b) : "memory");
    return r;
}
int syscall3(int n, int a, int b, int c) {
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}
int syscall4(int n, int a, int b, int c, int d) {
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d) : "memory");
    return r;
}
int syscall5(int n, int a, int b, int c, int d, int e) {
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory");
    return r;
}

static int set_errno(int r) {
    if (r < 0) { errno = -r; return -1; }
    return r;
}

ssize_t read(int fd, void *buf, size_t n) {
    for (;;) {
        int r = syscall3(SYS_READ, fd, (int)buf, (int)n);
        if (r != -11) return set_errno(r);
        syscall0(SYS_YIELD);
    }
}

ssize_t write(int fd, const void *buf, size_t n) {
    return set_errno(syscall3(SYS_WRITE, fd, (int)buf, (int)n));
}

int open(const char *path, int flags, ...) {
    int mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return set_errno(syscall3(SYS_OPEN, (int)path, flags, mode));
}

int close(int fd) { return set_errno(syscall1(SYS_CLOSE, fd)); }

off_t lseek(int fd, off_t off, int whence) {
    return set_errno(syscall3(SYS_LSEEK, fd, (int)off, whence));
}

int mkdir(const char *path, mode_t mode) {
    (void)mode;
    return set_errno(syscall1(SYS_MKDIR, (int)path));
}
int unlink(const char *path) { return set_errno(syscall1(SYS_UNLINK, (int)path)); }
int rmdir(const char *path) { return set_errno(syscall1(SYS_UNLINK, (int)path)); }
int rename(const char *a, const char *b) { return set_errno(syscall2(SYS_RENAME, (int)a, (int)b)); }
int remove(const char *a) { return unlink(a); }

int stat(const char *path, struct stat *st) {
    sp_stat_t s;
    int r = set_errno(syscall2(SYS_STAT, (int)path, (int)&s));
    if (r < 0) return r;
    st->st_ino = s.ino; st->st_mode = s.mode; st->st_size = s.size;
    st->st_blocks = s.blocks; st->st_mtime = s.mtime; st->st_nlink = s.nlink;
    return 0;
}
int lstat(const char *path, struct stat *st) { return stat(path, st); }
int fstat(int fd, struct stat *st) {
    sp_stat_t s;
    int r = set_errno(syscall2(SYS_FSTAT, fd, (int)&s));
    if (r < 0) return r;
    st->st_ino = s.ino; st->st_mode = s.mode; st->st_size = s.size;
    st->st_blocks = s.blocks; st->st_mtime = s.mtime; st->st_nlink = s.nlink;
    return 0;
}

int chdir(const char *path) { return set_errno(syscall1(SYS_CHDIR, (int)path)); }
char *getcwd(char *buf, size_t size) {
    int r = set_errno(syscall2(SYS_GETCWD, (int)buf, (int)size));
    return r < 0 ? NULL : buf;
}
int access(const char *path, int mode) {
    (void)mode;
    return set_errno(syscall1(SYS_ACCESS, (int)path));
}
int isatty(int fd) { return fd >= 0 && fd <= 2; }
int dup(int fd) { (void)fd; errno = ENOSYS; return -1; }
int dup2(int oldfd, int newfd) { return set_errno(syscall2(SYS_DUP2, oldfd, newfd)); }
pid_t getpid(void) { return syscall0(SYS_GETPID); }
pid_t fork(void) { errno = ENOSYS; return -1; }
int execv(const char *path, char *const argv[]) {
    int argc = 0;
    while (argv && argv[argc]) argc++;
    return syscall3(SYS_SPAWN, (int)path, (int)argv, argc) < 0 ? -1 : 0;
}
int execvp(const char *file, char *const argv[]) { return execv(file, argv); }
int fsync(int fd) { (void)fd; return 0; }
int truncate(const char *path, off_t len) { (void)path; (void)len; errno = ENOSYS; return -1; }
int ftruncate(int fd, off_t len) { (void)fd; (void)len; errno = ENOSYS; return -1; }
char *getlogin(void) { return "sp"; }

unsigned int sleep(unsigned int sec) {
    unsigned int end = (unsigned int)syscall0(SYS_TICKS) + sec * 60;
    while ((unsigned int)syscall0(SYS_TICKS) < end)
        syscall0(SYS_YIELD);
    return 0;
}
int usleep(unsigned int usec) {
    unsigned int ticks = (usec + 16666u) / 16667u;
    if (!ticks) ticks = 1;
    unsigned int end = (unsigned int)syscall0(SYS_TICKS) + ticks;
    while ((unsigned int)syscall0(SYS_TICKS) < end)
        syscall0(SYS_YIELD);
    return 0;
}

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    (void)tz;
    unsigned int t = (unsigned int)syscall0(SYS_TIME);
    unsigned int ticks = (unsigned int)syscall0(SYS_TICKS);
    tv->tv_sec = t;
    tv->tv_usec = (long)(ticks % 60u) * 16667L;
    return 0;
}

int sp_kbhit(void) { return syscall0(SYS_KBHIT); }
int sp_getkey(void) { return syscall0(SYS_GETKEY); }
int sp_ticks(void) { return syscall0(SYS_TICKS); }
void sp_gotoxy(int x, int y) { syscall2(SYS_GOTOXY, x, y); }
void sp_clrscr(void) { syscall0(SYS_CLRSCR); }
void sp_setcolor(int attr) { syscall1(SYS_SETCOLOR, attr); }
void sp_blit(int x, int y, int w, int h, const uint16_t *cells) {
    syscall4(SYS_BLIT, (x & 0xFFFF) | (y << 16), w, h, (int)cells);
}
int sp_spawn(const char *path, char *const argv[], int argc) {
    return set_errno(syscall3(SYS_SPAWN, (int)path, (int)argv, argc));
}
int sp_wait(int pid) { return syscall1(SYS_WAIT, pid); }
int sp_reboot(void) { return syscall0(SYS_REBOOT); }

int fb_info(sp_fbinfo_t *info) {
    return syscall1(SYS_FBINFO, (int)info);
}
void *fb_map(void) {
    int r = syscall0(SYS_FBMAP);
    return (void *)(uintptr_t)r;
}
int sp_win_present(const void *pixels, int src_w, int src_h) {
    return syscall3(SYS_WIN_PRESENT, (int)pixels, src_w, src_h);
}
int mouse_read(sp_mouse_t *m) {
    return syscall1(SYS_MOUSE, (int)m);
}
int sp_win_info(sp_wininfo_t *info) {
    return syscall1(SYS_WIN_INFO, (int)info);
}
int sp_net_info(sp_netinfo_t *info) {
    return syscall1(SYS_NET_INFO, (int)info);
}
int sp_net_send(const void *buf, int len) {
    return syscall2(SYS_NET_SEND, (int)buf, len);
}
int sp_net_recv(void *buf, int maxlen) {
    return syscall2(SYS_NET_RECV, (int)buf, maxlen);
}


int ioctl(int fd, unsigned long req, void *arg) {
    (void)fd;
    return set_errno(syscall3(SYS_IOCTL, (int)fd, (int)req, (int)arg));
}

int tcgetattr(int fd, struct termios *t) {
    (void)fd;
    if (t) memset(t, 0, sizeof(*t));
    return 0;
}

int tcsetattr(int fd, int act, const struct termios *t) {
    (void)fd; (void)act; (void)t;
    return 0;
}

void _exit(int code) {
    syscall1(SYS_EXIT, code);
    for (;;) ;
}
void exit(int code) { _exit(code); }
