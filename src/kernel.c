#include "uart.h"

void kernel_main(void) {
    uart_init();
    uart_puts("Hello, world\n");
    uart_puts("Hobby ARM OS booted\n");

    for (;;) {
        __asm__ volatile("wfi");
    }
}
