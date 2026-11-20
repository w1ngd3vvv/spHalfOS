/* spHalfOS "fire" -- ASCII fire effect using the VGA text console */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syscall.h>
#include <fb.h>

#define MAX_W 160
#define MAX_H 41

static unsigned char heat[MAX_H][MAX_W];
static unsigned short cells[MAX_H][MAX_W];
static int W = 80, H = 25;

static int keypressed(void) {
    int c = sp_getkey();
    if (c < 0) return 0;
    return (c == 3 || c == 'q' || c == 'Q');
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

    unsigned seed = (unsigned)syscall0(SYS_TICKS) * 2654435761u + 12345;
    srand(seed);

    memset(heat, 0, sizeof(heat));
    for (int x = 0; x < W; x++) heat[H - 1][x] = (unsigned char)(rand() % 8);

    sp_clrscr();
    for (;;) {
        /* propagate fire upward */
        for (int y = 1; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int below = heat[y][x];
                int left = x > 0 ? heat[y][x - 1] : below;
                int right = x < W - 1 ? heat[y][x + 1] : below;
                int sum = below + left + right + heat[(y + 1) % H][x];
                int v = sum / 4;
                v -= rand() % 3;
                if (v < 0) v = 0;
                if (v > 24) v = 24;
                heat[y - 1][x] = (unsigned char)v;
            }
        }
        for (int x = 0; x < W; x++) {
            int v = heat[H - 1][x] + (rand() % 5) - 2;
            if (v < 0) v = 0;
            if (v > 24) v = 24;
            heat[H - 1][x] = (unsigned char)v;
            if (rand() % 3 == 0) heat[H - 1][x] = (unsigned char)(6 + rand() % 8);
        }

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int v = heat[y][x];
                int attr;
                if (v <= 0) attr = 0x00;
                else if (v <= 2) attr = 0x40;
                else if (v <= 5) attr = 0x44;
                else if (v <= 8) attr = 0xC4;
                else if (v <= 12) attr = 0x60;
                else if (v <= 16) attr = 0xE0;
                else attr = 0xF0;
                cells[y][x] = (unsigned short)((' ' | (attr << 8)));
            }
        }
        sp_blit(0, 0, W, H, (const unsigned short *)cells);
        usleep(50000);
        if (keypressed()) break;
    }
    sp_clrscr();
    printf("fire: bye\n");
    return 0;
}
