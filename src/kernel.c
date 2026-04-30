#include <stdint.h>
#include "uart.h"
#include "console.h"
#include "shell.h"
#include "fs.h"
#include "sysinfo.h"
#include "heap.h"
#include "accounts.h"
#include "pkgmgr.h"
#ifdef BOARD_HAS_GIC
#include "exceptions.h"
#include "gic.h"
#include "timer.h"
#include "virtio_input.h"
#include "virtio_mouse.h"
#include "virtio_blk.h"
#include "virtio_net.h"
#include "mmu.h"
#include "task.h"
#endif

#ifdef BOARD_HAS_RAMFB
#include "fb.h"
#include "fb_console.h"
#include "window.h"
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

    console_puts("\n");
    console_puts("==========================================\n");
    console_printf(" HobbyBIOS v0.5    board: %s\n", sys_board_name());
    console_puts(" (c) 2026  Hobby ARM Operating System\n");
    console_puts("==========================================\n\n");

    delay_ms(120);
    console_puts("Power-on self test\n");
    console_puts("------------------\n");

    delay_ms(150);
    console_printf("[ OK ] CPU      %s  (EL%u)\n", sys_cpu_name(midr), el);
    console_printf("                MIDR  0x%lx\n", midr);
    console_printf("                MPIDR 0x%lx\n", sys_read_mpidr());
    console_printf("                timer %u.%u MHz\n", mhz_i, mhz_d);

    delay_ms(150);
    console_printf("[ OK ] Memory   256 MiB\n");
    console_printf("                kernel 0x%08x..0x%08x\n", kstart, kend);
    console_printf("                stack  0x%08x..0x%08x\n", kend, stop);

    delay_ms(150);
#ifdef BOARD_HAS_RAMFB
    console_puts("[ OK ] Display  ramfb 800x600 XRGB8888\n");
#else
    console_puts("[ -- ] Display  serial only\n");
#endif

    delay_ms(150);
    console_puts("[ OK ] Storage  RAM fs 16 x 4 KiB (volatile)\n");

    delay_ms(150);
    console_printf("[ OK ] Console  PL011 UART @ 0x%lx\n", (uint64_t)UART_BASE);

    delay_ms(150);
#ifdef BOARD_HAS_GIC
    console_puts("[ OK ] MMU      4 KiB granule, 39-bit VA, I+D caches\n");

    delay_ms(150);
    console_puts("[ OK ] Heap     kalloc/kfree, 2 MiB pool\n");

    delay_ms(150);
    console_printf("[ OK ] Sched    cooperative, %u Hz tick\n", timer_hz());

    delay_ms(150);
    console_puts("[ OK ] IRQ      GIC v2 (distributor + CPU iface)\n");

    delay_ms(150);
    int kbd_irq = vinput_irq_number();
    if (kbd_irq >= 0) {
        console_printf("[ OK ] Kbd      virtio-input @ IRQ %d  (mmio v%u)\n",
                       kbd_irq, vinput_mmio_version());
    } else {
        console_puts("[ -- ] Kbd      no virtio-input found\n");
    }

    delay_ms(150);
    int mouse_irq = vmouse_irq_number();
    if (mouse_irq >= 0) {
        console_printf("[ OK ] Mouse    virtio-mouse @ IRQ %d\n", mouse_irq);
    } else {
        console_puts("[ -- ] Mouse    no virtio-mouse found\n");
    }

    delay_ms(150);
    if (vblk_present()) {
        console_printf("[ OK ] Block    virtio-blk @ IRQ %d  %lu sectors\n",
                       vblk_irq_number(),
                       (unsigned long)vblk_capacity_sectors());
    } else {
        console_puts("[ -- ] Block    no virtio-blk found\n");
    }

    delay_ms(150);
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

    delay_ms(150);
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
static widget_t *calc_display;

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

    win_store = window_create_widget("App Store", 16, 300, win_w, win_h);

    window_add_label(win_store, 10, 4, win_w - 20,
                     "Install / remove built-in packages");

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

static void build_calculator_window(void) {
    win_calc = window_create_widget("Calculator", 600, 280, 180, 240);

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

static void status_thread(void *arg) {
    (void)arg;
    for (;;) {
        ticker_beats++;
        render_monitor_window();
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
        win_terminal = window_create("Terminal", 16, 16);
        win_monitor  = window_create("System Monitor",
                                     16 + win_terminal->w + 16, 16);

        /* About window: a real widget-based dialog with labels + a button */
        win_about = window_create_widget(
            "About Hobby ARM OS",
            220, 280, 320, 160);
        window_add_label (win_about, 12, 14, 280, "Hobby ARM OS  v0.6");
        window_add_label (win_about, 12, 34, 280, "AArch64 hand-rolled kernel");
        window_add_label (win_about, 12, 54, 280, "ramfb + virtio + Spleen 8x16");
        window_add_label (win_about, 12, 80, 280, "Click 'Close' to dismiss.");
        window_add_button(win_about, 200, 110, 90, "Close", about_close_cb);

        build_calculator_window();
        build_store_window();
    }
#endif

#ifdef BOARD_HAS_GIC
    irq_enable();
    task_init();
    smp_bring_up();
#endif

    post();

#ifdef BOARD_HAS_RAMFB
    /* once boot is done, redirect console output into the Terminal
     * window so the shell prompt and user output land there instead
     * of dribbling out to the framebuffer-text console (which the
     * compositor wipes every refresh anyway). */
    if (win_terminal) {
        window_clear(win_terminal);
        console_attach_window(win_terminal);
        console_puts("Hobby ARM OS\n");
        console_puts("type 'help' for commands\n\n");
    }
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
