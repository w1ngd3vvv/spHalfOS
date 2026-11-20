#include "idt.h"
#include "io.h"
#include "kernel.h"

#define PIT_FREQ 1193182u
static volatile uint32_t ticks;

void timer_init(uint32_t hz) {
    uint32_t divisor = PIT_FREQ / hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    ticks = 0;
}

void timer_irq(void) {
    ticks++;
}

uint32_t ticks_get(void) {
    return ticks;
}

void sleep_ticks(uint32_t n) {
    uint32_t end = ticks + n;
    while ((int32_t)(ticks - end) < 0)
        __asm__ volatile ("sti; hlt");
}
