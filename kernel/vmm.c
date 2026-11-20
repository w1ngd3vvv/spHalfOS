/* spHalfOS virtual memory: 4 MiB kernel pages, 4 KiB user pages */
#include "mm.h"
#include "io.h"
#include "kernel.h"
#include "string.h"

#define PDE_PRESENT 0x1
#define PDE_RW      0x2
#define PDE_USER    0x4
#define PDE_PS      0x80
#define PTE_PRESENT 0x1
#define PTE_RW      0x2
#define PTE_USER    0x4

static uint32_t kernel_pd[1024] __attribute__((aligned(4096)));

static inline void invlpg(uint32_t vaddr) {
    __asm__ volatile ("invlpg (%0)" :: "r"(vaddr) : "memory");
}

void vmm_init(void) {
    memset(kernel_pd, 0, sizeof(kernel_pd));

    /* identity map low memory for kernel code/data/stack/BSS/framebuffer backbuffers */
    for (uint32_t i = 0; i < KERNEL_LOW_MAP / 0x400000u; i++)
        kernel_pd[i] = (i * 0x400000u) | PDE_PRESENT | PDE_RW | PDE_PS;

    /* map all physical memory (1 GiB) at 0xC0000000 */
    for (uint32_t i = 0; i < 256; i++)
        kernel_pd[768 + i] = (i * 0x400000u) | PDE_PRESENT | PDE_RW | PDE_PS;

    uint32_t cr4 = read_cr4();
    cr4 |= 0x10u;          /* PSE: allow 4 MiB pages */
    write_cr4(cr4);

    write_cr3((uint32_t)kernel_pd);
    uint32_t cr0 = read_cr0();
    cr0 |= 0x80000000u;   /* PG */
    write_cr0(cr0);
}

uint32_t vmm_create_user_pd(void) {
    uint32_t frame = pmm_alloc_frame();
    if (!frame) return 0;
    uint32_t *pd = (uint32_t *)phys_to_virt(frame);
    memset(pd, 0, PAGE_SIZE);
    /* kernel mappings: low identity + physical window, supervisor only */
    uint32_t low_pdes = KERNEL_LOW_MAP / 0x400000u;
    for (uint32_t i = 0; i < low_pdes && i < 512; i++) pd[i] = kernel_pd[i];
    /* Share kernel high mappings: framebuffer window (0x80000000+, PDE 512+)
       and physical memory window (0xC0000000+, PDE 768+). */
    for (int i = 512; i < 1024; i++) pd[i] = kernel_pd[i];
    return frame;
}

void vmm_destroy_user_pd(uint32_t pd_phys) {
    if (!pd_phys) return;
    uint32_t *pd = (uint32_t *)phys_to_virt(pd_phys);
    for (int i = 4; i < 768; i++) {
        if (!(pd[i] & PDE_PRESENT) || !(pd[i] & PDE_USER)) continue;
        if (pd[i] & PDE_PS) continue;
        uint32_t *pt = (uint32_t *)phys_to_virt(pd[i] & ~0xFFFu);
        for (int j = 0; j < 1024; j++) {
            if (pt[j] & PTE_PRESENT) pmm_free_frame(pt[j] & ~0xFFFu);
        }
        pmm_free_frame(pd[i] & ~0xFFFu);
        pd[i] = 0;
    }
    pmm_free_frame(pd_phys);
}

int vmm_map_user_page(uint32_t pd_phys, uint32_t vaddr, uint32_t frame, int writable) {
    (void)writable;
    if (vaddr >= USER_VADDR_MAX || vaddr < 0x08000000u) return -1;
    uint32_t *pd = (uint32_t *)phys_to_virt(pd_phys);
    uint32_t pi = vaddr >> 22;
    if (pi < 4 || pi >= 768) return -1;
    uint32_t *pt;
    if (!(pd[pi] & PDE_PRESENT)) {
        uint32_t pt_phys = pmm_alloc_frame();
        if (!pt_phys) return -1;
        pt = (uint32_t *)phys_to_virt(pt_phys);
        memset(pt, 0, PAGE_SIZE);
        pd[pi] = pt_phys | PDE_PRESENT | PDE_RW | PDE_USER;
    } else {
        if (!(pd[pi] & PDE_USER)) return -1;
        pt = (uint32_t *)phys_to_virt(pd[pi] & ~0xFFFu);
    }
    uint32_t pti = (vaddr >> 12) & 0x3FF;
    pt[pti] = (frame & ~0xFFFu) | PTE_PRESENT | PTE_RW | PTE_USER;
    if (read_cr3() == pd_phys) invlpg(vaddr);
    return 0;
}

