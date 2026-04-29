#include "uart.h"

#ifdef BOARD_HAS_RAMFB
#include "fb.h"
#endif

#define BG_COLOR    0x00101830u
#define TEXT_COLOR  0x00f0f0f0u

void kernel_main(void) {
    uart_init();
    uart_puts("Hello, World\n");
    uart_puts("Hobby ARM OS booted\n");

#ifdef BOARD_HAS_RAMFB
    if (fb_init() == 0) {
        const char *message = "Hello, World";
        const uint32_t glyph_count = 12;
        const uint32_t scale = 6;
        const uint32_t text_w = 8 * scale * glyph_count;
        const uint32_t text_h = 8 * scale;

        fb_clear(BG_COLOR);
        fb_draw_string((FB_WIDTH  - text_w) / 2,
                       (FB_HEIGHT - text_h) / 2,
                       message, TEXT_COLOR, scale);

        uart_puts("framebuffer ready\n");
    } else {
        uart_puts("framebuffer init failed\n");
    }
#endif

    for (;;) {
        __asm__ volatile("wfi");
    }
}
