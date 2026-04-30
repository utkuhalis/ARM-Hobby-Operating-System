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

static void format_decimal(char *out, long v) {
    char tmp[24];
    int n = 0;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = '0' + (char)(v % 10); v /= 10; }
    int o = 0;
    if (neg) out[o++] = '-';
    while (n > 0) out[o++] = tmp[--n];
    out[o] = '\0';
}

static void say_pid(const char *prefix) {
    long pid = syscall0(SYS_GETPID);
    char num[24];
    format_decimal(num, pid);
    sys_write(prefix);
    sys_write(num);
    sys_write("\n");
}

void user_main_hello(void) {
    sys_write("[hello] Hello, World!\n");
    say_pid("[hello] my pid is ");
    sys_write("[hello] exiting\n");
    sys_exit();
}

void user_main_counter(void) {
    sys_write("[counter] starting\n");
    say_pid("[counter] my pid is ");
    for (int i = 1; i <= 5; i++) {
        char num[24];
        format_decimal(num, i);
        sys_write("[counter] ");
        sys_write(num);
        sys_write("\n");
        for (int y = 0; y < 60; y++) syscall0(SYS_YIELD);
    }
    sys_write("[counter] done\n");
    sys_exit();
}

void user_main_clock(void) {
    sys_write("[clock] tick-tick-tick\n");
    say_pid("[clock] my pid is ");
    for (int i = 0; i < 3; i++) {
        sys_write("[clock] tock\n");
        for (int y = 0; y < 100; y++) syscall0(SYS_YIELD);
    }
    sys_write("[clock] done\n");
    sys_exit();
}

void user_main_load(void) {
    sys_write("[load] burning some ticks\n");
    long sum = 0;
    for (int i = 0; i < 200000; i++) {
        sum += i;
        if ((i & 0x3fff) == 0) syscall0(SYS_YIELD);
    }
    char num[24];
    format_decimal(num, sum);
    sys_write("[load] sum = ");
    sys_write(num);
    sys_write("\n[load] done\n");
    sys_exit();
}

void user_main_notepad(void) {
    sys_write("[notepad] tiny editor\n");
    sys_write("[notepad] usage from the shell:\n");
    sys_write("[notepad]   write notes hello there\n");
    sys_write("[notepad]   cat notes\n");
    sys_write("[notepad]   rm notes\n");
    sys_write("[notepad] (a real interactive notepad needs a typed\n");
    sys_write("[notepad]  text-input widget; that lands next.)\n");
    sys_exit();
}

void user_main_files(void) {
    sys_write("[files] use 'ls' for a listing, 'cat <file>' to read\n");
    sys_write("[files] this app is a stub until the file-browser\n");
    sys_write("[files] window lands.\n");
    sys_exit();
}

void user_main_sysinfo(void) {
    sys_write("[sysinfo] use the shell commands:\n");
    sys_write("[sysinfo]   uname -a\n");
    sys_write("[sysinfo]   cpuinfo\n");
    sys_write("[sysinfo]   meminfo\n");
    sys_write("[sysinfo]   uptime\n");
    sys_exit();
}
