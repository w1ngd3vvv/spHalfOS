/* spHalfOS - x86 port I/O and CPU helpers */
#ifndef SP_IO_H
#define SP_IO_H

#include "types.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t inw(uint16_t port) {
    uint16_t r;
    __asm__ volatile ("inw %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(uint16_t port) {
    uint32_t r;
    __asm__ volatile ("inl %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}

static inline void io_wait(void) { outb(0x80, 0); }
static inline void cpu_cli(void) { __asm__ volatile ("cli" ::: "memory"); }
static inline void cpu_sti(void) { __asm__ volatile ("sti" ::: "memory"); }
static inline void cpu_hlt(void) { __asm__ volatile ("hlt"); }
static inline void cpu_relax(void) { __asm__ volatile ("pause"); }

static inline uint32_t read_cr0(void) { uint32_t v; __asm__ volatile ("mov %%cr0,%0":"=r"(v)); return v; }
static inline void write_cr0(uint32_t v) { __asm__ volatile ("mov %0,%%cr0"::"r"(v):"memory"); }
static inline uint32_t read_cr2(void) { uint32_t v; __asm__ volatile ("mov %%cr2,%0":"=r"(v)); return v; }
static inline uint32_t read_cr4(void) { uint32_t v; __asm__ volatile ("mov %%cr4,%0":"=r"(v)); return v; }
static inline void write_cr4(uint32_t v) { __asm__ volatile ("mov %0,%%cr4"::"r"(v):"memory"); }
static inline uint32_t read_cr3(void) { uint32_t v; __asm__ volatile ("mov %%cr3,%0":"=r"(v)); return v; }
static inline void write_cr3(uint32_t v) { __asm__ volatile ("mov %0,%%cr3"::"r"(v):"memory"); }
static inline uint32_t read_eflags(void) { uint32_t v; __asm__ volatile ("pushf; pop %0":"=r"(v)); return v; }

static inline uint16_t read_cs(void) { uint16_t v; __asm__ volatile ("mov %%cs,%0":"=r"(v)); return v; }
static inline uint16_t read_ds(void) { uint16_t v; __asm__ volatile ("mov %%ds,%0":"=r"(v)); return v; }
static inline uint16_t read_ss(void) { uint16_t v; __asm__ volatile ("mov %%ss,%0":"=r"(v)); return v; }

#endif
