/* spHalfOS console: VGA text shadow + Multiboot2 linear-framebuffer output */
#include "console.h"
#include "vbe.h"
#include "io.h"
#include "font16x32.h"
#include "spfs.h"
#include "string.h"
#include "process.h"
#include "kernel.h"

#define VGA_MEM ((volatile uint16_t *)0xB8000)

#define FB_CELL_W 8
#define FB_CELL_H 18
#define TASKBAR_H 34
#define TERM_BORDER 2
#define TERM_TITLE_H 26
#define TERM_BTN_W 20
#define TERM_BTN_H 18
#define UI_SHADOW_PAD 8

/* Windows 95 palette */
#define W95_FACE    0xC0C0C0u
#define W95_LIGHT   0xFFFFFFu
#define W95_SHADOW  0x808080u
#define W95_DARK    0x000000u
#define W95_NAVY    0x000080u
#define W95_INACTIVE 0x808080u
#define W95_TEXT    0x000000u
#define W95_TEXT_DIS 0x808080u

#define WP_W 1920
#define WP_H 1080

#define MAX_TERMS 8
#define TERM_INBUF_SIZE 1024

#define MCUR_W 12
#define MCUR_H 18

typedef struct menu_item {
    const char *label;
    const char *cmd;
    const char *path;
} menu_item_t;

static const menu_item_t start_menu_items[] = {
    { "Terminal",  NULL,            NULL },
    { "Files",     "/bin/files",    "/bin/files" },
    { "Editor",    "/bin/edit",     "/bin/edit" },
    { "Doom",      "/bin/doom",     "/bin/doom" },
    { "Bad Apple", "/bin/badapple", "/bin/badapple" },
    { "3D Gears",  "/bin/glgears",  "/bin/glgears" },
    { "Internet",  "/bin/ie",       "/bin/ie" },
    { "Fire",      "/bin/fire",     "/bin/fire" },
    { "Matrix",    "/bin/cmatrix",  "/bin/cmatrix" },
    { "Clear",     "clear\n",       NULL },
};
#define START_MENU_COUNT ((int)(sizeof(start_menu_items) / sizeof(start_menu_items[0])))

#define MENU_W 220
#define MENU_TITLE_H 28
#define MENU_ITEM_H 28
#define MENU_PAD 6
#define MENU_H (MENU_TITLE_H + START_MENU_COUNT * MENU_ITEM_H + MENU_PAD)
#define MENU_OUTER_W (MENU_W + UI_SHADOW_PAD)
#define MENU_OUTER_H (MENU_H + UI_SHADOW_PAD)

typedef struct term {
    int used;
    int id;
    char title[32];
    int owner_pid;
    int win_present_pid;

    uint16_t cells[CON_MAX_COLS * CON_MAX_ROWS];
    int cols, rows;
    int cx, cy;
    uint8_t cur_attr;
    int cursor_on;
    int saved_x, saved_y;
    int esc_state;
    char esc_buf[40];
    int esc_len;

    volatile uint8_t inbuf[TERM_INBUF_SIZE];
    volatile int in_head, in_tail;

    int sel_active, sel_drag;
    int sel_anchor, sel_end;

    int win_x, win_y;
    int win_open;
    int win_minimized;
    int win_maximized;
    int win_restore_x, win_restore_y;
    int win_restore_cols, win_restore_rows;
    int win_dragging;
    int win_drag_dx, win_drag_dy;

    int fb_cur_x, fb_cur_y;
    int present_suppress;
    int dirty;
    int dirty_x0, dirty_y0, dirty_x1, dirty_y1;

    int gui_mode;
    int gui_w, gui_h;
    int gui_slot;
    uint32_t *gui_buf;
} term_t;

/* fixed GUI framebuffer pool (no heap use: repeated app open/close must not leak) */
#define GUI_MAX_W 960
#define GUI_MAX_H 540
#define GUI_SLOTS 3
static uint32_t gui_pool[GUI_SLOTS][GUI_MAX_W * GUI_MAX_H];
static int gui_slot_used[GUI_SLOTS];

static uint32_t *gui_slot_get(term_t *t) {
    if (t->gui_slot >= 0 && t->gui_slot < GUI_SLOTS) return gui_pool[t->gui_slot];
    for (int i = 0; i < GUI_SLOTS; i++) {
        if (!gui_slot_used[i]) {
            gui_slot_used[i] = 1;
            t->gui_slot = i;
            return gui_pool[i];
        }
    }
    return 0;
}

static void gui_slot_put(term_t *t) {
    if (t->gui_slot >= 0 && t->gui_slot < GUI_SLOTS) gui_slot_used[t->gui_slot] = 0;
    t->gui_slot = -1;
    t->gui_buf = 0;
}

static term_t terms[MAX_TERMS];
static term_t *focused_term;
static int next_term_id = 1;

/* legacy VGA text shadow */
static int fbcon_on;

/* framebuffer/backbuffer */
static uint32_t *bbuf;
static uint32_t backbuf[WP_W * WP_H];

/* mouse cursor */
static int mcur_x, mcur_y;
static int mcur_saved;
static uint32_t mcur_buf[MCUR_W * MCUR_H];

/* menu */
static int start_menu_open;
static int start_menu_x = 6;
static int start_menu_y = 300;
static int start_menu_dragging;
static int start_menu_drag_dx, start_menu_drag_dy;
static uint32_t menu_bg[MENU_OUTER_W * MENU_OUTER_H];
static int menu_bg_valid;
static int desk_sel = -1;

/* taskbar */
static int start_btn_x = 8, start_btn_y = 4, start_btn_w = 110, start_btn_h = 30;
#define TASKBTN_W 150
#define TASKBTN_H 28
#define TASKBTN_GAP 6

/* pending spawn from menu (deferred from mouse IRQ) */
static volatile int pending_spawn;
static char pending_spawn_path[128];
static char pending_spawn_arg[128];

static uint32_t fb_palette[16] = {
    0x0A0A14, 0xE84848, 0x50D870, 0xF0D060,
    0x5898F0, 0xD068D8, 0x58D0D0, 0xD8D8D8,
    0x505060, 0xFF7070, 0x78F090, 0xFFF088,
    0x80B8FF, 0xF090F8, 0x80F0F0, 0xFFFFFF
};

static inline uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static inline void bbuf_put(int x, int y, uint32_t color) {
    if (!bbuf) return;
    if (x < 0 || y < 0 || (uint32_t)x >= vbe_width() || (uint32_t)y >= vbe_height()) return;
    bbuf[(uint32_t)y * vbe_width() + (uint32_t)x] = color;
}

static inline void copy32(uint32_t *dst, const uint32_t *src, int n) {
    uint32_t c = (uint32_t)n;
    __asm__ volatile ("cld; rep movsl" : "+D"(dst), "+S"(src), "+c"(c) : : "memory");
}

static inline void move32(uint32_t *dst, const uint32_t *src, int n) {
    if (n <= 0) return;
    if (dst < src || dst >= src + n) {
        copy32(dst, src, n);
    } else {
        dst += n; src += n;
        uint32_t c = (uint32_t)n;
        __asm__ volatile ("std; rep movsl; cld" : "+D"(dst), "+S"(src), "+c"(c) : : "memory");
    }
}

static inline void store32(uint32_t *dst, uint32_t val, int n) {
    uint32_t c = (uint32_t)n;
    __asm__ volatile ("cld; rep stosl" : "+D"(dst), "+c"(c) : "a"(val) : "memory");
}

static void bbuf_move_rect(int dx, int dy, int sx, int sy, int w, int h) {
    if (!bbuf || w <= 0 || h <= 0) return;
    int stride = (int)vbe_width();
    if (dy < sy) {
        for (int y = 0; y < h; y++)
            move32(&bbuf[(dy + y) * stride + dx], &bbuf[(sy + y) * stride + sx], w);
    } else {
        for (int y = h - 1; y >= 0; y--)
            move32(&bbuf[(dy + y) * stride + dx], &bbuf[(sy + y) * stride + sx], w);
    }
}

static void fb_present_rect(int rx, int ry, int rw, int rh) {
    if (!bbuf || !vbe_framebuffer()) return;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > sw) rw = sw - rx;
    if (ry + rh > sh) rh = sh - ry;
    if (rw <= 0 || rh <= 0) return;
    uint32_t *fb = (uint32_t *)vbe_framebuffer();
    int pitch32 = (int)vbe_pitch() / 4;
    for (int y = ry; y < ry + rh; y++) {
        copy32(&fb[y * pitch32 + rx], &bbuf[y * sw + rx], rw);
    }
}

static void fb_present(void) {
    if (!bbuf || !vbe_framebuffer()) return;
    fb_present_rect(0, 0, (int)vbe_width(), (int)vbe_height());
}

static void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!bbuf) return;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; yy++)
        store32(&bbuf[yy * sw + x], color, w);
}

static int rrect_inset(int row, int h, int r) {
    int dy;
    if (row < r) dy = r - 1 - row;
    else if (row >= h - r) dy = row - (h - r);
    else return 0;
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
    return r - dx;
}

static void fb_fill_rrect(int x, int y, int w, int h, int r, uint32_t color) {
    if (!bbuf || w <= 0 || h <= 0) return;
    if (r <= 0) { fb_fill_rect(x, y, w, h, color); return; }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    for (int yy = 0; yy < h; yy++) {
        int sy = y + yy;
        if (sy < 0 || sy >= sh) continue;
        int inset = rrect_inset(yy, h, r);
        int x0 = x + inset, x1 = x + w - inset;
        if (x0 < 0) x0 = 0;
        if (x1 > sw) x1 = sw;
        if (x1 > x0) store32(&bbuf[sy * sw + x0], color, x1 - x0);
    }
}

static void fb_rrect_border(int x, int y, int w, int h, int r, uint32_t color) {
    if (!bbuf || w <= 0 || h <= 0) return;
    if (r <= 0) {
        fb_fill_rect(x, y, w, 1, color);
        fb_fill_rect(x, y + h - 1, w, 1, color);
        fb_fill_rect(x, y, 1, h, color);
        fb_fill_rect(x + w - 1, y, 1, h, color);
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    for (int yy = 0; yy < h; yy++) {
        int sy = y + yy;
        if (sy < 0 || sy >= sh) continue;
        int inset = rrect_inset(yy, h, r);
        int lx = x + inset, rx = x + w - 1 - inset;
        if (lx >= 0 && lx < sw) bbuf[sy * sw + lx] = color;
        if (rx >= 0 && rx < sw && rx != lx) bbuf[sy * sw + rx] = color;
        if (yy == 0 || yy == h - 1) {
            int x0 = lx + 1, x1 = rx;
            if (x0 < 0) x0 = 0;
            if (x1 > sw) x1 = sw;
            if (x1 > x0) store32(&bbuf[sy * sw + x0], color, x1 - x0);
        }
    }
}

#define FONT_W 8
#define FONT_H 16

static inline uint32_t fb_blend(uint32_t bg, uint32_t fg, uint8_t a) {
    if (a == 0) return bg;
    if (a >= 255) return fg;
    uint32_t inv = 255 - a;
    uint32_t br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    uint32_t fr = (fg >> 16) & 0xFF, fgc = (fg >> 8) & 0xFF, fbv = fg & 0xFF;
    uint32_t r = (fr * a + br * inv + 127) / 255;
    uint32_t g = (fgc * a + bgc * inv + 127) / 255;
    uint32_t b = (fbv * a + bb * inv + 127) / 255;
    return (r << 16) | (g << 8) | b;
}

static void fb_draw_glyph_px(int px, int py, char c, uint32_t fg, uint32_t bg) {
    uint8_t uc = (uint8_t)c;
    if (uc < 32 || uc > 126) uc = '?';
    const uint8_t (*glyph)[FONT_W] = font16x32[uc - 32];
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    int yoff = (FB_CELL_H - FONT_H) / 2;
    for (int row = 0; row < FB_CELL_H; row++) {
        int sy = py + row;
        if (sy < 0 || sy >= sh) continue;
        uint32_t *dst = &bbuf[sy * sw + px];
        int gr = row - yoff;
        for (int col = 0; col < FB_CELL_W; col++) {
            int sx = px + col;
            if (sx < 0 || sx >= sw) continue;
            if (col < FONT_W && gr >= 0 && gr < FONT_H) {
                uint8_t a = glyph[gr][col];
                dst[col] = a ? fb_blend(bg, fg, a) : bg;
            } else {
                dst[col] = bg;
            }
        }
    }
}

static void fb_draw_text_px(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    for (int i = 0; s[i]; i++)
        fb_draw_glyph_px(x + i * FB_CELL_W, y, s[i], fg, bg);
}

static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

static int rects_intersect(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0) return 0;
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

#define WIN_RADIUS 0

/* raised/sunken 3D edge, Windows 95 style */
static void w95_edge(int x, int y, int w, int h, int raised) {
    uint32_t tl = raised ? W95_LIGHT : W95_SHADOW;
    uint32_t br = raised ? W95_DARK : W95_LIGHT;
    fb_fill_rect(x, y, w, 1, tl);
    fb_fill_rect(x, y, 1, h, tl);
    fb_fill_rect(x, y + h - 1, w, 1, br);
    fb_fill_rect(x + w - 1, y, 1, h, br);
}

static void w95_edge2(int x, int y, int w, int h, int raised) {
    /* two-pixel bevel with the classic inner gray step */
    w95_edge(x, y, w, h, raised);
    uint32_t tl = raised ? W95_FACE : W95_SHADOW;
    uint32_t br = raised ? W95_SHADOW : W95_FACE;
    fb_fill_rect(x + 1, y + 1, w - 2, 1, tl);
    fb_fill_rect(x + 1, y + 1, 1, h - 2, tl);
    fb_fill_rect(x + 1, y + h - 2, w - 2, 1, br);
    fb_fill_rect(x + w - 2, y + 1, 1, h - 2, br);
}

static void draw_window_shadow(int x, int y, int w, int h, int r) {
    (void)x; (void)y; (void)w; (void)h; (void)r;
    return;
}

static void draw_window_shadow_old(int x, int y, int w, int h, int r) {
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    uint32_t *fb = bbuf;
    int pad = UI_SHADOW_PAD;
    /* shadow offset slightly down/right */
    int sx0 = x + 4, sy0 = y + 4;
    for (int yy = 0; yy < h + pad; yy++) {
        int py = sy0 + yy;
        if (py < 0 || py >= sh) continue;
        int inset = rrect_inset(yy, h, r);
        int row_x0 = sx0 + inset;
        int row_x1 = sx0 + w - inset;
        if (row_x0 < 0) row_x0 = 0;
        if (row_x1 > sw) row_x1 = sw;
        for (int px = row_x0; px < row_x1; px++) {
            /* only draw outside the main window area to keep it cheap */
            int in_win = (px >= x && px < x + w && py >= y && py < y + h);
            if (in_win) continue;
            int edge_dist = 0;
            int d_bottom = (sy0 + h - 1) - py;
            int d_right = (sx0 + w - 1) - px;
            int d_top = py - sy0;
            int d_left = px - sx0;
            edge_dist = d_bottom; if (d_right < edge_dist) edge_dist = d_right;
            if (d_top < edge_dist) edge_dist = d_top;
            if (d_left < edge_dist) edge_dist = d_left;
            if (edge_dist < 0) edge_dist = 0;
            int alpha = 60 - (pad - 1 - edge_dist) * 10;
            if (alpha < 18) alpha = 18;
            if (alpha > 60) alpha = 60;
            uint32_t c = fb[py * sw + px];
            uint32_t rr = (c >> 16) & 0xFF, gg = (c >> 8) & 0xFF, bb = c & 0xFF;
            rr = rr * (255 - alpha) / 255;
            gg = gg * (255 - alpha) / 255;
            bb = bb * (255 - alpha) / 255;
            fb[py * sw + px] = fb_rgb(rr, gg, bb);
        }
    }
}

#define DESKTOP_COLOR 0x008080u

static void draw_wallpaper_full(void) {
    if (!bbuf) return;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    for (int y = 0; y < sh; y++)
        store32(&bbuf[y * sw], DESKTOP_COLOR, sw);
}

static void draw_wallpaper_rect(int rx, int ry, int rw, int rh) {
    if (!bbuf) return;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > sw) rw = sw - rx;
    if (ry + rh > sh) rh = sh - ry;
    if (rw <= 0 || rh <= 0) return;
    for (int y = ry; y < ry + rh; y++)
        store32(&bbuf[y * sw + rx], DESKTOP_COLOR, rw);
}

