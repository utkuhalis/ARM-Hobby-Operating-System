#include <stdint.h>
#include "wallpaper.h"
#include "fb.h"
#include "fs.h"

#define WP_FILE "/etc/wallpaper"

static int active_idx;

struct wp {
    const char *name;
    void      (*paint)(void);
};

/* ---- individual painters ---- */

static void wp_solid(uint32_t color) {
    fb_fill_rect(0, 0, FB_WIDTH, FB_HEIGHT, color);
}

static void wp_default(void) { wp_solid(0x000a0e18u); }
static void wp_midnight(void) { wp_solid(0x00040814u); }
static void wp_navy(void) { wp_solid(0x0014203au); }
static void wp_charcoal(void) { wp_solid(0x00141416u); }

static void wp_horizon(void) {
    /* vertical fade: deep blue at top, near-black at bottom */
    int rows = FB_HEIGHT / 8;
    for (int i = 0; i < 8; i++) {
        uint32_t r = 0x0a + (uint32_t)i * 0x02;
        uint32_t g = 0x12 + (uint32_t)i * 0x02;
        uint32_t b = 0x2c - (uint32_t)i * 0x03;
        uint32_t color = (r << 16) | (g << 8) | b;
        fb_fill_rect(0, (uint32_t)i * rows, FB_WIDTH,
                     (uint32_t)rows, color);
    }
}

static void wp_grid(void) {
    wp_solid(0x000a1422u);
    /* faint grid lines every 64 px */
    for (uint32_t x = 64; x < FB_WIDTH; x += 64) {
        fb_fill_rect(x, 0, 1, FB_HEIGHT, 0x00121e30u);
    }
    for (uint32_t y = 64; y < FB_HEIGHT; y += 64) {
        fb_fill_rect(0, y, FB_WIDTH, 1, 0x00121e30u);
    }
}

static void wp_stripes(void) {
    /* alternating dark / slightly lighter horizontal bands */
    uint32_t band = 56;
    int dark = 1;
    for (uint32_t y = 0; y < FB_HEIGHT; y += band) {
        fb_fill_rect(0, y, FB_WIDTH, band,
                     dark ? 0x000a0e18u : 0x00121826u);
        dark = !dark;
    }
}

static void wp_field(void) {
    /* deep teal */
    wp_solid(0x000a2a32u);
}

static const struct wp catalog[] = {
    {"Default",   wp_default},
    {"Midnight",  wp_midnight},
    {"Navy",      wp_navy},
    {"Charcoal",  wp_charcoal},
    {"Horizon",   wp_horizon},
    {"Grid",      wp_grid},
    {"Stripes",   wp_stripes},
    {"Field",     wp_field},
};
#define CATALOG_SIZE ((int)(sizeof(catalog)/sizeof(catalog[0])))

int wallpaper_count(void) { return CATALOG_SIZE; }
const char *wallpaper_name(int idx) {
    if (idx < 0 || idx >= CATALOG_SIZE) return "?";
    return catalog[idx].name;
}

int wallpaper_get(void) { return active_idx; }

int wallpaper_set(int idx) {
    if (idx < 0 || idx >= CATALOG_SIZE) return -1;
    active_idx = idx;
    char buf[2] = { (char)('0' + idx), '\n' };
    (void)fs_write(WP_FILE, buf, 2);
    return 0;
}

void wallpaper_load(void) {
    fs_file_t *f = fs_find(WP_FILE);
    if (!f || f->size == 0) {
        active_idx = 0;
        return;
    }
    int v = (int)(f->data[0] - '0');
    if (v < 0 || v >= CATALOG_SIZE) v = 0;
    active_idx = v;
}

void wallpaper_paint(void) {
    catalog[active_idx].paint();
}
