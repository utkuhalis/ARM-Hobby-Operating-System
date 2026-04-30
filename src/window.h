#ifndef HOBBY_OS_WINDOW_H
#define HOBBY_OS_WINDOW_H

#include <stdint.h>

#define WIN_TITLE_MAX 32
#define WIN_ROWS      14
#define WIN_COLS      48
#define WIN_CHAR_W    8
#define WIN_CHAR_H    16
#define WIN_TITLE_H   18
#define WIN_BORDER    1

#define WIN_KIND_TEXT   0
#define WIN_KIND_WIDGET 1

#define WIDGET_LABEL  0
#define WIDGET_BUTTON 1

#define WIDGET_TEXT_MAX 24
#define WIN_MAX_WIDGETS 16

struct window;

typedef struct widget {
    int   type;
    int   x, y, w, h;     /* relative to window content area */
    char  text[WIDGET_TEXT_MAX];
    int   pressed;
    void (*on_click)(struct window *win, struct widget *self);
} widget_t;

typedef struct window {
    int      id;
    int      x, y, w, h;
    char     title[WIN_TITLE_MAX];
    int      visible;
    int      focused;
    int      kind;
    uint32_t bg;
    uint32_t fg;
    uint32_t accent;
    /* tiny back buffer of text glyphs (used when kind == WIN_KIND_TEXT) */
    char     text[WIN_ROWS][WIN_COLS];
    int      cur_row;
    int      cur_col;
    /* widget tree (used when kind == WIN_KIND_WIDGET) */
    widget_t widgets[WIN_MAX_WIDGETS];
    int      widget_count;
} window_t;

void     window_init(void);
window_t *window_create(const char *title, int x, int y);
window_t *window_create_widget(const char *title, int x, int y, int w, int h);
widget_t *window_add_label(window_t *w, int x, int y,
                           int width, const char *text);
void     widget_set_text(widget_t *g, const char *text);
widget_t *window_add_button(window_t *w, int x, int y, int width,
                            const char *text,
                            void (*on_click)(window_t *, widget_t *));
void     window_close(window_t *w);
void     window_clear(window_t *w);
void     window_putc(window_t *w, char c);
void     window_puts(window_t *w, const char *s);
void     window_set_focus(window_t *w);
window_t *window_focused(void);

void     window_compose(void);   /* paint all windows + cursor */

int      window_count(void);
window_t *window_at(int idx);

/* Pointer event dispatcher: call once per tick with the latest cursor
 * state. Handles click-to-focus and title-bar dragging. */
void     window_handle_pointer(int32_t mx, int32_t my, int buttons);

#endif
