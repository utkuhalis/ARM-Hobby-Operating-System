#include "uart.h"
#include "console.h"
#include "shell.h"
#include "fs.h"
#include "sysinfo.h"

#ifdef BOARD_HAS_RAMFB
#include "fb.h"
#endif

#define BG_COLOR    0x00101830u
#define TEXT_COLOR  0x00f0f0f0u
#define DIM_COLOR   0x00606878u

void kernel_main(void) {
    uart_init();
    fs_init();

#ifdef BOARD_HAS_RAMFB
    if (fb_init() == 0) {
        const char *line = "Hello, World";
        const uint32_t scale = 6;
        const uint32_t glyph_count = 12;
        const uint32_t text_w = 8 * scale * glyph_count;
        const uint32_t text_h = 8 * scale;

        fb_clear(BG_COLOR);
        fb_draw_string((800 - text_w) / 2,
                       (600 - text_h) / 2,
                       line, TEXT_COLOR, scale);
    }
#endif

    console_puts("\nHobby ARM OS v0.2\n");
    console_printf("booted on %s, %s, EL%u\n",
                   sys_board_name(),
                   sys_cpu_name(sys_read_midr()),
                   sys_read_currentel());
    console_puts("type 'help' for commands\n\n");

    shell_run();

    for (;;) {
        __asm__ volatile("wfi");
    }
}
