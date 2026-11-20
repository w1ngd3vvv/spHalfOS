/* spHalfOS boot entry -- multiboot2, i386, GRUB jumps here in 32-bit PM */
.set MB2_MAGIC,        0xE85250D6
.set MB2_ARCH_I386,    0
.set MB2_TAG_INFO,     1
.set MB2_TAG_END,      0

.section .multiboot, "a"
.align 8
mb2_header:
    .long MB2_MAGIC
    .long MB2_ARCH_I386
    .long mb2_header_end - mb2_header
    .long 0x100000000 - (MB2_MAGIC + MB2_ARCH_I386 + (mb2_header_end - mb2_header))

    /* information request: basic meminfo (4), memory map (6), framebuffer (8) */
    .align 8
    .short MB2_TAG_INFO
    .short 0
    .long 24
    .long 4
    .long 6
    .long 8

    /* framebuffer request: 1920x1080, 32bpp linear */
    .align 8
    .short 5
    .short 0
    .long 20
    .long 1920
    .long 1080
    .long 32

    /* end tag */
    .align 8
    .short MB2_TAG_END
    .short 0
    .long 8
mb2_header_end:

.section .text
.global _start
.type _start, @function
_start:
    cli
    cld
    movl $kernel_stack_top, %esp
    xorl %ebp, %ebp

    /* kmain(magic, info) */
    pushl %ebx
    pushl %eax
    call kmain
1:  cli
    hlt
    jmp 1b

.section .bss
.align 16
.global kernel_stack_bottom
.global kernel_stack_top
kernel_stack_bottom:
    .skip 65536
kernel_stack_top:
