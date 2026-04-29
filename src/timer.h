#ifndef HOBBY_OS_TIMER_H
#define HOBBY_OS_TIMER_H

#include <stdint.h>

void     timer_init(uint32_t hz);
void     timer_tick(void);

uint64_t timer_ticks(void);
uint32_t timer_hz(void);

#endif