static void draw_wallpaper_rect_minus(int rx, int ry, int rw, int rh,
                                      int cx, int cy, int cw, int ch) {
    int ax0 = rx, ay0 = ry, ax1 = rx + rw, ay1 = ry + rh;
    int bx0 = cx, by0 = cy, bx1 = cx + cw, by1 = cy + ch;
    if (bx0 >= ax1 || bx1 <= ax0 || by0 >= ay1 || by1 <= ay0) {
        draw_wallpaper_rect(rx, ry, rw, rh);
        return;
    }
    int ix0 = ax0 > bx0 ? ax0 : bx0;
    int ix1 = ax1 < bx1 ? ax1 : bx1;
    int iy0 = ay0 > by0 ? ay0 : by0;
    int iy1 = ay1 < by1 ? ay1 : by1;
    if (iy0 > ay0) draw_wallpaper_rect(ax0, ay0, rw, iy0 - ay0);
    if (iy1 < ay1) draw_wallpaper_rect(ax0, iy1, rw, ay1 - iy1);
    if (ix0 > ax0) draw_wallpaper_rect(ax0, iy0, ix0 - ax0, iy1 - iy0);
    if (ix1 < ax1) draw_wallpaper_rect(ix1, iy0, ax1 - ix1, iy1 - iy0);
}

static void present_rect_minus(int rx, int ry, int rw, int rh,
                               int cx, int cy, int cw, int ch) {
    int ax0 = rx, ay0 = ry, ax1 = rx + rw, ay1 = ry + rh;
    int bx0 = cx, by0 = cy, bx1 = cx + cw, by1 = cy + ch;
    if (bx0 >= ax1 || bx1 <= ax0 || by0 >= ay1 || by1 <= ay0) {
        fb_present_rect(rx, ry, rw, rh);
        return;
    }
    int ix0 = ax0 > bx0 ? ax0 : bx0;
    int ix1 = ax1 < bx1 ? ax1 : bx1;
    int iy0 = ay0 > by0 ? ay0 : by0;
    int iy1 = ay1 < by1 ? ay1 : by1;
    if (iy0 > ay0) fb_present_rect(ax0, ay0, rw, iy0 - ay0);
    if (iy1 < ay1) fb_present_rect(ax0, iy1, rw, ay1 - iy1);
    if (ix0 > ax0) fb_present_rect(ax0, iy0, ix0 - ax0, iy1 - iy0);
    if (ix1 < ax1) fb_present_rect(ix1, iy0, ax1 - ix1, iy1 - iy0);
}

/* ---------------- term helpers ---------------- */

static int term_client_x(term_t *t) { return t->win_x + TERM_BORDER; }
static int term_client_y(term_t *t) { return t->win_y + TERM_TITLE_H; }
static int term_client_w(term_t *t) { return t->gui_mode ? t->gui_w : t->cols * FB_CELL_W; }
static int term_client_h(term_t *t) { return t->gui_mode ? t->gui_h : t->rows * FB_CELL_H; }
static int term_win_w(term_t *t) { return term_client_w(t) + TERM_BORDER * 2; }
static int term_win_h(term_t *t) { return term_client_h(t) + TERM_TITLE_H + TERM_BORDER; }
static int term_outer_w(term_t *t) { return term_win_w(t) + UI_SHADOW_PAD; }
static int term_outer_h(term_t *t) { return term_win_h(t) + UI_SHADOW_PAD; }

static int term_drag_target(term_t *t, int mx, int my, int *outx, int *outy) {
    int w = term_win_w(t), h = term_win_h(t);
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    int desk_h = sh - TASKBAR_H;
    int x = mx - t->win_drag_dx;
    int y = my - t->win_drag_dy;
    if (x < -w + 40) x = -w + 40;
    if (y < 0) y = 0;
    if (x > sw - 40) x = sw - 40;
    if (y > desk_h - TERM_TITLE_H) y = desk_h - TERM_TITLE_H;
    if (w <= 0 || h <= 0 || w > sw || h > desk_h) return 0;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > sw) x = sw - w;
    if (y + h > desk_h) y = desk_h - h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    *outx = x; *outy = y;
    return 1;
}

static term_t *term_by_id(int id) {
    for (int i = 0; i < MAX_TERMS; i++) if (terms[i].used && terms[i].id == id) return &terms[i];
    return 0;
}

static term_t *term_from_proc(proc_t *p) {
    if (!p || !p->term) return 0;
    term_t *t = (term_t *)p->term;
    for (int i = 0; i < MAX_TERMS; i++) if (&terms[i] == t && t->used) return t;
    return 0;
}

static term_t *term_for_current(void) {
    term_t *t = term_from_proc(current);
    if (t) return t;
    if (focused_term && focused_term->used) return focused_term;
    for (int i = 0; i < MAX_TERMS; i++) if (terms[i].used) return &terms[i];
    return 0;
}

static void term_mark_dirty(term_t *t, int cx0, int cy0, int cx1, int cy1) {
    if (!t) return;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > t->cols) cx1 = t->cols;
    if (cy1 > t->rows) cy1 = t->rows;
    if (cx0 >= cx1 || cy0 >= cy1) return;
    if (!t->dirty) {
        t->dirty = 1;
        t->dirty_x0 = cx0; t->dirty_y0 = cy0; t->dirty_x1 = cx1; t->dirty_y1 = cy1;
    } else {
        if (cx0 < t->dirty_x0) t->dirty_x0 = cx0;
        if (cy0 < t->dirty_y0) t->dirty_y0 = cy0;
        if (cx1 > t->dirty_x1) t->dirty_x1 = cx1;
        if (cy1 > t->dirty_y1) t->dirty_y1 = cy1;
    }
}

static void term_mark_all_dirty(term_t *t) {
    if (t) term_mark_dirty(t, 0, 0, t->cols, t->rows);
}

static void term_clear_dirty(term_t *t) {
    if (!t) return;
    t->dirty = 0;
    t->dirty_x0 = t->dirty_y0 = 0;
    t->dirty_x1 = t->dirty_y1 = 0;
}

/* z-order: bottom-to-top */
static term_t *zterms[MAX_TERMS];
static int zcount;

static void z_remove(term_t *t) {
    int w = 0;
    for (int i = 0; i < zcount; i++) if (zterms[i] != t) zterms[w++] = zterms[i];
    zcount = w;
}

static void z_bring_front(term_t *t) {
    if (!t) return;
    z_remove(t);
    if (zcount < MAX_TERMS) zterms[zcount++] = t;
}

static void z_add(term_t *t) {
    if (!t) return;
    for (int i = 0; i < zcount; i++) if (zterms[i] == t) return;
    if (zcount < MAX_TERMS) zterms[zcount++] = t;
}

static term_t *top_visible_term(void) {
    for (int i = zcount - 1; i >= 0; i--) {
        term_t *t = zterms[i];
        if (t && t->used && t->win_open) return t;
    }
    return 0;
}

/* ---------------- mouse cursor ---------------- */

static void mcur_save(int x, int y) {
    for (int yy = 0; yy < MCUR_H; yy++) {
        for (int xx = 0; xx < MCUR_W; xx++) {
            int px = x + xx, py = y + yy;
            if (px >= 0 && py >= 0 && (uint32_t)px < vbe_width() && (uint32_t)py < vbe_height())
                mcur_buf[yy * MCUR_W + xx] = bbuf[py * vbe_width() + px];
            else
                mcur_buf[yy * MCUR_W + xx] = 0;
        }
    }
}

static void mcur_restore(int x, int y) {
    if (!mcur_saved) return;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    for (int yy = 0; yy < MCUR_H; yy++) {
        int py = y + yy;
        if (py < 0 || py >= sh) continue;
        for (int xx = 0; xx < MCUR_W; xx++) {
            int px = x + xx;
            if (px < 0 || px >= sw) continue;
            bbuf[py * sw + px] = mcur_buf[yy * MCUR_W + xx];
        }
    }
}

static void mcur_draw(int x, int y) {
    static const uint8_t cur[MCUR_H][MCUR_W] = {
        {1,0,0,0,0,0,0,0,0,0,0,0},
        {1,1,0,0,0,0,0,0,0,0,0,0},
        {1,2,1,0,0,0,0,0,0,0,0,0},
        {1,2,2,1,0,0,0,0,0,0,0,0},
        {1,2,2,2,1,0,0,0,0,0,0,0},
        {1,2,2,2,2,1,0,0,0,0,0,0},
        {1,2,2,2,2,2,1,0,0,0,0,0},
        {1,2,2,2,2,2,2,1,0,0,0,0},
        {1,2,2,2,2,2,2,2,1,0,0,0},
        {1,2,2,2,2,2,2,2,2,1,0,0},
        {1,2,2,2,2,2,2,2,2,2,1,0},
        {1,2,2,2,2,2,2,1,1,1,1,1},
        {1,2,2,2,1,2,2,1,0,0,0,0},
        {1,2,2,1,0,1,2,2,1,0,0,0},
        {1,2,1,0,0,1,2,2,1,0,0,0},
        {1,1,0,0,0,0,1,2,2,1,0,0},
        {1,0,0,0,0,0,1,2,2,1,0,0},
        {0,0,0,0,0,0,0,1,1,0,0,0},
    };
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    for (int yy = 0; yy < MCUR_H; yy++) {
        int py = y + yy;
        if (py < 0 || py >= sh) continue;
        for (int xx = 0; xx < MCUR_W; xx++) {
            int px = x + xx;
            if (px < 0 || px >= sw) continue;
            uint8_t v = cur[yy][xx];
            if (v == 1) bbuf[py * sw + px] = 0x000000;
            else if (v == 2) bbuf[py * sw + px] = 0xFFFFFF;
        }
    }
}

static void mcur_hide(void) {
    if (!mcur_saved) return;
    mcur_restore(mcur_x, mcur_y);
    mcur_saved = 0;
}

static void mcur_show(void) {
    mcur_save(mcur_x, mcur_y);
    mcur_saved = 1;
    mcur_draw(mcur_x, mcur_y);
}

/* ---------------- menu bg ---------------- */

static void menu_capture_bg_at(int x, int y) {
    if (!bbuf) return;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    for (int yy = 0; yy < MENU_OUTER_H; yy++) {
        int sy = y + yy;
        int x0 = x, x1 = x + MENU_OUTER_W;
        if (x0 < 0) x0 = 0;
        if (x1 > sw) x1 = sw;
        if (sy >= 0 && sy < sh && x1 > x0)
            copy32(&menu_bg[yy * MENU_OUTER_W + (x0 - x)], &bbuf[sy * sw + x0], x1 - x0);
    }
    menu_bg_valid = 1;
}

static void menu_restore_bg_at(int x, int y) {
    if (!bbuf || !menu_bg_valid) return;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    for (int yy = 0; yy < MENU_OUTER_H; yy++) {
        int sy = y + yy;
        int x0 = x, x1 = x + MENU_OUTER_W;
        if (x0 < 0) x0 = 0;
        if (x1 > sw) x1 = sw;
        if (sy >= 0 && sy < sh && x1 > x0)
            copy32(&bbuf[sy * sw + x0], &menu_bg[yy * MENU_OUTER_W + (x0 - x)], x1 - x0);
    }
}

static void menu_update_bg_rect(int x0, int y0, int x1, int y1) {
    if (!bbuf || !menu_bg_valid) return;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    if (x0 < start_menu_x) x0 = start_menu_x;
    if (y0 < start_menu_y) y0 = start_menu_y;
    if (x1 > start_menu_x + MENU_OUTER_W) x1 = start_menu_x + MENU_OUTER_W;
    if (y1 > start_menu_y + MENU_OUTER_H) y1 = start_menu_y + MENU_OUTER_H;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;
    if (x0 >= x1 || y0 >= y1) return;
    for (int y = y0; y < y1; y++)
        copy32(&menu_bg[(y - start_menu_y) * MENU_OUTER_W + (x0 - start_menu_x)],
               &bbuf[y * sw + x0], x1 - x0);
}

/* ---------------- terminal rendering ---------------- */

static void term_cell_colors(term_t *t, int x, int y, uint32_t *fg, uint32_t *bg) {
    uint16_t v = t->cells[y * t->cols + x];
    uint8_t attr = (uint8_t)(v >> 8);
    *fg = fb_palette[attr & 0x0F];
    *bg = fb_palette[(attr >> 4) & 0x0F];
    if (t->sel_active && t->sel_anchor >= 0 && t->sel_end >= 0) {
        int idx = y * t->cols + x;
        int a = t->sel_anchor, b = t->sel_end;
        if (a > b) { int tmp = a; a = b; b = tmp; }
        if (idx >= a && idx <= b) {
            uint32_t tfg = *fg; *fg = *bg; *bg = tfg;
        }
    }
}

