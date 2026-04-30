#include <stdint.h>
#include "exceptions.h"
#include "console.h"
#include "gic.h"
#include "timer.h"
#include "uart.h"
#include "virtio_input.h"
#include "virtio_blk.h"
#include "virtio_mouse.h"
#include "virtio_net.h"
#include "panic.h"

#define IRQ_TIMER_PHYS  30
#define IRQ_UART_PL011  33
#define IRQ_VIRTIO_BASE 48
#define IRQ_VIRTIO_END  80

extern uint8_t vectors[];

void exceptions_init(void) {
    __asm__ volatile("msr vbar_el1, %0" :: "r"((uint64_t)(uintptr_t)vectors));
    __asm__ volatile("isb");
}

void irq_enable(void) {
    __asm__ volatile("msr daifclr, #0x2" ::: "memory");
}

void irq_disable(void) {
    __asm__ volatile("msr daifset, #0x2" ::: "memory");
}

void irq_handler(void) {
    uint32_t iar = gic_iar();
    uint32_t irq = iar & 0x3ffu;

    switch (irq) {
    case IRQ_TIMER_PHYS:
        timer_tick();
        break;
    case IRQ_UART_PL011:
        uart_irq();
        break;
    default:
        if (irq >= IRQ_VIRTIO_BASE && irq < IRQ_VIRTIO_END) {
            vinput_irq();
            vmouse_irq();
            vblk_irq();
            vnet_irq();
        }
        break;
    }

    gic_eoi(iar);
}

void panic_unhandled(void) {
    /* Vector dispatch lands here for any exception class we never
     * wired up (FIQ, SError, AArch32 traps, etc). Jump into the
     * graphical panic modal instead of just halting. */
    uint64_t esr, elr;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
    panic_show("unhandled exception (no handler wired)", esr, elr);
}
