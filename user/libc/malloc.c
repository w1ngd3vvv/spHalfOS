#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <syscall.h>

typedef struct block {
    uint32_t size;          /* payload size, excluding header */
    struct block *next;
    uint32_t free;
    uint32_t pad;
} block_t;

#define HDR ((uint32_t)sizeof(block_t))   /* 16 */
static block_t *head;
static uint8_t *heap_end;

static uint32_t align16(uint32_t x) { return (x + 15) & ~15u; }

static int grow(uint32_t need) {
    uint32_t cur = (uint32_t)syscall1(SYS_BRK, 0);
    if (!heap_end) heap_end = (uint8_t *)cur;
    uint32_t add = need + HDR + 65536;
    add = (add + 4095) & ~4095u;
    int nb = syscall1(SYS_BRK, (int)(cur + add));
    if (nb < 0) return -1;
    block_t *b = (block_t *)(uintptr_t)cur;
    b->size = add - HDR;
    b->free = 1;
    b->next = NULL;
    heap_end = (uint8_t *)(uintptr_t)nb;
    if (!head) {
        head = b;
    } else {
        block_t *p = head;
        while (p->next) p = p->next;
        p->next = b;
    }
    return 0;
}

static void split(block_t *b, uint32_t need) {
    if (b->size >= need + HDR + 32) {
        block_t *n = (block_t *)((uint8_t *)b + HDR + need);
        n->size = b->size - need - HDR;
        n->free = 1;
        n->next = b->next;
        b->next = n;
        b->size = need;
    }
}

void *malloc(size_t n) {
    if (!n) n = 1;
    uint32_t need = align16((uint32_t)n);
    for (;;) {
        for (block_t *b = head; b; b = b->next) {
            if (b->free && b->size >= need) {
                split(b, need);
                b->free = 0;
                return (uint8_t *)b + HDR;
            }
        }
        if (grow(need) < 0) return NULL;
    }
}

void free(void *p) {
    if (!p) return;
    block_t *b = (block_t *)((uint8_t *)p - HDR);
    b->free = 1;
    for (block_t *x = head; x; x = x->next) {
        while (x->free && x->next && x->next->free) {
            x->size += HDR + x->next->size;
            x->next = x->next->next;
        }
    }
}

void *calloc(size_t n, size_t sz) {
    if (sz && n > 0xFFFFFFFFu / sz) return NULL;
    size_t total = n * sz;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (!n) { free(p); return NULL; }
    block_t *b = (block_t *)((uint8_t *)p - HDR);
    uint32_t need = align16((uint32_t)n);
    if (b->size >= need) { split(b, need); return p; }
    if (b->next && b->next->free && b->size + HDR + b->next->size >= need) {
        b->size += HDR + b->next->size;
        b->next = b->next->next;
        split(b, need);
        return p;
    }
    void *np = malloc(n);
    if (!np) return NULL;
    memcpy(np, p, b->size);
    free(p);
    return np;
}
