#include <stdint.h>
#include "boot_splash.h"
#include "fb.h"

#define BG_TOP    0x000a2245u   /* deep blue */
#define BG_BOT    0x00060f1eu   /* near-black */
#define LOGO_FG   0x00ffffffu
#define LOGO_DIM  0x008cb6ddu
#define BAR_FRAME 0x002a3a52u
#define BAR_FILL  0x004a90e2u
#define BAR_BG    0x00141a26u
#define STATUS_FG 0x00b0c0d0u

#define LOGO_W 240
#define LOGO_H 240
#define BAR_W  640
#define BAR_H  18

/* A blocky 'H' rendered as a sequence of fb_fill_rects so we don't
 * need a real raster. Drawn at the center of the screen. */
static void paint_logo(int cx, int cy) {
    /* Background ring */
    fb_fill_rect((uint32_t)(cx - LOGO_W/2), (uint32_t)(cy - LOGO_H/2),
                 LOGO_W, LOGO_H, 0x00102950u);
    fb_fill_rect((uint32_t)(cx - LOGO_W/2 + 8), (uint32_t)(cy - LOGO_H/2 + 8),
                 LOGO_W - 16, LOGO_H - 16, 0x00081a30u);

    /* Big H */
    int bar_w = 32;
    int gap   = 60;
    int top   = cy - 70;
    int bot   = cy + 70;
    int left  = cx - gap - bar_w / 2;
    int right = cx + gap - bar_w / 2;
    fb_fill_rect((uint32_t)left,  (uint32_t)top,
                 (uint32_t)bar_w, (uint32_t)(bot - top), LOGO_FG);
    fb_fill_rect((uint32_t)right, (uint32_t)top,
                 (uint32_t)bar_w, (uint32_t)(bot - top), LOGO_FG);
    int crossbar_y = cy - 8;
    fb_fill_rect((uint32_t)(left + bar_w), (uint32_t)crossbar_y,
                 (uint32_t)(right - left - bar_w), 16, LOGO_FG);

    /* Brand text under the logo */
    fb_draw_string((uint32_t)(cx - 88), (uint32_t)(cy + LOGO_H/2 + 16),
                   "Hobby ARM OS", LOGO_FG, 2);
    fb_draw_string((uint32_t)(cx - 80), (uint32_t)(cy + LOGO_H/2 + 44),
                   "AArch64 hand-rolled kernel", LOGO_DIM, 1);
}

static int  bar_x, bar_y;
static int  bar_pct;
static int  splash_active;

static void paint_background(void) {
    /* simple vertical fade: top half BG_TOP, bottom BG_BOT */
    int half = (int)FB_HEIGHT / 2;
    fb_fill_rect(0, 0,           FB_WIDTH, (uint32_t)half, BG_TOP);
    fb_fill_rect(0, (uint32_t)half, FB_WIDTH,
                 FB_HEIGHT - (uint32_t)half, BG_BOT);
}

static void paint_bar_frame(void) {
    fb_fill_rect((uint32_t)(bar_x - 2), (uint32_t)(bar_y - 2),
                 BAR_W + 4, BAR_H + 4, BAR_FRAME);
    fb_fill_rect((uint32_t)bar_x, (uint32_t)bar_y, BAR_W, BAR_H, BAR_BG);
}

static void paint_bar_fill(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int w = (BAR_W * pct) / 100;
    if (w > 0) {
        fb_fill_rect((uint32_t)bar_x, (uint32_t)bar_y,
                     (uint32_t)w, BAR_H, BAR_FILL);
    }
    if (w < BAR_W) {
        fb_fill_rect((uint32_t)(bar_x + w), (uint32_t)bar_y,
                     (uint32_t)(BAR_W - w), BAR_H, BAR_BG);
    }
}

static void paint_status(const char *text) {
    /* clear strip */
    fb_fill_rect((uint32_t)bar_x, (uint32_t)(bar_y + BAR_H + 14),
                 BAR_W, 16, 0);
    int len = 0; while (text[len]) len++;
    int x = bar_x + (BAR_W - len * 8) / 2;
    if (x < 0) x = bar_x;
    fb_draw_string((uint32_t)x, (uint32_t)(bar_y + BAR_H + 14),
                   text, STATUS_FG, 1);
}

void boot_splash_init(void) {
    splash_active = 1;
    paint_background();

    int cx = (int)FB_WIDTH / 2;
    int cy = (int)FB_HEIGHT / 2 - 60;
    paint_logo(cx, cy);

    bar_x = (int)FB_WIDTH / 2 - BAR_W / 2;
    bar_y = (int)FB_HEIGHT / 2 + LOGO_H / 2 + 90;
    paint_bar_frame();
    paint_bar_fill(0);
    paint_status("Starting up...");
    bar_pct = 0;
    fb_present();
}

void boot_splash_step(int percent, const char *status) {
    if (!splash_active) return;
    /* Animate the bar smoothly toward the new percent so the boot
     * feels alive even on emulated hardware. */
    int from = bar_pct;
    int to   = percent;
    if (to < from) { bar_pct = to; paint_bar_fill(to); }
    else {
        for (int p = from; p <= to; p += 2) {
            paint_bar_fill(p);
            paint_status(status);
            fb_present();
            for (volatile int i = 0; i < 20000; i++) { }
        }
        bar_pct = to;
    }
    paint_status(status);
    fb_present();
}

void boot_splash_done(void) {
    if (!splash_active) return;
    paint_status("Ready.");
    paint_bar_fill(100);
    fb_present();
    for (volatile int i = 0; i < 800000; i++) { }
    splash_active = 0;
}
