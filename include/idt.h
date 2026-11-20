#ifndef SP_IDT_H
#define SP_IDT_H

#include "types.h"

void idt_init(void);
void pic_remap(void);
void pic_send_eoi(int irq);
void interrupts_enable(void);
void irq_register(int irq, void (*fn)(void));

/* saved register frame, see isr.S */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t vector, err;
    uint32_t eip, cs, eflags, useresp, ss;
} regs_t;

#endif
