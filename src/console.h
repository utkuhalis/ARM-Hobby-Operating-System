#ifndef HOBBY_OS_CONSOLE_H
#define HOBBY_OS_CONSOLE_H

#include <stdint.h>

void console_putc(char c);
void console_puts(const char *s);
void console_printf(const char *fmt, ...);
int  console_readline(char *buf, uint32_t max);

#endif
