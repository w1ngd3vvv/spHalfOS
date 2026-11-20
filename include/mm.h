#ifndef SP_MM_H
#define SP_MM_H

#include "types.h"

#define PAGE_SIZE 4096
#define PHYS_WINDOW 0xC0000000u
#define KERNEL_LOW_MAP 0x03000000u   /* 48 MiB identity mapped for kernel + large BSS/framebuffers */

#define USER_BASE   0x08048000u
#define USER_STACK_TOP 0xC0000000u
#define USER_STACK_SIZE 0x00010000u
#define USER_MMAP_BASE 0x50000000u
#define USER_VADDR_MAX 0xBFFFFFFFu

typedef struct {
    uint64_t base;
    uint64_t len;
    uint32_t type;
} mem_range_t;

void pmm_init(uint32_t mb_info_addr);
uint32_t pmm_alloc_frame(void);
void pmm_free_frame(uint32_t phys);
uint32_t pmm_free_frames(void);
uint32_t pmm_total_frames(void);
void pmm_reserve_range(uint32_t start, uint32_t end);
int pmm_is_free(uint32_t phys);

void vmm_init(void);
uint32_t vmm_create_user_pd(void);
void vmm_destroy_user_pd(uint32_t pd_phys);
int vmm_map_user_page(uint32_t pd_phys, uint32_t vaddr, uint32_t frame, int writable);
uint32_t vmm_user_phys(uint32_t pd_phys, uint32_t vaddr);
void vmm_switch(uint32_t pd_phys);
uint32_t vmm_current_pd(void);
int vmm_user_range_ok(uint32_t vaddr, uint32_t len, int write);
int vmm_alloc_user_pages(uint32_t pd_phys, uint32_t vaddr, uint32_t size, int writable);

/* map physical memory (frames) at a kernel virtual address */
int vmm_map_phys(uint32_t vaddr, uint32_t phys, uint32_t len, int cache_disable);

static inline void *phys_to_virt(uint32_t p) { return (void *)(PHYS_WINDOW + p); }
static inline uint32_t virt_to_phys(void *v) { return (uint32_t)v - PHYS_WINDOW; }

#endif
