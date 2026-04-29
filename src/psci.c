#include <stdint.h>
#include "psci.h"

#define PSCI_SYSTEM_OFF   0x84000008u
#define PSCI_SYSTEM_RESET 0x84000009u
#define PSCI_CPU_ON_64    0xC4000003ull

static void psci_call(uint32_t func) {
    register uint64_t x0 __asm__("x0") = func;
    __asm__ volatile("hvc #0" : "+r"(x0) :: "memory", "x1", "x2", "x3");
}

static void halt_forever(void) {
    for (;;) {
        __asm__ volatile("wfi");
    }
}

void psci_system_off(void) {
    psci_call(PSCI_SYSTEM_OFF);
    halt_forever();
}

void psci_system_reset(void) {
    psci_call(PSCI_SYSTEM_RESET);
    halt_forever();
}

int psci_cpu_on(uint64_t target_cpu, uint64_t entry, uint64_t ctx) {
    register uint64_t x0 __asm__("x0") = PSCI_CPU_ON_64;
    register uint64_t x1 __asm__("x1") = target_cpu;
    register uint64_t x2 __asm__("x2") = entry;
    register uint64_t x3 __asm__("x3") = ctx;
    __asm__ volatile("hvc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3)
        : "memory");
    return (int)x0;
}
