#include "sysinfo.h"

#ifndef BOARD_NAME
#define BOARD_NAME "unknown"
#endif

uint64_t sys_read_midr(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, midr_el1" : "=r"(v));
    return v;
}

uint64_t sys_read_mpidr(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(v));
    return v;
}

uint32_t sys_read_currentel(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(v));
    return (uint32_t)((v >> 2) & 0x3);
}

uint64_t sys_timer_freq(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

uint64_t sys_timer_count(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
    return v;
}

uint64_t sys_uptime_seconds(void) {
    uint64_t freq = sys_timer_freq();
    if (freq == 0) return 0;
    return sys_timer_count() / freq;
}

const char *sys_cpu_name(uint64_t midr) {
    uint32_t implementer = (uint32_t)((midr >> 24) & 0xff);
    uint32_t partnum     = (uint32_t)((midr >>  4) & 0xfff);

    if (implementer == 0x41) {
        switch (partnum) {
        case 0xD03: return "ARM Cortex-A53";
        case 0xD05: return "ARM Cortex-A55";
        case 0xD07: return "ARM Cortex-A57";
        case 0xD08: return "ARM Cortex-A72";
        case 0xD09: return "ARM Cortex-A73";
        case 0xD0A: return "ARM Cortex-A75";
        case 0xD0B: return "ARM Cortex-A76";
        case 0xD0D: return "ARM Cortex-A77";
        case 0xD41: return "ARM Cortex-A78";
        case 0xD46: return "ARM Cortex-A510";
        case 0xD47: return "ARM Cortex-A710";
        default:    return "ARM (unknown part)";
        }
    }
    if (implementer == 0x51) return "Qualcomm";
    if (implementer == 0x42) return "Broadcom";
    if (implementer == 0x43) return "Cavium";
    if (implementer == 0x4E) return "NVIDIA";
    if (implementer == 0x61) return "Apple";
    return "unknown vendor";
}

const char *sys_board_name(void) {
    return BOARD_NAME;
}
