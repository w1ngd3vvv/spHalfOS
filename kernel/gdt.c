/* spHalfOS GDT + TSS setup */
#include "gdt.h"
#include "io.h"
#include "kernel.h"
#include "string.h"

typedef struct PACKED {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} gdt_entry_t;

typedef struct PACKED {
    uint16_t limit;
    uint32_t base;
} gdt_ptr_t;

typedef struct PACKED {
    uint32_t prev_tss;
    uint32_t esp0, ss0;
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3;
    uint32_t eip, eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} tss_t;

static gdt_entry_t gdt[6];
static gdt_ptr_t gdt_ptr;
static tss_t tss;

extern void gdt_flush(uint32_t gdt_ptr_addr);
extern void tss_flush(void);

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low  = (uint16_t)(base & 0xFFFF);
    gdt[i].base_mid  = (uint8_t)((base >> 16) & 0xFF);
    gdt[i].base_high = (uint8_t)((base >> 24) & 0xFF);
    gdt[i].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[i].gran = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access = access;
}

void gdt_init(void) {
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = GDT_KERNEL_DS;
    tss.iomap_base = sizeof(tss);

    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF);
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF);
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xCF);
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xCF);
    gdt_set(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00);

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint32_t)&gdt;

    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush();
}

void tss_set_esp0(uint32_t esp0) {
    tss.esp0 = esp0;
}
