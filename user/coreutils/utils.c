/* spHalfOS core utilities: multi-call binary */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <syscall.h>
#include <time.h>

static const char *prog = "";

static const char *base(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static int cmd_ls(int argc, char **argv) {
    int longfmt = 0, all = 0, argi = 1;
    for (; argi < argc && argv[argi][0] == '-'; argi++) {
        for (const char *p = argv[argi] + 1; *p; p++) {
            if (*p == 'l') longfmt = 1;
            else if (*p == 'a') all = 1;
        }
    }
    const char *path = argi < argc ? argv[argi] : ".";
    DIR *d = opendir(path);
    if (!d) {
        struct stat st;
        if (stat(path, &st) == 0 && !S_ISDIR(st.st_mode)) {
            printf("%s\n", path);
            return 0;
        }
        fprintf(stderr, "ls: %s: %s\n", path, strerror(errno));
        return 1;
    }
    struct dirent *de;
    int count = 0;
    while ((de = readdir(d)) != NULL) {
        if (!all && (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))) continue;
        if (longfmt) {
            struct stat st;
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
            if (stat(full, &st) < 0) memset(&st, 0, sizeof(st));
            printf("%c %8u %6u %s%s\n",
                   S_ISDIR(st.st_mode) ? 'd' : '-',
                   (unsigned)st.st_size, (unsigned)st.st_ino,
                   de->d_name, S_ISDIR(st.st_mode) ? "/" : "");
        } else {
            if (de->d_type == 2) printf("\033[1;34m%s/\033[0m\n", de->d_name);
            else printf("%s\n", de->d_name);
        }
        count++;
    }
    closedir(d);
    if (!count) printf("(empty)\n");
    return 0;
}

static int copy_stream(int in, int out) {
    char buf[4096];
    int r;
    while ((r = (int)read(in, buf, sizeof(buf))) > 0) {
        int off = 0;
        while (off < r) {
            int w = (int)write(out, buf + off, r - off);
            if (w <= 0) return -1;
            off += w;
        }
    }
    return r < 0 ? -1 : 0;
}

static int cat_one(const char *path) {
    int fd = !strcmp(path, "-") ? 0 : open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "cat: %s: %s\n", path, strerror(errno)); return 1; }
    int r = copy_stream(fd, 1);
    if (fd) close(fd);
    return r < 0 ? 1 : 0;
}

static int cmd_cat(int argc, char **argv) {
    if (argc < 2) return cat_one("-");
    int rc = 0;
    for (int i = 1; i < argc; i++) rc |= cat_one(argv[i]);
    return rc;
}

static char *join_path(const char *dir, const char *name) {
    static char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s", dir, name);
    return buf;
}

static int cmd_cp(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: cp src dst\n"); return 1; }
    const char *src = argv[1], *dst = argv[2];
    struct stat ds;
    if (stat(dst, &ds) == 0 && S_ISDIR(ds.st_mode)) dst = join_path(dst, base(src));
    int in = open(src, O_RDONLY);
    if (in < 0) { fprintf(stderr, "cp: %s: %s\n", src, strerror(errno)); return 1; }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { fprintf(stderr, "cp: %s: %s\n", dst, strerror(errno)); close(in); return 1; }
    int r = copy_stream(in, out);
    close(in);
    close(out);
    if (r < 0) fprintf(stderr, "cp: write failed\n");
    return r < 0;
}

static int cmd_mv(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: mv src dst\n"); return 1; }
    const char *src = argv[1], *dst = argv[2];
    struct stat ds;
    if (stat(dst, &ds) == 0 && S_ISDIR(ds.st_mode)) dst = join_path(dst, base(src));
    if (rename(src, dst) < 0) {
        fprintf(stderr, "mv: %s: %s\n", src, strerror(errno));
        return 1;
    }
    return 0;
}

static int remove_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return -1;
        struct dirent *de;
        int rc = 0;
        while ((de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
            rc |= remove_recursive(full);
        }
        closedir(d);
        if (rc) return rc;
    }
    return unlink(path);
}

