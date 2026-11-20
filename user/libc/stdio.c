#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

struct _FILE {
    int fd;
    int eof;
    int err;
    int ungot;
    int has_ungot;
    int writable;
};

static FILE _stdin  = { 0, 0, 0, 0, 0, 0 };
static FILE _stdout = { 1, 0, 0, 0, 0, 1 };
static FILE _stderr = { 2, 0, 0, 0, 0, 1 };
FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

static FILE *new_file(int fd, int wr) {
    FILE *f = malloc(sizeof(FILE));
    if (!f) return NULL;
    f->fd = fd;
    f->eof = f->err = f->has_ungot = 0;
    f->writable = wr;
    return f;
}

FILE *fdopen(int fd, const char *mode) {
    return new_file(fd, mode && mode[0] != 'r');
}

FILE *fopen(const char *path, const char *mode) {
    int flags;
    if (!mode) { errno = EINVAL; return NULL; }
    if (mode[0] == 'r') flags = (strchr(mode, '+') ? O_RDWR : O_RDONLY);
    else if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC | (strchr(mode, '+') ? O_RDWR : 0);
    else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT | O_APPEND | (strchr(mode, '+') ? O_RDWR : 0);
    else { errno = EINVAL; return NULL; }
    int fd = open(path, flags, 0644);
    if (fd < 0) return NULL;
    return new_file(fd, flags != O_RDONLY);
}

FILE *freopen(const char *path, const char *mode, FILE *f) {
    if (!f) return NULL;
    fclose(f);
    FILE *n = fopen(path, mode);
    if (n) { f->fd = n->fd; f->eof = f->err = 0; free(n); }
    return n ? f : NULL;
}

int fclose(FILE *f) {
    if (!f) return EOF;
    int r = 0;
    if (f->fd > 2) r = close(f->fd);
    if (f != stdin && f != stdout && f != stderr) free(f);
    return r < 0 ? EOF : 0;
}

int fflush(FILE *f) { (void)f; return 0; }

size_t fread(void *p, size_t sz, size_t n, FILE *f) {
    if (!f || !sz || !n) return 0;
    size_t total = sz * n, got = 0;
    unsigned char *b = p;
    while (got < total) {
        int r = (int)read(f->fd, b + got, total - got);
        if (r < 0) { f->err = 1; break; }
        if (r == 0) { f->eof = 1; break; }
        got += (size_t)r;
    }
    return got / sz;
}

size_t fwrite(const void *p, size_t sz, size_t n, FILE *f) {
    if (!f || !sz || !n) return 0;
    size_t total = sz * n, put = 0;
    const unsigned char *b = p;
    while (put < total) {
        int r = (int)write(f->fd, b + put, total - put);
        if (r <= 0) { f->err = 1; break; }
        put += (size_t)r;
    }
    return put / sz;
}

int fseek(FILE *f, long off, int whence) {
    if (!f) return -1;
    if (lseek(f->fd, off, whence) < 0) return -1;
    f->eof = 0;
    return 0;
}

long ftell(FILE *f) { return f ? (long)lseek(f->fd, 0, SEEK_CUR) : -1; }
int feof(FILE *f) { return f ? f->eof : 0; }
int ferror(FILE *f) { return f ? f->err : 0; }
void clearerr(FILE *f) { if (f) f->eof = f->err = 0; }
int fileno(FILE *f) { return f ? f->fd : -1; }

int fgetc(FILE *f) {
    if (!f) return EOF;
    if (f->has_ungot) { f->has_ungot = 0; return f->ungot; }
    unsigned char c;
    int r = (int)read(f->fd, &c, 1);
    if (r <= 0) { if (r == 0) f->eof = 1; else f->err = 1; return EOF; }
    return c;
}
int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *f) {
    if (!f || c == EOF) return EOF;
    f->ungot = c;
    f->has_ungot = 1;
    f->eof = 0;
    return c;
}

