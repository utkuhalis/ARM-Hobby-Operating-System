#include <stdint.h>
#include "uart.h"
#include "console.h"
#include "shell.h"
#include "str.h"
#include "fs.h"
#include "sysinfo.h"
#include "heap.h"
#include "accounts.h"
#include "pkgmgr.h"
#ifdef BOARD_HAS_GIC
#include "pkgstore.h"
#include "desktop.h"
#include "exceptions.h"
#include "gic.h"
#include "timer.h"
#include "virtio_input.h"
#include "virtio_mouse.h"
#include "virtio_blk.h"
#include "virtio_net.h"
#include "net.h"
#include "mmu.h"
#include "task.h"
#endif

#ifdef BOARD_HAS_RAMFB
#include "fb.h"
#include "fb_console.h"
#include "window.h"
#include "login.h"
#include "boot_splash.h"
#include "wallpaper.h"
#endif

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];
extern uint8_t _stack_top[];

#ifdef BOARD_HAS_GIC
#include "psci.h"

#define MAX_CPUS 4
uint8_t secondary_stacks[MAX_CPUS * 16384] __attribute__((aligned(16)));

static volatile int cpus_alive = 1;  /* boot CPU counts itself */

void secondary_main(void) {
    /* Tiny ldxr/stxr increment so we don't drop counts when secondaries
     * race on entry (no LSE on cortex-a72). */
    int *p = (int *)&cpus_alive;
    __asm__ volatile(
        "1: ldxr w8, [%0]\n"
        "   add  w8, w8, #1\n"
        "   stxr w9, w8, [%0]\n"
        "   cbnz w9, 1b\n"
        : : "r"(p) : "x8", "x9", "memory");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

int kernel_cpu_count(void) { return cpus_alive; }

extern void secondary_start(void);

static void smp_bring_up(void) {
    for (int cpu = 1; cpu < MAX_CPUS; cpu++) {
        uint64_t target = 0x80000000ull | (uint64_t)cpu;
        psci_cpu_on(target,
                    (uint64_t)(uintptr_t)secondary_start,
                    0);
    }
}
#endif

/*
 * Slightly tinted background and softer foreground than pure white --
 * looks closer to a modern terminal than to monochrome VGA, and the
 * 2x bitmap font reads more legibly against it.
 */
#define BG_COLOR  0x000c1018u
#define FG_COLOR  0x00cfd4e0u

static void delay_ms(uint32_t ms) {
    uint64_t freq = sys_timer_freq();
    if (freq == 0) return;
    uint64_t cycles = (freq / 1000) * ms;
    uint64_t start  = sys_timer_count();
    while ((sys_timer_count() - start) < cycles) {
        __asm__ volatile("nop");
    }
}

static void post(void) {
    uint64_t midr   = sys_read_midr();
    uint32_t el     = sys_read_currentel();
    uint64_t tfreq  = sys_timer_freq();
    uint32_t kstart = (uint32_t)(uintptr_t)_kernel_start;
    uint32_t kend   = (uint32_t)(uintptr_t)_kernel_end;
    uint32_t stop   = (uint32_t)(uintptr_t)_stack_top;
    uint32_t ksize  = kend - kstart;
    uint32_t mhz_i  = (uint32_t)(tfreq / 1000000u);
    uint32_t mhz_d  = (uint32_t)((tfreq % 1000000u) / 100000u);

    /* Serial copy of POST output (still useful for headless logs). */
    console_puts("\n==========================================\n");
    console_printf(" HobbyBIOS v0.5    board: %s\n", sys_board_name());
    console_puts(" (c) 2026  Hobby ARM Operating System\n");
    console_puts("==========================================\n\n");

#ifdef BOARD_HAS_RAMFB
    boot_splash_step(5, "Detecting CPU...");
#endif
    delay_ms(120);
    console_printf("[ OK ] CPU      %s  (EL%u)\n", sys_cpu_name(midr), el);
    console_printf("                MIDR  0x%lx\n", midr);
    console_printf("                MPIDR 0x%lx\n", sys_read_mpidr());
    console_printf("                timer %u.%u MHz\n", mhz_i, mhz_d);

#ifdef BOARD_HAS_RAMFB
    boot_splash_step(15, "Mapping memory...");
#endif
    delay_ms(120);
    console_printf("[ OK ] Memory   256 MiB\n");
    console_printf("                kernel 0x%08x..0x%08x\n", kstart, kend);
    console_printf("                stack  0x%08x..0x%08x\n", kend, stop);

#ifdef BOARD_HAS_RAMFB
    boot_splash_step(25, "Bringing up display...");
    console_printf("[ OK ] Display  ramfb %ux%u XRGB8888\n",
                   (unsigned)FB_WIDTH, (unsigned)FB_HEIGHT);
#else
    console_puts("[ -- ] Display  serial only\n");
#endif

#ifdef BOARD_HAS_RAMFB
    boot_splash_step(30, "Initializing storage...");
#endif
    delay_ms(120);
    console_puts("[ OK ] Storage  RAM fs 16 x 4 KiB (volatile)\n");

    delay_ms(120);
    console_printf("[ OK ] Console  PL011 UART @ 0x%lx\n", (uint64_t)UART_BASE);

#ifdef BOARD_HAS_GIC
#ifdef BOARD_HAS_RAMFB
    boot_splash_step(40, "Enabling MMU + caches...");
#endif
    delay_ms(120);
    console_puts("[ OK ] MMU      4 KiB granule, 39-bit VA, I+D caches\n");

    delay_ms(120);
    console_puts("[ OK ] Heap     kalloc/kfree, 2 MiB pool\n");

    delay_ms(120);
    console_printf("[ OK ] Sched    cooperative, %u Hz tick\n", timer_hz());

#ifdef BOARD_HAS_RAMFB
    boot_splash_step(55, "Configuring interrupts...");
#endif
    delay_ms(120);
    console_puts("[ OK ] IRQ      GIC v2 (distributor + CPU iface)\n");

#ifdef BOARD_HAS_RAMFB
    boot_splash_step(65, "Probing input devices...");
#endif
    delay_ms(120);
    int kbd_irq = vinput_irq_number();
    if (kbd_irq >= 0) {
        console_printf("[ OK ] Kbd      virtio-input @ IRQ %d  (mmio v%u)\n",
                       kbd_irq, vinput_mmio_version());
    } else {
        console_puts("[ -- ] Kbd      no virtio-input found\n");
    }

    delay_ms(120);
    int mouse_irq = vmouse_irq_number();
    if (mouse_irq >= 0) {
        console_printf("[ OK ] Mouse    virtio-mouse @ IRQ %d\n", mouse_irq);
    } else {
        console_puts("[ -- ] Mouse    no virtio-mouse found\n");
    }

#ifdef BOARD_HAS_RAMFB
    boot_splash_step(80, "Mounting filesystem...");
#endif
    delay_ms(120);
    if (vblk_present()) {
        console_printf("[ OK ] Block    virtio-blk @ IRQ %d  %lu sectors\n",
                       vblk_irq_number(),
                       (unsigned long)vblk_capacity_sectors());
        if (fs_load() == 0) {
            console_puts("                fs auto-loaded from disk\n");
        }
        pkgstore_init();
        desktop_rebuild_dock();
        int installed = 0;
        for (int i = 0; i < pkg_count(); i++) {
            if (pkg_is_installed(i)) installed++;
        }
        console_printf("                pkgstore: %d package%s installed\n",
                       installed, installed == 1 ? "" : "s");
        /* Seed the desktop with a couple of files on a fresh disk so
         * the icon area isn't empty and the user has something to
         * click. Skipped silently if these names already exist. */
        if (!fs_find("readme")) {
            const char *msg =
                "Welcome to Hobby ARM OS!\n\n"
                "- Drag this icon around the desktop with the mouse.\n"
                "- Click an icon to open the file in a viewer.\n"
                "- Use the dock at the bottom to launch installed apps.\n"
                "- Use 'pkg install <name>' from the terminal.\n";
            (void)fs_write("readme", msg, (uint32_t)strlen(msg));
        }
        if (!fs_find("notes")) {
            (void)fs_write("notes", "scratch pad\n", 12);
        }
        /* Persist the seed files now, then arm auto-save so every
         * subsequent fs_write / fs_delete flushes to virtio-blk. */
        (void)fs_save();
        fs_set_autosave(1);
        /* Pick up the user's last wallpaper choice, if any. */
        wallpaper_load();
    } else {
        console_puts("[ -- ] Block    no virtio-blk found\n");
    }

#ifdef BOARD_HAS_RAMFB
    boot_splash_step(90, "Bringing up network...");
#endif
    delay_ms(120);
    if (vnet_present()) {
        const uint8_t *m = vnet_mac();
        console_printf("[ OK ] Net      virtio-net @ IRQ %d\n", vnet_irq_number());
        console_printf("                MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                       m[0], m[1], m[2], m[3], m[4], m[5]);
    } else {
        console_puts("[ -- ] Net      no virtio-net found\n");
    }
#else
    console_puts("[ -- ] IRQ      polling only on this board\n");
#endif

#ifdef BOARD_HAS_RAMFB
    boot_splash_step(100, "Almost there...");
#endif
    delay_ms(120);
#ifdef BOARD_HAS_GIC
    /* give secondaries a chance to register before we report the count */
    delay_ms(40);
    console_printf("[ OK ] SMP      %d CPU%s online (via PSCI CPU_ON)\n",
                   cpus_alive, cpus_alive == 1 ? "" : "s");
#endif

    delay_ms(150);
    console_puts("[ OK ] Power    PSCI SYSTEM_OFF / RESET\n");

    delay_ms(200);
    console_puts("\n------------------------------------------\n");
    console_puts(" Boot complete. Type 'help' for commands.\n");
    console_puts("------------------------------------------\n\n");
}

#ifdef BOARD_HAS_GIC
static volatile uint64_t ticker_beats;

static int task_state_count(int *running, int *ready) {
    int total = 0;
    *running = 0;
    *ready = 0;
    for (task_t *t = task_first(); t; t = t->next) {
        total++;
        if (t->state == 0) (*ready)++;
        if (t->state == 1) (*running)++;
    }
    return total;
}

static void format_uint(char *buf, uint64_t v) {
    char tmp[24];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = '0' + (char)(v % 10); v /= 10; }
    int o = 0;
    while (n > 0) buf[o++] = tmp[--n];
    buf[o] = '\0';
}

static window_t *win_terminal;
static window_t *win_monitor;
static window_t *win_about;
static window_t *win_calc;
static window_t *win_store;
static window_t *win_notepad;
static window_t *win_browser;
static window_t *win_calendar;
static window_t *win_tasks;
static window_t *win_disks;
static window_t *win_settings;
static widget_t *calc_display;
static widget_t *notepad_input;
static widget_t *notepad_status;
static widget_t *browser_input;
static widget_t *browser_status;
static window_t *win_browser_view;
static widget_t *settings_wp_label;
static widget_t *settings_user_label;
static widget_t *settings_net_label;

/* ---------------- App Store ---------------- */

#include "pkgmgr.h"

#define STORE_MAX_ENTRIES 8
static widget_t *store_status_lbl[STORE_MAX_ENTRIES];
static widget_t *store_action_btn[STORE_MAX_ENTRIES];

static void store_refresh_row(int idx) {
    int installed = pkg_is_installed(idx);
    widget_set_text(store_status_lbl[idx],
                    installed ? "INSTALLED" : "available");
    widget_set_text(store_action_btn[idx],
                    installed ? "Remove" : "Install");
}

static void store_action_cb(window_t *w, widget_t *self) {
    (void)w;
    /* find which package this button belongs to */
    for (int i = 0; i < STORE_MAX_ENTRIES; i++) {
        if (store_action_btn[i] == self) {
            if (pkg_is_installed(i)) {
                pkg_remove_by_name(pkg_name_at(i));
            } else {
                pkg_install_by_name(pkg_name_at(i));
            }
            store_refresh_row(i);
            return;
        }
    }
}

static void build_store_window(void) {
    int n = pkg_count();
    if (n > STORE_MAX_ENTRIES) n = STORE_MAX_ENTRIES;

    const int row_h = 24;
    int win_w = 480;
    int win_h = 22 + n * row_h + 8;

    win_store = window_create_widget("App Store", 200, 300, win_w, win_h);

    window_add_label(win_store, 10, 4, win_w - 20,
                     "Install / remove apps from the repo");

    for (int i = 0; i < n; i++) {
        int row_y = 22 + i * row_h;
        window_add_label (win_store,  10, row_y,  72, pkg_name_at(i));
        window_add_label (win_store,  82, row_y, 240, pkg_summary_at(i));
        store_status_lbl[i] = window_add_label (win_store, 322, row_y,  80, "");
        store_action_btn[i] = window_add_button(win_store, 400, row_y - 3, 70,
                                                "", store_action_cb);
        store_refresh_row(i);
    }
}

static void about_close_cb(window_t *w, widget_t *g) {
    (void)g;
    window_close(w);
}

/* Tiny stack-machine calculator backing the on-screen buttons. */
static long  calc_acc;
static long  calc_pending;
static char  calc_op = 0;
static int   calc_after_eq;

static void calc_render(void) {
    char buf[24];
    long v = calc_acc;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    char tmp[20];
    int  n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = '0' + (char)(v % 10); v /= 10; }
    int o = 0;
    if (neg) buf[o++] = '-';
    while (n > 0) buf[o++] = tmp[--n];
    buf[o] = '\0';
    widget_set_text(calc_display, buf);
}

