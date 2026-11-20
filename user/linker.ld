ENTRY(_start)
SECTIONS
{
    . = 0x08048000;
    .text : {
        *(.text.start)
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : {
        *(COMMON)
        *(.bss*)
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
