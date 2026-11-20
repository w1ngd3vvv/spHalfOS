#ifndef SP_VBE_H
#define SP_VBE_H

#include "types.h"

void vbe_init(uint32_t mb_info);
int vbe_available(void);
int vbe_enter(void);
int vbe_leave(void);

uint32_t vbe_width(void);
uint32_t vbe_height(void);
uint32_t vbe_pitch(void);
uint32_t vbe_bpp(void);
uint32_t vbe_phys(void);
uint8_t *vbe_framebuffer(void);
void vbe_clear(uint32_t color);
void vbe_present(void);

#endif
