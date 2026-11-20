#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#include <sys/types.h>
#define S_IFMT   0xF000
#define S_IFDIR  0x4000
#define S_IFREG  0x8000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
struct stat {
    ino_t st_ino;
    mode_t st_mode;
    off_t st_size;
    unsigned int st_blocks;
    time_t st_mtime;
    nlink_t st_nlink;
};
int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int lstat(const char *path, struct stat *st);
int mkdir(const char *path, mode_t mode);
#endif
