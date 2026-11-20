/* spHalfOS graphical Files (My Computer) -- Windows 95 style, mouse driven */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <syscall.h>
#include <fb.h>
#include <ui_font.h>

#define W 760
#define H 460
#define MAX_ENTRIES 256
#define NAME_MAX_LEN 60

#define C_FACE   0xC0C0C0u
#define C_LIGHT  0xFFFFFFu
#define C_SHADOW 0x808080u
#define C_DARK   0x000000u
#define C_NAVY   0x000080u
#define C_WHITE  0xFFFFFFu
#define C_TEXT   0x000000u
#define C_TEAL   0x008080u

#define MENU_H 24
#define PATH_Y 28
#define PATH_H 30
#define LIST_Y (PATH_Y + PATH_H + 4)
#define STATUS_H 24
#define LIST_H (H - LIST_Y - STATUS_H)
#define ROW_H 30
#define UP_X 8
#define UP_Y (PATH_Y + 3)
#define UP_W 52
#define UP_H (PATH_H - 6)
#define SBX (W - 18)

typedef struct { char name[NAME_MAX_LEN]; uint8_t type; uint32_t size; } Entry;
static uint32_t fb[W * H];
static Entry entries[MAX_ENTRIES];
static int entry_count;
static int sel;
static int scroll;
static char cwd[256];
static int dirty;

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

/* font: ui_font.h */

static void draw_char(int x, int y, char ch, uint32_t fg, int scale) { ui_draw_char(fb, W, H, x, y, ch, fg, scale); }

static void draw_text(int x, int y, const char *s, uint32_t fg, int scale) { ui_draw_text(fb, W, H, x, y, s, fg, scale); }

static void draw_text_lim(int x, int y, const char *s, int max_chars, uint32_t fg, int scale) { ui_draw_text_n(fb, W, H, x, y, s, max_chars, fg, scale); }

static void draw_folder(int x, int y) {
    /* Windows 95 yellow folder, 20x18 */
    fill_rect(x + 1, y + 5, 19, 12, C_DARK);
    fill_rect(x + 2, y + 6, 17, 10, 0xF0BE50);
    fill_rect(x + 2, y + 2, 8, 5, C_DARK);
    fill_rect(x + 3, y + 3, 6, 3, 0xF5C860);
    fill_rect(x + 2, y + 5, 17, 1, 0xFFE090);
    fill_rect(x + 2, y + 6, 17, 1, 0xE0A840);
}

static void draw_doc(int x, int y) {
    fill_rect(x + 3, y + 1, 15, 18, C_DARK);
    fill_rect(x + 4, y + 2, 13, 16, C_WHITE);
    fill_rect(x + 13, y + 2, 4, 4, C_FACE);
    fill_rect(x + 13, y + 2, 1, 4, C_SHADOW);
    fill_rect(x + 6, y + 7, 9, 1, 0x000080);
    fill_rect(x + 6, y + 10, 9, 1, 0x000080);
    fill_rect(x + 6, y + 13, 6, 1, 0x000080);
}

static int cmp_entries(const void *a, const void *b) {
    const Entry *ea = a, *eb = b;
    if ((ea->type == 2) != (eb->type == 2)) return (ea->type == 2) ? -1 : 1;
    return strcmp(ea->name, eb->name);
}

static void load_dir(void) {
    entry_count = 0;
    sel = 0;
    scroll = 0;
    DIR *d = opendir(cwd[0] ? cwd : ".");
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) && entry_count < MAX_ENTRIES) {
        if (!strcmp(de->d_name, ".")) continue;
        Entry *e = &entries[entry_count];
        strncpy(e->name, de->d_name, NAME_MAX_LEN - 1);
        e->name[NAME_MAX_LEN - 1] = 0;
        e->type = de->d_type;
        e->size = 0;
        struct stat st;
        char p[512];
        snprintf(p, sizeof(p), "%s/%s", cwd[0] ? cwd : "/", de->d_name);
        if (stat(p, &st) == 0) {
            e->type = S_ISDIR(st.st_mode) ? 2 : 1;
            e->size = (uint32_t)st.st_size;
        }
        entry_count++;
    }
    closedir(d);
    qsort(entries, entry_count, sizeof(Entry), cmp_entries);
    dirty = 1;
}

