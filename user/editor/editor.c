/* spHalfOS graphical text editor -- Windows 95 Notepad style */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syscall.h>
#include <fb.h>
#include <ui_font.h>

#define W 820
#define H 520
#define MAX_FILE (256 * 1024)

#define C_FACE   0xC0C0C0u
#define C_LIGHT  0xFFFFFFu
#define C_SHADOW 0x808080u
#define C_DARK   0x000000u
#define C_NAVY   0x000080u
#define C_TEXT   0x000000u
#define C_GRAY   0x808080u

#define MENU_H 24
#define STATUS_H 24
#define EDIT_X 3
#define EDIT_Y (MENU_H + 3)
#define EDIT_W (W - 6)
#define EDIT_H (H - MENU_H - STATUS_H - 6)
#define GUTTER 46
#define TEXT_X (EDIT_X + GUTTER)
#define TEXT_Y (EDIT_Y + 4)
#define CW 12
#define LH 18
#define VIS_COLS ((W - TEXT_X - 16) / CW)
#define VIS_ROWS ((EDIT_H - 8) / LH)

enum {
    K_UP = 0x1000, K_DOWN, K_RIGHT, K_LEFT, K_HOME, K_END,
    K_PGUP, K_PGDN, K_DEL
};

static uint32_t fb[W * H];
static char *buf;
static int len, cap;
static int modified;
static int cx;
static int top_line;
static const char *filename;
static int dirty = 1;
static int menu_open;
static int dialog;            /* 0 none, 1 save changes */
static int find_mode;
static char find_buf[96];
static int find_len;
static char status_msg[96];
static unsigned status_until;
static int prev_buttons;
static int last_blink = -1;

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; yy++) {
        uint32_t *row = fb + yy * W + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

static void edge(int x, int y, int w, int h, int raised) {
    uint32_t tl = raised ? C_LIGHT : C_SHADOW;
    uint32_t br = raised ? C_DARK : C_LIGHT;
    fill_rect(x, y, w, 1, tl);
    fill_rect(x, y, 1, h, tl);
    fill_rect(x, y + h - 1, w, 1, br);
    fill_rect(x + w - 1, y, 1, h, br);
}

/* 5x7 bitmap font */
static const uint8_t font5x7[96][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},
    {0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},
    {0x3E,0x41,0x41,0x51,0x32},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x00,0x7F,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},{0x41,0x41,0x7F,0x00,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x08,0x14,0x54,0x54,0x3C},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
    {0x00,0x7F,0x10,0x28,0x44},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},{0x08,0x08,0x2A,0x1C,0x08},{0x08,0x1C,0x2A,0x08,0x08},
};
static void draw_char(int x, int y, char ch, uint32_t fg, int scale) {
    if (scale < 2) scale = 2;
    unsigned char uc = (unsigned char)ch;
    if (uc < 32 || uc > 126) uc = '?';
    const uint8_t *g = font5x7[uc - 32];
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col*scale+sx, py = y + row*scale+sy;
                        if ((unsigned)px < W && (unsigned)py < H) fb[py*W+px] = fg;
                    }
            }
        }
    }
}
static void draw_text(int x, int y, const char *s, uint32_t fg, int scale) {
    for (int i = 0; s[i]; i++) draw_char(x + i*6*scale, y, s[i], fg, scale);
}
static void draw_text_lim(int x, int y, const char *s, int max_chars, uint32_t fg, int scale) {
    for (int i = 0; s[i] && i < max_chars; i++) draw_char(x + i*6*scale, y, s[i], fg, scale);
}
static void gui_draw_text(int x, int y, const char *s, uint32_t fg, int scale) { ui_draw_text(fb, W, H, x, y, s, fg, scale); }

/* ---- text buffer ---- */

static int line_start(int line) {
    int l = 0;
    if (line <= 0) return 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            l++;
            if (l == line) return i + 1;
        }
    }
    return len;
}

static int line_end(int line) {
    int s = line_start(line);
    int i = s;
    while (i < len && buf[i] != '\n') i++;
    return i;
}

