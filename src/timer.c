#include <stdint.h>
#include "timer.h"
#include "gic.h"

#define IRQ_TIMER_PHYS 30

static volatile uint64_t tick_count;
static uint64_t  reload_cycles;
static uint32_t  hz;

static uint64_t cntfrq(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static void set_tval(uint64_t v) {
    __asm__ volatile("msr cntp_tval_el0, %0" :: "r"(v));
}

static void set_ctl(uint64_t v) {
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"(v));
    __asm__ volatile("isb");
}

void timer_init(uint32_t want_hz) {
    hz = want_hz;
    uint64_t f = cntfrq();
    reload_cycles = f / want_hz;

    set_tval(reload_cycles);
    set_ctl(1);

    gic_enable_irq(IRQ_TIMER_PHYS);
}

void timer_tick(void) {
    tick_count++;
    set_tval(reload_cycles);
}

uint64_t timer_ticks(void) {
    return tick_count;
}

uint32_t timer_hz(void) {
    return hz;
}
