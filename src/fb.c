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

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t *px = fb_pixels();
    if (x >= FB_WIDTH || y >= FB_HEIGHT) return;
    if (x + w > FB_WIDTH)  w = FB_WIDTH  - x;
    if (y + h > FB_HEIGHT) h = FB_HEIGHT - y;
    for (uint32_t dy = 0; dy < h; dy++) {
        uint32_t *row = &px[(y + dy) * FB_WIDTH + x];
        for (uint32_t dx = 0; dx < w; dx++) {
            row[dx] = color;
        }
    }
}

void fb_scroll_up(uint32_t pixel_rows, uint32_t bg) {
    if (pixel_rows == 0 || pixel_rows >= FB_HEIGHT) return;
    uint32_t *px = fb_pixels();
    uint32_t move = (FB_HEIGHT - pixel_rows) * FB_WIDTH;
    for (uint32_t i = 0; i < move; i++) {
        px[i] = px[i + pixel_rows * FB_WIDTH];
    }
    for (uint32_t i = move; i < FB_WIDTH * FB_HEIGHT; i++) {
        px[i] = bg;
    }
}

void fb_draw_glyph(uint32_t x, uint32_t y, char c, uint32_t color, uint32_t scale) {
    const uint8_t *glyph = font_8x8_glyph(c);
    uint32_t *px = fb_pixels();
    for (uint32_t row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            if (bits & (uint8_t)(1u << col)) {
                for (uint32_t dy = 0; dy < scale; dy++) {
                    uint32_t py = y + row * scale + dy;
                    if (py >= FB_HEIGHT) break;
                    uint32_t *line = &px[py * FB_WIDTH];
                    for (uint32_t dx = 0; dx < scale; dx++) {
                        uint32_t pxs = x + col * scale + dx;
                        if (pxs >= FB_WIDTH) break;
                        line[pxs] = color;
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
