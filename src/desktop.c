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
#include "fs.h"
#include "window.h"

#define TOP_BG      0x00141a26u
#define TOP_FG      0x00d6dde9u
#define TOP_DIM     0x008894a4u
#define ACCENT_OK   0x0044c87au
#define ACCENT_BAD  0x00d04c4cu

#define DOCK_BG     0x000d1220u
#define DOCK_BORDER 0x002a3550u
#define ICON_SIZE   64
#define ICON_GAP    44
#define ICON_LABEL_H  18

#define MAX_DOCK_ITEMS 16

/* Built-in dock entries: always present, can't be removed, never appear
 * in the App Store. The kernel side resolves the name -> launcher via
 * kernel_launch_builtin (in kernel.c). */
struct builtin_app {
    const char *name;
    uint32_t    color;
};

static const struct builtin_app builtins[] = {
    {"Terminal",   0x002a3a52u},
    {"Calculator", 0x004a78bcu},
    {"Notepad",    0x00b0b8c8u},
    {"Browser",    0x0044c87au},
    {"Calendar",   0x00cc7a44u},
    {"Tasks",      0x00a23a4eu},
    {"Disks",      0x004e7aa2u},
    {"Settings",   0x006a6e80u},
    {"App Store",  0x00bc7a44u},
    {0, 0},
};

extern int kernel_launch_builtin(const char *name);

#define DOCK_KIND_BUILTIN 0
#define DOCK_KIND_PKG     1

struct dock_item {
    int      kind;            /* DOCK_KIND_* */
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
    it->kind = DOCK_KIND_PKG;
    int i = 0;
    while (i < (int)sizeof(it->name) - 1 && name[i]) {
        it->name[i] = name[i];
        i++;
    }
    it->name[i] = 0;
    it->color = color_for(name);
    it->pressed = 0;
}

static void add_builtin(const struct builtin_app *b) {
    if (dock_count >= MAX_DOCK_ITEMS) return;
    struct dock_item *it = &dock[dock_count++];
    it->kind = DOCK_KIND_BUILTIN;
    int i = 0;
    while (i < (int)sizeof(it->name) - 1 && b->name[i]) {
        it->name[i] = b->name[i];
        i++;
    }
    it->name[i] = 0;
    it->color = b->color;
    it->pressed = 0;
}

void desktop_init(void) {
    dock_count = 0;
}

void desktop_rebuild_dock(void) {
    dock_count = 0;
    /* built-ins first (left side of the dock) */
    for (int i = 0; builtins[i].name; i++) {
        add_builtin(&builtins[i]);
    }
    /* then user-installed packages */
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
    uint32_t end = (uint32_t)n < out_max ? (uint32_t)n : out_max - 1;
    out[end] = 0;
}

static void paint_top_bar(void) {
    fb_fill_rect(0, 0, FB_WIDTH, DESKTOP_TOPBAR_H, TOP_BG);
    fb_fill_rect(0, DESKTOP_TOPBAR_H - 1, FB_WIDTH, 1, DOCK_BORDER);

    uint32_t lh = fb_text_ui_line_height();
    int text_y = ((int)DESKTOP_TOPBAR_H - (int)lh) / 2 - 1;

    /* Left: brand label */
    fb_draw_string_ui(14, (uint32_t)text_y, "Hobby ARM OS", TOP_FG);

    /* Right: net status + clock. Lay them out from the right edge. */
    char net[40];
    format_net(net, sizeof(net));

    char clock[24];
    format_clock(clock, timer_ticks() / (uint64_t)timer_hz());

    int right_x = (int)FB_WIDTH - 18;
    int cl_w = (int)fb_text_ui_width(clock);
    int cl_x = right_x - cl_w;
    fb_draw_string_ui((uint32_t)cl_x, (uint32_t)text_y, clock, TOP_FG);

    int net_w = (int)fb_text_ui_width(net);
    int net_x = cl_x - 28 - net_w;
    if (net_x < 220) net_x = 220;
    uint32_t color = vnet_present() ? ACCENT_OK : ACCENT_BAD;
    fb_fill_rect((uint32_t)(net_x - 14),
                 (uint32_t)(text_y + (int)lh / 2 - 4),
                 8, 8, color);
    fb_draw_string_ui((uint32_t)net_x, (uint32_t)text_y, net, TOP_DIM);
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

        /* Big first letter (heading-size smooth font) */
        char letter = it->name[0];
        if (letter >= 'a' && letter <= 'z') letter -= 'a' - 'A';
        char one[2] = { letter, 0 };
        uint32_t letter_w = fb_text_hd_width(one);
        fb_draw_string_hd((uint32_t)(ix + sz / 2 - (int)letter_w / 2),
                          (uint32_t)(iy + sz / 2 - 14),
                          one, TOP_FG);

        /* Label, smooth UI font, clipped to fit. */
        char label[20];
        int li = 0;
        while (li < 12 && it->name[li]) { label[li] = it->name[li]; li++; }
        label[li] = 0;
        uint32_t lw = fb_text_ui_width(label);
        int label_x = it->x + ICON_SIZE / 2 - (int)lw / 2;
        fb_draw_string_ui((uint32_t)label_x,
                          (uint32_t)(it->y + ICON_SIZE + 4),
                          label, TOP_FG);
    }
}

