/*
 * User-mode linker script for Hobby ARM OS packages.
 *
 * Programs are static ELFs loaded by elf_load() at the virtual
 * address fixed below. The address is well above the boot heap and
 * stack but inside the 256 MiB of RAM the kernel has at qemu-virt.
 *
 * The kernel currently runs user code in its own address space (no
 * EL0 isolation yet), so we just need the program's PT_LOAD range to
 * not collide with the kernel image, heap or stack.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x4F000000;
    _user_start = .;

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
        *(.bss*)
        *(COMMON)
    }

    . = ALIGN(16);
    _user_end = .;
}
