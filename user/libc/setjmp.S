.section .text
.global setjmp
.global _setjmp
.type setjmp,@function
.type _setjmp,@function
setjmp:
_setjmp:
    movl 4(%esp), %eax
    movl %ebx, 0(%eax)
    movl %esi, 4(%eax)
    movl %edi, 8(%eax)
    movl %ebp, 12(%eax)
    movl %esp, 16(%eax)
    movl (%esp), %edx
    movl %edx, 20(%eax)
    xorl %eax, %eax
    ret

.global longjmp
.global _longjmp
.type longjmp,@function
.type _longjmp,@function
longjmp:
_longjmp:
    movl 4(%esp), %ecx
    movl 8(%esp), %eax
    testl %eax, %eax
    jnz 1f
    incl %eax
1:
    movl 0(%ecx), %ebx
    movl 4(%ecx), %esi
    movl 8(%ecx), %edi
    movl 12(%ecx), %ebp
    movl 16(%ecx), %esp
    jmp *20(%ecx)

.section .note.GNU-stack,"",@progbits
