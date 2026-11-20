.section .text
.global gdt_flush
.type gdt_flush, @function
gdt_flush:
    movl 4(%esp), %eax
    lgdt (%eax)
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ljmp $0x08, $1f
1:  ret

.global tss_flush
.type tss_flush, @function
tss_flush:
    movw $0x28, %ax
    ltr %ax
    ret
