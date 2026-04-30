#include <stdint.h>
#include "syscall.h"
#include "console.h"
#include "task.h"
#include "panic.h"

static uint64_t do_write(const char *s) {
    if (!s) return (uint64_t)-1;
    console_puts(s);
    return 0;
}

void sync_handler(struct trapframe *tf) {
    uint64_t esr;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    uint32_t ec = (uint32_t)(esr >> 26);

    if (ec != 0x15) {
        const char *what;
        switch (ec) {
        case 0x20: case 0x21: what = "instruction abort"; break;
        case 0x24: case 0x25: what = "data abort";        break;
        case 0x26:            what = "stack alignment fault"; break;
        case 0x2c:            what = "FP/SIMD trap";      break;
        case 0x3c:            what = "BRK debug breakpoint"; break;
        default:              what = "synchronous exception"; break;
        }
        panic_show(what, esr, tf->elr_el1);
    }

    uint64_t num = tf->x[8];
    switch (num) {
    case SYS_WRITE:
        tf->x[0] = do_write((const char *)tf->x[0]);
        break;
    case SYS_EXIT:
        task_exit();
        /* unreachable */
        break;
    case SYS_GETPID:
        tf->x[0] = (uint64_t)task_current()->id;
        break;
    case SYS_YIELD:
        task_yield();
        tf->x[0] = 0;
        break;
    default:
        tf->x[0] = (uint64_t)-1;
        break;
    }
}
