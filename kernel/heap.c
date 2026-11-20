/* simple bump allocator for kernel objects -- 4 MiB static arena */
#include "kernel.h"
#include "io.h"
#include "string.h"

#define KHEAP_SIZE (4u * 1024u * 1024u)
static uint8_t kheap[KHEAP_SIZE] __attribute__((aligned(16)));
static uint32_t kheap_used;

void *kmalloc(size_t size) {
    uint32_t flags = read_eflags();
    cpu_cli();
    size = (size + 15) & ~15u;
    if (!size) size = 16;
    if (kheap_used + size > KHEAP_SIZE) {
        if (flags & 0x200) cpu_sti();
        panic("kernel heap exhausted (requested %u bytes)", (uint32_t)size);
    }
    void *p = &kheap[kheap_used];
    kheap_used += (uint32_t)size;
    if (flags & 0x200) cpu_sti();
    return p;
}

void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    memset(p, 0, size);
    return p;
}

void *kmalloc_aligned(size_t size, size_t align) {
    uint32_t flags = read_eflags();
    cpu_cli();
    uint32_t base = (uint32_t)&kheap[kheap_used];
    uint32_t aligned = (base + (uint32_t)align - 1) & ~((uint32_t)align - 1);
    uint32_t off = aligned - (uint32_t)&kheap[0];
    size = (size + 15) & ~15u;
    if (off + size > KHEAP_SIZE) {
        if (flags & 0x200) cpu_sti();
        panic("kernel heap exhausted");
    }
    kheap_used = off + (uint32_t)size;
    if (flags & 0x200) cpu_sti();
    return (void *)aligned;
}
