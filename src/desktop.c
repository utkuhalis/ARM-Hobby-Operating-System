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
    int      is_folder;
    int      selected;
    char     name[32];          /* fs file name (folders end with '/') */
    int      x, y;
    uint32_t color;
};

static struct file_icon ficons[MAX_FILE_ICONS];

/* multi-select drag rectangle */
static int       sel_drag_active;
static int       sel_drag_x0, sel_drag_y0;
static int       sel_drag_x1, sel_drag_y1;

/* double-click detection */
static int       last_click_idx = -1;
static uint64_t  last_click_tick;

/* right-click context menu */
#define MENU_MAX 5
static int       menu_visible;
static int       menu_x, menu_y;
static int       menu_w, menu_h;
static int       menu_hover = -1;
static int       menu_target_icon = -1;  /* -1 = empty-area menu */

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

static int name_ends_slash(const char *s) {
    int n = 0; while (s[n]) n++;
    return n > 0 && s[n - 1] == '/';
}

static int is_top_level(const char *name) {
    /* Top-level icons: no '/' in the name, OR exactly one '/' at the
     * end (a folder marker). Files nested inside folders are hidden
     * from the desktop. */
    int slashes = 0;
    int last_slash_pos = -1;
    int n = 0;
    while (name[n]) {
        if (name[n] == '/') { slashes++; last_slash_pos = n; }
        n++;
    }
    if (slashes == 0) return 1;
    return slashes == 1 && last_slash_pos == n - 1;
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
            ficons[i].is_folder = name_ends_slash(name);
            ficons[i].selected = 0;
            ficons[i].x = -1;  /* mark unplaced */
            return &ficons[i];
        }
    }
    return 0;
}

static void clear_selection(void) {
    for (int i = 0; i < MAX_FILE_ICONS; i++) ficons[i].selected = 0;
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
        if (!is_top_level(f->name)) continue;
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

        /* selection halo */
        if (it->selected) {
            fb_fill_rect((uint32_t)(it->x - 4), (uint32_t)(it->y - 4),
                         FICON_SIZE + 8, FICON_SIZE + 8, 0x4a90e2ffu & 0x40ffffffu);
            fb_fill_rect((uint32_t)(it->x - 4), (uint32_t)(it->y - 4),
                         FICON_SIZE + 8, 2, 0x004a90e2u);
            fb_fill_rect((uint32_t)(it->x - 4), (uint32_t)(it->y + FICON_SIZE + 2),
                         FICON_SIZE + 8, 2, 0x004a90e2u);
            fb_fill_rect((uint32_t)(it->x - 4), (uint32_t)(it->y - 4),
                         2, FICON_SIZE + 8, 0x004a90e2u);
            fb_fill_rect((uint32_t)(it->x + FICON_SIZE + 2), (uint32_t)(it->y - 4),
                         2, FICON_SIZE + 8, 0x004a90e2u);
        }

        if (it->is_folder) {
            /* folder shape: a tab + body */
            uint32_t body = 0x00ddc070u;   /* warm folder yellow */
            uint32_t edge = 0x00b89c50u;
            int tab_w = FICON_SIZE / 2;
            fb_fill_rect((uint32_t)it->x, (uint32_t)it->y + 6,
                         (uint32_t)tab_w, 12, body);
            fb_fill_rect((uint32_t)it->x, (uint32_t)it->y + 12,
                         FICON_SIZE, FICON_SIZE - 12, body);
            fb_fill_rect((uint32_t)it->x, (uint32_t)it->y + 6,
                         FICON_SIZE, 1, edge);
            fb_fill_rect((uint32_t)it->x, (uint32_t)(it->y + FICON_SIZE - 1),
                         FICON_SIZE, 1, edge);
        } else {
            /* page background */
            fb_fill_rect((uint32_t)it->x, (uint32_t)it->y,
                         FICON_SIZE, FICON_SIZE, 0x00ffffff);
            fb_fill_rect((uint32_t)it->x + 1, (uint32_t)it->y + 1,
                         FICON_SIZE - 2, FICON_SIZE - 2, 0x00f0f4ff);
            fb_fill_rect((uint32_t)(it->x + FICON_SIZE - 12),
                         (uint32_t)it->y, 12, 12, it->color);
            for (int row = 18; row < FICON_SIZE - 6; row += 6) {
                fb_fill_rect((uint32_t)(it->x + 6),
                             (uint32_t)(it->y + row),
                             FICON_SIZE - 12, 1, 0x00b0b8c8);
            }
        }

        /* label centered below */
        int label_len = 0; while (it->name[label_len]) label_len++;
        int max_chars = 14;
        if (label_len > max_chars) label_len = max_chars;
        char label[32];
        for (int k = 0; k < label_len; k++) label[k] = it->name[k];
        if (label_len > 0 && label[label_len - 1] == '/') label_len--;
        label[label_len] = 0;
        uint32_t lw = fb_text_ui_width(label);
        int label_x = it->x + FICON_SIZE / 2 - (int)lw / 2;
        fb_draw_string_ui((uint32_t)label_x,
                          (uint32_t)(it->y + FICON_SIZE + 4),
                          label, TOP_FG);
    }

    /* multi-select drag rectangle */
    if (sel_drag_active) {
        int x0 = sel_drag_x0 < sel_drag_x1 ? sel_drag_x0 : sel_drag_x1;
        int y0 = sel_drag_y0 < sel_drag_y1 ? sel_drag_y0 : sel_drag_y1;
        int x1 = sel_drag_x0 < sel_drag_x1 ? sel_drag_x1 : sel_drag_x0;
        int y1 = sel_drag_y0 < sel_drag_y1 ? sel_drag_y1 : sel_drag_y0;
        int w = x1 - x0;
        int h = y1 - y0;
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        /* translucent fill via 1px stripes -- our fb_fill_rect is opaque */
        for (int yy = y0; yy < y1; yy += 2) {
            fb_fill_rect((uint32_t)x0, (uint32_t)yy,
                         (uint32_t)w, 1, 0x002c4a78u);
        }
        fb_fill_rect((uint32_t)x0, (uint32_t)y0, (uint32_t)w, 1, 0x004a90e2u);
        fb_fill_rect((uint32_t)x0, (uint32_t)y1, (uint32_t)w, 1, 0x004a90e2u);
        fb_fill_rect((uint32_t)x0, (uint32_t)y0, 1, (uint32_t)h, 0x004a90e2u);
        fb_fill_rect((uint32_t)x1, (uint32_t)y0, 1, (uint32_t)h, 0x004a90e2u);
    }

}