static int total_lines(void) {
    int n = 1;
    for (int i = 0; i < len; i++) if (buf[i] == '\n') n++;
    return n;
}

static int line_of(int pos) {
    int line = 0;
    if (pos > len) pos = len;
    for (int i = 0; i < pos; i++) if (buf[i] == '\n') line++;
    return line;
}

static int cursor_column(void) {
    int s = line_start(line_of(cx));
    int col = 0;
    for (int i = s; i < cx; i++) {
        if (buf[i] == '\t') col = (col + 4) & ~3;
        else col++;
    }
    return col;
}

static void ensure_cap(int need) {
    if (need <= cap) return;
    int ncap = cap ? cap * 2 : 8192;
    while (ncap < need) ncap *= 2;
    char *nb = realloc(buf, ncap);
    if (!nb) return;
    buf = nb;
    cap = ncap;
}

static void insert_char(int c) {
    if (len + 1 >= MAX_FILE) return;
    ensure_cap(len + 2);
    if (len + 1 > cap) return;
    if (cx > len) cx = len;
    if (cx < 0) cx = 0;
    memmove(buf + cx + 1, buf + cx, len - cx);
    buf[cx++] = (char)c;
    len++;
    modified = 1;
    dirty = 1;
}

static void delete_at(int pos) {
    if (pos < 0 || pos >= len) return;
    memmove(buf + pos, buf + pos + 1, len - pos - 1);
    len--;
    modified = 1;
    dirty = 1;
}

static int save_file(void) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        snprintf(status_msg, sizeof(status_msg), "Cannot save: %s", strerror(errno));
        status_until = (unsigned)sp_ticks() + 180;
        dirty = 1;
        return -1;
    }
    int off = 0;
    while (off < len) {
        int w = (int)write(fd, buf + off, len - off);
        if (w <= 0) { close(fd); return -1; }
        off += w;
    }
    close(fd);
    modified = 0;
    snprintf(status_msg, sizeof(status_msg), "Saved %s", filename);
    status_until = (unsigned)sp_ticks() + 120;
    dirty = 1;
    return 0;
}

static void move_vertical(int dir) {
    int line = line_of(cx);
    int col = cursor_column();
    int target = line + dir;
    int total = total_lines();
    if (target < 0 || target >= total) return;
    int s = line_start(target), e = line_end(target);
    int c = 0, pos = s;
    while (pos < e && c < col) {
        if (buf[pos] == '\t') c = (c + 4) & ~3;
        else c++;
        pos++;
    }
    cx = pos;
    dirty = 1;
}

static void find_next(void) {
    if (!find_len) return;
    find_buf[find_len] = 0;
    for (int i = cx + 1; i + find_len <= len; i++) {
        if (!memcmp(buf + i, find_buf, find_len)) { cx = i; dirty = 1; return; }
    }
    for (int i = 0; i + find_len <= cx; i++) {
        if (!memcmp(buf + i, find_buf, find_len)) { cx = i; dirty = 1; return; }
    }
    snprintf(status_msg, sizeof(status_msg), "Not found: %s", find_buf);
    status_until = (unsigned)sp_ticks() + 120;
}

/* ---- drawing ---- */

static const char *menu_names[4] = { "File", "Edit", "Search", "Help" };
static const int menu_x[4] = { 8, 70, 132, 200 };

