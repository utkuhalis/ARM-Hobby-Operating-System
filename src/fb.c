#include <stdint.h>
#include "fb.h"
#include "fw_cfg.h"
#include "font.h"

#define FB_BPP    4
#define FB_STRIDE (FB_WIDTH * FB_BPP)

#define DRM_FORMAT_XRGB8888 0x34325258u

struct ramfb_cfg {
    uint64_t addr;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} __attribute__((packed));

static uint8_t fb_buffer[FB_WIDTH * FB_HEIGHT * FB_BPP]
    __attribute__((aligned(4096)));

static uint32_t *fb_pixels(void) {
    return (uint32_t *)fb_buffer;
}

int fb_init(void) {
    struct ramfb_cfg cfg = {
        .addr   = __builtin_bswap64((uint64_t)(uintptr_t)fb_buffer),
        .fourcc = __builtin_bswap32(DRM_FORMAT_XRGB8888),
        .flags  = 0,
        .width  = __builtin_bswap32(FB_WIDTH),
        .height = __builtin_bswap32(FB_HEIGHT),
        .stride = __builtin_bswap32(FB_STRIDE),
    };
    return fw_cfg_write_named("etc/ramfb", &cfg, sizeof(cfg));
}

void fb_clear(uint32_t color) {
    uint32_t *px = fb_pixels();
    for (uint32_t i = 0; i < FB_WIDTH * FB_HEIGHT; i++) {
        px[i] = color;
    }
}

static void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x < FB_WIDTH && y < FB_HEIGHT) {
        fb_pixels()[y * FB_WIDTH + x] = color;
    }
}

static void fb_draw_glyph(uint32_t x, uint32_t y, char c, uint32_t color, uint32_t scale) {
    const uint8_t *glyph = font_8x8_glyph(c);
    for (uint32_t row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            if (bits & (uint8_t)(0x80u >> col)) {
                for (uint32_t dy = 0; dy < scale; dy++) {
                    for (uint32_t dx = 0; dx < scale; dx++) {
                        fb_putpixel(x + col * scale + dx,
                                    y + row * scale + dy,
                                    color);
                    }
                }
            }
        }
    }
}

void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t color, uint32_t scale) {
    while (*s) {
        fb_draw_glyph(x, y, *s, color, scale);
        x += 8 * scale;
        s++;
    }
}
