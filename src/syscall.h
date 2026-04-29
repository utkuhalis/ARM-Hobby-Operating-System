#ifndef HOBBY_OS_SYSCALL_H
#define HOBBY_OS_SYSCALL_H

#include <stdint.h>

#define SYS_WRITE  0
#define SYS_EXIT   1
#define SYS_GETPID 2
#define SYS_YIELD  3

struct trapframe {
    uint64_t x[31];   /* x0..x30 */
    uint64_t sp_el0;
    uint64_t elr_el1;
    uint64_t spsr_el1;
};

void sync_handler(struct trapframe *tf);

#endif
