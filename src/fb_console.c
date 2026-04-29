#include <stdint.h>
#include "fb.h"
#include "fb_console.h"

#define SCALE      2
#define CELL_W     (8 * SCALE)
#define CELL_H     (8 * SCALE)
#define LINE_PAD   0
#define ROW_H      (CELL_H + LINE_PAD)
#define COLS       (FB_WIDTH  / CELL_W)
#define STATUS_H   (ROW_H + 4)
#define USABLE_H   (FB_HEIGHT - STATUS_H)
#define ROWS       (USABLE_H / ROW_H)
#define BAR_BG     0x00161c28u
#define BAR_FG     0x00cfd4e0u
#define BAR_DIM    0x00606878u

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
    /* paint the status strip background */
    fb_fill_rect(0, USABLE_H, FB_WIDTH, STATUS_H, BAR_BG);
}

void fb_console_set_fg(uint32_t fg) {
    fg_color = fg;
}

static void newline(void) {
    cur_col = 0;
    cur_row++;
    if (cur_row >= ROWS) {
        /*
         * Scroll the framebuffer up by one row of text. The status bar
         * is repainted by the status thread at ~10 Hz so any stale
         * pixels in that strip get covered up quickly.
         */
        fb_scroll_up(ROW_H, bg_color);
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
            fb_fill_rect(cur_col * CELL_W, cur_row * ROW_H,
                         CELL_W, ROW_H, bg_color);
        }
        return;
    }
    if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7f) {
        return;
    }

    uint32_t x = cur_col * CELL_W;
    uint32_t y = cur_row * ROW_H;
    fb_fill_rect(x, y, CELL_W, ROW_H, bg_color);
    fb_draw_glyph(x, y + LINE_PAD / 2, c, fg_color, SCALE);

    cur_col++;
    if (cur_col >= COLS) {
        newline();
    }
}

void fb_console_status_set(const char *line) {
    fb_fill_rect(0, USABLE_H, FB_WIDTH, STATUS_H, BAR_BG);
    /* thin accent line above the bar */
    fb_fill_rect(0, USABLE_H, FB_WIDTH, 1, BAR_DIM);

    uint32_t x = 12;
    uint32_t y = USABLE_H + (STATUS_H - CELL_H) / 2;
    for (uint32_t i = 0; line[i] != '\0' && x + CELL_W <= FB_WIDTH; i++) {
        fb_draw_glyph(x, y, line[i], BAR_FG, SCALE);
        x += CELL_W;
    }
}
