#include <stdint.h>
#include "window.h"
#include "fb.h"
#include "fb_console.h"
#include "str.h"
#include "desktop.h"
#include "wallpaper.h"

#ifdef BOARD_HAS_GIC
#include "virtio_mouse.h"
#endif

#define MAX_WINDOWS 99

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
    w->kind    = WIN_KIND_TEXT;
    w->bg      = WIN_BG;
    w->fg      = WIN_FG;
    w->accent  = WIN_ACCENT;
    w->cur_row = 0;
    w->cur_col = 0;
    w->widget_count = 0;
    w->anim_t      = 32;       /* start small, grow into place */
    w->anim_target = 256;
    w->minimized   = 0;
    w->closing     = 0;
    w->paint_content = 0;
    w->click_content = 0;
    w->user_data     = 0;
    for (int r = 0; r < WIN_ROWS; r++)
        for (int c = 0; c < WIN_COLS; c++)
            w->text[r][c] = ' ';
    if (focus_idx < 0) focus_idx = window_n;
    window_n++;
    return w;
}

window_t *window_create_widget(const char *title, int x, int y, int w, int h) {
    if (window_n >= MAX_WINDOWS) return NULL;
    window_t *win = &windows[window_n];
    win->id      = window_n;
    win->x       = x;
    win->y       = y;
    win->w       = w;
    win->h       = h;
    copy_title(win->title, title);
    win->visible = 1;
    win->focused = 0;
    win->kind    = WIN_KIND_WIDGET;
    win->bg      = WIN_BG;
    win->fg      = WIN_FG;
    win->accent  = WIN_ACCENT;
    win->widget_count = 0;
    win->anim_t      = 32;
    win->anim_target = 256;
    win->minimized   = 0;
    win->closing     = 0;
    win->paint_content = 0;
    win->click_content = 0;
    win->user_data     = 0;
    if (focus_idx < 0) focus_idx = window_n;
    window_n++;
    return win;
}

