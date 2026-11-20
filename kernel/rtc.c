/* CMOS real-time clock */
#include "io.h"
#include "types.h"

static int cmos_updating(void) {
    outb(0x70, 0x0A);
    return inb(0x71) & 0x80;
}

static uint8_t cmos_read(int reg) {
    outb(0x70, (uint8_t)reg);
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t v) { return (uint8_t)((v & 0x0F) + ((v >> 4) * 10)); }

static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

uint32_t rtc_time(void) {
    int timeout = 100000;
    while (cmos_updating() && timeout--) ;
    uint8_t sec = cmos_read(0x00);
    uint8_t min = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t mon = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);
    uint8_t century = cmos_read(0x32);
    uint8_t status_b = cmos_read(0x0B);

    int binary = status_b & 0x04;
    int h24 = status_b & 0x02;
    if (!binary) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hour = bcd_to_bin(hour & 0x7F) | (hour & 0x80);
        day = bcd_to_bin(day);
        mon = bcd_to_bin(mon);
        year = bcd_to_bin(year);
        if (century) century = bcd_to_bin(century);
    }
    if (!h24 && (hour & 0x80)) hour = (uint8_t)(((hour & 0x7F) + 12) % 24);

    int y = year;
    if (century) y += century * 100;
    else y += 2000;

    int64_t days = days_from_civil(y, mon, day);
    int64_t t = days * 86400 + hour * 3600 + min * 60 + sec;
    return (uint32_t)t;
}
