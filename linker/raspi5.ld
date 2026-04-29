ENTRY(_start)

SECTIONS
{
    . = 0x80000;
    _kernel_start = .;

    .text : ALIGN(8) {
        KEEP(*(.text.boot))
        *(.text*)
    }

    .rodata : ALIGN(8) {
        *(.rodata*)
    }

    .data : ALIGN(8) {
        *(.data*)
    }

    .bss : ALIGN(8) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(16);
    _kernel_end = .;
    . = . + 0x10000;
    _stack_top = .;
}