static void draw(void) {
    fill_rect(0, 0, W, H, C_FACE);
    fill_rect(0, MENU_H - 1, W, 1, C_SHADOW);
    fill_rect(0, MENU_H, W, 1, C_LIGHT);

    for (int i = 0; i < 4; i++) {
        int active = (menu_open && i == 0);
        if (active) fill_rect(menu_x[i] - 4, 2, 54, MENU_H - 4, C_NAVY);
        gui_draw_text(menu_x[i], 4, menu_names[i], active ? C_LIGHT : C_TEXT, 1);
    }

    /* edit field */
    fill_rect(EDIT_X, EDIT_Y, EDIT_W, EDIT_H, C_LIGHT);
    edge(EDIT_X, EDIT_Y, EDIT_W, EDIT_H, 0);
    fill_rect(EDIT_X + 2, EDIT_Y + 2, GUTTER - 4, EDIT_H - 4, 0xF0F0F0);
    fill_rect(EDIT_X + GUTTER - 1, EDIT_Y + 2, 1, EDIT_H - 4, C_SHADOW);

    int total = total_lines();
    int curline = line_of(cx);
    if (curline < top_line) top_line = curline;
    if (curline >= top_line + VIS_ROWS) top_line = curline - VIS_ROWS + 1;
    if (top_line < 0) top_line = 0;
    if (top_line > total - 1) top_line = total > 0 ? total - 1 : 0;

    for (int r = 0; r < VIS_ROWS; r++) {
        int ln = top_line + r;
        int y = TEXT_Y + r * LH;
        if (ln >= total) break;
        char num[12];
        snprintf(num, sizeof(num), "%d", ln + 1);
        int nl = (int)strlen(num);
        draw_text(EDIT_X + GUTTER - 8 - nl * 12, y + 2, num, C_GRAY, 1);

        int s = line_start(ln), e = line_end(ln);
        int col = 0;
        for (int i = s; i < e && col < VIS_COLS; i++) {
            unsigned char ch = (unsigned char)buf[i];
            if (ch == '\t') {
                int spaces = 4 - (col & 3);
                while (spaces-- && col < VIS_COLS) col++;
            } else {
                if (ch < 32 || ch >= 127) ch = '?';
                draw_char(TEXT_X + col * CW, y, (char)ch, C_TEXT, 2);
                col++;
            }
        }
    }

    /* caret */
    unsigned tick = (unsigned)sp_ticks();
    if (!dialog && !find_mode && ((tick / 30) & 1) == 0) {
        int col = cursor_column();
        if (col > VIS_COLS) col = VIS_COLS;
        int cy = TEXT_Y + (curline - top_line) * LH;
        if (curline >= top_line && curline < top_line + VIS_ROWS)
            fill_rect(TEXT_X + col * CW, cy, 2, LH - 2, C_DARK);
    }

    /* status bar */
    int sy = H - STATUS_H;
    fill_rect(0, sy, W, STATUS_H, C_FACE);
    fill_rect(0, sy, W, 1, C_LIGHT);
    fill_rect(0, sy + 1, W, 1, C_SHADOW);
    char st[160];
    if (find_mode) {
        find_buf[find_len] = 0;
        snprintf(st, sizeof(st), "Find: %s_", find_buf);
    } else if (status_msg[0] && tick < status_until) {
        snprintf(st, sizeof(st), "%s", status_msg);
    } else {
        snprintf(st, sizeof(st), "%s%s", filename, modified ? " *" : "");
    }
    gui_draw_text(8, sy + 6, st, C_TEXT, 1);
    char pos[64];
    snprintf(pos, sizeof(pos), "Ln %d, Col %d", curline + 1, cursor_column() + 1);
    int pw = (int)strlen(pos) * 8 + 12;
    fill_rect(W - pw - 8, sy + 3, pw, STATUS_H - 7, C_FACE);
    edge(W - pw - 8, sy + 3, pw, STATUS_H - 7, 0);
    gui_draw_text(W - pw - 2, sy + 6, pos, C_TEXT, 1);

    /* File menu */
    if (menu_open) {
        int mx = 4, my = MENU_H, mw = 150, mh = 46;
        fill_rect(mx, my, mw, mh, C_FACE);
        edge(mx, my, mw, mh, 1);
        gui_draw_text(mx + 22, my + 5, "Save", C_TEXT, 1);
        gui_draw_text(mx + 22, my + 25, "Exit", C_TEXT, 1);
        gui_draw_text(mx + 6, my + 5, "S", C_GRAY, 1);
        gui_draw_text(mx + 6, my + 25, "X", C_GRAY, 1);
    }

    /* save-changes dialog */
    if (dialog) {
        int dx = (W - 360) / 2, dy = (H - 150) / 2, dw = 360, dh = 150;
        fill_rect(dx, dy, dw, dh, C_FACE);
        edge(dx, dy, dw, dh, 1);
        fill_rect(dx + 3, dy + 3, dw - 6, 4, C_NAVY);
        gui_draw_text(dx + 16, dy + 22, "Save changes to this file?", C_TEXT, 1);
        const char *btns[3] = { "Yes", "No", "Cancel" };
        for (int i = 0; i < 3; i++) {
            int bx = dx + 24 + i * 108, by = dy + dh - 40;
            fill_rect(bx, by, 92, 26, C_FACE);
            edge(bx, by, 92, 26, 1);
            gui_draw_text(bx + 16, by + 6, btns[i], C_TEXT, 1);
        }
    }

    last_blink = (int)((tick / 30) & 1);
}

