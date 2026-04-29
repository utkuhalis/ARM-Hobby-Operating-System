#ifndef HOBBY_OS_FB_H
#define HOBBY_OS_FB_H

#include <stdint.h>

#define FB_WIDTH   800
#define FB_HEIGHT  600

int  fb_init(void);
void fb_clear(uint32_t color);
void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t color, uint32_t scale);

#endif
