/* spHalfOS system calls (int 0x80) */
#include "idt.h"
#include "process.h"
#include "syscall.h"
#include "spfs.h"
#include "console.h"
#include "kernel.h"
#include "string.h"
#include "mm.h"
#include "io.h"
#include "vbe.h"
#include "mouse.h"
#include "fb.h"
#include "rtl8139.h"

extern uint32_t rtc_time(void);

static int copy_str_from_user(char *dst, uint32_t uptr, int max) {
    if (!uptr) return -1;
    for (int i = 0; i < max - 1; i++) {
        if ((i & 0xFFF) == 0 && !vmm_user_range_ok(uptr + (uint32_t)i, 1, 0)) return -1;
        char c = *(volatile char *)(uptr + (uint32_t)i);
        dst[i] = c;
        if (!c) return 0;
    }
    dst[max - 1] = 0;
    return -1;
}

static int copy_to_user(uint32_t uptr, const void *src, uint32_t len) {
    if (!vmm_user_range_ok(uptr, len, 1)) return -1;
    memcpy((void *)uptr, src, len);
    return 0;
}

static int copy_from_user(void *dst, uint32_t uptr, uint32_t len) {
    if (!vmm_user_range_ok(uptr, len, 0)) return -1;
    memcpy(dst, (const void *)uptr, len);
    return 0;
}

static int fd_get(int fd, fd_t **out) {
    if (!current || fd < 0 || fd >= FD_MAX) return -1;
    if (!current->fds[fd].used) return -1;
    *out = &current->fds[fd];
    return 0;
}

static int alloc_fd(void) {
    for (int i = 3; i < FD_MAX; i++)
        if (!current->fds[i].used) return i;
    return -1;
}

static int sys_open(const char *path, int flags, int mode) {
    (void)mode;
    uint32_t ino;
    int r = fs_resolve(current->cwd, path, &ino);
    if (r < 0 && (flags & O_CREAT)) {
        char dbuf[256], nbuf[FS_MAX_NAME + 1];
        if (fs_split_path(path, dbuf, sizeof(dbuf), nbuf, sizeof(nbuf)) < 0) return FS_EINVAL;
        uint32_t dir;
        r = fs_resolve(current->cwd, dbuf, &dir);
        if (r < 0) return r;
        r = fs_create(dir, nbuf, FS_MODE_FILE, &ino);
        if (r < 0) return r;
    } else if (r < 0) {
        return r;
    }
    if ((flags & O_TRUNC) && !fs_is_dir(ino)) fs_truncate(ino);
    int fd = alloc_fd();
    if (fd < 0) return -1;
    fd_t *f = &current->fds[fd];
    memset(f, 0, sizeof(*f));
    f->used = 1;
    f->ino = ino;
    f->is_dir = fs_is_dir(ino);
    f->flags = 0;
    if (!(flags & O_WRONLY) || (flags & O_RDWR)) f->flags |= FD_FLAG_READ;
    if ((flags & O_WRONLY) || (flags & O_RDWR)) f->flags |= FD_FLAG_WRITE;
    if (flags & O_APPEND) f->flags |= FD_FLAG_APPEND;
    f->off = 0;
    if (flags & O_APPEND) {
        fs_inode_t in;
        if (fs_inode_get(ino, &in) == FS_OK) f->off = in.size;
    }
    return fd;
}

static int sys_read(int fd, uint32_t buf, uint32_t count) {
    if (!vmm_user_range_ok(buf, count, 1)) return -1;
    if (fd == 0 && !current->fds[0].used) {
        if (count == 0) return 0;
        int c = console_input_get();
        if (c < 0) return -11;   /* EAGAIN */
        uint8_t *p = (uint8_t *)buf;
        p[0] = (uint8_t)c;
        uint32_t n = 1;
        while (n < count) {
            c = console_input_get();
            if (c < 0) break;
            p[n++] = (uint8_t)c;
        }
        return (int)n;
    }
    fd_t *f;
    if (fd_get(fd, &f) < 0) return -1;
    if (f->is_dir) return -1;
    int n = fs_read_file(f->ino, f->off, (void *)buf, count);
    if (n > 0) f->off += (uint32_t)n;
    return n;
}