static void calc_apply(void) {
    if (calc_op == '+') calc_acc = calc_pending + calc_acc;
    else if (calc_op == '-') calc_acc = calc_pending - calc_acc;
    else if (calc_op == '*') calc_acc = calc_pending * calc_acc;
    else if (calc_op == '/') calc_acc = (calc_acc != 0)
                                        ? calc_pending / calc_acc
                                        : 0;
    calc_op = 0;
    calc_pending = 0;
}

static void calc_digit(window_t *w, widget_t *g) {
    (void)w;
    int d = g->text[0] - '0';
    if (calc_after_eq) { calc_acc = 0; calc_after_eq = 0; }
    if (calc_acc < 100000000) {
        calc_acc = calc_acc * 10 + d;
    }
    calc_render();
}

static void calc_set_op(window_t *w, widget_t *g) {
    (void)w;
    if (calc_op) { calc_apply(); }
    calc_pending = calc_acc;
    calc_acc     = 0;
    calc_op      = g->text[0];
    calc_after_eq = 0;
    calc_render();
}

static void calc_eq(window_t *w, widget_t *g) {
    (void)w; (void)g;
    if (calc_op) calc_apply();
    calc_after_eq = 1;
    calc_render();
}

static void calc_clear(window_t *w, widget_t *g) {
    (void)w; (void)g;
    calc_acc = 0;
    calc_pending = 0;
    calc_op = 0;
    calc_after_eq = 0;
    calc_render();
}