static void term_draw_cell_at(term_t *t, int x, int y) {
    if (!fbcon_on || !t || !t->win_open) return;
    if (x < 0 || x >= t->cols || y < 0 || y >= t->rows) return;
    if (t->fb_cur_x == x && t->fb_cur_y == y) {
        t->fb_cur_x = -1;
        t->fb_cur_y = -1;
    }
    int ox = term_client_x(t) + x * FB_CELL_W;
    int oy = term_client_y(t) + y * FB_CELL_H;
    uint32_t fg, bg;
    term_cell_colors(t, x, y, &fg, &bg);
    uint8_t ch = (uint8_t)(t->cells[y * t->cols + x] & 0xFF);
    fb_draw_glyph_px(ox, oy, (char)ch, fg, bg);
}

static void term_draw_cells_rect(term_t *t, int x0, int y0, int x1, int y1) {
    if (!t || !t->win_open) return;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > t->cols) x1 = t->cols;
    if (y1 > t->rows) y1 = t->rows;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            term_draw_cell_at(t, x, y);
}

static void term_undraw_cursor(term_t *t) {
    if (!t) return;
    if (t->fb_cur_x >= 0 && t->fb_cur_y >= 0)
        term_draw_cell_at(t, t->fb_cur_x, t->fb_cur_y);
    t->fb_cur_x = t->fb_cur_y = -1;
}

static void term_draw_cursor(term_t *t) {
    if (!t || !t->win_open || !t->cursor_on) { if (t) term_undraw_cursor(t); return; }
    if (t != focused_term) { term_undraw_cursor(t); return; }
    int cx = t->cx, cy = t->cy;
    if (cx < 0 || cx >= t->cols || cy < 0 || cy >= t->rows) { term_undraw_cursor(t); return; }
    if (t->fb_cur_x == cx && t->fb_cur_y == cy) return;
    term_undraw_cursor(t);
    int ox = term_client_x(t) + cx * FB_CELL_W;
    int oy = term_client_y(t) + cy * FB_CELL_H;
    uint32_t fg, bg;
    term_cell_colors(t, cx, cy, &fg, &bg);
    uint8_t ch = (uint8_t)(t->cells[cy * t->cols + cx] & 0xFF);
    fb_draw_glyph_px(ox, oy, (char)ch, bg, fg);
    t->fb_cur_x = cx;
    t->fb_cur_y = cy;
}

static void draw_term_chrome(term_t *t, int active) {
    if (!t || !t->win_open) return;
    int x = t->win_x, y = t->win_y, w = term_win_w(t), h = term_win_h(t);
    uint32_t title_bg = active ? W95_NAVY : W95_INACTIVE;
    uint32_t title_fg = W95_LIGHT;

    /* outer black outline, then light/dark 3D frame */
    fb_fill_rect(x, y, w, h, W95_DARK);
    fb_fill_rect(x + 1, y + 1, w - 2, h - 2, W95_FACE);
    fb_fill_rect(x + 1, y + 1, w - 2, 1, W95_LIGHT);
    fb_fill_rect(x + 1, y + 1, 1, h - 2, W95_LIGHT);
    fb_fill_rect(x + 1, y + h - 2, w - 2, 1, W95_SHADOW);
    fb_fill_rect(x + w - 2, y + 1, 1, h - 2, W95_SHADOW);

    /* square title bar */
    int tb_h = TERM_TITLE_H;
    fb_fill_rect(x + 2, y + 2, w - 4, tb_h - 3, title_bg);

    /* title text */
    const char *title = t->title[0] ? t->title : "Terminal";
    char tbuf[16];
    int n = 0;
    while (title[n] && n < 14) { tbuf[n] = title[n]; n++; }
    tbuf[n] = 0;
    fb_draw_text_px(x + 8, y + 2 + (tb_h - 3 - FB_CELL_H) / 2, tbuf, title_fg, title_bg);

    /* close button */
    int bbh = TERM_BTN_H, bbw = TERM_BTN_W;
    int cby = y + 3;
    int cbx = x + w - bbw - 3;
    fb_fill_rect(cbx, cby, bbw, bbh, W95_FACE);
    w95_edge2(cbx, cby, bbw, bbh, 1);
    for (int i = 0; i < 7; i++) {
        fb_fill_rect(cbx + 6 + i, cby + 6 + i, 1, 1, W95_TEXT);
        fb_fill_rect(cbx + 12 - i, cby + 6 + i, 1, 1, W95_TEXT);
    }

    /* maximize / restore button */
    int xbx = cbx - bbw - 2;
    fb_fill_rect(xbx, cby, bbw, bbh, W95_FACE);
    w95_edge2(xbx, cby, bbw, bbh, 1);
    if (t->win_maximized) {
        fb_fill_rect(xbx + 6, cby + 4, 7, 2, W95_TEXT);
        fb_fill_rect(xbx + 6, cby + 8, 7, 4, W95_TEXT);
        fb_fill_rect(xbx + 7, cby + 9, 5, 2, W95_FACE);
    } else {
        fb_fill_rect(xbx + 6, cby + 4, 8, 1, W95_TEXT);
        fb_fill_rect(xbx + 6, cby + 4, 1, 8, W95_TEXT);
        fb_fill_rect(xbx + 13, cby + 4, 1, 8, W95_TEXT);
        fb_fill_rect(xbx + 6, cby + 11, 8, 1, W95_TEXT);
    }

    /* minimize button */
    int mbx = xbx - bbw - 2;
    fb_fill_rect(mbx, cby, bbw, bbh, W95_FACE);
    w95_edge2(mbx, cby, bbw, bbh, 1);
    fb_fill_rect(mbx + 6, cby + bbh - 6, 8, 2, W95_TEXT);

    /* client area: sunken 2px border */
    int cw = term_client_w(t), ch = term_client_h(t);
    int cx = term_client_x(t), cy = term_client_y(t);
    fb_fill_rect(cx - 2, cy - 2, cw + 4, 2, W95_SHADOW);
    fb_fill_rect(cx - 2, cy - 2, 2, ch + 4, W95_SHADOW);
    fb_fill_rect(cx - 2, cy + ch, cw + 4, 2, W95_LIGHT);
    fb_fill_rect(cx + cw, cy - 2, 2, ch + 4, W95_LIGHT);
}

static void term_draw_window(term_t *t, int active) {
    if (!t || !t->win_open || !fbcon_on) return;
    draw_term_chrome(t, active);
    if (t->gui_mode && t->gui_buf) {
        int cx0 = term_client_x(t), cy0 = term_client_y(t);
        int gw = t->gui_w, gh = t->gui_h;
        int sw = (int)vbe_width(), sh = (int)vbe_height();
        for (int y = 0; y < gh; y++) {
            int py = cy0 + y;
            if (py < 0 || py >= sh) continue;
            int x0 = cx0 < 0 ? 0 : cx0;
            int x1 = cx0 + gw > sw ? sw : cx0 + gw;
            if (x1 > x0) copy32(&bbuf[py * sw + x0], &t->gui_buf[y * gw + (x0 - cx0)], x1 - x0);
        }
        t->fb_cur_x = t->fb_cur_y = -1;
    } else {
        term_draw_cells_rect(t, 0, 0, t->cols, t->rows);
        t->fb_cur_x = t->fb_cur_y = -1;
        if (active) term_draw_cursor(t);
    }
}

/* ---------------- taskbar / start menu / compose ---------------- */

extern uint32_t rtc_time(void);

static int taskbtn_x_for_index(int i) {
    return start_btn_x + start_btn_w + 12 + i * (TASKBTN_W + TASKBTN_GAP);
}

static int term_taskbar_index(term_t *t, int *out_idx) {
    int idx = 0;
    for (int i = 0; i < zcount; i++) {
        term_t *tt = zterms[i];
        if (!tt->used) continue;
        if (tt == t) { if (out_idx) *out_idx = idx; return 1; }
        idx++;
    }
    /* also include used but not in zlist? shouldn't happen */
    for (int i = 0; i < MAX_TERMS; i++) {
        term_t *tt = &terms[i];
        if (!tt->used) continue;
        int in_z = 0;
        for (int j = 0; j < zcount; j++) if (zterms[j] == tt) in_z = 1;
        if (in_z) continue;
        if (tt == t) { if (out_idx) *out_idx = idx; return 1; }
        idx++;
    }
    return 0;
}

static term_t *taskbar_term_at(int px, int py) {
    int sh = (int)vbe_height();
    int tby = sh - TASKBAR_H;
    int th = TASKBAR_H - 10;
    int y = tby + 5;
    if (py < y || py >= y + th) return 0;
    int idx = 0;
    for (int i = 0; i < zcount; i++) {
        term_t *t = zterms[i];
        if (!t->used) continue;
        int tx = taskbtn_x_for_index(idx);
        if (px >= tx && px < tx + TASKBTN_W) return t;
        idx++;
    }
    for (int i = 0; i < MAX_TERMS; i++) {
        term_t *t = &terms[i];
        if (!t->used) continue;
        int in_z = 0;
        for (int j = 0; j < zcount; j++) if (zterms[j] == t) in_z = 1;
        if (in_z) continue;
        int tx = taskbtn_x_for_index(idx);
        if (px >= tx && px < tx + TASKBTN_W) return t;
        idx++;
    }
    return 0;
}

static void draw_app_icon16(int x, int y, const char *name);

static void draw_taskbar(void) {
    if (!fbcon_on || !vbe_available()) return;
    int sw = (int)vbe_width();
    int sh = (int)vbe_height();
    int ty = sh - TASKBAR_H;
    fb_fill_rect(0, ty, sw, TASKBAR_H, W95_FACE);
    fb_fill_rect(0, ty, sw, 1, W95_LIGHT);
    fb_fill_rect(0, ty + 1, sw, 1, W95_FACE);

    /* Start button */
    start_btn_h = TASKBAR_H - 6;
    start_btn_y = ty + 3;
    start_btn_w = 82;
    int pressed = start_menu_open;
    fb_fill_rect(start_btn_x, start_btn_y, start_btn_w, start_btn_h, W95_FACE);
    /* little Windows flag */
    int fx = start_btn_x + 4, fy = start_btn_y + (start_btn_h - 16) / 2;
    fb_fill_rect(fx, fy, 16, 16, 0x000000);
    fb_fill_rect(fx + 2, fy + 2, 5, 5, 0xE84848);
    fb_fill_rect(fx + 9, fy + 2, 5, 5, 0x50D870);
    fb_fill_rect(fx + 2, fy + 9, 5, 5, 0x5898F0);
    fb_fill_rect(fx + 9, fy + 9, 5, 5, 0xF0D060);
    w95_edge2(start_btn_x, start_btn_y, start_btn_w, start_btn_h, !pressed);
    fb_draw_text_px(start_btn_x + 26, start_btn_y + (start_btn_h - FB_CELL_H) / 2, "Start", W95_TEXT, W95_FACE);

    /* Window buttons */
    int idx = 0;
    for (int pass = 0; pass < 2; pass++) {
        int count = (pass == 0) ? zcount : MAX_TERMS;
        for (int i = 0; i < count; i++) {
            term_t *t = (pass == 0) ? zterms[i] : &terms[i];
            if (!t->used) continue;
            if (pass == 1) {
                int in_z = 0;
                for (int j = 0; j < zcount; j++) if (zterms[j] == t) in_z = 1;
                if (in_z) continue;
            }
            int tx = taskbtn_x_for_index(idx);
            int tby2 = ty + 3;
            int th = TASKBAR_H - 6;
            int active = (t == focused_term && t->win_open && !t->win_minimized);
            fb_fill_rect(tx, tby2, TASKBTN_W, th, W95_FACE);
            w95_edge2(tx, tby2, TASKBTN_W, th, !active);
            char label[16];
            int n = 0;
            const char *src = t->title[0] ? t->title : "Term";
            while (src[n] && n < 8) { label[n] = src[n]; n++; }
            label[n] = 0;
            int lx = tx + 24, ly = tby2 + (th - FB_CELL_H) / 2;
            if (active) { lx++; ly++; }
            draw_app_icon16(tx + 5 + (active ? 1 : 0), tby2 + (th - 16) / 2 + (active ? 1 : 0),
                            t->title[0] ? t->title : "Terminal");
            fb_draw_text_px(lx, ly, label, W95_TEXT, W95_FACE);
            idx++;
        }
    }

    /* Clock: sunken inset box */
    uint32_t tsec = rtc_time();
    uint32_t h24 = (tsec / 3600u) % 24u;
    uint32_t min = (tsec / 60u) % 60u;
    const char *ampm = h24 < 12 ? "AM" : "PM";
    uint32_t h12 = h24 % 12u; if (!h12) h12 = 12;
    char clk[16];
    int ci = 0;
    if (h12 >= 10) clk[ci++] = '0' + (char)(h12 / 10);
    clk[ci++] = '0' + (char)(h12 % 10);
    clk[ci++] = ':';
    clk[ci++] = '0' + (char)(min / 10);
    clk[ci++] = '0' + (char)(min % 10);
    clk[ci++] = ' ';
    clk[ci++] = ampm[0];
    clk[ci++] = ampm[1];
    clk[ci] = 0;
    int clk_w = 8 * FB_CELL_W + 12;
    int cpx = sw - clk_w - 6;
    int cy = ty + 3;
    int ch = TASKBAR_H - 6;
    fb_fill_rect(cpx, cy, clk_w, ch, W95_FACE);
    w95_edge2(cpx, cy, clk_w, ch, 0);
    fb_draw_text_px(cpx + 5, cy + (ch - FB_CELL_H) / 2, clk, W95_TEXT, W95_FACE);
}

static void fb_fill_quad(int x0, int y0, int x1, int y1,
                         int x2, int y2, int x3, int y3, uint32_t color) {
    int xs[4] = { x0, x1, x2, x3 };
    int ys[4] = { y0, y1, y2, y3 };
    int ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < 4; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    for (int y = ymin; y <= ymax; y++) {
        int l = 0x7FFFFFFF, r = -0x7FFFFFFF;
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) & 3;
            int ya = ys[i], yb = ys[j];
            if (ya == yb) continue;
            if ((y >= ya && y < yb) || (y >= yb && y < ya)) {
                int xa = xs[i], xb = xs[j];
                int x = xa + (y - ya) * (xb - xa) / (yb - ya);
                if (x < l) l = x;
                if (x > r) r = x;
            }
        }
        if (l <= r) fb_fill_rect(l, y, r - l + 1, 1, color);
    }
}

