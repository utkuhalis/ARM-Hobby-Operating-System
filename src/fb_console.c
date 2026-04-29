#include <stdint.h>
#include "fb.h"
#include "fb_console.h"

#define SCALE   1
#define CELL_W  (8 * SCALE)
#define CELL_H  (8 * SCALE)
#define COLS    (FB_WIDTH  / CELL_W)
#define ROWS    (FB_HEIGHT / CELL_H)

static uint32_t cur_col;
static uint32_t cur_row;
static uint32_t bg_color;
static uint32_t fg_color;

void fb_console_init(uint32_t bg, uint32_t fg) {
    bg_color = bg;
    fg_color = fg;
    cur_col = 0;
    cur_row = 0;
    fb_clear(bg);
}

void fb_console_set_fg(uint32_t fg) {
    fg_color = fg;
}

static void newline(void) {
    cur_col = 0;
    cur_row++;
    if (cur_row >= ROWS) {
        fb_scroll_up(CELL_H, bg_color);
        cur_row = ROWS - 1;
    }
}

void fb_console_putc(char c) {
    if (c == '\n') {
        newline();
        return;
    }
    if (c == '\r') {
        cur_col = 0;
        return;
    }
    if (c == '\b') {
        if (cur_col > 0) {
            cur_col--;
            fb_fill_rect(cur_col * CELL_W, cur_row * CELL_H, CELL_W, CELL_H, bg_color);
        }
        return;
    }
    if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7f) {
        return;
    }

    fb_fill_rect(cur_col * CELL_W, cur_row * CELL_H, CELL_W, CELL_H, bg_color);
    fb_draw_glyph(cur_col * CELL_W, cur_row * CELL_H, c, fg_color, SCALE);

    cur_col++;
    if (cur_col >= COLS) {
        newline();
    }
}
