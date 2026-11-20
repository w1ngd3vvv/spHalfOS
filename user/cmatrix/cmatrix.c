/* spHalfOS "cmatrix" -- falling glyphs effect */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syscall.h>
#include <fb.h>

#define MAX_W 160
#define MAX_H 41

typedef struct {
    int y;
    int delay;
    int speed;
} drop_t;

static drop_t drops[MAX_W];
static unsigned char intensity[MAX_H][MAX_W];
static char glyph[MAX_H][MAX_W];
static unsigned short cells[MAX_H][MAX_W];
static int W = 80, H = 25;

static const char glyphs[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "@#$%&*+=<>?/\\|[]{}()!;:^~";

static int keypressed(void) {
    int c = sp_getkey();
    if (c < 0) return 0;
    return (c == 3 || c == 'q' || c == 'Q');
}

static char rnd_glyph(void) {
    return glyphs[rand() % (int)(sizeof(glyphs) - 1)];
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    sp_fbinfo_t fb;
    if (fb_info(&fb) == 0 && fb.available && fb.width && fb.height) {
        W = (int)fb.width / 12;
        H = (int)fb.height / 26;
    }
    if (W > MAX_W) W = MAX_W;
    if (H > MAX_H) H = MAX_H;
    if (W < 20) W = 80;
    if (H < 10) H = 25;

    srand((unsigned)syscall0(SYS_TICKS) * 1103515245u + 12345);

    sp_clrscr();
    memset(intensity, 0, sizeof(intensity));
    for (int x = 0; x < W; x++) {
        drops[x].y = rand() % H;
        drops[x].delay = rand() % 20;
        drops[x].speed = 1 + rand() % 3;
    }

    for (;;) {
        /* fade */
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (intensity[y][x] && rand() % 4 == 0)
                    intensity[y][x]--;

        for (int x = 0; x < W; x++) {
            drop_t *d = &drops[x];
            if (d->delay > 0) { d->delay--; continue; }
            if (rand() % d->speed != 0) continue;

            if (d->y >= 0 && d->y < H) {
                intensity[d->y][x] = 1;
                glyph[d->y][x] = rnd_glyph();
            }
            d->y++;
            if (d->y >= 0 && d->y < H) {
                intensity[d->y][x] = 3;
                glyph[d->y][x] = rnd_glyph();
            }
            if (d->y >= H + rand() % 8) {
                d->y = -rand() % 6;
                d->delay = rand() % 25;
                d->speed = 1 + rand() % 3;
            }
        }

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int a = intensity[y][x];
                int attr = a == 3 ? 0x0F : a == 2 ? 0x0A : a == 1 ? 0x02 : 0x00;
                char ch = a ? glyph[y][x] : ' ';
                cells[y][x] = (unsigned short)((unsigned char)ch | (attr << 8));
            }
        }
        sp_blit(0, 0, W, H, (const unsigned short *)cells);
        usleep(70000);
        if (keypressed()) break;
    }
    sp_clrscr();
    printf("cmatrix: follow the white rabbit.\n");
    return 0;
}
