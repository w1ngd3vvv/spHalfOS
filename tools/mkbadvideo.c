/*
 * mkbadvideo -- turn raw grayscale frames (stdin) into a compact .bav movie
 * for the spHalfOS badapple player.
 *
 * usage: mkbadvideo OUT WIDTH HEIGHT FPS KEY_INTERVAL < frames.gray
 *
 * Frame format (all little-endian):
 *   header "BAV1"
 *   u16 width, u16 height, u16 fps, u16 flags (1 = delta coded)
 *   u32 frame_count, u32 duration_ms, u32 key_interval
 *   u32 offsets_offset, u32 data_offset, u32 reserved[6]
 *   u32 frame_offset[frame_count]      (absolute file offsets)
 *   frame: u32 payload_len, payload (RLE tokens)
 *
 * RLE token: high bit = colour (1 = white), low 7 bits = run length 1..127.
 * A frame is either a full bitmap or (for delta frames) the XOR of the
 * current bitmap with the previous one.  Every KEY_INTERVAL-th frame is a
 * full keyframe so the player can seek without replaying from the start.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_RUN 127

static void die(const char *msg) {
    fprintf(stderr, "mkbadvideo: %s\n", msg);
    exit(1);
}

static int encode_bits(const uint8_t *bits, int n, uint8_t *out) {
    int pos = 0, o = 0;
    while (pos < n) {
        int colour = bits[pos] ? 1 : 0;
        int run = 0;
        while (pos < n && run < MAX_RUN && (bits[pos] ? 1 : 0) == colour) {
            run++;
            pos++;
        }
        out[o++] = (uint8_t)((colour << 7) | run);
    }
    return o;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: %s OUT WIDTH HEIGHT FPS KEY_INTERVAL < frames.gray\n", argv[0]);
        return 1;
    }
    const char *outpath = argv[1];
    int w = atoi(argv[2]);
    int h = atoi(argv[3]);
    int fps = atoi(argv[4]);
    int key_interval = atoi(argv[5]);
    if (w <= 0 || h <= 0 || w > 1024 || h > 768 || fps <= 0)
        die("bad dimensions/fps");
    if (key_interval <= 0) key_interval = 1;

    const int pixels = w * h;
    uint8_t *cur = malloc((size_t)pixels);
    uint8_t *prev = malloc((size_t)pixels);
    uint8_t *diff = malloc((size_t)pixels);
    uint8_t *payload = malloc((size_t)pixels + 16);
    if (!cur || !prev || !diff || !payload) die("out of memory");
    memset(prev, 0, (size_t)pixels);

    uint32_t *offsets = NULL;
    uint32_t capacity = 0, frames = 0;

    FILE *tmp = tmpfile();
    if (!tmp) die("cannot create temporary file");

    uint8_t *gray = malloc((size_t)pixels);
    if (!gray) die("out of memory");

    for (;;) {
        size_t got = fread(gray, 1, (size_t)pixels, stdin);
        if (got == 0) break;
        if (got != (size_t)pixels) die("short frame on stdin");

        for (int i = 0; i < pixels; i++)
            cur[i] = gray[i] >= 128 ? 1 : 0;

        int key = (frames % (uint32_t)key_interval) == 0;
        const uint8_t *bits;
        if (key) {
            bits = cur;
        } else {
            for (int i = 0; i < pixels; i++)
                diff[i] = (uint8_t)(cur[i] ^ prev[i]);
            bits = diff;
        }
        int len = encode_bits(bits, pixels, payload);

        if (frames == capacity) {
            capacity = capacity ? capacity * 2 : 1024;
            offsets = realloc(offsets, capacity * sizeof(uint32_t));
            if (!offsets) die("out of memory");
        }

        long pos = ftell(tmp);
        if (pos < 0) die("ftell failed");
        uint32_t ulen = (uint32_t)len;
        if (fwrite(&ulen, sizeof(ulen), 1, tmp) != 1) die("temp write failed");
        if (fwrite(payload, 1, (size_t)len, tmp) != (size_t)len) die("temp write failed");
        offsets[frames] = 0; /* patched after data_offset is known */
        frames++;

        memcpy(prev, cur, (size_t)pixels);

        if ((frames % 500) == 0)
            fprintf(stderr, "mkbadvideo: %u frames\n", frames);
    }

    if (frames == 0) die("no frames on stdin");

    const uint32_t header_size = 48;
    uint32_t data_offset = header_size + frames * 4;
    data_offset = (data_offset + 3) & ~3u;

    FILE *out = fopen(outpath, "wb");
    if (!out) die("cannot create output");

    uint8_t header[48];
    memset(header, 0, sizeof(header));
    memcpy(header + 0, "BAV1", 4);
    uint16_t w16 = (uint16_t)w, h16 = (uint16_t)h, f16 = (uint16_t)fps;
    uint16_t flags = 1;
    memcpy(header + 4, &w16, 2);
    memcpy(header + 6, &h16, 2);
    memcpy(header + 8, &f16, 2);
    memcpy(header + 10, &flags, 2);
    uint32_t dur = (uint32_t)(((uint64_t)frames * 1000u) / (uint32_t)fps);
    uint32_t ki = (uint32_t)key_interval;
    uint32_t off_pos = header_size, data_pos = data_offset;
    memcpy(header + 12, &frames, 4);
    memcpy(header + 16, &dur, 4);
    memcpy(header + 20, &ki, 4);
    memcpy(header + 24, &off_pos, 4);
    memcpy(header + 28, &data_pos, 4);
    if (fwrite(header, 1, sizeof(header), out) != sizeof(header)) die("write failed");

    /* patch offsets: frame i begins at data_offset + sum(4 + len_j) */
    if (fseek(tmp, 0, SEEK_SET) != 0) die("seek failed");
    uint32_t at = data_offset;
    for (uint32_t i = 0; i < frames; i++) {
        offsets[i] = at;
        if (fwrite(&offsets[i], 4, 1, out) != 1) die("write failed");
        uint32_t len = 0;
        if (fread(&len, 4, 1, tmp) != 1) die("temp read failed");
        at += 4 + len;
        if (fseek(tmp, (long)len, SEEK_CUR) != 0) die("temp seek failed");
    }
    if (ftell(out) < (long)data_offset) {
        while (ftell(out) < (long)data_offset) fputc(0, out);
    }
    if (fseek(tmp, 0, SEEK_SET) != 0) die("seek failed");
    uint8_t copybuf[65536];
    size_t n;
    while ((n = fread(copybuf, 1, sizeof(copybuf), tmp)) > 0) {
        if (fwrite(copybuf, 1, n, out) != n) die("write failed");
    }
    fclose(tmp);
    if (fclose(out) != 0) die("close failed");

    fprintf(stderr, "mkbadvideo: wrote %s, %u frames, %ux%u @ %d fps, key every %d\n",
            outpath, frames, w, h, fps, key_interval);
    return 0;
}
