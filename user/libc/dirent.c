#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <syscall.h>
#include <errno.h>

struct __DIR {
    int fd;
    struct dirent de;
};

DIR *opendir(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return NULL;
    DIR *d = malloc(sizeof(DIR));
    if (!d) { close(fd); return NULL; }
    d->fd = fd;
    return d;
}

struct dirent *readdir(DIR *d) {
    if (!d) return NULL;
    int r = syscall2(SYS_READDIR, d->fd, (int)&d->de);
    if (r <= 0) return NULL;
    return &d->de;
}

int closedir(DIR *d) {
    if (!d) return -1;
    close(d->fd);
    free(d);
    return 0;
}

void rewinddir(DIR *d) {
    if (d) lseek(d->fd, 0, SEEK_SET);
}
