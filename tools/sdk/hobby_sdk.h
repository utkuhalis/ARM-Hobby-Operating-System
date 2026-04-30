/*
 * Hobby ARM OS user-space SDK header.
 *
 * Programs include this and link against tools/sdk/crt0.S +
 * tools/sdk/user.ld to produce a static ELF that the kernel can load
 * with elf_load() and run at EL0.
 *
 * The ABI is just SVC #0 with the syscall number in x8 and arguments
 * in x0..x5 (return value in x0). Numbers must match src/syscall.c.
 */

#ifndef HOBBY_SDK_H
#define HOBBY_SDK_H

typedef unsigned long  uintptr_t;
typedef unsigned int   uint32_t;
typedef unsigned char  uint8_t;
typedef long           ssize_t;

#define SYS_WRITE  0
#define SYS_EXIT   1
#define SYS_GETPID 2
#define SYS_YIELD  3

static inline long __syscall0(long n) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long __syscall1(long n, long a) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline void  hobby_write(const char *s)  { __syscall1(SYS_WRITE, (long)(uintptr_t)s); }
static inline void  hobby_yield(void)           { __syscall0(SYS_YIELD); }
static inline long  hobby_getpid(void)          { return __syscall0(SYS_GETPID); }
__attribute__((noreturn))
static inline void  hobby_exit(int code) {
    (void)code;
    __syscall0(SYS_EXIT);
    for (;;) {}
}

/* tiny string helpers so packages don't have to reimplement them */
static inline void hobby_itoa(char *out, long v) {
    char tmp[24];
    int n = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = '0' + (char)(v % 10); v /= 10; }
    int o = 0;
    if (neg) out[o++] = '-';
    while (n > 0) out[o++] = tmp[--n];
    out[o] = '\0';
}

/* Programs export 'int hobby_main(void)'. crt0 calls it and exits. */
int hobby_main(void);

#endif