int fputc(int c, FILE *f) {
    if (!f) return EOF;
    unsigned char b = (unsigned char)c;
    if (write(f->fd, &b, 1) != 1) { f->err = 1; return EOF; }
    return (unsigned char)c;
}
int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }

char *fgets(char *s, int n, FILE *f) {
    if (!s || n <= 0) return NULL;
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    s[i] = 0;
    return s;
}

int fputs(const char *s, FILE *f) {
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? 0 : EOF;
}
int puts(const char *s) {
    if (fputs(s, stdout) == EOF) return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

/* ---------------- formatted output ---------------- */

typedef struct {
    FILE *file;
    char *buf;
    size_t cap;
    size_t pos;
    int total;
} out_t;

static void out_ch(out_t *o, char c) {
    if (o->file) {
        fputc(c, o->file);
    } else if (o->pos + 1 < o->cap) {
        o->buf[o->pos++] = c;
    }
    o->total++;
}

static void out_str(out_t *o, const char *s, int len) {
    for (int i = 0; i < len; i++) out_ch(o, s[i]);
}

static void out_pad(out_t *o, char c, int n) {
    while (n-- > 0) out_ch(o, c);
}

static const char digits_lower[] = "0123456789abcdef";
static const char digits_upper[] = "0123456789ABCDEF";

static void out_num(out_t *o, unsigned long long v, int base, int upper,
                    int width, int zero, int left, int plus, int space,
                    int neg, const char *prefix, int prec) {
    char tmp[70];
    int n = 0;
    const char *dig = upper ? digits_upper : digits_lower;

    if (prec == 0 && v == 0) {
        n = 0;
    } else if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v) { tmp[n++] = dig[v % (unsigned)base]; v /= (unsigned)base; }
    }

    if (prec >= 0) zero = 0;
    int plen = (int)strlen(prefix);
    int signlen = (neg || plus || space) ? 1 : 0;
    int zpad = (prec > n) ? (prec - n) : 0;
    int pad = width - plen - signlen - n - zpad;
    if (pad < 0) pad = 0;

    if (left) {
        if (neg) out_ch(o, '-');
        else if (plus) out_ch(o, '+');
        else if (space) out_ch(o, ' ');
        out_str(o, prefix, plen);
        out_pad(o, '0', zpad);
        while (n > 0) out_ch(o, tmp[--n]);
        out_pad(o, ' ', pad);
    } else if (zero) {
        if (neg) out_ch(o, '-');
        else if (plus) out_ch(o, '+');
        else if (space) out_ch(o, ' ');
        out_str(o, prefix, plen);
        out_pad(o, '0', pad);
        out_pad(o, '0', zpad);
        while (n > 0) out_ch(o, tmp[--n]);
    } else {
        out_pad(o, ' ', pad);
        if (neg) out_ch(o, '-');
        else if (plus) out_ch(o, '+');
        else if (space) out_ch(o, ' ');
        out_str(o, prefix, plen);
        out_pad(o, '0', zpad);
        while (n > 0) out_ch(o, tmp[--n]);
    }
}

static void out_double(out_t *o, double v, int width, int prec, int left, int zero, int plus, int space) {
    if (prec < 0) prec = 6;
    if (prec > 18) prec = 18;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    unsigned long long ip = (unsigned long long)v;
    double frac = v - (double)ip;
    unsigned long long fp = 0;
    for (int i = 0; i < prec; i++) {
        frac *= 10.0;
        fp = fp * 10 + (unsigned long long)frac;
        frac -= (double)(unsigned long long)frac;
    }
    char ib[32], fb[32];
    int il = 0, fl = 0;
    if (ip == 0) ib[il++] = '0';
    while (ip) { ib[il++] = (char)('0' + ip % 10); ip /= 10; }
    for (int i = 0; i < prec; i++) { fb[fl++] = (char)('0' + fp % 10); fp /= 10; }
    int total = il + (prec ? prec + 1 : 0) + (neg || plus || space);
    int pad = width - total;
    if (!left && !zero) out_pad(o, ' ', pad);
    if (neg) out_ch(o, '-');
    else if (plus) out_ch(o, '+');
    else if (space) out_ch(o, ' ');
    if (!left && zero) out_pad(o, '0', pad);
    while (il > 0) out_ch(o, ib[--il]);
    if (prec) {
        out_ch(o, '.');
        while (fl > 0) out_ch(o, fb[--fl]);
    }
    if (left) out_pad(o, ' ', pad);
}

