/* AArch64 EL1 exception vector table.
 *
 * IRQs from current EL with SPx and synchronous exceptions from a
 * lower EL (EL0) are wired up; the rest jump to a panic stub.
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
    b       __sync_lower
    ENTRY_ALIGN
    b       __irq_entry
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception

    /* Lower EL using AArch64 (EL0) */
    ENTRY_ALIGN
    b       __sync_lower
    ENTRY_ALIGN
    b       __irq_entry
    ENTRY_ALIGN
    b       __unhandled_exception
    ENTRY_ALIGN
    b       __unhandled_exception

    /* Lower EL using AArch32 */
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

/*
 * Synchronous exception from EL0. Save the full GP register set plus
 * SP_EL0/ELR_EL1/SPSR_EL1 into a 34-dword frame on the kernel stack,
 * pass its address to the C handler, then restore and ERET.
 */
__sync_lower:
    sub     sp, sp, #(34 * 8)
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
    str     x30,      [sp, #(8  * 30)]
    mrs     x9,  sp_el0
    str     x9,       [sp, #(8  * 31)]
    mrs     x9,  elr_el1
    str     x9,       [sp, #(8  * 32)]
    mrs     x9,  spsr_el1
    str     x9,       [sp, #(8  * 33)]

    mov     x0, sp
    bl      sync_handler

    ldr     x9,       [sp, #(8  * 33)]
    msr     spsr_el1, x9
    ldr     x9,       [sp, #(8  * 32)]
    msr     elr_el1, x9
    ldr     x9,       [sp, #(8  * 31)]
    msr     sp_el0, x9
    ldr     x30,      [sp, #(8  * 30)]
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
    add     sp, sp, #(34 * 8)
    eret

__unhandled_exception:
    bl      panic_unhandled
1:
    wfi
    b       1b
