#include <stdint.h>
#include "panic.h"
#include "fb.h"
#include "fs.h"
#include "psci.h"
#include "uart.h"
#include "console.h"

#ifdef BOARD_HAS_GIC
#include "virtio_mouse.h"
#endif

/*
 * The kernel's "blue screen": when something explodes we don't fall
 * into a wfi loop -- we paint a modal, keep just enough of the
 * runtime alive to handle a click on Save / Report / Close, and let
 * the user pick how to land. Touching as little as possible: only
 * the framebuffer (direct), the fs (for Save / Report), and PSCI
 * (for Close) -- no scheduler, no syscalls, no window manager.
 */

#define BG_FILL    0x00103a6cu      /* deep blue */
#define MODAL_BG   0x00f4f6fau
#define MODAL_FG   0x00141a26u
#define ACCENT     0x00cc3737u      /* error red */
#define BTN_BG     0x002a3a52u
#define BTN_FG     0x00ffffffu
#define BTN_HI     0x00466992u
#define BTN_DN     0x001f2a3eu
#define DIM        0x00606878u

static volatile int in_panic;

static void hex64(uint64_t v, char out[19]) {
    out[0] = '0'; out[1] = 'x';
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[2 + i] = hex[(v >> ((15 - i) * 4)) & 0xf];
    }
    out[18] = 0;
}

static int strput(char *dst, uint32_t off, uint32_t max, const char *src) {
    while (*src && off + 1 < max) dst[off++] = *src++;
    dst[off] = 0;
    return (int)off;
}

static int hits(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void paint_button(int x, int y, int w, int h,
                         const char *label, int hovered, int pressed) {
    uint32_t bg = pressed ? BTN_DN : (hovered ? BTN_HI : BTN_BG);
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, bg);
    /* 1 px border */
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, 1, MODAL_FG);
    fb_fill_rect((uint32_t)x, (uint32_t)(y + h - 1), (uint32_t)w, 1, MODAL_FG);
    fb_fill_rect((uint32_t)x, (uint32_t)y, 1, (uint32_t)h, MODAL_FG);
    fb_fill_rect((uint32_t)(x + w - 1), (uint32_t)y, 1, (uint32_t)h, MODAL_FG);

    int label_len = 0; while (label[label_len]) label_len++;
    int lw = label_len * 16; /* scale 2 */
    int lx = x + (w - lw) / 2;
    int ly = y + (h - 16) / 2;
    fb_draw_string((uint32_t)lx, (uint32_t)(ly + (pressed ? 1 : 0)),
                   label, BTN_FG, 2);
}

static int  saved_done;
static int  reported_done;

static void mark_status(int mx, int my, int mw, const char *txt) {
    int len = 0; while (txt[len]) len++;
    int x = mx + 24;
    int y = my + 18;
    /* clear a strip so successive messages don't pile on top */
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)(mw - 48), 18, MODAL_BG);
    fb_draw_string((uint32_t)x, (uint32_t)y, txt, ACCENT, 1);
    (void)len;
}

static void do_save(int mx, int my, int mw) {
#ifdef BOARD_HAS_GIC
    int r = fs_save();
    mark_status(mx, my, mw, r == 0
                ? "  saved: filesystem flushed to disk."
                : "  save failed: virtio-blk error.");
    saved_done = 1;
#else
    (void)mx; (void)my; (void)mw;
    saved_done = 1;
#endif
}

static void do_report(int mx, int my, int mw,
                      const char *what, uint64_t esr, uint64_t elr) {
    char log[512];
    uint32_t off = 0;
    off = (uint32_t)strput(log, off, sizeof(log), "panic: ");
    off = (uint32_t)strput(log, off, sizeof(log), what);
    off = (uint32_t)strput(log, off, sizeof(log), "\nESR_EL1=");
    char buf[20];
    hex64(esr, buf);
    off = (uint32_t)strput(log, off, sizeof(log), buf);
    off = (uint32_t)strput(log, off, sizeof(log), "\nELR_EL1=");
    hex64(elr, buf);
    off = (uint32_t)strput(log, off, sizeof(log), buf);
    off = (uint32_t)strput(log, off, sizeof(log), "\n");

    int r = fs_write("crash.log", log, off);
#ifdef BOARD_HAS_GIC
    if (r == 0) (void)fs_save();
#endif
    mark_status(mx, my, mw,
                r == 0 ? "  report saved to /crash.log."
                       : "  report failed: filesystem full.");
    reported_done = 1;
}

__attribute__((noreturn))
static void do_close(void) {
#ifdef BOARD_HAS_GIC
    psci_system_off();
#endif
    for (;;) __asm__ volatile("wfi");
}

