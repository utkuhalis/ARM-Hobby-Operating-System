#ifndef HOBBY_OS_VIRTIO_MOUSE_H
#define HOBBY_OS_VIRTIO_MOUSE_H

#include <stdint.h>

int  vmouse_init(void);
int  vmouse_present(void);
int  vmouse_irq_number(void);
void vmouse_irq(void);

void vmouse_position(int32_t *x, int32_t *y);
int  vmouse_buttons(void);

/* Synthetic input from the keyboard fallback (arrow keys + Esc). */
void vmouse_inject_move(int dx, int dy);
void vmouse_inject_button(int left_down);

uint64_t vmouse_event_count(void);
uint64_t vmouse_irq_count(void);

#endif