static void format(out_t *o, const char *fmt, va_list ap) {
    while (*fmt) {
        if (*fmt != '%') { out_ch(o, *fmt++); continue; }
        fmt++;
        if (!*fmt) break;
        if (*fmt == '%') { out_ch(o, '%'); fmt++; continue; }

        int left = 0, zero = 0, plus = 0, space = 0, alt = 0;
        for (;;) {
            if (*fmt == '-') { left = 1; fmt++; }
            else if (*fmt == '0') { zero = 1; fmt++; }
            else if (*fmt == '+') { plus = 1; fmt++; }
            else if (*fmt == ' ') { space = 1; fmt++; }
            else if (*fmt == '#') { alt = 1; fmt++; }
            else break;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }
        int len = 0; /* 0 int, 1 long, 2 long long, 3 short, 4 char, 5 size_t */
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { len = 2; fmt++; }
            else len = 1;
        } else if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') { len = 4; fmt++; }
            else len = 3;
        } else if (*fmt == 'z' || *fmt == 't') {
            len = 5; fmt++;
        }

        char conv = *fmt ? *fmt++ : 0;
        switch (conv) {
        case 'd': case 'i': {
            long long v;
            if (len == 2) v = va_arg(ap, long long);
            else if (len == 1) v = va_arg(ap, long);
            else v = va_arg(ap, int);
            int neg = v < 0;
            unsigned long long uv = neg ? (unsigned long long)(-(v + 1)) + 1 : (unsigned long long)v;
            out_num(o, uv, 10, 0, width, zero, left, plus, space, neg, "", prec);
            break;
        }
        case 'u': case 'o': case 'x': case 'X': {
            unsigned long long v;
            if (len == 2) v = va_arg(ap, unsigned long long);
            else if (len == 1) v = va_arg(ap, unsigned long);
            else if (len == 5) v = va_arg(ap, size_t);
            else v = va_arg(ap, unsigned int);
            int base = conv == 'o' ? 8 : (conv == 'u' ? 10 : 16);
            const char *pfx = "";
            if (alt && conv == 'x') pfx = "0x";
            else if (alt && conv == 'X') pfx = "0X";
            else if (alt && conv == 'o') pfx = "0";
            out_num(o, v, base, conv == 'X', width, zero, left, 0, 0, 0, pfx, prec);
            break;
        }
        case 'p': {
            uintptr_t v = (uintptr_t)va_arg(ap, void *);
            out_num(o, v, 16, 0, width, zero, left, 0, 0, 0, "0x", prec);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (!left) out_pad(o, ' ', width - 1);
            out_ch(o, c);
            if (left) out_pad(o, ' ', width - 1);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int n = (int)strlen(s);
            if (prec >= 0 && n > prec) n = prec;
            if (!left) out_pad(o, ' ', width - n);
            out_str(o, s, n);
            if (left) out_pad(o, ' ', width - n);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double v = va_arg(ap, double);
            out_double(o, v, width, prec, left, zero, plus, space);
            break;
        }
        case 'n': {
            int *ip = va_arg(ap, int *);
            if (ip) *ip = o->total;
            break;
        }
        default:
            out_ch(o, '%');
            if (conv) out_ch(o, conv);
            break;
        }
    }
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    out_t o = { f, NULL, 0, 0, 0 };
    format(&o, fmt, ap);
    return o.total;
}
int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }
int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}
int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    out_t o = { NULL, buf, n, 0, 0 };
    format(&o, fmt, ap);
    if (n) {
        size_t end = o.pos < n - 1 ? o.pos : n - 1;
        buf[end] = 0;
    }
    return o.total;
}
int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}
int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
}
int sprintf(char *buf, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
    va_end(ap);
    return r;
}

