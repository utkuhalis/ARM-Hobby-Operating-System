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

/* Owning task pointer + click-area for the kernel-painted close
 * button. The compositor and the input dispatcher consult these when
 * a fullscreen GUI app is active. */
static task_t *gui_owner_task;
task_t *gui_owner(void)             { return gui_owner_task; }
void    gui_clear(void)             { gui_taken = 0; gui_owner_task = 0; }

/* Geometry of the kernel-overlaid close button, rendered each
 * SYS_GUI_PRESENT after the user app has finished its frame. */
#define GUI_CLOSE_W 110
#define GUI_CLOSE_H 36
int  gui_close_x(void)              { return FB_WIDTH - GUI_CLOSE_W - 16; }
int  gui_close_y(void)              { return 16; }
int  gui_close_w(void)              { return GUI_CLOSE_W; }
int  gui_close_h(void)              { return GUI_CLOSE_H; }

static void paint_close_button(void) {
    int x = gui_close_x();
    int y = gui_close_y();
    int w = GUI_CLOSE_W, h = GUI_CLOSE_H;
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, 0x00cc3737u);
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, 1, 0x00141a26u);
    fb_fill_rect((uint32_t)x, (uint32_t)(y + h - 1), (uint32_t)w, 1, 0x00141a26u);
    fb_fill_rect((uint32_t)x, (uint32_t)y, 1, (uint32_t)h, 0x00141a26u);
    fb_fill_rect((uint32_t)(x + w - 1), (uint32_t)y, 1, (uint32_t)h, 0x00141a26u);
    /* "× Close" centered in smooth font */
    const char *label = "x  Close";
    int lw = (int)fb_text_ui_width(label);
    int lh_text = (int)fb_text_ui_line_height();
    fb_draw_string_ui((uint32_t)(x + (w - lw) / 2),
                      (uint32_t)(y + (h - lh_text) / 2),
                      label, 0x00ffffffu);
}

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
        /* If a fullscreen GUI app is exiting on its own, hand the
         * framebuffer back to the desktop so the next compose paints
         * the wallpaper instead of leaving the user's last frame
         * frozen on screen. */
        if (gui_taken && gui_owner_task == task_current()) {
            gui_taken = 0;
            gui_owner_task = 0;
        }
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
        gui_owner_task = task_current();
        fb_clear(0);
        tf->x[0] = 0;
        break;
    case SYS_GUI_RELEASE:
        gui_taken = 0;
        gui_owner_task = 0;
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
        /* Kernel overlays the Close button on top of whatever the
         * user app just finished painting, then flips. */
        if (gui_taken) paint_close_button();
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
