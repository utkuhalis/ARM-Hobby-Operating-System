#ifndef HOBBY_OS_FB_CONSOLE_H
#define HOBBY_OS_FB_CONSOLE_H

#include <stdint.h>

void fb_console_init(uint32_t bg, uint32_t fg);
void fb_console_putc(char c);
void fb_console_set_fg(uint32_t fg);
void fb_console_status_set(const char *line);

#endif
