#ifndef HOBBY_OS_VIRTIO_MOUSE_H
#define HOBBY_OS_VIRTIO_MOUSE_H

#include <stdint.h>

int  vmouse_init(void);
int  vmouse_present(void);
int  vmouse_irq_number(void);
void vmouse_irq(void);

void vmouse_position(int32_t *x, int32_t *y);
int  vmouse_buttons(void);

#endif
