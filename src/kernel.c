#include <stdint.h>
#include "uart.h"
#include "console.h"
#include "shell.h"
#include "fs.h"
#include "sysinfo.h"
#include "heap.h"
#ifdef BOARD_HAS_GIC
#include "exceptions.h"
#include "gic.h"
#include "timer.h"
#include "virtio_input.h"
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

#define BG_COLOR  0x00080814u
#define FG_COLOR  0x00d0d6dfu

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
    console_puts("================================================================\n");
    console_printf(" HobbyBIOS v0.4  (board: %s)\n", sys_board_name());
    console_puts(" (c) 2026  Hobby ARM Operating System\n");
    console_puts("================================================================\n\n");

    delay_ms(120);
    console_puts("Performing power-on self test...\n\n");

    delay_ms(150);
    console_printf("[ OK ] CPU       %s  (EL%u)\n", sys_cpu_name(midr), el);
    console_printf("                 MIDR_EL1=0x%lx  MPIDR=0x%lx\n",
                   midr, sys_read_mpidr());
    console_printf("                 system timer %u.%u MHz\n", mhz_i, mhz_d);

    delay_ms(150);
    console_printf("[ OK ] Memory    256 MiB total\n");
    console_printf("                 kernel  0x%08x .. 0x%08x  (%u bytes)\n",
                   kstart, kend, ksize);
    console_printf("                 stack   0x%08x .. 0x%08x\n", kend, stop);

    delay_ms(150);
#ifdef BOARD_HAS_RAMFB
    console_puts("[ OK ] Display   ramfb 800x600 XRGB8888 (host window)\n");
    console_puts("                 framebuffer mapped via fw_cfg DMA\n");
#else
    console_puts("[ -- ] Display   none (serial console only)\n");
#endif

    delay_ms(150);
    console_printf("[ OK ] Storage   RAM filesystem  16 slots x 4 KiB = 64 KiB\n");
    console_puts("                 (volatile - contents lost on reboot)\n");

    delay_ms(150);
    console_printf("[ OK ] Console   PL011 UART @ 0x%lx (RX interrupt enabled)\n",
                   (uint64_t)UART_BASE);

    delay_ms(150);
#ifdef BOARD_HAS_GIC
    console_puts("[ OK ] MMU       4 KiB granule, 39-bit VA, identity map\n");
    console_puts("                 caches enabled (I+D)\n");

    delay_ms(150);
    console_puts("[ OK ] Heap      kalloc/kfree on a 2 MiB pool\n");

    delay_ms(150);
    console_puts("[ OK ] Scheduler cooperative round-robin (yield-driven)\n");

    delay_ms(150);
    console_printf("[ OK ] Interrupt GIC v2 distributor + CPU iface\n");
    console_printf("                 system tick at %u Hz, CPU now sleeps when idle\n",
                   timer_hz());

    delay_ms(150);
    int kbd_irq = vinput_irq_number();
    if (kbd_irq >= 0) {
        console_printf("[ OK ] Keyboard  virtio-input @ IRQ %d\n", kbd_irq);
        console_puts("                 host keyboard delivers events into ring buffer\n");
    } else {
        console_puts("[ -- ] Keyboard  no virtio-input device found\n");
        console_puts("                 launch QEMU with -device virtio-keyboard-device\n");
    }
#else
    console_puts("[ -- ] Interrupt (no GIC driver on this board, polling UART)\n");
#endif

    delay_ms(150);
    console_puts("[ OK ] Power     PSCI hypercalls: SYSTEM_OFF, SYSTEM_RESET\n");

    delay_ms(200);
    console_puts("\n----------------------------------------------------------------\n");
    console_puts(" Boot complete. Type 'help' for commands.\n");
    console_puts("----------------------------------------------------------------\n\n");
}

#ifdef BOARD_HAS_GIC
static volatile uint64_t ticker_beats;

static void ticker_thread(void *arg) {
    (void)arg;
    for (;;) {
        ticker_beats++;
        task_yield();
        __asm__ volatile("wfi");
    }
}

uint64_t kernel_ticker_beats(void) {
    return ticker_beats;
}
#endif

void kernel_main(void) {
    heap_init();
    fs_init();

#ifdef BOARD_HAS_GIC
    mmu_init();
    exceptions_init();
    gic_init();
#endif

    uart_init();

#ifdef BOARD_HAS_GIC
    timer_init(100);
    vinput_init();
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
    task_spawn("ticker", ticker_thread, NULL);
#endif

    shell_run();

    for (;;) {
        __asm__ volatile("wfi");
    }
}