static void copy_widget_text(char *dst, const char *src) {
    int i = 0;
    while (i < WIDGET_TEXT_MAX - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

widget_t *window_add_label(window_t *w, int x, int y,
                           int width, const char *text) {
    if (!w || w->widget_count >= WIN_MAX_WIDGETS) return NULL;
    widget_t *g = &w->widgets[w->widget_count++];
    g->type   = WIDGET_LABEL;
    g->x = x; g->y = y;
    g->w = width; g->h = WIN_CHAR_H;
    g->pressed = 0;
    g->on_click = NULL;
    copy_widget_text(g->text, text);
    return g;
}

widget_t *window_add_button(window_t *w, int x, int y, int width,
                            const char *text,
                            void (*on_click)(window_t *, widget_t *)) {
    if (!w || w->widget_count >= WIN_MAX_WIDGETS) return NULL;
    widget_t *g = &w->widgets[w->widget_count++];
    g->type   = WIDGET_BUTTON;
    g->x = x; g->y = y;
    g->w = width; g->h = WIN_CHAR_H + 6;
    g->pressed  = 0;
    g->on_click = on_click;
    copy_widget_text(g->text, text);
    return g;
}

widget_t *window_add_text_input(window_t *w, int x, int y, int width,
                                const char *placeholder,
                                void (*on_submit)(window_t *, widget_t *)) {
    if (!w || w->widget_count >= WIN_MAX_WIDGETS) return NULL;
    widget_t *g = &w->widgets[w->widget_count++];
    g->type      = WIDGET_TEXT_INPUT;
    g->x = x; g->y = y;
    g->w = width; g->h = WIN_CHAR_H + 6;
    g->pressed   = 0;
    g->on_click  = NULL;
    g->on_submit = on_submit;
    copy_widget_text(g->text, placeholder ? placeholder : "");
    g->input[0]  = '\0';
    g->input_len = 0;
    g->input_focus = 0;
    g->canvas_ctx = 0;
    g->canvas_paint = 0;
    g->canvas_click = 0;
    return g;
}

widget_t *window_add_canvas(window_t *w, int x, int y, int width, int height,
                            void *ctx,
                            void (*paint)(window_t *, widget_t *, int, int),
                            void (*click)(window_t *, widget_t *, int, int, int)) {
    if (!w || w->widget_count >= WIN_MAX_WIDGETS) return NULL;
    widget_t *g = &w->widgets[w->widget_count++];
    g->type = WIDGET_CANVAS;
    g->x = x; g->y = y;
    g->w = width; g->h = height;
    g->pressed = 0;
    g->on_click = 0;
    g->on_submit = 0;
    g->text[0] = 0;
    g->input[0] = 0;
    g->input_len = 0;
    g->input_focus = 0;
    g->canvas_ctx = ctx;
    g->canvas_paint = paint;
    g->canvas_click = click;
    return g;
}

const char *widget_input_text(widget_t *g) {
    return g ? g->input : "";
}

void widget_input_clear(widget_t *g) {
    if (!g) return;
    g->input[0]  = '\0';
    g->input_len = 0;
}

void window_close(window_t *w) {
    if (!w) return;
    /* Begin the close animation: shrink toward 0; window_compose
     * finalizes visible=0 once anim_t reaches the target. */
    w->closing     = 1;
    w->anim_target = 0;
}

void window_minimize(window_t *w) {
    if (!w) return;
    /* Same shrinking animation as close, but flagged minimized so
     * window_restore can re-grow it back into place. */
    w->closing     = 0;
    w->minimized   = 1;
    w->anim_target = 0;
}

void window_restore(window_t *w) {
    if (!w) return;
    w->minimized   = 0;
    w->closing     = 0;
    w->visible     = 1;
    w->anim_target = 256;
    if (w->anim_t == 0) w->anim_t = 32;  /* a touch of pop-in */
}

void widget_set_text(widget_t *g, const char *text) {
    if (!g) return;
    copy_widget_text(g->text, text);
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

int window_hits(int x, int y) {
    for (int i = window_n - 1; i >= 0; i--) {
        window_t *w = &windows[i];
        if (!w->visible) continue;
        if (w->anim_t < 200) continue;   /* still animating in */
        if (x >= w->x && x < w->x + w->w &&
            y >= w->y && y < w->y + w->h) {
            return 1;
        }
    }
    return 0;
}

static int point_in_window(window_t *w, int x, int y) {
    return x >= w->x && x < w->x + w->w &&
           y >= w->y && y < w->y + w->h;
}

static int point_in_titlebar(window_t *w, int x, int y) {
    return x >= w->x && x < w->x + w->w &&
           y >= w->y && y < w->y + WIN_TITLE_H;
}

static int point_in_close_button(window_t *w, int x, int y) {
    int button_size = WIN_TITLE_H - 2 * WIN_BORDER;
    int button_x = w->x + w->w - WIN_BORDER - button_size;
    int button_y = w->y + WIN_BORDER;
    return x >= button_x && x < button_x + button_size &&
           y >= button_y && y < button_y + button_size;
}

static int point_in_minimize_button(window_t *w, int x, int y) {
    int button_size = WIN_TITLE_H - 2 * WIN_BORDER;
    int button_x = w->x + w->w - WIN_BORDER - 2 * button_size - 2;
    int button_y = w->y + WIN_BORDER;
    return x >= button_x && x < button_x + button_size &&
           y >= button_y && y < button_y + button_size;
}

static widget_t *widget_at(window_t *w, int gx, int gy) {
    if (w->kind != WIN_KIND_WIDGET) return NULL;
    int local_x = gx - w->x - WIN_BORDER;
    int local_y = gy - w->y - WIN_TITLE_H;
    for (int i = 0; i < w->widget_count; i++) {
        widget_t *g = &w->widgets[i];
        if (g->type != WIDGET_BUTTON &&
            g->type != WIDGET_TEXT_INPUT &&
            g->type != WIDGET_CANVAS) continue;
        if (local_x >= g->x && local_x < g->x + g->w &&
            local_y >= g->y && local_y < g->y + g->h) {
            return g;
        }
    }
    return NULL;
}

static widget_t *focused_input;

static void clear_text_input_focus(void) {
    if (focused_input) {
        focused_input->input_focus = 0;
        focused_input = NULL;
    }
}

int window_handle_keyboard(char c) {
    if (!focused_input) return 0;
    if (c == '\n' || c == '\r') {
        if (focused_input->on_submit) {
            focused_input->on_submit(NULL, focused_input);
        }
        return 1;
    }
    if (c == '\b' || c == 0x7f) {
        if (focused_input->input_len > 0) {
            focused_input->input_len--;
            focused_input->input[focused_input->input_len] = '\0';
        }
        return 1;
    }
    if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7f) {
        return 1; /* swallow other control chars */
    }
    if (focused_input->input_len + 1 < WIDGET_INPUT_MAX) {
        focused_input->input[focused_input->input_len++] = c;
        focused_input->input[focused_input->input_len]   = '\0';
    }
    return 1;
}

void window_handle_pointer(int32_t mx, int32_t my, int buttons) {
    /* Top bar / dock get first crack: if a dock click hits, no
     * window underneath should also receive it. */
    if (desktop_handle_pointer(mx, my, buttons, prev_buttons)) {
        prev_buttons = buttons;
        return;
    }

    int left_now  = (buttons & 1) != 0;
    int left_prev = (prev_buttons & 1) != 0;
    int press     = left_now && !left_prev;
    int release   = !left_now && left_prev;

    if (press) {
        /* topmost window under the cursor wins (focused first, then later) */
        int hit = -1;
        if (focus_idx >= 0 && focus_idx < window_n &&
            windows[focus_idx].visible &&
            point_in_window(&windows[focus_idx], (int)mx, (int)my)) {
            hit = focus_idx;
        } else {
            for (int i = window_n - 1; i >= 0; i--) {
                if (windows[i].visible &&
                    point_in_window(&windows[i], (int)mx, (int)my)) {
                    hit = i;
                    break;
                }
            }
        }
        if (hit >= 0) {
            window_set_focus(&windows[hit]);
            /* Check for close / minimize button click first */
            if (point_in_close_button(&windows[hit], (int)mx, (int)my)) {
                window_close(&windows[hit]);
                clear_text_input_focus();
            } else if (point_in_minimize_button(&windows[hit], (int)mx, (int)my)) {
                window_minimize(&windows[hit]);
                clear_text_input_focus();
            } else if (windows[hit].kind == WIN_KIND_CUSTOM &&
                       windows[hit].click_content &&
                       (int)my >= windows[hit].y + WIN_TITLE_H) {
                int lx = (int)mx - (windows[hit].x + WIN_BORDER);
                int ly = (int)my - (windows[hit].y + WIN_TITLE_H);
                windows[hit].click_content(&windows[hit], lx, ly, 1);
                clear_text_input_focus();
            } else {
                widget_t *g = widget_at(&windows[hit], (int)mx, (int)my);
                if (g && g->type == WIDGET_BUTTON) {
                    g->pressed = 1;
                    clear_text_input_focus();
                } else if (g && g->type == WIDGET_TEXT_INPUT) {
                    clear_text_input_focus();
                    focused_input = g;
                    g->input_focus = 1;
                } else if (g && g->type == WIDGET_CANVAS) {
                    clear_text_input_focus();
                    if (g->canvas_click) {
                        int lx = (int)mx - (windows[hit].x + WIN_BORDER + g->x);
                        int ly = (int)my - (windows[hit].y + WIN_TITLE_H + g->y);
                        g->canvas_click(&windows[hit], g, lx, ly, 1);
                    }
                } else if (point_in_titlebar(&windows[hit], (int)mx, (int)my)) {
                    drag_active = 1;
                    drag_window = hit;
                    drag_off_x  = (int)mx - windows[hit].x;
                    drag_off_y  = (int)my - windows[hit].y;
                    clear_text_input_focus();
                } else {
                    clear_text_input_focus();
                }
            }
        } else {
            clear_text_input_focus();
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

        /* fire button click on release if cursor is still over a pressed
         * button in the focused window */
        if (focus_idx >= 0 && focus_idx < window_n) {
            window_t *w = &windows[focus_idx];
            for (int i = 0; i < w->widget_count; i++) {
                widget_t *g = &w->widgets[i];
                if (g->type != WIDGET_BUTTON) continue;
                if (g->pressed) {
                    widget_t *over = widget_at(w, (int)mx, (int)my);
                    if (over == g && g->on_click) {
                        g->on_click(w, g);
                    }
                    g->pressed = 0;
                }
            }
        }
    }

    prev_buttons = buttons;
}

static int draw_string_in_window(int wx, int wy, const char *s, uint32_t color,
                                 int max_chars) {
    int i;
    for (i = 0; i < max_chars && s[i]; i++) {
        fb_draw_glyph16((uint32_t)(wx + i * WIN_CHAR_W),
                        (uint32_t)wy, s[i], color);
    }
    return i;
}

static void draw_widget(window_t *w, widget_t *g) {
    int wx = w->x + WIN_BORDER + g->x;
    int wy = w->y + WIN_TITLE_H + g->y;

    if (g->type == WIDGET_CANVAS) {
        if (g->canvas_paint) g->canvas_paint(w, g, wx, wy);
        return;
    }

    if (g->type == WIDGET_TEXT_INPUT) {
        uint32_t bg     = 0x000a0e16u;
        uint32_t border = g->input_focus ? 0x004a78bcu : 0x00404858u;
        fb_fill_rect(wx, wy, g->w, g->h, bg);
        fb_fill_rect(wx, wy, g->w, 1, border);
        fb_fill_rect(wx, wy + g->h - 1, g->w, 1, border);
        fb_fill_rect(wx, wy, 1, g->h, border);
        fb_fill_rect(wx + g->w - 1, wy, 1, g->h, border);

        int tx = wx + 4;
        int ty = wy + (g->h - 16) / 2;
        int max_chars = (g->w - 8) / WIN_CHAR_W;

        if (g->input_len == 0 && g->text[0]) {
            /* placeholder */
            draw_string_in_window(tx, ty, g->text, 0x00606878u, max_chars);
        } else {
            int n = draw_string_in_window(tx, ty, g->input, w->fg, max_chars);
            if (g->input_focus && n < max_chars) {
                fb_fill_rect(tx + n * WIN_CHAR_W, ty, 1, 16, w->fg);
            }
        }
        return;
    }

    if (g->type == WIDGET_BUTTON) {
        uint32_t bg = g->pressed ? 0x00355077u : 0x002a3e60u;
        uint32_t edge = 0x00708ab0u;
        fb_fill_rect(wx, wy, g->w, g->h, bg);
        fb_fill_rect(wx, wy, g->w, 1, edge);
        fb_fill_rect(wx, wy + g->h - 1, g->w, 1, edge);
        fb_fill_rect(wx, wy, 1, g->h, edge);
        fb_fill_rect(wx + g->w - 1, wy, 1, g->h, edge);

        /* center the label horizontally */
        int len = 0;
        while (len < WIDGET_TEXT_MAX && g->text[len]) len++;
        int text_w = len * WIN_CHAR_W;
        int tx = wx + (g->w - text_w) / 2;
        int ty = wy + (g->h - 16) / 2;
        for (int i = 0; i < len; i++) {
            fb_draw_glyph16((uint32_t)(tx + i * WIN_CHAR_W),
                            (uint32_t)ty, g->text[i], 0x00e0e6f0u);
        }
        return;
    }

    /* WIDGET_LABEL */
    for (int i = 0; i < WIDGET_TEXT_MAX && g->text[i]; i++) {
        fb_draw_glyph16((uint32_t)(wx + i * WIN_CHAR_W),
                        (uint32_t)wy, g->text[i], w->fg);
    }
}

static void paint_window(window_t *w) {
    if (!w->visible) return;

    /* During open / minimize / close animations the window renders
     * at a fraction of its real size, scaled around the center. */
    int t = w->anim_t;
    if (t < 16) return;       /* basically gone -- skip drawing */
    if (t < 256) {
        int real_x = w->x, real_y = w->y, real_w = w->w, real_h = w->h;
        int scaled_w = real_w * t / 256;
        int scaled_h = real_h * t / 256;
        int scaled_x = real_x + (real_w - scaled_w) / 2;
        int scaled_y = real_y + (real_h - scaled_h) / 2;

        /* Just stroke a card outline + accent fill so the user sees
         * the animation without us trying to render every widget at
         * a non-integer scale. */
        fb_fill_rect((uint32_t)scaled_x, (uint32_t)scaled_y,
                     (uint32_t)scaled_w, (uint32_t)scaled_h,
                     w->focused ? WIN_ACCENT_FOCUS : w->accent);
        fb_fill_rect((uint32_t)scaled_x, (uint32_t)scaled_y,
                     (uint32_t)scaled_w, 1, WIN_BORDER_C);
        fb_fill_rect((uint32_t)scaled_x, (uint32_t)(scaled_y + scaled_h - 1),
                     (uint32_t)scaled_w, 1, WIN_BORDER_C);
        fb_fill_rect((uint32_t)scaled_x, (uint32_t)scaled_y, 1,
                     (uint32_t)scaled_h, WIN_BORDER_C);
        fb_fill_rect((uint32_t)(scaled_x + scaled_w - 1), (uint32_t)scaled_y,
                     1, (uint32_t)scaled_h, WIN_BORDER_C);
        return;
    }

    /* Border + frame */
    fb_fill_rect(w->x, w->y, w->w, w->h, WIN_BORDER_C);
    /* Title bar */
    fb_fill_rect(w->x + WIN_BORDER, w->y + WIN_BORDER,
                 w->w - 2 * WIN_BORDER, WIN_TITLE_H - WIN_BORDER,
                 w->focused ? WIN_ACCENT_FOCUS : w->accent);

    /* Title text - centered, smooth UI font */
    int title_width = (int)fb_text_ui_width(w->title);
    int button_size = WIN_TITLE_H - 2 * WIN_BORDER;
    int available_width = w->w - 2 * WIN_BORDER - 2 * button_size - 12;
    int tx = w->x + WIN_BORDER + (available_width - title_width) / 2;
    int ty = w->y + (WIN_TITLE_H - (int)fb_text_ui_line_height()) / 2;
    fb_draw_string_ui((uint32_t)tx, (uint32_t)ty, w->title, WIN_FG);

    /* Close button */
    int button_x = w->x + w->w - WIN_BORDER - button_size;
    int button_y = w->y + WIN_BORDER;
    uint32_t button_bg = 0x00303a4eu;
    uint32_t button_fg = 0x00d0d6e0u;
    fb_fill_rect(button_x, button_y, button_size, button_size, button_bg);
    int bx = button_x + (button_size - WIN_CHAR_W) / 2;
    int by = button_y + (button_size - 16) / 2;
    fb_draw_glyph16((uint32_t)bx, (uint32_t)by, 'X', button_fg);

    /* Minimize button (left of close) */
    int min_x = w->x + w->w - WIN_BORDER - 2 * button_size - 2;
    fb_fill_rect(min_x, button_y, button_size, button_size, button_bg);
    fb_draw_glyph16((uint32_t)(min_x + (button_size - WIN_CHAR_W) / 2),
                    (uint32_t)by, '_', button_fg);
    /* Content background */
    int cx = w->x + WIN_BORDER;
    int cy = w->y + WIN_TITLE_H;
    int cw = w->w - 2 * WIN_BORDER;
    int ch = w->h - WIN_TITLE_H - WIN_BORDER;
    fb_fill_rect(cx, cy, cw, ch, w->bg);

    if (w->kind == WIN_KIND_WIDGET) {
        for (int i = 0; i < w->widget_count; i++) {
            draw_widget(w, &w->widgets[i]);
        }
        return;
    }
    if (w->kind == WIN_KIND_CUSTOM && w->paint_content) {
        w->paint_content(w, cx, cy, cw, ch);
        return;
    }

    /* WIN_KIND_TEXT */
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
    wallpaper_paint();
    /* Desktop file icons sit on the wallpaper; windows go on top of
     * them. */
    desktop_paint_icons();

    /* Step every window's open/close/minimize animation one tick
     * before painting so motion is visible across frames. */
    for (int i = 0; i < window_n; i++) {
        window_t *w = &windows[i];
        if (!w->visible) continue;
        if (w->anim_t < w->anim_target) {
            w->anim_t += 32;
            if (w->anim_t > w->anim_target) w->anim_t = w->anim_target;
        } else if (w->anim_t > w->anim_target) {
            w->anim_t -= 32;
            if (w->anim_t < w->anim_target) w->anim_t = w->anim_target;
            if (w->anim_t == 0) {
                /* shrink finished -- finalize state */
                w->visible = 0;
                if (!w->minimized) {
                    /* a real close: leave visible=0 with anim_t=0 so
                     * the slot can be reused. minimized is preserved
                     * if set so window_restore can re-open. */
                    w->closing = 0;
                }
            }
        }
    }

    /* Paint windows back-to-front so the focused one ends up on top */
    int focus = focus_idx;
    for (int i = 0; i < window_n; i++) {
        if (i == focus) continue;
        paint_window(&windows[i]);
    }
    if (focus >= 0 && focus < window_n) {
        paint_window(&windows[focus]);
    }

    /* Top bar + dock paint last so they sit on top of any window
     * the user dragged into their strips. */
    desktop_paint_chrome();

#ifdef BOARD_HAS_GIC
    if (vmouse_present()) {
        int32_t mx = 0, my = 0;
        vmouse_position(&mx, &my);
        fb_draw_cursor((uint32_t)mx, (uint32_t)my, CURSOR_COLOR);
    }
#endif

    /* Flip the back buffer to the front buffer so the host sees a
     * fully composed frame instead of mid-render flicker. */
    fb_present();
}