static void draw_app_icon16(int x, int y, const char *name) {
    if (!strcmp(name, "My Computer") || !strcmp(name, "Files")) {
        fb_fill_rect(x + 1, y + 2, 14, 11, 0x000000);
        fb_fill_rect(x + 2, y + 3, 12, 9, W95_FACE);
        fb_fill_rect(x + 3, y + 4, 10, 7, 0x000080);
        fb_fill_rect(x + 3, y + 4, 10, 1, 0x3A6EA5);
        fb_fill_rect(x + 4, y + 6, 4, 3, 0xFFFFFF);
        fb_fill_rect(x + 5, y + 7, 2, 1, 0x008080);
        fb_fill_rect(x + 5, y + 13, 6, 2, 0x808080);
        fb_fill_rect(x + 2, y + 15, 12, 1, 0xC0C0C0);
    } else if (!strcmp(name, "Terminal") || !strcmp(name, "MS-DOS Prompt") || !strcmp(name, "MS-DOS")) {
        fb_fill_rect(x + 1, y + 2, 14, 11, 0x000000);
        fb_fill_rect(x + 2, y + 3, 12, 9, W95_FACE);
        fb_fill_rect(x + 3, y + 4, 10, 7, 0x000000);
        fb_fill_rect(x + 4, y + 5, 3, 2, 0x50D870);
        fb_fill_rect(x + 7, y + 5, 2, 2, 0x50D870);
        fb_fill_rect(x + 4, y + 8, 4, 1, 0x50D870);
        fb_fill_rect(x + 5, y + 13, 6, 2, 0x808080);
        fb_fill_rect(x + 2, y + 15, 12, 1, 0xC0C0C0);
    } else if (!strcmp(name, "Editor") || !strcmp(name, "Notepad") || !strcmp(name, "Untitled - Notepad")) {
        fb_fill_rect(x + 2, y + 1, 12, 15, 0x000000);
        fb_fill_rect(x + 3, y + 2, 10, 13, 0xFFFFFF);
        fb_fill_rect(x + 3, y + 2, 10, 4, 0x000080);
        fb_fill_rect(x + 4, y + 8, 8, 1, 0x000080);
        fb_fill_rect(x + 4, y + 10, 8, 1, 0x000080);
        fb_fill_rect(x + 4, y + 12, 5, 1, 0x000080);
    } else if (!strcmp(name, "Doom")) {
        fb_fill_rect(x + 1, y + 1, 14, 14, 0x000000);
        fb_fill_rrect(x + 2, y + 2, 12, 12, 6, 0x7A2020);
        fb_fill_rect(x + 4, y + 4, 3, 3, 0xF0D060);
        fb_fill_rect(x + 9, y + 4, 3, 3, 0xF0D060);
        fb_fill_rect(x + 5, y + 10, 6, 2, 0xF0D060);
    } else if (!strcmp(name, "Bad Apple")) {
        fb_fill_rect(x, y, 16, 16, 0x000000);
        fb_fill_rrect(x + 4, y + 4, 9, 10, 5, 0xFFFFFF);
        fb_fill_rrect(x + 5, y + 2, 6, 5, 3, 0xFFFFFF);
        fb_fill_rect(x + 9, y + 2, 1, 3, 0x50D870);
        fb_fill_rrect(x + 10, y + 5, 5, 5, 3, 0x000000);
    } else if (!strcmp(name, "3D Gears") || !strcmp(name, "glgears") || !strcmp(name, "TinyGL")) {
        fb_fill_quad(x + 8, y + 1, x + 15, y + 4, x + 8, y + 8, x + 1, y + 4, 0xFFFFFF);
        fb_fill_quad(x + 1, y + 4, x + 8, y + 8, x + 8, y + 15, x + 1, y + 11, 0x3A6EA5);
        fb_fill_quad(x + 8, y + 8, x + 15, y + 4, x + 15, y + 11, x + 8, y + 15, 0x000080);
        fb_fill_quad(x + 6, y + 5, x + 9, y + 5, x + 9, y + 9, x + 6, y + 9, 0xF0D060);
    } else if (!strcmp(name, "Internet") || !strcmp(name, "Internet Explorer") || !strcmp(name, "ie")) {
        fb_fill_rrect(x, y, 16, 16, 8, 0x2A70C8);
        fb_fill_rect(x + 4, y + 4, 8, 2, 0xFFFFFF);
        fb_fill_rect(x + 4, y + 7, 8, 2, 0xFFFFFF);
        fb_fill_rect(x + 4, y + 10, 8, 2, 0xFFFFFF);
        fb_fill_rect(x + 4, y + 4, 2, 8, 0xFFFFFF);
        fb_fill_rect(x + 10, y + 7, 2, 5, 0xFFFFFF);
        fb_fill_rect(x + 2, y + 6, 12, 1, 0xF0D060);
    } else if (!strcmp(name, "Fire")) {
        fb_fill_rect(x, y, 16, 16, 0x000000);
        fb_fill_rect(x + 6, y + 3, 4, 4, 0xF0D060);
        fb_fill_rect(x + 4, y + 7, 8, 5, 0xE84848);
        fb_fill_rect(x + 6, y + 10, 4, 4, 0xF09030);
    } else if (!strcmp(name, "Matrix")) {
        fb_fill_rect(x, y, 16, 16, 0x000000);
        fb_fill_rect(x + 3, y + 2, 2, 10, 0x50D870);
        fb_fill_rect(x + 7, y + 4, 2, 8, 0x30A050);
        fb_fill_rect(x + 11, y + 1, 2, 11, 0x50D870);
    } else if (!strcmp(name, "Clear")) {
        fb_fill_rect(x + 2, y + 1, 12, 15, 0xFFFFFF);
        fb_fill_rect(x + 2, y + 1, 12, 1, 0x808080);
        fb_fill_rect(x + 2, y + 15, 12, 1, 0x808080);
    } else {
        fb_fill_rect(x, y, 16, 16, 0x000000);
        fb_fill_rrect(x + 2, y + 2, 12, 12, 3, W95_FACE);
    }
}

typedef struct { const char *label; const char *path; int x; int y; } desk_icon_t;
static const desk_icon_t desk_icons[] = {
    { "My Computer", "/bin/files",   80, 22 },
    { "Notepad",     "/bin/edit",    80, 114 },
    { "MS-DOS",      "/bin/sh",      80, 206 },
    { "Bad Apple",   "/bin/badapple",80, 298 },
    { "3D Gears",    "/bin/glgears", 80, 390 },
    { "Internet",    "/bin/ie",      80, 482 },
    { "Doom",        "/bin/doom",    80, 574 },
};
#define DESK_ICON_COUNT ((int)(sizeof(desk_icons)/sizeof(desk_icons[0])))

static void draw_app_icon32(int x, int y, const char *name) {
    if (!strcmp(name, "My Computer") || !strcmp(name, "Files")) {
        fb_fill_rect(x + 2, y + 3, 28, 19, 0x000000);
        fb_fill_rect(x + 3, y + 4, 26, 17, W95_FACE);
        fb_fill_rect(x + 5, y + 6, 22, 13, 0x000080);
        fb_fill_rect(x + 5, y + 6, 22, 2, 0x3A6EA5);
        fb_fill_rect(x + 7, y + 9, 6, 5, 0xFFFFFF);
        fb_fill_rect(x + 8, y + 10, 4, 3, 0x008080);
        fb_fill_rect(x + 8, y + 15, 6, 2, 0xFFFFFF);
        fb_fill_rect(x + 12, y + 22, 8, 3, 0x808080);
        fb_fill_rect(x + 7, y + 25, 18, 4, W95_FACE);
        fb_fill_rect(x + 7, y + 25, 18, 1, 0xFFFFFF);
        fb_fill_rect(x + 7, y + 28, 18, 1, 0x808080);
        fb_fill_rect(x + 9, y + 26, 5, 1, 0x808080);
    } else if (!strcmp(name, "Terminal") || !strcmp(name, "MS-DOS Prompt") || !strcmp(name, "MS-DOS")) {
        fb_fill_rect(x + 1, y + 3, 30, 22, 0x000000);
        fb_fill_rect(x + 2, y + 4, 28, 20, W95_FACE);
        fb_fill_rect(x + 4, y + 6, 24, 14, 0x000000);
        fb_fill_rect(x + 6, y + 9, 6, 2, 0x50D870);
        fb_fill_rect(x + 12, y + 9, 3, 2, 0x50D870);
        fb_fill_rect(x + 6, y + 13, 6, 2, 0x50D870);
        fb_fill_rect(x + 6, y + 16, 2, 2, 0x50D870);
        fb_fill_rect(x + 12, y + 2, 8, 3, 0x808080);
        fb_fill_rect(x + 7, y + 25, 18, 4, W95_FACE);
        fb_fill_rect(x + 7, y + 25, 18, 1, 0xFFFFFF);
        fb_fill_rect(x + 7, y + 28, 18, 1, 0x808080);
        fb_fill_rect(x + 9, y + 26, 5, 1, 0x808080);
    } else if (!strcmp(name, "Editor") || !strcmp(name, "Notepad") || !strcmp(name, "Untitled - Notepad")) {
        fb_fill_rect(x + 4, y + 1, 24, 30, 0x000000);
        fb_fill_rect(x + 5, y + 2, 22, 28, 0xFFFFFF);
        fb_fill_rect(x + 5, y + 2, 22, 5, 0x000080);
        fb_fill_rect(x + 7, y + 10, 18, 1, 0x000080);
        fb_fill_rect(x + 7, y + 13, 18, 1, 0x000080);
        fb_fill_rect(x + 7, y + 16, 18, 1, 0x000080);
        fb_fill_rect(x + 7, y + 19, 18, 1, 0x000080);
        fb_fill_rect(x + 7, y + 22, 11, 1, 0x000080);
        fb_fill_rect(x + 23, y + 2, 4, 4, W95_FACE);
        fb_fill_rect(x + 23, y + 6, 4, 1, W95_FACE);
    } else if (!strcmp(name, "Doom")) {
        fb_fill_rect(x + 1, y + 1, 30, 30, 0x000000);
        fb_fill_rrect(x + 2, y + 2, 28, 28, 13, 0x7A2020);
        fb_fill_rect(x + 7, y + 7, 7, 6, 0xF0D060);
        fb_fill_rect(x + 18, y + 7, 7, 6, 0xF0D060);
        fb_fill_rect(x + 10, y + 21, 12, 4, 0xF0D060);
        fb_fill_rect(x + 13, y + 22, 6, 2, 0x7A2020);
    } else if (!strcmp(name, "Bad Apple")) {
        fb_fill_rect(x + 1, y + 1, 30, 30, 0x000000);
        fb_fill_rrect(x + 8, y + 8, 18, 20, 9, 0xFFFFFF);
        fb_fill_rrect(x + 10, y + 3, 11, 10, 5, 0xFFFFFF);
        fb_fill_rect(x + 18, y + 3, 2, 4, 0x50D870);
        fb_fill_rrect(x + 20, y + 10, 10, 10, 5, 0x000000);
        fb_fill_rect(x + 12, y + 24, 12, 2, 0xC0C0C0);
    } else if (!strcmp(name, "3D Gears") || !strcmp(name, "glgears") || !strcmp(name, "TinyGL")) {
        fb_fill_quad(x + 16, y + 2, x + 30, y + 9, x + 16, y + 16, x + 2, y + 9, 0xFFFFFF);
        fb_fill_quad(x + 2, y + 9, x + 16, y + 16, x + 16, y + 30, x + 2, y + 23, 0x3A6EA5);
        fb_fill_quad(x + 16, y + 16, x + 30, y + 9, x + 30, y + 23, x + 16, y + 30, 0x000080);
        fb_fill_quad(x + 12, y + 10, x + 20, y + 10, x + 20, y + 20, x + 12, y + 20, 0xF0D060);
    } else if (!strcmp(name, "Internet") || !strcmp(name, "Internet Explorer") || !strcmp(name, "ie")) {
        fb_fill_rrect(x + 2, y + 2, 28, 28, 14, 0x2A70C8);
        fb_fill_rect(x + 8, y + 8, 16, 3, 0xFFFFFF);
        fb_fill_rect(x + 8, y + 14, 16, 3, 0xFFFFFF);
        fb_fill_rect(x + 8, y + 20, 16, 3, 0xFFFFFF);
        fb_fill_rect(x + 8, y + 8, 3, 15, 0xFFFFFF);
        fb_fill_rect(x + 21, y + 14, 3, 9, 0xFFFFFF);
        fb_fill_rect(x + 4, y + 12, 24, 2, 0xF0D060);
    } else {
        fb_fill_rect(x, y, 32, 32, 0x000000);
        fb_fill_rrect(x + 3, y + 3, 26, 26, 5, W95_FACE);
    }
}

static void draw_desktop_icons(void) {
    if (!fbcon_on) return;
    for (int i = 0; i < DESK_ICON_COUNT; i++) {
        const desk_icon_t *ic = &desk_icons[i];
        int tw = 0; while (ic->label[tw]) tw++;
        int lw = tw * FB_CELL_W + 6;
        int lx = ic->x + 16 - lw / 2;
        int selected = (i == desk_sel);
        if (selected) {
            fb_fill_rect(ic->x - 8, ic->y - 6, 48, 48, W95_NAVY);
            fb_fill_rect(lx - 1, ic->y + 35, lw + 2, FB_CELL_H + 2, W95_NAVY);
        }
        draw_app_icon32(ic->x, ic->y, ic->label);
        fb_draw_text_px(lx + 3, ic->y + 36, ic->label, 0xFFFFFF, selected ? W95_NAVY : 0x008080);
    }
}

static void draw_start_menu_window(void) {
    if (!start_menu_open || !fbcon_on) return;
    int x = start_menu_x, y = start_menu_y, w = MENU_W, h = MENU_H;

    /* raised face with classic 3D border */
    fb_fill_rect(x, y, w, h, W95_DARK);
    fb_fill_rect(x + 1, y + 1, w - 2, h - 2, W95_FACE);
    fb_fill_rect(x + 1, y + 1, w - 2, 1, W95_LIGHT);
    fb_fill_rect(x + 1, y + 1, 1, h - 2, W95_LIGHT);
    fb_fill_rect(x + 1, y + h - 2, w - 2, 1, W95_SHADOW);
    fb_fill_rect(x + w - 2, y + 1, 1, h - 2, W95_SHADOW);

    /* vertical navy sidebar */
    fb_fill_rect(x + 2, y + 2, 24, h - 4, W95_NAVY);
    const char *brand = "spHalfOS";
    for (int i = 0; brand[i]; i++)
        fb_draw_text_px(x + 8, y + h - 18 - (i + 1) * FB_CELL_H, (char[2]){brand[i], 0}, W95_LIGHT, W95_NAVY);
    fb_fill_rect(x + 8, y + 8, 12, 12, W95_LIGHT);

    /* small title strip */
    fb_fill_rect(x + 27, y + 2, w - 29, MENU_TITLE_H - 2, W95_FACE);
    fb_draw_text_px(x + 34, y + 3, "spHalfOS", W95_TEXT, W95_FACE);

    for (int i = 0; i < START_MENU_COUNT; i++) {
        int iy = y + MENU_TITLE_H + i * MENU_ITEM_H;
        int hovered = point_in_rect(mcur_x, mcur_y, x + 27, iy, w - 29, MENU_ITEM_H);
        uint32_t bg = hovered ? W95_NAVY : W95_FACE;
        uint32_t fg = hovered ? W95_LIGHT : W95_TEXT;
        if (hovered) fb_fill_rect(x + 27, iy, w - 29, MENU_ITEM_H - 1, bg);
        draw_app_icon16(x + 34, iy + 6, start_menu_items[i].label);
        fb_draw_text_px(x + 54, iy + 2, start_menu_items[i].label, fg, bg);
    }
}

