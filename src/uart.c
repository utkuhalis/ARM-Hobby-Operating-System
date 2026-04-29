#include <stdint.h>
#include "uart.h"

#ifdef BOARD_HAS_GIC
#include "gic.h"
#endif

#ifndef UART_BASE
#error "UART_BASE must be defined by the board header"
#endif

#define IRQ_UART_PL011 33

#define UART_DR   (*(volatile uint32_t *)(UART_BASE + 0x00))
#define UART_FR   (*(volatile uint32_t *)(UART_BASE + 0x18))
#define UART_LCRH (*(volatile uint32_t *)(UART_BASE + 0x2C))
#define UART_CR   (*(volatile uint32_t *)(UART_BASE + 0x30))
#define UART_IFLS (*(volatile uint32_t *)(UART_BASE + 0x34))
#define UART_IMSC (*(volatile uint32_t *)(UART_BASE + 0x38))
#define UART_ICR  (*(volatile uint32_t *)(UART_BASE + 0x44))

#define FR_RXFE   (1u << 4)
#define FR_TXFF   (1u << 5)

#define LCRH_FEN     (1u << 4)
#define LCRH_WLEN_8  (3u << 5)

#define IMSC_RXIM (1u << 4)
#define IMSC_RTIM (1u << 6)
#define ICR_ALL   0x7ffu

#ifdef BOARD_HAS_GIC

#define RX_BUF_SIZE 256

static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_head;
static volatile uint32_t rx_tail;

static void rx_push(uint8_t c) {
    uint32_t head = rx_head;
    uint32_t next = (head + 1) % RX_BUF_SIZE;
    if (next == rx_tail) {
        return;
    }
    rx_buf[head] = c;
    rx_head = next;
}

static int rx_pop(uint8_t *out) {
    uint32_t tail = rx_tail;
    if (tail == rx_head) return 0;
    *out = rx_buf[tail];
    rx_tail = (tail + 1) % RX_BUF_SIZE;
    return 1;
}

#endif /* BOARD_HAS_GIC */

void uart_init(void) {
#ifdef BOARD_HAS_GIC
    UART_ICR  = ICR_ALL;
    UART_LCRH = LCRH_FEN | LCRH_WLEN_8;
    UART_IFLS = 0;
    UART_IMSC = IMSC_RXIM | IMSC_RTIM;
    gic_enable_irq(IRQ_UART_PL011);
#else
    /* polling-only UART */
#endif
}

void uart_putc(char c) {
    while (UART_FR & FR_TXFF) {
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

#ifdef BOARD_HAS_GIC
extern void task_yield(void);
#endif

char uart_getc(void) {
#ifdef BOARD_HAS_GIC
    uint8_t c;
    for (;;) {
        if (rx_pop(&c)) return (char)c;
        task_yield();
        if (rx_pop(&c)) return (char)c;
        __asm__ volatile("wfi");
    }
#else
    while (UART_FR & FR_RXFE) {
    }
    return (char)(UART_DR & 0xffu);
#endif
}

int uart_has_input(void) {
#ifdef BOARD_HAS_GIC
    return rx_head != rx_tail;
#else
    return !(UART_FR & FR_RXFE);
#endif
}

void uart_irq(void) {
#ifdef BOARD_HAS_GIC
    while (!(UART_FR & FR_RXFE)) {
        rx_push((uint8_t)(UART_DR & 0xffu));
    }
    UART_ICR = ICR_ALL;
#endif
}
