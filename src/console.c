#include <stdarg.h>
#include "console.h"
#include "uart.h"
#include "str.h"

void console_putc(char c) {
    uart_putc(c);
}

void console_puts(const char *s) {
    uart_puts(s);
}

void console_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf_(console_putc, fmt, ap);
    va_end(ap);
}

int console_readline(char *buf, uint32_t max) {
    if (max == 0) return 0;
    uint32_t pos = 0;
    for (;;) {
        char c = uart_getc();
        if (c == '\r' || c == '\n') {
            buf[pos] = '\0';
            console_puts("\n");
            return (int)pos;
        }
        if (c == '\b' || c == 0x7f) {
            if (pos > 0) {
                pos--;
                console_puts("\b \b");
            }
            continue;
        }
        if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7f) {
            continue;
        }
        if (pos + 1 < max) {
            buf[pos++] = c;
            console_putc(c);
        }
    }
}
