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
#include "virtio_blk.h"
#include "virtio_net.h"
#include "mmu.h"
#include "task.h"
#endif

#ifdef BOARD_HAS_RAMFB
#include "fb.h"
#include "fb_console.h"
#endif

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];
extern uint8_t _stack_top[];

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
        console_printf("[ OK ] Kbd      virtio-input @ IRQ %d\n", kbd_irq);
    } else {
        console_puts("[ -- ] Kbd      no virtio-input found\n");
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

static void status_thread(void *arg) {
    (void)arg;
    static char line[120];
    for (;;) {
        uint64_t s = sys_uptime_seconds();
        uint64_t ticks = timer_ticks();
        int running = 0, ready = 0;
        int total = task_state_count(&running, &ready);

        char up[24], tk[24], to[8], tr[8];
        format_uint(up, s);
        format_uint(tk, ticks);
        format_uint(to, (uint64_t)total);
        format_uint(tr, (uint64_t)ready);

        char *p = line;
        const char *parts[] = {
            "Hobby ARM OS  | up ", up, "s  | ticks ", tk,
            "  | tasks ", to, " (", tr, " ready)  | beats ", NULL
        };
        for (int i = 0; parts[i]; i++) {
            const char *s2 = parts[i];
            while (*s2) *p++ = *s2++;
        }
        char beats[24];
        format_uint(beats, ticker_beats);
        const char *s3 = beats;
        while (*s3) *p++ = *s3++;
        *p = '\0';

#ifdef BOARD_HAS_RAMFB
        fb_console_status_set(line);
#endif
        ticker_beats++;
        for (int i = 0; i < 5; i++) task_yield();
        __asm__ volatile("wfi");
    }
}

static void ticker_thread(void *arg) {
    (void)arg;
    for (;;) {
        ticker_beats++;
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
    timer_init(100);
    vinput_init();
    vblk_init();
    vnet_init();
#endif

#ifdef BOARD_HAS_RAMFB
    if (fb_init() == 0) {
        fb_console_init(BG_COLOR, FG_COLOR);
    }
#endif

#ifdef BOARD_HAS_GIC
    irq_enable();
    task_init();
#endif

    post();

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
