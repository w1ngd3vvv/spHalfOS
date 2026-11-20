/* spHalfOS ui -- framebuffer terminal window: typing, rendering, visual cursor */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <syscall.h>
#include <fb.h>
#include <sys/mman.h>
#include "font.h"

#define COLS 80
#define ROWS 25
#define FB_BASE_OFF 0x40000000u
#define WND_W 640
#define WND_H 400
#define WND_X 60
#define WND_Y 40

static char out_lines[24][COLS + 1];
static int n_out;
static char cur_line[256];
static int cur_len;
static int cur_pos;

static unsigned char *fbmap;
static uint32_t fb_pitch;
static uint32_t fb_width, fb_height;
static uint32_t *backbuf;
static int dirty;
static int win_x = WND_X, win_y = WND_Y;
static int wnd_w = WND_W, wnd_h = WND_H;

static uint32_t *bb(void) { return backbuf; }

static void put_px(uint32_t x, uint32_t y, uint32_t color) {
    backbuf[(y * (fb_pitch / 4)) + x] = color;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t yy = 0; yy < h; yy++)
        for (uint32_t xx = 0; xx < w; xx++)
            put_px(x + xx, y + yy, color);
}

static void draw_text(int x, int y, const char *s, int len, uint32_t fg, uint32_t bg) {
    for (int c = 0; c < len; c++) {
        unsigned char ch = (unsigned char)(s[c] ? s[c] : ' ');
        if (ch < 32 || ch > 126) ch = '?';
        const uint8_t *g = ui_font[ch - 32];
        for (int r = 0; r < UI_FONT_HEIGHT; r++) {
            uint8_t bits = g[r];
            for (int b = 0; b < 8; b++) {
                uint32_t color = (bits & (0x80 >> b)) ? fg : bg;
                put_px(x + (uint32_t)(c * 8) + (uint32_t)b, y + (uint32_t)r, color);
            }
        }
    }
}

static void draw_arrow(uint32_t x, uint32_t y) {
    uint32_t col = 0x00FFFFFFu;
    fill_rect(x, y, 9, 16, 0x000000u);
    fill_rect(x + 7, y, 2, 16, 0x000000u);
    fill_rect(x, y + 8, 9, 8, 0x000000u);
    fill_rect(x + 6, y + 2, 2, 12, col);
    for (uint32_t r = 0; r < 8; r++) {
        uint32_t wdt = 2 + (r * 2);
        for (uint32_t xx = 0; xx < wdt; xx++) put_px(x + xx, y + r, col);
    }
}

static void render(void) {
    if (!dirty) return;
    dirty = 0;
    uint32_t *bb = backbuf;
    uint32_t total = fb_pitch * fb_height;
    for (uint32_t i = 0; i < total / 4; i++) bb[i] = 0;

    fill_rect(0, 0, fb_width, fb_height, 0x000000u);
    fill_rect(win_x, win_y, wnd_w, wnd_h, 0x303030u);
    fill_rect(win_x, win_y, wnd_w, 24, 0x0050C0u);
    draw_text(win_x, win_y, "sp& spHalfOS Terminal", wnd_w, 0x00FFFFFFu, 0x0050C0u);

    int input_row = (n_out < ROWS) ? n_out : ROWS - 1;
    for (int r = 0; r < ROWS; r++) {
        uint32_t y = (uint32_t)win_y + 24 + (uint32_t)(r * UI_FONT_HEIGHT);
        if (r < n_out) {
            draw_text(win_x, y, out_lines[r], (int)strlen(out_lines[r]), 0x00FFFFFFu, 0x000000u);
        } else if (r == n_out && n_out < ROWS) {
            draw_text(win_x, y, cur_line, cur_len, 0x00FFFFFFu, 0x000000u);
            uint32_t cx = (uint32_t)win_x + (uint32_t)(cur_pos * 8);
            uint32_t cy = (uint32_t)win_y + 24 + (uint32_t)(input_row * UI_FONT_HEIGHT);
            fill_rect(cx, cy, 8, UI_FONT_HEIGHT, 0x00FFFF00u);
        }
    }

    fill_rect(0, fb_height - 30, fb_width, 30, 0x202020u);
    fill_rect(10, fb_height - 26, 70, 22, 0x0050C0u);
    draw_text(10, fb_height - 26, "Start", 40, 0x00FFFFFFu, 0x0050C0u);

    draw_arrow((uint32_t)0, (uint32_t)0);

    memcpy(fbmap, backbuf, total);
}

static void push_output(const char *s, int n) {
    int len = 0;
    while (len < n && s[len] && s[len] != '\n') len++;
    for (int i = 0; i < len; i++) out_lines[n_out][i] = s[i];
    out_lines[n_out][len] = 0;
    n_out++;
    if (n_out > 24) {
        int last = 23;
        for (int i = 0; i < last; i++) strcpy(out_lines[i], out_lines[i + 1]);
        out_lines[last][0] = 0;
        n_out = last;
    }
    dirty = 1;
}

