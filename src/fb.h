#ifndef HOBBY_OS_FB_H
#define HOBBY_OS_FB_H

#include <stdint.h>

#define FB_WIDTH   1920
#define FB_HEIGHT  1080

int  fb_init(void);

void fb_clear(uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_scroll_up(uint32_t pixel_rows, uint32_t bg);
void fb_draw_glyph(uint32_t x, uint32_t y, char c, uint32_t color, uint32_t scale);
void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t color, uint32_t scale);
void fb_draw_glyph16(uint32_t x, uint32_t y, char c, uint32_t color);
void fb_draw_cursor(uint32_t x, uint32_t y, uint32_t color);

/* Copy the back buffer (where everything draws) over the front buffer
 * the ramfb device is reading. Call once per finished frame so the
 * host never sees a half-rendered scene. */
void fb_present(void);

#endif
