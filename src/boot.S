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

#ifdef BOARD_HAS_GIC
    ldr     x0, =vectors
    msr     vbar_el1, x0
    isb
#endif

    bl      kernel_main

.Lhalt:
    wfi
    b       .Lhalt

/*
 * Secondary cores brought up via PSCI CPU_ON land here. Each core
 * picks a 16 KiB private stack from secondary_stacks indexed by its
 * affinity-0 byte, installs the shared vector table, and calls into C.
 */
.globl secondary_start
secondary_start:
    mrs     x0, mpidr_el1
    and     x0, x0, #0xff

    /* sp = &secondary_stacks[(core_id+1) * 16384] */
    ldr     x1, =secondary_stacks
    add     x2, x0, #1
    lsl     x2, x2, #14
    add     sp, x1, x2

#ifdef BOARD_HAS_GIC
    ldr     x1, =vectors
    msr     vbar_el1, x1
    isb
#endif

    bl      secondary_main
.Lhalt2:
    wfi
    b       .Lhalt2