/* ---------------- Notepad ---------------- */

#include "fs.h"

static void notepad_save_cb(window_t *w, widget_t *self) {
    (void)w; (void)self;
    if (!notepad_input) return;
    const char *txt = widget_input_text(notepad_input);
    int n = 0;
    while (txt[n]) n++;
    if (fs_write("notes", txt, (uint32_t)n) == 0) {
        widget_set_text(notepad_status, "saved -> /notes");
    } else {
        widget_set_text(notepad_status, "save failed");
    }
}

static void notepad_load_cb(window_t *w, widget_t *self) {
    (void)w; (void)self;
    if (!notepad_input) return;
    fs_file_t *f = fs_find("notes");
    if (!f) {
        widget_set_text(notepad_status, "no /notes yet");
        return;
    }
    /* copy file contents into the input buffer */
    widget_input_clear(notepad_input);
    for (uint32_t i = 0; i < f->size && i < WIDGET_INPUT_MAX - 1; i++) {
        char c = (char)f->data[i];
        if (c == '\n') break;
        if (notepad_input->input_len + 1 < WIDGET_INPUT_MAX) {
            notepad_input->input[notepad_input->input_len++] = c;
            notepad_input->input[notepad_input->input_len]   = '\0';
        }
    }
    widget_set_text(notepad_status, "loaded /notes");
}

