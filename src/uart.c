#include <stdint.h>
#include "uart.h"

#ifndef UART_BASE
#error "UART_BASE must be defined by the board header"
#endif

#define UART_DR (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_FR (*(volatile uint32_t *)(UART_BASE + 0x18))

#define UART_FR_RXFE (1u << 4)
#define UART_FR_TXFF (1u << 5)

void uart_init(void) {
}

void uart_putc(char c) {
    while (UART_FR & UART_FR_TXFF) {
    }
    UART_DR = (uint32_t)(unsigned char)c;
}

void uart_puts(const char *s) {
    for (; *s != '\0'; ++s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s);
    }
}

char uart_getc(void) {
    while (UART_FR & UART_FR_RXFE) {
    }
    return (char)(UART_DR & 0xffu);
}

int uart_has_input(void) {
    return (UART_FR & UART_FR_RXFE) ? 0 : 1;
}