/* ------------- desktop file icons ------------- */

#define MAX_FILE_ICONS 16
#define FICON_SIZE     56
#define FICON_LABEL_H  12

struct file_icon {
    int      used;
    char     name[32];          /* fs file name */
    int      x, y;
    uint32_t color;
};

static struct file_icon ficons[MAX_FILE_ICONS];

/* Drag state for desktop icons. */
static int dicon_drag_idx = -1;
static int dicon_drag_off_x;
static int dicon_drag_off_y;
static int dicon_press_idx = -1;
static int dicon_press_x, dicon_press_y;
static int dicon_drag_started;
#define DRAG_THRESH 5

static int icon_visible_area(int y) {
    /* Icons live between the top bar and the dock. */
    return y >= DESKTOP_TOPBAR_H + 4
        && y <  (int)FB_HEIGHT - DESKTOP_DOCK_H - FICON_SIZE - FICON_LABEL_H;
}

static struct file_icon *find_or_alloc_icon(const char *name) {
    for (int i = 0; i < MAX_FILE_ICONS; i++) {
        if (ficons[i].used) {
            int eq = 1;
            for (int k = 0; k < (int)sizeof(ficons[i].name); k++) {
                if (ficons[i].name[k] != name[k]) { eq = 0; break; }
                if (name[k] == 0) break;
            }
            if (eq) return &ficons[i];
        }
    }
    for (int i = 0; i < MAX_FILE_ICONS; i++) {
        if (!ficons[i].used) {
            ficons[i].used = 1;
            int k = 0;
            while (k + 1 < (int)sizeof(ficons[i].name) && name[k]) {
                ficons[i].name[k] = name[k]; k++;
            }
            ficons[i].name[k] = 0;
            ficons[i].color = color_for(name);
            ficons[i].x = -1;  /* mark unplaced */
            return &ficons[i];
        }
    }
    return 0;
}

/* Re-derive the icon set from the fs each call. Preserves positions
 * for existing icons; auto-lays-out new ones in a top-down grid. */
static void sync_icons_from_fs(void) {
    /* mark all as candidates for removal first */
    static int seen[MAX_FILE_ICONS];
    for (int i = 0; i < MAX_FILE_ICONS; i++) seen[i] = 0;

    int next_x = 16;
    int next_y = DESKTOP_TOPBAR_H + 12;

    for (int i = 0; i < 16; i++) {
        fs_file_t *f = fs_at(i);
        if (!f) continue;
        struct file_icon *it = find_or_alloc_icon(f->name);
        if (!it) continue;
        int idx = (int)(it - ficons);
        seen[idx] = 1;
        if (it->x < 0) {
            /* fresh icon -> auto-place */
            it->x = next_x;
            it->y = next_y;
            next_y += FICON_SIZE + FICON_LABEL_H + 14;
            if (next_y > (int)FB_HEIGHT - DESKTOP_DOCK_H - FICON_SIZE - 30) {
                next_y = DESKTOP_TOPBAR_H + 12;
                next_x += FICON_SIZE + 28;
            }
        }
    }

    /* drop icons whose backing files are gone */
    for (int i = 0; i < MAX_FILE_ICONS; i++) {
        if (ficons[i].used && !seen[i]) {
            ficons[i].used = 0;
            ficons[i].name[0] = 0;
        }
    }
}

static int hits_ficon(struct file_icon *it, int32_t mx, int32_t my) {
    return mx >= it->x && mx < it->x + FICON_SIZE
        && my >= it->y && my < it->y + FICON_SIZE;
}

static void paint_file_icons(void) {
    sync_icons_from_fs();
    for (int i = 0; i < MAX_FILE_ICONS; i++) {
        if (!ficons[i].used) continue;
        struct file_icon *it = &ficons[i];

        /* page background */
        fb_fill_rect((uint32_t)it->x, (uint32_t)it->y,
                     FICON_SIZE, FICON_SIZE, 0x00ffffff);
        fb_fill_rect((uint32_t)it->x + 1, (uint32_t)it->y + 1,
                     FICON_SIZE - 2, FICON_SIZE - 2, 0x00f0f4ff);
        /* folded corner */
        fb_fill_rect((uint32_t)(it->x + FICON_SIZE - 12),
                     (uint32_t)it->y, 12, 12, it->color);
        /* lines on the page */
        for (int row = 18; row < FICON_SIZE - 6; row += 6) {
            fb_fill_rect((uint32_t)(it->x + 6),
                         (uint32_t)(it->y + row),
                         FICON_SIZE - 12, 1, 0x00b0b8c8);
        }

        /* label centered below */
        int label_len = 0; while (it->name[label_len]) label_len++;
        int max_chars = 14;
        if (label_len > max_chars) label_len = max_chars;
        char label[32];
        for (int k = 0; k < label_len; k++) label[k] = it->name[k];
        label[label_len] = 0;
        uint32_t lw = fb_text_ui_width(label);
        int label_x = it->x + FICON_SIZE / 2 - (int)lw / 2;
        fb_draw_string_ui((uint32_t)label_x,
                          (uint32_t)(it->y + FICON_SIZE + 4),
                          label, TOP_FG);
    }
}

