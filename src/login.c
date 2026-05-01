#include <stdint.h>
#include "login.h"
#include "fb.h"
#include "accounts.h"
#include "virtio_input.h"
#include "virtio_mouse.h"
#include "timer.h"
#include "uart.h"

#define BG       0x0008243au       /* deep blue gradient base */
#define CARD_BG  0x00f4f6fau
#define CARD_FG  0x00141a26u
#define DIM      0x00606878u
#define ACCENT   0x002c6cb8u
#define ACCENT_HI 0x004988dau
#define ERROR_FG 0x00cc3737u
#define FIELD_BG 0x00ffffffu
#define FIELD_FG 0x00141a26u
#define FIELD_FOCUS 0x00d6e6ffu

#define USER_MAX 16
#define PASS_MAX 32

static int hits(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void draw_field(int x, int y, int w, int h,
                       const char *label, const char *text,
                       int focused, int is_password) {
    fb_draw_string((uint32_t)x, (uint32_t)(y - 18), label, DIM, 1);
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,
                 focused ? FIELD_FOCUS : FIELD_BG);
    /* border, double thickness when focused */
    uint32_t bc = focused ? ACCENT : DIM;
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, 1, bc);
    fb_fill_rect((uint32_t)x, (uint32_t)(y + h - 1), (uint32_t)w, 1, bc);
    fb_fill_rect((uint32_t)x, (uint32_t)y, 1, (uint32_t)h, bc);
    fb_fill_rect((uint32_t)(x + w - 1), (uint32_t)y, 1, (uint32_t)h, bc);
    if (focused) {
        fb_fill_rect((uint32_t)x, (uint32_t)(y + h), (uint32_t)w, 1, bc);
    }

    /* inner text */
    char rendered[64];
    int n = 0;
    while (text[n] && n < (int)sizeof(rendered) - 1) {
        rendered[n] = is_password ? '*' : text[n];
        n++;
    }
    rendered[n] = 0;
    fb_draw_string((uint32_t)(x + 10), (uint32_t)(y + (h - 16) / 2),
                   rendered, FIELD_FG, 2);

    /* blinking caret when focused */
    if (focused) {
        uint64_t t = timer_ticks();
        if ((t / (timer_hz() / 2)) & 1) {
            int caret_x = x + 10 + n * 16;
            fb_fill_rect((uint32_t)caret_x, (uint32_t)(y + (h - 18) / 2),
                         2, 18, FIELD_FG);
        }
    }
}

static void draw_button(int x, int y, int w, int h,
                        const char *label, int hovered, int pressed) {
    uint32_t bg = pressed ? 0x00164f99u
                : hovered ? ACCENT_HI : ACCENT;
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, bg);
    int label_len = 0; while (label[label_len]) label_len++;
    int lw = label_len * 16;
    int lx = x + (w - lw) / 2;
    int ly = y + (h - 16) / 2;
    fb_draw_string((uint32_t)lx, (uint32_t)(ly + (pressed ? 1 : 0)),
                   label, 0xffffffffu, 2);
}

static void paint_screen(const char *user, int ulen,
                         const char *pass, int plen,
                         int focus, const char *err,
                         int btn_hover, int btn_press) {
    fb_clear(BG);

    /* a soft band across the top so it isn't pure flat */
    fb_fill_rect(0, 0, FB_WIDTH, FB_HEIGHT / 6, 0x000c2e4au);

    /* card centered */
    int cw = 540, ch = 360;
    int cx = ((int)FB_WIDTH  - cw) / 2;
    int cy = ((int)FB_HEIGHT - ch) / 2;
    fb_fill_rect((uint32_t)cx, (uint32_t)cy, (uint32_t)cw, (uint32_t)ch,
                 CARD_BG);
    fb_fill_rect((uint32_t)cx, (uint32_t)cy, (uint32_t)cw, 4, ACCENT);

    /* header */
    fb_draw_string((uint32_t)(cx + 24), (uint32_t)(cy + 26),
                   "Welcome to Hobby ARM OS", CARD_FG, 2);
    fb_draw_string((uint32_t)(cx + 24), (uint32_t)(cy + 56),
                   "Sign in to continue", DIM, 1);

    /* fields */
    draw_field(cx + 40, cy + 110, cw - 80, 36,
               "Username", user, focus == 0, 0);
    draw_field(cx + 40, cy + 190, cw - 80, 36,
               "Password", pass, focus == 1, 1);

    /* button */
    int bw = 220, bh = 44;
    int bx = cx + (cw - bw) / 2;
    int by = cy + ch - bh - 32;
    draw_button(bx, by, bw, bh, "Sign in", btn_hover, btn_press);

    if (err) {
        fb_draw_string((uint32_t)(cx + 40), (uint32_t)(by - 28),
                       err, ERROR_FG, 1);
    }

    /* tiny hint */
    fb_draw_string((uint32_t)(cx + 40), (uint32_t)(cy + ch - 16),
                   "Default: root / root  -  Tab to switch fields, Enter to sign in",
                   DIM, 1);

    /* used by login_run for hit-tests */
    extern int  login_user_x, login_user_y, login_user_w, login_user_h;
    extern int  login_pass_x, login_pass_y, login_pass_w, login_pass_h;
    extern int  login_btn_x,  login_btn_y,  login_btn_w,  login_btn_h;
    login_user_x = cx + 40; login_user_y = cy + 110;
    login_user_w = cw - 80; login_user_h = 36;
    login_pass_x = cx + 40; login_pass_y = cy + 190;
    login_pass_w = cw - 80; login_pass_h = 36;
    login_btn_x  = bx; login_btn_y = by;
    login_btn_w  = bw; login_btn_h = bh;

    (void)ulen; (void)plen;
}

