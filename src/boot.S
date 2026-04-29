.section ".text.boot"
.globl _start

_start:
    mrs     x0, mpidr_el1
    and     x0, x0, #0xff
    cbnz    x0, .Lhalt

    ldr     x0, =_stack_top
    mov     sp, x0

    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
.Lbss_loop:
    cmp     x0, x1
    b.ge    .Lbss_done
    str     xzr, [x0], #8
    b       .Lbss_loop
.Lbss_done:

    bl      kernel_main

.Lhalt:
    wfi
    b       .Lhalt
