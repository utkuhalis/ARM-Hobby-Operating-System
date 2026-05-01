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
#define WIN_KIND_CUSTOM 2   /* the window's content is painted via
                             * a callback the owner installs */

#define WIDGET_LABEL      0
#define WIDGET_BUTTON     1
#define WIDGET_TEXT_INPUT 2
#define WIDGET_CANVAS     3   /* custom-painted region with click cb */

#define WIDGET_TEXT_MAX 64
#define WIDGET_INPUT_MAX 256
#define WIN_MAX_WIDGETS 64

struct window;

typedef struct widget {
    int   type;
    int   x, y, w, h;     /* relative to window content area */
    char  text[WIDGET_TEXT_MAX];
    int   pressed;
    void (*on_click)(struct window *win, struct widget *self);

    /* WIDGET_TEXT_INPUT-only fields */
    char  input[WIDGET_INPUT_MAX];
    int   input_len;
    int   input_focus;
    void (*on_submit)(struct window *win, struct widget *self);

    /* WIDGET_CANVAS hooks. The widget owns its (x,y,w,h) rect inside
     * the window content area; canvas_paint paints into framebuffer
     * space (origin already translated for the caller's convenience). */
    void *canvas_ctx;
    void (*canvas_paint)(struct window *win, struct widget *self,
                         int abs_x, int abs_y);
    void (*canvas_click)(struct window *win, struct widget *self,
                         int local_x, int local_y, int btn);
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
    /* WIN_KIND_CUSTOM hooks: paint_content owns the content area
     * (everything below the title bar); click_content is called
     * with window-local coords on a left click. user_data is owner-
     * controlled context. */
    void   (*paint_content)(struct window *w, int cx, int cy, int cw, int ch);
    void   (*click_content)(struct window *w, int local_x, int local_y, int btn);
    void    *user_data;
    /* Minimize / open / close animation state.
     * anim_t advances toward anim_target each compose frame; the
     * window is rendered scaled around its center by anim_t/256. */
    int      anim_t;
    int      anim_target;
    int      minimized;     /* visible == 0 but should restore later */
    int      closing;       /* anim_target == 0 and meant to disappear */
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
widget_t *window_add_text_input(window_t *w, int x, int y, int width,
                                const char *placeholder,
                                void (*on_submit)(window_t *, widget_t *));
widget_t *window_add_canvas    (window_t *w, int x, int y, int width, int height,
                                void *ctx,
                                void (*paint)(window_t *, widget_t *, int abs_x, int abs_y),
                                void (*click)(window_t *, widget_t *, int local_x, int local_y, int btn));
const char *widget_input_text(widget_t *g);
void        widget_input_clear(widget_t *g);
void     window_close(window_t *w);
void     window_minimize(window_t *w);
void     window_restore (window_t *w);   /* called by launch_*  */
void     window_clear(window_t *w);
void     window_putc(window_t *w, char c);
void     window_puts(window_t *w, const char *s);
void     window_set_focus(window_t *w);
window_t *window_focused(void);

void     window_compose(void);   /* paint all windows + cursor */

int      window_count(void);
window_t *window_at(int idx);

/* True if any visible (and not-fully-collapsed) window contains the
 * point. Used by the desktop so it doesn't start a rubber-band
 * selection or eat clicks that should reach a window. */
int      window_hits(int x, int y);

/* Find a non-dead window by title. Returns NULL if none exists, or
 * if the only matches are minimized / closing. */
window_t *window_find_by_title(const char *title);

/* Pointer event dispatcher: call once per tick with the latest cursor
 * state. Handles click-to-focus and title-bar dragging. */
void     window_handle_pointer(int32_t mx, int32_t my, int buttons);

/* Route a single typed character to the focused text-input widget.
 * Returns 1 if it was consumed, 0 if no input had focus and the
 * caller should treat the character as regular shell input. */
int      window_handle_keyboard(char c);

#endif
