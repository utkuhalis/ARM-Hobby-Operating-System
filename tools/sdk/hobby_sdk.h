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

#define SYS_WRITE        0
#define SYS_EXIT         1
#define SYS_GETPID       2
#define SYS_YIELD        3
#define SYS_GUI_TAKE     4
#define SYS_GUI_RELEASE  5
#define SYS_GUI_FILL_RECT 6
#define SYS_GUI_DRAW_TEXT 7
#define SYS_GUI_PRESENT  8
#define SYS_GUI_POLL     9
#define SYS_GUI_SLEEP_MS 10
#define SYS_GUI_FB_INFO  11

struct hobby_event {
    int mouse_x;
    int mouse_y;
    int buttons;
    int key;
};

struct hobby_fb_info {
    unsigned width;
    unsigned height;
};

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

static inline long __syscall5(long n, long a, long b, long c, long d, long e) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
                     : "memory");
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

/* GUI wrappers: take the screen, paint, poll input, release. */
static inline void hobby_gui_take(void)        { __syscall0(SYS_GUI_TAKE); }
static inline void hobby_gui_release(void)     { __syscall0(SYS_GUI_RELEASE); }
static inline void hobby_gui_present(void)     { __syscall0(SYS_GUI_PRESENT); }
static inline void hobby_gui_sleep_ms(int ms)  { __syscall1(SYS_GUI_SLEEP_MS, ms); }
static inline void hobby_gui_fill_rect(int x, int y, int w, int h, unsigned color) {
    __syscall5(SYS_GUI_FILL_RECT, x, y, w, h, (long)color);
}
static inline void hobby_gui_draw_text(int x, int y, const char *s,
                                       unsigned color, int scale) {
    __syscall5(SYS_GUI_DRAW_TEXT, x, y, (long)(uintptr_t)s,
               (long)color, scale);
}
static inline void hobby_gui_poll(struct hobby_event *ev) {
    __syscall1(SYS_GUI_POLL, (long)(uintptr_t)ev);
}
static inline void hobby_gui_fb_info(struct hobby_fb_info *info) {
    __syscall1(SYS_GUI_FB_INFO, (long)(uintptr_t)info);
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