static void build_notepad_window(void) {
    win_notepad = window_create_widget("Notepad", 320, 200, 480, 90);
    window_add_label(win_notepad,  10,  4, 460, "Type a note, then Save:");
    notepad_input  = window_add_text_input(win_notepad, 10, 22, 280,
                                           "(click here, then type)",
                                           notepad_save_cb);
    window_add_button(win_notepad, 296, 18, 80, "Save", notepad_save_cb);
    window_add_button(win_notepad, 380, 18, 80, "Load", notepad_load_cb);
    notepad_status = window_add_label(win_notepad, 10, 44, 460, "");
}

static void build_calculator_window(void) {
    win_calc = window_create_widget("Calculator", 800, 200, 180, 240);

    calc_display = window_add_label(win_calc, 10, 10, 160, "0");

    struct {
        const char *label;
        int x, y;
        void (*cb)(window_t *, widget_t *);
    } buttons[] = {
        {"7", 10,  40, calc_digit}, {"8", 50,  40, calc_digit},
        {"9", 90,  40, calc_digit}, {"/",130,  40, calc_set_op},
        {"4", 10,  80, calc_digit}, {"5", 50,  80, calc_digit},
        {"6", 90,  80, calc_digit}, {"*",130,  80, calc_set_op},
        {"1", 10, 120, calc_digit}, {"2", 50, 120, calc_digit},
        {"3", 90, 120, calc_digit}, {"-",130, 120, calc_set_op},
        {"0", 10, 160, calc_digit}, {"C", 50, 160, calc_clear},
        {"=", 90, 160, calc_eq},    {"+",130, 160, calc_set_op},
    };
    for (int i = 0; i < 16; i++) {
        window_add_button(win_calc, buttons[i].x, buttons[i].y, 36,
                          buttons[i].label, buttons[i].cb);
    }
}

/* ---------------- Browser ---------------- */

#include "http.h"
#include "browser.h"
#define LEGACY_BROWSER 0
#if LEGACY_BROWSER

static void browser_go_cb(window_t *w, widget_t *self) {
    (void)w; (void)self;
    if (!browser_input) return;
    const char *url = widget_input_text(browser_input);

    /* Accept either "http://host/path" or just "/path" (defaults to
     * the QEMU host gateway 10.0.2.2:8090). */
    char host_buf[64] = "10.0.2.2";
    uint16_t port = 8090;
    uint32_t ip = (10u<<24) | (0u<<16) | (2u<<8) | 2u;
    const char *path = url;

    if (url[0] == 'h' && url[1] == 't' && url[2] == 't' && url[3] == 'p') {
        const char *p = url;
        while (*p && *p != '/') p++;
        while (*p == '/') p++;
        int hi = 0;
        while (*p && *p != '/' && *p != ':' && hi + 1 < (int)sizeof(host_buf)) {
            host_buf[hi++] = *p++;
        }
        host_buf[hi] = 0;
        if (*p == ':') {
            p++;
            int v = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            if (v) port = (uint16_t)v;
        }
        path = (*p == '/') ? p : "/";
    } else if (url[0] != '/') {
        path = "/";
    }

    if (browser_status) widget_set_text(browser_status, "fetching...");
    static uint8_t body[8192];
    int status = 0;
    int n = http_get(ip, port, host_buf, path,
                     body, sizeof(body) - 1, &status);
    if (n < 0) {
        if (browser_status) {
            char msg[40];
            const char *p = "fetch failed: ";
            int o = 0;
            while (*p) msg[o++] = *p++;
            msg[o++] = (n <= 0 && n > -10) ? (char)('0' - n) : '?';
            msg[o] = 0;
            widget_set_text(browser_status, msg);
        }
        return;
    }
    body[n] = 0;
    if (browser_status) {
        char msg[64];
        const char *p = "HTTP ";
        int o = 0;
        while (*p) msg[o++] = *p++;
        msg[o++] = (char)('0' + (status / 100) % 10);
        msg[o++] = (char)('0' + (status / 10)  % 10);
        msg[o++] = (char)('0' + status % 10);
        const char *suf = "  ";
        while (*suf) msg[o++] = *suf++;
        char szs[12];
        int v = n, sn = 0;
        if (v == 0) szs[sn++] = '0';
        char tmp[12]; int tn = 0;
        while (v > 0) { tmp[tn++] = (char)('0' + v % 10); v /= 10; }
        while (tn > 0) szs[sn++] = tmp[--tn];
        szs[sn] = 0;
        for (int i = 0; szs[i]; i++) msg[o++] = szs[i];
        const char *suf2 = " bytes";
        while (*suf2) msg[o++] = *suf2++;
        msg[o] = 0;
        widget_set_text(browser_status, msg);
    }

    /* Pump the body (raw, including headers) into a separate
     * scrollable text window so user actually sees content. */
    if (!win_browser_view) {
        win_browser_view = window_create("Page", 360, 320);
    }
    win_browser_view->visible = 1;
    window_clear(win_browser_view);
    for (int i = 0; i < n; i++) {
        char c = (char)body[i];
        if (c == '\r') continue;
        window_putc(win_browser_view, c);
    }
    window_set_focus(win_browser_view);
}

