#include "idt.h"
#include "io.h"
#include "kernel.h"
#include "process.h"
#include "console.h"

typedef struct PACKED {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t offset_high;
} idt_entry_t;

typedef struct PACKED {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;

extern uint32_t isr_stub_table[256];
extern void timer_irq(void);
extern void keyboard_irq(void);
extern void serial_irq(void);
extern void mouse_irq(void);
extern int  syscall_dispatch(regs_t *r);
extern void sched_timer_tick(regs_t *r);

static void (*irq_handlers[16])(void);

static void idt_set_gate(int n, uint32_t handler, uint16_t sel, uint8_t flags) {
    idt[n].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[n].selector = sel;
    idt[n].zero = 0;
    idt[n].flags = flags;
    idt[n].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void pic_remap(void) {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();
    /* mask all IRQs by default; drivers unmask as needed */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

void irq_register(int irq, void (*fn)(void)) {
    if (irq < 0 || irq > 15) return;
    irq_handlers[irq] = fn;
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t bit = (uint8_t)(1u << (irq & 7));
    outb(port, inb(port) & ~bit);
    if (irq >= 8) outb(0x21, inb(0x21) & ~0x04); /* ensure cascade */
}

void pic_send_eoi(int irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint32_t)&idt;
    for (int i = 0; i < 256; i++) {
        uint8_t flags = 0x8E;
        if (i == 0x80) flags = 0xEE;
        idt_set_gate(i, isr_stub_table[i], 0x08, flags);
    }
    __asm__ volatile ("lidt %0" :: "m"(idt_ptr));
    pic_remap();
    /* base devices */
    irq_register(0, timer_irq);
    irq_register(1, keyboard_irq);
    irq_register(4, serial_irq);
    irq_register(12, mouse_irq);
}

void interrupts_enable(void) {
    cpu_sti();
}

static const char *exception_names[32] = {
    "Divide by zero", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range",
    "Invalid opcode", "Device not available", "Double fault", "Coprocessor segment",
    "Invalid TSS", "Segment not present", "Stack fault", "General protection",
    "Page fault", "Reserved", "x87 FP error", "Alignment check", "Machine check",
    "SIMD FP error", "Virtualization", "Control protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Hypervisor", "VMM comm",
    "Security", "Reserved"
};

void interrupt_handler(regs_t *r) {
    uint32_t v = r->vector;

    if (v < 32) {
        uint32_t cr2 = 0;
        if (v == 14) cr2 = read_cr2();
        if ((r->cs & 3) == 3 && current) {
            kprintf("\n[exc] pid %d (%s) %s err=%x eip=%x cr2=%x -- killed\n",
                    current->pid, current->name, exception_names[v], r->err,
                    r->eip, cr2);
            process_exit(r, 128 + (int)v);
        }
        panic("CPU exception %u: %s\n  err=%x eip=%x cs=%x eflags=%x cr2=%x\n  pid-context unknown",
              v, exception_names[v], r->err, r->eip, r->cs, r->eflags, cr2);
    }

    if (v >= 32 && v < 48) {
        int irq = (int)v - 32;
        if (irq_handlers[irq]) irq_handlers[irq]();
        pic_send_eoi(irq);
        if (irq == 0) sched_timer_tick(r);
        return;
    }

    if (v == 0x80) {
        r->eax = (uint32_t)syscall_dispatch(r);
        return;
    }

    /* spurious */
    if (v == 0xFF) return;
    panic("unexpected interrupt vector %u", v);
}
