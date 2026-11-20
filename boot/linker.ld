/* spHalfOS kernel linker script (i386, loaded at 1 MiB) */
ENTRY(_start)

SECTIONS
{
    . = 1M;
    _kernel_start = .;

    .multiboot : {
        KEEP(*(.multiboot))
    }

    .text : ALIGN(4096) {
        *(.text*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    }

    _kernel_end = ALIGN(4096);
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