static int sys_write(int fd, uint32_t buf, uint32_t count) {
    if (!vmm_user_range_ok(buf, count, 0)) return -1;
    if ((fd == 1 || fd == 2) && !current->fds[fd].used) {
        console_write((const char *)buf, count);
        serial_write((const char *)buf, count);
        return (int)count;
    }
    fd_t *f;
    if (fd_get(fd, &f) < 0) return -1;
    if (f->is_dir) return -1;
    if (f->flags & FD_FLAG_APPEND) {
        fs_inode_t in;
        if (fs_inode_get(f->ino, &in) == FS_OK) f->off = in.size;
    }
    int n = fs_write_file(f->ino, f->off, (const void *)buf, count);
    if (n > 0) f->off += (uint32_t)n;
    return n;
}

static int sys_lseek(int fd, int32_t off, int whence) {
    fd_t *f;
    if (fd_get(fd, &f) < 0) return -1;
    fs_inode_t in;
    if (fs_inode_get(f->ino, &in) < 0) return -1;
    int32_t base;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (int32_t)f->off;
    else if (whence == SEEK_END) base = (int32_t)in.size;
    else return -1;
    int32_t n = base + off;
    if (n < 0) return -1;
    f->off = (uint32_t)n;
    return n;
}

static void fill_stat(uint32_t ino, sp_stat_t *st) {
    fs_inode_t in;
    memset(st, 0, sizeof(*st));
    if (fs_inode_get(ino, &in) < 0) return;
    st->ino = ino;
    st->mode = in.mode & 0xF000;
    st->size = in.size;
    st->blocks = (in.size + FS_BLOCK_SIZE - 1) / FS_BLOCK_SIZE;
    st->mtime = in.mtime;
    st->nlink = in.links;
}

static int sys_mkdir(const char *path) {
    char dbuf[256], nbuf[FS_MAX_NAME + 1];
    if (fs_split_path(path, dbuf, sizeof(dbuf), nbuf, sizeof(nbuf)) < 0) return -1;
    uint32_t dir;
    int r = fs_resolve(current->cwd, dbuf, &dir);
    if (r < 0) return r;
    return fs_mkdir(dir, nbuf, NULL);
}

static int sys_unlink(const char *path) {
    char dbuf[256], nbuf[FS_MAX_NAME + 1];
    if (fs_split_path(path, dbuf, sizeof(dbuf), nbuf, sizeof(nbuf)) < 0) return -1;
    uint32_t dir;
    int r = fs_resolve(current->cwd, dbuf, &dir);
    if (r < 0) return r;
    return fs_unlink(dir, nbuf);
}

static int sys_rename(const char *oldp, const char *newp) {
    char od[256], on[FS_MAX_NAME + 1], nd[256], nn[FS_MAX_NAME + 1];
    if (fs_split_path(oldp, od, sizeof(od), on, sizeof(on)) < 0) return -1;
    if (fs_split_path(newp, nd, sizeof(nd), nn, sizeof(nn)) < 0) return -1;
    uint32_t odir, ndir;
    int r = fs_resolve(current->cwd, od, &odir);
    if (r < 0) return r;
    r = fs_resolve(current->cwd, nd, &ndir);
    if (r < 0) return r;
    return fs_rename(odir, on, ndir, nn);
}

static int sys_stat(const char *path, sp_stat_t *st) {
    uint32_t ino;
    int r = fs_resolve(current->cwd, path, &ino);
    if (r < 0) return r;
    fill_stat(ino, st);
    return 0;
}

static int sys_readdir(int fd, uint32_t ubuf) {
    fd_t *f;
    if (fd_get(fd, &f) < 0 || !f->is_dir) return -1;
    sp_dirent_t d;
    char name[FS_MAX_NAME + 1];
    uint32_t ino;
    uint8_t type;
    int r = fs_dir_iterate(f->ino, &f->dir_cookie, name, sizeof(name), &ino, &type);
    if (r <= 0) return r;
    memset(&d, 0, sizeof(d));
    d.ino = ino;
    d.type = type;
    strncpy(d.name, name, sizeof(d.name) - 1);
    if (copy_to_user(ubuf, &d, sizeof(d)) < 0) return -1;
    return 1;
}