static void go_up(void) {
    char *s = strrchr(cwd, '/');
    if (s && s != cwd) *s = 0;
    else strcpy(cwd, "/");
    load_dir();
}

static void enter_sel(void) {
    if (sel < 0 || sel >= entry_count) return;
    Entry *e = &entries[sel];
    if (e->type != 2) return;
    if (!strcmp(e->name, "..")) { go_up(); return; }
    if (strcmp(cwd, "/") != 0)
        strncat(cwd, "/", sizeof(cwd) - strlen(cwd) - 1);
    strncat(cwd, e->name, sizeof(cwd) - strlen(cwd) - 1);
    load_dir();
}

static int sb_thumb_y(int *out_h) {
    int total = entry_count;
    int vis = LIST_H / ROW_H;
    if (total <= vis) { *out_h = LIST_H; return LIST_Y; }
    int th = LIST_H * vis / total;
    if (th < 24) th = 24;
    int maxscroll = total - vis;
    int y = LIST_Y + (LIST_H - th) * scroll / maxscroll;
    *out_h = th;
    return y;
}

static void click_scrollbar(int my) {
    int vis = LIST_H / ROW_H;
    if (entry_count <= vis) return;
    int th;
    int ty = sb_thumb_y(&th);
    if (my < ty) scroll -= vis;
    else if (my >= ty + th) scroll += vis;
    if (scroll < 0) scroll = 0;
    if (scroll > entry_count - vis) scroll = entry_count - vis;
    if (scroll < 0) scroll = 0;
    dirty = 1;
}

static void draw(void) {
    fill_rect(0, 0, W, H, C_FACE);

    /* menu bar */
    fill_rect(0, 0, W, MENU_H, C_FACE);
    draw_text(8, 4, "File", C_TEXT, 1);
    draw_text(58, 4, "Edit", C_TEXT, 1);
    draw_text(108, 4, "View", C_TEXT, 1);
    draw_text(158, 4, "Help", C_TEXT, 1);
    fill_rect(0, MENU_H - 1, W, 1, C_SHADOW);
    fill_rect(0, MENU_H, W, 1, C_LIGHT);

    /* path / toolbar */
    fill_rect(UP_X, UP_Y, UP_W, UP_H, C_FACE);
    edge(UP_X, UP_Y, UP_W, UP_H, 1);
    draw_text(UP_X + 8, UP_Y + 5, "Up", C_TEXT, 1);
    /* path field */
    int px = UP_X + UP_W + 8, pw = W - px - 8;
    fill_rect(px, PATH_Y + 3, pw, PATH_H - 6, C_WHITE);
    edge(px, PATH_Y + 3, pw, PATH_H - 6, 0);
    draw_text_lim(px + 6, PATH_Y + 9, cwd, (pw - 16) / 8, C_TEXT, 1);

    /* list background */
    fill_rect(0, LIST_Y, W, LIST_H, C_WHITE);
    edge(0, LIST_Y, W, LIST_H, 0);

    int vis = LIST_H / ROW_H;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + vis) scroll = sel - vis + 1;
    if (scroll < 0) scroll = 0;

    if (!entry_count) {
        draw_text(16, LIST_Y + 14, "This folder is empty.", C_SHADOW, 1);
    }

    for (int i = 0; i < vis && scroll + i < entry_count; i++) {
        int idx = scroll + i;
        Entry *e = &entries[idx];
        int y = LIST_Y + 2 + i * ROW_H;
        if (idx == sel) {
            fill_rect(2, y, W - 4, ROW_H - 1, C_NAVY);
            draw_text_lim(42, y + 4, e->name, (W - 160) / 8, C_WHITE, 1);
        } else {
            draw_text_lim(42, y + 4, e->name, (W - 160) / 8, C_TEXT, 1);
        }
        if (e->type == 2) draw_folder(16, y + 4);
        else draw_doc(16, y + 3);
        if (e->type != 2 && idx != sel) {
            char sb[24];
            snprintf(sb, sizeof(sb), "%u B", (unsigned)e->size);
            draw_text(W - 100, y + 4, sb, C_SHADOW, 1);
        } else if (e->type != 2) {
            char sb[24];
            snprintf(sb, sizeof(sb), "%u B", (unsigned)e->size);
            draw_text(W - 100, y + 4, sb, C_WHITE, 1);
        }
    }

    /* scrollbar */
    fill_rect(SBX, LIST_Y + 1, 16, LIST_H - 2, C_FACE);
    edge(SBX, LIST_Y + 1, 16, LIST_H - 2, 0);
    int th;
    int ty = sb_thumb_y(&th);
    fill_rect(SBX + 2, ty, 12, th, C_FACE);
    edge(SBX + 2, ty, 12, th, 1);

    /* status bar */
    int sy = H - STATUS_H;
    fill_rect(0, sy, W, STATUS_H, C_FACE);
    fill_rect(0, sy, W, 1, C_LIGHT);
    fill_rect(0, sy + 1, W, 1, C_SHADOW);
    char info[96];
    snprintf(info, sizeof(info), "%d object(s)", entry_count);
    draw_text(8, sy + 4, info, C_TEXT, 1);
    fill_rect(W - 110, sy + 3, 104, STATUS_H - 6, C_FACE);
    edge(W - 110, sy + 3, 104, STATUS_H - 6, 1);
    draw_text(W - 102, sy + 5, "Double-click", C_TEXT, 1);
}