int vsscanf(const char *s, const char *fmt, va_list ap) {
    int assigned = 0;
    while (*fmt) {
        if (*fmt == ' ' || *fmt == '\t' || *fmt == '\n') {
            while (*fmt == ' ' || *fmt == '\t' || *fmt == '\n') fmt++;
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
            continue;
        }
        if (*fmt != '%') {
            if (*s != *fmt) break;
            s++; fmt++;
            continue;
        }
        fmt++;
        if (*fmt == '%') {
            if (*s != '%') break;
            s++; fmt++;
            continue;
        }
        int suppress = 0;
        if (*fmt == '*') { suppress = 1; fmt++; }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int lng = 0;
        if (*fmt == 'l') { lng = 1; fmt++; if (*fmt == 'l') { lng = 2; fmt++; } }
        else if (*fmt == 'h') { lng = -1; fmt++; if (*fmt == 'h') lng = -2; fmt++; }

        char conv = *fmt ? *fmt++ : 0;
        if (conv != 'c' && conv != '[' && conv != 'n')
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;

        if (conv == 'n') {
            if (!suppress) { int *ip = va_arg(ap, int *); *ip = 0; }
            continue;
        }
        if (!*s && conv != 'c') break;

        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
            char *end;
            int base = conv == 'o' ? 8 : (conv == 'x' || conv == 'X') ? 16 : 10;
            long long v = (conv == 'd' || conv == 'i') ? strtoll(s, &end, base)
                                                       : (long long)strtoull(s, &end, base);
            if (end == s) break;
            s = end;
            if (!suppress) {
                if (lng == 2) *va_arg(ap, long long *) = v;
                else if (lng == 1) *va_arg(ap, long *) = (long)v;
                else if (lng == -2) *va_arg(ap, signed char *) = (signed char)v;
                else if (lng == -1) *va_arg(ap, short *) = (short)v;
                else *va_arg(ap, int *) = (int)v;
                assigned++;
            }
        } else if (conv == 'f' || conv == 'e' || conv == 'g') {
            char *end;
            double v = strtod(s, &end);
            if (end == s) break;
            s = end;
            if (!suppress) {
                if (lng == 1) *va_arg(ap, double *) = v;
                else *va_arg(ap, float *) = (float)v;
                assigned++;
            }
        } else if (conv == 's') {
            char *dst = suppress ? NULL : va_arg(ap, char *);
            int n = 0;
            while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' &&
                   (!width || n < width)) {
                if (dst) dst[n] = *s;
                n++; s++;
            }
            if (dst) dst[n] = 0;
            if (!n) break;
            if (!suppress) assigned++;
        } else if (conv == 'c') {
            int n = width ? width : 1;
            char *dst = suppress ? NULL : va_arg(ap, char *);
            for (int i = 0; i < n; i++) {
                if (!*s) return assigned;
                if (dst) dst[i] = *s;
                s++;
            }
            if (!suppress) assigned++;
        } else {
            break;
        }
    }
    return assigned;
}

int sscanf(const char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}

void perror(const char *s) {
    if (s && *s) fprintf(stderr, "%s: %s\n", s, strerror(errno));
    else fprintf(stderr, "%s\n", strerror(errno));
}


ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) return -1;
    if (!*lineptr || *n == 0) { *n = 128; *lineptr = malloc(*n); if (!*lineptr) return -1; }
    size_t len = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        if (len + 2 >= *n) {
            size_t nn = *n * 2;
            char *nb = realloc(*lineptr, nn);
            if (!nb) return -1;
            *lineptr = nb; *n = nn;
        }
        (*lineptr)[len++] = (char)c;
        if (c == '\n') break;
    }
    if (len == 0) return -1;
    (*lineptr)[len] = 0;
    return (ssize_t)len;
}