/* The right-click context menu has two sets of items. We pick which
 * set based on what was under the cursor when the user right-clicked
 * (icon -> "Open / Delete", empty -> "New Folder / New File / Refresh").
 * Filled in by desktop_handle_pointer when the menu opens. */
static const char *active_menu_labels[MENU_MAX];
static int          active_menu_count;
static const char *empty_menu[] = { "New Folder", "New File", "Refresh", 0 };
static const char *file_menu[]  = { "Open", "Delete", 0 };
static const char *folder_menu[]= { "Open Folder", "Delete", 0 };

static void set_menu(const char **items) {
    active_menu_count = 0;
    for (int i = 0; items[i] && i < MENU_MAX; i++) {
        active_menu_labels[i] = items[i];
        active_menu_count++;
    }
}

static void paint_context_menu(void) {
    if (!menu_visible) return;
    fb_fill_rect((uint32_t)menu_x, (uint32_t)menu_y,
                 (uint32_t)menu_w, (uint32_t)menu_h, 0x00f4f6fau);
    fb_fill_rect((uint32_t)menu_x, (uint32_t)menu_y, (uint32_t)menu_w, 1, 0x00141a26u);
    fb_fill_rect((uint32_t)menu_x, (uint32_t)(menu_y + menu_h - 1),
                 (uint32_t)menu_w, 1, 0x00141a26u);
    fb_fill_rect((uint32_t)menu_x, (uint32_t)menu_y, 1, (uint32_t)menu_h, 0x00141a26u);
    fb_fill_rect((uint32_t)(menu_x + menu_w - 1), (uint32_t)menu_y,
                 1, (uint32_t)menu_h, 0x00141a26u);
    int row_h = 30;
    for (int i = 0; i < active_menu_count; i++) {
        int ry = menu_y + 1 + i * row_h;
        if (i == menu_hover) {
            fb_fill_rect((uint32_t)(menu_x + 1), (uint32_t)ry,
                         (uint32_t)(menu_w - 2), (uint32_t)row_h, 0x00cce5ffu);
        }
        fb_draw_string_ui((uint32_t)(menu_x + 14), (uint32_t)(ry + 6),
                          active_menu_labels[i], 0x00141a26u);
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

/* Background layer: file icons + multi-select rubber-band. Painted
 * before windows so windows render on top of the icon layer. */
void desktop_paint_icons(void) {
    paint_file_icons();
}

/* Foreground layer: top bar, dock, and any context menu. Painted
 * after windows so chrome stays on top. */
void desktop_paint_chrome(void) {
    paint_top_bar();
    paint_dock();
    paint_context_menu();
}

/* ------------- pointer dispatch ------------- */

static int hits_icon(struct dock_item *it, int32_t mx, int32_t my) {
    return mx >= it->x && mx < it->x + ICON_SIZE
        && my >= it->y && my < it->y + ICON_SIZE;
}

/* Pick a unique fs name with the given prefix. Returns 1 on success. */
static int unique_fs_name(const char *prefix, char *out, int outsz, int folder) {
    for (int n = 1; n < 50; n++) {
        int o = 0;
        for (int i = 0; prefix[i] && o + 1 < outsz; i++) out[o++] = prefix[i];
        out[o++] = '-';
        if (n >= 10) out[o++] = (char)('0' + n / 10);
        out[o++] = (char)('0' + n % 10);
        if (folder) out[o++] = '/';
        out[o] = 0;
        if (!fs_find(out)) return 1;
    }
    return 0;
}

/* Open a window listing every fs file whose name starts with
 * "<folder_name>/". Each child gets a clickable label that calls
 * open_viewer when activated. The list is built once at open time;
 * to refresh, close the window and double-click the folder again. */
#include "window.h"
extern window_t *window_create(const char *title, int x, int y);
extern void      window_set_focus(window_t *w);
extern void      window_clear(window_t *w);
extern void      window_puts(window_t *w, const char *s);

static void open_folder(const char *folder_name) {
    /* folder_name is "foldername/" -- strip the slash for the title */
    char title[40];
    int o = 0;
    const char *p = "Folder: ";
    while (*p && o + 1 < (int)sizeof(title)) title[o++] = *p++;
    int i = 0;
    while (folder_name[i] && folder_name[i] != '/' && o + 1 < (int)sizeof(title)) {
        title[o++] = folder_name[i++];
    }
    title[o] = 0;

    window_t *w = window_create(title, 220, 140);
    if (!w) return;
    window_clear(w);

    /* prefix length including trailing slash */
    int plen = 0;
    while (folder_name[plen]) plen++;

    int found = 0;
    for (int k = 0; k < FS_MAX_FILES; k++) {
        fs_file_t *f = fs_at(k);
        if (!f) continue;
        int matches = 1;
        for (int m = 0; m < plen; m++) {
            if (f->name[m] != folder_name[m]) { matches = 0; break; }
        }
        if (!matches) continue;
        if (!f->name[plen]) continue;  /* the folder marker itself */
        window_puts(w, "  ");
        window_puts(w, f->name + plen);
        window_puts(w, "\n");
        found++;
    }
    if (!found) window_puts(w, "  (empty)\n");
    window_set_focus(w);
}

static int find_icon_at(int32_t mx, int32_t my) {
    for (int i = 0; i < MAX_FILE_ICONS; i++) {
        if (!ficons[i].used) continue;
        if (hits_ficon(&ficons[i], mx, my)) return i;
    }
    return -1;
}

static int find_folder_at(int32_t mx, int32_t my, int exclude_idx) {
    for (int i = 0; i < MAX_FILE_ICONS; i++) {
        if (i == exclude_idx) continue;
        if (!ficons[i].used) continue;
        if (!ficons[i].is_folder) continue;
        if (hits_ficon(&ficons[i], mx, my)) return i;
    }
    return -1;
}

static void delete_selected(void) {
    /* Delete every selected fs entry (recursively for folders -- so a
     * folder takes its children with it). */
    /* Snapshot names first since fs_delete may shuffle storage. */
    char doomed[MAX_FILE_ICONS][32];
    int  n = 0;
    for (int i = 0; i < MAX_FILE_ICONS; i++) {
        if (!ficons[i].used || !ficons[i].selected) continue;
        if (n >= MAX_FILE_ICONS) break;
        int k = 0;
        while (ficons[i].name[k] && k < 31) {
            doomed[n][k] = ficons[i].name[k]; k++;
        }
        doomed[n][k] = 0;
        n++;
    }
    for (int i = 0; i < n; i++) {
        const char *name = doomed[i];
        int is_folder = name[0] && name[(int)strlen(name) - 1] == '/';
        if (is_folder) {
            /* delete all children whose name starts with "name" */
            int plen = (int)strlen(name);
            for (int k = 0; k < FS_MAX_FILES; k++) {
                fs_file_t *f = fs_at(k);
                if (!f) continue;
                int matches = 1;
                for (int m = 0; m < plen; m++) {
                    if (f->name[m] != name[m]) { matches = 0; break; }
                }
                if (matches) (void)fs_delete(f->name);
            }
        } else {
            (void)fs_delete(name);
        }
    }
}

static void menu_action(int idx) {
    char name[32];
    if (menu_target_icon < 0) {
        /* empty-area menu */
        if (idx == 0) {
            if (unique_fs_name("folder", name, sizeof(name), 1)) {
                (void)fs_write(name, "", 0);
            }
        } else if (idx == 1) {
            if (unique_fs_name("file", name, sizeof(name), 0)) {
                (void)fs_write(name, "", 0);
            }
        }
        /* idx == 2 (Refresh): no-op */
    } else {
        /* icon-context menu: idx 0 = Open, idx 1 = Delete */
        struct file_icon *it = &ficons[menu_target_icon];
        if (idx == 0) {
            if (it->is_folder) open_folder(it->name);
            else               open_viewer(it->name);
        } else if (idx == 1) {
            /* If the right-clicked icon isn't already selected,
             * just delete it. Otherwise delete every selected icon. */
            int any_selected = 0;
            for (int i = 0; i < MAX_FILE_ICONS; i++) {
                if (ficons[i].used && ficons[i].selected) { any_selected = 1; break; }
            }
            if (!any_selected) {
                clear_selection();
                it->selected = 1;
            }
            delete_selected();
        }
    }
}

extern int window_hits(int x, int y);

int desktop_handle_pointer(int32_t mx, int32_t my,
                           int buttons, int prev_buttons) {
    /* Top bar swallows clicks but does no work. */
    if (my >= 0 && my < DESKTOP_TOPBAR_H) {
        return 1;
    }

    int left  = buttons & 0x1;
    int right = buttons & 0x2;
    int prev_left  = prev_buttons & 0x1;
    int prev_right = prev_buttons & 0x2;
    int press      = left  && !prev_left;
    int release    = !left && prev_left;
    int rpress     = right && !prev_right;

    int dock_top = (int)FB_HEIGHT - DESKTOP_DOCK_H;

    /* If the menu is visible, swallow clicks targeted at it first. */
    if (menu_visible) {
        int row_h = 30;
        if (mx >= menu_x && mx < menu_x + menu_w &&
            my >= menu_y && my < menu_y + menu_h) {
            menu_hover = (int)((my - menu_y - 1) / row_h);
            if (menu_hover < 0) menu_hover = 0;
            if (menu_hover >= active_menu_count) menu_hover = active_menu_count - 1;
            if (release) {
                menu_action(menu_hover);
                menu_visible = 0;
                menu_hover = -1;
            }
            return 1;
        } else if (press || rpress) {
            menu_visible = 0;
            menu_hover = -1;
            /* fall through so the click that dismissed the menu is
             * still processed normally below */
        } else {
            menu_hover = -1;
        }
    }

    /* ---- desktop-area ---- */
    if (my >= DESKTOP_TOPBAR_H && my < dock_top) {

        /* Right-click pops the context menu. Two modes:
         *   over an icon  -> Open / Delete (icon-context)
         *   over empty    -> New Folder / New File / Refresh
         * If a window is under the cursor and we're NOT over an icon,
         * defer the right-click to the window manager (currently a
         * no-op there, but keeps the menu from launching on top of a
         * window the user was clicking inside). */
        if (rpress) {
            int hovered_icon = find_icon_at(mx, my);
            if (hovered_icon < 0 && window_hits((int)mx, (int)my)) {
                return 0;
            }
            int x = (int)mx, y = (int)my;
            if (hovered_icon >= 0) {
                /* Single right-click on an unselected icon also selects it. */
                if (!ficons[hovered_icon].selected) {
                    clear_selection();
                    ficons[hovered_icon].selected = 1;
                }
                set_menu(ficons[hovered_icon].is_folder ? folder_menu : file_menu);
                menu_target_icon = hovered_icon;
            } else {
                set_menu(empty_menu);
                menu_target_icon = -1;
            }
            menu_w = 200;
            menu_h = active_menu_count * 30 + 2;
            if (x + menu_w > (int)FB_WIDTH)  x = (int)FB_WIDTH  - menu_w - 4;
            if (y + menu_h > dock_top)       y = dock_top - menu_h - 4;
            menu_x = x; menu_y = y;
            menu_visible = 1;
            menu_hover = -1;
            return 1;
        }

        if (press) {
            int hovered = find_icon_at(mx, my);
            if (hovered >= 0) {
                /* macOS-ish: single click selects (deselect others
                 * unless this one is already selected). Second click
                 * within ~half a second opens the file/folder. */
                if (!ficons[hovered].selected) {
                    clear_selection();
                    ficons[hovered].selected = 1;
                }
                uint64_t now = timer_ticks();
                if (last_click_idx == hovered &&
                    (now - last_click_tick) < (uint64_t)(timer_hz() / 2)) {
                    if (ficons[hovered].is_folder) {
                        open_folder(ficons[hovered].name);
                    } else {
                        open_viewer(ficons[hovered].name);
                    }
                    last_click_idx = -1;
                } else {
                    last_click_idx = hovered;
                    last_click_tick = now;
                }
                dicon_press_idx     = hovered;
                dicon_press_x       = (int)mx;
                dicon_press_y       = (int)my;
                dicon_drag_off_x    = (int)mx - ficons[hovered].x;
                dicon_drag_off_y    = (int)my - ficons[hovered].y;
                dicon_drag_started  = 0;
                return 1;
            }
            /* Empty press: if a window is under us, let the window
             * manager handle this click (focus / drag titlebar / hit
             * widgets). Otherwise start a rubber-band selection. */
            if (window_hits((int)mx, (int)my)) {
                return 0;
            }
            clear_selection();
            sel_drag_active = 1;
            sel_drag_x0 = sel_drag_x1 = (int)mx;
            sel_drag_y0 = sel_drag_y1 = (int)my;
            return 1;
        }

        if (left && dicon_press_idx >= 0) {
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

        if (left && sel_drag_active) {
            sel_drag_x1 = (int)mx;
            sel_drag_y1 = (int)my;
            int x0 = sel_drag_x0 < sel_drag_x1 ? sel_drag_x0 : sel_drag_x1;
            int y0 = sel_drag_y0 < sel_drag_y1 ? sel_drag_y0 : sel_drag_y1;
            int x1 = sel_drag_x0 < sel_drag_x1 ? sel_drag_x1 : sel_drag_x0;
            int y1 = sel_drag_y0 < sel_drag_y1 ? sel_drag_y1 : sel_drag_y0;
            for (int i = 0; i < MAX_FILE_ICONS; i++) {
                if (!ficons[i].used) continue;
                int ix0 = ficons[i].x;
                int iy0 = ficons[i].y;
                int ix1 = ix0 + FICON_SIZE;
                int iy1 = iy0 + FICON_SIZE;
                int overlap = !(ix1 < x0 || ix0 > x1 ||
                                iy1 < y0 || iy0 > y1);
                ficons[i].selected = overlap ? 1 : 0;
            }
            return 1;
        }

        /* Release of an icon drag: if dropped on a folder, move the
         * file (rename to "folder/name"). Otherwise leave it where the
         * user dropped it. */
        if (release && dicon_press_idx >= 0) {
            int src = dicon_press_idx;
            int was_drag = dicon_drag_started;
            dicon_press_idx    = -1;
            dicon_drag_idx     = -1;
            dicon_drag_started = 0;

            if (was_drag) {
                int folder = find_folder_at(mx, my, src);
                if (folder >= 0 && !ficons[src].is_folder) {
                    /* Build the new name "folderprefix" + "src->name". */
                    const char *fp = ficons[folder].name;  /* ends with '/' */
                    int fp_len = 0; while (fp[fp_len]) fp_len++;
                    char newname[FS_MAX_NAME + 1];
                    int o = 0;
                    while (fp[o] && o < FS_MAX_NAME) { newname[o] = fp[o]; o++; }
                    int sn_i = 0;
                    while (ficons[src].name[sn_i] && o + 1 < FS_MAX_NAME) {
                        newname[o++] = ficons[src].name[sn_i++];
                    }
                    newname[o] = 0;
                    if (!fs_find(newname)) {
                        fs_file_t *f = fs_find(ficons[src].name);
                        if (f) {
                            uint8_t  buf[FS_MAX_DATA];
                            uint32_t sz = f->size;
                            for (uint32_t k = 0; k < sz; k++) buf[k] = f->data[k];
                            (void)fs_delete(ficons[src].name);
                            (void)fs_write(newname, buf, sz);
                            /* Force the icon to disappear (it's no
                             * longer top-level) so sync_icons doesn't
                             * re-place it on the desktop. */
                            ficons[src].used = 0;
                        }
                    }
                }
            }
            return 1;
        }
        if (release && sel_drag_active) {
            sel_drag_active = 0;
            return 1;
        }

        (void)icon_visible_area;
        /* Empty desktop click that didn't start any of our drags --
         * defer to window manager so window focus / drag still work. */
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
