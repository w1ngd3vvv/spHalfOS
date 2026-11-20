/* spHalfOS physical memory manager: bitmap frame allocator */
#include "mm.h"
#include "multiboot2.h"
#include "kernel.h"
#include "string.h"

#define MAX_PHYS 0x40000000u                 /* 1 GiB */
#define MAX_FRAMES (MAX_PHYS / PAGE_SIZE)
static uint32_t bitmap[MAX_FRAMES / 32];
static uint32_t total_frames;
static uint32_t free_frames;
static uint32_t alloc_hint;

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

static inline int bit_test(uint32_t f) {
    return (bitmap[f >> 5] >> (f & 31)) & 1;
}
static inline void bit_set(uint32_t f) {
    bitmap[f >> 5] |= (1u << (f & 31));
}
static inline void bit_clear(uint32_t f) {
    bitmap[f >> 5] &= ~(1u << (f & 31));
}

static void mark_used(uint32_t phys) {
    uint32_t f = phys / PAGE_SIZE;
    if (f < MAX_FRAMES && !bit_test(f)) { bit_set(f); if (free_frames) free_frames--; }
}
static void mark_free(uint32_t phys) {
    uint32_t f = phys / PAGE_SIZE;
    if (f < MAX_FRAMES && bit_test(f)) { bit_clear(f); free_frames++; }
}

void pmm_reserve_range(uint32_t start, uint32_t end) {
    start &= ~(PAGE_SIZE - 1);
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint32_t p = start; p < end && p < MAX_PHYS; p += PAGE_SIZE)
        mark_used(p);
}

static void reserve_from_mb(uint32_t addr) {
    mb2_info_t *info = (mb2_info_t *)addr;
    uint8_t *p = (uint8_t *)info + sizeof(mb2_info_t);
    uint8_t *end = (uint8_t *)info + info->total_size;
    pmm_reserve_range(addr, addr + info->total_size);
    while (p < end) {
        mb2_tag_t *tag = (mb2_tag_t *)p;
        if (tag->type == MB2_TAG_END || tag->size < 8) break;
        if (tag->type == MB2_TAG_MODULE) {
            mb2_tag_module_t *m = (mb2_tag_module_t *)tag;
            pmm_reserve_range(m->mod_start, m->mod_end);
        }
        p += (tag->size + 7) & ~7u;
    }
}

void pmm_init(uint32_t mb_info_addr) {
    for (uint32_t i = 0; i < MAX_FRAMES / 32; i++) bitmap[i] = 0xFFFFFFFFu;
    free_frames = 0;
    alloc_hint = 0;

    mb2_info_t *info = (mb2_info_t *)mb_info_addr;
    if (!mb_info_addr || info->total_size < 16)
        panic("invalid multiboot info");

    uint8_t *p = (uint8_t *)info + sizeof(mb2_info_t);
    uint8_t *end = (uint8_t *)info + info->total_size;
    uint32_t highest = 0;

    while (p < end) {
        mb2_tag_t *tag = (mb2_tag_t *)p;
        if (tag->type == MB2_TAG_END || tag->size < 8) break;
        if (tag->type == MB2_TAG_MMAP) {
            mb2_tag_mmap_t *mm = (mb2_tag_mmap_t *)tag;
            uint8_t *e = (uint8_t *)mm + sizeof(mb2_tag_mmap_t);
            uint8_t *eend = (uint8_t *)mm + mm->size;
            while (e < eend) {
                mb2_mmap_entry_t *me = (mb2_mmap_entry_t *)e;
                if (me->type == MB2_MEM_AVAILABLE) {
                    uint64_t b = me->addr;
                    uint64_t l = me->len;
                    if (b < MAX_PHYS) {
                        uint64_t e2 = b + l;
                        if (e2 > MAX_PHYS) e2 = MAX_PHYS;
                        if (e2 > highest) highest = (uint32_t)e2;
                        uint32_t sb = (uint32_t)((b + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1));
                        uint32_t se = (uint32_t)(e2 & ~(uint64_t)(PAGE_SIZE - 1));
                        for (uint32_t a = sb; a + PAGE_SIZE <= se; a += PAGE_SIZE)
                            mark_free(a);
                    }
                }
                e += mm->entry_size ? mm->entry_size : sizeof(mb2_mmap_entry_t);
            }
        }
        p += (tag->size + 7) & ~7u;
    }

    total_frames = highest / PAGE_SIZE;
    if (total_frames > MAX_FRAMES) total_frames = MAX_FRAMES;

    /* low memory, kernel image, multiboot structures */
    pmm_reserve_range(0, 0x100000);
    pmm_reserve_range((uint32_t)_kernel_start, (uint32_t)_kernel_end);
    pmm_reserve_range(mb_info_addr, mb_info_addr + info->total_size);
    reserve_from_mb(mb_info_addr);
}

uint32_t pmm_alloc_frame(void) {
    for (uint32_t f = alloc_hint; f < total_frames; f++) {
        if (!bit_test(f)) {
            bit_set(f);
            free_frames--;
            alloc_hint = f + 1;
            return f * PAGE_SIZE;
        }
    }
    for (uint32_t f = 0; f < alloc_hint && f < total_frames; f++) {
        if (!bit_test(f)) {
            bit_set(f);
            free_frames--;
            alloc_hint = f + 1;
            return f * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_frame(uint32_t phys) {
    if (phys == 0 || (phys & (PAGE_SIZE - 1))) return;
    mark_free(phys);
}

uint32_t pmm_free_frames(void) { return free_frames; }
uint32_t pmm_total_frames(void) { return total_frames; }
int pmm_is_free(uint32_t phys) {
    uint32_t f = phys / PAGE_SIZE;
    return f < MAX_FRAMES && !bit_test(f);
}
