#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>

int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }
long long atoll(const char *s) { return strtoll(s, NULL, 10); }
double atof(const char *s) { return strtod(s, NULL); }

long strtol(const char *s, char **end, int base) { return (long)strtoll(s, end, base); }
unsigned long strtoul(const char *s, char **end, int base) { return (unsigned long)strtoull(s, end, base); }

long long strtoll(const char *s, char **end, int base) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    unsigned long long v = 0;
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned)base + (unsigned)d;
        s++;
    }
    if (end) *end = (char *)s;
    return neg ? -(long long)v : (long long)v;
}

unsigned long long strtoull(const char *s, char **end, int base) {
    return (unsigned long long)strtoll(s, end, base);
}

double strtod(const char *s, char **end) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;

    double v = 0.0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        double scale = 1.0;
        int seen_dot = 0;
        while (*s) {
            int d;
            if (*s >= '0' && *s <= '9') d = *s - '0';
            else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
            else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
            else if (*s == '.' && !seen_dot) { seen_dot = 1; s++; continue; }
            else break;
            if (seen_dot) { scale *= 0.0625; v += d * scale; }
            else v = v * 16.0 + d;
            s++;
        }
        int e = 0, eneg = 0;
        if (*s == 'p' || *s == 'P') {
            s++;
            if (*s == '-') { eneg = 1; s++; } else if (*s == '+') s++;
            while (*s >= '0' && *s <= '9') { e = e * 10 + (*s - '0'); s++; }
            if (eneg) e = -e;
        }
        v = ldexp(v, e);
    } else {
        while (*s >= '0' && *s <= '9') { v = v * 10.0 + (*s - '0'); s++; }
        if (*s == '.') {
            s++;
            double scale = 0.1;
            while (*s >= '0' && *s <= '9') { v += (*s - '0') * scale; scale *= 0.1; s++; }
        }
        if (*s == 'e' || *s == 'E') {
            const char *save = s;
            s++;
            int eneg = 0;
            if (*s == '-') { eneg = 1; s++; } else if (*s == '+') s++;
            if (*s >= '0' && *s <= '9') {
                int e = 0;
                while (*s >= '0' && *s <= '9') { e = e * 10 + (*s - '0'); s++; }
                if (eneg) e = -e;
                v = ldexp(v, e);
            } else {
                s = save;
            }
        }
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}
float strtof(const char *s, char **e) { return (float)strtod(s, e); }
long double strtold(const char *s, char **e) { return (long double)strtod(s, e); }

char *getenv(const char *name) { (void)name; return NULL; }
int system(const char *cmd) { (void)cmd; return -1; }

static unsigned int rng = 12345;
int rand(void) { rng = rng * 1103515245u + 12345u; return (rng >> 16) & 0x7FFF; }
void srand(unsigned seed) { rng = seed; }

int mkstemp(char *tmpl) {
    size_t n = strlen(tmpl);
    if (n < 6) return -1;
    for (int i = 0; i < 100; i++) {
        int x = rand();
        static const char cs[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        for (int k = 0; k < 6; k++) tmpl[n - 6 + k] = cs[(x >> (k * 5)) % 36];
        int fd = open(tmpl, 0x0040 | 0x0002, 0600);
        if (fd >= 0) return fd;
    }
    return -1;
}
char *mktemp(char *tmpl) {
    int fd = mkstemp(tmpl);
    if (fd >= 0) close(fd);
    return tmpl;
}

static void swap_bytes(void *a, void *b, size_t sz) {
    unsigned char *x = a, *y = b;
    while (sz--) { unsigned char t = *x; *x++ = *y; *y++ = t; }
}

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
    unsigned char *b = base;
    /* simple insertion sort; TCC uses small arrays, correctness first */
    for (size_t i = 1; i < n; i++) {
        size_t j = i;
        while (j > 0 && cmp(b + (j - 1) * sz, b + j * sz) > 0) {
            swap_bytes(b + (j - 1) * sz, b + j * sz, sz);
            j--;
        }
    }
}

void *bsearch(const void *key, const void *base, size_t n, size_t sz,
              int (*cmp)(const void *, const void *)) {
    const unsigned char *b = base;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = cmp(key, b + mid * sz);
        if (c == 0) return (void *)(b + mid * sz);
        if (c < 0) hi = mid; else lo = mid + 1;
    }
    return NULL;
}

int atexit(void (*func)(void)) { (void)func; return 0; }
