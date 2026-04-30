#include <stdint.h>
#include "window.h"
#include "fb.h"
#include "fb_console.h"
#include "str.h"

#ifdef BOARD_HAS_GIC
#include "virtio_mouse.h"
#endif

#define MAX_WINDOWS 8

static window_t windows[MAX_WINDOWS];
static int      window_n;
static int      focus_idx = -1;

/* Pointer state for click-to-focus + drag */
static int  prev_buttons;
static int  drag_active;
static int  drag_window;
static int  drag_off_x;
static int  drag_off_y;

#define DESKTOP_BG  0x000a0e18u
#define WIN_BG      0x00121826u
#define WIN_FG      0x00d0d6e0u
#define WIN_ACCENT  0x002a3e60u
#define WIN_ACCENT_FOCUS 0x004a78bcu
#define WIN_BORDER_C  0x00303a4eu
#define CURSOR_COLOR  0x00ffe060u

static void copy_title(char *dst, const char *src) {
    int i = 0;
    while (i < WIN_TITLE_MAX - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void window_init(void) {
    window_n = 0;
    focus_idx = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].visible = 0;
    }
}

window_t *window_create(const char *title, int x, int y) {
    if (window_n >= MAX_WINDOWS) return NULL;
    window_t *w = &windows[window_n];
    w->id      = window_n;
    w->x       = x;
    w->y       = y;
    w->w       = WIN_COLS * WIN_CHAR_W + 2 * WIN_BORDER;
    w->h       = WIN_TITLE_H + WIN_ROWS * WIN_CHAR_H + 2 * WIN_BORDER;
    copy_title(w->title, title);
    w->visible = 1;
    w->focused = (window_n == 0);
    w->bg      = WIN_BG;
    w->fg      = WIN_FG;
    w->accent  = WIN_ACCENT;
    w->cur_row = 0;
    w->cur_col = 0;
    for (int r = 0; r < WIN_ROWS; r++)
        for (int c = 0; c < WIN_COLS; c++)
            w->text[r][c] = ' ';
    if (focus_idx < 0) focus_idx = window_n;
    window_n++;
    return w;
}

void window_clear(window_t *w) {
    w->cur_row = 0;
    w->cur_col = 0;
    for (int r = 0; r < WIN_ROWS; r++)
        for (int c = 0; c < WIN_COLS; c++)
            w->text[r][c] = ' ';
}

static void scroll_up(window_t *w) {
    for (int r = 0; r < WIN_ROWS - 1; r++) {
        for (int c = 0; c < WIN_COLS; c++) {
            w->text[r][c] = w->text[r + 1][c];
        }
    }
    for (int c = 0; c < WIN_COLS; c++) {
        w->text[WIN_ROWS - 1][c] = ' ';
    }
}

void window_putc(window_t *w, char c) {
    if (c == '\n') {
        w->cur_col = 0;
        w->cur_row++;
        if (w->cur_row >= WIN_ROWS) {
            scroll_up(w);
            w->cur_row = WIN_ROWS - 1;
        }
        return;
    }
    if (c == '\r') {
        w->cur_col = 0;
        return;
    }
    if (c == '\b') {
        if (w->cur_col > 0) {
            w->cur_col--;
            w->text[w->cur_row][w->cur_col] = ' ';
        }
        return;
    }
    if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7f) return;

    w->text[w->cur_row][w->cur_col] = c;
    w->cur_col++;
    if (w->cur_col >= WIN_COLS) {
        w->cur_col = 0;
        w->cur_row++;
        if (w->cur_row >= WIN_ROWS) {
            scroll_up(w);
            w->cur_row = WIN_ROWS - 1;
        }
    }
}

void window_puts(window_t *w, const char *s) {
    while (*s) window_putc(w, *s++);
}

void window_set_focus(window_t *w) {
    if (!w) return;
    for (int i = 0; i < window_n; i++) {
        windows[i].focused = (&windows[i] == w);
    }
    focus_idx = w->id;
}

window_t *window_focused(void) {
    if (focus_idx < 0 || focus_idx >= window_n) return NULL;
    return &windows[focus_idx];
}

int window_count(void) { return window_n; }

window_t *window_at(int idx) {
    if (idx < 0 || idx >= window_n) return NULL;
    return &windows[idx];
}

