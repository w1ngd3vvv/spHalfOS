#ifndef _TGL_SPHALFOS_H
#define _TGL_SPHALFOS_H

#include <stdint.h>

/* Minimal TinyGL window binding for spHalfOS.
 *
 * Each process owns a single TinyGL context.  The framebuffer is a plain
 * 32-bit XRGB buffer compatible with SYS_WIN_PRESENT, so tgl_swap() is a
 * direct hand-off to the kernel compositor.
 *
 * Typical use:
 *
 *   if (tgl_open(640, 480) < 0) return 1;
 *   glViewport(0, 0, 640, 480);
 *   for (;;) {
 *       ... GL calls ...
 *       tgl_swap();
 *       if (tgl_key() == 27) break;
 *   }
 *   tgl_close();
 */

int  tgl_open(int width, int height);
void tgl_swap(void);
void tgl_close(void);

/* 32-bit XRGB pixels of the current draw buffer (width x height). */
uint32_t *tgl_pixels(void);
int  tgl_width(void);
int  tgl_height(void);

/* Window-local mouse position and button mask (bit 0 = left). */
int  tgl_mouse(int *x, int *y, int *buttons);

/* Blocking-free keyboard read.  Returns -1 when no key is pending. */
int  tgl_key(void);

/* Milliseconds since boot (for animation timing). */
uint32_t tgl_ticks(void);

#endif /* _TGL_SPHALFOS_H */
