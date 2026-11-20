#include <string.h>
#include <errno.h>
#include <stdlib.h>

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *dd = d; const unsigned char *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *dd = d; const unsigned char *ss = s;
    if (dd < ss) { while (n--) *dd++ = *ss++; }
    else if (dd > ss) { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}
void *memset(void *d, int c, size_t n) {
    unsigned char *dd = d;
    while (n--) *dd++ = (unsigned char)c;
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    while (n--) { if (*p == (unsigned char)c) return (void *)p; p++; }
    return NULL;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
size_t strnlen(const char *s, size_t n) { size_t i = 0; while (i < n && s[i]) i++; return i; }
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}
int strcasecmp(const char *a, const char *b) {
    for (;; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y || !x) return x - y;
    }
}
int strncasecmp(const char *a, const char *b, size_t n) {
    while (n--) {
        int x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y || !x) return x - y;
    }
    return 0;
}
char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) ; return r; }
char *strncpy(char *d, const char *s, size_t n) {
    char *r = d;
    while (n && *s) { *d++ = *s++; n--; }
    while (n--) *d++ = 0;
    return r;
}
char *strcat(char *d, const char *s) { char *r = d + strlen(d); while ((*d++ = *s++)) ; return r; }
char *strncat(char *d, const char *s, size_t n) {
    char *r = d + strlen(d);
    while (n-- && *s) *d++ = *s++;
    *d = 0;
    return r;
}
char *strchr(const char *s, int c) {
    for (;; s++) { if (*s == (char)c) return (char *)s; if (!*s) return NULL; }
}
char *strrchr(const char *s, int c) {
    const char *last = NULL;
    for (;; s++) { if (*s == (char)c) last = s; if (!*s) break; }
    return (char *)last;
}
char *strstr(const char *h, const char *n) {
    size_t l = strlen(n);
    if (!l) return (char *)h;
    for (; *h; h++) if (!strncmp(h, n, l)) return (char *)h;
    return NULL;
}
char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
char *strndup(const char *s, size_t n) {
    size_t l = strnlen(s, n);
    char *p = malloc(l + 1);
    if (!p) return NULL;
    memcpy(p, s, l);
    p[l] = 0;
    return p;
}
char *strtok(char *s, const char *d) {
    static char *save;
    if (!s) s = save;
    if (!s) return NULL;
    while (*s && strchr(d, *s)) s++;
    if (!*s) { save = NULL; return NULL; }
    char *r = s;
    while (*s && !strchr(d, *s)) s++;
    if (*s) { *s = 0; save = s + 1; } else save = NULL;
    return r;
}
char *strerror(int e) {
    switch (e) {
    case 0: return "Success";
    case ENOENT: return "No such file or directory";
    case EEXIST: return "File exists";
    case ENOTDIR: return "Not a directory";
    case EISDIR: return "Is a directory";
    case ENOSPC: return "No space left on device";
    case EINVAL: return "Invalid argument";
    case EACCES: return "Permission denied";
    case ENOMEM: return "Out of memory";
    case EAGAIN: return "Try again";
    case ENAMETOOLONG: return "Name too long";
    case ENOTEMPTY: return "Directory not empty";
    case ENOEXEC: return "Exec format error";
    case EBADF: return "Bad file descriptor";
    default: return "Error";
    }
}