static void compose_all(void) {
    if (!fbcon_on || !bbuf) return;
    draw_wallpaper_full();
    draw_desktop_icons();
    for (int i = 0; i < zcount; i++) {
        term_t *t = zterms[i];
        if (!t->used || !t->win_open) continue;
        term_draw_window(t, t == focused_term);
    }
    draw_taskbar();
    if (start_menu_open) {
        menu_capture_bg_at(start_menu_x, start_menu_y);
        draw_start_menu_window();
    }
    mcur_saved = 0;
    mcur_show();
    fb_present();
}

/* ---------------- term text/ansi ---------------- */

static void term_present_dirty(term_t *t) {
    if (!fbcon_on || !t || !t->win_open || !t->dirty) return;
    if (t != focused_term) { /* unfocused: just keep buffer, redraw on focus */
        /* but if it's visible on top, still present */
    }
    /* If term is focused and topmost visible, present its dirty client rect. */
    if (t != top_visible_term()) {
        /* not topmost - simpler to just mark dirty; full compose on focus change */
        t->dirty = 0;
        return;
    }
    int x0 = term_client_x(t) + t->dirty_x0 * FB_CELL_W;
    int y0 = term_client_y(t) + t->dirty_y0 * FB_CELL_H;
    int x1 = term_client_x(t) + t->dirty_x1 * FB_CELL_W;
    int y1 = term_client_y(t) + t->dirty_y1 * FB_CELL_H;
    term_clear_dirty(t);
    mcur_hide();
    term_draw_cursor(t);
    if (start_menu_open) {
        /* menu may overlap - just redraw menu over and present union */
        draw_start_menu_window();
    }
    mcur_show();
    fb_present_rect(x0, y0, x1 - x0, y1 - y0);
    /* cursor may be outside dirty rect */
}

static void term_present_if_needed(term_t *t) {
    if (!t || t->present_suppress > 0) return;
    if (!fbcon_on || !t->win_open) { t->dirty = 0; return; }
    if (t->gui_mode) { term_clear_dirty(t); return; }
    if (t != top_visible_term()) { term_clear_dirty(t); return; }
    if (!t->dirty) {
        int oldx = t->fb_cur_x, oldy = t->fb_cur_y;
        mcur_hide();
        term_draw_cursor(t);
        mcur_show();
        if (oldx != t->fb_cur_x || oldy != t->fb_cur_y) {
            if (oldx >= 0 && oldy >= 0)
                fb_present_rect(term_client_x(t) + oldx * FB_CELL_W, term_client_y(t) + oldy * FB_CELL_H, FB_CELL_W, FB_CELL_H);
            if (t->fb_cur_x >= 0 && t->fb_cur_y >= 0)
                fb_present_rect(term_client_x(t) + t->fb_cur_x * FB_CELL_W, term_client_y(t) + t->fb_cur_y * FB_CELL_H, FB_CELL_W, FB_CELL_H);
        }
        return;
    }
    int dx0 = t->dirty_x0, dy0 = t->dirty_y0, dx1 = t->dirty_x1, dy1 = t->dirty_y1;
    term_clear_dirty(t);
    int px0 = term_client_x(t) + dx0 * FB_CELL_W;
    int py0 = term_client_y(t) + dy0 * FB_CELL_H;
    int px1 = term_client_x(t) + dx1 * FB_CELL_W;
    int py1 = term_client_y(t) + dy1 * FB_CELL_H;
    mcur_hide();
    term_draw_cursor(t);
    if (start_menu_open &&
        rects_intersect(px0, py0, px1-px0, py1-py0, start_menu_x, start_menu_y, MENU_W+UI_SHADOW_PAD, MENU_H+UI_SHADOW_PAD)) {
        menu_update_bg_rect(px0, py0, px1, py1);
        draw_start_menu_window();
    }
    mcur_show();
    fb_present_rect(px0, py0, px1 - px0, py1 - py0);
}

static void term_scroll_up(term_t *t) {
    if (!t) return;
    t->sel_active = t->sel_drag = 0; t->sel_anchor = t->sel_end = -1;
    for (int i = 0; i < t->cols * (t->rows - 1); i++)
        t->cells[i] = t->cells[i + t->cols];
    uint16_t blank = (uint16_t)(' ' | (t->cur_attr << 8));
    for (int i = 0; i < t->cols; i++)
        t->cells[(t->rows - 1) * t->cols + i] = blank;
    if (t->cy > 0) t->cy--;
    if (t->gui_mode) { term_mark_all_dirty(t); return; }

    if (!fbcon_on) {
        int rmax = t->rows < 25 ? t->rows : 25;
        int cmax = t->cols < 80 ? t->cols : 80;
        for (int y = 0; y < rmax; y++)
            for (int x = 0; x < cmax; x++)
                VGA_MEM[y * 80 + x] = t->cells[y * t->cols + x];
        return;
    }
    if (!t->win_open) { term_mark_all_dirty(t); return; }
    if (t != top_visible_term()) { term_mark_all_dirty(t); return; }

    int cxp = term_client_x(t);
    int cyp = term_client_y(t);
    int cw = term_client_w(t);
    int ch = term_client_h(t);
    mcur_hide();
    bbuf_move_rect(cxp, cyp, cxp, cyp + FB_CELL_H, cw, ch - FB_CELL_H);
    fb_fill_rect(cxp, cyp + (t->rows - 1) * FB_CELL_H, cw, FB_CELL_H, fb_palette[(t->cur_attr >> 4) & 0x0F]);
    for (int x = 0; x < t->cols; x++)
        term_draw_cell_at(t, x, t->rows - 1);
    t->fb_cur_x = t->fb_cur_y = -1;
    term_mark_dirty(t, 0, 0, t->cols, t->rows);
    if (t->present_suppress <= 0) {
        term_clear_dirty(t);
        term_draw_cursor(t);
        if (start_menu_open) draw_start_menu_window();
        mcur_show();
        fb_present_rect(cxp, cyp, cw, ch);
    } else {
        mcur_show();
    }
}

static void term_put_cell(term_t *t, int x, int y, char c, uint8_t attr) {
    if (!t || x < 0 || x >= t->cols || y < 0 || y >= t->rows) return;
    t->cells[y * t->cols + x] = (uint16_t)((uint8_t)c | (attr << 8));
    if (!fbcon_on && x < 80 && y < 25) VGA_MEM[y * 80 + x] = (uint16_t)((uint8_t)c | (attr << 8));
    if (fbcon_on && t->win_open && t == top_visible_term()) {
        term_draw_cell_at(t, x, y);
        term_mark_dirty(t, x, y, x + 1, y + 1);
    } else {
        term_mark_dirty(t, x, y, x + 1, y + 1);
    }
}

static void term_clear_line_from(term_t *t, int x, int y) {
    if (!t || y < 0 || y >= t->rows) return;
    if (x < 0) x = 0;
    for (int i = x; i < t->cols; i++) term_put_cell(t, i, y, ' ', t->cur_attr);
}

static void term_clear_line_to(term_t *t, int x, int y) {
    if (!t || y < 0 || y >= t->rows) return;
    if (x >= t->cols) x = t->cols - 1;
    for (int i = 0; i <= x; i++) term_put_cell(t, i, y, ' ', t->cur_attr);
}

static void term_erase_display(term_t *t, int mode) {
    if (!t) return;
    if (mode == 2) {
        uint16_t blank = (uint16_t)(' ' | (t->cur_attr << 8));
        for (int i = 0; i < t->cols * t->rows; i++) t->cells[i] = blank;
        t->cx = t->cy = 0;
        if (fbcon_on && t->win_open) {
            if (t == top_visible_term()) {
                draw_term_chrome(t, 1);
                term_draw_cells_rect(t, 0, 0, t->cols, t->rows);
                t->fb_cur_x = t->fb_cur_y = -1;
                term_mark_all_dirty(t);
            } else {
                term_mark_all_dirty(t);
            }
        }
        return;
    }
    if (mode == 0) {
        term_clear_line_from(t, t->cx, t->cy);
        for (int y = t->cy + 1; y < t->rows; y++)
            for (int x = 0; x < t->cols; x++) term_put_cell(t, x, y, ' ', t->cur_attr);
    } else if (mode == 1) {
        term_clear_line_to(t, t->cx, t->cy);
        for (int y = 0; y < t->cy; y++)
            for (int x = 0; x < t->cols; x++) term_put_cell(t, x, y, ' ', t->cur_attr);
    }
}

static int term_parse_params(term_t *t, int *params, int max) {
    int n = 0;
    int val = 0;
    int any = 0;
    for (int i = 0; i < t->esc_len; i++) {
        char c = t->esc_buf[i];
        if (c >= '0' && c <= '9') { val = val * 10 + (c - '0'); any = 1; }
        else if (c == ';') {
            if (n < max) params[n++] = any ? val : 0;
            val = 0; any = 0;
        }
    }
    if (n < max) params[n++] = any ? val : 0;
    return n;
}

static void term_handle_sgr(term_t *t) {
    int params[16];
    int n = term_parse_params(t, params, 16);
    if (n == 0) { t->cur_attr = 0x07; return; }
    for (int i = 0; i < n; i++) {
        int p = params[i];
        if (p == 0) t->cur_attr = 0x07;
        else if (p == 1) t->cur_attr |= 0x08;
        else if (p == 22) t->cur_attr &= ~0x08;
        else if (p == 7) {
            uint8_t fg = t->cur_attr & 0x0F, bg = (t->cur_attr >> 4) & 0x0F;
            t->cur_attr = (uint8_t)(fg << 4 | bg);
        }
        else if (p >= 30 && p <= 37) t->cur_attr = (t->cur_attr & 0xF0) | (uint8_t)(p - 30);
        else if (p >= 40 && p <= 47) t->cur_attr = (t->cur_attr & 0x0F) | (uint8_t)((p - 40) << 4);
        else if (p >= 90 && p <= 97) t->cur_attr = (t->cur_attr & 0xF0) | (uint8_t)(p - 90 + 8);
        else if (p >= 100 && p <= 107) t->cur_attr = (t->cur_attr & 0x0F) | (uint8_t)((p - 100 + 8) << 4);
        else if (p == 39) t->cur_attr = (t->cur_attr & 0xF0) | 0x07;
        else if (p == 49) t->cur_attr = (t->cur_attr & 0x0F) | 0x00;
    }
}

static void term_handle_csi(term_t *t, char cmd) {
    int params[16];
    int n = term_parse_params(t, params, 16);
    int p1 = n >= 1 ? params[0] : 0;
    int p2 = n >= 2 ? params[1] : 0;
    if (p1 < 0) p1 = 0;
    if (p2 < 0) p2 = 0;
    switch (cmd) {
    case 'A': t->cy -= p1 ? p1 : 1; if (t->cy < 0) t->cy = 0; break;
    case 'B': t->cy += p1 ? p1 : 1; if (t->cy >= t->rows) t->cy = t->rows - 1; break;
    case 'C': t->cx += p1 ? p1 : 1; if (t->cx >= t->cols) t->cx = t->cols - 1; break;
    case 'D': t->cx -= p1 ? p1 : 1; if (t->cx < 0) t->cx = 0; break;
    case 'E': t->cy += p1 ? p1 : 1; t->cx = 0; if (t->cy >= t->rows) t->cy = t->rows - 1; break;
    case 'F': t->cy -= p1 ? p1 : 1; t->cx = 0; if (t->cy < 0) t->cy = 0; break;
    case 'G': t->cx = (p1 ? p1 : 1) - 1; if (t->cx < 0) t->cx = 0; if (t->cx >= t->cols) t->cx = t->cols - 1; break;
    case 'H': case 'f':
        t->cy = (p1 ? p1 : 1) - 1; t->cx = (p2 ? p2 : 1) - 1;
        if (t->cy < 0) t->cy = 0; if (t->cx < 0) t->cx = 0;
        if (t->cy >= t->rows) t->cy = t->rows - 1;
        if (t->cx >= t->cols) t->cx = t->cols - 1;
        break;
    case 'J': term_erase_display(t, p1); break;
    case 'K':
        if (p1 == 0) term_clear_line_from(t, t->cx, t->cy);
        else if (p1 == 1) term_clear_line_to(t, t->cx, t->cy);
        else if (p1 == 2) term_clear_line_from(t, 0, t->cy);
        break;
    case 's': t->saved_x = t->cx; t->saved_y = t->cy; break;
    case 'u': t->cx = t->saved_x; t->cy = t->saved_y; break;
    case 'm': term_handle_sgr(t); break;
    case 'h':
        if (t->esc_len > 0 && t->esc_buf[0] == '?') {
            int dparams[8]; int dn = term_parse_params(t, dparams, 8);
            for (int i = 0; i < dn; i++) if (dparams[i] == 25) t->cursor_on = 1;
        }
        break;
    case 'l':
        if (t->esc_len > 0 && t->esc_buf[0] == '?') {
            int dparams[8]; int dn = term_parse_params(t, dparams, 8);
            for (int i = 0; i < dn; i++) if (dparams[i] == 25) { t->cursor_on = 0; term_undraw_cursor(t); }
        }
        break;
    default: break;
    }
}

