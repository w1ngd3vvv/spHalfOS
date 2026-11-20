/*
 * doomgeneric platform for spHalfOS
 */
#include "doomgeneric.h"
#include "doomkeys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <syscall.h>
#include <fb.h>

#define MAX_ACTIVE 32
#define HOLD_TICKS 50   /* ~833ms at 60Hz; bridges PS/2 typematic repeat delay */

typedef struct {
    unsigned char key;
    uint32_t until;
    int active;
} active_key_t;

static active_key_t active[MAX_ACTIVE];
static int esc_state; /* 0 none, 1 got ESC, 2 got CSI, 3 got SS3 */
static int esc_buf[8];
static int esc_len;

/* queue of synthesized events (press/release) to return from DG_GetKey */
typedef struct { int pressed; unsigned char key; } ev_t;
static ev_t evq[64];
static int ev_head, ev_tail;

static void ev_push(int pressed, unsigned char key)
{
    int next = (ev_head + 1) % 64;
    if (next == ev_tail) return;
    evq[ev_head].pressed = pressed;
    evq[ev_head].key = key;
    ev_head = next;
}

static int ev_pop(int *pressed, unsigned char *key)
{
    if (ev_tail == ev_head) return 0;
    *pressed = evq[ev_tail].pressed;
    *key = evq[ev_tail].key;
    ev_tail = (ev_tail + 1) % 64;
    return 1;
}

static void touch_key(unsigned char key)
{
    uint32_t now = (uint32_t)sp_ticks();
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (active[i].active && active[i].key == key) {
            active[i].until = now + HOLD_TICKS;
            return;
        }
    }
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (!active[i].active) {
            active[i].active = 1;
            active[i].key = key;
            active[i].until = now + HOLD_TICKS;
            ev_push(1, key);
            return;
        }
    }
    /* replace oldest-ish: just overwrite first */
    active[0].active = 1;
    active[0].key = key;
    active[0].until = now + HOLD_TICKS;
    ev_push(1, key);
}

static void release_expired(void)
{
    uint32_t now = (uint32_t)sp_ticks();
    for (int i = 0; i < MAX_ACTIVE; i++) {
        if (!active[i].active) continue;
        if ((int32_t)(now - active[i].until) >= 0) {
            ev_push(0, active[i].key);
            active[i].active = 0;
        }
    }
}

static unsigned char map_ascii(unsigned char c)
{
    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == '\b' || c == 127) return KEY_BACKSPACE;
    if (c == '\t') return KEY_TAB;
    if (c == 27) return KEY_ESCAPE;
    if (c == 3) return KEY_FIRE;        /* Ctrl-C as fire/escape-ish fallback */
    /* common Doom defaults: ctrl not directly representable as char alone,
       but kernel sends ctrl-modified chars; space/use, etc. */
    if (c == ' ') return KEY_USE;
    if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
    return c;
}

static void handle_csi(void)
{
    /* esc_buf has chars after ESC[, terminated logically by final byte stored */
    unsigned char final = (unsigned char)esc_buf[esc_len - 1];
    unsigned char key = 0;
    if (final == 'A') key = KEY_UPARROW;
    else if (final == 'B') key = KEY_DOWNARROW;
    else if (final == 'C') key = KEY_RIGHTARROW;
    else if (final == 'D') key = KEY_LEFTARROW;
    else if (final == 'H') key = KEY_HOME;
    else if (final == 'F') key = KEY_END;
    else if (final == '~') {
        int n = 0;
        for (int i = 0; i < esc_len - 1; i++) {
            if (esc_buf[i] >= '0' && esc_buf[i] <= '9')
                n = n * 10 + (esc_buf[i] - '0');
        }
        if (n == 2) key = KEY_INS;
        else if (n == 3) key = KEY_DEL;
        else if (n == 5) key = KEY_PGUP;
        else if (n == 6) key = KEY_PGDN;
        else if (n == 15) key = KEY_F5;
        else if (n == 17) key = KEY_F6;
        else if (n == 18) key = KEY_F7;
        else if (n == 19) key = KEY_F8;
        else if (n == 20) key = KEY_F9;
        else if (n == 21) key = KEY_F10;
        else if (n == 23) key = KEY_F11;
        else if (n == 24) key = KEY_F12;
    }
    if (key) touch_key(key);
}

static void handle_ss3(void)
{
    unsigned char final = (unsigned char)esc_buf[esc_len - 1];
    unsigned char key = 0;
    if (final == 'P') key = KEY_F1;
    else if (final == 'Q') key = KEY_F2;
    else if (final == 'R') key = KEY_F3;
    else if (final == 'S') key = KEY_F4;
    else if (final == 'H') key = KEY_HOME;
    else if (final == 'F') key = KEY_END;
    if (key) touch_key(key);
}

static void feed_char(int c)
{
    unsigned char ch = (unsigned char)c;
    if (esc_state == 0) {
        if (ch == 0x1b) { esc_state = 1; esc_len = 0; return; }
        touch_key(map_ascii(ch));
        return;
    }
    if (esc_state == 1) {
        if (ch == '[') { esc_state = 2; esc_len = 0; return; }
        if (ch == 'O') { esc_state = 3; esc_len = 0; return; }
        /* bare ESC */
        esc_state = 0;
        touch_key(KEY_ESCAPE);
        feed_char(ch);
        return;
    }
    if (esc_state == 2) {
        if (esc_len < (int)sizeof(esc_buf) - 1) esc_buf[esc_len++] = (char)ch;
        if (ch >= 0x40 && ch <= 0x7E) {
            esc_state = 0;
            handle_csi();
        }
        return;
    }
    if (esc_state == 3) {
        if (esc_len < (int)sizeof(esc_buf) - 1) esc_buf[esc_len++] = (char)ch;
        esc_state = 0;
        handle_ss3();
        return;
    }
}

static void pump_input(void)
{
    int c;
    while ((c = sp_getkey()) >= 0)
        feed_char(c);
}

void DG_Init(void)
{
    memset(active, 0, sizeof(active));
    memset(evq, 0, sizeof(evq));
    ev_head = ev_tail = 0;
    esc_state = 0;
    sp_clrscr();
    /* hide text cursor */
    printf("\033[?25l");
}

void DG_DrawFrame(void)
{
    sp_win_present(DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
}

void DG_SleepMs(uint32_t ms)
{
    usleep(ms * 1000u);
}

uint32_t DG_GetTicksMs(void)
{
    /* timer is 60 Hz */
    return (uint32_t)((uint64_t)sp_ticks() * 1000u / 60u);
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    /* first, deliver queued press/release events */
    if (ev_pop(pressed, key)) return 1;

    pump_input();
    release_expired();

    if (ev_pop(pressed, key)) return 1;
    return 0;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

int main(int argc, char **argv)
{
    /* default to shareware WAD in common locations if user didn't specify */
    static char *default_argv[] = { "doom", "-iwad", "/usr/share/doom/DOOM1.WAD", NULL };
    if (argc <= 1) {
        argv = default_argv;
        argc = 3;
    }

    doomgeneric_Create(argc, argv);

    for (;;) {
        doomgeneric_Tick();
    }
    return 0;
}
