.section .text

.macro ISR_NOERR num, name
.global \name
\name:
    pushl $0
    pushl $\num
    jmp isr_common
.endm

.macro ISR_ERR num, name
.global \name
\name:
    pushl $\num
    jmp isr_common
.endm

.macro IRQ num, name
.global \name
\name:
    pushl $0
    pushl $\num
    jmp isr_common
.endm

ISR_NOERR 0,  isr0
ISR_NOERR 1,  isr1
ISR_NOERR 2,  isr2
ISR_NOERR 3,  isr3
ISR_NOERR 4,  isr4
ISR_NOERR 5,  isr5
ISR_NOERR 6,  isr6
ISR_NOERR 7,  isr7
ISR_ERR   8,  isr8
ISR_NOERR 9,  isr9
ISR_ERR   10, isr10
ISR_ERR   11, isr11
ISR_ERR   12, isr12
ISR_ERR   13, isr13
ISR_ERR   14, isr14
ISR_NOERR 15, isr15
ISR_NOERR 16, isr16
ISR_ERR   17, isr17
ISR_NOERR 18, isr18
ISR_NOERR 19, isr19
ISR_NOERR 20, isr20
ISR_NOERR 21, isr21
ISR_NOERR 22, isr22
ISR_NOERR 23, isr23
ISR_NOERR 24, isr24
ISR_NOERR 25, isr25
ISR_NOERR 26, isr26
ISR_NOERR 27, isr27
ISR_NOERR 28, isr28
ISR_NOERR 29, isr29
ISR_ERR   30, isr30
ISR_NOERR 31, isr31

IRQ 32, irq0
IRQ 33, irq1
IRQ 34, irq2
IRQ 35, irq3
IRQ 36, irq4
IRQ 37, irq5
IRQ 38, irq6
IRQ 39, irq7
IRQ 40, irq8
IRQ 41, irq9
IRQ 42, irq10
IRQ 43, irq11
IRQ 44, irq12
IRQ 45, irq13
IRQ 46, irq14
IRQ 47, irq15

.global isr_syscall
isr_syscall:
    pushl $0
    pushl $0x80
    jmp isr_common

.global isr_default
isr_default:
    pushl $0
    pushl $0xFF
    jmp isr_common

isr_common:
    pushal
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    pushl %esp
    call interrupt_handler
    addl $4, %esp

    popl %gs
    popl %fs
    popl %es
    popl %ds
    popal
    addl $8, %esp
    iret

/* void resume_context(uint32_t ksp) -- never returns */
.global resume_context
resume_context:
    movl 4(%esp), %esp
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popal
    addl $8, %esp
    iret

.section .rodata
.align 4
.global isr_stub_table
isr_stub_table:
    .long isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    .long isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    .long isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    .long isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    .long irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
    .long irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
    .rept 80
    .long isr_default
    .endr
    .long isr_syscall
    .rept 127
    .long isr_default
    .endr