static void term_putc(term_t *t, char c) {
    if (!t) return;
    uint8_t uc = (uint8_t)c;

    if (t->esc_state == 1) { /* got ESC */
        if (uc == '[') { t->esc_state = 2; t->esc_len = 0; return; }
        if (uc == 'c') { /* reset */
            t->cur_attr = 0x07; t->cx = t->cy = 0; t->saved_x = t->saved_y = 0;
            term_erase_display(t, 2);
            t->esc_state = 0; return;
        }
        if (uc == '7') { t->saved_x = t->cx; t->saved_y = t->cy; t->esc_state = 0; return; }
        if (uc == '8') { t->cx = t->saved_x; t->cy = t->saved_y; t->esc_state = 0; return; }
        t->esc_state = 0;
        return;
    }
    if (t->esc_state == 2) { /* CSI */
        if (uc >= 0x40 && uc <= 0x7E) {
            term_handle_csi(t, (char)uc);
            t->esc_state = 0;
        } else if (t->esc_len < (int)sizeof(t->esc_buf) - 1) {
            t->esc_buf[t->esc_len++] = (char)uc;
            t->esc_buf[t->esc_len] = 0;
        } else {
            t->esc_state = 0;
        }
        return;
    }

    if (uc == 27) { t->esc_state = 1; return; }
    if (uc == '\r') { t->cx = 0; return; }
    if (uc == '\n') {
        t->cx = 0; t->cy++;
        if (t->cy >= t->rows) term_scroll_up(t);
        return;
    }
    if (uc == '\b') {
        if (t->cx > 0) { t->cx--; term_put_cell(t, t->cx, t->cy, ' ', t->cur_attr); }
        return;
    }
    if (uc == '\t') {
        int next = (t->cx + 8) & ~7;
        while (t->cx < next && t->cx < t->cols) { term_put_cell(t, t->cx, t->cy, ' ', t->cur_attr); t->cx++; }
        if (t->cx >= t->cols) { t->cx = 0; t->cy++; if (t->cy >= t->rows) term_scroll_up(t); }
        return;
    }

    term_put_cell(t, t->cx, t->cy, (char)uc, t->cur_attr);
    t->cx++;
    if (t->cx >= t->cols) {
        t->cx = 0; t->cy++;
        if (t->cy >= t->rows) term_scroll_up(t);
    }
}

static void term_write(term_t *t, const char *s, size_t n) {
    if (!t) return;
    t->present_suppress++;
    for (size_t i = 0; i < n; i++) term_putc(t, s[i]);
    t->present_suppress--;
    if (t->present_suppress <= 0) {
        t->present_suppress = 0;
        term_present_if_needed(t);
    }
}

/* ---------------- public console API ---------------- */

static void term_reset(term_t *t, int cols, int rows) {
    memset(t->cells, 0, sizeof(t->cells));
    t->cols = cols; t->rows = rows;
    t->cx = t->cy = 0;
    t->cur_attr = 0x07;
    t->cursor_on = 1;
    t->saved_x = t->saved_y = 0;
    t->esc_state = 0; t->esc_len = 0;
    t->in_head = t->in_tail = 0;
    t->sel_active = t->sel_drag = 0; t->sel_anchor = t->sel_end = -1;
    t->fb_cur_x = t->fb_cur_y = -1;
    t->present_suppress = 0;
    t->dirty = 0;
    uint16_t blank = (uint16_t)(' ' | (t->cur_attr << 8));
    for (int i = 0; i < CON_MAX_COLS * CON_MAX_ROWS; i++) t->cells[i] = blank;
}

static void term_set_grid(term_t *t, int cols, int rows) {
    if (!t || t->gui_mode) return;
    if (cols < 20) cols = 20;
    if (rows < 5) rows = 5;
    if (cols > CON_MAX_COLS) cols = CON_MAX_COLS;
    if (rows > CON_MAX_ROWS) rows = CON_MAX_ROWS;
    if (cols == t->cols && rows == t->rows) return;
    static uint16_t old_cells[CON_MAX_COLS * CON_MAX_ROWS];
    int oc = t->cols, orows = t->rows;
    memcpy(old_cells, t->cells, sizeof(old_cells));
    uint16_t blank = (uint16_t)(' ' | (t->cur_attr << 8));
    for (int i = 0; i < CON_MAX_COLS * CON_MAX_ROWS; i++) t->cells[i] = blank;
    t->cols = cols; t->rows = rows;
    int cc = oc < cols ? oc : cols;
    int rc = orows < rows ? orows : rows;
    for (int y = 0; y < rc; y++)
        for (int x = 0; x < cc; x++)
            t->cells[y * cols + x] = old_cells[y * oc + x];
    if (t->cx >= cols) t->cx = cols - 1;
    if (t->cy >= rows) t->cy = rows - 1;
    t->fb_cur_x = t->fb_cur_y = -1;
}

static term_t *alloc_term(const char *title) {
    for (int i = 0; i < MAX_TERMS; i++) {
        if (terms[i].used) continue;
        term_t *t = &terms[i];
        memset(t, 0, sizeof(*t));
        t->gui_slot = -1;
        t->used = 1;
        t->id = next_term_id++;
        strncpy(t->title, title ? title : "Terminal", sizeof(t->title) - 1);
        t->owner_pid = -1;
        t->win_present_pid = -1;
        int sw = vbe_available() ? (int)vbe_width() : 1920;
        int sh = vbe_available() ? (int)vbe_height() : 1080;
        int cols = 80, rows = 24;
        int max_cols = (sw - 40 - TERM_BORDER * 2) / FB_CELL_W;
        int max_rows = (sh - TASKBAR_H - 40 - TERM_TITLE_H - TERM_BORDER) / FB_CELL_H;
        if (cols > max_cols) cols = max_cols;
        if (rows > max_rows) rows = max_rows;
        if (cols < 40) cols = 40;
        if (rows < 10) rows = 10;
        if (cols > CON_MAX_COLS) cols = CON_MAX_COLS;
        if (rows > CON_MAX_ROWS) rows = CON_MAX_ROWS;
        term_reset(t, cols, rows);
        t->win_open = 1;
        t->win_minimized = 0;
        /* cascade position */
        int off = (t->id % 10) * 28;
        t->win_x = 60 + off;
        t->win_y = 20 + off;
        if (t->win_x + term_win_w(t) > sw - 10) t->win_x = 60;
        if (t->win_y + term_win_h(t) > sh - TASKBAR_H - 10) t->win_y = 20;
        z_add(t);
        return t;
    }
    return 0;
}

void *console_create_term(const char *title) {
    term_t *t = alloc_term(title);
    if (t) { focused_term = t; z_bring_front(t); }
    return t;
}

void *console_default_term(void) {
    if (focused_term && focused_term->used) return focused_term;
    for (int i = 0; i < MAX_TERMS; i++) if (terms[i].used) return &terms[i];
    return 0;
}

void console_focus_term(void *term) {
    term_t *t = (term_t *)term;
    if (!t || !t->used) return;
    if (t->win_minimized) { t->win_minimized = 0; t->win_open = 1; }
    if (!t->win_open) t->win_open = 1;
    focused_term = t;
    z_bring_front(t);
    if (fbcon_on) compose_all();
}

void console_close_term(void *term) {
    term_t *t = (term_t *)term;
    if (!t || !t->used) return;
    if (t->win_present_pid > 0) {
        proc_t *p = process_find(t->win_present_pid);
        if (p) process_kill(p);
        t->win_present_pid = -1;
    }
    if (t->owner_pid > 0) {
        proc_t *p = process_find(t->owner_pid);
        if (p) process_kill(p);
    }
    for (int i = 0; i < PROC_MAX; i++) {
        if (procs[i].used && procs[i].term == t) {
            procs[i].term = 0;
            if (procs[i].state != PROC_ZOMBIE) process_kill(&procs[i]);
        }
    }
    t->win_open = 0;
    t->win_minimized = 0;
    t->gui_mode = 0;
    gui_slot_put(t);
    t->used = 0;
    z_remove(t);
    if (focused_term == t) focused_term = top_visible_term();
    if (fbcon_on) compose_all();
}

void console_set_term_owner(void *term, int pid) {
    term_t *t = (term_t *)term;
    if (t && t->used) t->owner_pid = pid;
}

void console_notify_process_exit(int pid) {
    for (int i = 0; i < MAX_TERMS; i++) {
        term_t *t = &terms[i];
        if (!t->used) continue;
        if (t->win_present_pid == pid) t->win_present_pid = -1;
        if (t->owner_pid == pid) {
            int others = 0;
            for (int j = 0; j < PROC_MAX; j++) {
                if (procs[j].used && procs[j].pid != pid && procs[j].term == t && procs[j].state != PROC_ZOMBIE)
                    others++;
            }
            if (!others) {
                t->win_open = 0;
                t->win_minimized = 0;
                t->gui_mode = 0;
                gui_slot_put(t);
                t->used = 0;
                z_remove(t);
                if (focused_term == t) focused_term = top_visible_term();
                if (fbcon_on) compose_all();
            } else {
                t->owner_pid = -1;
            }
        }
    }
}

int console_spawn_in_new_term(const char *path, char *const argv[], int argc) {
    term_t *t = alloc_term(path);
    if (!t) return -1;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    const char *pretty = base;
    if (!strcmp(base, "sh")) pretty = "MS-DOS Prompt";
    else if (!strcmp(base, "badapple")) pretty = "Bad Apple";
    else if (!strcmp(base, "files")) pretty = "My Computer";
    else if (!strcmp(base, "edit")) pretty = "Untitled - Notepad";
    else if (!strcmp(base, "doom")) pretty = "Doom";
    else if (!strcmp(base, "fire")) pretty = "Fire";
    else if (!strcmp(base, "cmatrix")) pretty = "Matrix";
    else if (!strcmp(base, "glgears")) pretty = "3D Gears";
    else if (!strcmp(base, "ie")) pretty = "Internet Explorer";
    strncpy(t->title, pretty, sizeof(t->title) - 1);
    t->title[sizeof(t->title)-1] = 0;
    focused_term = t;
    z_bring_front(t);
    proc_t *p = process_create(path, argv, argc, current);
    if (!p) {
        t->used = 0; z_remove(t);
        if (focused_term == t) focused_term = top_visible_term();
        return -1;
    }
    p->term = t;
    t->owner_pid = p->pid;
    if (fbcon_on) compose_all();
    return p->pid;
}

void console_init(void) {
    fbcon_on = 0;
    memset(terms, 0, sizeof(terms));
    zcount = 0;
    focused_term = 0;
    /* create a default kernel term for early boot messages */
    term_t *t = alloc_term("Terminal");
    if (t) {
        t->owner_pid = -1;
        focused_term = t;
    }
}

void console_clear(void) {
    term_t *t = term_for_current();
    if (!t) return;
    t->sel_active = t->sel_drag = 0; t->sel_anchor = t->sel_end = -1;
    uint16_t blank = (uint16_t)(' ' | (t->cur_attr << 8));
    for (int i = 0; i < t->cols * t->rows; i++) t->cells[i] = blank;
    t->cx = t->cy = 0;
    t->fb_cur_x = t->fb_cur_y = -1;
    if (fbcon_on && t->win_open) {
        if (t == top_visible_term()) {
            mcur_hide();
            term_draw_cells_rect(t, 0, 0, t->cols, t->rows);
            term_draw_cursor(t);
            mcur_show();
            fb_present_rect(term_client_x(t), term_client_y(t), term_client_w(t), term_client_h(t));
        } else {
            term_mark_all_dirty(t);
        }
    }
}

void console_putc(char c) {
    term_t *t = term_for_current();
    if (!t) return;
    term_putc(t, c);
    if (t->present_suppress <= 0) term_present_if_needed(t);
}

void console_write(const char *s, size_t n) {
    term_t *t = term_for_current();
    if (!t) return;
    term_write(t, s, n);
}

void console_puts(const char *s) {
    term_t *t = term_for_current();
    if (!t) return;
    t->present_suppress++;
    while (*s) term_putc(t, *s++);
    t->present_suppress--;
    if (t->present_suppress <= 0) { t->present_suppress = 0; term_present_if_needed(t); }
}

void console_gotoxy(int x, int y) {
    term_t *t = term_for_current();
    if (!t) return;
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x >= t->cols) x = t->cols - 1;
    if (y >= t->rows) y = t->rows - 1;
    t->cx = x; t->cy = y;
}

void console_set_attr(uint8_t attr) {
    term_t *t = term_for_current();
    if (t) t->cur_attr = attr;
}

uint8_t console_get_attr(void) {
    term_t *t = term_for_current();
    return t ? t->cur_attr : 0x07;
}

int console_cols(void) {
    term_t *t = term_for_current();
    return t ? t->cols : CON_COLS;
}

int console_rows(void) {
    term_t *t = term_for_current();
    return t ? t->rows : CON_ROWS;
}

void console_save_cursor(void) {
    term_t *t = term_for_current();
    if (t) { t->saved_x = t->cx; t->saved_y = t->cy; }
}

void console_restore_cursor(void) {
    term_t *t = term_for_current();
    if (t) { t->cx = t->saved_x; t->cy = t->saved_y; }
}

void console_cursor_visible(int on) {
    term_t *t = term_for_current();
    if (t) { t->cursor_on = on; }
}

void console_input_push(uint8_t c) {
    term_t *t = focused_term;
    if (!t || !t->used) t = term_for_current();
    if (!t) return;
    uint32_t flags = read_eflags();
    cpu_cli();
    int next = (t->in_head + 1) % TERM_INBUF_SIZE;
    if (next != t->in_tail) {
        t->inbuf[t->in_head] = c;
        t->in_head = next;
    }
    if (flags & 0x200) cpu_sti();
}

int console_input_get(void) {
    term_t *t = term_for_current();
    if (!t) return -1;
    uint32_t flags = read_eflags();
    cpu_cli();
    int r = -1;
    if (t->in_tail != t->in_head) {
        r = t->inbuf[t->in_tail];
        t->in_tail = (t->in_tail + 1) % TERM_INBUF_SIZE;
    }
    if (flags & 0x200) cpu_sti();
    return r;
}

int console_input_available(void) {
    term_t *t = term_for_current();
    if (!t) return 0;
    return t->in_head != t->in_tail;
}

void console_input_flush(void) {
    term_t *t = term_for_current();
    if (!t) return;
    uint32_t flags = read_eflags();
    cpu_cli();
    t->in_tail = t->in_head;
    if (flags & 0x200) cpu_sti();
}

void console_load_wallpaper(void) {
    /* Windows 95 style solid teal desktop; wallpapers are not used. */
}

