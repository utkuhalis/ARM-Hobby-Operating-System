#ifndef HOBBY_OS_PANIC_H
#define HOBBY_OS_PANIC_H

#include <stdint.h>

/*
 * Replaces the old panic-and-halt path. When the kernel hits an
 * unrecoverable trap, panic_show() paints a modal over the desktop
 * with a short reason + register dump and three buttons:
 *
 *   Save    -> flush the fs to virtio-blk so user data survives
 *   Report  -> write a /crash.log entry capturing the failure
 *   Close   -> power the machine off cleanly (PSCI SYSTEM_OFF)
 *
 * The function never returns -- closing always powers off.
 */
__attribute__((noreturn))
void panic_show(const char *what, uint64_t esr, uint64_t elr);

#endif
