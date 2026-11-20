.section .text
.global _start
.type _start,@function
_start:
    xorl %ebp, %ebp
    movl (%esp), %eax
    leal 4(%esp), %ebx
    pushl $0
    pushl %ebx
    pushl %eax
    call main
    pushl %eax
    call exit
1:  jmp 1b

.section .note.GNU-stack,"",@progbits