/* ---- input ---- */

static int next_key(void) {
    int c = sp_getkey();
    for (int i = 0; i < 50 && c < 0; i++) {
        usleep(1000);
        c = sp_getkey();
    }
    return c;
}

static int read_key(void) {
    int c = sp_getkey();
    if (c < 0) return -1;
    if (c != 27) return c;
    int c2 = next_key();
    if (c2 != '[' && c2 != 'O') return 27;
    int c3 = next_key();
    switch (c3) {
    case 'A': return K_UP;
    case 'B': return K_DOWN;
    case 'C': return K_RIGHT;
    case 'D': return K_LEFT;
    case 'H': return K_HOME;
    case 'F': return K_END;
    case '3': next_key(); return K_DEL;
    case '5': next_key(); return K_PGUP;
    case '6': next_key(); return K_PGDN;
    default: return 0;
    }
}

static void request_quit(void);

static void handle_key(int k) {
    if (k < 0) return;

    if (dialog) {
        if (k == 'y' || k == 'Y') { if (save_file() == 0) request_quit(); else dialog = 0; }
        else if (k == 'n' || k == 'N') request_quit();
        else if (k == 'c' || k == 'C' || k == 27) { dialog = 0; dirty = 1; }
        return;
    }

    if (find_mode) {
        if (k == '\n' || k == '\r') { find_mode = 0; find_next(); }
        else if (k == 27) { find_mode = 0; find_len = 0; }
        else if (k == 8 || k == 127) { if (find_len > 0) find_len--; }
        else if (k >= 32 && k < 127 && find_len < (int)sizeof(find_buf) - 2) find_buf[find_len++] = (char)k;
        dirty = 1;
        return;
    }

    if (menu_open && k == 27) { menu_open = 0; dirty = 1; return; }

    switch (k) {
    case K_UP: move_vertical(-1); break;
    case K_DOWN: move_vertical(1); break;
    case K_LEFT: if (cx > 0) cx--; dirty = 1; break;
    case K_RIGHT: if (cx < len) cx++; dirty = 1; break;
    case K_HOME: cx = line_start(line_of(cx)); dirty = 1; break;
    case K_END: cx = line_end(line_of(cx)); dirty = 1; break;
    case K_PGUP: for (int i = 0; i < VIS_ROWS - 1; i++) move_vertical(-1); break;
    case K_PGDN: for (int i = 0; i < VIS_ROWS - 1; i++) move_vertical(1); break;
    case K_DEL: if (cx < len) delete_at(cx); break;
    case 8:
    case 127:
        if (cx > 0) { cx--; delete_at(cx); }
        break;
    case 19: save_file(); break;               /* Ctrl-S */
    case 17:                                     /* Ctrl-Q */
        if (modified) { dialog = 1; dirty = 1; }
        else request_quit();
        break;
    case 6:                                      /* Ctrl-F */
        find_mode = 1; find_len = 0; find_buf[0] = 0; dirty = 1;
        break;
    case '\n':
    case '\r': insert_char('\n'); break;
    case '\t': insert_char('\t'); break;
    case 27: break;
    default:
        if (k >= 32 && k < 127) insert_char(k);
        break;
    }
}

static int quit_now;
static void request_quit(void) { quit_now = 1; }

