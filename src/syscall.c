#include <stdint.h>
#include "syscall.h"
#include "console.h"
#include "task.h"
#include "panic.h"
#include "fb.h"
#include "virtio_input.h"
#include "virtio_mouse.h"
#include "timer.h"

/* Set while a user-mode app has the framebuffer (SYS_GUI_TAKE).
 * status_thread checks this and skips compose so the desktop chrome
 * doesn't paint over the running game. */
volatile int gui_taken;
int gui_is_taken(void) { return gui_taken; }

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

    case SYS_GUI_TAKE:
        gui_taken = 1;
        fb_clear(0);
        tf->x[0] = 0;
        break;
    case SYS_GUI_RELEASE:
        gui_taken = 0;
        tf->x[0] = 0;
        break;
    case SYS_GUI_FILL_RECT:
        fb_fill_rect((uint32_t)tf->x[0], (uint32_t)tf->x[1],
                     (uint32_t)tf->x[2], (uint32_t)tf->x[3],
                     (uint32_t)tf->x[4]);
        tf->x[0] = 0;
        break;
    case SYS_GUI_DRAW_TEXT:
        fb_draw_string((uint32_t)tf->x[0], (uint32_t)tf->x[1],
                       (const char *)(uintptr_t)tf->x[2],
                       (uint32_t)tf->x[3], (uint32_t)tf->x[4]);
        tf->x[0] = 0;
        break;
    case SYS_GUI_PRESENT:
        fb_present();
        tf->x[0] = 0;
        break;
    case SYS_GUI_POLL: {
        struct gui_event *ev = (struct gui_event *)(uintptr_t)tf->x[0];
        if (ev) {
            int32_t mx = 0, my = 0;
            if (vmouse_present()) vmouse_position(&mx, &my);
            ev->mouse_x = mx;
            ev->mouse_y = my;
            ev->buttons = vmouse_present() ? vmouse_buttons() : 0;
            char c;
            ev->key = vinput_read_char(&c) ? (int32_t)(unsigned char)c : 0;
        }
        tf->x[0] = 0;
        break;
    }
    case SYS_GUI_SLEEP_MS: {
        uint64_t target = timer_ticks() +
                          ((uint64_t)tf->x[0] * timer_hz()) / 1000;
        while (timer_ticks() < target) {
            task_yield();
            __asm__ volatile("wfi");
        }
        tf->x[0] = 0;
        break;
    }
    case SYS_GUI_FB_INFO: {
        struct gui_info *gi = (struct gui_info *)(uintptr_t)tf->x[0];
        if (gi) { gi->width = FB_WIDTH; gi->height = FB_HEIGHT; }
        tf->x[0] = 0;
        break;
    }
    default:
        tf->x[0] = (uint64_t)-1;
        break;
    }
}
