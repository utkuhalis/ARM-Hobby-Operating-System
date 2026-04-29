/*
 * Tiny built-in user-mode program. Linked into the kernel image so
 * we don't yet need a real ELF loader; the only thing that makes
 * this "user code" is that it runs at EL0 and can only talk to the
 * outside world through SVC.
 */

#include <stdint.h>
#include "user_program.h"

static inline long syscall0(long n) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long syscall1(long n, long a) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

#define SYS_WRITE  0
#define SYS_EXIT   1
#define SYS_GETPID 2
#define SYS_YIELD  3

static void sys_write(const char *s) {
    syscall1(SYS_WRITE, (long)(uintptr_t)s);
}

__attribute__((noreturn))
static void sys_exit(void) {
    syscall0(SYS_EXIT);
    for (;;) {}
}

void user_main_hello(void) {
    sys_write("[user task] Hello, World!\n");
    long pid = syscall0(SYS_GETPID);
    char buf[16];
    int n = 0;
    if (pid == 0) {
        buf[n++] = '0';
    } else {
        char tmp[16];
        int t = 0;
        while (pid > 0) {
            tmp[t++] = '0' + (char)(pid % 10);
            pid /= 10;
        }
        while (t > 0) buf[n++] = tmp[--t];
    }
    buf[n++] = '\n';
    buf[n] = 0;
    sys_write("[user task] my pid is ");
    sys_write(buf);
    sys_write("[user task] going back to the kernel\n");
    sys_exit();
}
