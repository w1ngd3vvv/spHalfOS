/* ATA PIO driver (primary master, LBA28) */
#include "ata.h"
#include "io.h"
#include "kernel.h"

#define ATA_IO   0x1F0
#define ATA_CTRL 0x3F6

static uint32_t disk_sectors;
static int present;

static int wait_status(uint8_t mask, uint8_t value, int timeout) {
    while (timeout-- > 0) {
        uint8_t st = inb(ATA_IO + 7);
        if ((st & 0x80) == 0 && (st & mask) == value) return 0;
        if (st & 0x01) return -1;   /* ERR */
    }
    return -2;
}

static void delay400(void) {
    for (int i = 0; i < 4; i++) (void)inb(ATA_CTRL);
}

int ata_init(void) {
    present = 0;
    disk_sectors = 0;

    outb(ATA_CTRL, 0x02);   /* nIEN, we poll */
    outb(ATA_IO + 6, 0xA0); /* master */
    delay400();

    outb(ATA_IO + 2, 0);
    outb(ATA_IO + 3, 0);
    outb(ATA_IO + 4, 0);
    outb(ATA_IO + 5, 0);
    outb(ATA_IO + 7, 0xEC); /* IDENTIFY */
    uint8_t st = inb(ATA_IO + 7);
    kprintf("[ata] identify status=%x\n", st);
    if (st == 0) return -1;

    int wr = wait_status(0x08, 0x08, 1000000);
    if (wr < 0) { kprintf("[ata] identify wait failed %d st=%x\n", wr, inb(ATA_IO + 7)); return -1; }
    uint16_t ident[256];
    for (int i = 0; i < 256; i++) ident[i] = inw(ATA_IO);
    (void)ident;

    disk_sectors = ((uint32_t)ident[61] << 16) | ident[60];
    kprintf("[ata] sectors=%u\n", disk_sectors);
    if (!disk_sectors) {
        /* some images report 0; use a conservative default */
        disk_sectors = 0;
        return -1;
    }
    present = 1;
    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf) {
    if (!present) return -1;
    if (lba + count > disk_sectors) return -1;
    if (wait_status(0x40, 0x40, 1000000) < 0) return -1;

    outb(ATA_IO + 6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_IO + 2, count);
    outb(ATA_IO + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_IO + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_IO + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_IO + 7, 0x20);

    uint16_t *p = (uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (wait_status(0x08, 0x08, 1000000) < 0) return -1;
        for (int i = 0; i < 256; i++) *p++ = inw(ATA_IO);
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf) {
    if (!present) return -1;
    if (lba + count > disk_sectors) return -1;
    if (wait_status(0x40, 0x40, 1000000) < 0) return -1;

    outb(ATA_IO + 6, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_IO + 2, count);
    outb(ATA_IO + 3, (uint8_t)(lba & 0xFF));
    outb(ATA_IO + 4, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_IO + 5, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_IO + 7, 0x30);

    const uint16_t *p = (const uint16_t *)buf;
    for (int s = 0; s < count; s++) {
        if (wait_status(0x08, 0x08, 1000000) < 0) return -1;
        for (int i = 0; i < 256; i++) outw(ATA_IO, *p++);
        if (wait_status(0x40, 0x40, 1000000) < 0) return -1;
    }
    outb(ATA_IO + 7, 0xE7);   /* flush */
    wait_status(0x40, 0x40, 1000000);
    return 0;
}

uint32_t ata_disk_sectors(void) { return disk_sectors; }