static int cmd_rm(int argc, char **argv) {
    int recursive = 0, i = 1;
    for (; i < argc && argv[i][0] == '-'; i++)
        if (strchr(argv[i], 'r') || strchr(argv[i], 'R')) recursive = 1;
    if (i >= argc) { fprintf(stderr, "usage: rm [-r] file...\n"); return 1; }
    int rc = 0;
    for (; i < argc; i++) {
        int r = recursive ? remove_recursive(argv[i]) : unlink(argv[i]);
        if (r < 0) { fprintf(stderr, "rm: %s: %s\n", argv[i], strerror(errno)); rc = 1; }
    }
    return rc;
}

static int cmd_mkdir(int argc, char **argv) {
    int parents = 0, i = 1;
    for (; i < argc && argv[i][0] == '-'; i++)
        if (strchr(argv[i], 'p')) parents = 1;
    if (i >= argc) { fprintf(stderr, "usage: mkdir [-p] dir...\n"); return 1; }
    int rc = 0;
    for (; i < argc; i++) {
        if (!parents) {
            if (mkdir(argv[i], 0777) < 0) {
                fprintf(stderr, "mkdir: %s: %s\n", argv[i], strerror(errno));
                rc = 1;
            }
        } else {
            char tmp[512];
            snprintf(tmp, sizeof(tmp), "%s", argv[i]);
            for (char *p = tmp + 1; *p; p++) {
                if (*p == '/') {
                    *p = 0;
                    if (access(tmp, 0) < 0) mkdir(tmp, 0777);
                    *p = '/';
                }
            }
            if (access(tmp, 0) < 0 && mkdir(tmp, 0777) < 0) {
                fprintf(stderr, "mkdir: %s: %s\n", argv[i], strerror(errno));
                rc = 1;
            }
        }
    }
    return rc;
}

static int cmd_touch(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: touch file...\n"); return 1; }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);
        if (fd < 0) { fprintf(stderr, "touch: %s: %s\n", argv[i], strerror(errno)); rc = 1; }
        else close(fd);
    }
    return rc;
}

static int cmd_wc(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "-";
    int fd = !strcmp(path, "-") ? 0 : open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "wc: %s: %s\n", path, strerror(errno)); return 1; }
    char buf[4096];
    unsigned long bytes = 0, lines = 0, words = 0;
    int inword = 0, r;
    while ((r = (int)read(fd, buf, sizeof(buf))) > 0) {
        bytes += (unsigned long)r;
        for (int i = 0; i < r; i++) {
            if (buf[i] == '\n') lines++;
            if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r') inword = 0;
            else if (!inword) { inword = 1; words++; }
        }
    }
    if (fd) close(fd);
    printf("%lu %lu %lu %s\n", lines, words, bytes, strcmp(path, "-") ? path : "");
    return 0;
}

static int copy_head_tail(int argc, char **argv, int tail) {
    int n = 10, argi = 1;
    if (argi < argc && !strcmp(argv[argi], "-n") && argi + 1 < argc) {
        n = atoi(argv[argi + 1]);
        argi += 2;
    }
    const char *path = argi < argc ? argv[argi] : "-";
    int fd = !strcmp(path, "-") ? 0 : open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno)); return 1; }
    if (!tail) {
        char buf[4096];
        int left = n, r;
        while (left > 0 && (r = (int)read(fd, buf, sizeof(buf))) > 0) {
            int start = 0;
            for (int i = 0; i < r && left > 0; i++) {
                if (buf[i] == '\n') {
                    write(1, buf + start, i - start + 1);
                    start = i + 1;
                    left--;
                }
            }
            if (left > 0 && start < r) {
                /* keep partial line only if file may end */
                if (start < r) { write(1, buf + start, r - start); }
            }
        }
    } else {
        /* tail: keep a ring of lines in memory (files here are small) */
        struct stat st;
        fstat(fd, &st);
        unsigned size = (unsigned)st.st_size;
        char *data = malloc(size + 1);
        if (!data) { if (fd) close(fd); return 1; }
        unsigned got = 0;
        int r;
        while (got < size && (r = (int)read(fd, data + got, size - got)) > 0) got += (unsigned)r;
        data[got] = 0;
        unsigned start = 0;
        int lines = 0;
        for (unsigned i = 0; i < got; i++) if (data[i] == '\n') lines++;
        int skip = lines - n;
        if (skip > 0) {
            for (unsigned i = 0; i < got; i++) {
                if (data[i] == '\n') {
                    if (skip-- == 0) { start = i + 1; break; }
                }
            }
        }
        if (start < got) write(1, data + start, got - start);
        free(data);
    }
    if (fd) close(fd);
    return 0;
}

