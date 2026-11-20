/* spHalfOS VBE / linear framebuffer console (32bpp preferred) */
#include "vbe.h"
#include "mm.h"
#include "multiboot2.h"
#include "kernel.h"
#include "string.h"

static uint8_t *fb;
static uint32_t fb_pitch;
static uint32_t fb_width, fb_height, fb_bpp;
static uint32_t fb_phys;
static int fb_phys_mapped;
static int vbe_ok;
static int vbe_active;
static uint32_t vbe_saved_attr;

/* colour is 0x00RRGGBB */
static inline void put_pixel(int x, int y, uint32_t color) {
    if (!fb || x < 0 || y < 0 || (uint32_t)x >= fb_width || (uint32_t)y >= fb_height)
        return;
    if (fb_bpp == 32) {
        uint32_t *p = (uint32_t *)(fb + (uint32_t)y * fb_pitch + (uint32_t)x * 4);
        *p = color;
    } else if (fb_bpp == 24) {
        uint8_t *p = fb + (uint32_t)y * fb_pitch + (uint32_t)x * 3;
        p[0] = (uint8_t)(color & 0xFF);
        p[1] = (uint8_t)((color >> 8) & 0xFF);
        p[2] = (uint8_t)((color >> 16) & 0xFF);
    }
}

void vbe_init(uint32_t mb_info) {
    vbe_ok = 0;
    vbe_active = 0;
    fb = NULL;

    mb2_info_t *info = (mb2_info_t *)mb_info;
    uint8_t *p = (uint8_t *)info + sizeof(mb2_info_t);
    uint8_t *end = (uint8_t *)info + info->total_size;
    while (p < end) {
        mb2_tag_t *tag = (mb2_tag_t *)p;
        if (tag->type == MB2_TAG_END || tag->size < 8) break;
        if (tag->type == MB2_TAG_FRAMEBUFFER) {
            mb2_tag_framebuffer_t *ft = (mb2_tag_framebuffer_t *)tag;
            if (ft->framebuffer_addr && ft->framebuffer_width &&
                ft->framebuffer_bpp == 32 && ft->framebuffer_type == 1) {
                fb_phys = (uint32_t)ft->framebuffer_addr;
                fb_pitch = ft->framebuffer_pitch;
                fb_width = ft->framebuffer_width;
                fb_height = ft->framebuffer_height;
                fb_bpp = ft->framebuffer_bpp;
                if (fb_phys != 0) {
                    uint32_t fb_len = fb_pitch * fb_height;
                    uint32_t fbv = 0x80000000u;   /* kernel virtual for FB */
                    if (vmm_map_phys(fbv, fb_phys, fb_len, 1) == 0) {
                        fb = (uint8_t *)fbv;
                        fb_phys_mapped = 1;
                    } else {
                        fb_phys_mapped = 0;
                    }
                }
                if (fb_phys_mapped) vbe_ok = 1;
            }
            break;
        }
        p += (tag->size + 7) & ~7u;
    }
    if (!fb_phys_mapped)
        kprintf("[vbe] no usable linear framebuffer\n");
    else
        kprintf("[vbe] %ux%u pitch=%u at %x fbmap=%p\n", fb_width, fb_height,
                fb_pitch, fb_phys, fb);
}

int vbe_available(void) { return vbe_ok; }
int vbe_enter(void) {
    if (!vbe_ok) return -1;
    vbe_active = 1;
    vbe_clear(0x0A0A14);
    return 0;
}
int vbe_leave(void) { vbe_active = 0; return 0; }
uint32_t vbe_width(void) { return fb_width; }
uint32_t vbe_height(void) { return fb_height; }
uint32_t vbe_pitch(void) { return fb_pitch; }
uint32_t vbe_bpp(void) { return fb_bpp; }
uint32_t vbe_phys(void) { return fb_phys; }
uint8_t *vbe_framebuffer(void) { return fb; }

void vbe_clear(uint32_t color) {
    if (!fb) return;
    if (fb_bpp == 32) {
        uint32_t n = (uint32_t)(fb_pitch / 4) * fb_height;
        uint32_t *p = (uint32_t *)fb;
        for (uint32_t i = 0; i < n; i++) p[i] = color;
    } else {
        for (uint32_t y = 0; y < fb_height; y++)
            for (uint32_t x = 0; x < fb_width; x++) put_pixel((int)x, (int)y, color);
    }
}

void vbe_present(void) { (void)vbe_saved_attr; }