int login_user_x, login_user_y, login_user_w, login_user_h;
int login_pass_x, login_pass_y, login_pass_w, login_pass_h;
int login_btn_x,  login_btn_y,  login_btn_w,  login_btn_h;

void login_run(void) {
    char user[USER_MAX + 1] = {0};
    char pass[PASS_MAX + 1] = {0};
    int  ulen = 0, plen = 0;
    int  focus = 0;
    const char *err = 0;
    int  prev_left = 0;
    int  btn_press = 0;

    /* initial render */
    paint_screen(user, ulen, pass, plen, focus, err, 0, 0);
    fb_present();

    uint64_t last_repaint = timer_ticks();

    for (;;) {
        char c = 0;
        int dirty = 0;
        /* keyboard: virtio first, then UART (so headless serial works). */
        while (1) {
            if (!vinput_read_char(&c)) {
                if (!uart_has_input()) break;
                c = uart_getc();
            }
            if (c == '\t') {
                focus = !focus;
                dirty = 1;
            } else if (c == '\n' || c == '\r') {
                if (focus == 0) {
                    focus = 1;
                    dirty = 1;
                } else {
                    int r = account_login(user, pass);
                    if (r == 0) return;
                    err = (r == -1) ? "no such account"
                                    : "wrong password";
                    /* clear password on failure */
                    plen = 0; pass[0] = 0;
                    dirty = 1;
                }
            } else if (c == '\b' || c == 0x7f) {
                if (focus == 0 && ulen > 0)      { user[--ulen] = 0; }
                else if (focus == 1 && plen > 0) { pass[--plen] = 0; }
                dirty = 1;
            } else if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7f) {
                if (focus == 0 && ulen < USER_MAX) {
                    user[ulen++] = c; user[ulen] = 0;
                } else if (focus == 1 && plen < PASS_MAX) {
                    pass[plen++] = c; pass[plen] = 0;
                }
                dirty = 1;
            }
        }

        /* mouse */
        int32_t mx = 0, my = 0;
        int btn = 0;
        if (vmouse_present()) {
            vmouse_position(&mx, &my);
            btn = vmouse_buttons();
        }
        int left = btn & 1;
        int press = left && !prev_left;
        int release = !left && prev_left;

        int btn_hover = hits((int)mx, (int)my,
                             login_btn_x, login_btn_y,
                             login_btn_w, login_btn_h);

        if (press) {
            if (hits((int)mx, (int)my,
                     login_user_x, login_user_y,
                     login_user_w, login_user_h)) {
                focus = 0; dirty = 1;
            } else if (hits((int)mx, (int)my,
                            login_pass_x, login_pass_y,
                            login_pass_w, login_pass_h)) {
                focus = 1; dirty = 1;
            } else if (btn_hover) {
                btn_press = 1; dirty = 1;
            }
        }
        if (release) {
            if (btn_press && btn_hover) {
                int r = account_login(user, pass);
                if (r == 0) return;
                err = (r == -1) ? "no such account" : "wrong password";
                plen = 0; pass[0] = 0;
            }
            btn_press = 0;
            dirty = 1;
        }
        prev_left = left;

        /* repaint at ~10 fps even without input so the caret blinks */
        uint64_t now = timer_ticks();
        if (dirty || (now - last_repaint) > (timer_hz() / 4)) {
            paint_screen(user, ulen, pass, plen, focus, err,
                         btn_hover, btn_press);
            /* draw cursor on top */
            fb_draw_cursor((uint32_t)mx, (uint32_t)my, 0x00ffe060u);
            fb_present();
            last_repaint = now;
        }

        __asm__ volatile("wfi");
    }
}
