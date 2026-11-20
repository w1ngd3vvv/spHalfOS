/* spHalfOS shell: prompt sp&(dir)> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <syscall.h>

#define MAX_LINE 512
#define MAX_ARGS 32
#define HIST_MAX 16

static char history[HIST_MAX][MAX_LINE];
static int hist_count;
static int shell_alive = 1;

static int key_block(void) {
    int c;
    while ((c = sp_getkey()) < 0) syscall0(SYS_YIELD);
    return c;
}

static const char *base_name(const char *path) {
    if (!path || !*path) return "/";
    if (!strcmp(path, "/")) return "root";
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

static void make_prompt(char *buf, int size) {
    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "/");
    snprintf(buf, size, "sp&(%s)> ", base_name(cwd));
}

static void redraw_line(const char *prompt, const char *line, int len) {
    printf("\r\033[K%s%s", prompt, line);
    (void)len;
}

static int read_line(char *buf, int max) {
    char prompt[300];
    make_prompt(prompt, sizeof(prompt));
    printf("%s", prompt);
    int len = 0;
    buf[0] = 0;
    int hist_pos = hist_count;

    for (;;) {
        int c = key_block();
        if (c == '\n' || c == '\r') {
            printf("\n");
            buf[len] = 0;
            return len;
        }
        if (c == 3) {                 /* Ctrl-C */
            printf("^C\n");
            buf[0] = 0;
            return -1;
        }
        if (c == 4) {                 /* Ctrl-D */
            if (len == 0) { printf("\n"); return -2; }
            continue;
        }
        if (c == 8 || c == 127) {
            if (len > 0) {
                len--;
                buf[len] = 0;
                printf("\b \b");
            }
            continue;
        }
        if (c == 21) {                /* Ctrl-U */
            while (len > 0) {
                len--;
                printf("\b \b");
            }
            buf[0] = 0;
            continue;
        }
        if (c == '\t') {
            int spaces = 4 - (len & 3);
            while (spaces-- && len < max - 1) {
                buf[len++] = ' ';
                putchar(' ');
            }
            continue;
        }
        if (c == 27) {                /* ESC [ A/B */
            int c2 = key_block();
            if (c2 == '[') {
                int c3 = key_block();
                if (c3 == 'A' || c3 == 'B') {
                    int target = hist_pos;
                    if (c3 == 'A' && target > 0) target--;
                    if (c3 == 'B' && target < hist_count) target++;
                    if (target != hist_pos) {
                        hist_pos = target;
                        if (hist_pos == hist_count) buf[0] = 0;
                        else strncpy(buf, history[hist_pos], max - 1);
                        buf[max - 1] = 0;
                        len = (int)strlen(buf);
                        redraw_line(prompt, buf, len);
                    }
                }
            }
            continue;
        }
        if (c >= 32 && c < 127) {
            if (len < max - 1) {
                buf[len++] = (char)c;
                buf[len] = 0;
                putchar((char)c);
            }
        }
    }
}

/* split respecting quotes; treats >, >>, < as separate tokens */
static int tokenize(char *line, char *argv[], int max) {
    int argc = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '>' || *p == '<') {
            if (argc >= max) break;
            static char op_gt[] = ">";
            static char op_append[] = ">>";
            static char op_lt[] = "<";
            if (*p == '>' && p[1] == '>') {
                argv[argc++] = op_append;
                p += 2;
            } else {
                argv[argc++] = (*p == '>') ? op_gt : op_lt;
                p++;
            }
            continue;
        }
        if (argc >= max) break;
        char *start = p;
        char *out = p;
        char quote = 0;
        while (*p) {
            if (quote) {
                if (*p == quote) { quote = 0; p++; continue; }
                if (*p == '\\' && quote == '"' && p[1]) p++;
                *out++ = *p++;
                continue;
            }
            if (*p == '"' || *p == '\'') { quote = *p; p++; continue; }
            if (*p == ' ' || *p == '\t') break;
            if (*p == '>' || *p == '<') break;
            if (*p == '\\' && p[1]) p++;
            *out++ = *p++;
        }
        char delim = *p;
        int wasop = (delim == '>' || delim == '<');
        *out = 0;
        argv[argc++] = start;
        if (wasop) continue;
        if (delim) p++;
    }
    argv[argc] = NULL;
    return argc;
}

