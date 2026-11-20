/* spHalfOS PS/2 mouse driver */
#include "mouse.h"
#include "io.h"
#include "idt.h"
#include "vbe.h"
#include "console.h"

static mouse_state_t ms;
static int packet_idx;
static uint8_t packet[4];
static int packet_len = 3;
static int mouse_present;

static void mouse_wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(0x64) & 0x02)) return;
}

static void mouse_wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(0x64) & 0x01) return;
}

static void mouse_cmd(uint8_t c) {
    mouse_wait_write();
    outb(0x64, 0xD4);
    mouse_wait_write();
    outb(0x60, c);
    mouse_wait_read();
    (void)inb(0x60);
}

void mouse_init(void) {
    int w = vbe_available() ? (int)vbe_width() : 1280;
    int h = vbe_available() ? (int)vbe_height() : 720;
    if (w <= 0) w = 1280;
    if (h <= 0) h = 720;
    ms.x = w / 2; ms.y = h / 2; ms.dx = 0; ms.dy = 0; ms.buttons = 0;
    packet_idx = 0;
    mouse_present = 0;

    /* flush */
    while (inb(0x64) & 0x01) (void)inb(0x60);

    /* enable second PS/2 port */
    mouse_wait_write();
    outb(0x64, 0xA8);
    /* read configuration byte */
    mouse_wait_write();
    outb(0x64, 0x20);
    mouse_wait_read();
    uint8_t cfg = inb(0x60);
    cfg |= 0x02;          /* enable mouse interrupt */
    cfg &= ~0x20;         /* clear mouse clock disable */
    /* write configuration byte */
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, cfg);

    /* enable mouse */
    mouse_cmd(0xF6);      /* defaults */
    mouse_cmd(0xF3);      /* set sample rate */
    mouse_cmd(200);
    mouse_cmd(0xF3);
    mouse_cmd(100);
    mouse_cmd(0xF3);
    mouse_cmd(80);
    mouse_cmd(0xF4);      /* enable data reporting */
    mouse_present = 1;

    console_mouse_update(ms.x, ms.y, ms.buttons);
}

static int sext(uint8_t v, int bits) {
    return (int)(v << (32 - bits)) >> (32 - bits);
}

void mouse_irq(void) {
    if (!mouse_present) return;
    uint8_t b = inb(0x60);
    if (packet_idx == 0 && (b & 0x08) == 0) return;   /* sync */
    packet[packet_idx++] = b;
    if (packet_idx < packet_len) return;
    packet_idx = 0;

    int left = packet[0] & 1;
    int right = (packet[0] >> 1) & 1;
    int middle = (packet[0] >> 2) & 1;
    int dx = sext(packet[1], 8);
    int dy = -sext(packet[2], 8);

    int w = vbe_available() ? (int)vbe_width() : 1280;
    int h = vbe_available() ? (int)vbe_height() : 720;
    if (w <= 0) w = 1280;
    if (h <= 0) h = 720;
    ms.x += dx;
    ms.y += dy;
    if (ms.x < 0) ms.x = 0;
    if (ms.y < 0) ms.y = 0;
    if (ms.x >= w) ms.x = w - 1;
    if (ms.y >= h) ms.y = h - 1;
    ms.dx = dx;
    ms.dy = dy;
    ms.buttons = (left ? 1 : 0) | (right ? 2 : 0) | (middle ? 4 : 0);

    console_mouse_update(ms.x, ms.y, ms.buttons);
}

void mouse_state(mouse_state_t *out) {
    if (!out) return;
    out->x = ms.x;
    out->y = ms.y;
    out->dx = ms.dx;
    out->dy = ms.dy;
    out->buttons = ms.buttons;
}

void mouse_set_pos(int x, int y) {
    int w = vbe_available() ? (int)vbe_width() : 1280;
    int h = vbe_available() ? (int)vbe_height() : 720;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= w) x = w - 1;
    if (y >= h) y = h - 1;
    ms.x = x; ms.y = y;
    ms.dx = 0; ms.dy = 0;
    console_mouse_update(ms.x, ms.y, ms.buttons);
}
