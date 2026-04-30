#include <stdint.h>
#include "desktop.h"
#include "fb.h"
#include "str.h"
#include "pkgmgr.h"
#include "pkgstore.h"
#include "timer.h"
#include "console.h"
#include "virtio_net.h"
#include "net.h"

#define TOP_BG      0x00141a26u
#define TOP_FG      0x00d6dde9u
#define TOP_DIM     0x008894a4u
#define ACCENT_OK   0x0044c87au
#define ACCENT_BAD  0x00d04c4cu

#define DOCK_BG     0x000d1220u
#define DOCK_BORDER 0x002a3550u
#define ICON_SIZE   44
#define ICON_GAP    12
#define ICON_LABEL_H  10

#define MAX_DOCK_ITEMS 12

struct dock_item {
    char     name[24];
    uint32_t color;
    int      x, y;
    int      pressed;
};

static struct dock_item dock[MAX_DOCK_ITEMS];
static int              dock_count;

/* Distinct colors per pkg name so icons don't all look the same. */
static uint32_t color_for(const char *name) {
    uint32_t h = 2166136261u;
    while (*name) {
        h = (h ^ (uint8_t)*name++) * 16777619u;
    }
    /* Force the upper byte so colors stay vivid, not muddy. */
    uint8_t r = 0x60 + (uint8_t)((h >> 16) & 0x9f);
    uint8_t g = 0x60 + (uint8_t)((h >>  8) & 0x9f);
    uint8_t b = 0x60 + (uint8_t)((h >>  0) & 0x9f);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void collect_installed(const char *name, void *ctx) {
    (void)ctx;
    if (dock_count >= MAX_DOCK_ITEMS) return;
    struct dock_item *it = &dock[dock_count++];
    int i = 0;
    while (i < (int)sizeof(it->name) - 1 && name[i]) {
        it->name[i] = name[i];
        i++;
    }
    it->name[i] = 0;
    it->color = color_for(name);
    it->pressed = 0;
}

void desktop_init(void) {
    dock_count = 0;
}

void desktop_rebuild_dock(void) {
    dock_count = 0;
    pkgstore_foreach(collect_installed, 0);

    /* Layout: center horizontally inside the dock strip. */
    int dock_y = FB_HEIGHT - DESKTOP_DOCK_H;
    int icon_y = dock_y + (DESKTOP_DOCK_H - ICON_SIZE - ICON_LABEL_H) / 2;
    int total  = dock_count * ICON_SIZE + (dock_count - 1) * ICON_GAP;
    if (total < 0) total = 0;
    int x      = (int)FB_WIDTH / 2 - total / 2;
    for (int i = 0; i < dock_count; i++) {
        dock[i].x = x;
        dock[i].y = icon_y;
        x += ICON_SIZE + ICON_GAP;
    }
}

/* ------------- top bar ------------- */

static void uitoa2(char *out, uint32_t v) {
    out[0] = (char)('0' + (v / 10) % 10);
    out[1] = (char)('0' + v % 10);
    out[2] = 0;
}

static int u8_to_str(uint8_t v, char *out) {
    int n = 0;
    if (v >= 100) out[n++] = (char)('0' + v / 100);
    if (v >= 10)  out[n++] = (char)('0' + (v / 10) % 10);
    out[n++] = (char)('0' + v % 10);
    out[n] = 0;
    return n;
}

static void format_clock(char *out, uint64_t seconds_since_boot) {
    /* boot wall-clock baked in: 2026-05-01 12:00:00 local */
    uint64_t s = seconds_since_boot + (uint64_t)12 * 3600;
    uint32_t days = (uint32_t)(s / 86400);
    uint32_t rem  = (uint32_t)(s % 86400);
    uint32_t h    = rem / 3600;
    uint32_t m    = (rem / 60) % 60;
    uint32_t sec  = rem % 60;

    char hh[3], mm[3], ss[3];
    uitoa2(hh, h);
    uitoa2(mm, m);
    uitoa2(ss, sec);

    out[0] = hh[0]; out[1] = hh[1]; out[2] = ':';
    out[3] = mm[0]; out[4] = mm[1]; out[5] = ':';
    out[6] = ss[0]; out[7] = ss[1];

    /* date: simple year-month-day calendar, starting at 2026-05-01.
     * good enough for a desktop clock; not a real RTC. */
    static const uint8_t days_in_month[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t year = 2026, month = 5, day = 1;
    while (days > 0) {
        uint32_t dim = days_in_month[month - 1];
        if (month == 2 && (year % 4 == 0)) dim = 29;
        if (days >= dim - day + 1) {
            days -= (dim - day + 1);
            day = 1;
            month++;
            if (month > 12) { month = 1; year++; }
        } else {
            day += days;
            days = 0;
        }
    }

    /* "  YYYY-MM-DD" appended at offset 8 */
    char *p = out + 8;
    *p++ = ' '; *p++ = ' ';
    *p++ = (char)('0' + (year / 1000) % 10);
    *p++ = (char)('0' + (year / 100)  % 10);
    *p++ = (char)('0' + (year / 10)   % 10);
    *p++ = (char)('0' + year % 10);
    *p++ = '-';
    *p++ = (char)('0' + (month / 10) % 10);
    *p++ = (char)('0' + month % 10);
    *p++ = '-';
    *p++ = (char)('0' + (day / 10) % 10);
    *p++ = (char)('0' + day % 10);
    *p   = 0;
}

static void format_net(char *out, uint32_t out_max) {
    if (!vnet_present()) {
        const char *s = "net: down";
        for (uint32_t i = 0; i < out_max && s[i]; i++) out[i] = s[i];
        out[out_max - 1] = 0;
        return;
    }
    uint32_t ip = net_my_ipv4();
    /* "net up  10.0.2.15  rx 12 tx 7" */
    char b[64];
    int n = 0;
    const char *p = "net up  ";
    while (*p) b[n++] = *p++;
    uint8_t a = (ip >> 24) & 0xff, b2 = (ip >> 16) & 0xff,
            c2 = (ip >> 8) & 0xff, d = ip & 0xff;
    char tmp[8];
    u8_to_str(a, tmp);  for (int i = 0; tmp[i]; i++) b[n++] = tmp[i]; b[n++] = '.';
    u8_to_str(b2, tmp); for (int i = 0; tmp[i]; i++) b[n++] = tmp[i]; b[n++] = '.';
    u8_to_str(c2, tmp); for (int i = 0; tmp[i]; i++) b[n++] = tmp[i]; b[n++] = '.';
    u8_to_str(d, tmp);  for (int i = 0; tmp[i]; i++) b[n++] = tmp[i];
    b[n] = 0;
    for (int i = 0; b[i] && (uint32_t)i < out_max - 1; i++) out[i] = b[i];
    out[(uint32_t)n < out_max ? n : out_max - 1] = 0;
}

static void paint_top_bar(void) {
    fb_fill_rect(0, 0, FB_WIDTH, DESKTOP_TOPBAR_H, TOP_BG);
    fb_fill_rect(0, DESKTOP_TOPBAR_H - 1, FB_WIDTH, 1, DOCK_BORDER);

    /* Left: app launcher hint */
    fb_draw_string(8, 4, "Hobby ARM OS", TOP_FG, 1);

    /* Right: net status, then date/time. We compute strings, then
     * lay them out from the right edge so they don't collide. */
    char net[40];
    format_net(net, sizeof(net));

    char clock[24];
    format_clock(clock, timer_ticks() / (uint64_t)timer_hz());

    int net_len = 0; while (net[net_len]) net_len++;
    int cl_len  = 0; while (clock[cl_len]) cl_len++;

    int right_x = (int)FB_WIDTH - 8;
    int cl_w = cl_len * 8;
    int cl_x = right_x - cl_w;
    fb_draw_string((uint32_t)cl_x, 4, clock, TOP_FG, 1);

    int net_x = cl_x - 16 - net_len * 8;
    if (net_x < 130) net_x = 130;
    uint32_t color = vnet_present() ? ACCENT_OK : ACCENT_BAD;
    /* small status dot */
    fb_fill_rect((uint32_t)(net_x - 10), 8, 6, 6, color);
    fb_draw_string((uint32_t)net_x, 4, net, TOP_DIM, 1);
}

/* ------------- dock ------------- */

static void paint_dock(void) {
    int dy = (int)FB_HEIGHT - DESKTOP_DOCK_H;
    fb_fill_rect(0, (uint32_t)dy, FB_WIDTH, DESKTOP_DOCK_H, DOCK_BG);
    fb_fill_rect(0, (uint32_t)dy, FB_WIDTH, 1, DOCK_BORDER);

    if (dock_count == 0) {
        fb_draw_string(20, (uint32_t)(dy + DESKTOP_DOCK_H / 2 - 4),
                       "no apps installed -- try: pkg install hello",
                       TOP_DIM, 1);
        return;
    }

    for (int i = 0; i < dock_count; i++) {
        struct dock_item *it = &dock[i];
        int sz = ICON_SIZE - (it->pressed ? 2 : 0);
        int ix = it->x + (it->pressed ? 1 : 0);
        int iy = it->y + (it->pressed ? 1 : 0);

        fb_fill_rect((uint32_t)ix, (uint32_t)iy,
                     (uint32_t)sz, (uint32_t)sz, it->color);
        /* darker border */
        fb_fill_rect((uint32_t)ix, (uint32_t)iy, (uint32_t)sz, 1, DOCK_BORDER);
        fb_fill_rect((uint32_t)ix, (uint32_t)(iy + sz - 1),
                     (uint32_t)sz, 1, DOCK_BORDER);
        fb_fill_rect((uint32_t)ix, (uint32_t)iy, 1, (uint32_t)sz, DOCK_BORDER);
        fb_fill_rect((uint32_t)(ix + sz - 1), (uint32_t)iy,
                     1, (uint32_t)sz, DOCK_BORDER);

        /* big first letter as the icon graphic */
        char letter = it->name[0];
        if (letter >= 'a' && letter <= 'z') letter -= 'a' - 'A';
        char one[2] = { letter, 0 };
        fb_draw_string((uint32_t)(ix + sz / 2 - 8),
                       (uint32_t)(iy + sz / 2 - 8 * 2),
                       one, TOP_FG, 2);

        /* small label below */
        int label_len = 0; while (it->name[label_len]) label_len++;
        int label_w = label_len * 8;
        int label_x = it->x + ICON_SIZE / 2 - label_w / 2;
        fb_draw_string((uint32_t)label_x,
                       (uint32_t)(it->y + ICON_SIZE + 1),
                       it->name, TOP_FG, 1);
    }
}

void desktop_paint_chrome(void) {
    paint_top_bar();
    paint_dock();
}

/* ------------- pointer dispatch ------------- */

static int hits_icon(struct dock_item *it, int32_t mx, int32_t my) {
    return mx >= it->x && mx < it->x + ICON_SIZE
        && my >= it->y && my < it->y + ICON_SIZE;
}

int desktop_handle_pointer(int32_t mx, int32_t my,
                           int buttons, int prev_buttons) {
    /* Top bar swallows clicks but does no work. */
    if (my >= 0 && my < DESKTOP_TOPBAR_H) {
        return 1;
    }

    int dock_top = (int)FB_HEIGHT - DESKTOP_DOCK_H;
    if (my < dock_top) return 0;

    int left = buttons & 0x1;
    int prev_left = prev_buttons & 0x1;

    /* Track press on icon while button is down so we can render the
     * pressed (sunken) state and only fire on release. */
    int hovered = -1;
    for (int i = 0; i < dock_count; i++) {
        if (hits_icon(&dock[i], mx, my)) { hovered = i; break; }
    }

    for (int i = 0; i < dock_count; i++) dock[i].pressed = 0;
    if (left && hovered >= 0) dock[hovered].pressed = 1;

    if (!left && prev_left && hovered >= 0) {
        /* release-on-icon -> launch */
        const char *name = dock[hovered].name;
        console_printf("\ndock: launching %s\n", name);
        int id = pkg_run_by_name(name);
        if (id < 0) {
            console_printf("dock: '%s' failed to start (%d)\n", name, id);
        }
        return 1;
    }

    /* in dock area: consume the click so it doesn't fall through to
     * a window drag */
    return 1;
}
