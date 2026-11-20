#ifndef SP_CONSOLE_H
#define SP_CONSOLE_H

#include "types.h"

/* Default geometry used before the framebuffer is attached (and for the
   legacy 80x25 VGA text-mode fallback).  The framebuffer console grows to
   CON_MAX_COLS x CON_MAX_ROWS, enough for 1920x1080 with 12x26 cells. */
#define CON_COLS 80
#define CON_ROWS 25
#define CON_MAX_COLS 160
#define CON_MAX_ROWS 41

/* output */
void console_init(void);
void console_clear(void);
void console_putc(char c);
void console_write(const char *s, size_t n);
void console_puts(const char *s);
void console_gotoxy(int x, int y);
void console_set_attr(uint8_t attr);
uint8_t console_get_attr(void);
void console_blit(int x, int y, int w, int h, const uint16_t *cells);
void console_attach_fb(void);
void console_save_cursor(void);
void console_restore_cursor(void);
void console_cursor_visible(int on);
int  console_cols(void);
int  console_rows(void);

/* mouse integration (framebuffer console) */
void console_mouse_update(int x, int y, int buttons);
void console_load_wallpaper(void);
void console_win_present(const uint32_t *pixels, int src_w, int src_h);

typedef struct PACKED {
    int32_t x, y;               /* client area origin on screen */
    int32_t w, h;               /* client area size */
    int32_t screen_w, screen_h; /* full framebuffer size */
    int32_t win_x, win_y;       /* outer window origin */
    int32_t win_w, win_h;       /* outer window size */
} sp_wininfo_t;
int console_win_info(sp_wininfo_t *out);

/* input (fed by PS/2 keyboard and serial) */
void console_input_push(uint8_t c);
int  console_input_get(void);      /* -1 if empty */
int  console_input_available(void);
void console_input_flush(void);

/* multi-terminal */
void *console_create_term(const char *title);
void *console_default_term(void);
void console_focus_term(void *term);
void console_close_term(void *term);
void console_notify_process_exit(int pid);
void console_set_term_owner(void *term, int pid);
int  console_spawn_in_new_term(const char *path, char *const argv[], int argc);

int  vga_get_tty_size(int *cols, int *rows);

#endif