uint32_t vmm_user_phys(uint32_t pd_phys, uint32_t vaddr) {
    uint32_t *pd = (uint32_t *)phys_to_virt(pd_phys);
    uint32_t pi = vaddr >> 22;
    if (pi >= 1024 || !(pd[pi] & PDE_PRESENT)) return 0;
    if (pd[pi] & PDE_PS)
        return (pd[pi] & ~0x3FFFFFu) + (vaddr & 0x3FFFFFu);
    uint32_t *pt = (uint32_t *)phys_to_virt(pd[pi] & ~0xFFFu);
    uint32_t pti = (vaddr >> 12) & 0x3FF;
    if (!(pt[pti] & PTE_PRESENT)) return 0;
    return (pt[pti] & ~0xFFFu) + (vaddr & 0xFFFu);
}

void vmm_switch(uint32_t pd_phys) {
    if (pd_phys == 0) return;
    write_cr3(pd_phys);
}

uint32_t vmm_current_pd(void) { return read_cr3(); }

int vmm_alloc_user_pages(uint32_t pd_phys, uint32_t vaddr, uint32_t size, int writable) {
    uint32_t start = vaddr & ~(PAGE_SIZE - 1);
    uint32_t end = (vaddr + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint32_t a = start; a < end; a += PAGE_SIZE) {
        if (vmm_user_phys(pd_phys, a)) continue;
        uint32_t f = pmm_alloc_frame();
        if (!f) return -1;
        memset(phys_to_virt(f), 0, PAGE_SIZE);
        if (vmm_map_user_page(pd_phys, a, f, writable) < 0) {
            pmm_free_frame(f);
            return -1;
        }
    }
    return 0;
}

int vmm_user_range_ok(uint32_t vaddr, uint32_t len, int write) {
    (void)write;
    if (len == 0) return 1;
    if (vaddr >= USER_VADDR_MAX || len > USER_VADDR_MAX - vaddr) return 0;
    uint32_t pd_phys = read_cr3();
    uint32_t a = vaddr & ~(PAGE_SIZE - 1);
    uint32_t end = vaddr + len;
    while (a < end) {
        if (!vmm_user_phys(pd_phys, a)) return 0;
        a += PAGE_SIZE;
    }
    return 1;
}

int vmm_map_phys(uint32_t vaddr, uint32_t phys, uint32_t len, int cache_disable) {
    uint32_t *pd = (uint32_t *)phys_to_virt(read_cr3());
    uint32_t start = vaddr & ~(PAGE_SIZE - 1);
    uint32_t end = vaddr + len;
    uint32_t off = 0;
    for (uint32_t a = start; a < end; a += PAGE_SIZE, off += PAGE_SIZE) {
        uint32_t idx = a >> 22;
        uint32_t *pt = 0;
        if (!(pd[idx] & PDE_PRESENT)) {
            uint32_t pt_phys = pmm_alloc_frame();
            if (!pt_phys) return -1;
            pt = (uint32_t *)phys_to_virt(pt_phys);
            memset(pt, 0, PAGE_SIZE);
            pd[idx] = pt_phys | PDE_PRESENT | PDE_RW;  /* 4 KiB pages */
        } else {
            if (pd[idx] & PDE_PS) return -1;           /* don't split a 4 MiB page */
            pt = (uint32_t *)phys_to_virt(pd[idx] & ~0xFFFu);
        }
        uint32_t pti = (a >> 12) & 0x3FF;
        uint32_t flags = PTE_PRESENT | PTE_RW;
        if (cache_disable) flags |= 0x08 | 0x10;       /* PWT | PCD */
        pt[pti] = ((phys + off) & ~0xFFFu) | flags;
        invlpg(a);
    }
    return 0;
}