__attribute__((noreturn))
void panic_show(const char *what, uint64_t esr, uint64_t elr) {
    /* Re-entry: a fault during the modal would loop. Just halt. */
    if (in_panic) {
        for (;;) __asm__ volatile("wfi");
    }
    in_panic = 1;

    /* Tell anyone watching the serial line. */
    uart_puts("\r\n!! kernel panic: ");
    uart_puts(what);
    uart_puts(" !!\r\n");

    /* Paint the deep-blue background. */
    fb_fill_rect(0, 0, FB_WIDTH, FB_HEIGHT, BG_FILL);

    /* Modal centered on screen. */
    int mw = 760, mh = 380;
    int mx = ((int)FB_WIDTH  - mw) / 2;
    int my = ((int)FB_HEIGHT - mh) / 2;
    fb_fill_rect((uint32_t)mx, (uint32_t)my, (uint32_t)mw, (uint32_t)mh, MODAL_BG);
    fb_fill_rect((uint32_t)mx, (uint32_t)my, (uint32_t)mw, 1, MODAL_FG);
    fb_fill_rect((uint32_t)mx, (uint32_t)(my + mh - 1), (uint32_t)mw, 1, MODAL_FG);
    fb_fill_rect((uint32_t)mx, (uint32_t)my, 1, (uint32_t)mh, MODAL_FG);
    fb_fill_rect((uint32_t)(mx + mw - 1), (uint32_t)my, 1, (uint32_t)mh, MODAL_FG);

    /* Title bar */
    fb_fill_rect((uint32_t)mx, (uint32_t)my, (uint32_t)mw, 40, ACCENT);
    fb_draw_string((uint32_t)(mx + 16), (uint32_t)(my + 12),
                   "Hobby ARM OS  -  Something went wrong", 0xffffffffu, 2);

    /* Body */
    fb_draw_string((uint32_t)(mx + 24), (uint32_t)(my + 70),
                   "The kernel hit an unrecoverable fault and stopped",
                   MODAL_FG, 1);
    fb_draw_string((uint32_t)(mx + 24), (uint32_t)(my + 86),
                   "the program that triggered it. Your other work is",
                   MODAL_FG, 1);
    fb_draw_string((uint32_t)(mx + 24), (uint32_t)(my + 102),
                   "still in memory; choose what to do next.",
                   MODAL_FG, 1);

    fb_draw_string((uint32_t)(mx + 24), (uint32_t)(my + 140),
                   "Reason:", DIM, 1);
    fb_draw_string((uint32_t)(mx + 110), (uint32_t)(my + 140),
                   what, ACCENT, 1);

    char buf[20];
    hex64(esr, buf);
    fb_draw_string((uint32_t)(mx + 24), (uint32_t)(my + 162), "ESR_EL1:", DIM, 1);
    fb_draw_string((uint32_t)(mx + 110), (uint32_t)(my + 162), buf, MODAL_FG, 1);
    hex64(elr, buf);
    fb_draw_string((uint32_t)(mx + 24), (uint32_t)(my + 180), "ELR_EL1:", DIM, 1);
    fb_draw_string((uint32_t)(mx + 110), (uint32_t)(my + 180), buf, MODAL_FG, 1);

    fb_draw_string((uint32_t)(mx + 24), (uint32_t)(my + 230),
                   "What would you like to do?", MODAL_FG, 1);

    /* Three buttons spread across the bottom. */
    int bw = 180, bh = 56;
    int by = my + mh - bh - 32;
    int gap = (mw - 3 * bw) / 4;
    int bx_save   = mx + gap;
    int bx_report = bx_save + bw + gap;
    int bx_close  = bx_report + bw + gap;
    paint_button(bx_save,   by, bw, bh, "Save",   0, 0);
    paint_button(bx_report, by, bw, bh, "Report", 0, 0);
    paint_button(bx_close,  by, bw, bh, "Close",  0, 0);

    fb_present();

#ifdef BOARD_HAS_GIC
    /* Poll the mouse forever until the user picks Close. */
    int prev_left = 0;
    int hover = -1;
    int press = -1;
    for (;;) {
        int32_t cx = 0, cy = 0;
        if (vmouse_present()) vmouse_position(&cx, &cy);
        int btn = vmouse_present() ? vmouse_buttons() : 0;
        int left = btn & 1;

        int new_hover = -1;
        if      (hits(cx, cy, bx_save,   by, bw, bh)) new_hover = 0;
        else if (hits(cx, cy, bx_report, by, bw, bh)) new_hover = 1;
        else if (hits(cx, cy, bx_close,  by, bw, bh)) new_hover = 2;

        int new_press = -1;
        if (left && new_hover >= 0) new_press = new_hover;

        if (new_hover != hover || new_press != press) {
            paint_button(bx_save,   by, bw, bh, "Save",
                         new_hover==0, new_press==0);
            paint_button(bx_report, by, bw, bh, "Report",
                         new_hover==1, new_press==1);
            paint_button(bx_close,  by, bw, bh, "Close",
                         new_hover==2, new_press==2);
            fb_present();
            hover = new_hover;
            press = new_press;
        }

        if (!left && prev_left && hover >= 0) {
            switch (hover) {
            case 0: do_save(mx, my, mw);                          break;
            case 1: do_report(mx, my, mw, what, esr, elr);        break;
            case 2: do_close();
            }
            fb_present();
            press = -1;
        }
        prev_left = left;

        /* Throttle the polling without WFI'ing -- WFI here would
         * deadlock if the timer IRQ source was what tripped us up. */
        for (volatile int i = 0; i < 50000; i++) { }

        /* Cursor: redraw on top so the user can see where they are. */
        fb_draw_cursor((uint32_t)cx, (uint32_t)cy, 0x00ffe060u);
        fb_present();
    }
#else
    /* No GIC means no mouse -- just save and power off. */
    do_save(mx, my, mw);
    do_report(mx, my, mw, what, esr, elr);
    do_close();
#endif
}