void console_attach_fb(void) {
    if (!vbe_available() || !vbe_framebuffer()) return;
    bbuf = backbuf;
    fbcon_on = 1;
    mcur_saved = 0;
    mcur_x = (int)vbe_width() / 2;
    mcur_y = (int)vbe_height() / 2;
    start_menu_open = 0;
    start_menu_dragging = 0;
    start_menu_x = 6;
    start_menu_y = (int)vbe_height() - TASKBAR_H - MENU_H - 2;

    /* ensure at least one term exists and size it */
    if (!focused_term) {
        term_t *t = alloc_term("MS-DOS Prompt");
        focused_term = t;
    }
    for (int i = 0; i < MAX_TERMS; i++) {
        term_t *t = &terms[i];
        if (!t->used) continue;
        /* resize to fit screen */
        int sw = (int)vbe_width(), sh = (int)vbe_height();
        int cols = t->cols > 0 ? t->cols : 80;
        int rows = t->rows > 0 ? t->rows : 24;
        int max_cols = (sw - 40 - TERM_BORDER * 2) / FB_CELL_W;
        int max_rows = (sh - TASKBAR_H - 40 - TERM_TITLE_H - TERM_BORDER) / FB_CELL_H;
        if (max_cols > CON_MAX_COLS) max_cols = CON_MAX_COLS;
        if (max_rows > CON_MAX_ROWS) max_rows = CON_MAX_ROWS;
        if (cols > max_cols) cols = max_cols;
        if (rows > max_rows) rows = max_rows;
        if (cols < 40) cols = 40;
        if (rows < 10) rows = 10;
        if (cols != t->cols || rows != t->rows) {
            /* simple resize: preserve top-left content */
            uint16_t old[CON_MAX_COLS * CON_MAX_ROWS];
            int oc = t->cols, or_ = t->rows;
            memcpy(old, t->cells, sizeof(old));
            uint16_t blank = (uint16_t)(' ' | (t->cur_attr << 8));
            for (int k = 0; k < CON_MAX_COLS * CON_MAX_ROWS; k++) t->cells[k] = blank;
            t->cols = cols; t->rows = rows;
            int cc = oc < cols ? oc : cols;
            int rc = or_ < rows ? or_ : rows;
            for (int y = 0; y < rc; y++)
                for (int x = 0; x < cc; x++)
                    t->cells[y * cols + x] = old[y * oc + x];
            if (t->cx >= cols) t->cx = cols - 1;
            if (t->cy >= rows) t->cy = rows - 1;
        }
        if (t->win_x + term_win_w(t) > sw) t->win_x = sw - term_win_w(t) - 10;
        if (t->win_y + term_win_h(t) > sh - TASKBAR_H) t->win_y = sh - TASKBAR_H - term_win_h(t) - 10;
        if (t->win_x < 0) t->win_x = 0;
        if (t->win_y < 0) t->win_y = 0;
    }

    draw_wallpaper_full();
    compose_all();
}

void console_blit(int x, int y, int w, int h, const uint16_t *cells) {
    term_t *t = term_for_current();
    if (!t) return;
    uint32_t flags = read_eflags();
    cpu_cli();
    mcur_hide();
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= t->rows) continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= t->cols) continue;
            t->cells[yy * t->cols + xx] = cells[j * w + i];
        }
    }
    if (t->win_open && t == top_visible_term()) {
        term_draw_cells_rect(t, x, y, x + w, y + h);
        t->fb_cur_x = t->fb_cur_y = -1;
        term_draw_cursor(t);
        if (start_menu_open) draw_start_menu_window();
        mcur_show();
        fb_present_rect(term_client_x(t) + x * FB_CELL_W, term_client_y(t) + y * FB_CELL_H,
                        w * FB_CELL_W, h * FB_CELL_H);
    } else {
        term_mark_dirty(t, x, y, x + w, y + h);
        mcur_show();
    }
    if (flags & 0x200) cpu_sti();
}

void console_win_present(const uint32_t *pixels, int src_w, int src_h) {
    term_t *t = term_from_proc(current);
    if (!t) t = term_for_current();
    if (!t) return;
    if (!pixels || src_w <= 0 || src_h <= 0 || !bbuf || !fbcon_on) return;
    if (current) t->win_present_pid = current->pid;
    if (t->win_dragging) return;
    if (!t->win_open) return;

    uint32_t flags = read_eflags();
    cpu_cli();
    mcur_hide();

    int sw = (int)vbe_width();
    int sh = (int)vbe_height();
    int can_gui = t->gui_mode || (current && t->owner_pid == current->pid);
    if (!can_gui) {
        /* legacy: scale into text client */
        int cw = term_client_w(t), ch = term_client_h(t);
        int cx0 = term_client_x(t), cy0 = term_client_y(t);
        int scale = cw / src_w, scaleh = ch / src_h;
        if (scaleh < scale) scale = scaleh;
        if (scale < 1) scale = 1;
        int dw = src_w * scale, dh = src_h * scale;
        int ox = cx0 + (cw - dw) / 2, oy = cy0 + (ch - dh) / 2;
        for (int y = 0; y < ch; y++) {
            int py = cy0 + y; if (py < 0 || py >= sh) continue;
            uint32_t *row = &bbuf[py * sw]; int x0 = cx0 < 0 ? 0 : cx0, x1 = cx0 + cw > sw ? sw : cx0 + cw;
            for (int x = x0; x < x1; x++) row[x] = 0;
        }
        for (int y = 0; y < src_h; y++) for (int sy2 = 0; sy2 < scale; sy2++) {
            int dy = oy + y * scale + sy2; if (dy < cy0 || dy >= cy0 + ch || dy < 0 || dy >= sh) continue;
            uint32_t *dst = &bbuf[dy * sw]; const uint32_t *src = &pixels[y * src_w];
            for (int x = 0; x < src_w; x++) { uint32_t px = src[x]; int bx = ox + x * scale;
                for (int sx2 = 0; sx2 < scale; sx2++) { int dx = bx + sx2; if (dx >= cx0 && dx < cx0 + cw && dx >= 0 && dx < sw) dst[dx] = px; } }
        }
        if (t == top_visible_term()) { if (start_menu_open) { menu_capture_bg_at(start_menu_x, start_menu_y); draw_start_menu_window(); } mcur_show(); fb_present_rect(cx0, cy0, cw, ch); }
        else mcur_show();
        if (flags & 0x200) cpu_sti();
        return;
    }

    int max_w = sw - 40 - TERM_BORDER * 2;
    int max_h = sh - TASKBAR_H - 40 - TERM_TITLE_H - TERM_BORDER;
    if (max_w < 160) max_w = 160;
    if (max_h < 120) max_h = 120;
    int gw = src_w, gh = src_h;
    if (gw > max_w) gw = max_w;
    if (gh > max_h) gh = max_h;
    if (gw > GUI_MAX_W) gw = GUI_MAX_W;
    if (gh > GUI_MAX_H) gh = GUI_MAX_H;
    if (gw < 160) gw = 160;
    if (gh < 120) gh = 120;

    int need_resize = (!t->gui_mode || t->gui_w != gw || t->gui_h != gh || !t->gui_buf);
    if (need_resize) {
        uint32_t *nbuf = gui_slot_get(t);
        if (!nbuf) {
            /* all slots busy: keep the previous buffer if we already own one */
            if (!t->gui_buf) { mcur_show(); if (flags & 0x200) cpu_sti(); return; }
            gw = t->gui_w; gh = t->gui_h;
        } else {
            t->gui_buf = nbuf;
            t->gui_w = gw; t->gui_h = gh;
            t->gui_mode = 1;
        }
        if (t->win_x + term_win_w(t) > sw) t->win_x = sw - term_win_w(t) - 4;
        if (t->win_y + term_win_h(t) > sh - TASKBAR_H) t->win_y = sh - TASKBAR_H - term_win_h(t) - 4;
        if (t->win_x < 0) t->win_x = 0;
        if (t->win_y < 0) t->win_y = 0;
    }

    /* copy/scale into gui buffer */
    for (int y = 0; y < gh; y++) {
        int sy = (src_h == gh) ? y : (y * src_h / gh);
        if (sy >= src_h) sy = src_h - 1;
        uint32_t *dst = &t->gui_buf[y * gw];
        const uint32_t *src = &pixels[sy * src_w];
        if (src_w == gw) {
            for (int x = 0; x < gw; x++) dst[x] = src[x];
        } else {
            for (int x = 0; x < gw; x++) {
                int sx = x * src_w / gw;
                if (sx >= src_w) sx = src_w - 1;
                dst[x] = src[sx];
            }
        }
    }

    if (need_resize) {
        compose_all();
        if (flags & 0x200) cpu_sti();
        return;
    }

    if (t == top_visible_term()) {
        int cx0 = term_client_x(t), cy0 = term_client_y(t);
        for (int y = 0; y < gh; y++) {
            int py = cy0 + y;
            if (py < 0 || py >= sh) continue;
            int x0 = cx0 < 0 ? 0 : cx0;
            int x1 = cx0 + gw > sw ? sw : cx0 + gw;
            if (x1 > x0)
                copy32(&bbuf[py * sw + x0], &t->gui_buf[y * gw + (x0 - cx0)], x1 - x0);
        }
        if (start_menu_open) {
            menu_update_bg_rect(cx0, cy0, cx0 + gw, cy0 + gh);
            draw_start_menu_window();
        }
        mcur_show();
        fb_present_rect(cx0, cy0, gw, gh);
    } else {
        mcur_show();
    }
    if (flags & 0x200) cpu_sti();
}

int console_win_info(sp_wininfo_t *out) {
    if (!out) return -1;
    term_t *t = term_for_current();
    if (!t) return -1;
    out->x = term_client_x(t);
    out->y = term_client_y(t);
    out->w = term_client_w(t);
    out->h = term_client_h(t);
    out->screen_w = (int)vbe_width();
    out->screen_h = (int)vbe_height();
    out->win_x = t->win_x;
    out->win_y = t->win_y;
    out->win_w = term_win_w(t);
    out->win_h = term_win_h(t);
    return 0;
}

/* ---------------- mouse / interaction ---------------- */

static term_t *window_at(int px, int py) {
    for (int i = zcount - 1; i >= 0; i--) {
        term_t *t = zterms[i];
        if (!t->used || !t->win_open) continue;
        if (point_in_rect(px, py, t->win_x, t->win_y, term_win_w(t), term_win_h(t))) return t;
    }
    return 0;
}

static void term_button_geom(term_t *t, int *cbx, int *cby, int *mbx, int *mby, int *xbx, int *bbw, int *bbh) {
    int w = term_win_w(t);
    *bbh = TERM_BTN_H; *bbw = TERM_BTN_W;
    *cby = t->win_y + 3;
    *mby = *cby;
    *cbx = t->win_x + w - *bbw - 3;
    *xbx = *cbx - *bbw - 2;
    *mbx = *xbx - *bbw - 2;
}

static int term_title_hit(term_t *t, int px, int py) {
    if (!t || !t->win_open) return 0;
    int cbx, cby, mbx, mby, xbx, bbw, bbh;
    term_button_geom(t, &cbx, &cby, &mbx, &mby, &xbx, &bbw, &bbh);
    if (point_in_rect(px, py, cbx, cby, bbw, bbh)) return 0;
    if (point_in_rect(px, py, xbx, cby, bbw, bbh)) return 0;
    if (point_in_rect(px, py, mbx, mby, bbw, bbh)) return 0;
    return point_in_rect(px, py, t->win_x, t->win_y, term_win_w(t), TERM_TITLE_H);
}

static int term_close_hit(term_t *t, int px, int py) {
    if (!t || !t->win_open) return 0;
    int cbx, cby, mbx, mby, xbx, bbw, bbh;
    term_button_geom(t, &cbx, &cby, &mbx, &mby, &xbx, &bbw, &bbh);
    (void)mbx; (void)mby; (void)xbx;
    return point_in_rect(px, py, cbx, cby, bbw, bbh);
}

static int term_min_hit(term_t *t, int px, int py) {
    if (!t || !t->win_open) return 0;
    int cbx, cby, mbx, mby, xbx, bbw, bbh;
    term_button_geom(t, &cbx, &cby, &mbx, &mby, &xbx, &bbw, &bbh);
    (void)cbx; (void)cby; (void)xbx;
    return point_in_rect(px, py, mbx, mby, bbw, bbh);
}

static int term_max_hit(term_t *t, int px, int py) {
    if (!t || !t->win_open) return 0;
    int cbx, cby, mbx, mby, xbx, bbw, bbh;
    term_button_geom(t, &cbx, &cby, &mbx, &mby, &xbx, &bbw, &bbh);
    (void)cbx; (void)cby; (void)mbx; (void)mby;
    return point_in_rect(px, py, xbx, cby, bbw, bbh);
}

static void term_toggle_max(term_t *t) {
    if (!t || !t->win_open) return;
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    if (!t->win_maximized) {
        t->win_restore_x = t->win_x;
        t->win_restore_y = t->win_y;
        if (!t->gui_mode) {
            t->win_restore_cols = t->cols;
            t->win_restore_rows = t->rows;
            int cols = (sw - TERM_BORDER * 2) / FB_CELL_W;
            int rows = (sh - TASKBAR_H - TERM_TITLE_H - TERM_BORDER) / FB_CELL_H;
            term_set_grid(t, cols, rows);
        }
        t->win_x = 0;
        t->win_y = 0;
        t->win_maximized = 1;
    } else {
        if (!t->gui_mode && t->win_restore_cols > 0)
            term_set_grid(t, t->win_restore_cols, t->win_restore_rows);
        t->win_x = t->win_restore_x;
        t->win_y = t->win_restore_y;
        t->win_maximized = 0;
    }
}

static int term_client_hit(term_t *t, int px, int py, int *outcx, int *outcy) {
    if (!t || !t->win_open) return 0;
    if (t->gui_mode) return 0;
    int ox = term_client_x(t), oy = term_client_y(t);
    int cw = t->cols * FB_CELL_W, ch = t->rows * FB_CELL_H;
    if (px < ox || py < oy || px >= ox + cw || py >= oy + ch) return 0;
    int cc = (px - ox) / FB_CELL_W;
    int cr = (py - oy) / FB_CELL_H;
    if (cc < 0) cc = 0; if (cr < 0) cr = 0;
    if (cc >= t->cols) cc = t->cols - 1;
    if (cr >= t->rows) cr = t->rows - 1;
    *outcx = cc; *outcy = cr;
    return 1;
}

static int start_menu_hit(int px, int py) {
    return start_menu_open && point_in_rect(px, py, start_menu_x, start_menu_y, MENU_W, MENU_H);
}

static int start_menu_index(int px, int py) {
    if (!start_menu_open) return -1;
    if (px < start_menu_x || px >= start_menu_x + MENU_W) return -1;
    int local_y = py - start_menu_y - MENU_TITLE_H;
    if (local_y < 0) return -1;
    int idx = local_y / MENU_ITEM_H;
    if (idx < 0 || idx >= START_MENU_COUNT) return -1;
    return idx;
}

static void term_redraw_selection(term_t *t, int a, int b) {
    if (!t || !t->win_open || t->gui_mode) return;
    if (a > b) { int tmp = a; a = b; b = tmp; }
    if (a < 0) a = 0;
    int max = t->cols * t->rows - 1;
    if (b > max) b = max;
    int r0 = a / t->cols, r1 = b / t->cols;
    for (int y = r0; y <= r1; y++) {
        int x0 = (y == r0) ? (a % t->cols) : 0;
        int x1 = (y == r1) ? (b % t->cols) : (t->cols - 1);
        for (int x = x0; x <= x1; x++) term_draw_cell_at(t, x, y);
    }
    if (t == top_visible_term()) {
        int px0 = term_client_x(t);
        int py0 = term_client_y(t) + r0 * FB_CELL_H;
        int ph = (r1 - r0 + 1) * FB_CELL_H;
        mcur_hide();
        term_draw_cursor(t);
        mcur_show();
        fb_present_rect(px0, py0, t->cols * FB_CELL_W, ph);
    }
}

