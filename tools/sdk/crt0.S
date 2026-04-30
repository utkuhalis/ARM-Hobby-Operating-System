/*
 * Hobby ARM OS user-space crt0.
 *
 * The kernel jumps to _start with a clean stack at EL0. We just call
 * hobby_main, then issue SYS_EXIT (number 1 in x8) so the kernel
 * reaps us if main ever returns.
 */

    .section .text.boot, "ax", %progbits
    .global _start
    .type _start, %function
_start:
    bl      hobby_main
    mov     x8, #1          /* SYS_EXIT */
    svc     #0
1:  wfi
    b       1b
    .size _start, . - _start