int main(void) {
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "/");
    load_dir();

    sp_mouse_t m;
    int prev_buttons = 0;
    if (mouse_read(&m) == 0) prev_buttons = m.buttons;
    int last_click_idx = -1;
    unsigned last_click_tick = 0;

    for (;;) {
        if (dirty) {
            draw();
            sp_win_present(fb, W, H);
            dirty = 0;
        }

        /* keyboard */
        int c;
        while ((c = sp_getkey()) >= 0) {
            if (c == 3) return 0;
            if (c == '\n' || c == '\r') { enter_sel(); continue; }
            if (c == 127 || c == 8) { go_up(); continue; }
            if (c == 'k' || c == 'w') { if (sel > 0) sel--; dirty = 1; }
            else if (c == 'j' || c == 's') { if (sel < entry_count - 1) sel++; dirty = 1; }
            else if (c == 27) {
                int c2 = sp_getkey();
                if (c2 == '[' || c2 == 'O') {
                    int c3 = sp_getkey();
                    if (c3 == 'A' && sel > 0) sel--;
                    else if (c3 == 'B' && sel < entry_count - 1) sel++;
                    else if (c3 == 'H') sel = 0;
                    else if (c3 == 'F') sel = entry_count > 0 ? entry_count - 1 : 0;
                    dirty = 1;
                }
            }
        }

        /* mouse */
        if (mouse_read(&m) == 0) {
            sp_wininfo_t wi;
            if (sp_win_info(&wi) == 0) {
                int mx = m.x - wi.x;
                int my = m.y - wi.y;
                int left = m.buttons & 1;
                int pressed = left && !(prev_buttons & 1);
                int ins = (mx >= 0 && mx < W && my >= 0 && my < H);
                if (pressed && ins) {
                    if (my >= PATH_Y && my < PATH_Y + PATH_H && mx >= UP_X && mx < UP_X + UP_W) {
                        go_up();
                    } else if (my >= LIST_Y && my < LIST_Y + LIST_H && mx >= SBX) {
                        click_scrollbar(my);
                    } else if (my >= LIST_Y && my < LIST_Y + LIST_H - 1) {
                        int row = (my - LIST_Y - 2) / ROW_H;
                        int idx = scroll + row;
                        if (idx >= 0 && idx < entry_count) {
                            unsigned now = (unsigned)sp_ticks();
                            if (idx == last_click_idx && now - last_click_tick < 24) {
                                sel = idx;
                                enter_sel();
                                last_click_idx = -1;
                            } else {
                                sel = idx;
                                last_click_idx = idx;
                                last_click_tick = now;
                                dirty = 1;
                            }
                        }
                    }
                }
                prev_buttons = m.buttons;
            }
        }

        usleep(16000);
    }
}