static void clamp_menu(void) {
    int sw = (int)vbe_width(), sh = (int)vbe_height();
    int desk_h = sh - TASKBAR_H;
    if (start_menu_x < 0) start_menu_x = 0;
    if (start_menu_y < 0) start_menu_y = 0;
    if (start_menu_x + MENU_W > sw) start_menu_x = sw - MENU_W;
    if (start_menu_y + MENU_H > desk_h) start_menu_y = desk_h - MENU_H;
    if (start_menu_x < 0) start_menu_x = 0;
    if (start_menu_y < 0) start_menu_y = 0;
}

static void do_compose(void) {
    mcur_hide();
    compose_all();
}

static void launch_menu_item(int idx) {
    if (idx < 0 || idx >= START_MENU_COUNT) return;
    const menu_item_t *it = &start_menu_items[idx];
    start_menu_open = 0;
    start_menu_dragging = 0;
    if (it->path) {
        char *argv[2];
        argv[0] = (char *)it->path;
        argv[1] = 0;
        console_spawn_in_new_term(it->path, argv, 1);
    } else if (it->cmd) {
        if (strcmp(it->label, "Terminal") == 0) {
            char *argv[2]; argv[0] = "/bin/sh"; argv[1] = 0;
            console_spawn_in_new_term("/bin/sh", argv, 1);
        } else {
            term_t *t = focused_term ? focused_term : term_for_current();
            if (t) {
                if (t->win_minimized) { t->win_minimized = 0; t->win_open = 1; focused_term = t; z_bring_front(t); }
                for (int i = 0; it->cmd[i]; i++) console_input_push((uint8_t)it->cmd[i]);
                if (fbcon_on) compose_all();
            }
        }
    } else {
        char *argv[2]; argv[0] = "/bin/sh"; argv[1] = 0;
        console_spawn_in_new_term("/bin/sh", argv, 1);
    }
}

void console_mouse_update(int x, int y, int buttons) {
    static int prev_buttons;
    if (!fbcon_on || !vbe_available() || !bbuf) return;

    int sw = (int)vbe_width(), sh = (int)vbe_height();
    if (x < 0) x = 0; if (y < 0) y = 0;
    if (x >= sw) x = sw - 1;
    if (y >= sh) y = sh - 1;

    uint32_t flags = read_eflags();
    cpu_cli();

    int left = buttons & 1;
    int prev_left = prev_buttons & 1;
    int in_start_btn = point_in_rect(x, y, start_btn_x, start_btn_y, start_btn_w, start_btn_h);

    term_t *top = top_visible_term();
    term_t *hit_win = window_at(x, y);
    term_t *drag_term = 0;
    for (int i = 0; i < MAX_TERMS; i++) if (terms[i].used && terms[i].win_dragging) drag_term = &terms[i];

    int menu_hit = start_menu_hit(x, y);
    int need_compose = 0;

    /* Handle ongoing menu drag */
    if (left && start_menu_dragging) {
        int old_mx = start_menu_x, old_my = start_menu_y;
        start_menu_x = x - start_menu_drag_dx;
        start_menu_y = y - start_menu_drag_dy;
        clamp_menu();
        mcur_hide();
        mcur_x = x; mcur_y = y;
        draw_wallpaper_rect(old_mx, old_my, MENU_OUTER_W, MENU_OUTER_H);
        draw_desktop_icons();
        /* redraw windows that may be under old menu */
        for (int i = 0; i < zcount; i++) {
            term_t *t = zterms[i];
            if (!t->used || !t->win_open) continue;
            if (rects_intersect(t->win_x, t->win_y, term_outer_w(t), term_outer_h(t),
                                old_mx, old_my, MENU_OUTER_W, MENU_OUTER_H))
                term_draw_window(t, t == focused_term);
        }
        draw_taskbar();
        menu_capture_bg_at(start_menu_x, start_menu_y);
        draw_start_menu_window();
        mcur_show();
        fb_present();
        prev_buttons = buttons;
        if (flags & 0x200) cpu_sti();
        return;
    }

    /* Handle ongoing window drag */
    if (left && drag_term) {
        int old_tx = drag_term->win_x, old_ty = drag_term->win_y;
        int old_tw = term_win_w(drag_term), old_th = term_win_h(drag_term);
        int new_tx, new_ty;
        int on_screen = old_tx >= 0 && old_ty >= 0 && old_tx + old_tw <= sw && old_ty + old_th <= sh - TASKBAR_H;
        if (on_screen && drag_term == top && term_drag_target(drag_term, x, y, &new_tx, &new_ty)) {
            if (new_tx != old_tx || new_ty != old_ty) {
                int old_mx = mcur_x, old_my = mcur_y;
                mcur_hide();
                draw_wallpaper_rect(old_tx + old_tw, old_ty, UI_SHADOW_PAD, old_th + UI_SHADOW_PAD);
                draw_wallpaper_rect(old_tx, old_ty + old_th, old_tw + UI_SHADOW_PAD, UI_SHADOW_PAD);
                bbuf_move_rect(new_tx, new_ty, old_tx, old_ty, old_tw, old_th);
                draw_wallpaper_rect_minus(old_tx, old_ty, old_tw, old_th, new_tx, new_ty, old_tw, old_th);
                draw_desktop_icons();
                draw_window_shadow(new_tx, new_ty, old_tw, old_th, WIN_RADIUS);
                /* redraw windows below if exposed? top window drag means others below, draw them in exposed area */
                for (int i = 0; i < zcount; i++) {
                    term_t *t = zterms[i];
                    if (!t->used || !t->win_open || t == drag_term) continue;
                    if (rects_intersect(t->win_x, t->win_y, term_outer_w(t), term_outer_h(t),
                                        old_tx, old_ty, old_tw + UI_SHADOW_PAD, old_th + UI_SHADOW_PAD))
                        term_draw_window(t, t == focused_term);
                }
                /* redraw dragged window on top */
                drag_term->win_x = new_tx; drag_term->win_y = new_ty;
                term_draw_window(drag_term, 1);
                if (start_menu_open) draw_start_menu_window();
                draw_taskbar();
                mcur_x = x; mcur_y = y;
                mcur_show();
                present_rect_minus(old_tx, old_ty, old_tw + UI_SHADOW_PAD, old_th + UI_SHADOW_PAD,
                                   new_tx, new_ty, old_tw + UI_SHADOW_PAD, old_th + UI_SHADOW_PAD);
                fb_present_rect(new_tx, new_ty, old_tw + UI_SHADOW_PAD, old_th + UI_SHADOW_PAD);
                fb_present_rect(old_mx, old_my, MCUR_W, MCUR_H);
                fb_present_rect(mcur_x, mcur_y, MCUR_W, MCUR_H);
                prev_buttons = buttons;
                if (flags & 0x200) cpu_sti();
                return;
            }
        } else {
            /* fallback: full compose on move */
            int nx = x - drag_term->win_drag_dx;
            int ny = y - drag_term->win_drag_dy;
            if (nx < -term_win_w(drag_term) + 40) nx = -term_win_w(drag_term) + 40;
            if (ny < 0) ny = 0;
            if (nx > sw - 40) nx = sw - 40;
            if (ny > sh - TASKBAR_H - TERM_TITLE_H) ny = sh - TASKBAR_H - TERM_TITLE_H;
            drag_term->win_x = nx; drag_term->win_y = ny;
            mcur_hide();
            mcur_x = x; mcur_y = y;
            compose_all();
            prev_buttons = buttons;
            if (flags & 0x200) cpu_sti();
            return;
        }
    }

    if (left && !prev_left) {
        if (in_start_btn) {
            start_menu_open = !start_menu_open;
            if (start_menu_open) { start_menu_x = 6; start_menu_y = sh - TASKBAR_H - MENU_H - 2; clamp_menu(); }
            for (int i = 0; i < MAX_TERMS; i++) terms[i].win_dragging = 0;
            need_compose = 1;
        } else if (menu_hit) {
            int mi = start_menu_index(x, y);
            int local_y = y - start_menu_y;
            if (local_y >= 0 && local_y < MENU_TITLE_H) {
                start_menu_dragging = 1;
                start_menu_drag_dx = x - start_menu_x;
                start_menu_drag_dy = y - start_menu_y;
            } else if (mi >= 0) {
                launch_menu_item(mi);
                prev_buttons = buttons;
                if (flags & 0x200) cpu_sti();
                return;
            }
        } else {
            if (start_menu_open) { start_menu_open = 0; start_menu_dragging = 0; need_compose = 1; }
            term_t *tb = taskbar_term_at(x, y);
            if (tb) {
                if (tb->win_minimized) {
                    tb->win_minimized = 0; tb->win_open = 1; focused_term = tb; z_bring_front(tb);
                } else if (tb == focused_term && tb->win_open) {
                    tb->win_open = 0; tb->win_minimized = 1;
                    if (focused_term == tb) focused_term = top_visible_term();
                } else {
                    tb->win_open = 1; focused_term = tb; z_bring_front(tb);
                }
                need_compose = 1;
            } else if (hit_win) {
                /* check close/min on the hit window (even if not top, bring to front on click?) */
                if (term_close_hit(hit_win, x, y)) {
                    console_close_term(hit_win);
                    prev_buttons = buttons;
                    if (flags & 0x200) cpu_sti();
                    return;
                } else if (term_min_hit(hit_win, x, y)) {
                    hit_win->win_open = 0; hit_win->win_minimized = 1; hit_win->win_dragging = 0;
                    if (focused_term == hit_win) focused_term = top_visible_term();
                    need_compose = 1;
                } else if (term_max_hit(hit_win, x, y)) {
                    focused_term = hit_win; z_bring_front(hit_win);
                    term_toggle_max(hit_win);
                    need_compose = 1;
                } else if (term_title_hit(hit_win, x, y)) {
                    focused_term = hit_win; z_bring_front(hit_win);
                    hit_win->win_maximized = 0;
                    hit_win->win_dragging = 1;
                    hit_win->win_drag_dx = x - hit_win->win_x;
                    hit_win->win_drag_dy = y - hit_win->win_y;
                    need_compose = 1;
                } else {
                    int cc, cr;
                    focused_term = hit_win; z_bring_front(hit_win);
                    if (term_client_hit(hit_win, x, y, &cc, &cr)) {
                        int idxc = cr * hit_win->cols + cc;
                        int oa = hit_win->sel_anchor, ob = hit_win->sel_end, oactive = hit_win->sel_active;
                        hit_win->sel_active = 1; hit_win->sel_drag = 1;
                        hit_win->sel_anchor = idxc; hit_win->sel_end = idxc;
                        if (oactive) term_redraw_selection(hit_win, oa, ob);
                        term_redraw_selection(hit_win, hit_win->sel_anchor, hit_win->sel_end);
                    }
                    need_compose = 1;
                }
            } else {
                /* click on desktop */
                for (int i = 0; i < MAX_TERMS; i++) terms[i].win_dragging = 0;
                static int last_desk_click = -1;
                static uint32_t last_desk_tick = 0;
                int icon_hit = -1;
                for (int di = 0; di < DESK_ICON_COUNT; di++) {
                    const desk_icon_t *ic = &desk_icons[di];
                    if (point_in_rect(x, y, ic->x - 12, ic->y - 8, 56, 74) ||
                        point_in_rect(x, y, ic->x - 60, ic->y + 34, 160, FB_CELL_H + 4)) { icon_hit = di; break; }
                }
                if (desk_sel != icon_hit) { desk_sel = icon_hit; need_compose = 1; }
                if (icon_hit >= 0) {
                    uint32_t now = ticks_get();
                    if (last_desk_click == icon_hit && (now - last_desk_tick) < 36) {
                        char *argv[2]; argv[0] = (char *)desk_icons[icon_hit].path; argv[1] = 0;
                        console_spawn_in_new_term(desk_icons[icon_hit].path, argv, 1);
                        last_desk_click = -1;
                        last_desk_tick = 0;
                        prev_buttons = buttons;
                        if (flags & 0x200) cpu_sti();
                        return;
                    }
                    last_desk_click = icon_hit;
                    last_desk_tick = now;
                    need_compose = 1;
                } else {
                    last_desk_click = -1;
                }
            }
        }
    } else if (left && !start_menu_dragging && !drag_term) {
        /* selection drag in focused term */
        term_t *t = focused_term;
        if (t && t->used && t->win_open && t->sel_drag) {
            int cc, cr;
            if (term_client_hit(t, x, y, &cc, &cr)) {
                int idxc = cr * t->cols + cc;
                if (idxc != t->sel_end) {
                    int old_end = t->sel_end;
                    t->sel_end = idxc;
                    int lo = t->sel_anchor < old_end ? t->sel_anchor : old_end;
                    if (t->sel_end < lo) lo = t->sel_end;
                    int hi = t->sel_anchor > old_end ? t->sel_anchor : old_end;
                    if (t->sel_end > hi) hi = t->sel_end;
                    term_redraw_selection(t, lo, hi);
                }
            }
        }
    } else if (!left) {
        if (prev_left) {
            if (drag_term) {
                drag_term->win_dragging = 0;
                need_compose = 1;
            }
            if (start_menu_dragging) { start_menu_dragging = 0; need_compose = 1; }
            for (int i = 0; i < MAX_TERMS; i++) if (terms[i].used) terms[i].sel_drag = 0;
        }
    }

    prev_buttons = buttons;

    if (need_compose) {
        mcur_hide();
        mcur_x = x; mcur_y = y;
        compose_all();
    } else if (x != mcur_x || y != mcur_y || !mcur_saved) {
        int old_mx = mcur_x, old_my = mcur_y;
        mcur_hide();
        mcur_x = x; mcur_y = y;
        if (start_menu_open) {
            /* hover highlight - redraw menu */
            draw_start_menu_window();
        }
        mcur_show();
        if (start_menu_open)
            fb_present_rect(start_menu_x, start_menu_y, MENU_OUTER_W, MENU_OUTER_H);
        fb_present_rect(old_mx, old_my, MCUR_W, MCUR_H);
        fb_present_rect(mcur_x, mcur_y, MCUR_W, MCUR_H);
    }

    if (flags & 0x200) cpu_sti();
}


int vga_get_tty_size(int *cols, int *rows) {
    term_t *t = term_for_current();
    if (!t) { if (cols) *cols = 80; if (rows) *rows = 25; return 0; }
    if (cols) *cols = t->cols;
    if (rows) *rows = t->rows;
    return 0;
}
