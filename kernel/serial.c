/* spHalfOS serial port (COM1) + kernel printf */
#include "kernel.h"
#include "console.h"
#include "io.h"
#include "stdarg.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);    /* disable interrupts */
    outb(COM1 + 3, 0x80);    /* DLAB */
    outb(COM1 + 0, 0x01);    /* divisor 1 = 115200 */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);    /* 8N1 */
    outb(COM1 + 2, 0xC7);    /* FIFO enable, clear */
    outb(COM1 + 4, 0x0B);    /* IRQs enabled, RTS/DSR */
    outb(COM1 + 0, 0x00);
    outb(COM1 + 1, 0x01);    /* only "received data" interrupt */
    (void)inb(COM1 + 5);
}

void serial_irq(void) {
    /* drain the FIFO; translate terminal conventions to our keyboard bytes */
    while (serial_rx_ready()) {
        int c = serial_read();
        if (c == '\r') c = '\n';
        if (c == 0x7F) c = '\b';
        console_input_push((uint8_t)c);
    }
}

void serial_putc(char c) {
    int timeout = 100000;
    while (!(inb(COM1 + 5) & 0x20) && --timeout) ;
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n') serial_putc('\r');
        serial_putc(s[i]);
    }
}

void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

int serial_rx_ready(void) { return inb(COM1 + 5) & 1; }
int serial_read(void) { return inb(COM1); }

/* ---------------- kernel printf ---------------- */

typedef struct {
    int count;
    void (*emit)(char c, void *ctx);
    void *ctx;
} fmt_out_t;

static void emit_both(char c, void *ctx) {
    (void)ctx;
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    console_putc(c);
}

static void out_char(fmt_out_t *o, char c) {
    o->emit(c, o->ctx);
    o->count++;
}

static void out_str(fmt_out_t *o, const char *s) {
    if (!s) s = "(null)";
    while (*s) out_char(o, *s++);
}

static void out_uint(fmt_out_t *o, uint32_t v, int base, int upper, int width, char pad) {
    char tmp[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = digits[v % (uint32_t)base]; v /= (uint32_t)base; }
    while (width-- > n) out_char(o, pad);
    while (n--) out_char(o, tmp[n]);
}

static void out_int(fmt_out_t *o, int32_t v, int width, char pad) {
    if (v < 0) {
        out_char(o, '-');
        if (width > 0) width--;
        out_uint(o, (uint32_t)(-(v + 1)) + 1u, 10, 0, width, pad);
    } else {
        out_uint(o, (uint32_t)v, 10, 0, width, pad);
    }
}

void kputc(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
    console_putc(c);
}

static void kvprintf_impl(fmt_out_t *o, const char *fmt, va_list ap) {
    while (*fmt) {
        if (*fmt != '%') { out_char(o, *fmt++); continue; }
        fmt++;
        if (*fmt == '%') { out_char(o, '%'); fmt++; continue; }
        char pad = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        switch (*fmt++) {
        case 'd': case 'i':
            if (lng >= 2) {
                long long v = va_arg(ap, long long);
                if (v < 0) { out_char(o, '-'); out_uint(o, (uint32_t)(-v), 10, 0, width, pad); }
                else out_uint(o, (uint32_t)v, 10, 0, width, pad);
            } else out_int(o, va_arg(ap, int), width, pad);
            break;
        case 'u':
            out_uint(o, (uint32_t)va_arg(ap, unsigned int), 10, 0, width, pad);
            break;
        case 'x':
            out_uint(o, (uint32_t)va_arg(ap, unsigned int), 16, 0, width, pad);
            break;
        case 'X':
            out_uint(o, (uint32_t)va_arg(ap, unsigned int), 16, 1, width, pad);
            break;
        case 'o':
            out_uint(o, (uint32_t)va_arg(ap, unsigned int), 8, 0, width, pad);
            break;
        case 'p':
            out_str(o, "0x");
            out_uint(o, (uint32_t)va_arg(ap, void *), 16, 0, 8, '0');
            break;
        case 'c':
            out_char(o, (char)va_arg(ap, int));
            break;
        case 's':
            out_str(o, va_arg(ap, const char *));
            break;
        default:
            out_char(o, '?');
            break;
        }
    }
}

void kprintf(const char *fmt, ...) {
    fmt_out_t o = { 0, emit_both, NULL };
    va_list ap;
    va_start(ap, fmt);
    kvprintf_impl(&o, fmt, ap);
    va_end(ap);
}

void kputs(const char *s) {
    while (*s) kputc(*s++);
}

void panic(const char *fmt, ...) {
    cpu_cli();
    kputs("\n*** spHalfOS PANIC ***\n");
    fmt_out_t o = { 0, emit_both, NULL };
    va_list ap;
    va_start(ap, fmt);
    kvprintf_impl(&o, fmt, ap);
    va_end(ap);
    kputs("\nSystem halted.\n");
    for (;;) {
        cpu_cli();
        cpu_hlt();
    }
}

void reboot_now(void) {
    kputs("Rebooting...\n");
    /* try the 8042 reset line, then a triple fault */
    for (int i = 0; i < 100; i++) {
        if ((inb(0x64) & 0x02) == 0) break;
    }
    outb(0x64, 0xFE);
    cpu_cli();
    for (;;) cpu_hlt();
}