static void build_browser_window(void) {
    win_browser = window_create_widget("Browser", 200, 160, 540, 78);
    window_add_label(win_browser, 10, 4, 520,
                     "URL (e.g. /index.json or http://10.0.2.2:8090/...):");
    browser_input = window_add_text_input(win_browser, 10, 22, 380,
                                          "/index.json", browser_go_cb);
    window_add_button(win_browser, 396, 18, 60, "Go", browser_go_cb);
    browser_status = window_add_label(win_browser, 10, 50, 520, "ready");
}
#endif /* LEGACY_BROWSER */

/* ---------------- Calendar ---------------- */

static void build_calendar_window(void) {
    win_calendar = window_create_widget("Calendar", 1000, 200, 280, 240);

    /* Render the current month from the same wall-clock the top bar
     * uses (boot baseline 2026-05-01). */
    static const uint8_t dim_table[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    extern uint64_t timer_ticks(void);
    extern uint32_t timer_hz(void);
    uint64_t s = timer_ticks() / (uint64_t)timer_hz() + (uint64_t)12 * 3600;
    uint32_t total_days = (uint32_t)(s / 86400);
    uint32_t year = 2026, month = 5, day = 1;
    while (total_days > 0) {
        uint32_t dim = dim_table[month - 1];
        if (month == 2 && (year % 4 == 0)) dim = 29;
        if (total_days >= dim - day + 1) {
            total_days -= (dim - day + 1);
            day = 1;
            month++;
            if (month > 12) { month = 1; year++; }
        } else {
            day += total_days;
            total_days = 0;
        }
    }

    char header[32];
    static const char *month_name[] = {
        "January","February","March","April","May","June",
        "July","August","September","October","November","December"
    };
    int o = 0;
    const char *mn = month_name[month - 1];
    while (*mn) header[o++] = *mn++;
    header[o++] = ' ';
    header[o++] = (char)('0' + (year/1000)%10);
    header[o++] = (char)('0' + (year/100)%10);
    header[o++] = (char)('0' + (year/10)%10);
    header[o++] = (char)('0' + year%10);
    header[o] = 0;
    window_add_label(win_calendar, 10, 4, 260, header);
    window_add_label(win_calendar, 10, 22, 260, "Mo Tu We Th Fr Sa Su");

    /* Zeller's congruence to find the day-of-week for the 1st of the
     * month. Returns 0=Sat..6=Fri; remap so 0=Mon..6=Sun. */
    uint32_t y = year, m = month;
    if (m < 3) { m += 12; y -= 1; }
    uint32_t k = y % 100, j = y / 100;
    int h = (1 + (13*(m+1))/5 + k + k/4 + j/4 + 5*j) % 7;
    int dow_mon0 = (h + 6) % 7;  /* 0=Sat→6, 1=Sun→0, ..., 6=Fri→5 */
    /* Above gives 0=Sat=>6, 1=Sun=>0, 2=Mon=>1... let me recompute:
     * Zeller: 0=Sat,1=Sun,2=Mon,3=Tue,4=Wed,5=Thu,6=Fri.
     * Want 0=Mon. So mon0 = (zeller + 5) % 7. */
    dow_mon0 = (h + 5) % 7;

    uint32_t dim = dim_table[month - 1];
    if (month == 2 && (year % 4 == 0)) dim = 29;

    char cell[3];
    int col = dow_mon0;
    int row = 0;
    for (uint32_t d = 1; d <= dim; d++) {
        cell[0] = d < 10 ? ' ' : (char)('0' + d / 10);
        cell[1] = (char)('0' + d % 10);
        cell[2] = 0;
        int x = 10 + col * 26;
        int y2 = 42 + row * 18;
        if (d == day) {
            /* Highlight today via a label that includes brackets. */
            char today[5]; today[0] = '['; today[1] = cell[0];
            today[2] = cell[1]; today[3] = ']'; today[4] = 0;
            window_add_label(win_calendar, x - 4, y2, 32, today);
        } else {
            window_add_label(win_calendar, x, y2, 24, cell);
        }
        col++;
        if (col >= 7) { col = 0; row++; }
    }
}

/* ---------------- Lazy launchers (called from the dock) ---------------- */

static void show_window(window_t *w) {
    if (!w) return;
    /* window_restore re-grows from the minimize/close animation
     * state and sets visible. Use it for both fresh-launch and
     * restoring-from-minimize so the dock click always animates. */
    window_restore(w);
    window_set_focus(w);
}

static void launch_terminal(void) {
    if (!win_terminal) {
        win_terminal = window_create("Terminal", 60, 80);
        window_clear(win_terminal);
        console_attach_window(win_terminal);
        console_puts("Hobby ARM OS\ntype 'help' for commands\n\n");
    }
    show_window(win_terminal);
}
static void launch_monitor(void) {
    if (!win_monitor) win_monitor = window_create("System Monitor", 700, 80);
    show_window(win_monitor);
}
static void launch_about(void) {
    if (!win_about) {
        win_about = window_create_widget("About Hobby ARM OS",
                                         500, 460, 320, 160);
        window_add_label (win_about, 12, 14, 280, "Hobby ARM OS  v0.7");
        window_add_label (win_about, 12, 34, 280, "AArch64 hand-rolled kernel");
        window_add_label (win_about, 12, 54, 280, "ramfb + virtio + Spleen 8x16");
        window_add_label (win_about, 12, 80, 280, "Click 'Close' to dismiss.");
        window_add_button(win_about, 200, 110, 90, "Close", about_close_cb);
    }
    show_window(win_about);
}
static void launch_calculator(void) {
    if (!win_calc) build_calculator_window();
    show_window(win_calc);
}
static void launch_notepad(void) {
    if (!win_notepad) build_notepad_window();
    show_window(win_notepad);
}
static void launch_store(void) {
    if (!win_store) build_store_window();
    show_window(win_store);
}
static void launch_browser(void) {
    if (!win_browser) win_browser = browser_window();
    show_window(win_browser);
}
static void launch_calendar(void) {
    if (!win_calendar) build_calendar_window();
    show_window(win_calendar);
}

/* ---------------- Task Manager ---------------- */

static void launch_tasks(void) {
    if (!win_tasks) {
        win_tasks = window_create("Task Manager", 220, 100);
    }
    show_window(win_tasks);
}

/* ---------------- Disk Manager ---------------- */

static void launch_disks(void) {
    if (!win_disks) {
        win_disks = window_create("Disks", 260, 140);
    }
    show_window(win_disks);
}

/* ---------------- Settings (Control Panel) ---------------- */

#include "wallpaper.h"

static void wp_prev_cb(window_t *w, widget_t *self) {
    (void)w; (void)self;
    int n = wallpaper_count();
    int v = (wallpaper_get() + n - 1) % n;
    wallpaper_set(v);
    if (settings_wp_label) {
        char msg[32];
        const char *p = "Wallpaper: ";
        int o = 0; while (*p) msg[o++] = *p++;
        const char *nm = wallpaper_name(v);
        while (*nm) msg[o++] = *nm++;
        msg[o] = 0;
        widget_set_text(settings_wp_label, msg);
    }
}

static void wp_next_cb(window_t *w, widget_t *self) {
    (void)w; (void)self;
    int n = wallpaper_count();
    int v = (wallpaper_get() + 1) % n;
    wallpaper_set(v);
    if (settings_wp_label) {
        char msg[32];
        const char *p = "Wallpaper: ";
        int o = 0; while (*p) msg[o++] = *p++;
        const char *nm = wallpaper_name(v);
        while (*nm) msg[o++] = *nm++;
        msg[o] = 0;
        widget_set_text(settings_wp_label, msg);
    }
}

static void build_settings_window(void) {
    win_settings = window_create_widget("Settings", 300, 200, 480, 240);

    /* wallpaper section */
    char init_wp[40];
    {
        const char *p = "Wallpaper: ";
        int o = 0; while (*p) init_wp[o++] = *p++;
        const char *nm = wallpaper_name(wallpaper_get());
        while (*nm) init_wp[o++] = *nm++;
        init_wp[o] = 0;
    }
    settings_wp_label = window_add_label(win_settings, 20, 12, 320, init_wp);
    window_add_button(win_settings, 340, 8,  60, "<", wp_prev_cb);
    window_add_button(win_settings, 408, 8,  60, ">", wp_next_cb);

    /* account section */
    char who[32];
    {
        const char *p = "Logged in as: ";
        int o = 0; while (*p) who[o++] = *p++;
        const char *u = account_current();
        while (*u) who[o++] = *u++;
        who[o] = 0;
    }
    settings_user_label = window_add_label(win_settings, 20, 60, 440, who);

    /* network section */
    char net[40];
    {
        uint32_t ip = net_my_ipv4();
        const char *p = "Network: 10.0.2.15  (qemu user net)";
        (void)ip;
        int o = 0; while (*p) net[o++] = *p++;
        net[o] = 0;
    }
    settings_net_label = window_add_label(win_settings, 20, 90, 440, net);

    window_add_label(win_settings, 20, 140, 440,
                     "Tip: 'wallpaper list' / 'wallpaper set N' from the");
    window_add_label(win_settings, 20, 158, 440,
                     "terminal also work.");
}

static void launch_settings(void) {
    if (!win_settings) build_settings_window();
    show_window(win_settings);
}

/* Public dispatcher used by desktop.c when a built-in dock icon is
 * clicked. Returns 0 on success, -1 if the name is unknown. */
int kernel_launch_builtin(const char *name) {
    if (strcmp(name, "Terminal")  == 0) { launch_terminal();  return 0; }
    if (strcmp(name, "Monitor")   == 0) { launch_monitor();   return 0; }
    if (strcmp(name, "About")     == 0) { launch_about();     return 0; }
    if (strcmp(name, "Calculator")== 0) { launch_calculator();return 0; }
    if (strcmp(name, "Notepad")   == 0) { launch_notepad();   return 0; }
    if (strcmp(name, "App Store") == 0) { launch_store();     return 0; }
    if (strcmp(name, "Browser")   == 0) { launch_browser();   return 0; }
    if (strcmp(name, "Calendar")  == 0) { launch_calendar();  return 0; }
    if (strcmp(name, "Tasks")     == 0) { launch_tasks();     return 0; }
    if (strcmp(name, "Disks")     == 0) { launch_disks();     return 0; }
    if (strcmp(name, "Settings")  == 0) { launch_settings();  return 0; }
    return -1;
}

static void render_monitor_window(void) {
    if (!win_monitor) return;
    window_clear(win_monitor);

    char buf[64];
    int running = 0, ready = 0;
    int total = task_state_count(&running, &ready);

    window_puts(win_monitor, "uptime    : ");
    format_uint(buf, sys_uptime_seconds());
    window_puts(win_monitor, buf);
    window_puts(win_monitor, " s\n");

    window_puts(win_monitor, "ticks     : ");
    format_uint(buf, timer_ticks());
    window_puts(win_monitor, buf);
    window_puts(win_monitor, "\n");

    window_puts(win_monitor, "tasks     : ");
    format_uint(buf, (uint64_t)total);
    window_puts(win_monitor, buf);
    window_puts(win_monitor, "\n\n");

#ifdef BOARD_HAS_GIC
    if (vmouse_present()) {
        int32_t mx = 0, my = 0;
        vmouse_position(&mx, &my);
        window_puts(win_monitor, "cursor : ");
        format_uint(buf, (uint64_t)mx);
        window_puts(win_monitor, buf);
        window_puts(win_monitor, ",");
        format_uint(buf, (uint64_t)my);
        window_puts(win_monitor, buf);
        window_puts(win_monitor, " btn ");
        format_uint(buf, (uint64_t)vmouse_buttons());
        window_puts(win_monitor, buf);
        window_puts(win_monitor, "\n");
        window_puts(win_monitor, "kbd evt: ");
        format_uint(buf, vinput_event_count());
        window_puts(win_monitor, buf);
        window_puts(win_monitor, "  irq: ");
        format_uint(buf, vinput_irq_count());
        window_puts(win_monitor, buf);
        window_puts(win_monitor, "\n");
        window_puts(win_monitor, "ms evt : ");
        format_uint(buf, vmouse_event_count());
        window_puts(win_monitor, buf);
        window_puts(win_monitor, "  irq: ");
        format_uint(buf, vmouse_irq_count());
        window_puts(win_monitor, buf);
        window_puts(win_monitor, "\n");
    }
#endif

    for (task_t *t = task_first(); t; t = t->next) {
        format_uint(buf, (uint64_t)t->id);
        window_puts(win_monitor, "  [");
        window_puts(win_monitor, buf);
        window_puts(win_monitor, "] ");
        window_puts(win_monitor, t->name);
        window_puts(win_monitor, "\n");
    }
}

static const char *task_state_str(task_state_t s) {
    switch (s) {
    case TASK_READY:   return "READY";
    case TASK_RUNNING: return "RUN";
    case TASK_DEAD:    return "DEAD";
    }
    return "?";
}

static void render_tasks_window(void) {
    if (!win_tasks || !win_tasks->visible) return;
    window_clear(win_tasks);
    window_puts(win_tasks, "Task Manager\n");
    window_puts(win_tasks, "----------------------------\n");
    char buf[24];
    int n = 0;
    for (task_t *t = task_first(); t; t = t->next) {
        format_uint(buf, (uint64_t)t->id);
        window_puts(win_tasks, "[");
        window_puts(win_tasks, buf);
        window_puts(win_tasks, "] ");
        /* pad/truncate name to 14 chars */
        int j = 0;
        for (; t->name[j] && j < 14; j++) window_putc(win_tasks, t->name[j]);
        for (; j < 14; j++) window_putc(win_tasks, ' ');
        window_puts(win_tasks, "  ");
        window_puts(win_tasks, task_state_str(t->state));
        window_puts(win_tasks, "\n");
        n++;
    }
    window_puts(win_tasks, "\nuptime: ");
    format_uint(buf, sys_uptime_seconds());
    window_puts(win_tasks, buf);
    window_puts(win_tasks, " s\n");
}

static void render_disks_window(void) {
    if (!win_disks || !win_disks->visible) return;
    window_clear(win_disks);
    window_puts(win_disks, "Disks\n----------------------------\n");
    /* fs files */
    window_puts(win_disks, "[fs]\n");
    char buf[24];
    int total_bytes = 0;
    int file_count = 0;
    for (int i = 0; i < FS_MAX_FILES; i++) {
        fs_file_t *f = fs_at(i);
        if (!f) continue;
        int j = 0;
        for (; f->name[j] && j < 16; j++) window_putc(win_disks, f->name[j]);
        for (; j < 16; j++) window_putc(win_disks, ' ');
        format_uint(buf, (uint64_t)f->size);
        window_puts(win_disks, "  ");
        window_puts(win_disks, buf);
        window_puts(win_disks, " B\n");
        total_bytes += (int)f->size;
        file_count++;
    }
    window_puts(win_disks, " total ");
    format_uint(buf, (uint64_t)file_count);
    window_puts(win_disks, buf);
    window_puts(win_disks, " files, ");
    format_uint(buf, (uint64_t)total_bytes);
    window_puts(win_disks, buf);
    window_puts(win_disks, " B\n\n");

#ifdef BOARD_HAS_GIC
    /* virtio-blk */
    window_puts(win_disks, "[virtio-blk]\n");
    if (vblk_present()) {
        format_uint(buf, (uint64_t)vblk_capacity_sectors());
        window_puts(win_disks, " sectors  : ");
        window_puts(win_disks, buf);
        window_puts(win_disks, "\n size     : ");
        format_uint(buf, (uint64_t)vblk_capacity_sectors() * 512);
        window_puts(win_disks, buf);
        window_puts(win_disks, " B\n");
    } else {
        window_puts(win_disks, " (not present)\n");
    }
#endif
}

static void render_settings_window(void) {
    /* Refresh dynamic labels (account / wallpaper) so toggling state
     * outside the window stays in sync. */
    if (!win_settings || !win_settings->visible) return;
    if (settings_user_label) {
        char who[32];
        const char *p = "Logged in as: ";
        int o = 0; while (*p) who[o++] = *p++;
        const char *u = account_current();
        while (*u && o < 30) who[o++] = *u++;
        who[o] = 0;
        widget_set_text(settings_user_label, who);
    }
    if (settings_wp_label) {
        char msg[32];
        const char *p = "Wallpaper: ";
        int o = 0; while (*p) msg[o++] = *p++;
        const char *nm = wallpaper_name(wallpaper_get());
        while (*nm && o < 30) msg[o++] = *nm++;
        msg[o] = 0;
        widget_set_text(settings_wp_label, msg);
    }
}

extern int     gui_is_taken(void);
extern task_t *gui_owner(void);
extern void    gui_clear(void);
extern int     gui_close_x(void);
extern int     gui_close_y(void);
extern int     gui_close_w(void);
extern int     gui_close_h(void);

static void status_thread(void *arg) {
    (void)arg;
    int prev_gui_btn = 0;
    for (;;) {
        ticker_beats++;
        if (gui_is_taken()) {
            /* A user-mode GUI app owns the framebuffer. We don't
             * paint the desktop on top of it, but we DO watch for a
             * click on the kernel-overlaid Close button so the user
             * always has a way to bail out. */
            if (vmouse_present()) {
                int btn  = vmouse_buttons();
                int press = (btn & 1) && !(prev_gui_btn & 1);
                prev_gui_btn = btn;
                if (press) {
                    int32_t mx = 0, my = 0;
                    vmouse_position(&mx, &my);
                    if (mx >= gui_close_x() &&
                        mx <  gui_close_x() + gui_close_w() &&
                        my >= gui_close_y() &&
                        my <  gui_close_y() + gui_close_h()) {
                        task_t *t = gui_owner();
                        if (t) task_kill(t);
                        gui_clear();
                    }
                }
            }
            task_yield();
            __asm__ volatile("wfi");
            continue;
        }
        prev_gui_btn = 0;
        render_monitor_window();
        render_tasks_window();
        render_disks_window();
        render_settings_window();
#ifdef BOARD_HAS_RAMFB
        if (vmouse_present()) {
            int32_t mx = 0, my = 0;
            vmouse_position(&mx, &my);
            window_handle_pointer(mx, my, vmouse_buttons());
        }
        window_compose();
#endif
        /* Single yield + wfi keeps the render loop tight: at 250 Hz
         * timer ticks the compositor refreshes every ~4 ms, which is
         * fast enough to feel responsive without burning CPU. */
        task_yield();
        __asm__ volatile("wfi");
    }
}

static void ticker_thread(void *arg) {
    (void)arg;
    for (;;) {
        ticker_beats++;
        /* Cooperative scheduler: we MUST yield before sleeping or
         * the rest of the task list never runs. */
        task_yield();
        __asm__ volatile("wfi");
    }
}

static void idle_thread(void *arg) {
    (void)arg;
    for (;;) {
        __asm__ volatile("wfi");
        if (task_resched_pending()) {
            task_yield();
        }
    }
}

uint64_t kernel_ticker_beats(void) {
    return ticker_beats;
}
#endif

void kernel_main(void) {
    heap_init();
    fs_init();
    accounts_init();
    pkg_init();

#ifdef BOARD_HAS_GIC
    mmu_init();
    exceptions_init();
    gic_init();
#endif

    uart_init();

#ifdef BOARD_HAS_GIC
    timer_init(250);
    vinput_init();
    vmouse_init();
    vblk_init();
    vnet_init();
#endif

#ifdef BOARD_HAS_RAMFB
    if (fb_init() == 0) {
        fb_console_init(BG_COLOR, FG_COLOR);
        window_init();
        desktop_init();
        boot_splash_init();
        /* No windows are pre-created at boot. Each dock icon lazily
         * builds its window the first time the user clicks it, so a
         * fresh boot lands on a clean desktop. */
    }
#endif

#ifdef BOARD_HAS_GIC
    irq_enable();
    task_init();
    smp_bring_up();
#endif

    post();

    /* No terminal at boot -- it appears when the user clicks the
     * Terminal dock icon (launch_terminal attaches the console then). */

#if defined(BOARD_HAS_RAMFB) && defined(BOARD_HAS_GIC)
    /* Splash off, login on. */
    boot_splash_done();
    /* Gate the desktop behind a graphical login. Returns only after
     * accounts.c accepts a username/password. */
    login_run();
#endif

#ifdef BOARD_HAS_GIC
    task_spawn("idle",   idle_thread,   NULL);
    task_spawn("ticker", ticker_thread, NULL);
    task_spawn("status", status_thread, NULL);
#endif

    shell_run();

    for (;;) {
        __asm__ volatile("wfi");
    }
}