static int cmd_head(int argc, char **argv) { return copy_head_tail(argc, argv, 0); }
static int cmd_tail(int argc, char **argv) { return copy_head_tail(argc, argv, 1); }

static int cmd_date(int argc, char **argv) {
    (void)argc; (void)argv;
    time_t t = time(NULL);
    printf("%s", ctime(&t));
    return 0;
}

static int cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    sp_ps_t list[64];
    int n = syscall2(SYS_PS, (int)list, 64);
    if (n < 0) { fprintf(stderr, "ps: failed\n"); return 1; }
    printf("  PID  PPID STATE NAME\n");
    for (int i = 0; i < n; i++) {
        const char *state = list[i].state == 0 ? "free" :
                            list[i].state == 1 ? "ready" :
                            list[i].state == 2 ? "run" : "zombie";
        printf("%5d %5d %5s %s\n", list[i].pid, list[i].ppid, state, list[i].name);
    }
    return 0;
}

static int cmd_free(int argc, char **argv) {
    (void)argc; (void)argv;
    sp_sysinfo_t si;
    if (syscall1(SYS_SYSINFO, (int)&si) < 0) return 1;
    printf("           total      free\n");
    printf("Mem:  %8uK %8uK\n", si.total_frames * 4, si.free_frames * 4);
    printf("Procs: %u   Uptime: %us\n", si.procs, si.uptime_sec);
    return 0;
}

static int cmd_uname(int argc, char **argv) {
    char buf[64];
    if (syscall2(SYS_UNAME, (int)buf, sizeof(buf)) < 0) strcpy(buf, "spHalfOS");
    if (argc > 1 && !strcmp(argv[1], "-a"))
        printf("%s 0.1.0 i386 spFS\n", buf);
    else
        printf("%s\n", buf);
    return 0;
}

static int cmd_sleep(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: sleep seconds\n"); return 1; }
    int sec = atoi(argv[1]);
    while (sec-- > 0) sleep(1);
    return 0;
}

static int cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    sp_reboot();
    return 0;
}

static int cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar(' ');
        fputs(argv[i], stdout);
    }
    putchar('\n');
    return 0;
}

int main(int argc, char **argv) {
    prog = base(argv[0]);
    if (!strcmp(prog, "ls")) return cmd_ls(argc, argv);
    if (!strcmp(prog, "cat")) return cmd_cat(argc, argv);
    if (!strcmp(prog, "cp")) return cmd_cp(argc, argv);
    if (!strcmp(prog, "mv")) return cmd_mv(argc, argv);
    if (!strcmp(prog, "rm")) return cmd_rm(argc, argv);
    if (!strcmp(prog, "mkdir")) return cmd_mkdir(argc, argv);
    if (!strcmp(prog, "rmdir")) return cmd_rm(argc, argv);
    if (!strcmp(prog, "touch")) return cmd_touch(argc, argv);
    if (!strcmp(prog, "wc")) return cmd_wc(argc, argv);
    if (!strcmp(prog, "head")) return cmd_head(argc, argv);
    if (!strcmp(prog, "tail")) return cmd_tail(argc, argv);
    if (!strcmp(prog, "date")) return cmd_date(argc, argv);
    if (!strcmp(prog, "ps")) return cmd_ps(argc, argv);
    if (!strcmp(prog, "free")) return cmd_free(argc, argv);
    if (!strcmp(prog, "uname")) return cmd_uname(argc, argv);
    if (!strcmp(prog, "sleep")) return cmd_sleep(argc, argv);
    if (!strcmp(prog, "reboot")) return cmd_reboot(argc, argv);
    if (!strcmp(prog, "echo")) return cmd_echo(argc, argv);
    if (!strcmp(prog, "true")) return 0;
    if (!strcmp(prog, "false")) return 1;
    fprintf(stderr, "%s: unknown utility\n", prog);
    return 1;
}
