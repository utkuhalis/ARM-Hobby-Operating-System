#ifndef HOBBY_OS_SYSINFO_H
#define HOBBY_OS_SYSINFO_H

#include <stdint.h>

uint64_t sys_read_midr(void);
uint64_t sys_read_mpidr(void);
uint32_t sys_read_currentel(void);
uint64_t sys_timer_freq(void);
uint64_t sys_timer_count(void);
uint64_t sys_uptime_seconds(void);

const char *sys_cpu_name(uint64_t midr);
const char *sys_board_name(void);

#endif
