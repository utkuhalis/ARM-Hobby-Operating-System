#ifndef HOBBY_OS_GIC_H
#define HOBBY_OS_GIC_H

#include <stdint.h>

void     gic_init(void);
void     gic_enable_irq(uint32_t irq);
uint32_t gic_iar(void);
void     gic_eoi(uint32_t iar);

#endif
