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

typedef struct window {
    int      id;
    int      x, y, w, h;
    char     title[WIN_TITLE_MAX];
    int      visible;
    int      focused;
    uint32_t bg;
    uint32_t fg;
    uint32_t accent;
    /* tiny back buffer of text glyphs */
    char     text[WIN_ROWS][WIN_COLS];
    int      cur_row;
    int      cur_col;
} window_t;

void     window_init(void);
window_t *window_create(const char *title, int x, int y);
void     window_clear(window_t *w);
void     window_putc(window_t *w, char c);
void     window_puts(window_t *w, const char *s);
void     window_set_focus(window_t *w);
window_t *window_focused(void);

void     window_compose(void);   /* paint all windows + cursor */

int      window_count(void);
window_t *window_at(int idx);

#endif