static uint32_t user_mmap(uint32_t len, int fd) {
    if (len == 0) return 0;
    len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t addr = current->mmap_next;
    if (addr + len >= 0xB0000000u) return 0;
    if (vmm_alloc_user_pages(current->pd, addr, len, 1) < 0) return 0;
    current->mmap_next += len + PAGE_SIZE;

    if (fd >= 3) {
        fd_t *f;
        if (fd_get(fd, &f) == 0) {
            uint8_t tmp[FS_BLOCK_SIZE];
            uint32_t done = 0;
            while (done < len) {
                uint32_t chunk = len - done;
                if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
                int n = fs_read_file(f->ino, done, tmp, chunk);
                if (n <= 0) break;
                if (copy_to_user(addr + done, tmp, (uint32_t)n) < 0) return 0;
                done += (uint32_t)n;
                if ((uint32_t)n < chunk) break;
            }
        }
    }
    return addr;
}

static int sys_spawn(uint32_t upath, uint32_t uargv, int argc) {
    static char path[256];
    static char argbuf[33][128];
    static char *kargv[34];
    if (argc < 0 || argc > 32) return -1;
    if (copy_str_from_user(path, upath, sizeof(path)) < 0) return -1;
    for (int i = 0; i < argc; i++) {
        uint32_t up;
        if (copy_from_user(&up, uargv + (uint32_t)i * 4, 4) < 0) return -1;
        if (copy_str_from_user(argbuf[i], up, sizeof(argbuf[i])) < 0) return -1;
        kargv[i] = argbuf[i];
    }
    kargv[argc] = NULL;
    proc_t *p = process_create(path, kargv, argc, current);
    if (!p) return -1;
    return p->pid;
}

