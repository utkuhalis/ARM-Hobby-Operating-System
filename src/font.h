#ifndef HOBBY_OS_FONT_H
#define HOBBY_OS_FONT_H

#include <stdint.h>

#define FONT_GLYPH_W 8
#define FONT_GLYPH_H 8

const uint8_t *font_8x8_glyph(char c);

#endif
