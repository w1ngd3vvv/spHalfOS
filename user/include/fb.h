#ifndef _SP_FB_H
#define _SP_FB_H

#include <stdint.h>

#define FB_BASE 0x40000000u

typedef struct {
    int      available;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t pixels;
    uint32_t base;
} sp_fbinfo_t;

typedef struct {
    int x, y;
    int dx, dy;
    int buttons;
} sp_mouse_t;

int fb_info(sp_fbinfo_t *info);
void *fb_map(void);
int mouse_read(sp_mouse_t *m);

#endif
