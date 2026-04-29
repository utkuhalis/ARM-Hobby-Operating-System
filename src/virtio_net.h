#ifndef HOBBY_OS_VIRTIO_NET_H
#define HOBBY_OS_VIRTIO_NET_H

#include <stdint.h>

int  vnet_init(void);
int  vnet_present(void);
int  vnet_irq_number(void);
const uint8_t *vnet_mac(void);

#endif