int syscall_dispatch(regs_t *r) {
    int n = (int)r->eax;
    uint32_t a = r->ebx, b = r->ecx, c = r->edx, d = r->esi, e = r->edi;
    char path[256], path2[256];

    switch (n) {
    case SYS_EXIT:
        process_exit(r, (int)a);
        return 0;
    case SYS_WRITE:
        return sys_write((int)a, b, c);
    case SYS_READ:
        return sys_read((int)a, b, c);
    case SYS_OPEN:
        if (copy_str_from_user(path, a, sizeof(path)) < 0) return -1;
        return sys_open(path, (int)b, (int)c);
    case SYS_CLOSE:
        if (a < FD_MAX) { current->fds[a].used = 0; return 0; }
        return -1;
    case SYS_LSEEK:
        return sys_lseek((int)a, (int32_t)b, (int)c);
    case SYS_MKDIR:
        if (copy_str_from_user(path, a, sizeof(path)) < 0) return -1;
        return sys_mkdir(path);
    case SYS_UNLINK:
        if (copy_str_from_user(path, a, sizeof(path)) < 0) return -1;
        return sys_unlink(path);
    case SYS_RENAME:
        if (copy_str_from_user(path, a, sizeof(path)) < 0) return -1;
        if (copy_str_from_user(path2, b, sizeof(path2)) < 0) return -1;
        return sys_rename(path, path2);
    case SYS_STAT: {
        if (copy_str_from_user(path, a, sizeof(path)) < 0) return -1;
        sp_stat_t st;
        int rr = sys_stat(path, &st);
        if (rr < 0) return rr;
        return copy_to_user(b, &st, sizeof(st)) < 0 ? -1 : 0;
    }
    case SYS_FSTAT: {
        fd_t *f;
        if (fd_get((int)a, &f) < 0) return -1;
        sp_stat_t st;
        fill_stat(f->ino, &st);
        return copy_to_user(b, &st, sizeof(st)) < 0 ? -1 : 0;
    }
    case SYS_READDIR:
        return sys_readdir((int)a, b);
    case SYS_CHDIR: {
        if (copy_str_from_user(path, a, sizeof(path)) < 0) return -1;
        uint32_t ino;
        int rr = fs_resolve(current->cwd, path, &ino);
        if (rr < 0) return rr;
        if (!fs_is_dir(ino)) return FS_ENOTDIR;
        current->cwd = ino;
        return 0;
    }
    case SYS_GETCWD: {
        char cwd[256];
        if (fs_get_path(current->cwd, cwd, sizeof(cwd)) < 0) return -1;
        uint32_t len = (uint32_t)strlen(cwd) + 1;
        if (len > b) len = b;
        return copy_to_user(a, cwd, len) < 0 ? -1 : (int)len;
    }
    case SYS_BRK: {
        uint32_t req = a;
        if (req == 0) return (int)current->brk;
        if (req < current->brk) return (int)current->brk;
        if (req >= 0x4F000000u) return (int)current->brk;
        uint32_t newbrk = (req + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (newbrk > current->brk) {
            if (vmm_alloc_user_pages(current->pd, current->brk, newbrk - current->brk, 1) < 0)
                return (int)current->brk;
        }
        current->brk = newbrk;
        return (int)current->brk;
    }
    case SYS_MMAP: {
        uint32_t addr = user_mmap(b, (int)e);
        return (int)addr;
    }
    case SYS_MUNMAP:
        return 0;
    case SYS_FBINFO: {
        sp_fbinfo_t fi;
        if (vbe_available()) {
            fi.available = 1;
            fi.width = vbe_width();
            fi.height = vbe_height();
            fi.pitch = vbe_pitch();
            fi.bpp = vbe_bpp();
            fi.pixels = fi.width * fi.height;
            fi.base = FB_BASE;
        } else {
            memset(&fi, 0, sizeof(fi));
            fi.available = 0;
            fi.base = FB_BASE;
        }
        return copy_to_user(a, &fi, sizeof(fi)) < 0 ? -1 : 0;
    }
    case SYS_FBMAP: {
        if (!vbe_available()) return 0;
        uint32_t len = vbe_pitch() * vbe_height();
        len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint32_t phys = vbe_phys();
        if (phys && !(phys & (PAGE_SIZE - 1)) && (FB_BASE + len) < USER_VADDR_MAX) {
            for (uint32_t off = 0; off < len; off += PAGE_SIZE)
                if (vmm_map_user_page(current->pd, FB_BASE + off, phys + off, 1) < 0)
                    return 0;
            kprintf("[fbmap] ok phys=%x len=%x\n", phys, len);
            return (int)FB_BASE;
        }
        kprintf("[fbmap] refused phys=%x len=%x\n", phys, len);
        return 0;
    }
    case SYS_MOUSE: {
        mouse_state_t m;
        mouse_state(&m);
        sp_mouse_t sm;
        sm.x = m.x; sm.y = m.y; sm.dx = m.dx; sm.dy = m.dy; sm.buttons = m.buttons;
        return copy_to_user(a, &sm, sizeof(sm)) < 0 ? -1 : 0;
    }
    case SYS_WIN_PRESENT: {
        int sw = (int)b, sh = (int)c;
        if (sw <= 0 || sh <= 0 || sw > 1920 || sh > 1080) return -1;
        uint32_t bytes = (uint32_t)sw * (uint32_t)sh * 4u;
        if (!vmm_user_range_ok(a, bytes, 0)) return -1;
        console_win_present((const uint32_t *)a, sw, sh);
        return 0;
    }
    case SYS_WIN_INFO: {
        sp_wininfo_t wi;
        if (console_win_info(&wi) < 0) return -1;
        return copy_to_user(a, &wi, sizeof(wi)) < 0 ? -1 : 0;
    }
    case SYS_SPAWN:
        return sys_spawn(a, b, (int)c);
    case SYS_WAIT: {
        int status = 0;
        int rr = process_wait_child((int)a, &status);
        if (rr > 0) return status;
        if (rr == -2) return -2;
        return -1;
    }
    case SYS_GETPID:
        return current->pid;
    case SYS_SLEEP:
        r->eax = 0;
        sched_yield(r);
        return 0;
    case SYS_TIME:
        return (int)rtc_time();
    case SYS_KBHIT:
        return console_input_available();
    case SYS_GOTOXY:
        console_gotoxy((int)a, (int)b);
        return 0;
    case SYS_CLRSCR:
        console_clear();
        return 0;
    case SYS_SETCOLOR:
        console_set_attr((uint8_t)a);
        return 0;
    case SYS_BLIT: {
        int w = (int)b, h = (int)c;
        if (w <= 0 || h <= 0 || w > CON_MAX_COLS || h > CON_MAX_ROWS) return -1;
        uint32_t bytes = (uint32_t)w * (uint32_t)h * 2;
        if (!vmm_user_range_ok(d, bytes, 0)) return -1;
        console_blit((int)a & 0xFFFF, ((int)a >> 16) & 0xFFFF, w, h, (const uint16_t *)d);
        return 0;
    }
    case SYS_NET_INFO: {
        sp_netinfo_t ni;
        memset(&ni, 0, sizeof(ni));
        ni.present = rtl8139_present() ? 1 : 0;
        ni.link = ni.present;
        if (ni.present) rtl8139_get_mac(ni.mac);
        return copy_to_user(a, &ni, sizeof(ni)) < 0 ? -1 : 0;
    }
    case SYS_NET_SEND: {
        int len = (int)b;
        if (len <= 0 || len > 1600) return -1;
        if (!vmm_user_range_ok(a, (uint32_t)len, 0)) return -1;
        return rtl8139_send((const void *)a, (uint32_t)len);
    }
    case SYS_NET_RECV: {
        int maxlen = (int)b;
        if (maxlen <= 0 || maxlen > 1600) return -1;
        if (!vmm_user_range_ok(a, (uint32_t)maxlen, 1)) return -1;
        return rtl8139_recv((void *)a, (uint32_t)maxlen);
    }
    case SYS_IOCTL: {
        /* minimal ioctl: TIOCGWINSZ */
        #define K_TIOCGWINSZ 0x5413
        struct { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; } ws;
        if ((uint32_t)b == K_TIOCGWINSZ) {
            int cols=80, rows=25;
            vga_get_tty_size(&cols, &rows);
            ws.ws_col = (unsigned short)cols;
            ws.ws_row = (unsigned short)rows;
            ws.ws_xpixel = (unsigned short)(cols * 8);
            ws.ws_ypixel = (unsigned short)(rows * 16);
            if (!vmm_user_range_ok(c, sizeof(ws), 1)) return -1;
            return copy_to_user(c, &ws, sizeof(ws)) < 0 ? -1 : 0;
        }
        return -1;
    }
    case SYS_YIELD:
        r->eax = 0;
        sched_yield(r);
        return 0;
    case SYS_UNAME: {
        const char *s = "spHalfOS";
        uint32_t len = (uint32_t)strlen(s) + 1;
        if (len > b) len = b;
        return copy_to_user(a, s, len) < 0 ? -1 : (int)len;
    }
    case SYS_SYSINFO: {
        sp_sysinfo_t si;
        si.total_frames = pmm_total_frames();
        si.free_frames = pmm_free_frames();
        si.uptime_sec = ticks_get() / 60;
        si.procs = (uint32_t)process_count_used();
        return copy_to_user(a, &si, sizeof(si)) < 0 ? -1 : 0;
    }
    case SYS_PS: {
        int max = (int)b;
        if (max <= 0 || max > 64) return -1;
        sp_ps_t out[64];
        int k = 0;
        for (int i = 0; i < PROC_MAX && k < max; i++) {
            if (!procs[i].used) continue;
            memset(&out[k], 0, sizeof(out[k]));
            out[k].pid = procs[i].pid;
            out[k].ppid = procs[i].ppid;
            out[k].state = procs[i].state;
            strncpy(out[k].name, procs[i].name, sizeof(out[k].name) - 1);
            k++;
        }
        if (copy_to_user(a, out, (uint32_t)k * sizeof(sp_ps_t)) < 0) return -1;
        return k;
    }
    case SYS_KILL: {
        proc_t *p = process_find((int)a);
        if (!p || p == current) return -1;
        if (p->ppid != current->pid) return -1;
        process_kill(p);
        return 0;
    }
    case SYS_REBOOT:
        reboot_now();
        return 0;
    case SYS_GETKEY:
        return console_input_get();
    case SYS_TICKS:
        return (int)ticks_get();
    case SYS_ACCESS: {
        if (copy_str_from_user(path, a, sizeof(path)) < 0) return -1;
        uint32_t ino;
        return fs_resolve(current->cwd, path, &ino) < 0 ? -1 : 0;
    }
    case SYS_DUP2: {
        int oldfd = (int)a, newfd = (int)b;
        if (oldfd < 0 || oldfd >= FD_MAX || newfd < 0 || newfd >= FD_MAX) return -1;
        if (!current->fds[oldfd].used) return -1;
        current->fds[newfd] = current->fds[oldfd];
        return newfd;
    }
    default:
        return -1;
    }
}
