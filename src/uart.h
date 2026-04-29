#ifndef HOBBY_OS_UART_H
#define HOBBY_OS_UART_H

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc(void);
int  uart_has_input(void);

#endif
