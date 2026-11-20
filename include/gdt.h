#ifndef SP_GDT_H
#define SP_GDT_H

#include "types.h"

#define GDT_KERNEL_CS 0x08
#define GDT_KERNEL_DS 0x10
#define GDT_USER_CS   0x18
#define GDT_USER_DS   0x20
#define GDT_TSS       0x28

#define USER_CS (GDT_USER_CS | 3)
#define USER_DS (GDT_USER_DS | 3)

void gdt_init(void);
void tss_set_esp0(uint32_t esp0);

#endif
