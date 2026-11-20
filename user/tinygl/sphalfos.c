/* TinyGL <-> spHalfOS binding layer */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <syscall.h>
#include <fb.h>

#include "zbuffer.h"
#include "zgl.h"

#include <GL/gl.h>
#include <GL/sphalfos.h>

static ZBuffer *g_zb;
static int g_w, g_h;

int tgl_open(int width, int height) {
    if (g_zb) tgl_close();

    width &= ~3;                 /* TinyGL requires a multiple of 4 */
    if (width < 16) width = 16;
    if (height < 16) height = 16;
    if (width > 960) width = 960;
    if (height > 540) height = 540;

    g_zb = ZB_open(width, height, ZB_MODE_RGBA, 0, NULL, NULL, NULL);
    if (!g_zb) return -1;

    g_w = width;
    g_h = height;

    glInit(g_zb);
    glViewport(0, 0, width, height);

    /* sRGB-ish defaults: opaque black clear colour, depth test on. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    return 0;
}

void tgl_swap(void) {
    if (!g_zb) return;
    sp_win_present(g_zb->pbuf, g_w, g_h);
}

void tgl_close(void) {
    if (!g_zb) return;
    glClose();
    ZB_close(g_zb);
    g_zb = NULL;
    g_w = g_h = 0;
}

uint32_t *tgl_pixels(void) {
    return g_zb ? (uint32_t *)g_zb->pbuf : NULL;
}

int tgl_width(void)  { return g_w; }
int tgl_height(void) { return g_h; }

int tgl_mouse(int *x, int *y, int *buttons) {
    sp_mouse_t m;
    sp_wininfo_t wi;
    if (mouse_read(&m) < 0) return -1;
    if (sp_win_info(&wi) < 0) return -1;
    if (x) *x = m.x - wi.x;
    if (y) *y = m.y - wi.y;
    if (buttons) *buttons = m.buttons;
    return 0;
}

int tgl_key(void) {
    return sp_getkey();
}

uint32_t tgl_ticks(void) {
    return (uint32_t)sp_ticks();
}
