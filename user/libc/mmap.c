#include <sys/mman.h>
#include <syscall.h>
#include <errno.h>
#include <stdint.h>

void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    (void)addr; (void)prot; (void)flags; (void)off;
    int r = syscall5(SYS_MMAP, 0, (int)len, 0, 0, fd);
    if (r <= 0) { errno = ENOMEM; return MAP_FAILED; }
    return (void *)(uintptr_t)r;
}

int munmap(void *addr, size_t len) {
    (void)addr; (void)len;
    return 0;
}

int mprotect(void *addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot;
    return 0;
}