static int point_in_window(window_t *w, int x, int y) {
    return x >= w->x && x < w->x + w->w &&
           y >= w->y && y < w->y + w->h;
}

static int point_in_titlebar(window_t *w, int x, int y) {
    return x >= w->x && x < w->x + w->w &&
           y >= w->y && y < w->y + WIN_TITLE_H;
}

void window_handle_pointer(int32_t mx, int32_t my, int buttons) {
    int left_now  = (buttons & 1) != 0;
    int left_prev = (prev_buttons & 1) != 0;
    int press     = left_now && !left_prev;
    int release   = !left_now && left_prev;

    if (press) {
        /* topmost window under the cursor wins (focused first, then later) */
        int hit = -1;
        if (focus_idx >= 0 && focus_idx < window_n &&
            point_in_window(&windows[focus_idx], (int)mx, (int)my)) {
            hit = focus_idx;
        } else {
            for (int i = window_n - 1; i >= 0; i--) {
                if (point_in_window(&windows[i], (int)mx, (int)my)) {
                    hit = i;
                    break;
                }
            }
        }
        if (hit >= 0) {
            window_set_focus(&windows[hit]);
            if (point_in_titlebar(&windows[hit], (int)mx, (int)my)) {
                drag_active = 1;
                drag_window = hit;
                drag_off_x  = (int)mx - windows[hit].x;
                drag_off_y  = (int)my - windows[hit].y;
            }
        }
    }

    if (drag_active && left_now &&
        drag_window >= 0 && drag_window < window_n) {
        int new_x = (int)mx - drag_off_x;
        int new_y = (int)my - drag_off_y;
        /* clamp inside the framebuffer */
        if (new_x < 0) new_x = 0;
        if (new_y < 0) new_y = 0;
        windows[drag_window].x = new_x;
        windows[drag_window].y = new_y;
    }

    if (release) {
        drag_active = 0;
        drag_window = -1;
    }

    prev_buttons = buttons;
}

static void paint_window(window_t *w) {
    if (!w->visible) return;

    /* Border + frame */
    fb_fill_rect(w->x, w->y, w->w, w->h, WIN_BORDER_C);
    /* Title bar */
    fb_fill_rect(w->x + WIN_BORDER, w->y + WIN_BORDER,
                 w->w - 2 * WIN_BORDER, WIN_TITLE_H - WIN_BORDER,
                 w->focused ? WIN_ACCENT_FOCUS : w->accent);
    /* Title text */
    int tx = w->x + 6;
    int ty = w->y + (WIN_TITLE_H - 16) / 2 + 1;
    for (int i = 0; w->title[i] && i < WIN_TITLE_MAX; i++) {
        fb_draw_glyph16((uint32_t)(tx + i * WIN_CHAR_W),
                        (uint32_t)ty, w->title[i], WIN_FG);
    }
    /* Content background */
    int cx = w->x + WIN_BORDER;
    int cy = w->y + WIN_TITLE_H;
    int cw = w->w - 2 * WIN_BORDER;
    int ch = w->h - WIN_TITLE_H - WIN_BORDER;
    fb_fill_rect(cx, cy, cw, ch, w->bg);
    /* Content text */
    for (int r = 0; r < WIN_ROWS; r++) {
        for (int c = 0; c < WIN_COLS; c++) {
            char ch_ = w->text[r][c];
            if (ch_ == ' ') continue;
            fb_draw_glyph16((uint32_t)(cx + 4 + c * WIN_CHAR_W),
                            (uint32_t)(cy + 2 + r * WIN_CHAR_H),
                            ch_, w->fg);
        }
    }
}

void window_compose(void) {
    fb_clear(DESKTOP_BG);

    /* Paint windows back-to-front so the focused one ends up on top */
    int focus = focus_idx;
    for (int i = 0; i < window_n; i++) {
        if (i == focus) continue;
        paint_window(&windows[i]);
    }
    if (focus >= 0 && focus < window_n) {
        paint_window(&windows[focus]);
    }

#ifdef BOARD_HAS_GIC
    if (vmouse_present()) {
        int32_t mx = 0, my = 0;
        vmouse_position(&mx, &my);
        fb_draw_cursor((uint32_t)mx, (uint32_t)my, CURSOR_COLOR);
    }
#endif
}