/* Open a viewer window for a fs file. */
static void open_viewer(const char *name) {
    fs_file_t *f = fs_find(name);
    if (!f) return;
    char title[40];
    int t = 0;
    const char *p = "View: ";
    while (*p && t + 1 < (int)sizeof(title)) title[t++] = *p++;
    int n = 0;
    while (name[n] && t + 1 < (int)sizeof(title)) title[t++] = name[n++];
    title[t] = 0;

    window_t *w = window_create(title, 120, 80);
    if (!w) {
        console_printf("desktop: too many windows; close one first\n");
        return;
    }
    window_clear(w);
    for (uint32_t i = 0; i < f->size && i < 4096; i++) {
        char c = (char)f->data[i];
        if (c == 0) break;
        window_putc(w, c);
    }
    window_set_focus(w);
}

void desktop_paint_chrome(void) {
    paint_top_bar();
    paint_file_icons();
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

    int left = buttons & 0x1;
    int prev_left = prev_buttons & 0x1;
    int press   = left && !prev_left;
    int release = !left && prev_left;

    int dock_top = (int)FB_HEIGHT - DESKTOP_DOCK_H;

    /* ---- desktop-area: file icons ---- */
    if (my >= DESKTOP_TOPBAR_H && my < dock_top) {
        if (press) {
            for (int i = 0; i < MAX_FILE_ICONS; i++) {
                if (!ficons[i].used) continue;
                if (hits_ficon(&ficons[i], mx, my)) {
                    dicon_press_idx     = i;
                    dicon_press_x       = (int)mx;
                    dicon_press_y       = (int)my;
                    dicon_drag_off_x    = (int)mx - ficons[i].x;
                    dicon_drag_off_y    = (int)my - ficons[i].y;
                    dicon_drag_started  = 0;
                    return 1;
                }
            }
            return 0;  /* clicked empty desktop -> let windows handle it */
        }

        if (left && dicon_press_idx >= 0) {
            /* track for drag start */
            int dx = (int)mx - dicon_press_x;
            int dy = (int)my - dicon_press_y;
            if (!dicon_drag_started &&
                (dx > DRAG_THRESH || dx < -DRAG_THRESH ||
                 dy > DRAG_THRESH || dy < -DRAG_THRESH)) {
                dicon_drag_started = 1;
                dicon_drag_idx = dicon_press_idx;
            }
            if (dicon_drag_idx >= 0) {
                int nx = (int)mx - dicon_drag_off_x;
                int ny = (int)my - dicon_drag_off_y;
                if (nx < 0) nx = 0;
                if (ny < DESKTOP_TOPBAR_H + 2) ny = DESKTOP_TOPBAR_H + 2;
                if (nx > (int)FB_WIDTH  - FICON_SIZE) nx = FB_WIDTH - FICON_SIZE;
                if (ny > dock_top - FICON_SIZE - FICON_LABEL_H)
                    ny = dock_top - FICON_SIZE - FICON_LABEL_H;
                ficons[dicon_drag_idx].x = nx;
                ficons[dicon_drag_idx].y = ny;
            }
            return 1;
        }

        if (release && dicon_press_idx >= 0) {
            int idx = dicon_press_idx;
            int was_drag = dicon_drag_started;
            dicon_press_idx    = -1;
            dicon_drag_idx     = -1;
            dicon_drag_started = 0;
            if (!was_drag) {
                /* simple click on icon -> open viewer */
                open_viewer(ficons[idx].name);
            }
            return 1;
        }

        (void)icon_visible_area;  /* reserved for folder-drop later */
        return 0;
    }

    /* ---- dock area ---- */
    if (my < dock_top) return 0;

    int hovered = -1;
    for (int i = 0; i < dock_count; i++) {
        if (hits_icon(&dock[i], mx, my)) { hovered = i; break; }
    }
    for (int i = 0; i < dock_count; i++) dock[i].pressed = 0;
    if (left && hovered >= 0) dock[hovered].pressed = 1;

    if (release && hovered >= 0) {
        struct dock_item *it = &dock[hovered];
        if (it->kind == DOCK_KIND_BUILTIN) {
            kernel_launch_builtin(it->name);
        } else {
            int id = pkg_run_by_name(it->name);
            if (id < 0) {
                console_printf("\ndock: '%s' failed to start (%d)\n",
                               it->name, id);
            }
        }
        return 1;
    }
    return 1;
}
