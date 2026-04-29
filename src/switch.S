/*
 * void switch_context(uint64_t *old_sp, uint64_t new_sp);
 *
 *  x0 -> address where the outgoing task's SP must be stored
 *  x1 -> the SP of the incoming task
 *
 * Saves the AArch64 callee-saved general registers (x19..x29 plus the
 * link register) of the current task on its stack, swaps SP, then
 * restores the same set from the incoming stack and returns. The
 * caller is responsible for keeping the rest of the architectural
 * state (it's only called from C, so the C ABI takes care of x0..x18).
 */

    .section ".text", "ax"
    .global  switch_context

switch_context:
    sub     sp, sp, #(12 * 8)
    stp     x19, x20, [sp, #(0  * 8)]
    stp     x21, x22, [sp, #(2  * 8)]
    stp     x23, x24, [sp, #(4  * 8)]
    stp     x25, x26, [sp, #(6  * 8)]
    stp     x27, x28, [sp, #(8  * 8)]
    stp     x29, x30, [sp, #(10 * 8)]

    mov     x2, sp
    str     x2, [x0]

    mov     sp, x1

    ldp     x19, x20, [sp, #(0  * 8)]
    ldp     x21, x22, [sp, #(2  * 8)]
    ldp     x23, x24, [sp, #(4  * 8)]
    ldp     x25, x26, [sp, #(6  * 8)]
    ldp     x27, x28, [sp, #(8  * 8)]
    ldp     x29, x30, [sp, #(10 * 8)]
    add     sp, sp, #(12 * 8)

    ret
