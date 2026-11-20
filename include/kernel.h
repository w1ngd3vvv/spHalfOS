#ifndef SP_KERNEL_H
#define SP_KERNEL_H

#include "types.h"

#define KERNEL_VERSION "0.1.0"

void kprintf(const char *fmt, ...);
void kputs(const char *s);
void kputc(char c);
void panic(const char *fmt, ...) NORETURN;
void reboot_now(void) NORETURN;

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s, size_t n);
void serial_puts(const char *s);
int serial_rx_ready(void);
int serial_read(void);
void serial_irq(void);

void timer_init(uint32_t hz);
void keyboard_init(void);

/* memory */
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t align);

uint32_t ticks_get(void);
void sleep_ticks(uint32_t n);

#endif
