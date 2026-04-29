#include <stdint.h>
#include "psci.h"

#define PSCI_SYSTEM_OFF   0x84000008u
#define PSCI_SYSTEM_RESET 0x84000009u

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
