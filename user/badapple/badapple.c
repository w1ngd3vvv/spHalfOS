/* spHalfOS badapple -- plays /usr/share/badapple.bav (BAV1) full screen.
 *
 * Controls:
 *   q / Esc / Ctrl-C   quit
 *   space / p          pause
 *   left / right       seek -5s / +5s
 *   up / down          seek -30s / +30s
 *   r                  restart
 *   i                  invert colours
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <syscall.h>
#include <fb.h>

#define BAV_PATH "/usr/share/badapple.bav"
#define BAV_MAX_W 512
#define BAV_MAX_H 512
#define BAV_MAX_FRAMES 20000
#define BAV_MAX_PAYLOAD 65536

typedef struct __attribute__((packed)) {
    char     magic[4];
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    uint16_t flags;
    uint32_t frame_count;
    uint32_t duration_ms;
    uint32_t key_interval;
    uint32_t offsets_off;
    uint32_t data_off;
    uint32_t reserved[6];
} bav_header_t;

static bav_header_t hdr;
static uint32_t *offsets;
static uint8_t *bitmap;
static uint8_t *payload;
static uint8_t *framebuffer;
static sp_fbinfo_t fbinfo;

static int movie_fd = -1;
static uint32_t pixels;
static int invert;
static int paused;
static int quit;
static int redraw;
static int indexed;              /* next frame to play */
static int displayed = -1;       /* last frame shown */
static int seek_req = -1;
static uint32_t play_start;

static int read_full(int fd, void *buf, uint32_t n) {
    uint8_t *p = (uint8_t *)buf;
    uint32_t got = 0;
    while (got < n) {
        int r = read(fd, p + got, n - got);
        if (r <= 0) return -1;
        got += (uint32_t)r;
    }
    return 0;
}

static uint32_t frame_ticks(uint32_t frame) {
    /* PIT runs at 60 Hz */
    return ((frame * 60u) / hdr.fps);
}

static int load_frame(uint32_t index) {
    if (index >= hdr.frame_count) return -1;
    if (lseek(movie_fd, (off_t)offsets[index], SEEK_SET) < 0) return -1;
    uint32_t len;
    if (read_full(movie_fd, &len, 4) < 0) return -1;
    if (len > BAV_MAX_PAYLOAD) return -1;
    if (read_full(movie_fd, payload, len) < 0) return -1;

    int key = (index % hdr.key_interval) == 0;
    if (key) memset(bitmap, 0, pixels);

    uint32_t pos = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t tok = payload[i];
        uint32_t run = tok & 0x7Fu;
        if (!run) return -1;
        if (pos + run > pixels) return -1;
        if (tok & 0x80) {
            if (key) {
                memset(bitmap + pos, 1, run);
            } else {
                for (uint32_t j = 0; j < run; j++) bitmap[pos + j] ^= 1;
            }
        }
        pos += run;
    }
    if (key && pos != pixels) return -1;
    return 0;
}

static int seek_to(uint32_t target) {
    uint32_t key = (target / hdr.key_interval) * hdr.key_interval;
    if (load_frame(key) < 0) return -1;
    for (uint32_t i = key + 1; i <= target; i++)
        if (load_frame(i) < 0) return -1;
    return 0;
}

static void render(void) {
    int sw = (int)fbinfo.width, sh = (int)fbinfo.height;
    int vw = hdr.width, vh = hdr.height;
    uint32_t white = invert ? 0x00000000u : 0x00FFFFFFu;
    uint32_t black = invert ? 0x00FFFFFFu : 0x00000000u;
    uint32_t *fb32 = (uint32_t *)framebuffer;

    int dw = sw, dh = (sw * vh) / vw;
    if (dh > sh) { dh = sh; dw = (sh * vw) / vh; }
    int ox = (sw - dw) / 2, oy = (sh - dh) / 2;
    for (int y = 0; y < sh; y++) {
        int sy = (y >= oy && y < oy + dh) ? ((y - oy) * vh) / dh : -1;
        uint32_t *row = &fb32[y * sw];
        for (int x = 0; x < sw; x++) {
            uint32_t c = black;
            if (sy >= 0 && x >= ox && x < ox + dw) {
                int sx = ((x - ox) * vw) / dw;
                c = bitmap[sy * vw + sx] ? white : black;
            }
            row[x] = c;
        }
    }
    sp_win_present(fb32, sw, sh);
}

static void start_clock(void) {
    play_start = (uint32_t)sp_ticks() - frame_ticks((uint32_t)indexed);
}

static void handle_key(int c) {
    if (c == 27) {
        int c2 = sp_getkey();
        if (c2 < 0) { quit = 1; return; }
        if (c2 == '[' || c2 == 'O') {
            int c3 = sp_getkey();
            int step = hdr.fps * 5;
            int target = -1;
            if (c3 == 'C') target = indexed + step;
            else if (c3 == 'D') target = indexed - step;
            else if (c3 == 'B') target = indexed + step * 6;
            else if (c3 == 'A') target = indexed - step * 6;
            if (target < 0) target = 0;
            if (target >= (int)hdr.frame_count) target = (int)hdr.frame_count - 1;
            if (target != indexed && seek_req < 0) seek_req = target;
        }
        return;
    }
    if (c == 3 || c == 'q' || c == 'Q') { quit = 1; return; }
    if (c == ' ' || c == 'p' || c == 'P') {
        paused = !paused;
        if (!paused) {
            /* a paused seek already displayed this frame; don't apply it twice */
            if (displayed >= 0 && displayed == indexed && indexed < (int)hdr.frame_count)
                indexed++;
            start_clock();
        }
        return;
    }
    if (c == 'i' || c == 'I') { invert = !invert; redraw = 1; return; }
    if (c == 'r' || c == 'R') { if (seek_req < 0) seek_req = 0; return; }
}