static int point_in(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void click(int mx, int my) {
    if (dialog) {
        int dx = (W - 360) / 2, dy = (H - 150) / 2, dh = 150;
        const char *btns[3] = { "Yes", "No", "Cancel" };
        (void)btns;
        for (int i = 0; i < 3; i++) {
            int bx = dx + 24 + i * 108, by = dy + dh - 40;
            if (point_in(mx, my, bx, by, 92, 26)) {
                if (i == 0) {
                    if (save_file() == 0) request_quit();
                    else { dialog = 0; dirty = 1; }
                }
                else if (i == 1) request_quit();
                else { dialog = 0; dirty = 1; }
                return;
            }
        }
        return;
    }

    if (menu_open) {
        int mx0 = 4, my0 = MENU_H;
        if (point_in(mx, my, mx0, my0, 150, 23)) { save_file(); menu_open = 0; dirty = 1; return; }
        if (point_in(mx, my, mx0, my0 + 23, 150, 23)) {
            menu_open = 0;
            if (modified) { dialog = 1; dirty = 1; }
            else request_quit();
            return;
        }
        menu_open = 0;
        /* fall through: clicks elsewhere also act normally */
        dirty = 1;
    }

    /* menu bar */
    if (my < MENU_H) {
        if (point_in(mx, my, 4, 0, 48, MENU_H)) { menu_open = 1; dirty = 1; }
        else if (point_in(mx, my, 58, 0, 48, MENU_H)) { menu_open = 0; dirty = 1; }
        else if (point_in(mx, my, 112, 0, 72, MENU_H)) {
            find_mode = 1; find_len = 0; find_buf[0] = 0; dirty = 1;
        } else { menu_open = 0; dirty = 1; }
        return;
    }

    /* text area: place caret */
    if (point_in(mx, my, EDIT_X, EDIT_Y, EDIT_W, EDIT_H)) {
        int row = (my - TEXT_Y) / LH;
        if (row < 0) row = 0;
        if (row >= VIS_ROWS) row = VIS_ROWS - 1;
        int target_line = top_line + row;
        int total = total_lines();
        if (target_line >= total) target_line = total - 1;
        if (target_line < 0) target_line = 0;
        int col = (mx - TEXT_X) / CW;
        if (col < 0) col = 0;
        if (col > VIS_COLS) col = VIS_COLS;
        int s = line_start(target_line), e = line_end(target_line);
        int c = 0, pos = s;
        while (pos < e && c < col) {
            c = (buf[pos] == '\t') ? ((c + 4) & ~3) : (c + 1);
            pos++;
        }
        cx = pos;
        if (target_line < top_line) top_line = target_line;
        dirty = 1;
    }
}

int main(int argc, char **argv) {
    filename = argc > 1 ? argv[1] : "untitled.txt";
    cap = 16384;
    buf = malloc(cap);
    if (!buf) return 1;
    memset(buf, 0, cap);

    int fd = open(filename, O_RDONLY);
    if (fd >= 0) {
        int r;
        while ((r = (int)read(fd, buf + len, cap - len)) > 0) {
            len += r;
            if (len >= MAX_FILE) break;
            ensure_cap(len + 4096);
        }
        close(fd);
    }
    cx = 0;
    top_line = 0;

    sp_mouse_t m;
    if (mouse_read(&m) == 0) prev_buttons = m.buttons;

    while (!quit_now) {
        unsigned tick = (unsigned)sp_ticks();
        if (last_blink != (int)((tick / 30) & 1) && !dialog && !find_mode) dirty = 1;

        if (dirty) {
            draw();
            sp_win_present(fb, W, H);
            dirty = 0;
        }

        int k;
        while ((k = read_key()) >= 0) handle_key(k);

        if (mouse_read(&m) == 0) {
            sp_wininfo_t wi;
            if (sp_win_info(&wi) == 0) {
                int mx = m.x - wi.x;
                int my = m.y - wi.y;
                int left = m.buttons & 1;
                if (left && !(prev_buttons & 1) && mx >= 0 && mx < W && my >= 0 && my < H)
                    click(mx, my);
                prev_buttons = m.buttons;
            }
        }

        usleep(16000);
    }
    return 0;
}
