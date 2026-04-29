/* AArch64 EL1 exception vector table.
 *
 * Each of the 16 entries occupies 0x80 bytes; the whole table must be
 * aligned to a 2 KiB boundary so it can be installed via VBAR_EL1.
 *
 * Only IRQs taken from the current EL using SP_ELx are handled here;
 * everything else is sent to a panic stub.
 */

    .section ".text", "ax"
    .align 11
    .globl  vectors

#define ENTRY_ALIGN .balign 0x80

vectors:
    /* Current EL with SP0 */
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception

    /* Current EL with SPx (kernel mode) */
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __irq_entry
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception

    /* Lower EL (AArch64) */
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception

    /* Lower EL (AArch32) */
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception

__irq_entry:
    sub     sp, sp, #(16 * 16)
    stp     x0,  x1,  [sp, #(16 *  0)]
    stp     x2,  x3,  [sp, #(16 *  1)]
    stp     x4,  x5,  [sp, #(16 *  2)]
    stp     x6,  x7,  [sp, #(16 *  3)]
    stp     x8,  x9,  [sp, #(16 *  4)]
    stp     x10, x11, [sp, #(16 *  5)]
    stp     x12, x13, [sp, #(16 *  6)]
    stp     x14, x15, [sp, #(16 *  7)]
    stp     x16, x17, [sp, #(16 *  8)]
    stp     x18, x19, [sp, #(16 *  9)]
    stp     x20, x21, [sp, #(16 * 10)]
    stp     x22, x23, [sp, #(16 * 11)]
    stp     x24, x25, [sp, #(16 * 12)]
    stp     x26, x27, [sp, #(16 * 13)]
    stp     x28, x29, [sp, #(16 * 14)]
    str     x30,      [sp, #(16 * 15)]

    bl      irq_handler

    ldr     x30,      [sp, #(16 * 15)]
    ldp     x28, x29, [sp, #(16 * 14)]
    ldp     x26, x27, [sp, #(16 * 13)]
    ldp     x24, x25, [sp, #(16 * 12)]
    ldp     x22, x23, [sp, #(16 * 11)]
    ldp     x20, x21, [sp, #(16 * 10)]
    ldp     x18, x19, [sp, #(16 *  9)]
    ldp     x16, x17, [sp, #(16 *  8)]
    ldp     x14, x15, [sp, #(16 *  7)]
    ldp     x12, x13, [sp, #(16 *  6)]
    ldp     x10, x11, [sp, #(16 *  5)]
    ldp     x8,  x9,  [sp, #(16 *  4)]
    ldp     x6,  x7,  [sp, #(16 *  3)]
    ldp     x4,  x5,  [sp, #(16 *  2)]
    ldp     x2,  x3,  [sp, #(16 *  1)]
    ldp     x0,  x1,  [sp, #(16 *  0)]
    add     sp, sp, #(16 * 16)
    eret

__unhandled_exception:
    bl      panic_unhandled
1:
    wfi
    b       1b