static void pump_events(void) {
    int c;
    while (!quit && (c = sp_getkey()) >= 0)
        handle_key(c);
}

static void clear_screen(void) {
    if (!framebuffer) return;
    uint32_t total = (uint32_t)fbinfo.width * (uint32_t)fbinfo.height;
    uint32_t *p = (uint32_t *)framebuffer;
    for (uint32_t i = 0; i < total; i++) p[i] = 0;
}

static void fail(const char *msg) {
    sp_clrscr();
    fprintf(stderr, "badapple: %s\n", msg);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    movie_fd = open(BAV_PATH, O_RDONLY);
    if (movie_fd < 0) {
        printf("badapple: %s not found; run 'make badapple-data' on the host\n", BAV_PATH);
        return 1;
    }
    if (read_full(movie_fd, &hdr, sizeof(hdr)) < 0 ||
        memcmp(hdr.magic, "BAV1", 4) || hdr.width == 0 || hdr.height == 0 ||
        hdr.width > BAV_MAX_W || hdr.height > BAV_MAX_H ||
        hdr.fps == 0 || hdr.frame_count == 0 ||
        hdr.frame_count > BAV_MAX_FRAMES || hdr.key_interval == 0) {
        close(movie_fd);
        printf("badapple: bad movie file\n");
        return 1;
    }

    pixels = (uint32_t)hdr.width * hdr.height;
    offsets = malloc(hdr.frame_count * sizeof(uint32_t));
    bitmap = malloc(pixels);
    payload = malloc(BAV_MAX_PAYLOAD);
    if (!offsets || !bitmap || !payload) {
        fail("out of memory");
        return 1;
    }
    if (lseek(movie_fd, (off_t)hdr.offsets_off, SEEK_SET) < 0 ||
        read_full(movie_fd, offsets, hdr.frame_count * sizeof(uint32_t)) < 0) {
        fail("cannot read frame table");
        return 1;
    }

    if (fb_info(&fbinfo) < 0 || !fbinfo.available || fbinfo.bpp != 32) {
        fbinfo.width = 720; fbinfo.height = 405; fbinfo.bpp = 32; fbinfo.pitch = 720 * 4; fbinfo.available = 1;
    }
    if (fbinfo.width > 960) fbinfo.width = 960;
    if (fbinfo.height > 540) fbinfo.height = 540;
    fbinfo.pitch = fbinfo.width * 4;
    framebuffer = (uint8_t *)malloc((size_t)fbinfo.width * fbinfo.height * 4);
    if (!framebuffer) {
        fail("out of memory");
        return 1;
    }

    sp_clrscr();
    clear_screen();
    indexed = 0;
    displayed = -1;
    paused = 0;
    invert = 0;
    quit = 0;
    redraw = 0;
    seek_req = -1;
    start_clock();

    while (!quit) {
        pump_events();
        if (quit) break;

        if (seek_req >= 0) {
            uint32_t target = (uint32_t)seek_req;
            seek_req = -1;
            if (target >= hdr.frame_count) target = hdr.frame_count - 1;
            if (seek_to(target) < 0) { fail("movie decode error"); break; }
            indexed = (int)target;
            displayed = (int)target;
            render();
            redraw = 0;
            if (!paused) {
                indexed++;
                start_clock();
            }
            continue;
        }

        if (paused) {
            pump_events();
            if (quit) break;
            if (seek_req >= 0) continue;
            if (redraw) { render(); redraw = 0; }
            usleep(20000);
            continue;
        }

        if (indexed >= (int)hdr.frame_count) break;

        uint32_t due = play_start + frame_ticks((uint32_t)indexed);
        while ((int32_t)((uint32_t)sp_ticks() - due) < 0) {
            pump_events();
            if (quit || paused || seek_req >= 0) break;
            syscall0(SYS_YIELD);
        }
        if (quit) break;
        if (paused || seek_req >= 0) continue;

        if (load_frame((uint32_t)indexed) < 0) { fail("movie decode error"); break; }
        int late = (int32_t)((uint32_t)sp_ticks() - due);
        if (late <= 5) {
            render();
            displayed = indexed;
        }
        indexed++;
        redraw = 0;
    }

    /* let the final frame breathe if the movie ended on its own */
    if (!quit && displayed >= 0) {
        uint32_t hold = (uint32_t)sp_ticks() + 150;
        while ((int32_t)((uint32_t)sp_ticks() - hold) < 0 && !quit) {
            pump_events();
            usleep(20000);
        }
    }

    if (movie_fd >= 0) close(movie_fd);
    sp_clrscr();
    if (!quit)
        printf("badapple: done (%u frames, %u.%us)\n",
               hdr.frame_count, hdr.duration_ms / 1000u, (hdr.duration_ms % 1000u) / 100u);
    return 0;
}
