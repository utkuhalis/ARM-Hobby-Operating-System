#ifndef HOBBY_OS_PSCI_H
#define HOBBY_OS_PSCI_H

#include <stdint.h>

void psci_system_off(void);
void psci_system_reset(void);

int  psci_cpu_on(uint64_t target_cpu, uint64_t entry, uint64_t ctx);

#endif