static int builtin(int argc, char *argv[]) {
    const char *cmd = argv[0];
    if (!strcmp(cmd, "cd")) {
        const char *dir = argc > 1 ? argv[1] : "/";
        if (chdir(dir) < 0) fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
        return 0;
    }
    if (!strcmp(cmd, "pwd")) {
        char cwd[256];
        printf("%s\n", getcwd(cwd, sizeof(cwd)) ? cwd : "/");
        return 0;
    }
    if (!strcmp(cmd, "exit") || !strcmp(cmd, "logout")) {
        shell_alive = 0;
        return 0;
    }
    if (!strcmp(cmd, "clear")) {
        sp_clrscr();
        return 0;
    }
    if (!strcmp(cmd, "echo")) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) putchar(' ');
            fputs(argv[i], stdout);
        }
        putchar('\n');
        return 0;
    }
    if (!strcmp(cmd, "history")) {
        for (int i = 0; i < hist_count; i++)
            printf("%4d  %s\n", i + 1, history[i]);
        return 0;
    }
    if (!strcmp(cmd, "help")) {
        printf("spHalfOS shell commands:\n"
               "  cd pwd exit clear help history echo\n"
               "  ls cat cp mv rm mkdir rmdir touch wc head tail\n"
               "  date ps free uname sleep reboot\n"
               "  edit fire cmatrix badapple tcc\n"
               "Redirection: cmd > file, cmd >> file, cmd < file\n"
               "Prompt format: sp&(directory)> \n");
        return 0;
    }
    return -1;
}

static int resolve_command(const char *cmd, char *out, int size) {
    if (strchr(cmd, '/')) {
        snprintf(out, size, "%s", cmd);
        return 0;
    }
    const char *dirs[] = { "/bin", "/usr/bin", "/" };
    for (int i = 0; i < 3; i++) {
        snprintf(out, size, "%s/%s", dirs[i], cmd);
        if (access(out, 0) == 0) return 0;
    }
    snprintf(out, size, "/bin/%s", cmd);
    return -1;
}

static int run_external(char *argv[], int argc) {
    char path[300];
    if (resolve_command(argv[0], path, sizeof(path)) < 0 ||
        access(path, 0) < 0) {
        fprintf(stderr, "%s: command not found\n", argv[0]);
        return 127;
    }
    int pid = sp_spawn(path, argv, argc);
    if (pid < 0) {
        fprintf(stderr, "%s: cannot execute (%s)\n", argv[0], strerror(errno));
        return 126;
    }
    int st;
    while ((st = sp_wait(pid)) == -2)
        syscall0(SYS_YIELD);
    return st < 0 ? 1 : st;
}

static int execute(char *line) {
    char *argv[MAX_ARGS + 1];
    int argc = tokenize(line, argv, MAX_ARGS);
    if (argc == 0) return 0;

    char *outfile = NULL, *infile = NULL;
    int append = 0;
    char *clean[MAX_ARGS + 1];
    int n = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], ">") || !strcmp(argv[i], ">>")) {
            append = argv[i][1] == '>';
            if (i + 1 < argc) outfile = argv[++i];
            else { fprintf(stderr, "syntax: missing output file\n"); return 1; }
        } else if (!strcmp(argv[i], "<")) {
            if (i + 1 < argc) infile = argv[++i];
            else { fprintf(stderr, "syntax: missing input file\n"); return 1; }
        } else {
            clean[n++] = argv[i];
        }
    }
    clean[n] = NULL;
    argc = n;
    if (!argc) return 0;

    int in_fd = -1, out_fd = -1;
    if (infile) {
        in_fd = open(infile, O_RDONLY);
        if (in_fd < 0) {
            fprintf(stderr, "%s: %s\n", infile, strerror(errno));
            return 1;
        }
        dup2(in_fd, 0);
    }
    if (outfile) {
        out_fd = open(outfile, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
        if (out_fd < 0) {
            fprintf(stderr, "%s: %s\n", outfile, strerror(errno));
            if (in_fd >= 0) { close(0); close(in_fd); }
            return 1;
        }
        dup2(out_fd, 1);
    }

    int r = builtin(argc, clean);
    if (r < 0) r = run_external(clean, argc);

    if (out_fd >= 0) { close(1); close(out_fd); }
    if (in_fd >= 0) { close(0); close(in_fd); }
    return r;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "-c")) {
        execute(argv[2]);
        return 0;
    }
    (void)argc; (void)argv;
    char cwd[256];
    if (!getcwd(cwd, sizeof(cwd)) || strcmp(cwd, "/")) chdir("/");

    printf("\033[1;36m");
    printf("  spHalfOS 0.1.0 -- sp& shell\n");
    printf("  type 'help' for commands, Ctrl-D to exit\n");
    printf("\033[0m\n");

    char line[MAX_LINE];
    while (shell_alive) {
        int r = read_line(line, sizeof(line));
        if (r == -2) break;
        if (r <= 0) continue;
        if (hist_count == 0 || strcmp(history[hist_count - 1], line)) {
            if (hist_count == HIST_MAX) {
                memmove(history[0], history[1], sizeof(history[0]) * (HIST_MAX - 1));
                hist_count--;
            }
            strncpy(history[hist_count++], line, MAX_LINE - 1);
        }
        execute(line);
    }
    printf("bye\n");
    return 0;
}