static void run_command(const char *line) {
    const char *tmpfile = "/tmp/ui_out.txt";
    int fd = open(tmpfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { dirty = 1; return; }
    dup2(fd, 1);
    close(fd);

    void *m = mmap(0, 64, 3, 0x22, -1, 0);
    int *up = (int *)m;
    int sh  = (int)(unsigned long)"/bin/sh";
    int dash = (int)(unsigned long)"-c";
    int line_addr = (int)(unsigned long)line;
    up[0] = sh; up[1] = dash; up[2] = line_addr; up[3] = 0;
    int pid = sp_spawn("/bin/sh", (char *const *)up, 4);
    munmap(m, 64);
    int st;
    for (;;) {
        st = sp_wait(pid);
        if (st >= 0) break;
        syscall0(SYS_YIELD);
    }
    dup2(1, 1);

    int in = open(tmpfile, O_RDONLY);
    if (in >= 0) {
        char buf[512];
        char content[1024];
        content[0] = 0;
        int tot = 0;
        int n;
        while ((n = read(in, buf, sizeof(buf))) > 0) {
            for (int i = 0; i < n; i++) {
                if (tot < (int)sizeof(content) - 1) content[tot++] = buf[i];
            }
        }
        content[tot] = 0;
        close(in);
        push_output(content, tot);
    }
    close(fd);
    unlink(tmpfile);
}

static void insert_char(int c) {
    if (cur_len >= (int)sizeof(cur_line) - 1) return;
    memmove(cur_line + cur_pos + 1, cur_line + cur_pos, (size_t)(strlen(cur_line + cur_pos)));
    cur_line[cur_pos] = (char)c;
    if (cur_pos >= cur_len) cur_len = cur_pos + 1;
    cur_line[cur_len] = 0;
    cur_pos++;
}

static void erase_at(void) {
    if (cur_pos > 0) {
        memmove(cur_line + cur_pos - 1, cur_line + cur_pos, (size_t)(strlen(cur_line + cur_pos)));
        cur_pos--;
        cur_line[cur_pos] = 0;
        if (cur_pos < cur_len) { cur_len = cur_pos + 1; cur_line[cur_len] = 0; }
    }
}

static void set_pos(int pos) {
    if (pos < 0) pos = 0;
    if (pos > cur_len) pos = cur_len;
    cur_pos = pos;
}

static void handle_key(int c) {
    if (c == 3) { cur_len = 0; cur_pos = 0; cur_line[0] = 0; dirty = 1; return; }
    if (c == 21) { cur_len = 0; cur_pos = 0; cur_line[0] = 0; dirty = 1; return; }
    if (c == 12) return;
    if (c == 8 || c == 127) { erase_at(); dirty = 1; return; }
    if (c == 10 || c == 13) {
        cur_line[cur_len] = 0;
        if (cur_len > 0) {
            char saved[256];
            strcpy(saved, cur_line);
            run_command(saved);
        }
        cur_len = 0; cur_pos = 0; cur_line[0] = 0;
        dirty = 1;
        return;
    }
    if (c == 27) { cur_len = 0; cur_pos = 0; cur_line[0] = 0; dirty = 1; return; }
    if (c < 32 || c > 126) return;
    insert_char(c);
    dirty = 1;
}

static int read_key(int *out) {
    int c = sp_getkey();
    if (c < 0) return -1;
    if (c == 27) {
        int c2 = sp_getkey();
        if (c2 == '[') {
            int c3 = sp_getkey();
            if (c3 == 'C') { set_pos(cur_pos + 1); dirty = 1; return 0; }
            if (c3 == 'D') { set_pos(cur_pos - 1); dirty = 1; return 0; }
            return 0;
        }
        *out = 27;
        return 1;
    }
    *out = c;
    return 1;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    sp_fbinfo_t fi;
    if (fb_info(&fi) < 0 || !fi.available) { printf("ui: no framebuffer\n"); return 1; }
    fbmap = (unsigned char *)fb_map();
    if (!fbmap) { printf("ui: cannot map fb\n"); return 1; }
    fb_pitch = fi.pitch;
    fb_width = fi.width;
    fb_height = fi.height;
    backbuf = (uint32_t *)malloc(fb_pitch * fb_height);
    if (!backbuf) { printf("ui: no backbuffer\n"); return 1; }
    dirty = 1;

    for (;;) {
        render();
        int c;
        if (!read_key(&c)) { syscall0(SYS_YIELD); continue; }
        handle_key(c);
        if (c == 3) break;
    }
    return 0;
}
